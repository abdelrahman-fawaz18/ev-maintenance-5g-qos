# Reproducibility Guide

## Requirements

- Linux with a C++23-capable compiler, CMake, Ninja, Git, and Python 3.
- ns-3.47 and 5G-LENA v4.2.
- Python packages declared in `requirements.txt`.

Ubuntu/Debian toolchain packages:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build ccache git python3 python3-venv \
  libeigen3-dev libgsl-dev libsqlite3-dev libxml2-dev
```

## Clean setup

```bash
git clone https://github.com/abdelrahman-fawaz18/ev-maintenance-5g-qos.git
cd ev-maintenance-5g-qos
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
./scripts/bootstrap_ns3.sh ../ns3-workspace
```

`bootstrap_ns3.sh` creates a new path at the exact supported tags and refuses to overwrite an existing path.

## Environment and tests

```bash
./reproduce.sh check --ns3-root ../ns3-workspace
```

This mode is read-only with respect to the simulator source: it verifies both Git tags and runs the repository tests.

## Fast end-to-end validation

```bash
./reproduce.sh smoke --ns3-root ../ns3-workspace --jobs 2
```

The smoke matrix contains baseline and QoS-aware runs at 0 and 30 Mbps with seed 1. It exercises installation, compilation, direct executable discovery, simulation, structural validation, matrix audit, and analysis.

## Complete experiment

```bash
./reproduce.sh paper --ns3-root ../ns3-workspace --jobs 4
```

This executes 2,600 independent simulations. `--jobs` controls concurrent simulation processes. Each cell is indexed in `artifacts/sweeps/<id>/run_index.csv`; a failed process stops the workflow and reports its log.

## Continue an interrupted sweep

```bash
./reproduce.sh paper --ns3-root ../ns3-workspace --jobs 8 \
  --resume-dir artifacts/sweeps/<existing-sweep>
```

Only cells indexed with status `ok` are reused. Deterministic generated configurations are recreated and missing cells are launched.

## Re-audit and re-analyze

```bash
./reproduce.sh analyze --sweep-dir artifacts/sweeps/<id>
```

The audit is executed before analysis. Bootstrap random streams are deterministically derived from the configured analysis seed, load, class, policy, and metric. To refresh the public evidence package after a complete audited sweep:

```bash
python3 scripts/curate_results.py --sweep-dir artifacts/sweeps/<id>
```

The curator reruns the full declared-design audit before copying files.

## Non-destructive ns-3 integration

`install_into_ns3.py` creates one project link at the ns-3 root and two scratch entry-point shims. Repeating the install is safe when those targets already resolve to this repository. Existing unrelated files are never replaced.

## Troubleshooting

- **Unsupported tag:** `check_environment.py` requires `ns-3.47` and `v4.2` exactly.
- **Occupied scratch path:** use a clean ns-3 tree or move the unrelated scratch source; the installer will not overwrite it.
- **Interrupted matrix:** retain the sweep directory and use `--resume-dir`.
- **Missing plots:** install `requirements.txt` and rerun `analyze`.
- **Storage pressure:** raw `artifacts/` are ignored by Git; preserve `results/` before removing raw runs.

The [output reference](output-reference.md) describes the resulting directory tree and file schemas.
