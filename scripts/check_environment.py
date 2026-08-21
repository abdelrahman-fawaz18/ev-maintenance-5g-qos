#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check simulator compatibility without changing the environment.

Inputs: An ns-3 root containing the ns-3 and contrib/nr Git worktrees.
Outputs: Version findings on stdout and a nonzero exit for unsupported versions.
"""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def describe(path: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(path), "describe", "--tags", "--exact-match"],
        text=True,
        capture_output=True,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ns3-root", required=True)
    args = parser.parse_args()
    root = Path(args.ns3_root).expanduser().resolve()
    errors: list[str] = []
    if not (root / "ns3").is_file():
        errors.append("missing ns3 launcher")
    if not (root / "contrib" / "nr").is_dir():
        errors.append("missing contrib/nr")
    ns3_version = describe(root)
    nr_version = describe(root / "contrib" / "nr") if (root / "contrib" / "nr").is_dir() else "missing"
    if ns3_version != "ns-3.47":
        errors.append(f"expected ns-3.47, found {ns3_version}")
    if nr_version != "v4.2":
        errors.append(f"expected 5G-LENA v4.2, found {nr_version}")
    if errors:
        raise SystemExit("[FAIL] " + "; ".join(errors))
    print(f"[OK] ns-3 {ns3_version}; 5G-LENA {nr_version}")


if __name__ == "__main__":
    main()
