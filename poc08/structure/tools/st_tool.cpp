// hd8struct: runs C and L on toks.jsonl and writes their IR as JSON lines; trains and scores the head model.
//
//   hd8struct --data DIR [--no-heads] < toks.jsonl > ir.jsonl
//   hd8struct --data DIR --train-heads GOLD.jsonl [--iters N] --out head.tsv < toks.jsonl
//   hd8struct --data DIR --eval-heads GOLD.jsonl < toks.jsonl
//
// DIR holds aux.tsv, ud.tsv, forms.tsv and optionally head.tsv. GOLD.jsonl has {"doc","line","heads":[...]}
// per line, with a gold head bunsetsu index per bunsetsu (-1 root, -2 unknown).
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../structure.h"
#include "io.h"
#include "json_min.h"

using hd8json::Quote;
using hd8json::Value;

using hd8io::ReadLine;
using hd8io::WriteIr;

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Example {
    hd8::Line line;
    std::vector<hd8::TokInfo> info;
    std::vector<int> gold;
};

// Head decoding with explicit weights (the training loop's copy of AttachHeads' search).
std::vector<int> Decode(const std::unordered_map<std::string, double>& w, const std::vector<hd8::HeadFacts>& facts) {
    const int n = static_cast<int>(facts.size());
    std::vector<int> head(n, -1);
    std::vector<std::string> feats;
    for (int i = n - 1; i >= 0; --i) {
        if (facts[i].sentEnd == i) continue;
        double best = -1e300;
        int bestJ = i + 1;
        for (int j = i + 1; j != -1 && j <= facts[i].sentEnd; j = head[j]) {
            hd8::HeadFeatures(facts, i, j, feats);
            double s = 0;
            for (const auto& f : feats) {
                auto it = w.find(f);
                if (it != w.end()) s += it->second;
            }
            if (s > best) {
                best = s;
                bestJ = j;
            }
            if (head[j] == -1) break;
        }
        head[i] = bestJ;
    }
    return head;
}

