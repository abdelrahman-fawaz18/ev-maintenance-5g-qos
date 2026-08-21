# Workflow Scripts

| Script | Input | Output / effect |
|---|---|---|
| `bootstrap_ns3.sh` | New destination path | Clones ns-3.47 and 5G-LENA v4.2 |
| `check_environment.py` | ns-3 root | Exact-version verification |
| `install_into_ns3.py` | ns-3 root | Non-destructive project links and scratch shims |
| `reproduce.py` | Workflow mode and paths | Coordinates test, build, run, audit, analysis, and curation |
| `run_sweep.py` | YAML contracts, ns-3 root, jobs | Generated configs, immutable runs, index, seed rows, quick summaries |
| `validate_run.py` | One run directory and duration | Structural/accounting PASS or failed invariant |
| `audit_sweep.py` | Sweep directory and design | Matrix-integrity report |
| `analyze_results.py` | Completed sweep | Absolute and paired bootstrap tables/plots |
| `curate_results.py` | Completed sweep | Sanitized portable evidence under `results/` |
| `plot_offered_traffic.py` | App Tx packet CSV | Binned offered-load table and timeline figure |

`reproduce.sh` at the repository root is the standard entry point. Direct script invocation is useful for targeted diagnostics and result regeneration. The complete call order is documented in [`docs/execution-flow.md`](../docs/execution-flow.md).
