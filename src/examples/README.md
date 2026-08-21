# Simulation Entry Points

| Source | Purpose | Inputs | Outputs |
|---|---|---|---|
| `maintenance-traffic-smoke.cc` | Fast traffic-generator validation on a lightweight point-to-point IP topology | Traffic YAML and output path | Packet trace and class Tx counters |
| `maintenance-nr-single-run.cc` | Complete one-gNB/one-EV 5G NR policy experiment | Scenario YAML, traffic YAML, seed/load/output overrides | Flow summary, delay histogram, Tx trace, FlowMonitor XML, metadata, manifest |

The repository installer exposes both files as small ns-3 scratch entry points without copying or altering the source. The sweep runner executes the already-built NR binary directly so concurrent cells do not trigger relinking.
