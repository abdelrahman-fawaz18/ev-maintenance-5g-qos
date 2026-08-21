# Methodology

## Research question

Can maintenance-aware 5G QoS differentiation preserve urgent EV telemetry under uplink congestion while routine telemetry remains best effort?

The comparison holds topology, radio/channel model, traffic realization, offered load, and seed constant. It changes only the bearer/scheduler policy.

| Policy | Flow treatment | Scheduler |
|---|---|---|
| Baseline | All application flows on the default QoS flow | `NrMacSchedulerOfdmaPF` |
| QoS-aware | Dedicated ALERT and FAULT flows; DIAG/background remain default | `NrMacSchedulerOfdmaQos` |

## Traffic model

| Class | Activity window | Payload | Inter-arrival model | Mean payload rate |
|---|---:|---:|---|---:|
| DIAG | 0–60 s | 1,136 B | fixed, 0.1 s | 90.88 kbps |
| ALERT | 15–35 s | 350 B | lognormal, mean 0.1 s, CV 0.5 | 28.00 kbps |
| FAULT | 20–50 s | 1,056 B | lognormal, mean 0.01 s, CV 0.25 | 844.80 kbps |

For lognormal inter-arrival mean `m` and coefficient of variation `CV`:

```text
sigma = sqrt(log(1 + CV²))
mu    = log(m) - sigma² / 2
```

All application payloads are at or below the configured 1,200 B UDP ceiling. A separate 1,200 B best-effort flow from the same EV UE creates the 0–40 Mbps congestion sweep.

## Radio and topology

- One gNB, one stationary EV UE at 50 m, and one remote server.
- 3.5 GHz center frequency, 10 MHz channel, one BWP, numerology 1.
- UMi Street Canyon, line of sight, shadowing enabled, HARQ enabled.
- 100 Gb/s, zero-delay core link to isolate the radio bottleneck.

## Experiment matrix and pairing

The load vector is:

```text
0, 5, 10, 15, 20, 22, 23, 24, 24.5, 25, 25.5, 26, 26.5,
27, 27.5, 28, 28.5, 29, 29.5, 30, 31, 32, 33, 34, 35, 40 Mbps
```

Seeds 1–50 are evaluated at every load under both policies while `RngRun` remains 1. Each load/seed pair shares one generated traffic file across policies. The complete matrix contains 2,600 simulations and 1,300 matched comparisons.

## KPI definitions

- **PDR:** received packets divided by application transmissions.
- **Delivered-packet mean latency:** mean delay among received packets.
- **Lost packets:** application transmissions minus received packets.
- **Deadline-violation rate:** late received packets plus all undelivered packets, divided by application transmissions.
- **Deadline-penalized mean delay:** delivered delay plus a class-deadline penalty for each missing packet, divided by transmissions.
- **Throughput:** received IP bytes divided by the class activity window.

Delivered-packet latency is interpreted jointly with PDR, deadline violations, and penalized delay so packet loss cannot appear as an artificial latency improvement.

## Statistical analysis

Two complementary summaries are generated:

1. Per-policy seed distributions are resampled independently for absolute KPI curves.
2. Policy effects are computed inside every matched load/seed/class pair before resampling.

The paired effect definitions are:

```text
PDR gain                         = QoS-aware PDR - baseline PDR
Deadline violations avoided     = baseline rate - QoS-aware rate
Penalized delay reduced         = baseline delay - QoS-aware delay
```

Each distribution uses 10,000 deterministic percentile-bootstrap resamples and a 95% interval. Probability bounds in absolute summaries are clipped to [0, 1]; paired differences are not clipped. No completed seed is excluded based on KPI outcome.
