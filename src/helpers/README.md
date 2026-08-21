# Helper Headers

| Header | Purpose | Inputs | Outputs |
|---|---|---|---|
| `maintenance-yaml-config.h` | Dependency-free parser and traffic-contract validation | Traffic YAML | Typed `MaintenanceTrafficConfig` or validation exception |
| `maintenance-traffic-installer.h` | Install sinks and DIAG/ALERT/FAULT clients | Typed traffic config, nodes, address, timing/RNG offsets | Application containers and typed app handles |
| `maintenance-nr-scenario-config.h` | Parse and validate radio, topology, scheduler, background, QoS, and logging groups | Scenario YAML and overrides | Typed `MaintenanceNrScenarioConfig` or validation exception |
| `maintenance-nr-output.h` | Construct collision-safe output paths and metadata | Output root, run identity, key/value data | Unique run directory, paths, metadata text |

The YAML parser intentionally supports the nested scalar subset used by this repository and fails on absent or invalid required values.
