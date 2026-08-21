#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Connect the repository source to an ns-3 tree using non-destructive links.

Inputs: The target ns-3 root and this repository's ``src`` tree.
Outputs: Project symlinks and scratch entry-point shims inside the target ns-3 tree.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path


PROJECT_NAME = "ev-maintenance-5g-qos"


def ensure_symlink(link: Path, target: Path) -> None:
    if link.is_symlink():
        if link.resolve() != target.resolve():
            raise SystemExit(f"Refusing to replace unrelated symlink: {link}")
        return
    if link.exists():
        if link.resolve() == target.resolve():
            return
        raise SystemExit(f"Refusing to replace existing path: {link}")
    link.symlink_to(target, target_is_directory=True)


def ensure_file(path: Path, content: str) -> None:
    if path.exists():
        existing = path.read_text(encoding="utf-8")
        if existing == content:
            return
        raise SystemExit(f"Refusing to overwrite existing scratch source: {path}")
    path.write_text(content, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ns3-root", required=True)
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[1]
    ns3_root = Path(args.ns3_root).expanduser().resolve()
    if not (ns3_root / "ns3").is_file():
        raise SystemExit(f"Not an ns-3 tree: {ns3_root}")
    if not (ns3_root / "contrib" / "nr").is_dir():
        raise SystemExit(f"5G-LENA is missing under {ns3_root / 'contrib' / 'nr'}")

    ensure_symlink(ns3_root / PROJECT_NAME, project_root)
    scratch = ns3_root / "scratch"
    scratch.mkdir(exist_ok=True)
    ensure_file(
        scratch / "maintenance-nr-single-run.cc",
        "// Generated integration shim; project source remains in its own repository.\n"
        "// int main(\n"
        f'#include "../{PROJECT_NAME}/src/examples/maintenance-nr-single-run.cc"\n',
    )
    ensure_file(
        scratch / "maintenance-traffic-smoke.cc",
        "// Generated integration shim; project source remains in its own repository.\n"
        "// int main(\n"
        f'#include "../{PROJECT_NAME}/src/examples/maintenance-traffic-smoke.cc"\n',
    )
    print(f"[OK] linked {project_root} into {ns3_root}")
    print("[OK] installed scratch/maintenance-nr-single-run.cc")
    print("[OK] installed scratch/maintenance-traffic-smoke.cc")


if __name__ == "__main__":
    main()
