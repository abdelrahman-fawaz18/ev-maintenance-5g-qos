# Traffic Applications

The three header-only ns-3 applications model application-layer maintenance traffic and expose a common timestamp/payload Tx trace.

| Header | Purpose | Principal inputs | Outputs |
|---|---|---|---|
| `diag-traffic-app.h` | Fixed-period routine telemetry | Endpoint, payload, period, activity window, RNG stream | UDP DIAG packets and Tx trace |
| `alert-traffic-app.h` | Bounded stochastic alerts | Endpoint, payload, lognormal IAT, activity window, first-packet control, RNG stream | UDP ALERT packets and Tx trace |
| `fault-traffic-app.h` | Bounded stochastic fault-refresh fragments | Endpoint, fragment payload/count, burst timing, lognormal IAT, activity window, RNG stream | UDP FAULT fragments and Tx trace |

Scientific values are loaded from YAML by the installer before each app starts. The packet size passed to `Create<Packet>` is application payload bytes.
