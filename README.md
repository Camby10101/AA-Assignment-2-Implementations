# Vertex Cover: Exact vs Approximation vs Heuristic

Three standalone C++ programs that solve minimum vertex cover on the same input graph.

| File | Method | Type | Guarantee |
|---|---|---|---|
| `src/vc_exact.cpp` | Branch-and-bound | Exact | Always optimal (exponential worst-case time) |
| `src/vc_approx.cpp` | Maximal matching | Approximation | Cover size at most 2 x optimal |
| `src/vc_greedy.cpp` | Highest-degree greedy | Heuristic | None (no constant ratio) |
| `example_graph.txt` | Example input | n/a | n/a |

## Authors

Advanced Algorithms, Assignment 2 (group of 3). Each implementation has one primary author, who is also named in a comment at the top of the source file.

| Implementation | Method | Primary author |
|---|---|---|
| `src/vc_exact.cpp` | Branch-and-bound (exact) | Matt |
| `src/vc_approx.cpp` | Maximal matching (2-approximation) | Campbell Finlay (24772473) |
| `src/vc_greedy.cpp` | Highest-degree greedy (heuristic) | Luke |

The benchmarking framework, graph generators and plotting scripts in `scripts/` are shared group work.

## Repository layout

```
.
├── README.md
├── requirements.txt          Python dependency for plotting (matplotlib)
├── example_graph.txt         10-vertex example input
├── src/                      the three implementations (C++17)
│   ├── vc_exact.cpp
│   ├── vc_approx.cpp
│   └── vc_greedy.cpp
├── scripts/
│   ├── generate_graphs.py    graph generators + experiment suites (seeded)
│   ├── benchmark.py          unified benchmark driver -> results/results.csv
│   ├── plot_results.py       figures + summary table from the CSV
│   └── run_experiments.sh    runs all of the above in order
├── graphs/                   generated inputs and manifest.csv (not committed)
└── results/                  results.csv, environment.txt, plots/ (not committed)
```

## Requirements

- A C++17 compiler (g++ 7 or newer, clang, or MSVC) for the three programs; no external C++ libraries
- Python 3.8+ for the scripts. Generating graphs and benchmarking use only the standard library; plotting needs matplotlib (`pip install -r requirements.txt`)

## Build

From the repository root:

```
mkdir -p bin
g++ -O2 -std=c++17 src/vc_exact.cpp  -o bin/vc_exact
g++ -O2 -std=c++17 src/vc_approx.cpp -o bin/vc_approx
g++ -O2 -std=c++17 src/vc_greedy.cpp -o bin/vc_greedy
```

On Windows with g++ (MinGW), the outputs will be `bin/vc_exact.exe` etc. Run them as `bin\vc_exact.exe example_graph.txt`.

## Run

Each program takes the graph file as its first argument:

```
bin/vc_exact  example_graph.txt
bin/vc_approx example_graph.txt
bin/vc_greedy example_graph.txt
```

You can also pipe the graph in on standard input:

```
bin/vc_exact < example_graph.txt
```

### Optional timeout (exact only)

The exact program stops after 60 seconds by default. Give a different limit in seconds as the second argument:

```
bin/vc_exact example_graph.txt 10
```

If the limit is hit, the output says `TIMED OUT` and the cover shown is only the best found so far, not proven optimal. Record this as "timed out" in your experiments: it is how you locate where the exact algorithm hits the wall.

## Input format

Plain text. The first line is `n m` (number of vertices, number of edges). Then `m` lines, each `u v`, an undirected edge between vertices `u` and `v`. Vertices are numbered `0` to `n-1`.

Example (a 4-cycle):

```
4 4
0 1
1 2
2 3
3 0
```

Notes:
- Self-loops are ignored by the greedy program. Avoid them in your inputs.
- Duplicate edges are harmless but unnecessary.
- To test your own graphs, save them in this format and pass the filename.

## Output

Every program prints:

- The algorithm name and the graph size (`n`, `m`)
- `Cover size` and the list of cover vertices
- `Valid cover: yes/no`, a check that every edge has at least one endpoint in the cover
- `Time (s)`, measured over the algorithm only (not file reading)

Extra lines per program:
- **Exact:** `Status: optimal` or `TIMED OUT`, and the number of search nodes visited
- **Approximation:** the matching size |M|, which is a lower bound on the optimum, and the resulting bounds on OPT
- **Heuristic:** a note that it carries no guarantee

