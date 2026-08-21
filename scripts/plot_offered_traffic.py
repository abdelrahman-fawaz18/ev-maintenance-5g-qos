#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Render application-layer offered traffic from a simulation packet trace.

Inputs:
    ``maintenance_nr_app_tx_packets.csv`` and an optional bin width.
Outputs:
    A four-panel DIAG/ALERT/FAULT/TOTAL figure and a binned summary CSV.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt


CLASSES = ("DIAG", "ALERT", "FAULT")
PANEL_CLASSES = ("DIAG", "ALERT", "FAULT", "TOTAL")
ALERT_WINDOW_S = (15.0, 35.0)
FAULT_WINDOW_S = (20.0, 50.0)
COLORS = {
    "DIAG": "#1f77b4",
    "ALERT": "#d62728",
    "FAULT": "#2ca02c",
    "TOTAL": "#111111",
}
LABELS = {
    "DIAG": "DIAG",
    "ALERT": "ALERT",
    "FAULT": "FAULT",
    "TOTAL": "Total",
}


def read_packet_trace(path: Path) -> list[tuple[float, str, int]]:
    rows: list[tuple[float, str, int]] = []
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        required = {"time_s", "traffic_class", "payload_B"}
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"{path} is missing required columns: {sorted(missing)}")
        for row in reader:
            cls = row["traffic_class"].strip().upper()
            if cls not in CLASSES:
                continue
            rows.append((float(row["time_s"]), cls, int(row["payload_B"])))
    if not rows:
        raise ValueError(f"No DIAG/ALERT/FAULT rows found in {path}")
    return rows


def bin_offered_load(
    rows: list[tuple[float, str, int]],
    duration_s: float,
    bin_width_s: float,
) -> tuple[list[float], dict[str, list[float]]]:
    if duration_s <= 0:
        raise ValueError("duration_s must be positive")
    if bin_width_s <= 0:
        raise ValueError("bin_width_s must be positive")

    n_bins = int(math.ceil(duration_s / bin_width_s))
    times = [(idx + 0.5) * bin_width_s for idx in range(n_bins)]
    bytes_by_class = {cls: [0 for _ in range(n_bins)] for cls in CLASSES}

    for time_s, cls, payload_b in rows:
        if time_s < 0.0 or time_s >= duration_s:
            continue
        idx = min(int(time_s / bin_width_s), n_bins - 1)
        bytes_by_class[cls][idx] += payload_b

    bytes_by_class["TOTAL"] = [
        sum(bytes_by_class[cls][idx] for cls in CLASSES) for idx in range(n_bins)
    ]
    kbps = {
        cls: [(value * 8.0 / bin_width_s) / 1000.0 for value in values]
        for cls, values in bytes_by_class.items()
    }
    return times, kbps


def summarize(rows: list[tuple[float, str, int]], duration_s: float) -> dict[str, dict[str, float]]:
    summary: dict[str, dict[str, float]] = {}
    for cls in CLASSES:
        cls_rows = [(time_s, payload_b) for time_s, row_cls, payload_b in rows if row_cls == cls]
        tx_packets = len(cls_rows)
        tx_bytes = sum(payload_b for _, payload_b in cls_rows)
        active_start = min((time_s for time_s, _ in cls_rows), default=math.nan)
        active_stop = max((time_s for time_s, _ in cls_rows), default=math.nan)
        if cls_rows:
            active_duration = active_stop - active_start
        else:
            active_duration = math.nan
        if active_duration > 0.0:
            active_mean_kbps = tx_bytes * 8.0 / active_duration / 1000.0
        else:
            active_mean_kbps = math.nan
        summary[cls] = {
            "tx_packets": tx_packets,
            "tx_bytes": tx_bytes,
            "mean_over_60s_kbps": tx_bytes * 8.0 / duration_s / 1000.0,
            "active_mean_kbps": active_mean_kbps,
            "first_packet_s": active_start,
            "last_packet_s": active_stop,
            "observed_active_span_s": active_duration,
        }
    total_bytes = sum(item["tx_bytes"] for item in summary.values())
    summary["TOTAL"] = {
        "tx_packets": sum(item["tx_packets"] for item in summary.values()),
        "tx_bytes": total_bytes,
        "mean_over_60s_kbps": total_bytes * 8.0 / duration_s / 1000.0,
        "active_mean_kbps": total_bytes * 8.0 / duration_s / 1000.0,
        "first_packet_s": min(item["first_packet_s"] for item in summary.values()),
        "last_packet_s": max(item["last_packet_s"] for item in summary.values()),
        "observed_active_span_s": duration_s,
    }
    return summary


