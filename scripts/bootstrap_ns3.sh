#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Purpose: Create a clean simulator tree at the supported ns-3 and 5G-LENA tags.
# Inputs: One absolute, nonexistent destination path.
# Outputs: A new ns-3.47 tree with 5G-LENA v4.2 under contrib/nr.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 /absolute/path/to/new/ns-3.47-tree" >&2
  exit 2
fi

ns3_target="$1"
if [[ -e "$ns3_target" ]]; then
  echo "Refusing to overwrite existing path: $ns3_target" >&2
  exit 1
fi

git clone --branch ns-3.47 --depth 1 https://gitlab.com/nsnam/ns-3-dev.git "$ns3_target"
git clone --branch v4.2 --depth 1 https://gitlab.com/cttc-lena/nr.git "$ns3_target/contrib/nr"
echo "Created ns-3.47 + 5G-LENA v4.2 at $ns3_target"
