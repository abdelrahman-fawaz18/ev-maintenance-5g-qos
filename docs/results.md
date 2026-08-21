# Results

## Main finding

The audited experiment shows initial stochastic deadline stress near 22 Mbps and a stronger congestion transition around 27–30 Mbps. Under congestion, the QoS-aware policy selectively protects ALERT and FAULT while DIAG remains on default treatment.

QoS-aware scheduling reallocates scarce radio resources; it does not add capacity. Similar DIAG behavior under both policies is therefore an important negative control.

## Matched policy effects

Values are means with 95% paired-bootstrap confidence intervals. A positive value favors QoS-aware scheduling.

| Load | Class | PDR gain [pp] | Deadline violations avoided [pp] | Penalized delay reduced [ms] |
|---:|---|---:|---:|---:|
| 23 Mbps | ALERT | 0.999 [0.000, 2.745] | 14.000 [6.000, 24.000] | 244 [72, 477] |
| 23 Mbps | FAULT | 2.695 [0.208, 6.623] | 15.737 [5.999, 25.999] | 208 [83, 359] |
| 30 Mbps | ALERT | 5.413 [4.024, 7.496] | 100.000 [100.000, 100.000] | 1,526 [1,452, 1,647] |
| 30 Mbps | FAULT | 18.927 [16.296, 22.433] | 99.998 [99.995, 100.000] | 1,271 [1,265, 1,278] |
| 40 Mbps | ALERT | 11.876 [10.741, 13.373] | 100.000 [100.000, 100.000] | 1,439 [1,357, 1,577] |
| 40 Mbps | FAULT | 37.469 [35.533, 39.937] | 99.999 [99.996, 100.000] | 987 [975, 1,002] |

At 30 Mbps, QoS-aware FAULT deadline violations were **0.002013%**, not exactly zero; ALERT violations were zero. At 40 Mbps, the corresponding FAULT value was **0.001345%** and ALERT remained zero. The displayed avoided-violation effects retain three decimal percentage-point precision, while the machine-readable table preserves full precision.

![Matched-seed policy effects](../results/figures/paired_policy_effects_bootstrap_ci.png)

## Absolute KPI snapshot

At 40 Mbps, baseline ALERT PDR was 0.881 [0.866, 0.893] and baseline FAULT PDR was 0.625 [0.601, 0.644]. Both QoS-aware PDR means were 1.000 [1.000, 1.000]. QoS-aware loss-penalized mean delay was 2.95 ms [2.92, 2.98] for ALERT and 3.05 ms [2.97, 3.19] for FAULT, compared with 1,442 ms [1,360, 1,580] and 990 ms [979, 1,006] under baseline operation.

At the same endpoint, DIAG PDR was 0.110 under baseline and 0.107 under QoS-aware scheduling. Its paired PDR effect was -0.223 percentage points [-0.663, 0.183], consistent with no material delivery improvement for the best-effort control flow.

## Evidence integrity

The sweep audit accepted:

- 2,600 complete scenario/load/seed cells;
- 1,300 matched policy pairs;
- 7,800 maintenance-class accounting rows;
- 50 seed-specific traffic configurations;
- exact policy-specific QFI/5QI mappings; and
- exact `lost_packets = tx_packets - rx_packets` reconciliation.

The curated [paired effects](../results/tables/paired_policy_effects_bootstrap_ci.csv), [absolute summaries](../results/tables/paper_summary_bootstrap_ci.csv), [seed-level KPI rows](../results/tables/paper_seed_level_kpis.csv), and [audit report](../results/sweep_audit.md) form the quantitative evidence package.
