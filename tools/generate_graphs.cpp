// Shared group tool: graph generator for the vertex cover experiments.
// Course: Advanced Algorithms, Assignment 2
//
// Build:  g++ -O2 -std=c++17 tools/generate_graphs.cpp -o bin/generate_graphs
//         (bin/benchmark --build does this for you)
//
// Every graph is written in the format the vc_* programs read:
//     n m
//     u v        (m lines, vertices 0..n-1)
//
// Families
//   er                  Erdos-Renyi G(n,p). Give --p or --avg-degree.
//   bipartite           Random bipartite graph, sides a and b, edge probability p.
//   complete_bipartite  K(k,k). Worst case for the matching 2-approximation (ratio 2).
//   greedy_adversarial  Classic bipartite construction: highest-degree greedy picks about
//                       k*ln(k) vertices while OPT = k.
//   scale_free          Barabasi-Albert preferential attachment, k edges per new vertex.
//   planted             Only vertices 0..c-1 can cover edges, so OPT <= c (upper bound only).
//
// For bipartite graphs the exact optimum is computed with Hopcroft-Karp (Konig's theorem:
// minimum vertex cover = maximum matching) and stored in the manifest as known_opt, so the
// benchmark has a true baseline even where the branch-and-bound times out.
//
// Usage
//   Whole suite:   generate_graphs --suite quick|full [--out-dir graphs]
//                      [--seeds N] [--max-n N] [--families a,b,c] [--order-count N]
//   Single graph:  generate_graphs --family er --n 100 --avg-degree 3 --seed 1 --out g.txt
//                      [--order-seed K]
//
// All randomness is seeded and uses only std::mt19937_64 raw output (no std::*_distribution,
// whose results differ between standard libraries), so a given command produces the same
// files on every machine. --order-seed shuffles the order of the edges in the file (and the
// orientation of each edge) without changing the graph; this matters for the matching
// algorithm, which scans edges in input order.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using Edge = pair<int, int>;

// ---------------------------------------------------------------- RNG
struct Rng {
    mt19937_64 g;
    explicit Rng(uint64_t seed) : g(seed) {}
    double u01() { return (double)(g() >> 11) * (1.0 / 9007199254740992.0); }
    uint64_t below(uint64_t n) { return g() % n; }
};

// ---------------------------------------------------------------- graphs
struct Graph {
    int n = 0;
    vector<Edge> edges;
    int left = -1;      // size of left side if bipartite (vertices 0..left-1)
    int optUB = -1;     // planted upper bound on OPT, if any
};

struct Params {
    int n = 0, a = 0, b = 0, k = 0;
    double p = -1, avgDeg = -1, frac = 0;
};

Graph genER(int n, double p, Rng &r) {
    Graph g; g.n = n;
    for (int u = 0; u < n; u++)
        for (int v = u + 1; v < n; v++)
            if (r.u01() < p) g.edges.push_back({u, v});
    return g;
}

Graph genBipartite(int a, int b, double p, Rng &r) {
    Graph g; g.n = a + b; g.left = a;
    for (int i = 0; i < a; i++)
        for (int j = 0; j < b; j++)
            if (r.u01() < p) g.edges.push_back({i, a + j});
    return g;
}

Graph genCompleteBipartite(int k) {
    Graph g; g.n = 2 * k; g.left = k;
    for (int i = 0; i < k; i++)
        for (int j = 0; j < k; j++) g.edges.push_back({i, k + j});
    return g;
}

// Left side A = {0..k-1}. For each i = 2..k add floor(k/i) right-side vertices, each adjacent
// to a distinct block of i vertices of A. A is a cover of size k. When greedy must choose, a
// right vertex of group i has degree i while any A vertex has degree <= i-1, so greedy takes
// every right vertex: sum_{i=2..k} floor(k/i) ~ k ln k vertices.
Graph genGreedyAdversarial(int k) {
    Graph g; g.left = k;
    int next = k;
    for (int i = 2; i <= k; i++)
        for (int blk = 0; blk < k / i; blk++) {
            for (int t = 0; t < i; t++) g.edges.push_back({blk * i + t, next});
            next++;
        }
    g.n = next;
    return g;
}