std::map<std::pair<std::string, int>, std::vector<int>> ReadGold(const std::string& path) {
    std::map<std::pair<std::string, int>, std::vector<int>> gold;
    std::ifstream in(path);
    std::string s;
    while (std::getline(in, s)) {
        Value v;
        if (!hd8json::Parser(s).Parse(v)) continue;
        std::vector<int> h;
        if (const Value* a = v.Get("heads"))
            for (const auto& x : a->arr) h.push_back(static_cast<int>(x.num));
        gold[{std::string(v.S("doc")), static_cast<int>(v.N("line"))}] = h;
    }
    return gold;
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataDir, trainGold, evalGold, outPath;
    int iters = 10;
    bool noHeads = false, annotate = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--data") dataDir = next();
        else if (a == "--train-heads") trainGold = next();
        else if (a == "--eval-heads") evalGold = next();
        else if (a == "--out") outPath = next();
        else if (a == "--iters") iters = std::atoi(next().c_str());
        else if (a == "--no-heads") noHeads = true;
        else if (a == "--annotate") annotate = true;
        else {
            std::fprintf(stderr, "unknown flag %s\n", a.c_str());
            return 2;
        }
    }
    if (dataDir.empty()) {
        std::fprintf(stderr, "--data DIR is required\n");
        return 2;
    }
    hd8::Strings st;
    hd8::StructureData data;
    std::string err;
    std::string headTsv = noHeads || !trainGold.empty() ? std::string() : ReadFile(dataDir + "/head.tsv");
    if (!hd8::LoadStructureData(ReadFile(dataDir + "/aux.tsv"), ReadFile(dataDir + "/ud.tsv"),
                                ReadFile(dataDir + "/forms.tsv"), headTsv, st, data, err)) {
        std::fprintf(stderr, "data: %s\n", err.c_str());
        return 1;
    }
    std::ios::sync_with_stdio(false);
    std::string s;
    if (!trainGold.empty() || !evalGold.empty()) {
        auto gold = ReadGold(trainGold.empty() ? evalGold : trainGold);
        std::vector<Example> exs;
        while (std::getline(std::cin, s)) {
            Value v;
            if (!hd8json::Parser(s).Parse(v)) continue;
            Example ex;
            if (!ReadLine(v, st, ex.line)) continue;
            auto it = gold.find({ex.line.doc, static_cast<int>(ex.line.id)});
            if (it == gold.end()) continue;
            ex.info = hd8::Annotate(data, st, ex.line.path.toks);
            hd8::Chunk(data, st, ex.info, ex.line.path);
            ex.gold = it->second;
            if (ex.gold.size() != ex.line.path.buns.size()) continue;
            exs.push_back(std::move(ex));
        }
        if (!evalGold.empty()) {
            long ok = 0, tot = 0;
            for (auto& ex : exs) {
                hd8::AttachHeads(data, st, ex.info, ex.line.path, ex.line.decisions, st);
                const auto& b = ex.line.path.buns;
                for (size_t i = 0; i + 1 < b.size(); ++i) {
                    if (ex.gold[i] < -1) continue;
                    ++tot;
                    ok += b[i].head == ex.gold[i];
                }
            }
            std::printf("lines %zu heads %ld correct %ld acc %.4f\n", exs.size(), tot, ok, tot ? double(ok) / tot : 0.0);
            return 0;
        }
        // Averaged perceptron.
        std::unordered_map<std::string, double> w, acc;
        std::unordered_map<std::string, long> last;
        long t = 1;
        std::vector<std::vector<hd8::HeadFacts>> facts(exs.size());
        for (size_t e = 0; e < exs.size(); ++e) facts[e] = hd8::ComputeHeadFacts(st, exs[e].info, exs[e].line.path);
        std::vector<std::string> feats;
        for (int it = 0; it < iters; ++it) {
            long ok = 0, tot = 0;
            for (size_t e = 0; e < exs.size(); ++e) {
                Example& ex = exs[e];
                std::vector<int> pred = Decode(w, facts[e]);
                const int n = static_cast<int>(pred.size());
                for (int i = 0; i + 1 < n; ++i) {
                    int g = ex.gold[i];
                    if (g < 0 || g <= i || g > facts[e][i].sentEnd) continue;
                    ++tot;
                    if (pred[i] == g) {
                        ++ok;
                        continue;
                    }
                    auto upd = [&](int j, double delta) {
                        hd8::HeadFeatures(facts[e], i, j, feats);
                        for (const auto& f : feats) {
                            // lazy averaging: acc holds the sum of weights over steps
                            auto& wl = last[f];
                            acc[f] += w[f] * static_cast<double>(t - wl);
                            wl = t;
                            w[f] += delta;
                        }
                    };
                    upd(g, 1.0);
                    upd(pred[i], -1.0);
                }
                ++t;
            }
            std::fprintf(stderr, "iter %d train acc %.4f (%ld)\n", it + 1, tot ? double(ok) / tot : 0.0, tot);
        }
        std::ofstream out(outPath);
        out << "#! learned head\n# Averaged perceptron over named head features (HeadFeatures).\nfeature\tw\n";
        std::vector<std::pair<std::string, double>> rows;
        for (auto& [f, v] : w) {
            double total = acc[f] + v * static_cast<double>(t - last[f]);
            double avg = total / static_cast<double>(t);
            if (avg > 1e-4 || avg < -1e-4) rows.emplace_back(f, avg);
        }
        std::sort(rows.begin(), rows.end());
        for (auto& [f, v] : rows) out << f << '\t' << v << '\n';
        std::fprintf(stderr, "wrote %zu weights\n", rows.size());
        return 0;
    }
    hd8::Line line;
    while (std::getline(std::cin, s)) {
        Value v;
        if (!hd8json::Parser(s).Parse(v) || !ReadLine(v, st, line)) continue;
        auto info = hd8::Annotate(data, st, line.path.toks);
        if (annotate) {
            for (size_t k = 0; k < info.size(); ++k)
                std::cout << st.Get(line.path.toks[k].surf) << '\t' << st.Get(line.path.toks[k].lemma) << '\t'
                          << (info[k].row ? st.Get(info[k].row->id) : "-") << '\t'
                          << (info[k].frow ? st.Get(info[k].frow->id) : "-") << '\n';
            continue;
        }
        hd8::Chunk(data, st, info, line.path);
        hd8::AttachHeads(data, st, info, line.path, line.decisions, st);
        hd8::BuildClauses(data, st, info, line.path, line.decisions);
        WriteIr(line, st, std::cout);
    }
    return 0;
}
