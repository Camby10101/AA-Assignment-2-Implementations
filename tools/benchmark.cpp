// Shared group tool: unified benchmark driver and the ONE program to run for the experiments.
// Course: Advanced Algorithms, Assignment 2
//
// Build (once, from the repository root):
//     mkdir bin
//     g++ -O2 -std=c++17 tools/benchmark.cpp -o bin/benchmark
// Run (from the repository root):
//     bin/benchmark --build --suite full
//
// What one run does
//   1. --build          compiles the three algorithms, generate_graphs and plot_results into bin/
//                       with -O2 -std=c++17 (use --cxx to pick a compiler, or set CXX).
//   2. generate         runs bin/generate_graphs to make graphs/ and graphs/manifest.csv
//                       (skip with --no-generate to reuse existing graphs).
//   3. benchmark        runs vc_exact, vc_approx and vc_greedy on the SAME graph files and
//                       writes results/results.csv and results/environment.txt.
//   4. plot             runs bin/plot_results to write results/plots/*.svg and summary.csv
//                       (skip with --no-plot).
//
// Options (all optional)
//   --suite quick|full     which experiment suite (default quick)
//   --repeats N            timed passes per graph (default 5)
//   --exact-timeout S      seconds before vc_exact gives up (default 10)
//   --slow-threshold S     runs longer than this are not repeated (default 2)
//   --seeds N              random instances per size (default: quick 1, full 3)
//   --max-n N              skip graphs with more than N vertices
//   --families a,b,c       only these families (er_sparse, er_dense, bipartite, scale_free,
//                          planted, complete_bipartite, greedy_adversarial, edge_order)
//   --order-count N        number of edge orderings in the edge_order experiment
//   --graph FILE           benchmark a single graph file instead of a suite
//   --manifest FILE, --graph-dir DIR, --out-dir DIR, --bin-dir DIR, --cxx COMPILER
//
// Timing
//   The vc_* programs time only the algorithm (file reading excluded) and print "Time (s)"
//   with microsecond resolution, so that number is used directly; times under 1 microsecond
//   print as 0.
//   * One untimed warm-up pass over the three programs per graph (file cache, CPU).
//   * Then --repeats timed passes, running the three programs back to back inside each pass
//     so slow drift on the machine hits all of them equally.
//   * Median is the headline number; min, mean and stdev are also recorded.
//   * A run longer than --slow-threshold is not repeated. A timed-out exact run is recorded
//     once. The exact program enforces its own timeout.
//
// Quality baselines, best first (ratio = cover size / baseline):
//   1. exact       vc_exact finished and proved the optimum
//   2. known_opt   bipartite graph: optimum = maximum matching (Konig), from the generator
//   3. best_known  smallest valid cover found by any program (only an UPPER bound on OPT, so
//                  the ratio UNDERSTATES the true ratio)
//   ratio_vs_lb = cover size / |M| (|M| = matching size, a LOWER bound on OPT), which
//   OVERSTATES the true ratio. The 2-approximation's own ratio_vs_lb is exactly 2 by
//   construction. The true ratio lies between the two.
//
// Commands are run with popen/system, so run this from the repository root. Paths with spaces
// in the repository location are fine as long as you run it from the root.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
static const char *EXE = ".exe";
static const char *OS_NAME = "Windows";
#else
#define POPEN popen
#define PCLOSE pclose
static const char *EXE = "";
#if defined(__APPLE__)
static const char *OS_NAME = "macOS";
#else
static const char *OS_NAME = "Linux/Unix";
#endif
#endif

struct Algo { string key, prog; };
static const vector<Algo> ALGOS = {{"exact", "vc_exact"}, {"approx", "vc_approx"}, {"greedy", "vc_greedy"}};

struct Options {
    string suite = "quick", manifest, graphDir, graphFile, outDir = "results", binDir = "bin";
    string cxx, families, extraGen;
    int repeats = 5, seeds = -1, maxN = 0, orderCount = -1;
    double exactTimeout = 10.0, slowThreshold = 2.0;
    bool build = false, noGenerate = false, noPlot = false;
};

// ---------------------------------------------------------------- helpers
string quote(const string &s) { return "\"" + s + "\""; }

string nativePath(const fs::path &p) {
    fs::path q = p;
    return q.make_preferred().string();
}

string exePath(const string &binDir, const string &name) {
    return nativePath(fs::path(binDir) / (name + EXE));
}

int runSystem(const string &cmd) {
    cout << flush;
#ifdef _WIN32
    return system(("\"" + cmd + "\"").c_str());     // cmd.exe strips one outer pair of quotes
#else
    return system(cmd.c_str());
#endif
}

string num(double v) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

vector<string> splitCsv(const string &line) {
    vector<string> out;
    string cur;
    for (char c : line) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else if (c != '\r' && c != '\n') cur += c;
    }
    out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------- running one program
