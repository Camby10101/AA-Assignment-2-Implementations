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

int numVertices, numEdges;
vector<pair<int,int>> edges;

bool readGraph(istream &input) {
    if (!(input >> numVertices >> numEdges)) return false;
    edges.resize(numEdges);
    for (auto &edge : edges) {
        if (!(input >> edge.first >> edge.second)) return false;
        if (edge.first < 0 || edge.first >= numVertices || edge.second < 0 || edge.second >= numVertices) return false;
    }
    return true;
}

bool isValidCover(const vector<char> &c) {
    for (auto &[vertex, neighbour] : edges) if (!c[vertex] && !c[neighbour]) return false;
    return true;
}

int main(int argc, char **argv) {
    bool graphReadSuccessfully;
    if (argc > 1) {
        ifstream f(argv[1]);
        if (!f) { cerr << "Cannot open " << argv[1] << "\n"; return 1; }
        graphReadSuccessfully = readGraph(f);
    } else {
        graphReadSuccessfully = readGraph(cin);
    }
    if (!graphReadSuccessfully) { cerr << "Bad graph file format\n"; return 1; }

    auto t0 = Clock::now();

    vector<vector<int>> adj(numVertices);
    vector<int> deg(numVertices, 0);
    for (auto &[vertex, neighbour] : edges) {
        if (vertex == neighbour) continue;              // ignore self-loops
        adj[vertex].push_back(neighbour);
        adj[neighbour].push_back(vertex);
        deg[vertex]++; deg[neighbour]++;
    }

    // Ordered set of (-degree, vertex): begin() is the highest degree,
    // smallest ID on ties. Updates cost O(log n), so no O(n) rescans.
    set<pair<int,int>> pq;
    for (int i = 0; i < numVertices; i++) if (deg[i] > 0) pq.insert({-deg[i], i});

    vector<char> inCover(numVertices, 0);
    while (!pq.empty()) {
        int selectedVertex = pq.begin()->second;
        pq.erase(pq.begin());
        inCover[selectedVertex] = 1;
        // Removing neighbour deletes its remaining edges: lower neighbours' degrees.
        for (int vertex : adj[selectedVertex]) {
            if (inCover[vertex] || deg[vertex] == 0) continue;
            pq.erase({-deg[vertex], vertex});
            deg[vertex]--;
            if (deg[vertex] > 0) pq.insert({-deg[vertex], vertex});
        }
        deg[selectedVertex] = 0;
    }

    double t = chrono::duration<double>(Clock::now() - t0).count();

    int coverSize = 0;
    for (int i = 0; i < numVertices; i++) coverSize += inCover[i];

    cout << "Algorithm: heuristic (highest-degree greedy)\n";
    cout << "n = " << numVertices << ", m = " << numEdges << "\n";
    cout << "Cover size: " << coverSize << "\nCover: ";
    for (int i = 0; i < numVertices; i++) if (inCover[i]) cout << i << " ";
    cout << "\n";
    cout << "Valid cover: " << (isValidCover(inCover) ? "yes" : "NO") << "\n";
    cout << "Guarantee: none (no constant approximation ratio)\n";
    cout << fixed << setprecision(6) << "Time (s): " << t << "\n";
    return 0;
}