def write_summary_csv(summary: dict[str, dict[str, float]], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "traffic_class",
        "tx_packets",
        "tx_bytes",
        "mean_over_60s_kbps",
        "active_mean_kbps",
        "first_packet_s",
        "last_packet_s",
        "observed_active_span_s",
    ]
    with output_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for cls in PANEL_CLASSES:
            row = {"traffic_class": cls}
            row.update(summary[cls])
            writer.writerow(row)


def configure_matplotlib() -> None:
    mpl.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 10,
            "axes.titlesize": 11,
            "axes.labelsize": 10,
            "xtick.labelsize": 9,
            "ytick.labelsize": 9,
            "legend.fontsize": 9,
            "axes.linewidth": 0.8,
            "grid.linewidth": 0.45,
            "lines.linewidth": 1.7,
            "savefig.bbox": "tight",
            "savefig.pad_inches": 0.03,
            "svg.fonttype": "none",
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )


def add_windows(ax: plt.Axes) -> None:
    ax.axvspan(*ALERT_WINDOW_S, color=COLORS["ALERT"], alpha=0.055, linewidth=0.0)
    ax.axvspan(*FAULT_WINDOW_S, color=COLORS["FAULT"], alpha=0.055, linewidth=0.0)


def plot_figure(
    times: list[float],
    kbps: dict[str, list[float]],
    summary: dict[str, dict[str, float]],
    png_path: Path,
    svg_path: Path,
    bin_width_s: float,
) -> None:
    configure_matplotlib()
    fig, axes = plt.subplots(
        4,
        1,
        figsize=(7.4, 8.6),
        sharex=True,
        constrained_layout=True,
    )
    fig.suptitle(
        "Application-layer offered traffic from current EV maintenance generators",
        fontsize=12,
        fontweight="bold",
    )

    for ax, cls in zip(axes, PANEL_CLASSES):
        add_windows(ax)
        ax.bar(
            times,
            kbps[cls],
            width=bin_width_s * 0.92,
            color=COLORS[cls],
            edgecolor=COLORS[cls],
            linewidth=0.0,
            alpha=0.82,
            label=f"{LABELS[cls]} offered load",
        )
        mean_kbps = summary[cls]["active_mean_kbps"]
        mean_label = "mean" if cls == "TOTAL" else "active mean"
        ax.axhline(
            mean_kbps,
            color="#4d4d4d",
            linewidth=1.0,
            linestyle="--",
            label=f"{mean_label} = {mean_kbps:.1f} kbps",
        )
        ax.set_ylabel("Offered load\n[kbps]")
        ax.set_title(LABELS[cls], loc="left", fontweight="bold")
        ax.grid(True, axis="both", alpha=0.32)
        ax.set_ylim(bottom=0.0)
        ax.legend(loc="upper right", frameon=True, framealpha=0.92, borderpad=0.35)

    axes[-1].set_xlabel("Simulation time [s]")
    axes[-1].set_xlim(0.0, max(times) + bin_width_s / 2.0)

    png_path.parent.mkdir(parents=True, exist_ok=True)
    svg_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png_path, dpi=900)
    fig.savefig(svg_path)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", required=True, help="maintenance_nr_app_tx_packets.csv path.")
    parser.add_argument("--duration-s", type=float, default=60.0)
    parser.add_argument("--bin-width-s", type=float, default=0.5)
    parser.add_argument("--png", required=True, help="Output PNG path.")
    parser.add_argument("--svg", required=True, help="Output SVG path.")
    parser.add_argument("--summary-csv", help="Optional output summary CSV path.")
    args = parser.parse_args()

    rows = read_packet_trace(Path(args.csv))
    times, kbps = bin_offered_load(rows, args.duration_s, args.bin_width_s)
    summary = summarize(rows, args.duration_s)
    plot_figure(times, kbps, summary, Path(args.png), Path(args.svg), args.bin_width_s)
    if args.summary_csv:
        write_summary_csv(summary, Path(args.summary_csv))

    print(f"Wrote PNG: {args.png}")
    print(f"Wrote SVG: {args.svg}")
    if args.summary_csv:
        print(f"Wrote summary CSV: {args.summary_csv}")


if __name__ == "__main__":
    main()
