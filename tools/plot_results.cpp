// Shared group tool: plots and summary table from results/results.csv.
// Course: Advanced Algorithms, Assignment 2
//
// Build:  g++ -O2 -std=c++17 tools/plot_results.cpp -o bin/plot_results
//         (bin/benchmark --build does this for you)
// Usage:  plot_results [--results results/results.csv] [--out-dir results/plots]
//
// No libraries are needed: the figures are written as SVG files, which open in any web
// browser and can be inserted into Word, PowerPoint and Google Docs (or screenshotted).
//
// Outputs (in --out-dir)
//   runtime_by_family.svg        median runtime vs n, one panel per graph family (log y).
//                                A black X marks an exact-algorithm timeout.
//   ratio_by_family.svg          cover size / baseline vs n, one panel per family.
//   ratio_summary.svg            mean ratio per family and algorithm (bar chart).
//   edge_order_sensitivity.svg   matching cover size across edge orderings of ONE graph.
//   summary.csv                  mean/max ratio, mean time and timeouts per (family, algorithm).
//
// Lines are the mean over the instances at each n; shaded bands are min..max.
// The baseline of each ratio is exact optimum > known_opt (bipartite) > best known cover
// (see benchmark.cpp). Where the baseline is "best_known" the ratios UNDERSTATE the true ratio.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- data
struct Row {
    string graph, label, algo, status, baselineType;
    double n = NAN, cover = NAN, time = NAN, ratio = NAN, ratioLb = NAN, baseline = NAN;
};

static double toD(const string &s) { return s.empty() ? NAN : atof(s.c_str()); }

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

vector<Row> loadRows(const string &path) {
    ifstream f(path);
    if (!f) { cerr << "Cannot open " << path << "\n"; exit(1); }
    string line;
    getline(f, line);
    vector<string> head = splitCsv(line);
    map<string, int> col;
    for (size_t i = 0; i < head.size(); i++) col[head[i]] = (int)i;
    auto need = [&](const string &k) {
        if (!col.count(k)) { cerr << "results file is missing column " << k << "\n"; exit(1); }
        return col[k];
    };
    int cGraph = need("graph"), cLabel = need("label"), cN = need("n"), cAlgo = need("algorithm"),
        cCover = need("cover_size"), cStatus = need("status"), cTime = need("time_median"),
        cRatio = need("ratio"), cRatioLb = need("ratio_vs_lb"), cBase = need("baseline"),
        cBType = need("baseline_type");
    vector<Row> rows;
    while (getline(f, line)) {
        if (line.empty()) continue;
        auto v = splitCsv(line);
        if (v.size() < head.size()) continue;
        Row r;
        r.graph = v[cGraph]; r.label = v[cLabel]; r.algo = v[cAlgo]; r.status = v[cStatus];
        r.baselineType = v[cBType];
        r.n = toD(v[cN]); r.cover = toD(v[cCover]); r.time = toD(v[cTime]); r.ratio = toD(v[cRatio]);
        r.ratioLb = toD(v[cRatioLb]); r.baseline = toD(v[cBase]);
        rows.push_back(r);
    }
    return rows;
}

// ---------------------------------------------------------------- SVG helpers
static string esc(const string &s) {
    string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else o += c;
    }
    return o;
}

static string f2(double v) {
    char b[48];
    snprintf(b, sizeof b, "%.2f", v);
    return b;
}

static string g(double v) {
    char b[48];
    snprintf(b, sizeof b, "%g", v);
    return b;
}