Graph genScaleFree(int n, int k, Rng &r) {
    Graph g; g.n = n;
    k = max(1, min(k, n - 1));
    vector<int> repeated;
    for (int u = 0; u <= k; u++)
        for (int v = u + 1; v <= k; v++) {
            g.edges.push_back({u, v});
            repeated.push_back(u); repeated.push_back(v);
        }
    for (int nw = k + 1; nw < n; nw++) {
        set<int> targets;
        while ((int)targets.size() < k) targets.insert(repeated[r.below(repeated.size())]);
        for (int t : targets) {
            g.edges.push_back({t, nw});
            repeated.push_back(t); repeated.push_back(nw);
        }
    }
    return g;
}

Graph genPlanted(int n, double frac, double p, Rng &r) {
    Graph g; g.n = n;
    int c = (int)llround(frac * n);
    g.optUB = c;
    for (int u = 0; u < c; u++)
        for (int v = u + 1; v < n; v++)
            if (r.u01() < p) g.edges.push_back({u, v});
    return g;
}

// Exact optimum of a bipartite graph = maximum matching (Hopcroft-Karp + Konig).
int bipartiteOpt(const Graph &g) {
    int L = g.left, R = g.n - g.left;
    vector<vector<int>> adj(L);
    for (auto [u, v] : g.edges) {
        if (u < L && v >= L) adj[u].push_back(v - L);
        else if (v < L && u >= L) adj[v].push_back(u - L);
        else { cerr << "edge inside one side: graph is not bipartite as declared\n"; exit(1); }
    }
    const int INF = 1 << 29;
    vector<int> mL(L, -1), mR(R, -1), dist(L);
    auto bfs = [&]() {
        queue<int> q;
        for (int u = 0; u < L; u++) { dist[u] = (mL[u] == -1) ? 0 : INF; if (mL[u] == -1) q.push(u); }
        bool found = false;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            for (int v : adj[u]) {
                int w = mR[v];
                if (w == -1) found = true;
                else if (dist[w] == INF) { dist[w] = dist[u] + 1; q.push(w); }
            }
        }
        return found;
    };
    auto dfs = [&](auto &&self, int u) -> bool {
        for (int v : adj[u]) {
            int w = mR[v];
            if (w == -1 || (dist[w] == dist[u] + 1 && self(self, w))) { mL[u] = v; mR[v] = u; return true; }
        }
        dist[u] = INF;
        return false;
    };
    int size = 0;
    while (bfs())
        for (int u = 0; u < L; u++)
            if (mL[u] == -1 && dfs(dfs, u)) size++;
    return size;
}

// ---------------------------------------------------------------- output
void applyEdgeOrder(vector<Edge> &edges, long orderSeed) {
    if (orderSeed < 0) return;
    Rng r((uint64_t)orderSeed);
    for (int i = (int)edges.size() - 1; i > 0; i--) swap(edges[i], edges[r.below(i + 1)]);
    for (auto &e : edges) if (r.u01() < 0.5) swap(e.first, e.second);
}

void writeGraph(const string &path, const Graph &g) {
    ofstream f(path);
    if (!f) { cerr << "Cannot write " << path << "\n"; exit(1); }
    f << g.n << " " << g.edges.size() << "\n";
    for (auto &e : g.edges) f << e.first << " " << e.second << "\n";
}

Graph build(const string &family, const Params &P, uint64_t seed) {
    Rng r(seed);
    if (family == "er") {
        double p = P.p >= 0 ? P.p : min(1.0, P.avgDeg / max(1, P.n - 1));
        return genER(P.n, p, r);
    }
    if (family == "bipartite") {
        double p = P.p >= 0 ? P.p : min(1.0, P.avgDeg * (P.a + P.b) / (2.0 * P.a * P.b));
        return genBipartite(P.a, P.b, p, r);
    }
    if (family == "complete_bipartite") return genCompleteBipartite(P.k);
    if (family == "greedy_adversarial") return genGreedyAdversarial(P.k);
    if (family == "scale_free") return genScaleFree(P.n, P.k, r);
    if (family == "planted") return genPlanted(P.n, P.frac, P.p, r);
    cerr << "unknown family " << family << "\n";
    exit(1);
}

