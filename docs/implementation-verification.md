# Implementation Verification

## Evidence chain

The custom C++ implementation, YAML contracts, sweep index, per-run outputs, analysis scripts, and generated figures were checked as one traceable system. The publication repository integrates with a separate ns-3.47/5G-LENA v4.2 tree through non-destructive links.

| Contract | Implementation | Enforcement |
|---|---|---|
| Simulator | ns-3.47 + 5G-LENA v4.2 | Exact Git tags checked by `check_environment.py` |
| Topology/radio | 1 gNB, 1 stationary EV UE, 1 server; 3.5 GHz; 10 MHz; numerology 1; UMi; TDD; HARQ | Shared values in both scenario templates and regression assertions |
| Traffic | Independent DIAG, ALERT, and FAULT ns-3 apps with declared payloads and timing models | Typed YAML validation, fixed-window tests, per-run Tx trace |
| Pairing | Same load, seed, and generated traffic file across the two policies | Sweep index audit |
| Baseline mapping | QFI 1/default for all maintenance classes | Flow-summary audit and source invariant |
| QoS-aware mapping | DIAG default; ALERT QFI 3/5QI 3; FAULT QFI 4/5QI 83 | Flow-summary audit |
| Packet loss | Application Tx minus FlowMonitor Rx | Per-run validation and full-sweep reconciliation |
| Uncertainty | 10,000-resample 95% bootstrap on seed rows | Deterministic unit tests and generated method note |
| Policy effect | Within-seed QoS-aware-minus-baseline contrast | Synthetic arithmetic test and paired-output schema |

## Output traceability

| Evidence | Source | Curated artifact |
|---|---|---|
| Offered workload | traffic YAML + app Tx ledger | `offered-traffic-seed1.*` and summary CSV |
| Absolute PDR/latency/deadline/throughput curves | seed-level FlowMonitor/app-ledger KPI rows | `paper_*_bootstrap_ci.png` and summary CSVs |
| Policy deltas | matched load/seed/class rows | `paired_policy_effects_bootstrap_ci.csv` and PNG |
| Matrix integrity | run index, generated traffic files, flow summaries | `sweep_audit.md` |
| Reproduction identity | resolved design and repository-source hashes | `metadata.yaml` |

## Executed verification

- Both custom C++ entry points compiled against ns-3.47 and 5G-LENA v4.2.
- Baseline and QoS-aware smoke runs at 0 and 30 Mbps completed and passed structural validation.
- The 2,600-run matrix passed coverage, status, pairing, traffic-contract, QFI/5QI, and packet-accounting checks.
- All 50 seed realizations were retained at every load and policy.
- Twenty-five repository regression tests pass.
- The result curator reruns the sweep audit before copying any public evidence.
- Private document formats and raw simulator artifacts are excluded from the repository tree.

The verified claim is mechanism-specific: under the declared simulated conditions, QoS-aware scheduling selectively preserves urgent maintenance flows during uplink congestion. The [limitations](limitations.md) define the boundary of that claim.
