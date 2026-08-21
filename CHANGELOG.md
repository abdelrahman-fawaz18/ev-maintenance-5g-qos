# Changelog

All notable changes to the public implementation are documented here.

## [1.0.0] - 2026-08-21

### Added

- Standalone custom C++ traffic generators, helpers, and 5G NR scenario.
- Paper-aligned 50-seed, 26-load, paired-policy experiment configuration.
- Non-destructive ns-3 installer and exact-version environment checks.
- Parallel sweep runner, per-run validation, deterministic bootstrap analysis, and plots.
- Regression tests for scientific configuration and source accounting invariants.
- Architecture, execution, methodology, configuration, reproducibility, output, verification, results, and limitations documentation.
- Engineering system and experiment-pipeline diagrams for the repository landing page.
- Matched-seed policy-effect tables and visualization with deterministic 95% paired-bootstrap intervals.

### Corrected

- Standardized the finalized FAULT activity window and 50-seed experiment contract across configuration, tests, analysis, and documentation.
- Counted undelivered packets as application TX minus received packets.
- Reported effective default-bearer QoS metadata in the baseline case.
- Executed prebuilt simulation binaries directly so concurrent runs cannot race an ns-3 relink.
- Renamed diagnostic delay-confidence columns to explicit lower/upper bounds and enforced expected run duration.