// ---------------------------------------------------------------- suites
struct Spec {
    string label, family;
    Params params;
    int seed = 0;
    long orderSeed = -1;
};

struct SuiteOpts {
    string kind = "quick";
    int seeds = -1;          // instances per size for random families (default depends on suite)
    int maxN = 0;            // drop graphs with more than this many vertices (0 = no limit)
    set<string> only;        // keep only these labels (empty = all)
    int orderCount = -1;     // number of edge orderings in the edge_order experiment
};

// EDIT THE SIZE LISTS HERE to change which graph sizes the experiments cover.
vector<Spec> suiteSpecs(const SuiteOpts &o) {
    bool full = (o.kind == "full");
    if (!full && o.kind != "quick") { cerr << "suite must be quick or full\n"; exit(1); }
    int seeds = o.seeds > 0 ? o.seeds : (full ? 3 : 1);
    int orders = o.orderCount >= 0 ? o.orderCount : (full ? 30 : 10);

    vector<int> erSparseN = full ? vector<int>{20, 40, 60, 80, 100, 150, 200, 250, 300, 500, 1000}
                                 : vector<int>{20, 40, 80, 160};
    vector<int> erDenseN  = full ? vector<int>{20, 40, 60, 80, 100, 125, 150, 175, 200, 250}
                                 : vector<int>{15, 20, 25, 30};
    vector<int> bipN      = full ? vector<int>{20, 40, 60, 80, 100, 150, 200, 300, 500, 1000}
                                 : vector<int>{20, 40, 80, 160};
    vector<int> sfN       = full ? vector<int>{20, 40, 60, 80, 100, 150, 200, 250, 300, 500, 1000}
                                 : vector<int>{20, 40, 80, 160};
    vector<int> plantedN  = full ? vector<int>{20, 40, 60, 80, 100, 150, 200, 300, 500, 1000}
                                 : vector<int>{20, 40, 80, 160};
    vector<int> kbipK     = full ? vector<int>{5, 10, 20, 40, 80, 160, 320} : vector<int>{5, 10, 20, 40};
    vector<int> advK      = full ? vector<int>{5, 10, 15, 20, 30, 40, 60, 80, 100} : vector<int>{5, 10, 15, 20};

    vector<Spec> specs;
    auto add = [&](const string &label, const string &family, Params p, int seed, long os = -1) {
        specs.push_back({label, family, p, seed, os});
    };
    for (int s = 1; s <= seeds; s++) {
        for (int n : erSparseN) { Params p; p.n = n; p.avgDeg = 3.0; add("er_sparse", "er", p, s); }
        for (int n : erDenseN)  { Params p; p.n = n; p.p = 0.5; add("er_dense", "er", p, s); }
        for (int n : bipN)      { Params p; p.a = n / 2; p.b = n - n / 2; p.avgDeg = 4.0; add("bipartite", "bipartite", p, s); }
        for (int n : sfN)       { Params p; p.n = n; p.k = 3; add("scale_free", "scale_free", p, s); }
        for (int n : plantedN)  { Params p; p.n = n; p.frac = 0.3; p.p = min(1.0, 10.0 / n); add("planted", "planted", p, s); }
    }
    for (int k : kbipK) { Params p; p.k = k; add("complete_bipartite", "complete_bipartite", p, 0); }
    for (int k : advK)  { Params p; p.k = k; add("greedy_adversarial", "greedy_adversarial", p, 0); }
    // Edge-order experiment: ONE graph, many edge orderings (order 0 = file order as generated).
    for (int o2 = 0; o2 <= orders; o2++) {
        Params p; p.n = 150; p.avgDeg = 3.0;
        add("edge_order", "er", p, 1, o2 == 0 ? -1 : o2);
    }
    return specs;
}

string describe(const Params &p) {
    ostringstream o; bool first = true;
    auto put = [&](const string &k, double v) { o << (first ? "" : ";") << k << "=" << v; first = false; };
    if (p.n) put("n", p.n);
    if (p.a) put("a", p.a);
    if (p.b) put("b", p.b);
    if (p.k) put("k", p.k);
    if (p.p >= 0) put("p", p.p);
    if (p.avgDeg >= 0) put("avg_degree", p.avgDeg);
    if (p.frac > 0) put("frac", p.frac);
    return o.str();
}

