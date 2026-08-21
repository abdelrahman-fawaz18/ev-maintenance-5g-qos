# SPDX-License-Identifier: GPL-2.0-only
"""Verify deterministic discovery of the prebuilt ns-3 simulation executable.

Inputs: temporary build-directory layouts.
Outputs: unittest assertions; temporary files are removed automatically.
"""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from scripts.run_sweep import find_simulation_executable


class SweepRunnerTests(unittest.TestCase):
    def test_resolves_one_prebuilt_simulation_executable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            scratch = Path(tmp) / "build" / "scratch"
            scratch.mkdir(parents=True)
            executable = scratch / "ns3.47-maintenance-nr-single-run-optimized"
            executable.write_text("#!/bin/sh\n", encoding="utf-8")
            executable.chmod(0o755)
            self.assertEqual(find_simulation_executable(Path(tmp)), executable)

    def test_rejects_missing_simulation_executable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "build" / "scratch").mkdir(parents=True)
            with self.assertRaises(SystemExit):
                find_simulation_executable(Path(tmp))

    def test_ignores_non_executable_build_products(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            scratch = Path(tmp) / "build" / "scratch"
            scratch.mkdir(parents=True)
            product = scratch / "ns3.47-maintenance-nr-single-run-optimized"
            product.write_text("not executable\n", encoding="utf-8")
            product.chmod(0o644)
            self.assertFalse(os.access(product, os.X_OK))
            with self.assertRaises(SystemExit):
                find_simulation_executable(Path(tmp))


if __name__ == "__main__":
    unittest.main()
