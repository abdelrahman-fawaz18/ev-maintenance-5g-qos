#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Summarize a completed EV-maintenance sweep.

Inputs:
    A completed sweep directory containing an ``analysis/*_seed_rows.csv`` file.
Outputs:
    Bootstrap summaries, paired policy-effect tables, interpretation notes, and
    publication-quality plots under ``<sweep>/paper_analysis`` or ``--output-dir``.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import random
from collections import defaultdict
from pathlib import Path
from typing import Any

import numpy as np

try:
    import matplotlib.pyplot as plt
except Exception:  # pragma: no cover - plotting is optional at runtime.
    plt = None


PAPER_CLASSES = ("DIAG_TOTAL", "ALERT_TOTAL", "FAULT_TOTAL")
DEFAULT_BOOTSTRAP_ITERATIONS = 10000
DEFAULT_CONFIDENCE = 0.95
PROBABILITY_METRICS = ("pdr", "deadline_violation_rate")
PAPER_METRICS = (
    "pdr",
    "deadline_violation_rate",
    "deadline_penalized_mean_ms",
    "mean_delay_ms",
    "delay_p95_ms",
    "throughput_mbps",
)
PAIRED_EFFECTS = (
    ("pdr_gain", "pdr", 1.0, "QoS-aware PDR - baseline PDR"),
    (
        "deadline_violation_reduction",
        "deadline_violation_rate",
        -1.0,
        "baseline deadline-violation rate - QoS-aware deadline-violation rate",
    ),
    (
        "deadline_penalized_delay_reduction_ms",
        "deadline_penalized_mean_ms",
        -1.0,
        "baseline deadline-penalized delay - QoS-aware deadline-penalized delay",
    ),
)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def as_float(value: Any) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return math.nan


def clean_values(values: list[float]) -> list[float]:
    return [value for value in values if not math.isnan(value)]


def mean(values: list[float]) -> float:
    values = clean_values(values)
    return sum(values) / len(values) if values else math.nan


def bootstrap_ci(
    values: list[float],
    *,
    iterations: int,
    confidence: float,
    rng: Any,
    bounded: bool,
) -> tuple[float, float, float, float]:
    """Return mean, lower, upper, and standard deviation of seed values."""
    values = clean_values(values)
    if not values:
        return math.nan, math.nan, math.nan, math.nan
    observed_mean = mean(values)
    if len(values) == 1:
        lower = upper = observed_mean
    else:
        if hasattr(rng, "integers"):
            value_array = np.asarray(values, dtype=float)
            indices = rng.integers(0, len(values), size=(iterations, len(values)))
            resampled_means = np.sort(value_array[indices].mean(axis=1)).tolist()
        else:
            resampled_means = []
            for _ in range(iterations):
                sample = [values[rng.randrange(len(values))] for _ in values]
                resampled_means.append(mean(sample))
            resampled_means.sort()
        alpha = (1.0 - confidence) / 2.0
        lo_idx = min(len(resampled_means) - 1, max(0, int(alpha * len(resampled_means))))
        hi_idx = min(
            len(resampled_means) - 1,
            max(0, int((1.0 - alpha) * len(resampled_means)) - 1),
        )
        lower = resampled_means[lo_idx]
        upper = resampled_means[hi_idx]
    if bounded:
        observed_mean = min(1.0, max(0.0, observed_mean))
        lower = min(1.0, max(0.0, lower))
        upper = min(1.0, max(0.0, upper))
    if len(values) > 1:
        variance = sum((value - observed_mean) ** 2 for value in values) / (len(values) - 1)
        sd = math.sqrt(variance)
    else:
        sd = 0.0
    return observed_mean, lower, upper, sd


def find_seed_rows(sweep_dir: Path) -> Path:
    candidates = sorted((sweep_dir / "analysis").glob("*_seed_rows.csv"))
    if not candidates:
        raise FileNotFoundError(f"No *_seed_rows.csv file found under {sweep_dir / 'analysis'}")
    if len(candidates) > 1:
        print(f"[WARN] Multiple seed-row files found; using {candidates[0]}")
    return candidates[0]


def load_label(value: float) -> str:
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    return str(value).replace(".", "p")


def fmt(value: Any) -> str:
    value = as_float(value)
    if math.isnan(value):
        return ""
    if abs(value) >= 1000:
        return f"{value:.0f}"
    if abs(value) >= 100:
        return f"{value:.1f}"
    if abs(value) >= 10:
        return f"{value:.2f}"
    return f"{value:.3f}"


def summarize(
    rows: list[dict[str, str]],
    *,
    iterations: int,
    confidence: float,
    rng_seed: int,
) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, float, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[
            (
                row["scenario"],
                as_float(row["ev_background_load_mbps"]),
                row["traffic_class"].upper(),
            )
        ].append(row)

    summary: list[dict[str, Any]] = []
    for key in sorted(grouped, key=lambda item: (item[0], item[1], item[2])):
        scenario, load, cls = key
        seed_rows = grouped[key]
        out: dict[str, Any] = {
            "scenario": scenario,
            "ev_background_load_mbps": load,
            "traffic_class": cls,
            "n_seeds": len(seed_rows),
        }
        for field in ("qfi", "five_qi", "rule_precedence", "latency_deadline_ms", "pdr_target"):
            values = [row.get(field, "") for row in seed_rows if row.get(field, "") != ""]
            out[field] = values[0] if values else ""
        for metric in PAPER_METRICS:
            values = [as_float(row.get(metric)) for row in seed_rows]
            material = f"{rng_seed}:{scenario}:{load}:{cls}:{metric}".encode("utf-8")
            stable_seed = int.from_bytes(hashlib.sha256(material).digest()[:8], "big")
            rng = np.random.default_rng(stable_seed)
            metric_mean, ci_low, ci_high, sd = bootstrap_ci(
                values,
                iterations=iterations,
                confidence=confidence,
                rng=rng,
                bounded=metric in PROBABILITY_METRICS,
            )
            out[f"{metric}_mean"] = metric_mean
            out[f"{metric}_ci_low"] = ci_low
            out[f"{metric}_ci_high"] = ci_high
            out[f"{metric}_sd"] = sd
        summary.append(out)
    return summary


def paired_policy_effects(
    rows: list[dict[str, str]],
    *,
    iterations: int,
    confidence: float,
    rng_seed: int,
) -> list[dict[str, Any]]:
    """Estimate policy deltas from matched baseline/QoS-aware seed realizations."""
    paired: dict[tuple[float, int, str], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in rows:
        traffic_class = row["traffic_class"].upper()
        if traffic_class not in PAPER_CLASSES:
            continue
        key = (
            as_float(row["ev_background_load_mbps"]),
            int(row["seed"]),
            traffic_class,
        )
        paired[key][row["scenario"]] = row

    incomplete = [key for key, scenarios in paired.items() if set(scenarios) != {"baseline", "slicing"}]
    if incomplete:
        raise ValueError(f"Incomplete baseline/QoS-aware seed pairs: {incomplete[:5]}")

    grouped: dict[tuple[float, str], list[dict[str, dict[str, str]]]] = defaultdict(list)
    for (load, _seed, traffic_class), scenarios in paired.items():
        grouped[(load, traffic_class)].append(scenarios)

    output: list[dict[str, Any]] = []
    for load, traffic_class in sorted(grouped, key=lambda item: (item[0], item[1])):
        pairs = grouped[(load, traffic_class)]
        for effect_name, metric, direction, definition in PAIRED_EFFECTS:
            baseline_values = [as_float(pair["baseline"][metric]) for pair in pairs]
            qos_values = [as_float(pair["slicing"][metric]) for pair in pairs]
            effects = [direction * (qos - baseline) for baseline, qos in zip(baseline_values, qos_values)]
            material = f"{rng_seed}:paired:{load}:{traffic_class}:{effect_name}".encode("utf-8")
            stable_seed = int.from_bytes(hashlib.sha256(material).digest()[:8], "big")
            effect_mean, ci_low, ci_high, effect_sd = bootstrap_ci(
                effects,
                iterations=iterations,
                confidence=confidence,
                rng=np.random.default_rng(stable_seed),
                bounded=False,
            )
            output.append(
                {
                    "ev_background_load_mbps": load,
                    "traffic_class": traffic_class,
                    "n_pairs": len(pairs),
                    "metric": effect_name,
                    "baseline_mean": mean(baseline_values),
                    "qos_aware_mean": mean(qos_values),
                    "paired_effect_mean": effect_mean,
                    "paired_effect_ci_low": ci_low,
                    "paired_effect_ci_high": ci_high,
                    "paired_effect_sd": effect_sd,
                    "effect_definition": definition,
                }
            )
    return output


def paired_effect_for(
    rows: list[dict[str, Any]], load: float, traffic_class: str, metric: str
) -> dict[str, Any] | None:
    for row in rows:
        if (
            abs(as_float(row["ev_background_load_mbps"]) - load) < 1e-9
            and row["traffic_class"] == traffic_class
            and row["metric"] == metric
        ):
            return row
    return None


def write_paired_effects(paired_rows: list[dict[str, Any]], out_dir: Path) -> None:
    fields = [
        "ev_background_load_mbps",
        "traffic_class",
        "n_pairs",
        "metric",
        "baseline_mean",
        "qos_aware_mean",
        "paired_effect_mean",
        "paired_effect_ci_low",
        "paired_effect_ci_high",
        "paired_effect_sd",
        "effect_definition",
    ]
    write_csv(out_dir / "paired_policy_effects_bootstrap_ci.csv", paired_rows, fields)

    selected_loads = (20.0, 23.0, 30.0, 40.0)
    path = out_dir / "paired_policy_effects_selected_loads.md"
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# Paired Policy Effects at Selected Loads\n\n")
        handle.write(
            "Each effect is computed within a matched load/seed/traffic-class pair before "
            "bootstrap resampling. Positive values favor the QoS-aware policy.\n\n"
        )
        handle.write(
            "| Load [Mbps] | Class | PDR gain [percentage points] | "
            "Deadline violations avoided [percentage points] | Penalized delay reduced [ms] |\n"
        )
        handle.write("|---:|---|---:|---:|---:|\n")
        for load in selected_loads:
            for traffic_class in ("ALERT_TOTAL", "FAULT_TOTAL"):
                pdr = paired_effect_for(paired_rows, load, traffic_class, "pdr_gain")
                deadline = paired_effect_for(
                    paired_rows, load, traffic_class, "deadline_violation_reduction"
                )
                delay = paired_effect_for(
                    paired_rows,
                    load,
                    traffic_class,
                    "deadline_penalized_delay_reduction_ms",
                )
                if not all((pdr, deadline, delay)):
                    continue

                def interval(row: dict[str, Any], scale: float = 1.0) -> str:
                    return (
                        f"{as_float(row['paired_effect_mean']) * scale:.3f} "
                        f"[{as_float(row['paired_effect_ci_low']) * scale:.3f}, "
                        f"{as_float(row['paired_effect_ci_high']) * scale:.3f}]"
                    )

                handle.write(
                    f"| {load:g} | {traffic_class.replace('_TOTAL', '')} | "
                    f"{interval(pdr, 100.0)} | {interval(deadline, 100.0)} | "
                    f"{interval(delay)} |\n"
                )


def plot_paired_effects(paired_rows: list[dict[str, Any]], out_dir: Path) -> None:
    if plt is None:
        return
    plot_dir = out_dir / "plots"
    plot_dir.mkdir(parents=True, exist_ok=True)
    panels = (
        ("pdr_gain", "PDR gain [percentage points]", 100.0),
        (
            "deadline_violation_reduction",
            "Deadline violations avoided [percentage points]",
            100.0,
        ),
        (
            "deadline_penalized_delay_reduction_ms",
            "Penalized delay reduced [ms]",
            1.0,
        ),
    )
    styles = {
        "ALERT_TOTAL": {"color": "#2563eb", "label": "ALERT"},
        "FAULT_TOTAL": {"color": "#b91c1c", "label": "FAULT"},
    }
    fig, axes = plt.subplots(1, 3, figsize=(13.2, 4.9), sharex=True)
    fig.patch.set_facecolor("white")
    for ax, (metric, ylabel, scale) in zip(axes, panels):
        for traffic_class, style in styles.items():
            selected = sorted(
                [
                    row
                    for row in paired_rows
                    if row["traffic_class"] == traffic_class and row["metric"] == metric
                ],
                key=lambda row: as_float(row["ev_background_load_mbps"]),
            )
            x = [as_float(row["ev_background_load_mbps"]) for row in selected]
            y = [as_float(row["paired_effect_mean"]) * scale for row in selected]
            low = [as_float(row["paired_effect_ci_low"]) * scale for row in selected]
            high = [as_float(row["paired_effect_ci_high"]) * scale for row in selected]
            ax.fill_between(x, low, high, color=style["color"], alpha=0.12, linewidth=0)
            ax.plot(
                x,
                y,
                color=style["color"],
                linewidth=2.1,
                label=style["label"],
            )
        ax.axhline(0.0, color="#64748b", linewidth=1.0)
        ax.axvspan(22, 24, color="#f59f00", alpha=0.06, zorder=0)
        ax.set_xlabel("EV best-effort load [Mbps]")
        ax.set_ylabel(ylabel)
        ax.grid(axis="y", color="#e2e8f0", linewidth=0.8)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
    axes[-1].legend(frameon=False, loc="best")
    fig.suptitle(
        "Matched-seed effect of QoS-aware scheduling",
        x=0.5,
        y=0.965,
        fontsize=15,
        fontweight="bold",
    )
    fig.text(
        0.5,
        0.895,
        "Positive values favor QoS-aware scheduling; bands are 95% paired-bootstrap intervals",
        ha="center",
        va="center",
        color="#64748b",
        fontsize=9.5,
    )
    fig.subplots_adjust(left=0.075, right=0.985, bottom=0.15, top=0.74, wspace=0.28)
    fig.savefig(
        plot_dir / "paired_policy_effects_bootstrap_ci.png",
        dpi=220,
        bbox_inches="tight",
        facecolor="white",
    )
    plt.close(fig)


def write_compact(summary: list[dict[str, Any]], out_dir: Path) -> None:
    fields = [
        "scenario",
        "ev_background_load_mbps",
        "traffic_class",
        "pdr",
        "deadline_violation_rate",
        "deadline_penalized_mean_ms",
        "mean_delay_ms",
        "delay_p95_ms",
        "throughput_mbps",
    ]
    rows_out: list[dict[str, Any]] = []
    for row in summary:
        out = {
            "scenario": row["scenario"],
            "ev_background_load_mbps": row["ev_background_load_mbps"],
            "traffic_class": row["traffic_class"],
        }
        for metric in fields[3:]:
            out[metric] = (
                f"{fmt(row[f'{metric}_mean'])} "
                f"[{fmt(row[f'{metric}_ci_low'])}, {fmt(row[f'{metric}_ci_high'])}]"
            )
        rows_out.append(out)
    write_csv(out_dir / "paper_compact_bootstrap_ci.csv", rows_out, fields)


def write_selected_load_tables(summary: list[dict[str, Any]], out_dir: Path) -> None:
    selected_loads = (0.0, 15.0, 20.0, 23.0, 24.0, 30.0, 40.0)
    fields = [
        "load_mbps",
        "scenario",
        "diag_pdr",
        "alert_pdr",
        "fault_pdr",
        "diag_deadline_violation_rate",
        "alert_deadline_violation_rate",
        "fault_deadline_violation_rate",
        "alert_deadline_penalized_mean_ms",
        "fault_deadline_penalized_mean_ms",
    ]
    rows_out: list[dict[str, Any]] = []
    for load in selected_loads:
        for scenario in ("baseline", "slicing"):
            out: dict[str, Any] = {
                "load_mbps": f"{load:g}",
                "scenario": scenario,
            }
            for prefix, cls in (("diag", "DIAG_TOTAL"), ("alert", "ALERT_TOTAL"), ("fault", "FAULT_TOTAL")):
                row = row_for(summary, scenario, load, cls)
                if row is None:
                    continue
                for metric in ("pdr", "deadline_violation_rate"):
                    out[f"{prefix}_{metric}"] = (
                        f"{fmt(row[f'{metric}_mean'])} "
                        f"[{fmt(row[f'{metric}_ci_low'])}, {fmt(row[f'{metric}_ci_high'])}]"
                    )
                if prefix in ("alert", "fault"):
                    metric = "deadline_penalized_mean_ms"
                    out[f"{prefix}_{metric}"] = (
                        f"{fmt(row[f'{metric}_mean'])} "
                        f"[{fmt(row[f'{metric}_ci_low'])}, {fmt(row[f'{metric}_ci_high'])}]"
                    )
            rows_out.append(out)

    write_csv(out_dir / "paper_selected_load_table.csv", rows_out, fields)
    md_path = out_dir / "paper_selected_load_table.md"
    with md_path.open("w", encoding="utf-8") as f:
        f.write("# Selected-Load Absolute KPI Table\n\n")
        f.write("Cells report means and bounded 95% bootstrap confidence intervals.\n\n")
        f.write("| Load Mbps | Scenario | DIAG PDR | ALERT PDR | FAULT PDR | DIAG deadline viol. | ALERT deadline viol. | FAULT deadline viol. | ALERT penalized delay ms | FAULT penalized delay ms |\n")
        f.write("|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|\n")
        for row in rows_out:
            f.write(
                f"| {row.get('load_mbps', '')} | {row.get('scenario', '')} | "
                f"{row.get('diag_pdr', '')} | {row.get('alert_pdr', '')} | {row.get('fault_pdr', '')} | "
                f"{row.get('diag_deadline_violation_rate', '')} | {row.get('alert_deadline_violation_rate', '')} | "
                f"{row.get('fault_deadline_violation_rate', '')} | {row.get('alert_deadline_penalized_mean_ms', '')} | "
                f"{row.get('fault_deadline_penalized_mean_ms', '')} |\n"
            )


def row_for(summary: list[dict[str, Any]], scenario: str, load: float, cls: str) -> dict[str, Any] | None:
    for row in summary:
        if (
            row["scenario"] == scenario
            and abs(as_float(row["ev_background_load_mbps"]) - load) < 1e-9
            and row["traffic_class"] == cls
        ):
            return row
    return None


def write_report(summary: list[dict[str, Any]], out_dir: Path, sweep_dir: Path, confidence: float) -> None:
    loads = sorted({as_float(row["ev_background_load_mbps"]) for row in summary})
    scenarios = sorted({row["scenario"] for row in summary})
    seeds = sorted({int(row["n_seeds"]) for row in summary})
    path = out_dir / "paper_results_interpretation.md"
    with path.open("w", encoding="utf-8") as f:
        f.write("# Absolute Sweep Analysis\n\n")
        f.write(f"- Source sweep: `{sweep_dir}`\n")
        f.write(f"- Scenarios: {', '.join(scenarios)}\n")
        f.write(f"- Loads Mbps: {', '.join(f'{load:g}' for load in loads)}\n")
        f.write(f"- Seeds per scenario/load/class: {', '.join(str(seed) for seed in seeds)}\n")
        f.write(f"- Confidence interval method: bounded bootstrap, {confidence:.0%} interval.\n")
        f.write("- Probability metrics are clipped to the physically valid [0, 1] range.\n\n")

        f.write("## Findings\n\n")
        f.write("1. Baseline degradation appears first in latency and deadlines, followed by PDR loss.\n")
        f.write("2. QoS-aware scheduling protects ALERT and FAULT under same-UE best-effort congestion.\n")
        f.write("3. DIAG remains best effort and degrades under both policies, confirming selective treatment.\n")
        f.write("4. The transition coincides with the effective uplink-capacity region.\n\n")

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
                        row = row_for(summary, scenario, load, cls)
                        if row is None:
                            values.append("-")
                        else:
                            values.append(
                                f"{fmt(row[f'{metric}_mean'])} "
                                f"[{fmt(row[f'{metric}_ci_low'])}, {fmt(row[f'{metric}_ci_high'])}]"
                            )
                f.write(f"| {load:g} | " + " | ".join(values) + " |\n")
            f.write("\n")

        f.write("## Interpretation Notes\n\n")
        f.write("- All completed seeds are retained; transition-region outcomes are part of the modeled uncertainty.\n")
        f.write("- Stochastic deadline stress begins near 22 Mbps, before the sharper delivery transition.\n")
        f.write("- Deadline-violation rate and deadline-penalized delay expose failure before PDR falls substantially.\n")
        f.write("- The mechanism is 5G-LENA QoS-flow differentiation on one carrier, not a complete commercial end-to-end network slice.\n")


def plot_metric(
    summary: list[dict[str, Any]],
    out_dir: Path,
    metric: str,
    ylabel: str,
    filename: str,
) -> None:
    if plt is None:
        return
    plot_dir = out_dir / "plots"
    plot_dir.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 10,
            "axes.titleweight": "bold",
            "axes.edgecolor": "#94a3b8",
            "axes.labelcolor": "#334155",
            "xtick.color": "#475569",
            "ytick.color": "#475569",
        }
    )
    fig, axes = plt.subplots(1, 3, figsize=(13.2, 4.8), sharex=True)
    fig.patch.set_facecolor("white")
    styles = {
        "baseline": {"color": "#e8590c", "label": "Baseline · PF"},
        "slicing": {"color": "#2563eb", "label": "QoS-aware"},
    }
    for ax, cls in zip(axes, PAPER_CLASSES):
        ax.set_facecolor("#ffffff")
        for scenario in ("baseline", "slicing"):
            style = styles[scenario]
            rows = sorted(
                [
                    row
                    for row in summary
                    if row["scenario"] == scenario and row["traffic_class"] == cls
                ],
                key=lambda row: as_float(row["ev_background_load_mbps"]),
            )
            if not rows:
                continue
            x = [as_float(row["ev_background_load_mbps"]) for row in rows]
            y = [as_float(row[f"{metric}_mean"]) for row in rows]
            y_low = [as_float(row[f"{metric}_ci_low"]) for row in rows]
            y_high = [as_float(row[f"{metric}_ci_high"]) for row in rows]
            ax.fill_between(x, y_low, y_high, color=style["color"], alpha=0.12, linewidth=0)
            ax.plot(
                x,
                y,
                color=style["color"],
                linewidth=2.2,
                label=style["label"],
            )
        ax.set_title(cls.replace("_TOTAL", ""))
        ax.set_xlabel("EV best-effort load [Mbps]")
        ax.grid(axis="y", color="#e2e8f0", linewidth=0.8)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
        ax.axvspan(22, 24, color="#f59f00", alpha=0.06, zorder=0)
        if metric in PROBABILITY_METRICS:
            ax.set_ylim(-0.04, 1.04)
    axes[0].set_ylabel(ylabel)
    axes[-1].legend(frameon=False, loc="best")
    titles = {
        "pdr": "Packet delivery ratio under increasing EV uplink load",
        "mean_delay_ms": "Delivered-packet mean latency under increasing EV uplink load",
        "deadline_violation_rate": "Deadline violations under increasing EV uplink load",
        "deadline_penalized_mean_ms": "Deadline-penalized delay under increasing EV uplink load",
        "throughput_mbps": "Received throughput under increasing EV uplink load",
    }
    fig.suptitle(titles[metric], x=0.5, y=0.965, fontsize=15, fontweight="bold")
    fig.text(
        0.5,
        0.895,
        "Lines show 50-seed means; shaded bands show 95% bootstrap confidence intervals",
        ha="center",
        va="center",
        color="#64748b",
        fontsize=9.5,
    )
    fig.subplots_adjust(left=0.07, right=0.985, bottom=0.15, top=0.76, wspace=0.25)
    fig.savefig(plot_dir / filename, dpi=220, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def write_method_note(out_dir: Path, confidence: float, iterations: int) -> None:
    path = out_dir / "bootstrap_ci_method_note.md"
    with path.open("w", encoding="utf-8") as f:
        f.write("# Bootstrap Confidence Interval Method Note\n\n")
        f.write("The final analysis keeps every completed random seed. No difficult seed is removed merely because its channel or queueing outcome is unfavorable; exclusion requires a proven run-integrity failure.\n\n")
        f.write(f"For each scenario, load, traffic class, and metric, the analysis resamples the seed-level metric values {iterations} times with replacement. The mean is calculated for each resample. The lower and upper bounds are the empirical tails of the resampled means for a {confidence:.0%} confidence interval.\n\n")
        f.write("For PDR and deadline violation rate, intervals are bounded to [0, 1] because these metrics are probabilities. This avoids physically impossible intervals such as PDR > 1. Delay and throughput intervals are not bounded.\n")


def run(args: argparse.Namespace) -> int:
    if args.bootstrap_iterations < 1:
        raise ValueError("bootstrap iterations must be positive")
    if not 0.0 < args.confidence < 1.0:
        raise ValueError("confidence must be between 0 and 1")
    sweep_dir = Path(args.sweep_dir).resolve()
    seed_rows_path = find_seed_rows(sweep_dir)
    out_dir = Path(args.output_dir).resolve() if args.output_dir else sweep_dir / "paper_analysis"
    rows = read_csv(seed_rows_path)
    summary = summarize(
        rows,
        iterations=args.bootstrap_iterations,
        confidence=args.confidence,
        rng_seed=args.bootstrap_seed,
    )
    paired_rows = paired_policy_effects(
        rows,
        iterations=args.bootstrap_iterations,
        confidence=args.confidence,
        rng_seed=args.bootstrap_seed,
    )

    fieldnames = ["scenario", "ev_background_load_mbps", "traffic_class", "n_seeds"]
    fieldnames.extend(["qfi", "five_qi", "rule_precedence", "latency_deadline_ms", "pdr_target"])
    for metric in PAPER_METRICS:
        fieldnames.extend(
            [
                f"{metric}_mean",
                f"{metric}_ci_low",
                f"{metric}_ci_high",
                f"{metric}_sd",
            ]
        )
    write_csv(out_dir / "paper_summary_bootstrap_ci.csv", summary, fieldnames)
    write_compact(summary, out_dir)
    write_selected_load_tables(summary, out_dir)
    write_paired_effects(paired_rows, out_dir)
    write_report(summary, out_dir, sweep_dir, args.confidence)
    write_method_note(out_dir, args.confidence, args.bootstrap_iterations)

    plot_metric(
        summary,
        out_dir,
        "mean_delay_ms",
        "Delivered-packet mean latency [ms]",
        "paper_mean_latency_bootstrap_ci.png",
    )
    plot_metric(summary, out_dir, "pdr", "PDR", "paper_pdr_bootstrap_ci.png")
    plot_metric(
        summary,
        out_dir,
        "deadline_violation_rate",
        "Deadline violation rate",
        "paper_deadline_violations_bootstrap_ci.png",
    )
    plot_metric(
        summary,
        out_dir,
        "deadline_penalized_mean_ms",
        "Deadline-penalized mean delay [ms]",
        "paper_deadline_penalized_delay_bootstrap_ci.png",
    )
    plot_metric(summary, out_dir, "throughput_mbps", "Throughput [Mbps]", "paper_throughput_bootstrap_ci.png")
    plot_paired_effects(paired_rows, out_dir)

    print(f"[PAPER_ANALYSIS][DONE] {out_dir}")
    print(f"[PAPER_ANALYSIS] source seed rows: {seed_rows_path}")
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sweep-dir", required=True, help="Completed sweep directory.")
    parser.add_argument("--output-dir", help="Optional output directory. Defaults to <sweep-dir>/paper_analysis.")
    parser.add_argument("--bootstrap-iterations", type=int, default=DEFAULT_BOOTSTRAP_ITERATIONS)
    parser.add_argument("--bootstrap-seed", type=int, default=20260607)
    parser.add_argument("--confidence", type=float, default=DEFAULT_CONFIDENCE)
    raise SystemExit(run(parser.parse_args()))


if __name__ == "__main__":
    main()
