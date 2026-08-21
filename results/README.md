# Curated Results

This directory is the portable evidence package produced from the audited 2,600-run experiment. Raw packet traces, FlowMonitor XML, generated configurations, and process logs remain under ignored `artifacts/` paths.

## Inventory

| Path | Contents |
|---|---|
| `metadata.yaml` | Simulator versions, matrix values, pairing definition, bootstrap settings, and source SHA-256 fingerprints |
| `sweep_audit.md` | Coverage, pairing, traffic-contract, QoS-mapping, and packet-accounting evidence gate |
| `figures/paired_policy_effects_bootstrap_ci.png` | Primary matched-seed policy-effect visualization |
| `figures/paper_*_bootstrap_ci.png` | Absolute PDR, latency, deadline, penalized-delay, and throughput curves |
| `figures/offered-traffic-seed1.*` | Representative application workload timeline |
| `tables/paired_policy_effects_bootstrap_ci.csv` | Full matched-seed delta estimates and confidence bounds |
| `tables/paired_policy_effects_selected_loads.md` | Compact effect table at selected loads |
| `tables/paper_seed_level_kpis.csv` | Sanitized seed-level data without machine-local paths |
| `tables/paper_summary_bootstrap_ci.csv` | Full absolute bootstrap statistics |
| `tables/paper_compact_bootstrap_ci.csv` | Human-readable mean-and-interval cells |
| `tables/bootstrap_ci_method_note.md` | Statistical procedure |

Every quantitative statement in the root README and results narrative is traceable to these files. `metadata.yaml` records the exact design and source fingerprints used for curation.

## Regeneration

```bash
./reproduce.sh analyze --sweep-dir artifacts/sweeps/<full-sweep-id>
python3 scripts/curate_results.py --sweep-dir artifacts/sweeps/<full-sweep-id>
```

The curator reruns the audit, removes machine-local run paths from the seed dataset, and copies only compact public evidence. The file-level layout is specified in [`docs/output-reference.md`](../docs/output-reference.md).
