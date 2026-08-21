#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Execute and aggregate the declared EV-side congestion experiment matrix.

Inputs:
    Sweep, baseline, QoS-aware, and traffic YAML files plus execution overrides.
Outputs:
    Immutable per-run directories, an indexed sweep manifest, seed-level CSV data,
    aggregate summaries, plots, and logs under one timestamped sweep directory.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import re
import subprocess
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
from pathlib import Path
from typing import Any

import yaml

try:
    import matplotlib.pyplot as plt
except Exception:  # pragma: no cover - plotting is optional at runtime.
    plt = None


TRAFFIC_CLASSES = (
    "DIAG_TOTAL",
    "ALERT_TOTAL",
    "FAULT_TOTAL",
    "EV_BACKGROUND_TOTAL",
    "TOTAL",
)
PAPER_CLASSES = ("DIAG_TOTAL", "ALERT_TOTAL", "FAULT_TOTAL")
METRICS = (
    "tx_packets",
    "rx_packets",
    "lost_packets",
    "tx_bytes",
    "rx_bytes",
    "pdr",
    "mean_delay_ms",
    "delay_p95_ms",
    "deadline_violation_rate",
    "deadline_penalized_mean_ms",
    "throughput_mbps",
)
def repo_root() -> Path:
    """Return the publication repository root."""
    return Path(__file__).resolve().parents[1]


def load_yaml(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as f:
        return yaml.safe_load(f)


def write_yaml(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, sort_keys=False)


def parse_number_list(text: str | None, default: list[float]) -> list[float]:
    if not text:
        return default
    return [float(item.strip()) for item in text.split(",") if item.strip()]


def parse_int_list(text: str | None, default: list[int]) -> list[int]:
    if not text:
        return default
    return [int(item.strip()) for item in text.split(",") if item.strip()]


def load_label(value: float) -> str:
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    return str(value).replace(".", "p")


def rel_or_abs(root: Path, path_text: str) -> Path:
    path = Path(path_text)
    return path if path.is_absolute() else root / path


def now_stamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def unique_path(path: Path) -> Path:
    """Return path, or path_2/path_3/etc. if the requested folder exists."""
    if not path.exists():
        return path
    for idx in range(2, 1000):
        candidate = path.with_name(f"{path.name}_{idx}")
        if not candidate.exists():
            return candidate
    raise RuntimeError(f"could not create a unique sweep folder near {path}")


def analysis_prefix(config: dict[str, Any]) -> str:
    return str(config["experiment"]["name"])


def tee_subprocess(
    command: list[str], cwd: Path, env: dict[str, str], log_path: Path, *, echo: bool = True
) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            if echo:
                print(line, end="")
            log.write(line)
        return process.wait()


def find_latest_run_dir(output_root: Path, run_name: str) -> Path | None:
    candidates = sorted(output_root.glob(f"{run_name}_*"), key=lambda p: p.stat().st_mtime, reverse=True)
    return candidates[0] if candidates else None


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def validate_run_dir(run_dir: Path, expect_ev_background: bool) -> None:
    required = (
        "maintenance_nr_app_tx_packets.csv",
        "maintenance_nr_flow_summary.csv",
        "run_metadata.txt",
        "run_manifest.csv",
        "flow-monitor.xml",
    )
    for name in required:
        if not (run_dir / name).exists():
            raise RuntimeError(f"{run_dir} is missing {name}")

    rows = read_csv(run_dir / "maintenance_nr_flow_summary.csv")
    if not rows:
        raise RuntimeError(f"{run_dir} has an empty flow summary")
    present = {row["traffic_class"].upper() for row in rows}
    for cls in ("DIAG_TOTAL", "ALERT_TOTAL", "FAULT_TOTAL", "TOTAL"):
        if cls not in present:
            raise RuntimeError(f"{run_dir} is missing {cls}")
    if expect_ev_background and "EV_BACKGROUND_TOTAL" not in present and "EV_BACKGROUND" not in present:
        raise RuntimeError(f"{run_dir} is missing EV background flow rows")

    for row in rows:
        cls = row.get("traffic_class", "")
        if cls.upper() not in present:
            continue
        pdr = float(row.get("pdr", "0"))
        violation = float(row.get("deadline_violation_rate", "0"))
        if not 0.0 <= pdr <= 1.0:
            raise RuntimeError(f"{run_dir} {cls} PDR outside [0,1]: {pdr}")
        if not 0.0 <= violation <= 1.0:
            raise RuntimeError(f"{run_dir} {cls} deadline violation outside [0,1]: {violation}")


def metric_float(row: dict[str, Any], key: str) -> float:
    try:
        return float(row.get(key, "nan"))
    except (TypeError, ValueError):
        return math.nan


def normal_ci95(values: list[float]) -> tuple[float, float, float, float]:
    clean = [value for value in values if not math.isnan(value)]
    if not clean:
        return math.nan, math.nan, math.nan, math.nan
    mean = sum(clean) / len(clean)
    if len(clean) == 1:
        return mean, mean, mean, 0.0
    variance = sum((value - mean) ** 2 for value in clean) / (len(clean) - 1)
    sd = math.sqrt(variance)
    half_width = 1.959963984540054 * sd / math.sqrt(len(clean))
    return mean, mean - half_width, mean + half_width, sd


def write_seed_rows(
    sweep_root: Path,
    run_records: list[dict[str, Any]],
    prefix: str,
) -> list[dict[str, Any]]:
    rows_out: list[dict[str, Any]] = []
    for record in run_records:
        if record["status"] != "ok":
            continue
        flow_rows = read_csv(Path(record["run_dir"]) / "maintenance_nr_flow_summary.csv")
        has_ev_background_total = any(
            row["traffic_class"].upper() == "EV_BACKGROUND_TOTAL" for row in flow_rows
        )
        for row in flow_rows:
            cls = row["traffic_class"].upper()
            if cls == "EV_BACKGROUND" and not has_ev_background_total:
                cls = "EV_BACKGROUND_TOTAL"
            if cls not in TRAFFIC_CLASSES:
                continue
            out = {
                "scenario": record["scenario"],
                "ev_background_load_mbps": record["ev_background_load_mbps"],
                "seed": record["seed"],
                "traffic_class": cls,
                "run_dir": record["run_dir"],
            }
            for field in ("qfi", "five_qi", "rule_precedence", "latency_deadline_ms", "pdr_target"):
                out[field] = row.get(field, "")
            for metric in METRICS:
                out[metric] = row.get(metric, "")
            rows_out.append(out)

    path = sweep_root / "analysis" / f"{prefix}_seed_rows.csv"
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "scenario",
        "ev_background_load_mbps",
        "seed",
        "traffic_class",
        "qfi",
        "five_qi",
        "rule_precedence",
        "latency_deadline_ms",
        "pdr_target",
        *METRICS,
        "run_dir",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows_out)
    return rows_out