void generateSuite(const SuiteOpts &o, const string &outDir) {
    filesystem::create_directories(outDir);
    ofstream man(filesystem::path(outDir) / "manifest.csv");
    man << "file,label,family,n,m,seed,order_seed,params,known_opt,opt_upper_bound\n";
    int count = 0;
    for (const Spec &s : suiteSpecs(o)) {
        if (!o.only.empty() && !o.only.count(s.label)) continue;
        Graph g = build(s.family, s.params, (uint64_t)s.seed);
        if (o.maxN > 0 && g.n > o.maxN) continue;
        string known = g.left >= 0 ? to_string(bipartiteOpt(g)) : "";
        applyEdgeOrder(g.edges, s.orderSeed);
        bool kFamily = (s.family == "complete_bipartite" || s.family == "greedy_adversarial");
        string name = s.label + "_" + (kFamily ? "k" + to_string(s.params.k) : "n" + to_string(g.n)) +
                      "_s" + to_string(s.seed);
        if (s.label == "edge_order") name += "_o" + to_string(max(0L, s.orderSeed));
        name += ".txt";
        writeGraph((filesystem::path(outDir) / name).string(), g);
        man << name << "," << s.label << "," << s.family << "," << g.n << "," << g.edges.size() << ","
            << s.seed << "," << (s.orderSeed >= 0 ? to_string(s.orderSeed) : "") << ","
            << describe(s.params) << "," << known << "," << (g.optUB >= 0 ? to_string(g.optUB) : "") << "\n";
        count++;
    }
    cout << "Wrote " << count << " graphs and " << (filesystem::path(outDir) / "manifest.csv").string() << "\n";
}

// ---------------------------------------------------------------- main
int main(int argc, char **argv) {
    SuiteOpts so;
    bool suite = false;
    string outDir = "graphs", family, outFile;
    Params P;
    uint64_t seed = 1;
    long orderSeed = -1;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        auto val = [&]() -> string {
            if (i + 1 >= argc) { cerr << "missing value for " << a << "\n"; exit(1); }
            return argv[++i];
        };
        if (a == "--suite") { suite = true; so.kind = val(); }
        else if (a == "--out-dir") outDir = val();
        else if (a == "--seeds") so.seeds = stoi(val());
        else if (a == "--max-n") so.maxN = stoi(val());
        else if (a == "--order-count") so.orderCount = stoi(val());
        else if (a == "--families") {
            stringstream ss(val()); string t;
            while (getline(ss, t, ',')) if (!t.empty()) so.only.insert(t);
        }
        else if (a == "--family") family = val();
        else if (a == "--n") P.n = stoi(val());
        else if (a == "--p") P.p = stod(val());
        else if (a == "--avg-degree") P.avgDeg = stod(val());
        else if (a == "--a") P.a = stoi(val());
        else if (a == "--b") P.b = stoi(val());
        else if (a == "--k") P.k = stoi(val());
        else if (a == "--frac") P.frac = stod(val());
        else if (a == "--seed") seed = stoull(val());
        else if (a == "--order-seed") orderSeed = stol(val());
        else if (a == "--out") outFile = val();
        else { cerr << "unknown option " << a << "\n"; return 1; }
    }
    if (suite) { generateSuite(so, outDir); return 0; }
    if (family.empty() || outFile.empty()) {
        cerr << "give --suite quick|full, or --family and --out (see the header comment)\n";
        return 1;
    }
    Graph g = build(family, P, seed);
    string extra;
    if (g.left >= 0) extra = ", known OPT=" + to_string(bipartiteOpt(g)) + " (bipartite, Konig)";
    if (g.optUB >= 0) extra += ", OPT <= " + to_string(g.optUB) + " (planted)";
    applyEdgeOrder(g.edges, orderSeed);
    writeGraph(outFile, g);
    cout << "Wrote " << outFile << ": n=" << g.n << ", m=" << g.edges.size() << extra << "\n";
    return 0;
}
