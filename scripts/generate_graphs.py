#!/usr/bin/env python3
"""
Graph generators for the vertex cover experiments (standard library only).

Every graph is written in the format the C++ programs read:
    n m
    u v        (m lines, vertices 0..n-1)

Families
--------
er                  Erdos-Renyi G(n, p). Give --p or --avg-degree.
bipartite           Random bipartite graph, sides a and b, edge prob p.
complete_bipartite  K_{k,k}. Worst case for the matching 2-approximation (ratio 2).
greedy_adversarial  Classic bipartite construction where highest-degree greedy
                    picks ~k*ln(k) vertices while OPT = k.
scale_free          Barabasi-Albert preferential attachment (hubs), k edges per new node.
planted             Random graph where only vertices 0..c-1 can cover edges, so
                    OPT <= c (an upper bound only).

For bipartite graphs the exact optimum is computed with Hopcroft-Karp (Konig's
theorem: min vertex cover = max matching), so the benchmark has a true baseline
even when the branch-and-bound times out. It is written to the manifest as
`known_opt`.

Usage
-----
Single graph:
    python3 scripts/generate_graphs.py --family er --n 100 --avg-degree 3 \
        --seed 1 --out graphs/test.txt

Whole experiment suite (writes graphs/ and graphs/manifest.csv):
    python3 scripts/generate_graphs.py --suite full  --out-dir graphs
    python3 scripts/generate_graphs.py --suite quick --out-dir graphs

All randomness is seeded, so the same command always produces the same files.
--order-seed shuffles the order of the edges in the file (and the orientation of
each edge) without changing the graph. This matters for the matching algorithm,
which scans edges in input order.
"""
import argparse
import csv
import math
import os
import random
import sys
from collections import deque

sys.setrecursionlimit(100000)


# ----------------------------------------------------------------------------
# Generators. Each returns (n, edges, info). edges are (u, v) with u != v.
# info may contain: "left" (size of left side if bipartite), "opt_upper_bound".
# ----------------------------------------------------------------------------

def gen_er(n, p, rng):
    """G(n, p) using geometric skipping (fast for sparse graphs)."""
    edges = []
    if p <= 0 or n < 2:
        return n, edges, {}
    if p >= 1:
        return n, [(u, v) for u in range(n) for v in range(u + 1, n)], {}
    lp = math.log(1.0 - p)
    v, w = 1, -1
    while v < n:
        r = rng.random()
        w += 1 + int(math.log(1.0 - r) / lp)
        while w >= v and v < n:
            w -= v
            v += 1
        if v < n:
            edges.append((w, v))
    return n, edges, {}


def gen_bipartite(a, b, p, rng):
    edges = [(i, a + j) for i in range(a) for j in range(b) if rng.random() < p]
    return a + b, edges, {"left": a}


def gen_complete_bipartite(k):
    edges = [(i, k + j) for i in range(k) for j in range(k)]
    return 2 * k, edges, {"left": k}