def write_summary(sweep_root: Path, seed_rows: list[dict[str, Any]], prefix: str) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, float, str], list[dict[str, Any]]] = defaultdict(list)
    for row in seed_rows:
        grouped[(row["scenario"], float(row["ev_background_load_mbps"]), row["traffic_class"])].append(row)

    summary: list[dict[str, Any]] = []
    for key in sorted(grouped, key=lambda item: (item[0], item[1], item[2])):
        scenario, load, cls = key
        rows = grouped[key]
        out: dict[str, Any] = {
            "scenario": scenario,
            "ev_background_load_mbps": load,
            "traffic_class": cls,
            "n_seeds": len(rows),
        }
        for field in ("qfi", "five_qi", "rule_precedence", "latency_deadline_ms", "pdr_target"):
            values = [row.get(field, "") for row in rows if row.get(field, "") != ""]
            out[field] = values[0] if values else ""
        for metric in METRICS:
            mean, lo, hi, sd = normal_ci95([metric_float(row, metric) for row in rows])
            out[f"{metric}_mean"] = mean
            out[f"{metric}_ci_lower"] = lo
            out[f"{metric}_ci_upper"] = hi
            out[f"{metric}_sd"] = sd
        summary.append(out)

    path = sweep_root / "analysis" / f"{prefix}_summary.csv"
    fieldnames = [
        "scenario",
        "ev_background_load_mbps",
        "traffic_class",
        "n_seeds",
        "qfi",
        "five_qi",
        "rule_precedence",
        "latency_deadline_ms",
        "pdr_target",
    ]
    for metric in METRICS:
        fieldnames.extend([f"{metric}_mean", f"{metric}_ci_lower", f"{metric}_ci_upper", f"{metric}_sd"])
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(summary)
    return summary


