#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Purpose: Stable command-line entry point for the complete reproduction workflow.
# Inputs: All arguments accepted by scripts/reproduce.py.
# Outputs: Delegates build, smoke, sweep, audit, analysis, and curation artifacts.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$project_dir/scripts/reproduce.py" "$@"
