#!/usr/bin/env python3
"""
Plots and summary tables from results/results.csv (needs matplotlib).

Outputs (in --out-dir, default results/plots):
  runtime_by_family.png        median runtime vs n, one panel per graph family (log y).
                               An X marks an exact-algorithm timeout.
  ratio_by_family.png          cover size / baseline vs n, one panel per family.
  ratio_summary.png            mean ratio per family and algorithm (bar chart).
  edge_order_sensitivity.png   matching cover size across edge orderings of ONE graph.
  summary.csv                  mean/max ratio and mean time per (family, algorithm).

Lines are the mean over instances at each n, shaded bands are min..max.
The baseline for each ratio is exact optimum > known_opt (bipartite) > best known
cover (see benchmark.py); the baseline mix is stated in each figure title. Where the
baseline is "best_known" the plotted ratios understate the true ratio.

Usage:
    python3 scripts/plot_results.py --results results/results.csv --out-dir results/plots
"""
import argparse
import csv
import math
import os
import statistics
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

ALGOS = ["exact", "approx", "greedy"]
NAMES = {"exact": "Exact (branch & bound)", "approx": "2-approx (maximal matching)",
         "greedy": "Greedy (highest degree)"}
COLORS = {"exact": "#1f77b4", "approx": "#d62728", "greedy": "#2ca02c"}
MARKERS = {"exact": "o", "approx": "s", "greedy": "^"}
TIME_FLOOR = 1e-7   # programs print 6 decimals; 0.000000 is plotted at this floor


def fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in ("n", "m", "cover_size", "time_median", "ratio", "ratio_vs_lb", "baseline", "repeats"):
            r[k] = fnum(r.get(k))
    return rows


def group(rows, labels):
    """label -> algo -> n -> list of rows"""
    g = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    for r in rows:
        if r["label"] in labels and r["n"] is not None:
            g[r["label"]][r["algorithm"]][int(r["n"])].append(r)
    return g


def panels(n_items, ncols=3):
    ncols = min(ncols, max(1, n_items))
    nrows = math.ceil(n_items / ncols)
    fig, axes = plt.subplots(nrows, ncols, figsize=(5.2 * ncols, 3.8 * nrows), squeeze=False)
    flat = [a for row in axes for a in row]
    for a in flat[n_items:]:
        a.axis("off")
    return fig, flat[:n_items]


def band(ax, xs, vals, algo, label=None):
    mean = [statistics.mean(v) for v in vals]
    lo = [min(v) for v in vals]
    hi = [max(v) for v in vals]
    ax.plot(xs, mean, marker=MARKERS[algo], color=COLORS[algo], label=label, markersize=4, linewidth=1.5)
    if any(l != h for l, h in zip(lo, hi)):
        ax.fill_between(xs, lo, hi, color=COLORS[algo], alpha=0.15, linewidth=0)


def plot_runtime(g, labels, out):
    fig, axes = panels(len(labels))
    for ax, lab in zip(axes, labels):
        for algo in ALGOS:
            data = g[lab].get(algo, {})
            xs = sorted(data)
            if not xs:
                continue
            vals = [[max(r["time_median"], TIME_FLOOR) for r in data[x] if r["time_median"] is not None]
                    for x in xs]
            keep = [(x, v) for x, v in zip(xs, vals) if v]
            if not keep:
                continue
            xs2, vals2 = zip(*keep)
            band(ax, list(xs2), list(vals2), algo, NAMES[algo])
            if algo == "exact":
                to = [x for x in xs2 if any(r["status"] == "timed_out" for r in data[x])]
                if to:
                    ys = [statistics.mean([max(r["time_median"], TIME_FLOOR) for r in data[x]]) for x in to]
                    ax.scatter(to, ys, marker="x", color="black", s=60, zorder=5,
                               label="exact timed out")
        ax.set_yscale("log")
        ax.set_title(lab)
        ax.set_xlabel("n (vertices)")
        ax.set_ylabel("median time (s)")
        ax.grid(True, which="both", alpha=0.25)
    handles, labs = [], []
    for ax in axes:
        for h, l in zip(*ax.get_legend_handles_labels()):
            if l not in labs:
                handles.append(h)
                labs.append(l)
    fig.legend(handles, labs, loc="lower center", ncol=4, frameon=False)
    fig.suptitle("Runtime vs graph size (algorithm only; times under 1 us plotted at 1e-7 s)", y=0.995)
    fig.tight_layout(rect=(0, 0.06, 1, 0.97))
    fig.savefig(out, dpi=160)
    plt.close(fig)


def baseline_note(rows):
    c = defaultdict(int)
    for r in rows:
        if r["algorithm"] == "approx":
            c[r.get("baseline_type", "?")] += 1
    return ", ".join(f"{k}: {v}" for k, v in sorted(c.items()))


