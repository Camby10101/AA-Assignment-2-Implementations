// Primary author: Matt
// Course: Advanced Algorithms, Assignment 2

// Exact minimum vertex cover: branch-and-bound.
// Build:  g++ -O2 -std=c++17 vc_exact.cpp -o vc_exact
// Run:    ./vc_exact example_graph.txt [timeout_seconds]
//         ./vc_exact < example_graph.txt
//
// Graph format: first line "n m", then m lines "u v" (vertices 0..n-1).

#include <bits/stdc++.h>
using namespace std;
using Clock = chrono::steady_clock;

int n, m;
vector<pair<int,int>> edges;
vector<char> inCover;
vector<char> bestCover;
int best;
bool timedOut = false;
double timeLimit = 60.0;
Clock::time_point startTime;
long long nodes = 0;

bool readGraph(istream &in) {
    if (!(in >> n >> m)) return false;
    edges.resize(m);
    for (auto &e : edges) {
        if (!(in >> e.first >> e.second)) return false;
        if (e.first < 0 || e.first >= n || e.second < 0 || e.second >= n) return false;
    }
    return true;
}

double elapsed() { return chrono::duration<double>(Clock::now() - startTime).count(); }

// Lower bound: size of a greedy maximal matching on the uncovered edges.
// Each matched edge needs its own cover vertex, so this is a valid bound.
int matchingLowerBound() {
    vector<char> used(n, 0);
    int lb = 0;
    for (auto &[u, v] : edges) {
        if (inCover[u] || inCover[v]) continue;
        if (!used[u] && !used[v]) { used[u] = used[v] = 1; lb++; }
    }
    return lb;
}

void solve(int size) {
    if (timedOut) return;
    if ((++nodes & 1023) == 0 && elapsed() > timeLimit) { timedOut = true; return; }

    // Degrees in the remaining (uncovered) graph.
    vector<int> deg(n, 0);
    int remaining = 0;
    for (auto &[u, v] : edges) {
        if (inCover[u] || inCover[v]) continue;
        deg[u]++; deg[v]++; remaining++;
    }
    if (remaining == 0) {               // valid cover found
        if (size < best) { best = size; bestCover = inCover; }
        return;
    }
    if (size + matchingLowerBound() >= best) return;   // prune

    // Branch on the highest-degree vertex v (ties: smallest ID):
    //   Branch A: v is in the cover.
    //   Branch B: v is not in the cover, so all its uncovered neighbours are.
    int v = 0;
    for (int i = 1; i < n; i++) if (deg[i] > deg[v]) v = i;

    // Branch A
    inCover[v] = 1;
    solve(size + 1);
    inCover[v] = 0;

    // Branch B
    vector<int> added;
    for (auto &[a, b] : edges) {
        int other = -1;
        if (a == v) other = b; else if (b == v) other = a;
        if (other >= 0 && !inCover[other]) { inCover[other] = 1; added.push_back(other); }
    }
    if (size + (int)added.size() < best) solve(size + (int)added.size());
    for (int x : added) inCover[x] = 0;
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
    if (argc > 2) timeLimit = atof(argv[2]);

    inCover.assign(n, 0);
    best = n + 1;                       // trivially worse than taking every vertex
    bestCover.assign(n, 1);
    startTime = Clock::now();
    solve(0);
    double t = elapsed();

    cout << "Algorithm: exact branch-and-bound\n";
    cout << "n = " << n << ", m = " << m << "\n";
    if (timedOut) {
        cout << "Status: TIMED OUT after " << timeLimit << "s (result below is best found, NOT proven optimal)\n";
    } else {
        cout << "Status: optimal\n";
    }
    if (best <= n) {
        cout << "Cover size: " << best << "\nCover: ";
        for (int i = 0; i < n; i++) if (bestCover[i]) cout << i << " ";
        cout << "\n";
        cout << "Valid cover: " << (isValidCover(bestCover) ? "yes" : "NO") << "\n";
    } else {
        cout << "No cover found before timeout\n";
    }
    cout << "Search nodes: " << nodes << "\n";
    cout << fixed << setprecision(6) << "Time (s): " << t << "\n";
    return 0;
}
