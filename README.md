# Vertex Cover: Exact vs Approximation vs Heuristic

Three standalone C++ programs that solve minimum vertex cover on the same input graph.

| File | Method | Type | Guarantee |
|---|---|---|---|
| `vc_exact.cpp` | Branch-and-bound | Exact | Always optimal (exponential worst-case time) |
| `vc_approx.cpp` | Maximal matching | Approximation | Cover size at most 2 x optimal |
| `vc_greedy.cpp` | Highest-degree greedy | Heuristic | None (no constant ratio) |
| `example_graph.txt` | Example input | n/a | n/a |

## Requirements

- A C++17 compiler (g++ 7 or newer, clang, or MSVC)
- No external libraries

## Build

From inside this folder:

```
g++ -O2 -std=c++17 vc_exact.cpp  -o vc_exact
g++ -O2 -std=c++17 vc_approx.cpp -o vc_approx
g++ -O2 -std=c++17 vc_greedy.cpp -o vc_greedy
```

On Windows with g++ (MinGW), the outputs will be `vc_exact.exe` etc. Run them as `vc_exact.exe example_graph.txt`.

## Run

Each program takes the graph file as its first argument:

```
./vc_exact  example_graph.txt
./vc_approx example_graph.txt
./vc_greedy example_graph.txt
```

You can also pipe the graph in on standard input:

```
./vc_exact < example_graph.txt
```

### Optional timeout (exact only)

The exact program stops after 60 seconds by default. Give a different limit in seconds as the second argument:

```
./vc_exact example_graph.txt 10
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

## Comparing the algorithms fairly

- Run all three on the same graph file.
- Compute the ratio as `algorithm cover size / exact cover size`. If the exact program times out, use the matching lower bound from `vc_approx` as the baseline instead, and say so in the report.
- Tie-breaking is fixed (input edge order for matching, smallest vertex ID for greedy), so results are reproducible. Changing the order of edges in the file can change the matching result.
- Time with `-O2` on the same machine, and repeat runs to average out noise.
