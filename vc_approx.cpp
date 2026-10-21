// 2-approximation for minimum vertex cover via maximal matching.
// Build:  g++ -O2 -std=c++17 vc_approx.cpp -o vc_approx
// Run:    ./vc_approx example_graph.txt
//         ./vc_approx < example_graph.txt
//
// Graph format: first line "n m", then m lines "u v" (vertices 0..n-1).
//
// Guarantee: |cover| = 2|M| <= 2*OPT, because the matched edges M share no
// endpoints, so any cover (including the optimum) needs >= |M| vertices.
// So |M| is also a LOWER BOUND on OPT, which this program reports.

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

    vector<char> inCover(n, 0);
    int matchingSize = 0;
    // Scan edges in input order (fixed, deterministic rule).
    for (auto &[u, v] : edges) {
        if (!inCover[u] && !inCover[v]) {   // edge still uncovered: match it
            inCover[u] = inCover[v] = 1;    // take BOTH endpoints
            matchingSize++;
        }
    }

    double t = chrono::duration<double>(Clock::now() - t0).count();

    int size = 0;
    for (int i = 0; i < n; i++) size += inCover[i];

    cout << "Algorithm: 2-approximation (maximal matching)\n";
    cout << "n = " << n << ", m = " << m << "\n";
    cout << "Cover size: " << size << "\nCover: ";
    for (int i = 0; i < n; i++) if (inCover[i]) cout << i << " ";
    cout << "\n";
    cout << "Valid cover: " << (isValidCover(inCover) ? "yes" : "NO") << "\n";
    cout << "Matching size |M| (lower bound on OPT): " << matchingSize << "\n";
    cout << "Bounds: " << matchingSize << " <= OPT <= " << size
         << " (= 2|M|), so ratio vs OPT is at most 2\n";
    cout << fixed << setprecision(6) << "Time (s): " << t << "\n";
    return 0;
}