struct Svg {
    ostringstream o;
    double W, H;
    Svg(double w, double h) : W(w), H(h) {}
    void line(double x1, double y1, double x2, double y2, const string &col, double w = 1, const string &dash = "") {
        o << "<line x1='" << f2(x1) << "' y1='" << f2(y1) << "' x2='" << f2(x2) << "' y2='" << f2(y2)
          << "' stroke='" << col << "' stroke-width='" << w << "'"
          << (dash.empty() ? "" : " stroke-dasharray='" + dash + "'") << "/>\n";
    }
    void rect(double x, double y, double w, double h, const string &fill, const string &stroke = "none", double op = 1) {
        o << "<rect x='" << f2(x) << "' y='" << f2(y) << "' width='" << f2(w) << "' height='" << f2(h)
          << "' fill='" << fill << "' stroke='" << stroke << "' fill-opacity='" << op << "'/>\n";
    }
    void circle(double x, double y, double r, const string &fill) {
        o << "<circle cx='" << f2(x) << "' cy='" << f2(y) << "' r='" << r << "' fill='" << fill << "'/>\n";
    }
    void text(double x, double y, const string &s, double size = 11, const string &anchor = "start",
              const string &col = "#222", bool bold = false, double rot = 0) {
        o << "<text x='" << f2(x) << "' y='" << f2(y) << "' font-size='" << size << "' text-anchor='" << anchor
          << "' fill='" << col << "' font-family='Arial, Helvetica, sans-serif'"
          << (bold ? " font-weight='bold'" : "");
        if (rot != 0) o << " transform='rotate(" << rot << " " << f2(x) << " " << f2(y) << ")'";
        o << ">" << esc(s) << "</text>\n";
    }
    void polygon(const vector<pair<double, double>> &pts, const string &fill, double op, const string &stroke = "none", double sw = 1) {
        o << "<polygon points='";
        for (auto &p : pts) o << f2(p.first) << "," << f2(p.second) << " ";
        o << "' fill='" << fill << "' fill-opacity='" << op << "' stroke='" << stroke << "' stroke-width='" << sw << "'/>\n";
    }
    void polyline(const vector<pair<double, double>> &pts, const string &col, double w) {
        o << "<polyline fill='none' stroke='" << col << "' stroke-width='" << w << "' points='";
        for (auto &p : pts) o << f2(p.first) << "," << f2(p.second) << " ";
        o << "'/>\n";
    }
    void save(const string &path) {
        ofstream f(path);
        f << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H << "' viewBox='0 0 " << W
          << " " << H << "'>\n<rect width='100%' height='100%' fill='white'/>\n" << o.str() << "</svg>\n";
    }
};

// ---------------------------------------------------------------- styles
static const vector<string> ALGO_ORDER = {"exact", "approx", "greedy"};
static string algoName(const string &a) {
    if (a == "exact") return "Exact (branch & bound)";
    if (a == "approx") return "2-approx (maximal matching)";
    return "Greedy (highest degree)";
}
static string algoColor(const string &a) {
    if (a == "exact") return "#1f77b4";
    if (a == "approx") return "#d62728";
    return "#2ca02c";
}
static int algoMarker(const string &a) { return a == "exact" ? 0 : (a == "approx" ? 1 : 2); }

void marker(Svg &s, int kind, double x, double y, const string &col, double r = 3.4) {
    if (kind == 0) s.circle(x, y, r, col);
    else if (kind == 1) s.rect(x - r, y - r, 2 * r, 2 * r, col);
    else s.polygon({{x, y - r - 0.8}, {x - r - 0.5, y + r}, {x + r + 0.5, y + r}}, col, 1);
}

void crossMark(Svg &s, double x, double y, double r = 5) {
    s.line(x - r, y - r, x + r, y + r, "black", 2);
    s.line(x - r, y + r, x + r, y - r, "black", 2);
}

// ---------------------------------------------------------------- ticks
double niceNum(double x) {
    double e = floor(log10(x)), f = x / pow(10, e);
    double nf = f <= 1 ? 1 : (f <= 2 ? 2 : (f <= 5 ? 5 : 10));
    return nf * pow(10, e);
}

vector<double> linTicks(double lo, double hi, int target = 5) {
    vector<double> t;
    if (hi <= lo) { t.push_back(lo); return t; }
    double step = niceNum((hi - lo) / target);
    for (double v = ceil(lo / step - 1e-9) * step; v <= hi + step * 1e-9; v += step) t.push_back(fabs(v) < step * 1e-9 ? 0.0 : v);
    return t;
}

// ---------------------------------------------------------------- panels
struct Pt { double x, mean, lo, hi; bool timeout; };
struct Series { string algo; vector<Pt> pts; };
struct Panel {
    string title, xlabel, ylabel;
    bool logy = false;
    vector<Series> series;
    vector<double> hlines;     // dashed reference lines (e.g. ratio 1 and 2)
};

