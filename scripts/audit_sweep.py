#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Audit a completed sweep against the declared experimental contract.

Inputs:
    A sweep directory and a sweep-design YAML file.
Outputs:
    A human-readable PASS report, or a nonzero exit describing violated invariants.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import yaml


ROOT = Path(__file__).resolve().parents[1]
PAPER_CLASSES = ("DIAG_TOTAL", "ALERT_TOTAL", "FAULT_TOTAL")
TRAFFIC_CONTRACT_FIELDS = (
    ("sim", "duration_s"),
    ("rng", "run"),
    ("diag", "start_s"),
    ("diag", "stop_s"),
    ("diag", "payload_B"),
    ("diag", "iat", "model"),
    ("diag", "iat", "period_s"),
    ("alert", "start_s"),
    ("alert", "stop_s"),
    ("alert", "payload_B"),
    ("alert", "iat", "model"),
    ("alert", "iat", "mu"),
    ("alert", "iat", "sigma"),
    ("fault", "start_s"),
    ("fault", "stop_s"),
    ("fault", "payload_B"),
    ("fault", "packets_per_full_refresh"),
    ("fault", "iat", "model"),
    ("fault", "iat", "mu"),
    ("fault", "iat", "sigma"),
)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_yaml(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def canonical_load(value: str | float | int) -> float:
    return round(float(value), 9)


def nested_value(mapping: dict[str, Any], path: tuple[str, ...]) -> Any:
    value: Any = mapping
    for key in path:
        value = value[key]
    return value


def audit(sweep_dir: Path, design_path: Path) -> list[str]:
    design = load_yaml(design_path)
    traffic_contract = load_yaml(ROOT / "config" / "paper-traffic.yaml")
    records = read_csv(sweep_dir / "run_index.csv")
    expected = {
        (scenario, canonical_load(load), int(seed))
        for scenario in design["scenarios"]
        for load in design["ev_background_loads_mbps"]
        for seed in design["seeds"]
    }
    actual = [
        (row["scenario"], canonical_load(row["ev_background_load_mbps"]), int(row["seed"]))
        for row in records
    ]
    counts = Counter(actual)
    duplicates = sorted(key for key, count in counts.items() if count != 1)
    missing = sorted(expected - set(actual))
    unexpected = sorted(set(actual) - expected)
    failed = [row for row in records if row["status"] != "ok"]
    errors: list[str] = []
    if duplicates:
        errors.append(f"duplicate matrix cells: {duplicates[:5]}")
    if missing:
        errors.append(f"missing matrix cells: {missing[:5]}")
    if unexpected:
        errors.append(f"unexpected matrix cells: {unexpected[:5]}")
    if failed:
        errors.append(f"non-OK runs: {len(failed)}")

    by_pair: dict[tuple[float, int], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in records:
        by_pair[(canonical_load(row["ev_background_load_mbps"]), int(row["seed"]))][
            row["scenario"]
        ] = row
    for key, pair in sorted(by_pair.items()):
        if set(pair) != {"baseline", "slicing"}:
            errors.append(f"incomplete policy pair at load/seed {key}")
            continue
        if Path(pair["baseline"]["traffic_config"]).resolve() != Path(
            pair["slicing"]["traffic_config"]
        ).resolve():
            errors.append(f"unpaired traffic realization at load/seed {key}")

    traffic_cache: dict[Path, dict[str, Any]] = {}
    for record in records:
        traffic_path = Path(record["traffic_config"]).resolve()
        if traffic_path not in traffic_cache:
            traffic_cache[traffic_path] = load_yaml(traffic_path)
        traffic = traffic_cache[traffic_path]
        indexed_seed = int(record["seed"])
        if int(traffic["rng"]["seed"]) != indexed_seed:
            errors.append(
                f"{traffic_path}: rng.seed={traffic['rng']['seed']} != indexed seed {indexed_seed}"
            )
        for field_path in TRAFFIC_CONTRACT_FIELDS:
            actual_value = nested_value(traffic, field_path)
            expected_value = nested_value(traffic_contract, field_path)
            if actual_value != expected_value:
                errors.append(
                    f"{traffic_path}: {'.'.join(field_path)}={actual_value!r} "
                    f"!= declared contract {expected_value!r}"
                )

    flow_rows_checked = 0
    for record in records:
        if record["status"] != "ok":
            continue
        flow_path = Path(record["run_dir"]) / "maintenance_nr_flow_summary.csv"
        rows = {row["traffic_class"].upper(): row for row in read_csv(flow_path)}
        for traffic_class in PAPER_CLASSES:
            if traffic_class not in rows:
                errors.append(f"{flow_path} missing {traffic_class}")
                continue
            row = rows[traffic_class]
            tx = int(row["tx_packets"])
            rx = int(row["rx_packets"])
            lost = int(row["lost_packets"])
            if tx - rx != lost:
                errors.append(f"{flow_path} {traffic_class}: tx-rx != lost")
            expected_mapping = {
                "baseline": {
                    "DIAG_TOTAL": ("1", "DEFAULT_QOS_FLOW"),
                    "ALERT_TOTAL": ("1", "DEFAULT_QOS_FLOW"),
                    "FAULT_TOTAL": ("1", "DEFAULT_QOS_FLOW"),
                },
                "slicing": {
                    "DIAG_TOTAL": ("1", "DEFAULT_QOS_FLOW"),
                    "ALERT_TOTAL": ("3", "GBR_GAMING"),
                    "FAULT_TOTAL": ("4", "DGBR_DISCRETE_AUT_LARGE"),
                },
            }[record["scenario"]][traffic_class]
            if (row["qfi"], row["five_qi"]) != expected_mapping:
                errors.append(
                    f"{flow_path} {traffic_class}: mapping {(row['qfi'], row['five_qi'])} "
                    f"!= {expected_mapping}"
                )
            flow_rows_checked += 1

    if errors:
        raise RuntimeError("\n".join(errors[:20]))

    return [
        f"matrix_cells={len(records)}",
        f"paired_cells={len(by_pair)}",
        f"flow_rows_checked={flow_rows_checked}",
        f"traffic_configs_checked={len(traffic_cache)}",
        f"seeds={len(design['seeds'])}",
        f"loads={len(design['ev_background_loads_mbps'])}",
    ]


def write_audit_report(path: Path, facts: list[str]) -> None:
    labels = {
        "matrix_cells": "Complete scenario × load × seed matrix cells",
        "paired_cells": "Matched load × seed policy pairs",
        "flow_rows_checked": "Traffic-class accounting rows checked",
        "traffic_configs_checked": "Seed-specific traffic configurations checked",
        "seeds": "Independent seed values",
        "loads": "Declared background-load points",
    }
    rows = [fact.split("=", 1) for fact in facts]
    content = [
        "# Sweep Integrity Audit",
        "",
        "Status: **PASS**",
        "",
        "The completed sweep satisfies the declared design matrix, matched-realization, "
        "traffic-contract, QoS-mapping, and packet-accounting invariants.",
        "",
        "| Check | Verified value |",
        "|---|---:|",
    ]
    content.extend(f"| {labels.get(key, key)} | {value} |" for key, value in rows)
    content.extend(
        [
            "",
            "## Enforced invariants",
            "",
            "- Every declared scenario/load/seed cell appears exactly once and reports `ok`.",
            "- Baseline and QoS-aware runs share the same traffic realization for each load and seed.",
            "- Each generated traffic file matches the finalized traffic contract and indexed seed.",
            "- DIAG, ALERT, and FAULT rows use the declared QFI/5QI mapping for each policy.",
            "- Packet loss is exactly `tx_packets - rx_packets` for every checked class row.",
            "",
        ]
    )
    path.write_text("\n".join(content), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sweep-dir", required=True)
    parser.add_argument("--design", default=str(ROOT / "config/paper-sweep.yaml"))
    args = parser.parse_args()
    sweep_dir = Path(args.sweep_dir).resolve()
    facts = audit(sweep_dir, Path(args.design).resolve())
    report = sweep_dir / "sweep_audit.md"
    write_audit_report(report, facts)
    print(f"[AUDIT][PASS] {sweep_dir}")
    for fact in facts:
        print(f"[AUDIT] {fact}")


if __name__ == "__main__":
    main()
