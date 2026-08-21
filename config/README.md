# Configuration

Four YAML files define the finalized experiment:

| File | Role |
|---|---|
| `paper-traffic.yaml` | Simulation duration, RNG contract, payloads, activity windows, and inter-arrival distributions |
| `paper-baseline.yaml` | Shared topology/radio values with default flows and proportional-fair scheduling |
| `paper-slicing.yaml` | Shared topology/radio values with dedicated ALERT/FAULT flows and QoS-aware scheduling |
| `paper-sweep.yaml` | Two policies, 26 background-load points, 50 seeds, and aggregate KPI selection |

The scenario templates differ only in intended policy controls. `tests/test_paper_configuration.py` enforces that boundary. Execution reads these files and writes generated per-seed/per-load variants below `artifacts/sweeps/<id>/generated_configs/`; source files in this directory are not modified.

Field definitions and units are documented in [`docs/configuration-reference.md`](../docs/configuration-reference.md). Parameter rationale is recorded in [`docs/parameter-provenance.md`](../docs/parameter-provenance.md).
