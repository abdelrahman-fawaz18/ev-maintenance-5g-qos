# Limitations

The implementation evaluates a specific scheduling mechanism under a controlled simulation contract.

## Model boundary

- One stationary EV UE and one serving gNB; no multi-UE contention, mobility, handover, or multi-cell interference.
- One 10 MHz carrier with a fixed TDD pattern and declared UMi channel model.
- Non-bottleneck, zero-delay core transport; core congestion and end-to-end service processing are outside scope.
- Synthetic application traffic representing maintenance semantics; no physical battery, powertrain, thermal, or fault-propagation model.
- QoS-aware scheduling and dedicated flows on one carrier; no fixed physical resource partition or complete commercial network-slice lifecycle.
- One baseline scheduler and one QoS-aware scheduler; broader scheduler sensitivity is not established.

## Evidence boundary

- Results are simulation outcomes for ns-3.47 and 5G-LENA v4.2 under the versioned configuration.
- Fifty matched seeds quantify stochastic variability within this model, not uncertainty across deployments, hardware, geography, or operator configuration.
- The congestion source is a same-UE best-effort UDP flow; external competing UEs may produce different scheduling behavior.
- Deadline-penalized delay is an analysis metric defined by this study, not a standardized network KPI.
- PDR of 1.000 at three-decimal display precision does not imply zero deadline violations; full-precision rate tables remain authoritative.

## Claim boundary

The results support selective protection of urgent maintenance traffic in the declared simulated RAN scenario. They do not establish field performance, functional safety, cybersecurity, regulatory compliance, or production readiness. Deployment evidence would require multi-UE/mobility studies, sensitivity analysis, hardware- or network-in-the-loop testing, and operational validation.
