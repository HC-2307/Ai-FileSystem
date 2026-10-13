"""Runs every workload against versions A, B and C and writes the results.

    python/evaluate.py [path to ai_fs_bench]

Writes experiments/results.csv (one row per run), experiments/summary.md
(medians per version and workload) and, when matplotlib is installed, one PNG
chart per metric in experiments/.
"""
import csv
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "ai_fs_bench"
OUT = ROOT / "experiments"

VERSIONS = ["A", "B", "C"]
WORKLOADS = ["sequential", "random", "repetitive", "mixed"]
THINK_US = [0, 200]      # back-to-back requests, and an application that works on each block
SEEDS = [1, 2, 3]
CACHE_KB = 1024          # about 6% of the 16 MiB of file data

# column, heading, chart title, lower is better
METRICS = [
    ("read_avg_us", "Read latency (us)", "Average read latency, microseconds", True),
    ("read_p99_us", "Read p99 (us)", "99th percentile read latency, microseconds", True),
    ("write_avg_us", "Write latency (us)", "Average write latency, microseconds", True),
    ("throughput_ops_s", "Throughput (ops/s)", "Throughput, operations per second", False),
    ("cache_hit_rate", "Cache hit rate", "Cache hit rate", False),
    ("prediction_precision", "Prefetch precision", "Share of prefetched blocks that were used", False),
    ("prediction_coverage", "Reads served by prefetch", "Share of reads served by a prefetch", False),
    ("prefetch_wasted", "Wasted prefetches", "Prefetched blocks evicted unread", True),
    ("disk_reads", "Disk reads", "Blocks read from the image", True),
    ("memory_kb", "Memory (KiB)", "Memory held by the caches, KiB", True),
    ("index_queries", "Mapping queries", "Block-mapping queries sent to SQLite", True),
    ("index_time_ms", "Mapping time (ms)", "Time spent in block-mapping queries, ms", True),
    ("model_accuracy", "Model accuracy (C only)", "Share of recent reads the model guessed first", False),
]
CHARTS = ["read_avg_us", "cache_hit_rate", "prefetch_wasted", "disk_reads", "index_time_ms"]
COLORS = {"A": "#2a78d6", "B": "#eb6834", "C": "#1baf7a"}
NAMES = {"A": "A: baseline", "B": "B: prediction", "C": "C: learned model"}


def run():
    header = subprocess.run([BENCH, "header"], capture_output=True, text=True, check=True).stdout.strip().split(",")
    rows = []
    for think in THINK_US:
        for workload in WORKLOADS:
            for seed in SEEDS:
                for version in VERSIONS:   # versions interleaved so drift in the machine hits all three alike
                    out = subprocess.run([BENCH, version, workload, str(CACHE_KB), str(think), str(seed)],
                                         capture_output=True, text=True, check=True, cwd=OUT).stdout.strip()
                    rows.append(dict(zip(header, out.split(","))))
                    print(out, flush=True)
    with open(OUT / "results.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)
    return rows


def median(rows, think, workload, version, column):
    return statistics.median(float(r[column]) for r in rows
                             if int(r["think_us"]) == think and r["workload"] == workload and r["version"] == version)


def fmt(x):
    return f"{x:,.0f}" if x >= 10 or x == int(x) else f"{x:.3f}"


def summarize(rows):
    lines = [f"# Results\n\nMedian of {len(SEEDS)} runs (seeds {SEEDS}). Cache budget {CACHE_KB} KiB, "
             "16 MiB of file data, 8192 operations per run. Produced by `python/evaluate.py`.\n"]
    for think in THINK_US:
        lines.append(f"\n## Think time {think} us between operations\n")
        for column, heading, _, _ in METRICS:
            lines.append(f"\n**{heading}**\n\n| Workload | A | B | C |\n|---|---:|---:|---:|")
            for workload in WORKLOADS:
                cells = " | ".join(fmt(median(rows, think, workload, v, column)) for v in VERSIONS)
                lines.append(f"| {workload} | {cells} |")
    (OUT / "summary.md").write_text("\n".join(lines) + "\n")


def chart(rows):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed: skipping charts")
        return
    ink, muted, surface = "#1f1f1e", "#6b6a63", "#fcfcfb"
    titles = {column: title for column, _, title, _ in METRICS}
    better = {column: lower for column, _, _, lower in METRICS}
    for column in CHARTS:
        fig, axes = plt.subplots(1, len(THINK_US), figsize=(11, 4), sharey=True, facecolor=surface)
        for ax, think in zip(axes, THINK_US):
            ax.set_facecolor(surface)
            for i, version in enumerate(VERSIONS):
                values = [median(rows, think, w, version, column) for w in WORKLOADS]
                xs = [x + (i - 1) * 0.27 for x in range(len(WORKLOADS))]
                bars = ax.bar(xs, values, width=0.25, color=COLORS[version], label=NAMES[version])
                ax.bar_label(bars, labels=[fmt(v) for v in values], fontsize=7, color=muted, padding=2)
            ax.set_xticks(range(len(WORKLOADS)), WORKLOADS, color=ink)
            ax.set_title(f"think time {think} us", fontsize=10, color=muted, loc="left")
            ax.tick_params(colors=muted, length=0)
            ax.grid(axis="y", color="#e6e5df", linewidth=0.8)
            ax.set_axisbelow(True)
            for side in ("top", "right", "left"):
                ax.spines[side].set_visible(False)
            ax.spines["bottom"].set_color("#c9c8c0")
        direction = "lower is better" if better[column] else "higher is better"
        fig.suptitle(f"{titles[column]} ({direction})", x=0.01, ha="left", fontsize=12, color=ink)
        fig.legend(*axes[0].get_legend_handles_labels(), loc="upper right", ncols=3, frameon=False, fontsize=9)
        fig.tight_layout(rect=(0, 0, 1, 0.93))
        fig.savefig(OUT / f"{column}.png", dpi=150)
        plt.close(fig)


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    results = run()
    summarize(results)
    chart(results)
