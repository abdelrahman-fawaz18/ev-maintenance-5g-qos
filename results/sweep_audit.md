# Sweep Integrity Audit

Status: **PASS**

The completed sweep satisfies the declared design matrix, matched-realization, traffic-contract, QoS-mapping, and packet-accounting invariants.

| Check | Verified value |
|---|---:|
| Complete scenario × load × seed matrix cells | 2600 |
| Matched load × seed policy pairs | 1300 |
| Traffic-class accounting rows checked | 7800 |
| Seed-specific traffic configurations checked | 50 |
| Independent seed values | 50 |
| Declared background-load points | 26 |

## Enforced invariants

- Every declared scenario/load/seed cell appears exactly once and reports `ok`.
- Baseline and QoS-aware runs share the same traffic realization for each load and seed.
- Each generated traffic file matches the finalized traffic contract and indexed seed.
- DIAG, ALERT, and FAULT rows use the declared QFI/5QI mapping for each policy.
- Packet loss is exactly `tx_packets - rx_packets` for every checked class row.