def plot_ratio(g, labels, rows, out):
    fig, axes = panels(len(labels))
    for ax, lab in zip(axes, labels):
        for algo in ("approx", "greedy"):
            data = g[lab].get(algo, {})
            xs = sorted(data)
            vals = [[r["ratio"] for r in data[x] if r["ratio"] is not None] for x in xs]
            keep = [(x, v) for x, v in zip(xs, vals) if v]
            if keep:
                xs2, vals2 = zip(*keep)
                band(ax, list(xs2), list(vals2), algo, NAMES[algo])
        ax.axhline(1.0, color="gray", linewidth=0.8)
        ax.axhline(2.0, color="gray", linestyle="--", linewidth=0.8)
        ax.set_title(lab)
        ax.set_xlabel("n (vertices)")
        ax.set_ylabel("cover size / baseline")
        ax.grid(True, alpha=0.25)
    handles, labs = axes[0].get_legend_handles_labels()
    fig.legend(handles, labs, loc="lower center", ncol=2, frameon=False)
    fig.suptitle("Solution quality (dashed line = 2, the matching guarantee). Baselines used: "
                 + baseline_note(rows), y=0.995, fontsize=10)
    fig.tight_layout(rect=(0, 0.06, 1, 0.96))
    fig.savefig(out, dpi=160)
    plt.close(fig)


def plot_summary_bars(g, labels, out):
    fig, ax = plt.subplots(figsize=(max(7, 1.3 * len(labels)), 4.2))
    width = 0.38
    for i, algo in enumerate(("approx", "greedy")):
        means = []
        for lab in labels:
            vals = [r["ratio"] for n in g[lab].get(algo, {}) for r in g[lab][algo][n] if r["ratio"] is not None]
            means.append(statistics.mean(vals) if vals else 0)
        xs = [j + (i - 0.5) * width for j in range(len(labels))]
        ax.bar(xs, means, width, color=COLORS[algo], label=NAMES[algo])
    ax.axhline(1.0, color="gray", linewidth=0.8)
    ax.axhline(2.0, color="gray", linestyle="--", linewidth=0.8)
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels, rotation=30, ha="right")
    ax.set_ylabel("mean cover size / baseline")
    ax.set_title("Mean solution ratio by graph family")
    ax.legend(frameon=False)
    fig.tight_layout()
    fig.savefig(out, dpi=160)
    plt.close(fig)


def plot_edge_order(rows, out):
    sel = [r for r in rows if r["label"] == "edge_order"]
    if not sel:
        return False
    by = defaultdict(list)
    for r in sel:
        if r["cover_size"] is not None:
            by[r["algorithm"]].append(r["cover_size"])
    if not by["approx"]:
        return False
    fig, ax = plt.subplots(figsize=(6.5, 4.2))
    vals = by["approx"]
    lo, hi = int(min(vals)), int(max(vals))
    ax.hist(vals, bins=range(lo, hi + 2), color=COLORS["approx"], alpha=0.8, align="left",
            label=f"2-approx over {len(vals)} edge orderings")
    for algo in ("greedy", "exact"):
        if by[algo]:
            ax.axvline(statistics.mean(by[algo]), color=COLORS[algo], linewidth=2,
                       label=f"{algo} (order independent: {statistics.mean(by[algo]):g})")
    lb = sel[0].get("baseline")
    if lb:
        ax.axvline(lb, color="black", linestyle=":", label=f"baseline ({sel[0]['baseline_type']}): {lb:g}")
    ax.set_xlabel("cover size")
    ax.set_ylabel("number of edge orderings")
    ax.yaxis.set_major_locator(matplotlib.ticker.MaxNLocator(integer=True))
    ax.set_title("Same graph, different edge orders: matching cover size varies", fontsize=10)
    ax.legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=160)
    plt.close(fig)
    return True


def write_summary(rows, labels, out):
    acc = defaultdict(list)
    for r in rows:
        if r["label"] in labels:
            acc[(r["label"], r["algorithm"])].append(r)
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["label", "algorithm", "instances", "mean_ratio", "max_ratio",
                    "mean_time_s", "timeouts"])
        for (lab, algo), rs in sorted(acc.items()):
            ratios = [r["ratio"] for r in rs if r["ratio"] is not None]
            times = [r["time_median"] for r in rs if r["time_median"] is not None]
            w.writerow([lab, algo, len(rs),
                        f"{statistics.mean(ratios):.4f}" if ratios else "",
                        f"{max(ratios):.4f}" if ratios else "",
                        f"{statistics.mean(times):.6g}" if times else "",
                        sum(1 for r in rs if r["status"] == "timed_out")])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results", default="results/results.csv")
    ap.add_argument("--out-dir", default="results/plots")
    args = ap.parse_args()

    rows = load(args.results)
    os.makedirs(args.out_dir, exist_ok=True)
    labels = []
    for r in rows:
        if r["label"] not in labels and r["label"] not in ("edge_order", "single"):
            labels.append(r["label"])
    g = group(rows, set(labels))

    if labels:
        plot_runtime(g, labels, os.path.join(args.out_dir, "runtime_by_family.png"))
        plot_ratio(g, labels, rows, os.path.join(args.out_dir, "ratio_by_family.png"))
        plot_summary_bars(g, labels, os.path.join(args.out_dir, "ratio_summary.png"))
    made_order = plot_edge_order(rows, os.path.join(args.out_dir, "edge_order_sensitivity.png"))
    write_summary(rows, set(labels) | {"edge_order"}, os.path.join(args.out_dir, "summary.csv"))
    print(f"Wrote plots and summary.csv to {args.out_dir}"
          + ("" if made_order else " (no edge_order data, skipped that plot)"))


if __name__ == "__main__":
    main()
