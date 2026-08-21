# SPDX-License-Identifier: GPL-2.0-only
"""Exercise sweep-audit invariants with an isolated synthetic experiment.

Inputs: temporary run-index, traffic-contract, and flow-summary fixtures.
Outputs: unittest assertions; temporary files are removed automatically.
"""

from __future__ import annotations

import csv
import tempfile
import unittest
from pathlib import Path

import yaml

from scripts.audit_sweep import audit


FLOW_FIELDS = ["traffic_class", "qfi", "five_qi", "tx_packets", "rx_packets", "lost_packets"]
INDEX_FIELDS = [
    "scenario",
    "ev_background_load_mbps",
    "seed",
    "status",
    "run_dir",
    "nr_config",
    "traffic_config",
    "log_file",
]


class SweepAuditTests(unittest.TestCase):
    def make_fixture(self, root: Path) -> tuple[Path, Path, Path]:
        sweep = root / "sweep"
        sweep.mkdir()
        design = root / "design.yaml"
        design.write_text(
            yaml.safe_dump(
                {
                    "scenarios": ["baseline", "slicing"],
                    "ev_background_loads_mbps": [30],
                    "seeds": [1],
                }
            ),
            encoding="utf-8",
        )
        traffic = sweep / "traffic-seed1.yaml"
        contract = yaml.safe_load(
            (Path(__file__).resolve().parents[1] / "config" / "paper-traffic.yaml").read_text(
                encoding="utf-8"
            )
        )
        contract["rng"]["seed"] = 1
        traffic.write_text(yaml.safe_dump(contract, sort_keys=False), encoding="utf-8")
        records = []
        for scenario in ("baseline", "slicing"):
            run_dir = sweep / scenario
            run_dir.mkdir()
            mappings = {
                "DIAG_TOTAL": ("1", "DEFAULT_QOS_FLOW"),
                "ALERT_TOTAL": (
                    ("1", "DEFAULT_QOS_FLOW")
                    if scenario == "baseline"
                    else ("3", "GBR_GAMING")
                ),
                "FAULT_TOTAL": (
                    ("1", "DEFAULT_QOS_FLOW")
                    if scenario == "baseline"
                    else ("4", "DGBR_DISCRETE_AUT_LARGE")
                ),
            }
            with (run_dir / "maintenance_nr_flow_summary.csv").open(
                "w", newline="", encoding="utf-8"
            ) as handle:
                writer = csv.DictWriter(handle, fieldnames=FLOW_FIELDS)
                writer.writeheader()
                for traffic_class, (qfi, five_qi) in mappings.items():
                    writer.writerow(
                        {
                            "traffic_class": traffic_class,
                            "qfi": qfi,
                            "five_qi": five_qi,
                            "tx_packets": 100,
                            "rx_packets": 98,
                            "lost_packets": 2,
                        }
                    )
            records.append(
                {
                    "scenario": scenario,
                    "ev_background_load_mbps": 30,
                    "seed": 1,
                    "status": "ok",
                    "run_dir": run_dir,
                    "nr_config": "config.yaml",
                    "traffic_config": traffic,
                    "log_file": "run.log",
                }
            )
        with (sweep / "run_index.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=INDEX_FIELDS)
            writer.writeheader()
            writer.writerows(records)
        return sweep, design, sweep / "baseline" / "maintenance_nr_flow_summary.csv"

    def test_complete_paired_fixture_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sweep, design, _ = self.make_fixture(Path(temporary))
            facts = audit(sweep, design)
            self.assertIn("matrix_cells=2", facts)
            self.assertIn("paired_cells=1", facts)
            self.assertIn("flow_rows_checked=6", facts)
            self.assertIn("traffic_configs_checked=1", facts)

    def test_loss_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sweep, design, flow_path = self.make_fixture(Path(temporary))
            text = flow_path.read_text(encoding="utf-8").replace(",98,2", ",98,1", 1)
            flow_path.write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "tx-rx != lost"):
                audit(sweep, design)


if __name__ == "__main__":
    unittest.main()
