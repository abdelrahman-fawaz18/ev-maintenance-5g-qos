# Contributing

Thank you for improving the project. Keep changes reproducible, reviewable, and scoped to the public implementation.

1. Open an issue describing the problem, expected scientific or software impact, and proposed validation.
2. Create a focused branch and avoid committing `artifacts/`, generated ns-3 trees, or private study documents.
3. Run `python3 -m unittest discover -s tests -v`.
4. For C++ or configuration changes, run the one-command smoke workflow against ns-3.47 + 5G-LENA v4.2.
5. Document any change to traffic, radio, QoS, seed, load, KPI, or statistical assumptions.

Pull requests should include the command used for verification and a concise explanation of whether results are expected to change. New experiment claims require seed-level output and uncertainty estimates; single-run screenshots are not sufficient evidence.

By contributing, you agree that your contribution is licensed under GPL-2.0-only.