struct RunResult {
    bool ok = false;
    string status = "error";   // exact: optimal|timed_out ; others: ok ; else error
    long cover = -1;
    string valid = "n/a";
    double time = NAN;
    long nodes = -1;
    long match = -1;
};

bool startsWith(const string &s, const string &p) { return s.compare(0, p.size(), p) == 0; }

long parseLongAfterColon(const string &line) {
    size_t c = line.find(':');
    if (c == string::npos) return -1;
    return atol(line.c_str() + c + 1);
}

RunResult runOnce(const Options &o, const Algo &a, const string &graph) {
    string cmd = quote(exePath(o.binDir, a.prog)) + " " + quote(nativePath(graph));
    if (a.key == "exact") cmd += " " + num(o.exactTimeout);
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif
    RunResult r;
    FILE *p = POPEN(cmd.c_str(), "r");
    if (!p) return r;
    string out;
    char buf[4096];
    while (size_t k = fread(buf, 1, sizeof buf, p)) out.append(buf, k);
    PCLOSE(p);

    bool timedOut = false, optimal = false;
    stringstream ss(out);
    string line;
    while (getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (startsWith(line, "Cover size:")) r.cover = parseLongAfterColon(line);
        else if (startsWith(line, "Valid cover:")) {
            r.valid = line.substr(line.find(':') + 1);
            r.valid.erase(0, r.valid.find_first_not_of(' '));
        }
        else if (startsWith(line, "Time (s):")) r.time = atof(line.c_str() + line.find(':') + 1);
        else if (startsWith(line, "Search nodes:")) r.nodes = parseLongAfterColon(line);
        else if (startsWith(line, "Matching size |M|")) r.match = parseLongAfterColon(line);
        else if (startsWith(line, "Status:")) {
            if (line.find("TIMED OUT") != string::npos) timedOut = true;
            else if (line.find("optimal") != string::npos) optimal = true;
        }
    }
    if (a.key == "exact") {
        r.status = timedOut ? "timed_out" : (optimal ? "optimal" : "error");
        r.ok = (timedOut || optimal) && !isnan(r.time);
    } else {
        r.status = (r.cover >= 0 && !isnan(r.time)) ? "ok" : "error";
        r.ok = (r.status == "ok");
    }
    return r;
}

// ---------------------------------------------------------------- per-graph benchmark
struct AlgoStats {
    long cover = -1;
    string valid = "n/a", status = "error";
    double tMedian = NAN, tMin = NAN, tMean = NAN, tStd = 0;
    int repeats = 0;
    long nodes = -1, matchLb = -1;
    double baseline = NAN, ratio = NAN, ratioLb = NAN;
    string baselineType;
};

