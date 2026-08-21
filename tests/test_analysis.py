# SPDX-License-Identifier: GPL-2.0-only
"""Verify deterministic interval estimation and matched-seed policy effects.

Inputs: synthetic metric samples and seed-level policy rows.
Outputs: unittest assertions; no persistent files.
"""

from __future__ import annotations

import random
import unittest

import numpy as np

from scripts import analyze_results


class AnalysisTests(unittest.TestCase):
    def test_paper_defaults(self) -> None:
        self.assertEqual(analyze_results.DEFAULT_BOOTSTRAP_ITERATIONS, 10_000)
        self.assertEqual(analyze_results.DEFAULT_CONFIDENCE, 0.95)

    def test_bootstrap_is_deterministic_and_bounded(self) -> None:
        args = dict(iterations=500, confidence=0.95, bounded=True)
        first = analyze_results.bootstrap_ci([0.8, 0.9, 1.0], rng=random.Random(7), **args)
        second = analyze_results.bootstrap_ci([0.8, 0.9, 1.0], rng=random.Random(7), **args)
        self.assertEqual(first, second)
        self.assertTrue(all(0.0 <= value <= 1.0 for value in first[:3]))

    def test_single_value_interval_collapses_to_mean(self) -> None:
        mean, low, high, sd = analyze_results.bootstrap_ci(
            [3.0], iterations=100, confidence=0.95, rng=random.Random(1), bounded=False
        )
        self.assertEqual((mean, low, high, sd), (3.0, 3.0, 3.0, 0.0))

    def test_vectorized_bootstrap_is_deterministic(self) -> None:
        args = dict(iterations=500, confidence=0.95, bounded=False)
        first = analyze_results.bootstrap_ci(
            [2.0, 4.0, 8.0], rng=np.random.default_rng(42), **args
        )
        second = analyze_results.bootstrap_ci(
            [2.0, 4.0, 8.0], rng=np.random.default_rng(42), **args
        )
        self.assertEqual(first, second)

    def test_paired_effects_use_matched_seed_differences(self) -> None:
        rows = []
        values = {
            1: {"baseline": (0.8, 0.3, 100.0), "slicing": (0.9, 0.1, 10.0)},
            2: {"baseline": (0.9, 0.4, 120.0), "slicing": (1.0, 0.2, 20.0)},
        }
        for seed, scenarios in values.items():
            for scenario, (pdr, deadline, delay) in scenarios.items():
                rows.append(
                    {
                        "scenario": scenario,
                        "ev_background_load_mbps": "30",
                        "seed": str(seed),
                        "traffic_class": "ALERT_TOTAL",
                        "pdr": str(pdr),
                        "deadline_violation_rate": str(deadline),
                        "deadline_penalized_mean_ms": str(delay),
                    }
                )
        effects = analyze_results.paired_policy_effects(
            rows, iterations=500, confidence=0.95, rng_seed=19
        )
        by_metric = {row["metric"]: row for row in effects}
        self.assertAlmostEqual(by_metric["pdr_gain"]["paired_effect_mean"], 0.1)
        self.assertAlmostEqual(
            by_metric["deadline_violation_reduction"]["paired_effect_mean"], 0.2
        )
        self.assertAlmostEqual(
            by_metric["deadline_penalized_delay_reduction_ms"]["paired_effect_mean"],
            95.0,
        )
        self.assertTrue(all(row["n_pairs"] == 2 for row in effects))


if __name__ == "__main__":
    unittest.main()
