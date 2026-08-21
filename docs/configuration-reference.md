# Configuration Reference

The four YAML files in `config/` are the authoritative experiment contract. Scenario templates are never modified during execution; generated per-cell files are written inside the sweep directory.

## `paper-traffic.yaml`

| Group | Fields | Units and constraints |
|---|---|---|
| `sim` | `duration_s` | seconds; positive |
| `rng` | `seed`, `run` | positive integers; seed is replaced per realization |
| `validation` | `max_udp_payload_B` | bytes; every application payload must not exceed it |
| `diag` | activity window, payload, fixed IAT | seconds and bytes; start < stop ≤ duration |
| `alert` | activity window, payload, lognormal `mu`/`sigma`, deterministic first packet | seconds, bytes, log-seconds |
| `fault` | activity window, payload, fragments per refresh, lognormal `mu`/`sigma`, deterministic first burst | seconds, bytes, count, log-seconds |

## `paper-baseline.yaml` and `paper-slicing.yaml`

Shared groups:

- `run`: experiment name, output root, traffic path, app offset, RNG stream base, drain time, trace flags.
- `radio`: center frequency, bandwidth, numerology, transmit powers, channel model, LOS/shadowing, TDD pattern, HARQ.
- `topology`: gNB/UE positions and remote-core link properties.
- `scheduler`: OFDMA access and policy selector.
- `background` and `ev_background`: offered-load sources, packet size, port, and activity window.
- `qos`: per-class QFI, 5QI, precedence, ARP, GBR/MBR, deadline, and PDR target.
- `logging`: filenames for app traces, summaries, histograms, FlowMonitor XML, metadata, and manifest.

Policy-specific controls:

| Field | Baseline | QoS-aware |
|---|---|---|
| `scheduler.type` | `pf` | `qos` |
| `scenario.activate_dedicated_qos_flows` | `false` | `true` |
| `qos.alert.dedicated_qos_flow_enabled` | `false` | `true` |
| `qos.fault.dedicated_qos_flow_enabled` | `false` | `true` |

All other scientific fields are asserted equal by `test_policy_pair_changes_only_intended_controls`.

## `paper-sweep.yaml`

| Field | Meaning |
|---|---|
| `name` | Sweep identifier prefix |
| `scenarios` | Policy templates to execute: `baseline`, `slicing` |
| `ev_background_loads_mbps` | 26 offered-load values from 0 through 40 Mbps |
| `seeds` | 50 independent seed values, 1 through 50 |
| `radio` | Matrix-wide radio overrides that must match both templates |
| `analysis` | Traffic classes and metrics aggregated from per-run flow summaries |

The matrix size is the Cartesian product of scenarios, loads, and seeds. Any change to these files changes the scientific contract and requires regression, smoke, and result regeneration.