void drawPanel(Svg &s, double cx, double cy, double cw, double ch, const Panel &P) {
    double x0 = cx + 66, y0 = cy + 34, w = cw - 66 - 18, h = ch - 34 - 50;
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    for (auto &se : P.series)
        for (auto &p : se.pts) {
            xmin = min(xmin, p.x); xmax = max(xmax, p.x);
            ymin = min({ymin, p.lo, p.mean}); ymax = max({ymax, p.hi, p.mean});
        }
    if (xmin > xmax) { s.text(cx + cw / 2, cy + ch / 2, P.title + ": no data", 12, "middle"); return; }
    for (double hl : P.hlines) { ymin = min(ymin, hl); ymax = max(ymax, hl); }
    if (xmax == xmin) { xmin -= 1; xmax += 1; }
    vector<double> yt;
    double ylo, yhi;
    if (P.logy) {
        ylo = pow(10, floor(log10(ymin))); yhi = pow(10, ceil(log10(ymax)));
        if (yhi <= ylo) yhi = ylo * 10;
        for (double v = ylo; v <= yhi * 1.0001; v *= 10) yt.push_back(v);
    } else {
        double pad = (ymax - ymin) * 0.08;
        if (pad == 0) pad = 0.5;
        ylo = max(0.0, ymin - pad); yhi = ymax + pad;
        yt = linTicks(ylo, yhi);
    }
    auto px = [&](double x) { return x0 + (x - xmin) / (xmax - xmin) * w; };
    auto py = [&](double y) {
        double f = P.logy ? (log10(y) - log10(ylo)) / (log10(yhi) - log10(ylo)) : (y - ylo) / (yhi - ylo);
        return y0 + h - f * h;
    };
    // grid + ticks
    for (double v : yt) {
        double y = py(v);
        s.line(x0, y, x0 + w, y, "#e3e3e3", 1);
        string lab = P.logy ? "1e" + to_string((int)lround(log10(v))) : g(v);
        s.text(x0 - 6, y + 4, lab, 10, "end", "#444");
    }
    for (double v : linTicks(xmin, xmax)) {
        double x = px(v);
        s.line(x, y0, x, y0 + h, "#efefef", 1);
        s.text(x, y0 + h + 15, g(v), 10, "middle", "#444");
    }
    s.rect(x0, y0, w, h, "none", "#888");
    for (double hl : P.hlines)
        if (hl >= ylo && hl <= yhi) s.line(x0, py(hl), x0 + w, py(hl), "#777", 1, "5,4");
    // series
    for (auto &se : P.series) {
        string col = algoColor(se.algo);
        bool band = false;
        for (auto &p : se.pts) if (p.lo != p.hi) band = true;
        if (band) {
            vector<pair<double, double>> poly;
            for (auto &p : se.pts) poly.push_back({px(p.x), py(p.hi)});
            for (int i = (int)se.pts.size() - 1; i >= 0; i--) poly.push_back({px(se.pts[i].x), py(se.pts[i].lo)});
            s.polygon(poly, col, 0.15);
        }
        vector<pair<double, double>> line;
        for (auto &p : se.pts) line.push_back({px(p.x), py(p.mean)});
        s.polyline(line, col, 1.7);
        for (auto &p : se.pts) marker(s, algoMarker(se.algo), px(p.x), py(p.mean), col);
        for (auto &p : se.pts) if (p.timeout) crossMark(s, px(p.x), py(p.mean));
    }
    s.text(cx + cw / 2, cy + 20, P.title, 13, "middle", "#111", true);
    s.text(x0 + w / 2, cy + ch - 8, P.xlabel, 11, "middle");
    s.text(cx + 14, y0 + h / 2, P.ylabel, 11, "middle", "#222", false, -90);
}

void legend(Svg &s, double y, bool withTimeout, const vector<string> &algos) {
    double total = 0;
    vector<double> widths;
    for (auto &a : algos) { double w = 40 + algoName(a).size() * 6.2; widths.push_back(w); total += w; }
    if (withTimeout) total += 40 + 15 * 6.2;
    double x = (s.W - total) / 2;
    for (size_t i = 0; i < algos.size(); i++) {
        s.line(x, y, x + 22, y, algoColor(algos[i]), 2);
        marker(s, algoMarker(algos[i]), x + 11, y, algoColor(algos[i]));
        s.text(x + 28, y + 4, algoName(algos[i]), 11);
        x += widths[i];
    }
    if (withTimeout) {
        crossMark(s, x + 11, y);
        s.text(x + 28, y + 4, "exact timed out", 11);
    }
}

