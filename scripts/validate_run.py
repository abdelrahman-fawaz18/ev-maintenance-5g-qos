#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Validate the structural and accounting integrity of one simulation run.

Inputs:
    A run directory and the expected simulation duration.
Outputs:
    A concise validation report on stdout, or a nonzero exit with the failed invariant.
"""

from __future__ import annotations

import argparse
import csv
import sys
from collections import Counter
from pathlib import Path

CLASSES = ("DIAG", "ALERT", "FAULT")
REQUIRED_FLOW_COLUMNS = {
    "traffic_class",
    "qfi",
    "five_qi",
    "rule_precedence",
    "latency_deadline_ms",
    "pdr_target",
    "tx_packets",
    "rx_packets",
    "lost_packets",
    "pdr",
    "deadline_violations",
    "deadline_violation_rate",
    "mean_delay_ms",
    "mean_delay_ci_lower_ms",
    "mean_delay_ci_upper_ms",
    "delay_p05_ms",
    "delay_p95_ms",
    "deadline_penalized_mean_ms",
}


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    return rows


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def load_metadata(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--expected-duration-s", type=float, default=60.0)
    args = parser.parse_args()

    run_dir = Path(args.run_dir)
    if not run_dir.is_dir():
        fail(f"run directory does not exist: {run_dir}")

    app_trace = run_dir / "maintenance_nr_app_tx_packets.csv"
    flow_summary = run_dir / "maintenance_nr_flow_summary.csv"
    delay_histogram = run_dir / "maintenance_nr_delay_histogram.csv"
    metadata = run_dir / "run_metadata.txt"
    manifest = run_dir / "run_manifest.csv"
    flow_xml = run_dir / "flow-monitor.xml"
    for path in (app_trace, flow_summary, delay_histogram, metadata, manifest, flow_xml):
        if not path.exists():
            fail(f"missing required output: {path.name}")

    metadata_values = load_metadata(metadata)
    try:
        actual_duration = float(metadata_values["duration_s"])
    except (KeyError, ValueError):
        fail("run metadata has no valid duration_s")
    if abs(actual_duration - args.expected_duration_s) > 1e-9:
        fail(
            f"duration_s {actual_duration:g} != expected {args.expected_duration_s:g}"
        )

    flow_rows = load_csv(flow_summary)
    if not flow_rows:
        fail("flow summary is empty")
    missing_cols = REQUIRED_FLOW_COLUMNS.difference(flow_rows[0].keys())
    if missing_cols:
        fail(f"flow summary missing columns: {sorted(missing_cols)}")

    histogram_rows = load_csv(delay_histogram)
    if not histogram_rows:
        fail("delay histogram is empty")
    manifest_rows = load_csv(manifest)
    if not manifest_rows:
        fail("run manifest is empty")

    class_rows = {row["traffic_class"].upper(): row for row in flow_rows if row["traffic_class"].upper() in CLASSES}
    total_rows = [row for row in flow_rows if row["traffic_class"].upper() == "TOTAL"]
    missing_classes = [cls for cls in CLASSES if cls not in class_rows]
    if missing_classes:
        fail(f"missing flow-summary class rows: {missing_classes}")
    if len(total_rows) != 1:
        fail("expected exactly one TOTAL row")

    app_counts: Counter[str] = Counter()
    with app_trace.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            cls = row.get("traffic_class", "").upper()
            if cls in CLASSES:
                app_counts[cls] += 1
    for cls in CLASSES:
        tx_packets = int(float(class_rows[cls]["tx_packets"]))
        if app_counts[cls] != tx_packets:
            fail(f"{cls} app trace count {app_counts[cls]} != FlowMonitor tx_packets {tx_packets}")
        pdr = float(class_rows[cls]["pdr"])
        if not 0.0 <= pdr <= 1.0:
            fail(f"{cls} PDR is outside [0, 1]: {pdr}")
        deadline_rate = float(class_rows[cls]["deadline_violation_rate"])
        if not 0.0 <= deadline_rate <= 1.0:
            fail(f"{cls} deadline_violation_rate is outside [0, 1]: {deadline_rate}")
        ci_lower = float(class_rows[cls]["mean_delay_ci_lower_ms"])
        ci_upper = float(class_rows[cls]["mean_delay_ci_upper_ms"])
        if ci_lower > ci_upper:
            fail(f"{cls} delay CI bounds are reversed: {ci_lower} > {ci_upper}")

    total = total_rows[0]
    total_tx = int(float(total["tx_packets"]))
    summed_tx = sum(int(float(class_rows[cls]["tx_packets"])) for cls in CLASSES)
    if total_tx != summed_tx:
        fail(f"TOTAL tx_packets {total_tx} != sum of class tx_packets {summed_tx}")

    print(f"[OK] validated {run_dir}")
    for cls in CLASSES:
        row = class_rows[cls]
        print(
            f"[OK] {cls}: tx={row['tx_packets']} rx={row['rx_packets']} "
            f"pdr={float(row['pdr']):.6f} mean_delay_ms={float(row['mean_delay_ms']):.6f} "
            f"deadline_violations={row['deadline_violations']}"
        )
    print(
        f"[OK] TOTAL: tx={total['tx_packets']} rx={total['rx_packets']} "
        f"pdr={float(total['pdr']):.6f} mean_delay_ms={float(total['mean_delay_ms']):.6f}"
    )


if __name__ == "__main__":
    main()
