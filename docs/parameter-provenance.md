# Parameter Provenance

This document records the finalized parameter set encoded by the repository and the rationale used to keep the experiment internally consistent.

| Parameter | Final value | Basis |
|---|---:|---|
| Simulator | ns-3.47 + 5G-LENA v4.2 | Pinned implementation environment |
| Carrier | 3.5 GHz, 10 MHz | Controlled sub-6 GHz NR uplink bottleneck |
| Numerology | 1 | Shared radio contract across both policies |
| Topology | 1 gNB, 1 stationary EV UE, 1 server | Isolates same-UE traffic differentiation |
| DIAG | 1,136 B every 0.1 s, 0–60 s | Continuous routine telemetry abstraction |
| ALERT | 350 B, mean IAT 0.1 s, CV 0.5, 15–35 s | Bounded stochastic urgent notifications |
| FAULT | 1,056 B, mean IAT 0.01 s, CV 0.25, 20–50 s | Sustained stochastic critical-report workload |
| Deadlines | DIAG 300 ms; ALERT 50 ms; FAULT 10 ms | Encodes increasing maintenance urgency |
| QoS-aware mapping | DIAG QFI 1; ALERT QFI 3/5QI 3; FAULT QFI 4/5QI 83 | Scheduler-visible differentiation with DIAG as control |
| Offered load | 0–40 Mbps, 26 points | Low-load, transition, and congested regimes |
| Randomness | seeds 1–50, run 1 | Fifty independent matched realizations per load |
| Bootstrap | 10,000 resamples, 95% interval | Stable seed-level uncertainty estimate |

Mean application payload rates are derived as `8 × payload_bytes / mean_IAT`. Lognormal parameters are derived from the declared mean and coefficient of variation using the formulas in [methodology](methodology.md). GBR values represent mean IP-layer demand; MBR values permit short stochastic bursts.

The core link is configured far above offered application demand and with zero modeled delay to keep the radio uplink as the intended bottleneck. All payloads remain within the configured UDP ceiling so the workload does not depend on IP fragmentation behavior.