void multiPanel(const string &path, const string &title, const vector<Panel> &panels, bool withTimeout,
                const vector<string> &legendAlgos) {
    const double CW = 440, CH = 320;
    int cols = (int)min<size_t>(3, panels.size());
    int rows = ((int)panels.size() + cols - 1) / cols;
    Svg s(cols * CW, rows * CH + 90);
    s.text(s.W / 2, 28, title, 14, "middle", "#111", true);
    for (size_t i = 0; i < panels.size(); i++)
        drawPanel(s, (i % cols) * CW, 40 + (i / cols) * CH, CW, CH, panels[i]);
    legend(s, rows * CH + 62, withTimeout, legendAlgos);
    s.save(path);
}

// ---------------------------------------------------------------- figures
typedef map<string, map<string, map<int, vector<const Row *>>>> Grouped;   // label -> algo -> n -> rows

template <class Get>
vector<Pt> collect(const map<int, vector<const Row *>> &byN, Get get, bool markTimeouts) {
    vector<Pt> pts;
    for (auto &kv : byN) {
        vector<double> v;
        bool to = false;
        for (const Row *r : kv.second) {
            double x = get(*r);
            if (!isnan(x)) v.push_back(x);
            if (r->status == "timed_out") to = true;
        }
        if (v.empty()) continue;
        double sum = 0;
        for (double x : v) sum += x;
        pts.push_back({(double)kv.first, sum / v.size(), *min_element(v.begin(), v.end()),
                       *max_element(v.begin(), v.end()), markTimeouts && to});
    }
    return pts;
}

void barChart(const string &path, const vector<string> &labels, const Grouped &G) {
    const double W = max(720.0, 120.0 * labels.size() + 140), H = 420;
    Svg s(W, H);
    double x0 = 70, y0 = 50, w = W - x0 - 30, h = H - y0 - 120;
    vector<double> means[2];
    double ymax = 2.2;
    const vector<string> algos = {"approx", "greedy"};
    for (int a = 0; a < 2; a++)
        for (auto &lab : labels) {
            double sum = 0; int cnt = 0;
            auto it = G.find(lab);
            if (it != G.end() && it->second.count(algos[a]))
                for (auto &kv : it->second.at(algos[a]))
                    for (const Row *r : kv.second) if (!isnan(r->ratio)) { sum += r->ratio; cnt++; }
            means[a].push_back(cnt ? sum / cnt : 0);
            ymax = max(ymax, (cnt ? sum / cnt : 0) * 1.1);
        }
    auto py = [&](double v) { return y0 + h - v / ymax * h; };
    for (double v : linTicks(0, ymax)) {
        s.line(x0, py(v), x0 + w, py(v), "#e3e3e3");
        s.text(x0 - 6, py(v) + 4, g(v), 10, "end", "#444");
    }
    s.line(x0, py(1), x0 + w, py(1), "#777", 1, "5,4");
    s.line(x0, py(2), x0 + w, py(2), "#777", 1, "5,4");
    double slot = w / labels.size(), bw = slot * 0.32;
    for (size_t i = 0; i < labels.size(); i++) {
        double cx = x0 + slot * (i + 0.5);
        for (int a = 0; a < 2; a++) {
            double bx = cx + (a - 1) * bw;
            s.rect(bx, py(means[a][i]), bw - 2, y0 + h - py(means[a][i]), algoColor(algos[a]));
            s.text(bx + bw / 2 - 1, py(means[a][i]) - 4, f2(means[a][i]), 9, "middle", "#333");
        }
        s.text(cx, y0 + h + 14, labels[i], 10, "end", "#222", false, -30);
    }
    s.rect(x0, y0, w, h, "none", "#888");
    s.text(W / 2, 28, "Mean solution ratio (cover size / baseline) by graph family", 14, "middle", "#111", true);
    s.text(20, y0 + h / 2, "cover size / baseline", 11, "middle", "#222", false, -90);
    legend(s, H - 22, false, algos);
    s.save(path);
}

