#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Coordinate the complete one-command reproduction workflow.

Inputs:
    An ns-3 root, execution mode, parallelism, and optional sweep-resume directory.
Outputs:
    A built simulator, validated smoke run, audited sweep, analysis products, and
    curated repository results according to the selected mode.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NS3_MODULES = "nr;flow-monitor;applications;point-to-point;mobility;internet"


def run(command: list[str], *, cwd: Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def install_and_build(ns3_root: Path) -> None:
    run([sys.executable, str(ROOT / "scripts/check_environment.py"), "--ns3-root", str(ns3_root)])
    run([sys.executable, str(ROOT / "scripts/install_into_ns3.py"), "--ns3-root", str(ns3_root)])
    run(
        [
            str(ns3_root / "ns3"),
            "configure",
            "--build-profile=optimized",
            "--enable-examples",
            f"--enable-modules={NS3_MODULES}",
        ],
        cwd=ns3_root,
    )
    run(
        [
            str(ns3_root / "ns3"),
            "build",
            "maintenance-nr-single-run",
            "maintenance-traffic-smoke",
        ],
        cwd=ns3_root,
    )


def latest_sweep() -> Path:
    candidates = sorted(
        (ROOT / "artifacts" / "sweeps").glob("evside_10mhz_ofdma_stochastic_transition_*"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        raise SystemExit("No sweep output found")
    return candidates[0]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("check", "smoke", "paper", "analyze"))
    parser.add_argument("--ns3-root", default=os.environ.get("NS3_ROOT"))
    parser.add_argument("--jobs", type=int, default=max(1, min(4, os.cpu_count() or 1)))
    parser.add_argument("--sweep-dir")
    parser.add_argument("--resume-dir")
    args = parser.parse_args()

    if args.mode == "analyze":
        sweep = Path(args.sweep_dir).resolve() if args.sweep_dir else latest_sweep()
        run(
            [
                sys.executable,
                str(ROOT / "scripts/audit_sweep.py"),
                "--sweep-dir",
                str(sweep),
                "--design",
                str(sweep / "sweep_config_resolved.yaml"),
            ]
        )
        run([sys.executable, str(ROOT / "scripts/analyze_results.py"), "--sweep-dir", str(sweep)])
        return

    if not args.ns3_root:
        raise SystemExit("Provide --ns3-root or set NS3_ROOT")
    ns3_root = Path(args.ns3_root).expanduser().resolve()
    run([sys.executable, str(ROOT / "scripts/check_environment.py"), "--ns3-root", str(ns3_root)])
    run([sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tests"), "-v"])
    if args.mode == "check":
        return

    install_and_build(ns3_root)
    sweep_command = [
        sys.executable,
        str(ROOT / "scripts/run_sweep.py"),
        "--config",
        str(ROOT / "config/paper-sweep.yaml"),
        "--ns3-root",
        str(ns3_root),
        "--jobs",
        str(args.jobs),
    ]
    if args.mode == "smoke":
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        sweep_command.extend(
            ["--loads", "0,30", "--seeds", "1", "--sweep-id", f"smoke_{stamp}"]
        )
    elif args.resume_dir:
        sweep_command.extend(["--resume-dir", str(Path(args.resume_dir).expanduser().resolve())])
    run(sweep_command)
    sweep = latest_sweep() if args.mode == "paper" else max(
        (ROOT / "artifacts" / "sweeps").glob("smoke_*"),
        key=lambda path: path.stat().st_mtime,
    )
    run(
        [
            sys.executable,
            str(ROOT / "scripts/audit_sweep.py"),
            "--sweep-dir",
            str(sweep),
            "--design",
            str(sweep / "sweep_config_resolved.yaml"),
        ]
    )
    run([sys.executable, str(ROOT / "scripts/analyze_results.py"), "--sweep-dir", str(sweep)])
    if args.mode == "paper":
        run([sys.executable, str(ROOT / "scripts/curate_results.py"), "--sweep-dir", str(sweep)])


if __name__ == "__main__":
    main()