def gen_greedy_adversarial(k):
    """
    Left side A = {0..k-1}. For each i = 2..k, add floor(k/i) right-side vertices,
    each adjacent to a distinct block of i vertices of A. A is a cover of size k.
    When greedy has to choose, a right vertex from group i has degree i while any
    A vertex has degree <= i-1, so greedy takes all right vertices:
    sum_{i=2..k} floor(k/i) ~ k ln k vertices.
    """
    edges = []
    nxt = k
    for i in range(2, k + 1):
        for blk in range(k // i):
            for t in range(i):
                edges.append((blk * i + t, nxt))
            nxt += 1
    return nxt, edges, {"left": k}


def gen_scale_free(n, k, rng):
    k = max(1, min(k, n - 1))
    edges = []
    repeated = []
    for u in range(k + 1):
        for v in range(u + 1, k + 1):
            edges.append((u, v))
            repeated.extend([u, v])
    for new in range(k + 1, n):
        targets = set()
        while len(targets) < k:
            targets.add(rng.choice(repeated))
        for t in sorted(targets):
            edges.append((t, new))
            repeated.extend([t, new])
    return n, edges, {}


def gen_planted(n, frac, p, rng):
    c = int(round(frac * n))
    edges = []
    for u in range(c):
        for v in range(u + 1, n):
            if rng.random() < p:
                edges.append((u, v))
    return n, edges, {"opt_upper_bound": c}


# ----------------------------------------------------------------------------
# Exact optimum for bipartite graphs (Hopcroft-Karp + Konig's theorem)
# ----------------------------------------------------------------------------

def bipartite_opt(n, left, edges):
    adj = [[] for _ in range(left)]
    for u, v in edges:
        if u < left <= v:
            adj[u].append(v - left)
        elif v < left <= u:
            adj[v].append(u - left)
        else:
            raise ValueError("edge inside one side: graph is not bipartite as declared")
    right = n - left
    INF = 10 ** 9
    match_l = [-1] * left
    match_r = [-1] * right

    def bfs():
        dist = [INF] * left
        q = deque()
        for u in range(left):
            if match_l[u] == -1:
                dist[u] = 0
                q.append(u)
        found = False
        while q:
            u = q.popleft()
            for v in adj[u]:
                w = match_r[v]
                if w == -1:
                    found = True
                elif dist[w] == INF:
                    dist[w] = dist[u] + 1
                    q.append(w)
        return found, dist

    def dfs(u, dist):
        for v in adj[u]:
            w = match_r[v]
            if w == -1 or (dist[w] == dist[u] + 1 and dfs(w, dist)):
                match_l[u] = v
                match_r[v] = u
                return True
        dist[u] = INF
        return False

    size = 0
    while True:
        found, dist = bfs()
        if not found:
            break
        for u in range(left):
            if match_l[u] == -1 and dfs(u, dist):
                size += 1
    return size


# ----------------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------------

def apply_edge_order(edges, order_seed):
    """Shuffle edge order and flip orientations (graph itself is unchanged)."""
    if order_seed is None:
        return edges
    r = random.Random(order_seed)
    out = list(edges)
    r.shuffle(out)
    return [(v, u) if r.random() < 0.5 else (u, v) for u, v in out]


def write_graph(path, n, edges):
    with open(path, "w") as f:
        f.write(f"{n} {len(edges)}\n")
        f.write("\n".join(f"{u} {v}" for u, v in edges))
        f.write("\n")


def build(family, params, seed):
    """Return (n, edges, info) for a family + params dict + seed."""
    rng = random.Random(seed)
    if family == "er":
        n = params["n"]
        p = params["p"] if "p" in params else min(1.0, params["avg_degree"] / max(1, n - 1))
        return gen_er(n, p, rng)
    if family == "bipartite":
        a, b = params["a"], params["b"]
        p = params["p"] if "p" in params else min(1.0, params["avg_degree"] * (a + b) / (2.0 * a * b))
        return gen_bipartite(a, b, p, rng)
    if family == "complete_bipartite":
        return gen_complete_bipartite(params["k"])
    if family == "greedy_adversarial":
        return gen_greedy_adversarial(params["k"])
    if family == "scale_free":
        return gen_scale_free(params["n"], params["k"], rng)
    if family == "planted":
        return gen_planted(params["n"], params["frac"], params["p"], rng)
    raise ValueError(f"unknown family {family}")


# ----------------------------------------------------------------------------
# Experiment suites
# ----------------------------------------------------------------------------

def suite_specs(kind):
    """
    Returns a list of dicts: label, family, params, seed, order_seed.
    `label` is the name used to group instances in the plots.
    """
    if kind == "quick":
        seeds = [1]
        er_sparse_n = [20, 40, 80, 160]
        er_dense_n = [15, 20, 25, 30]
        bip_n = [20, 40, 80, 160]
        kbip_k = [5, 10, 20, 40]
        adv_k = [5, 10, 15, 20]
        sf_n = [20, 40, 80, 160]
        planted_n = [20, 40, 80, 160]
        order_orders = 10
    elif kind == "full":
        seeds = [1, 2, 3]
        er_sparse_n = [20, 40, 60, 80, 100, 150, 200, 250, 300, 500, 1000]
        er_dense_n = [20, 40, 60, 80, 100, 125, 150, 175, 200, 250]
        bip_n = [20, 40, 60, 80, 100, 150, 200, 300, 500, 1000]
        kbip_k = [5, 10, 20, 40, 80, 160, 320]
        adv_k = [5, 10, 15, 20, 30, 40, 60, 80, 100]
        sf_n = [20, 40, 60, 80, 100, 150, 200, 250, 300, 500, 1000]
        planted_n = [20, 40, 60, 80, 100, 150, 200, 300, 500, 1000]
        order_orders = 30
    else:
        raise ValueError("suite must be 'quick' or 'full'")

    specs = []

    def add(label, family, params, seed, order_seed=None):
        specs.append(dict(label=label, family=family, params=params, seed=seed,
                          order_seed=order_seed))

    for s in seeds:
        for n in er_sparse_n:
            add("er_sparse", "er", {"n": n, "avg_degree": 3.0}, s)
        for n in er_dense_n:
            add("er_dense", "er", {"n": n, "p": 0.5}, s)
        for n in bip_n:
            add("bipartite", "bipartite", {"a": n // 2, "b": n - n // 2, "avg_degree": 4.0}, s)
        for n in sf_n:
            add("scale_free", "scale_free", {"n": n, "k": 3}, s)
        for n in planted_n:
            add("planted", "planted", {"n": n, "frac": 0.3, "p": min(1.0, 10.0 / n)}, s)
    # Deterministic structures: one instance per size.
    for k in kbip_k:
        add("complete_bipartite", "complete_bipartite", {"k": k}, 0)
    for k in adv_k:
        add("greedy_adversarial", "greedy_adversarial", {"k": k}, 0)
    # Edge-order experiment: ONE graph, many edge orderings (order_seed None = file order as generated).
    add("edge_order", "er", {"n": 150, "avg_degree": 3.0}, 1, None)
    for o in range(1, order_orders + 1):
        add("edge_order", "er", {"n": 150, "avg_degree": 3.0}, 1, o)
    return specs


def fmt_params(params):
    return ";".join(f"{k}={v:g}" if isinstance(v, float) else f"{k}={v}" for k, v in params.items())


def generate_suite(kind, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    for spec in suite_specs(kind):
        n, edges, info = build(spec["family"], spec["params"], spec["seed"])
        known_opt = ""
        if "left" in info:
            known_opt = bipartite_opt(n, info["left"], edges)
        edges = apply_edge_order(edges, spec["order_seed"])
        if "k" in spec["params"] and spec["family"] in ("complete_bipartite", "greedy_adversarial"):
            size_tag = f"k{spec['params']['k']}"
        else:
            size_tag = f"n{n}"
        name = f"{spec['label']}_{size_tag}_s{spec['seed']}"
        if spec["label"] == "edge_order":
            name += f"_o{spec['order_seed'] if spec['order_seed'] is not None else 0}"
        fname = name + ".txt"
        write_graph(os.path.join(out_dir, fname), n, edges)
        rows.append({
            "file": fname,
            "label": spec["label"],
            "family": spec["family"],
            "n": n,
            "m": len(edges),
            "seed": spec["seed"],
            "order_seed": "" if spec["order_seed"] is None else spec["order_seed"],
            "params": fmt_params(spec["params"]),
            "known_opt": known_opt,
            "opt_upper_bound": info.get("opt_upper_bound", ""),
        })
    manifest = os.path.join(out_dir, "manifest.csv")
    with open(manifest, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"Wrote {len(rows)} graphs and {manifest}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--suite", choices=["quick", "full"], help="generate the whole experiment suite")
    ap.add_argument("--out-dir", default="graphs", help="output directory for --suite")
    ap.add_argument("--family", choices=["er", "bipartite", "complete_bipartite",
                                         "greedy_adversarial", "scale_free", "planted"])
    ap.add_argument("--n", type=int)
    ap.add_argument("--p", type=float)
    ap.add_argument("--avg-degree", type=float)
    ap.add_argument("--a", type=int, help="bipartite: left side size")
    ap.add_argument("--b", type=int, help="bipartite: right side size")
    ap.add_argument("--k", type=int, help="complete_bipartite / greedy_adversarial size; scale_free edges per node")
    ap.add_argument("--frac", type=float, help="planted: fraction of vertices in the planted cover")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--order-seed", type=int, default=None, help="shuffle edge order with this seed")
    ap.add_argument("--out", help="output file for a single graph")
    args = ap.parse_args()

    if args.suite:
        generate_suite(args.suite, args.out_dir)
        return
    if not args.family or not args.out:
        ap.error("give --suite, or --family and --out")

    params = {k: v for k, v in dict(n=args.n, p=args.p, avg_degree=args.avg_degree, a=args.a,
                                    b=args.b, k=args.k, frac=args.frac).items() if v is not None}
    n, edges, info = build(args.family, params, args.seed)
    edges = apply_edge_order(edges, args.order_seed)
    write_graph(args.out, n, edges)
    msg = f"Wrote {args.out}: n={n}, m={len(edges)}"
    if "left" in info:
        msg += f", known OPT={bipartite_opt(n, info['left'], edges)} (bipartite, Konig)"
    if "opt_upper_bound" in info:
        msg += f", OPT <= {info['opt_upper_bound']} (planted)"
    print(msg)


if __name__ == "__main__":
    main()