bool histogram(const string &path, const vector<Row> &rows) {
    vector<double> approx;
    double exactV = NAN, greedyV = NAN, base = NAN;
    string btype;
    for (auto &r : rows) {
        if (r.label != "edge_order" || isnan(r.cover)) continue;
        if (r.algo == "approx") approx.push_back(r.cover);
        if (r.algo == "exact" && isnan(exactV)) exactV = r.cover;
        if (r.algo == "greedy" && isnan(greedyV)) greedyV = r.cover;
        if (isnan(base) && !isnan(r.baseline)) { base = r.baseline; btype = r.baselineType; }
    }
    if (approx.empty()) return false;
    int lo = (int)*min_element(approx.begin(), approx.end()), hi = (int)*max_element(approx.begin(), approx.end());
    double xlo = lo, xhi = hi;
    for (double v : {exactV, greedyV, base}) if (!isnan(v)) { xlo = min(xlo, v); xhi = max(xhi, v); }
    xlo = floor(xlo) - 1; xhi = ceil(xhi) + 1;
    map<int, int> counts;
    int cmax = 1;
    for (double v : approx) cmax = max(cmax, ++counts[(int)v]);
    const double W = 760, H = 500, x0 = 70, y0 = 60, w = W - x0 - 30, h = H - y0 - 150;
    Svg s(W, H);
    auto px = [&](double x) { return x0 + (x - xlo) / (xhi - xlo) * w; };
    auto py = [&](double y) { return y0 + h - y / (cmax + 1) * h; };
    for (int v = 0; v <= cmax + 1; v += max(1, (cmax + 1) / 6)) {
        s.line(x0, py(v), x0 + w, py(v), "#e3e3e3");
        s.text(x0 - 6, py(v) + 4, to_string(v), 10, "end", "#444");
    }
    for (double v : linTicks(xlo, xhi, 8)) s.text(px(v), y0 + h + 15, g(v), 10, "middle", "#444");
    double bw = w / (xhi - xlo) * 0.8;
    for (auto &kv : counts)
        s.rect(px(kv.first) - bw / 2, py(kv.second), bw, y0 + h - py(kv.second), algoColor("approx"), "none", 0.85);
    s.rect(x0, y0, w, h, "none", "#888");
    // reference lines with a legend underneath
    struct Ref { string text, col; double v; };
    vector<Ref> refs;
    if (!isnan(greedyV)) refs.push_back({"greedy (order independent): " + g(greedyV), algoColor("greedy"), greedyV});
    if (!isnan(exactV)) refs.push_back({"exact (order independent): " + g(exactV), algoColor("exact"), exactV});
    if (!isnan(base)) refs.push_back({"baseline (" + btype + "): " + g(base), "#000", base});
    for (auto &r : refs) s.line(px(r.v), y0, px(r.v), y0 + h, r.col, 2, r.col == "#000" ? "2,3" : "");
    double ly = H - 96;
    s.rect(x0, ly - 8, 12, 12, algoColor("approx"), "none", 0.85);
    s.text(x0 + 18, ly + 2, "2-approx cover size over " + to_string(approx.size()) + " edge orderings of one graph", 11);
    for (size_t i = 0; i < refs.size(); i++) {
        double y = ly + 18 * (i + 1);
        s.line(x0, y - 3, x0 + 12, y - 3, refs[i].col, 2, refs[i].col == "#000" ? "2,3" : "");
        s.text(x0 + 18, y + 1, refs[i].text, 11);
    }
    s.text(W / 2, 30, "Same graph, different edge orders: the matching cover size varies", 14, "middle", "#111", true);
    s.text(x0 + w / 2, y0 + h + 34, "cover size", 11, "middle");
    s.text(22, y0 + h / 2, "number of edge orderings", 11, "middle", "#222", false, -90);
    s.save(path);
    return true;
}

