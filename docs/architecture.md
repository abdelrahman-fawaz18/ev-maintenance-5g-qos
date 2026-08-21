# Architecture

![EV maintenance telemetry and 5G QoS experiment architecture](assets/system-architecture.svg)

## Runtime data path

Vehicle data sources are represented by three application semantics:

1. `DiagTrafficApp` emits continuous routine telemetry.
2. `AlertTrafficApp` emits urgent anomaly notifications during a bounded activity window.
3. `FaultTrafficApp` emits high-rate fault-refresh fragments during a bounded activity window.

The apps send UDP traffic from the EV UE to class-specific remote-server ports. The application Tx ledger records every transmission independently of FlowMonitor. Port-based uplink classifiers then assign the effective QoS mapping.

| Policy | DIAG | ALERT | FAULT | Scheduler |
|---|---|---|---|---|
| Baseline | QFI 1, default | QFI 1, default | QFI 1, default | `NrMacSchedulerOfdmaPF` |
| QoS-aware | QFI 1, default | QFI 3, 5QI 3 | QFI 4, 5QI 83 | `NrMacSchedulerOfdmaQos` |

The physical topology, radio/channel model, transport endpoints, offered traffic, load, and seed realization are held constant. Dedicated flow activation and scheduler selection are the controlled policy variables.

## Measurement architecture

- The application Tx ledger supplies the complete packet denominator.
- FlowMonitor supplies received packets, bytes, and delay distributions.
- Class/port mapping joins the two sources into per-class KPI rows.
- Undelivered packets are computed as application transmissions minus received packets.
- Run metadata records resolved radio, scheduler, traffic, seed, and output parameters.
- The sweep index joins every scenario/load/seed cell to its immutable run directory and log.

## Source ownership

| Path | Responsibility |
|---|---|
| `src/apps/` | Packet timing, payload generation, UDP transmission, and Tx trace sources |
| `src/helpers/maintenance-yaml-config.h` | Traffic YAML parsing and validation |
| `src/helpers/maintenance-traffic-installer.h` | Application and sink installation plus RNG stream assignment |
| `src/helpers/maintenance-nr-scenario-config.h` | Radio, topology, scheduler, background-load, and QoS configuration |
| `src/helpers/maintenance-nr-output.h` | Collision-safe run directories, paths, and metadata |
| `src/examples/maintenance-nr-single-run.cc` | Topology composition, QoS activation, simulation, FlowMonitor accounting, and output emission |
| `scripts/run_sweep.py` | Matrix expansion, parallel execution, validation, indexing, and seed-level aggregation |
| `scripts/audit_sweep.py` | Matrix, pairing, contract, mapping, and loss-accounting evidence gate |
| `scripts/analyze_results.py` | Independent summaries, matched-seed effects, bootstrap intervals, tables, and plots |

The core transport is configured as non-bottleneck so the experimental bottleneck remains the 5G NR uplink. The QoS-aware case uses differentiated resource scheduling on one carrier; it is not a fixed physical partition.
