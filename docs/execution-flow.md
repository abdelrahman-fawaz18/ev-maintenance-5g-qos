# Execution Flow

![Configuration-to-evidence pipeline](assets/experiment-pipeline.svg)

## Command dispatch

`reproduce.sh` is the stable shell entry point. It forwards arguments to `scripts/reproduce.py`, which supports four modes:

| Mode | Work performed | Primary outputs |
|---|---|---|
| `check` | Version checks and Python regression tests | Console verification report |
| `smoke` | Install, build, four-run matrix, per-run validation, audit, and analysis | Fast sweep under `artifacts/sweeps/` |
| `paper` | Install, build, full 2,600-run matrix, validation, audit, analysis, and curation | Full sweep plus refreshed `results/` |
| `analyze` | Re-audit and re-analyze an existing sweep | Updated audit and `paper_analysis/` artifacts |

## Full execution sequence

1. `check_environment.py` confirms exact ns-3.47 and 5G-LENA v4.2 tags.
2. Repository unit tests enforce configuration, statistical, accounting, licensing, and runner invariants.
3. `install_into_ns3.py` creates non-destructive project links and two scratch shims.
4. ns-3 builds the traffic smoke program and the NR single-run program.
5. `run_sweep.py` reads `paper-sweep.yaml`, creates one traffic realization per seed, generates scenario/load configurations, and executes independent cells in parallel.
6. `validate_run.py` checks required files, simulation duration, class coverage, Tx/Rx consistency, probability bounds, interval ordering, and TOTAL accounting.
7. `audit_sweep.py` proves complete matrix coverage, matched traffic realizations, traffic-contract equality, effective QFI/5QI mapping, and exact loss reconciliation.
8. `analyze_results.py` computes per-policy bootstrap summaries and within-pair policy deltas.
9. In `paper` mode, `curate_results.py` reruns the audit and copies sanitized, portable evidence to `results/`.

Every run is written to a unique directory. Configuration templates are read-only inputs; generated variants are isolated within the sweep directory.