void writeSummary(const string &path, const vector<Row> &rows, const vector<string> &labels) {
    map<pair<string, string>, vector<const Row *>> acc;
    for (auto &r : rows) acc[{r.label, r.algo}].push_back(&r);
    ofstream f(path);
    f << "label,algorithm,instances,mean_ratio,max_ratio,mean_ratio_vs_lb,mean_time_s,timeouts\n";
    vector<string> order = labels;
    if (!acc.empty()) order.push_back("edge_order");
    for (auto &lab : order)
        for (auto &algo : ALGO_ORDER) {
            auto it = acc.find({lab, algo});
            if (it == acc.end()) continue;
            double sr = 0, mr = 0, slb = 0, st = 0;
            int nr = 0, nlb = 0, nt = 0, to = 0;
            for (const Row *r : it->second) {
                if (!isnan(r->ratio)) { sr += r->ratio; mr = max(mr, r->ratio); nr++; }
                if (!isnan(r->ratioLb)) { slb += r->ratioLb; nlb++; }
                if (!isnan(r->time)) { st += r->time; nt++; }
                if (r->status == "timed_out") to++;
            }
            f << lab << "," << algo << "," << it->second.size() << ",";
            f << (nr ? g(sr / nr) : "") << "," << (nr ? g(mr) : "") << "," << (nlb ? g(slb / nlb) : "") << ","
              << (nt ? g(st / nt) : "") << "," << to << "\n";
        }
}

// ---------------------------------------------------------------- main
int main(int argc, char **argv) {
    string results = "results/results.csv", outDir = "results/plots";
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if ((a == "--results" || a == "--out-dir") && i + 1 < argc) (a == "--results" ? results : outDir) = argv[++i];
        else { cerr << "usage: plot_results [--results FILE] [--out-dir DIR]\n"; return 1; }
    }
    vector<Row> rows = loadRows(results);
    fs::create_directories(outDir);

    vector<string> labels;
    Grouped G;
    for (auto &r : rows) {
        if (r.label == "edge_order" || r.label == "single" || isnan(r.n)) continue;
        if (find(labels.begin(), labels.end(), r.label) == labels.end()) labels.push_back(r.label);
        G[r.label][r.algo][(int)r.n].push_back(&r);
    }

    if (!labels.empty()) {
        const double FLOOR = 1e-7;     // programs print 6 decimals, so 0.000000 is plotted here
        vector<Panel> rt, rr;
        for (auto &lab : labels) {
            Panel p;
            p.title = lab; p.xlabel = "n (vertices)"; p.ylabel = "median time (s)"; p.logy = true;
            Panel q = p;
            q.ylabel = "cover size / baseline"; q.logy = false; q.hlines = {1.0, 2.0};
            for (auto &algo : ALGO_ORDER) {
                if (!G[lab].count(algo)) continue;
                p.series.push_back({algo, collect(G[lab][algo], [&](const Row &r) { return isnan(r.time) ? NAN : max(r.time, FLOOR); }, algo == "exact")});
                if (algo != "exact")
                    q.series.push_back({algo, collect(G[lab][algo], [](const Row &r) { return r.ratio; }, false)});
            }
            rt.push_back(p);
            rr.push_back(q);
        }
        multiPanel((fs::path(outDir) / "runtime_by_family.svg").string(),
                   "Runtime vs graph size (algorithm only; times under 1 microsecond plotted at 1e-7 s)", rt, true,
                   ALGO_ORDER);
        map<string, int> bt;
        for (auto &r : rows) if (r.algo == "approx" && r.label != "single") bt[r.baselineType]++;
        string note;
        for (auto &kv : bt) note += (note.empty() ? "" : ", ") + kv.first + ": " + to_string(kv.second);
        multiPanel((fs::path(outDir) / "ratio_by_family.svg").string(),
                   "Solution quality (dashed lines at 1 and 2). Baselines used: " + note, rr, false,
                   {"approx", "greedy"});
        barChart((fs::path(outDir) / "ratio_summary.svg").string(), labels, G);
    }
    bool order = histogram((fs::path(outDir) / "edge_order_sensitivity.svg").string(), rows);
    writeSummary((fs::path(outDir) / "summary.csv").string(), rows, labels);
    cout << "Wrote figures and summary.csv to " << outDir
         << (order ? "" : " (no edge_order data, skipped that figure)") << "\n";
    return 0;
}