def fmt(value: float) -> str:
    if math.isnan(value):
        return ""
    if abs(value) >= 1000:
        return f"{value:.0f}"
    if abs(value) >= 100:
        return f"{value:.1f}"
    if abs(value) >= 10:
        return f"{value:.2f}"
    return f"{value:.3f}"


def write_compact(sweep_root: Path, summary: list[dict[str, Any]], prefix: str) -> None:
    path = sweep_root / "analysis" / f"{prefix}_compact.csv"
    fields = [
        "scenario",
        "ev_background_load_mbps",
        "traffic_class",
        "pdr",
        "mean_delay_ms",
        "delay_p95_ms",
        "deadline_violation_rate",
        "deadline_penalized_mean_ms",
        "throughput_mbps",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in summary:
            out = {
                "scenario": row["scenario"],
                "ev_background_load_mbps": row["ev_background_load_mbps"],
                "traffic_class": row["traffic_class"],
            }
            for metric in fields[3:]:
                mean = float(row[f"{metric}_mean"])
                lo = float(row[f"{metric}_ci_lower"])
                hi = float(row[f"{metric}_ci_upper"])
                out[metric] = f"{fmt(mean)} [{fmt(lo)}, {fmt(hi)}]"
            writer.writerow(out)


def plot_metric(sweep_root: Path, summary: list[dict[str, Any]], metric: str, ylabel: str, filename: str) -> None:
    if plt is None:
        return
    plot_dir = sweep_root / "analysis" / "plots"
    plot_dir.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(1, 3, figsize=(13.2, 4.0), sharex=True)
    for ax, cls in zip(axes, PAPER_CLASSES):
        for scenario in ("baseline", "slicing"):
            rows = sorted(
                [
                    row
                    for row in summary
                    if row["scenario"] == scenario and row["traffic_class"] == cls
                ],
                key=lambda row: float(row["ev_background_load_mbps"]),
            )
            if not rows:
                continue
            x = [float(row["ev_background_load_mbps"]) for row in rows]
            y = [float(row[f"{metric}_mean"]) for row in rows]
            ax.plot(x, y, linewidth=1.8, label=scenario)
        ax.set_title(cls.replace("_TOTAL", ""))
        ax.set_xlabel("EV best-effort load [Mbps]")
        ax.grid(True, alpha=0.25)
    axes[0].set_ylabel(ylabel)
    axes[-1].legend(frameon=False)
    fig.tight_layout()
    fig.savefig(plot_dir / filename, dpi=300)
    plt.close(fig)


def write_report(
    sweep_root: Path,
    config: dict[str, Any],
    run_records: list[dict[str, Any]],
    summary: list[dict[str, Any]],
    prefix: str,
) -> None:
    ok = sum(1 for row in run_records if row["status"] == "ok")
    failed = [row for row in run_records if row["status"] != "ok"]

    def row_for(scenario: str, load: float, cls: str) -> dict[str, Any] | None:
        for row in summary:
            if (
                row["scenario"] == scenario
                and abs(float(row["ev_background_load_mbps"]) - load) < 1e-9
                and row["traffic_class"] == cls
            ):
                return row
        return None

    loads = sorted({float(row["ev_background_load_mbps"]) for row in summary})
    path = sweep_root / "analysis" / f"{prefix}_report.md"
    with path.open("w", encoding="utf-8") as f:
        f.write("# EV-Side Best-Effort 10 MHz Sweep Report\n\n")
        f.write(f"- Sweep folder: `{sweep_root}`\n")
        f.write(f"- Completed runs: {ok} / {len(run_records)}\n")
        f.write(f"- Scenarios: {', '.join(config['scenarios'])}\n")
        f.write(f"- Seeds: {', '.join(str(seed) for seed in config['seeds'])}\n")
        f.write(f"- EV best-effort loads Mbps: {', '.join(str(load) for load in config['ev_background_loads_mbps'])}\n")
        f.write("- Bandwidth: 10 MHz\n")
        f.write("- External background UEs: disabled\n")
        f.write("- DIAG: default/best-effort in both scenarios\n")
        f.write("- ALERT and FAULT: dedicated QoS only in slicing\n\n")
        if failed:
            f.write("## Failed Runs\n\n")
            for row in failed:
                f.write(f"- {row['scenario']} load={row['ev_background_load_mbps']} seed={row['seed']}: {row['status']}\n")
            f.write("\n")

        for metric, title in (
            ("pdr", "PDR"),
            ("deadline_violation_rate", "Deadline Violation Rate"),
            ("deadline_penalized_mean_ms", "Deadline-Penalized Mean Delay ms"),
        ):
            f.write(f"## {title}\n\n")
            f.write("| Load Mbps | Baseline DIAG | Baseline ALERT | Baseline FAULT | Slicing DIAG | Slicing ALERT | Slicing FAULT |\n")
            f.write("|---:|---:|---:|---:|---:|---:|---:|\n")
            for load in loads:
                values = []
                for scenario in ("baseline", "slicing"):
                    for cls in PAPER_CLASSES:
                        row = row_for(scenario, load, cls)
                        values.append("-" if row is None else fmt(float(row[f"{metric}_mean"])))
                f.write(f"| {load:g} | " + " | ".join(values) + " |\n")
            f.write("\n")

        f.write("## Output Files\n\n")
        f.write(f"- `analysis/{prefix}_seed_rows.csv`\n")
        f.write(f"- `analysis/{prefix}_summary.csv`\n")
        f.write(f"- `analysis/{prefix}_compact.csv`\n")
        f.write("- `analysis/plots/`\n")
        f.write("- `run_index.csv`\n")
        f.write("- `generated_configs/`\n")
        f.write("- `runs/`\n")


def aggregate_and_plot(sweep_root: Path, config: dict[str, Any], run_records: list[dict[str, Any]]) -> None:
    prefix = analysis_prefix(config)
    seed_rows = write_seed_rows(sweep_root, run_records, prefix)
    summary = write_summary(sweep_root, seed_rows, prefix)
    write_compact(sweep_root, summary, prefix)
    plot_metric(sweep_root, summary, "pdr", "PDR", "pdr_vs_load.png")
    plot_metric(sweep_root, summary, "deadline_violation_rate", "Deadline violation rate", "deadline_violations_vs_load.png")
    plot_metric(sweep_root, summary, "deadline_penalized_mean_ms", "Deadline-penalized mean delay [ms]", "deadline_penalized_delay_vs_load.png")
    plot_metric(sweep_root, summary, "throughput_mbps", "Throughput [Mbps]", "throughput_vs_load.png")
    write_report(sweep_root, config, run_records, summary, prefix)


def write_run_index(sweep_root: Path, run_records: list[dict[str, Any]]) -> None:
    path = sweep_root / "run_index.csv"
    fieldnames = [
        "scenario",
        "ev_background_load_mbps",
        "seed",
        "status",
        "run_dir",
        "nr_config",
        "traffic_config",
        "log_file",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(run_records)


def prepare_configs(
    root: Path,
    sweep_root: Path,
    sweep_config: dict[str, Any],
    scenario: str,
    load: float,
    seed: int,
) -> tuple[str, Path, Path]:
    label = load_label(load)
    run_name = f"{sweep_config['experiment']['name']}-{scenario}-evbg{label}-seed{seed}"
    template_path = rel_or_abs(root, sweep_config["templates"][scenario])
    nr_config = load_yaml(template_path)
    traffic_template = rel_or_abs(root, nr_config["run"]["traffic_config_path"])
    traffic_config = load_yaml(traffic_template)

    traffic_config["rng"]["seed"] = int(seed)
    traffic_config["rng"]["run"] = 1

    nr_config["run"]["name"] = run_name
    nr_config["run"]["output_root"] = str(sweep_root / "runs")
    nr_config["run"]["traffic_config_path"] = str(
        sweep_root / "generated_configs" / "traffic" / f"traffic-seed{seed}.yaml"
    )
    nr_config["radio"]["bandwidth_hz"] = int(sweep_config["radio"]["bandwidth_hz"])
    if "shadowing_enabled" in sweep_config["radio"]:
        nr_config["radio"]["shadowing_enabled"] = bool(sweep_config["radio"]["shadowing_enabled"])
    nr_config["background"]["enabled"] = bool(sweep_config["external_background"]["enabled"])
    nr_config["background"]["offered_load_mbps"] = 0.0
    nr_config["ev_background"]["enabled"] = load > 0.0
    nr_config["ev_background"]["offered_load_mbps"] = float(load)
    nr_config["ev_background"]["packet_size_B"] = int(sweep_config["ev_background"]["packet_size_B"])
    nr_config["ev_background"]["udp_port"] = int(sweep_config["ev_background"]["udp_port"])
    nr_config["ev_background"]["start_s"] = float(sweep_config["ev_background"]["start_s"])
    nr_config["ev_background"]["stop_s"] = float(sweep_config["ev_background"]["stop_s"])

    traffic_path = Path(nr_config["run"]["traffic_config_path"])
    nr_path = sweep_root / "generated_configs" / "nr" / f"{run_name}.yaml"
    write_yaml(traffic_path, traffic_config)
    write_yaml(nr_path, nr_config)
    return run_name, nr_path, traffic_path


def find_simulation_executable(ns3_root: Path) -> Path:
    """Resolve the prebuilt executable so parallel runs never race a relink."""
    candidates = sorted(
        path
        for path in (ns3_root / "build" / "scratch").glob("*maintenance-nr-single-run*")
        if path.is_file() and os.access(path, os.X_OK)
    )
    if len(candidates) != 1:
        rendered = ", ".join(str(path) for path in candidates) or "none"
        raise SystemExit(
            "Expected exactly one built maintenance-nr-single-run executable under "
            f"{ns3_root / 'build' / 'scratch'}; found: {rendered}. "
            "Run ./reproduce.sh smoke first."
        )
    return candidates[0]


def run_sweep(args: argparse.Namespace) -> int:
    root = repo_root()
    ns3_root_text = args.ns3_root or os.environ.get("NS3_ROOT")
    if not ns3_root_text:
        raise SystemExit("Provide --ns3-root or set NS3_ROOT to an ns-3.47 + 5G-LENA v4.2 tree.")
    ns3_root = Path(ns3_root_text).expanduser().resolve()
    ns3_launcher = ns3_root / "ns3"
    if not ns3_launcher.is_file():
        raise SystemExit(f"ns-3 launcher not found: {ns3_launcher}")
    simulation_executable = find_simulation_executable(ns3_root)

    config_path = rel_or_abs(root, args.config)
    sweep_config = load_yaml(config_path)
    loads = parse_number_list(args.loads, [float(v) for v in sweep_config["ev_background_loads_mbps"]])
    seeds = parse_int_list(args.seeds, [int(v) for v in sweep_config["seeds"]])
    scenarios = [item.strip() for item in (args.scenarios or ",".join(sweep_config["scenarios"])).split(",") if item.strip()]
    if args.shadowing_enabled is not None:
        sweep_config["radio"]["shadowing_enabled"] = args.shadowing_enabled == "true"
    sweep_config["ev_background_loads_mbps"] = loads
    sweep_config["seeds"] = seeds
    sweep_config["scenarios"] = scenarios

    sweep_id = args.sweep_id or f"{sweep_config['experiment']['name']}_{now_stamp()}"
    requested_sweep_root = rel_or_abs(root, sweep_config["experiment"]["output_root"]) / sweep_id
    sweep_root = Path(args.resume_dir).expanduser().resolve() if args.resume_dir else unique_path(requested_sweep_root)
    if args.resume_dir and not (sweep_root / "run_index.csv").is_file():
        raise SystemExit(f"Cannot resume: run_index.csv not found under {sweep_root}")
    (sweep_root / "logs").mkdir(parents=True, exist_ok=True)
    (sweep_root / "runs").mkdir(parents=True, exist_ok=True)
    write_yaml(sweep_root / "sweep_config_resolved.yaml", sweep_config)

    env = os.environ.copy()
    env.setdefault("CCACHE_DIR", "/tmp/ccache")
    env.setdefault("MPLCONFIGDIR", "/tmp/mpl")

    jobs = max(1, int(args.jobs))
    run_records: list[dict[str, Any]] = (
        [row for row in read_csv(sweep_root / "run_index.csv") if row["status"] == "ok"]
        if args.resume_dir
        else []
    )
    completed_keys = {
        (
            row["scenario"],
            round(float(row["ev_background_load_mbps"]), 9),
            int(row["seed"]),
        )
        for row in run_records
        if row["status"] == "ok"
    }
    if completed_keys:
        print(f"[SWEEP][RESUME] keeping {len(completed_keys)} completed matrix cells", flush=True)
    pending: list[tuple[dict[str, Any], list[str], Path, str, float]] = []
    for scenario in scenarios:
        for load in loads:
            for seed in seeds:
                run_name, nr_path, traffic_path = prepare_configs(root, sweep_root, sweep_config, scenario, load, seed)
                log_file = sweep_root / "logs" / f"{run_name}.log"
                record = {
                    "scenario": scenario,
                    "ev_background_load_mbps": load,
                    "seed": seed,
                    "status": "planned" if args.dry_run else "pending",
                    "run_dir": "",
                    "nr_config": str(nr_path),
                    "traffic_config": str(traffic_path),
                    "log_file": str(log_file),
                }
                command = [str(simulation_executable), f"--nrCfg={nr_path}"]
                key = (scenario, round(float(load), 9), int(seed))
                if key in completed_keys:
                    continue
                if args.dry_run:
                    run_records.append(record)
                else:
                    pending.append((record, command, log_file, run_name, load))

    def execute_one(item: tuple[dict[str, Any], list[str], Path, str, float]) -> dict[str, Any]:
        record, command, log_file, run_name, load = item
        print(
            f"[SWEEP] {record['scenario']} load={load:g} Mbps seed={record['seed']}",
            flush=True,
        )
        code = tee_subprocess(command, ns3_root, env, log_file, echo=jobs == 1)
        status = "ok" if code == 0 else f"failed_exit_{code}"
        run_dir: Path | None = None
        if status == "ok":
            run_dir = find_latest_run_dir(sweep_root / "runs", run_name)
            if run_dir is None:
                status = "failed_missing_run_dir"
            elif sweep_config["runner"].get("validate_each_run", True):
                try:
                    validate_run_dir(run_dir, expect_ev_background=load > 0.0)
                except Exception as exc:
                    status = f"failed_validation: {exc}"
        record["status"] = status
        record["run_dir"] = "" if run_dir is None else str(run_dir)
        return record

    if args.dry_run:
        write_run_index(sweep_root, run_records)
    elif jobs == 1:
        for item in pending:
            record = execute_one(item)
            run_records.append(record)
            write_run_index(sweep_root, run_records)
            if record["status"] != "ok":
                print(f"[SWEEP][FAIL] {record['status']}", file=sys.stderr)
                if sweep_config["runner"].get("stop_on_failure", True):
                    return 1
    else:
        failed = False
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(execute_one, item) for item in pending]
            for future in as_completed(futures):
                record = future.result()
                run_records.append(record)
                run_records.sort(
                    key=lambda row: (
                        scenarios.index(row["scenario"]),
                        float(row["ev_background_load_mbps"]),
                        int(row["seed"]),
                    )
                )
                write_run_index(sweep_root, run_records)
                print(
                    f"[SWEEP][{record['status'].upper()}] {record['scenario']} "
                    f"load={record['ev_background_load_mbps']:g} seed={record['seed']}",
                    flush=True,
                )
                failed = failed or record["status"] != "ok"
        if failed and sweep_config["runner"].get("stop_on_failure", True):
            return 1

    if not args.dry_run:
        aggregate_and_plot(sweep_root, sweep_config, run_records)
    print(f"[SWEEP][DONE] {sweep_root}")
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        default="config/paper-sweep.yaml",
        help="Sweep YAML path, relative to this repository or absolute.",
    )
    parser.add_argument("--ns3-root", help="ns-3.47 tree containing 5G-LENA v4.2.")
    parser.add_argument("--jobs", type=int, default=1, help="Number of independent runs to execute concurrently.")
    parser.add_argument("--seeds", help="Comma-separated seed override, e.g. 1 or 1,2,3.")
    parser.add_argument("--loads", help="Comma-separated EV best-effort load override in Mbps.")
    parser.add_argument("--scenarios", help="Comma-separated scenario override: baseline,slicing.")
    parser.add_argument(
        "--shadowing-enabled",
        choices=("true", "false"),
        help="Optional radio-shadowing override for sensitivity analysis.",
    )
    parser.add_argument("--sweep-id", help="Optional parent output folder name.")
    parser.add_argument(
        "--resume-dir",
        help="Continue an existing sweep, retaining completed OK cells and running only missing cells.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Generate configs and run index without launching ns-3.")
    raise SystemExit(run_sweep(parser.parse_args()))


if __name__ == "__main__":
    main()