## Expected output on `example_graph.txt`

| Program | Cover size |
|---|---|
| `vc_exact` | 5 (optimal) |
| `vc_approx` | 10 (the example has a perfect matching, so it takes every vertex) |
| `vc_greedy` | 5 |

The approximation ratio on this graph is 10 / 5 = 2, the worst case the guarantee allows.

## Reproducing the experiments

Everything below is run from the repository root. All graph generation is seeded, so the same commands always produce the same inputs.

One command (Linux/macOS/WSL/Git Bash):

```
pip install -r requirements.txt
bash scripts/run_experiments.sh full      # or: quick
```

Or step by step (works on Windows too):

```
# 1. Generate the graphs and graphs/manifest.csv
python3 scripts/generate_graphs.py --suite full --out-dir graphs

# 2. Compile the three programs (-O2 -std=c++17) and benchmark them on the same files
python3 scripts/benchmark.py --build --manifest graphs/manifest.csv \
        --out results/results.csv --repeats 5 --exact-timeout 10

# 3. Make the figures and results/plots/summary.csv
python3 scripts/plot_results.py --results results/results.csv --out-dir results/plots
```

`quick` is a small suite for checking that everything works (a few minutes). `full` is the suite for the report and is much longer, mainly because every graph on which the exact program hits its timeout costs the full timeout.

### Input families (scripts/generate_graphs.py)

| Label | Graph | What it probes |
|---|---|---|
| `er_sparse` | Erdos-Renyi, average degree 3 | where the exact search hits the wall on sparse graphs |
| `er_dense` | Erdos-Renyi, p = 0.5 | dense graphs: large cover, high degrees |
| `bipartite` | random bipartite, average degree 4 | true optimum known from Konig's theorem even when exact times out |
| `scale_free` | Barabasi-Albert, 3 edges per new vertex | hubs, which favour highest-degree greedy |
| `planted` | only vertices 0..0.3n-1 can cover edges | structured cover, OPT <= 0.3n |
| `complete_bipartite` | K(k,k) | worst case for the matching 2-approximation (ratio exactly 2) |
| `greedy_adversarial` | classic bipartite construction | greedy picks about k ln k vertices while OPT = k |
| `edge_order` | one random graph, many edge orderings | sensitivity of the matching algorithm to edge order |

A single graph can be made with, for example:

```
python3 scripts/generate_graphs.py --family er --n 100 --avg-degree 3 --seed 1 --out graphs/test.txt
python3 scripts/benchmark.py --graph graphs/test.txt
```

### Measurement method (scripts/benchmark.py)

- All three programs run on the same graph file. Timing is the programs' own "Time (s)", which covers the algorithm only (not file reading) and has microsecond resolution, so times under about 1 microsecond print as 0.
- Per graph: one untimed warm-up pass, then `--repeats` timed passes (default 5) with the three programs run back to back inside each pass. The median is the headline time; min, mean and stdev are also recorded.
- If a run takes longer than `--slow-threshold` seconds (default 2), it is not repeated further. A timed-out exact run is recorded once.
- Every cover is checked for validity by the programs, and the validity is stored in the CSV.
- `results/environment.txt` records the machine, compiler and settings used.

### Quality baseline

Each row stores `ratio` = cover size / baseline, with the baseline chosen in this order, and the choice recorded in `baseline_type`:

1. `exact`: the exact program finished and proved the optimum.
2. `known_opt`: the graph is bipartite, so the optimum equals the maximum matching size (Konig's theorem), computed by the generator.
3. `best_known`: the smallest valid cover found by any of the three programs. This is only an upper bound on OPT, so the ratio understates the true ratio.

`ratio_vs_lb` is cover size / |M|, where |M| is the maximal matching size (a lower bound on OPT), so it overstates the true ratio. The 2-approximation's own `ratio_vs_lb` is always exactly 2 by construction. If `exact` and `known_opt` ever disagree, the benchmark prints a warning.

### Outputs

- `results/results.csv`: one row per (graph, algorithm)
- `results/environment.txt`: hardware and compiler details
- `results/plots/runtime_by_family.png`, `ratio_by_family.png`, `ratio_summary.png`, `edge_order_sensitivity.png`
- `results/plots/summary.csv`: mean and max ratio and mean time per (family, algorithm)
