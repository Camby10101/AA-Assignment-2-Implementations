// Primary author: Luke
// Course: Advanced Algorithms, Assignment 2

// Heuristic for minimum vertex cover: highest-degree greedy.
// Build:  g++ -O2 -std=c++17 vc_greedy.cpp -o vc_greedy
// Run:    ./vc_greedy example_graph.txt
//         ./vc_greedy < example_graph.txt
//
// Graph format: first line "n m", then m lines "u v" (vertices 0..n-1).
//
// No constant-factor guarantee: on adversarial graphs the cover can be
// about ln(n) times larger than optimal. Often better than the
// 2-approximation on random graphs.
// Ties are broken by smallest vertex ID.

#include <bits/stdc++.h>
using namespace std;
using Clock = chrono::steady_clock;

int n, m;
vector<pair<int,int>> edges;

bool readGraph(istream &in) {
    if (!(in >> n >> m)) return false;
    edges.resize(m);
    for (auto &e : edges) {
        if (!(in >> e.first >> e.second)) return false;
        if (e.first < 0 || e.first >= n || e.second < 0 || e.second >= n) return false;
    }
    return true;
}

bool isValidCover(const vector<char> &c) {
    for (auto &[u, v] : edges) if (!c[u] && !c[v]) return false;
    return true;
}

int main(int argc, char **argv) {
    bool ok;
    if (argc > 1) {
        ifstream f(argv[1]);
        if (!f) { cerr << "Cannot open " << argv[1] << "\n"; return 1; }
        ok = readGraph(f);
    } else {
        ok = readGraph(cin);
    }
    if (!ok) { cerr << "Bad graph file format\n"; return 1; }

    auto t0 = Clock::now();

    vector<vector<int>> adj(n);
    vector<int> deg(n, 0);
    for (auto &[u, v] : edges) {
        if (u == v) continue;              // ignore self-loops
        adj[u].push_back(v);
        adj[v].push_back(u);
        deg[u]++; deg[v]++;
    }

    // Ordered set of (-degree, vertex): begin() is the highest degree,
    // smallest ID on ties. Updates cost O(log n), so no O(n) rescans.
    set<pair<int,int>> pq;
    for (int i = 0; i < n; i++) if (deg[i] > 0) pq.insert({-deg[i], i});

    vector<char> inCover(n, 0);
    while (!pq.empty()) {
        int v = pq.begin()->second;
        pq.erase(pq.begin());
        inCover[v] = 1;
        // Removing v deletes its remaining edges: lower neighbours' degrees.
        for (int u : adj[v]) {
            if (inCover[u] || deg[u] == 0) continue;
            pq.erase({-deg[u], u});
            deg[u]--;
            if (deg[u] > 0) pq.insert({-deg[u], u});
        }
        deg[v] = 0;
    }

    double t = chrono::duration<double>(Clock::now() - t0).count();

    int size = 0;
    for (int i = 0; i < n; i++) size += inCover[i];

    cout << "Algorithm: heuristic (highest-degree greedy)\n";
    cout << "n = " << n << ", m = " << m << "\n";
    cout << "Cover size: " << size << "\nCover: ";
    for (int i = 0; i < n; i++) if (inCover[i]) cout << i << " ";
    cout << "\n";
    cout << "Valid cover: " << (isValidCover(inCover) ? "yes" : "NO") << "\n";
    cout << "Guarantee: none (no constant approximation ratio)\n";
    cout << fixed << setprecision(6) << "Time (s): " << t << "\n";
    return 0;
}
