# SPDX-License-Identifier: GPL-2.0-only
"""Pin the finalized experiment design and its policy-controlled differences.

Inputs: the four authoritative YAML configurations.
Outputs: unittest assertions; no persistent files.
"""

from __future__ import annotations

import unittest
from pathlib import Path

import yaml

from scripts.plot_offered_traffic import ALERT_WINDOW_S, FAULT_WINDOW_S


ROOT = Path(__file__).resolve().parents[1]


class PaperConfigurationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sweep = yaml.safe_load((ROOT / "config/paper-sweep.yaml").read_text())
        cls.traffic = yaml.safe_load((ROOT / "config/paper-traffic.yaml").read_text())
        cls.baseline = yaml.safe_load((ROOT / "config/paper-baseline.yaml").read_text())
        cls.slicing = yaml.safe_load((ROOT / "config/paper-slicing.yaml").read_text())

    def test_fifty_independent_seeds(self) -> None:
        self.assertEqual(self.sweep["seeds"], list(range(1, 51)))

    def test_load_sweep_covers_zero_through_forty(self) -> None:
        loads = self.sweep["ev_background_loads_mbps"]
        self.assertEqual(min(loads), 0)
        self.assertEqual(max(loads), 40)
        self.assertEqual(len(loads), len(set(loads)))

    def test_fault_window_matches_final_contract(self) -> None:
        self.assertEqual(self.traffic["fault"]["start_s"], 20.0)
        self.assertEqual(self.traffic["fault"]["stop_s"], 50.0)
        self.assertEqual(FAULT_WINDOW_S, (20.0, 50.0))
        self.assertEqual(ALERT_WINDOW_S, (15.0, 35.0))

    def test_scenarios_use_the_authoritative_traffic_contract(self) -> None:
        self.assertEqual(self.baseline["run"]["traffic_config_path"], "config/paper-traffic.yaml")
        self.assertEqual(self.slicing["run"]["traffic_config_path"], "config/paper-traffic.yaml")

    def test_radio_and_policy_pair(self) -> None:
        self.assertEqual(self.baseline["radio"]["bandwidth_hz"], 10_000_000)
        self.assertEqual(self.slicing["radio"]["bandwidth_hz"], 10_000_000)
        self.assertEqual(self.baseline["scheduler"]["type"], "pf")
        self.assertEqual(self.slicing["scheduler"]["type"], "qos")
        self.assertFalse(self.baseline["scenario"]["activate_dedicated_qos_flows"])
        self.assertTrue(self.slicing["scenario"]["activate_dedicated_qos_flows"])

    def test_full_design_run_count(self) -> None:
        count = len(self.sweep["scenarios"]) * len(self.sweep["seeds"]) * len(
            self.sweep["ev_background_loads_mbps"]
        )
        self.assertEqual(count, 2600)

    def test_policy_pair_changes_only_intended_controls(self) -> None:
        baseline = dict(self.baseline)
        slicing = dict(self.slicing)
        for config in (baseline, slicing):
            config["run"] = dict(config["run"])
            config["scenario"] = dict(config["scenario"])
            config["scheduler"] = dict(config["scheduler"])
            config["qos"] = {
                name: dict(values) for name, values in config["qos"].items()
            }

        baseline["run"]["name"] = slicing["run"]["name"] = "policy-run"
        baseline["scenario"] = slicing["scenario"] = {"policy_control": True}
        baseline["scheduler"] = slicing["scheduler"] = {"policy_control": True}
        for traffic_class in ("alert", "fault"):
            baseline["qos"][traffic_class]["dedicated_qos_flow_enabled"] = True

        self.assertEqual(baseline, slicing)

    def test_payloads_do_not_require_ip_fragmentation(self) -> None:
        ceiling = self.traffic["validation"]["max_udp_payload_B"]
        for traffic_class in ("diag", "alert", "fault"):
            self.assertLessEqual(self.traffic[traffic_class]["payload_B"], ceiling)

    def test_dense_sampling_covers_congestion_transition(self) -> None:
        loads = set(self.sweep["ev_background_loads_mbps"])
        expected = {22, 23, 24, 24.5, 25, 25.5, 26, 26.5, 27, 27.5, 28, 28.5, 29, 29.5, 30}
        self.assertTrue(expected.issubset(loads))


if __name__ == "__main__":
    unittest.main()
