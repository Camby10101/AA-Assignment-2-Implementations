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

The benchmarking driver, graph generator and plotting program in `tools/` are shared group work. They are also C++, so the whole project needs only a C++ compiler.

## Repository layout

```
.
├── README.md
├── example_graph.txt         10-vertex example input
├── src/                      the three implementations (C++17)
│   ├── vc_exact.cpp
│   ├── vc_approx.cpp
│   └── vc_greedy.cpp
├── tools/                    shared experiment infrastructure (C++17)
│   ├── benchmark.cpp         unified driver: the ONE program to run for the experiments
│   ├── generate_graphs.cpp   graph generators + experiment suites (seeded)
│   └── plot_results.cpp      figures (SVG) + summary table from the results CSV
├── graphs/                   generated inputs and manifest.csv (not committed)
└── results/                  results.csv, environment.txt, plots/ (not committed)
```

## Requirements

A C++17 compiler with `<filesystem>` support (g++ 9 or newer, clang 9 or newer, or a recent MSVC). No external libraries and no Python.

## Build

From the repository root (create `bin` first: `mkdir bin`):

```
g++ -O2 -std=c++17 src/vc_exact.cpp  -o bin/vc_exact
g++ -O2 -std=c++17 src/vc_approx.cpp -o bin/vc_approx
g++ -O2 -std=c++17 src/vc_greedy.cpp -o bin/vc_greedy
```

To build the experiment tools as well, compile the driver and let it build everything else (see "Reproducing the experiments"):

```
g++ -O2 -std=c++17 tools/benchmark.cpp -o bin/benchmark
bin/benchmark --build --graph example_graph.txt
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

Everything is run from the repository root. Graph generation is seeded (`std::mt19937_64` raw output only), so the same command produces the same inputs on every machine.

**The only program you run is `bin/benchmark`.** Build it once, then run it:

```
mkdir bin
g++ -O2 -std=c++17 tools/benchmark.cpp -o bin/benchmark
bin/benchmark --build --suite full
```

On Windows PowerShell use `bin\benchmark.exe --build --suite full`.

One run does four things: (1) `--build` compiles the three algorithms, the generator and the plotter into `bin/` with `-O2 -std=c++17`; (2) generates the graphs into `graphs/`; (3) runs all three algorithms on the same files and writes `results/results.csv` and `results/environment.txt`; (4) writes the figures and `summary.csv` to `results/plots/`.

`--suite quick` is a small suite for checking that everything works (under a minute). `--suite full` is the suite for the report: it took about 10 minutes on a single-core test machine with the default settings, mainly because every graph on which the exact program hits its timeout costs the full timeout.

### Adjusting a run

| Option | Default | Effect |
|---|---|---|
| `--suite quick\|full` | quick | which set of graph sizes to use |
| `--repeats N` | 5 | timed passes per graph (more = steadier medians) |
| `--exact-timeout S` | 10 | seconds before `vc_exact` gives up; raise it to push the wall further out |
| `--slow-threshold S` | 2 | runs longer than this are not repeated |
| `--seeds N` | quick 1, full 3 | random instances per size for the random families |
| `--max-n N` | none | skip graphs with more than N vertices (faster runs) |
| `--families a,b,c` | all | only these labels (see the table below) |
| `--order-count N` | quick 10, full 30 | number of edge orderings in the `edge_order` experiment |
| `--no-generate` | off | reuse the existing `graphs/` instead of regenerating |
| `--no-plot` | off | skip the plotting step |
| `--graph FILE` | none | benchmark one graph file instead of a suite |
| `--out-dir`, `--graph-dir`, `--bin-dir`, `--cxx` | `results`, `graphs`, `bin`, `g++` | locations and compiler |

Examples:

```
bin/benchmark --suite full --exact-timeout 30 --repeats 10      # steadier, further out
bin/benchmark --suite full --families er_sparse,greedy_adversarial
bin/benchmark --suite quick --max-n 100
bin/benchmark --no-generate --suite full --repeats 9            # rerun timings on the same graphs
```

To change which graph sizes a suite contains (or the graph parameters), edit the size lists in `suiteSpecs()` in `tools/generate_graphs.cpp` and rebuild with `--build`.

### Input families (tools/generate_graphs.cpp)

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

A single graph can be made and benchmarked with, for example:

```
bin/generate_graphs --family er --n 100 --avg-degree 3 --seed 1 --out graphs/test.txt
bin/benchmark --graph graphs/test.txt
```

### Measurement method (tools/benchmark.cpp)

- All three programs run on the same graph file. Timing is the programs' own "Time (s)", which covers the algorithm only (not file reading) and has microsecond resolution, so times under about 1 microsecond print as 0.
- Per graph: one untimed warm-up pass, then `--repeats` timed passes (default 5) with the three programs run back to back inside each pass. The median is the headline time; min, mean and stdev are also recorded.
- If a run takes longer than `--slow-threshold` seconds (default 2), it is not repeated further. A timed-out exact run is recorded once.
- Every cover is checked for validity by the programs, and the validity is stored in the CSV.
- `results/environment.txt` records the date, OS, compiler version and settings. CPU model and RAM are not detected automatically, so add them by hand for the report.

### Quality baseline

Each row stores `ratio` = cover size / baseline, with the baseline chosen in this order, and the choice recorded in `baseline_type`:

1. `exact`: the exact program finished and proved the optimum.
2. `known_opt`: the graph is bipartite, so the optimum equals the maximum matching size (Konig's theorem), computed by the generator.
3. `best_known`: the smallest valid cover found by any of the three programs. This is only an upper bound on OPT, so the ratio understates the true ratio.

`ratio_vs_lb` is cover size / |M|, where |M| is the maximal matching size (a lower bound on OPT), so it overstates the true ratio. The 2-approximation's own `ratio_vs_lb` is always exactly 2 by construction. If `exact` and `known_opt` ever disagree, the benchmark prints a warning.

### Outputs

- `results/results.csv`: one row per (graph, algorithm)
- `results/environment.txt`: machine and compiler details
- `results/plots/runtime_by_family.svg`, `ratio_by_family.svg`, `ratio_summary.svg`, `edge_order_sensitivity.svg`
- `results/plots/summary.csv`: mean and max ratio and mean time per (family, algorithm)

The figures are SVG: open them in a web browser, or insert them directly into Word, PowerPoint or Google Docs.
