#!/usr/bin/env python3
"""
Unified benchmark driver for the three vertex cover programs (standard library only).

For every graph in the manifest it runs vc_exact, vc_approx and vc_greedy on the
SAME file, parses their output, and writes one CSV row per (graph, algorithm).

Timing
------
The C++ programs time only the algorithm (file reading is excluded) and print
"Time (s)" with microsecond resolution, so that number is used directly. Times
below 1 microsecond appear as 0.000000.
  * One untimed warm-up pass over all three programs per graph (file cache, CPU).
  * Then --repeats timed passes. Within a pass the programs run back to back, so
    slow drift on the machine (thermal, background load) hits all of them equally.
  * The median is the headline number; min, mean and stdev are also recorded.
  * If any run takes longer than --slow-threshold seconds, further repeats of that
    (graph, algorithm) are skipped: at that scale noise is tiny relative to the run.
  * If the exact program times out, it is not repeated.

Quality baselines, best first (ratio = cover size / baseline):
  1. exact optimum (vc_exact finished with Status: optimal)       -> baseline_type "exact"
  2. known_opt from the manifest (bipartite graphs, Konig)        -> "known_opt"
  3. best known cover: the smallest valid cover found by any of the three programs
     (including the exact program's best-so-far when it timed out). This is only an
     upper bound on OPT, so the ratio UNDERSTATES the true ratio.   -> "best_known"
A second column, ratio_vs_lb, is always cover size / |M| where |M| is the maximal
matching size (a lower bound on OPT), so it OVERSTATES the true ratio. Note the
2-approximation's own ratio_vs_lb is exactly 2 by construction (cover = 2|M|); it is
informative for the other algorithms only. The true ratio lies between the two.

Usage
-----
    python3 scripts/benchmark.py --build --manifest graphs/manifest.csv \
        --out results/results.csv --repeats 5 --exact-timeout 10
    python3 scripts/benchmark.py --graph example_graph.txt        # single file
"""
import argparse
import csv
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from datetime import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = ".exe" if os.name == "nt" else ""
ALGOS = [("exact", "vc_exact"), ("approx", "vc_approx"), ("greedy", "vc_greedy")]
CXX_FLAGS = ["-O2", "-std=c++17"]

FIELDS = ["graph", "label", "family", "n", "m", "order_seed", "algorithm", "cover_size",
          "valid", "status", "time_median", "time_min", "time_mean", "time_std", "repeats",
          "nodes", "matching_lb", "known_opt", "baseline", "baseline_type", "ratio",
          "ratio_vs_lb"]


def compiler():
    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        sys.exit("No C++ compiler found (set CXX or install g++/clang++).")
    return cxx


def build(bin_dir):
    os.makedirs(bin_dir, exist_ok=True)
    cxx = compiler()
    for _, prog in ALGOS:
        src = os.path.join(ROOT, "src", prog + ".cpp")
        out = os.path.join(bin_dir, prog + EXE)
        cmd = [cxx] + CXX_FLAGS + [src, "-o", out]
        print("Building:", " ".join(cmd))
        subprocess.run(cmd, check=True)


def write_environment(path, bin_dir, args):
    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++") or "unknown"
    try:
        ver = subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    except Exception:
        ver = "unknown"
    lines = [
        f"date: {datetime.now().isoformat(timespec='seconds')}",
        f"platform: {platform.platform()}",
        f"processor: {platform.processor() or 'unknown'}",
        f"logical cpus: {os.cpu_count()}",
        f"python: {platform.python_version()}",
        f"compiler: {ver}",
        f"flags: {' '.join(CXX_FLAGS)}",
        f"repeats: {args.repeats}",
        f"exact timeout (s): {args.exact_timeout}",
        f"slow threshold (s): {args.slow_threshold}",
    ]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


RE = {
    "cover": re.compile(r"Cover size:\s*(\d+)"),
    "valid": re.compile(r"Valid cover:\s*(\w+)"),
    "time": re.compile(r"Time \(s\):\s*([0-9.eE+-]+)"),
    "nodes": re.compile(r"Search nodes:\s*(\d+)"),
    "match": re.compile(r"Matching size \|M\|[^:]*:\s*(\d+)"),
}


def run_once(bin_dir, prog, graph, exact_timeout):
    cmd = [os.path.join(bin_dir, prog + EXE), graph]
    limit = None
    if prog == "vc_exact":
        cmd.append(str(exact_timeout))
        limit = exact_timeout * 3 + 60      # safety net only; the program enforces its own limit
    t0 = time.perf_counter()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=limit)
    except subprocess.TimeoutExpired:
        return {"status": "killed", "valid": "n/a", "time": time.perf_counter() - t0}
    out = p.stdout
    res = {"status": "ok" if p.returncode == 0 else f"exit{p.returncode}"}
    m = RE["cover"].search(out)
    res["cover"] = int(m.group(1)) if m else None
    m = RE["valid"].search(out)
    res["valid"] = m.group(1) if m else "n/a"
    m = RE["time"].search(out)
    res["time"] = float(m.group(1)) if m else None
    m = RE["nodes"].search(out)
    res["nodes"] = int(m.group(1)) if m else None
    m = RE["match"].search(out)
    res["match"] = int(m.group(1)) if m else None
    if prog == "vc_exact":
        res["status"] = "timed_out" if "TIMED OUT" in out else ("optimal" if "Status: optimal" in out else res["status"])
    return res


