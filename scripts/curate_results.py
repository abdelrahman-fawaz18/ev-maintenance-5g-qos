#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Curate compact, publication-safe evidence from a completed sweep.

Inputs:
    A completed sweep with generated analysis and the repository experiment contract.
Outputs:
    Audited tables, figures, metadata, and a sanitized seed-level dataset under ``results``.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import shutil
from pathlib import Path
from typing import Any

import yaml

try:
    from scripts.audit_sweep import audit, write_audit_report
except ModuleNotFoundError:  # Direct execution adds scripts/ rather than the repository root.
    from audit_sweep import audit, write_audit_report


ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sweep-dir", required=True)
    parser.add_argument("--output-dir", default=str(ROOT / "results"))
    args = parser.parse_args()
    sweep = Path(args.sweep_dir).resolve()
    output = Path(args.output_dir).resolve()
    audit_path = sweep / "sweep_audit.md"
    facts = audit(sweep, ROOT / "config" / "paper-sweep.yaml")
    write_audit_report(audit_path, facts)

    tables = output / "tables"
    figures = output / "figures"
    tables.mkdir(parents=True, exist_ok=True)
    figures.mkdir(parents=True, exist_ok=True)
    paper = sweep / "paper_analysis"
    for name in (
        "paper_summary_bootstrap_ci.csv",
        "paper_compact_bootstrap_ci.csv",
        "paper_selected_load_table.csv",
        "paper_selected_load_table.md",
        "bootstrap_ci_method_note.md",
        "paper_results_interpretation.md",
        "paired_policy_effects_bootstrap_ci.csv",
        "paired_policy_effects_selected_loads.md",
    ):
        source = paper / name
        destination = tables / name
        if source.suffix == ".md":
            portable = source.read_text(encoding="utf-8").replace(
                str(sweep), f"artifacts/sweeps/{sweep.name}"
            )
            destination.write_text(portable, encoding="utf-8")
        else:
            shutil.copy2(source, destination)
    for source in sorted((paper / "plots").glob("*.png")):
        shutil.copy2(source, figures / source.name)

    seed_candidates = sorted((sweep / "analysis").glob("*_seed_rows.csv"))
    if len(seed_candidates) != 1:
        raise SystemExit(f"Expected exactly one seed-row CSV, found {len(seed_candidates)}")
    seed_rows = read_csv(seed_candidates[0])
    fields = [field for field in seed_rows[0] if field != "run_dir"]
    sanitized = [{field: row[field] for field in fields} for row in seed_rows]
    write_csv(tables / "paper_seed_level_kpis.csv", sanitized, fields)

    design = yaml.safe_load((sweep / "sweep_config_resolved.yaml").read_text(encoding="utf-8"))
    metadata = {
        "experiment_id": sweep.name,
        "status": "audited-pass",
        "simulator": {"ns3": "ns-3.47", "five_g_lena": "v4.2"},
        "scenarios": design["scenarios"],
        "loads_mbps": design["ev_background_loads_mbps"],
        "seeds": design["seeds"],
        "run_count": len(design["scenarios"])
        * len(design["ev_background_loads_mbps"])
        * len(design["seeds"]),
        "bootstrap": {"iterations": 10000, "confidence": 0.95},
        "paired_design": {
            "keys": ["ev_background_load_mbps", "seed", "traffic_class"],
            "comparison": "metric_specific_differences_with_positive_effects_favoring_qos_aware",
        },
        "source_fingerprints_sha256": {
            "paper_sweep_yaml": sha256(ROOT / "config" / "paper-sweep.yaml"),
            "paper_traffic_yaml": sha256(ROOT / "config" / "paper-traffic.yaml"),
            "paper_baseline_yaml": sha256(ROOT / "config" / "paper-baseline.yaml"),
            "paper_slicing_yaml": sha256(ROOT / "config" / "paper-slicing.yaml"),
            "analysis_script": sha256(ROOT / "scripts" / "analyze_results.py"),
            "simulation_source": sha256(
                ROOT / "src" / "examples" / "maintenance-nr-single-run.cc"
            ),
        },
        "raw_artifacts_in_git": False,
    }
    (output / "metadata.yaml").write_text(
        yaml.safe_dump(metadata, sort_keys=False), encoding="utf-8"
    )
    shutil.copy2(audit_path, output / "sweep_audit.md")
    print(f"[CURATE][DONE] {output}")


if __name__ == "__main__":
    main()
