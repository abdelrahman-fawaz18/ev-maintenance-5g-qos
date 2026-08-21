# SPDX-License-Identifier: GPL-2.0-only
"""Check licensing, accounting, metadata, and public-repository source invariants.

Inputs: tracked custom source, scripts, and repository paths.
Outputs: unittest assertions; no persistent files.
"""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SourceInvariantTests(unittest.TestCase):
    def test_custom_sources_declare_spdx_license(self) -> None:
        sources = [
            *ROOT.glob("src/**/*.h"),
            *ROOT.glob("src/**/*.cc"),
            *ROOT.glob("scripts/*.py"),
            *ROOT.glob("scripts/*.sh"),
            ROOT / "reproduce.sh",
        ]
        for source in sources:
            with self.subTest(source=source):
                self.assertIn("SPDX-License-Identifier: GPL-2.0-only", source.read_text()[:200])

    def test_complete_loss_accounting(self) -> None:
        source = (ROOT / "src/examples/maintenance-nr-single-run.cc").read_text()
        self.assertIn("st.txPackets - st.rxPackets", source)

    def test_baseline_reports_effective_default_qos(self) -> None:
        source = (ROOT / "src/examples/maintenance-nr-single-run.cc").read_text()
        self.assertIn('nrConfig.qos.alert.pdrTarget, "DEFAULT_QOS_FLOW"', source)
        self.assertIn("qosRuntime[name].fiveQiName", source)

    def test_repository_contains_no_raw_simulation_outputs(self) -> None:
        self.assertFalse((ROOT / "outputs").exists())

    def test_public_repository_excludes_private_document_formats(self) -> None:
        self.assertFalse(any(ROOT.glob("**/*.docx")))
        self.assertFalse(any(ROOT.glob("**/*.pptx")))
        self.assertFalse((ROOT / "presentation").exists())

    def test_raw_delay_interval_columns_are_unambiguous(self) -> None:
        source = (ROOT / "src/examples/maintenance-nr-single-run.cc").read_text()
        self.assertIn("mean_delay_ci_lower_ms,mean_delay_ci_upper_ms", source)


if __name__ == "__main__":
    unittest.main()