def benchmark_graph(bin_dir, path, args):
    """Returns {algo: row-dict} for one graph file."""
    # Warm-up pass (timings discarded), except that a timed-out exact run is kept as
    # the measurement: re-running a full timeout would only repeat the same number.
    runs = {a: [] for a, _ in ALGOS}
    done = set()
    for algo, prog in ALGOS:
        r = run_once(bin_dir, prog, path, args.exact_timeout)
        if r["status"] in ("timed_out", "killed"):
            runs[algo].append(r)
            done.add(algo)
    for _ in range(args.repeats):
        for algo, prog in ALGOS:
            if algo in done:
                continue
            r = run_once(bin_dir, prog, path, args.exact_timeout)
            runs[algo].append(r)
            if r["status"] in ("timed_out", "killed") or (r.get("time") or 0) > args.slow_threshold:
                done.add(algo)
        if len(done) == len(ALGOS):
            break

    out = {}
    for algo, rs in runs.items():
        times = [r["time"] for r in rs if r.get("time") is not None]
        last = rs[-1]
        covers = {r["cover"] for r in rs if r.get("cover") is not None}
        if len(covers) > 1:
            print(f"  WARNING: {algo} gave different cover sizes across repeats on {path}: {covers}")
        out[algo] = {
            "cover_size": last.get("cover"),
            "valid": "yes" if all(r.get("valid") == "yes" for r in rs) else last.get("valid"),
            "status": last["status"],
            "time_median": statistics.median(times) if times else None,
            "time_min": min(times) if times else None,
            "time_mean": statistics.mean(times) if times else None,
            "time_std": statistics.stdev(times) if len(times) > 1 else 0.0,
            "repeats": len(rs),
            "nodes": last.get("nodes"),
            "matching_lb": last.get("match"),
        }
    return out


def add_baseline(rows, known_opt):
    exact = rows["exact"]
    lb = rows["approx"].get("matching_lb")
    if exact["status"] == "optimal" and exact["cover_size"] is not None:
        base, btype = exact["cover_size"], "exact"
        if known_opt not in (None, "") and int(known_opt) != base:
            print(f"  WARNING: exact optimum {base} != known_opt {known_opt}")
    elif known_opt not in (None, ""):
        base, btype = int(known_opt), "known_opt"
    else:
        valid = [r["cover_size"] for r in rows.values()
                 if r["cover_size"] is not None and r["valid"] == "yes"]
        base, btype = (min(valid) if valid else None), "best_known"
    for r in rows.values():
        r["baseline"] = base
        r["baseline_type"] = btype
        r["ratio"] = (r["cover_size"] / base) if (base and r["cover_size"] is not None) else None
        r["ratio_vs_lb"] = (r["cover_size"] / lb) if (lb and r["cover_size"] is not None) else None
        r["known_opt"] = known_opt
        r["matching_lb"] = lb


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", default=os.path.join(ROOT, "graphs", "manifest.csv"))
    ap.add_argument("--graph", help="benchmark a single graph file instead of a manifest")
    ap.add_argument("--graph-dir", help="directory holding the graph files (default: manifest's directory)")
    ap.add_argument("--bin-dir", default=os.path.join(ROOT, "bin"))
    ap.add_argument("--out", default=os.path.join(ROOT, "results", "results.csv"))
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--exact-timeout", type=float, default=10.0, help="seconds, passed to vc_exact")
    ap.add_argument("--slow-threshold", type=float, default=2.0)
    ap.add_argument("--build", action="store_true", help="compile src/*.cpp into --bin-dir first")
    args = ap.parse_args()

    if args.build:
        build(args.bin_dir)
    for _, prog in ALGOS:
        if not os.path.exists(os.path.join(args.bin_dir, prog + EXE)):
            sys.exit(f"Missing binary {prog}{EXE} in {args.bin_dir}. Re-run with --build.")

    if args.graph:
        entries = [{"file": os.path.basename(args.graph), "label": "single", "family": "", "n": "",
                    "m": "", "order_seed": "", "known_opt": ""}]
        graph_dir = os.path.dirname(os.path.abspath(args.graph))
    else:
        with open(args.manifest, newline="") as f:
            entries = list(csv.DictReader(f))
        graph_dir = args.graph_dir or os.path.dirname(os.path.abspath(args.manifest))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    write_environment(os.path.join(os.path.dirname(os.path.abspath(args.out)), "environment.txt"),
                      args.bin_dir, args)

    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        for i, e in enumerate(entries, 1):
            path = os.path.join(graph_dir, e["file"])
            print(f"[{i}/{len(entries)}] {e['file']}", flush=True)
            rows = benchmark_graph(args.bin_dir, path, args)
            add_baseline(rows, e.get("known_opt"))
            for algo, r in rows.items():
                w.writerow({"graph": e["file"], "label": e["label"], "family": e["family"],
                            "n": e["n"], "m": e["m"], "order_seed": e.get("order_seed", ""),
                            "algorithm": algo, **r})
            f.flush()
            if args.graph:
                for algo, r in rows.items():
                    print(f"  {algo:7s} cover={r['cover_size']} valid={r['valid']} "
                          f"status={r['status']} median_time={r['time_median']}")
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    main()
