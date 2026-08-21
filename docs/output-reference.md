# Output Reference

Generated runtime artifacts are written below `artifacts/` and ignored by Git. The portable evidence subset is curated under `results/`.

## Sweep directory

```text
artifacts/sweeps/<experiment-id>/
├── sweep_config_resolved.yaml
├── run_index.csv
├── generated_configs/
│   ├── traffic/
│   └── scenarios/
├── runs/<unique-run-id>/
├── logs/
├── analysis/
├── paper_analysis/
└── sweep_audit.md
```

`run_index.csv` is the primary lookup table for scenario, background load, seed, status, run directory, generated configurations, and process log.

## Per-run outputs

| File | Contents |
|---|---|
| `maintenance_nr_app_tx_packets.csv` | Application Tx time, class, and payload bytes |
| `maintenance_nr_flow_summary.csv` | Per-flow and per-class QoS identity, Tx/Rx/loss, PDR, deadlines, delay, and throughput |
| `maintenance_nr_delay_histogram.csv` | Flow/class delay bins, counts, cumulative counts, and CDF |
| `flow-monitor.xml` | Native FlowMonitor output |
| `run_metadata.txt` | Resolved scenario, seed, timing, background, QoS, and scheduler values |
| `run_manifest.csv` | Logical artifact name and resolved path |

The raw flow summary uses `mean_delay_ci_lower_ms` and `mean_delay_ci_upper_ms` for its diagnostic 95% normal interval. Seed-level bootstrap uncertainty is generated separately.

## Sweep analysis

- `analysis/*_seed_rows.csv`: normalized run/class KPI rows with a `run_dir` traceability column.
- `analysis/*_summary.csv`: quick 95% normal summaries used during sweep inspection.
- `paper_analysis/paper_summary_bootstrap_ci.csv`: full absolute 95% bootstrap statistics.
- `paper_analysis/paired_policy_effects_bootstrap_ci.csv`: matched-seed effect estimates and definitions.
- `paper_analysis/plots/`: generated absolute and paired plots.

## Curated results

```text
results/
├── metadata.yaml
├── sweep_audit.md
├── figures/
└── tables/
```

The curator removes machine-local `run_dir` fields from `paper_seed_level_kpis.csv`, records configuration/source fingerprints in `metadata.yaml`, and copies only compact tables and figures. Raw packet traces, XML, logs, and generated configs are not committed.