double median(vector<double> v) {
    sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

map<string, AlgoStats> benchmarkGraph(const Options &o, const string &path) {
    map<string, vector<RunResult>> runs;
    map<string, bool> done;
    // Warm-up pass (timings discarded), except that a timed-out exact run is kept as the
    // measurement: re-running a full timeout would only repeat the same number.
    for (auto &a : ALGOS) {
        RunResult r = runOnce(o, a, path);
        if (r.status == "timed_out" || !r.ok) { runs[a.key].push_back(r); done[a.key] = true; }
    }
    for (int rep = 0; rep < o.repeats; rep++) {
        for (auto &a : ALGOS) {
            if (done[a.key]) continue;
            RunResult r = runOnce(o, a, path);
            runs[a.key].push_back(r);
            if (r.status == "timed_out" || !r.ok || (!isnan(r.time) && r.time > o.slowThreshold)) done[a.key] = true;
        }
        bool every = true;
        for (auto &a : ALGOS) every = every && done[a.key];
        if (every) break;
    }

    map<string, AlgoStats> out;
    for (auto &a : ALGOS) {
        auto &rs = runs[a.key];
        AlgoStats s;
        if (rs.empty()) { out[a.key] = s; continue; }
        vector<double> t;
        bool allYes = true;
        for (auto &r : rs) {
            if (!isnan(r.time)) t.push_back(r.time);
            if (r.valid != "yes") allYes = false;
        }
        const RunResult &last = rs.back();
        for (auto &r : rs)
            if (r.cover != last.cover)
                cerr << "  WARNING: " << a.key << " gave different cover sizes across repeats on " << path << "\n";
        s.cover = last.cover;
        s.valid = allYes ? "yes" : last.valid;
        s.status = last.status;
        s.repeats = (int)rs.size();
        s.nodes = last.nodes;
        s.matchLb = last.match;
        if (!t.empty()) {
            s.tMedian = median(t);
            s.tMin = *min_element(t.begin(), t.end());
            s.tMean = accumulate(t.begin(), t.end(), 0.0) / t.size();
            if (t.size() > 1) {
                double ss = 0;
                for (double x : t) ss += (x - s.tMean) * (x - s.tMean);
                s.tStd = sqrt(ss / (t.size() - 1));
            }
        }
        out[a.key] = s;
    }
    return out;
}

void addBaseline(map<string, AlgoStats> &rows, const string &knownOpt) {
    AlgoStats &ex = rows["exact"];
    long lb = rows["approx"].matchLb;
    double base = NAN;
    string type;
    if (ex.status == "optimal" && ex.cover >= 0) {
        base = (double)ex.cover; type = "exact";
        if (!knownOpt.empty() && atol(knownOpt.c_str()) != ex.cover)
            cerr << "  WARNING: exact optimum " << ex.cover << " != known_opt " << knownOpt << "\n";
    } else if (!knownOpt.empty()) {
        base = (double)atol(knownOpt.c_str()); type = "known_opt";
    } else {
        long best = -1;
        for (auto &kv : rows)
            if (kv.second.cover >= 0 && kv.second.valid == "yes" && (best < 0 || kv.second.cover < best))
                best = kv.second.cover;
        if (best >= 0) base = (double)best;
        type = "best_known";
    }
    for (auto &kv : rows) {
        AlgoStats &s = kv.second;
        s.baseline = base;
        s.baselineType = type;
        if (!isnan(base) && base > 0 && s.cover >= 0) s.ratio = s.cover / base;
        if (lb > 0 && s.cover >= 0) s.ratioLb = (double)s.cover / lb;
        s.matchLb = lb;
    }
}

// ---------------------------------------------------------------- environment, build
void writeEnvironment(const Options &o, const string &path) {
    ofstream f(path);
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", localtime(&now));
    f << "date: " << buf << "\n";
    f << "os: " << OS_NAME << "\n";
    f << "logical cpus: " << thread::hardware_concurrency() << "\n";
#ifdef __VERSION__
    f << "compiler used to build benchmark: " << __VERSION__ << "\n";
#endif
    f << "algorithm build flags: -O2 -std=c++17 (via --build; otherwise as built by hand)\n";
    f << "suite: " << (o.graphFile.empty() ? o.suite : "single graph") << "\n";
    f << "repeats: " << o.repeats << "\n";
    f << "exact timeout (s): " << o.exactTimeout << "\n";
    f << "slow threshold (s): " << o.slowThreshold << "\n";
    f << "CPU model and RAM are not detected automatically: add them by hand for the report.\n";
}

void buildAll(const Options &o) {
    fs::create_directories(o.binDir);
    string cxx = !o.cxx.empty() ? o.cxx : (getenv("CXX") ? getenv("CXX") : "g++");
    vector<pair<string, string>> targets = {
        {"src/vc_exact.cpp", "vc_exact"}, {"src/vc_approx.cpp", "vc_approx"},
        {"src/vc_greedy.cpp", "vc_greedy"}, {"tools/generate_graphs.cpp", "generate_graphs"},
        {"tools/plot_results.cpp", "plot_results"}};
    for (auto &t : targets) {
        string cmd = cxx + " -O2 -std=c++17 " + quote(nativePath(t.first)) + " -o " +
                     quote(exePath(o.binDir, t.second));
        cout << "Building: " << cmd << "\n";
        if (runSystem(cmd) != 0) { cerr << "Build failed for " << t.first << "\n"; exit(1); }
    }
}

// ---------------------------------------------------------------- main
int main(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        auto val = [&]() -> string {
            if (i + 1 >= argc) { cerr << "missing value for " << a << "\n"; exit(1); }
            return argv[++i];
        };
        if (a == "--build") o.build = true;
        else if (a == "--no-generate") o.noGenerate = true;
        else if (a == "--no-plot") o.noPlot = true;
        else if (a == "--suite") o.suite = val();
        else if (a == "--repeats") o.repeats = max(1, stoi(val()));
        else if (a == "--exact-timeout") o.exactTimeout = stod(val());
        else if (a == "--slow-threshold") o.slowThreshold = stod(val());
        else if (a == "--seeds") o.seeds = stoi(val());
        else if (a == "--max-n") o.maxN = stoi(val());
        else if (a == "--families") o.families = val();
        else if (a == "--order-count") o.orderCount = stoi(val());
        else if (a == "--graph") o.graphFile = val();
        else if (a == "--manifest") o.manifest = val();
        else if (a == "--graph-dir") o.graphDir = val();
        else if (a == "--out-dir") o.outDir = val();
        else if (a == "--bin-dir") o.binDir = val();
        else if (a == "--cxx") o.cxx = val();
        else { cerr << "unknown option " << a << " (see the header comment of tools/benchmark.cpp)\n"; return 1; }
    }
    if (o.graphDir.empty()) o.graphDir = "graphs";
    if (o.manifest.empty()) o.manifest = nativePath(fs::path(o.graphDir) / "manifest.csv");

    if (o.build) buildAll(o);
    for (auto &a : ALGOS)
        if (!fs::exists(exePath(o.binDir, a.prog))) {
            cerr << "Missing " << exePath(o.binDir, a.prog) << ". Re-run with --build.\n";
            return 1;
        }

    // 1. generate
    if (o.graphFile.empty() && !o.noGenerate) {
        string cmd = quote(exePath(o.binDir, "generate_graphs")) + " --suite " + o.suite + " --out-dir " +
                     quote(nativePath(o.graphDir));
        if (o.seeds > 0) cmd += " --seeds " + to_string(o.seeds);
        if (o.maxN > 0) cmd += " --max-n " + to_string(o.maxN);
        if (!o.families.empty()) cmd += " --families " + o.families;
        if (o.orderCount >= 0) cmd += " --order-count " + to_string(o.orderCount);
        if (runSystem(cmd) != 0) { cerr << "Graph generation failed\n"; return 1; }
    }

    // 2. read the manifest
    struct Entry { string file, label, family, n, m, orderSeed, knownOpt; };
    vector<Entry> entries;
    string graphDir = o.graphDir;
    if (!o.graphFile.empty()) {
        fs::path p(o.graphFile);
        graphDir = p.has_parent_path() ? p.parent_path().string() : ".";
        entries.push_back({p.filename().string(), "single", "", "", "", "", ""});
    } else {
        ifstream f(o.manifest);
        if (!f) { cerr << "Cannot open manifest " << o.manifest << "\n"; return 1; }
        string line;
        getline(f, line);
        vector<string> head = splitCsv(line);
        auto col = [&](const string &name) {
            for (size_t i = 0; i < head.size(); i++) if (head[i] == name) return (int)i;
            return -1;
        };
        int cFile = col("file"), cLabel = col("label"), cFam = col("family"), cN = col("n"), cM = col("m"),
            cOrd = col("order_seed"), cOpt = col("known_opt");
        while (getline(f, line)) {
            if (line.empty()) continue;
            auto v = splitCsv(line);
            auto get = [&](int c) { return (c >= 0 && c < (int)v.size()) ? v[c] : string(); };
            entries.push_back({get(cFile), get(cLabel), get(cFam), get(cN), get(cM), get(cOrd), get(cOpt)});
        }
    }

    // 3. benchmark
    fs::create_directories(o.outDir);
    writeEnvironment(o, nativePath(fs::path(o.outDir) / "environment.txt"));
    string resultsPath = nativePath(fs::path(o.outDir) / "results.csv");
    ofstream csv(resultsPath);
    csv << "graph,label,family,n,m,order_seed,algorithm,cover_size,valid,status,time_median,time_min,"
           "time_mean,time_std,repeats,nodes,matching_lb,known_opt,baseline,baseline_type,ratio,ratio_vs_lb\n";
    for (size_t i = 0; i < entries.size(); i++) {
        const Entry &e = entries[i];
        cout << "[" << i + 1 << "/" << entries.size() << "] " << e.file << endl;
        string path = (fs::path(graphDir) / e.file).string();
        auto rows = benchmarkGraph(o, path);
        addBaseline(rows, e.knownOpt);
        for (auto &a : ALGOS) {
            const AlgoStats &s = rows[a.key];
            auto d = [](double v) { return isnan(v) ? string() : num(v); };
            auto l = [](long v) { return v < 0 ? string() : to_string(v); };
            csv << e.file << "," << e.label << "," << e.family << "," << e.n << "," << e.m << "," << e.orderSeed
                << "," << a.key << "," << l(s.cover) << "," << s.valid << "," << s.status << "," << d(s.tMedian)
                << "," << d(s.tMin) << "," << d(s.tMean) << "," << d(s.tStd) << "," << s.repeats << ","
                << l(s.nodes) << "," << l(s.matchLb) << "," << e.knownOpt << "," << d(s.baseline) << ","
                << s.baselineType << "," << d(s.ratio) << "," << d(s.ratioLb) << "\n";
            if (!o.graphFile.empty())
                cout << "  " << a.key << ": cover=" << s.cover << " valid=" << s.valid << " status=" << s.status
                     << " median_time=" << d(s.tMedian) << "\n";
        }
        csv.flush();
    }
    csv.close();
    cout << "Wrote " << resultsPath << "\n";

    // 4. plot
    if (!o.noPlot) {
        string cmd = quote(exePath(o.binDir, "plot_results")) + " --results " + quote(resultsPath) +
                     " --out-dir " + quote(nativePath(fs::path(o.outDir) / "plots"));
        if (runSystem(cmd) != 0) cerr << "Plotting failed (results.csv is still written)\n";
    }
    return 0;
}
