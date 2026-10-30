// hd8gen: command-line driver of the 0.8 Korean side (generator, register, rewrite) for tests and experiments.
//
//   hd8gen [--data DIR] conj LEMMA FORM...          one predicate: 가다 -었- -어요 → 갔어요
//   hd8gen [--data DIR] gold FILE.tsv               gold paradigm set: id lemma pos cls forms expected
//   hd8gen [--data DIR] gen FILE.jsonl              IR lines (kir2 + reg + lev [+ out]) → text
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "hd8gen/generator.h"
#include "../src/ir_json.h"
#include "../src/json.h"
#include "../src/tables.h"

#ifndef HD8GEN_DATA_DIR
#define HD8GEN_DATA_DIR "data"
#endif

using namespace hd8gen;

namespace {

int Usage() {
    std::fprintf(stderr, "usage: hd8gen [--data DIR] conj LEMMA FORM... | gold FILE.tsv | gen FILE.jsonl\n");
    return 2;
}

std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> out;
    for (const auto& w : Split(s, ' '))
        if (!w.empty()) out.push_back(w);
    return out;
}

int Gold(const Tables& t, const std::string& path, bool verbose) {
    TsvTable tsv;
    std::string err;
    if (!ReadTsv(path, tsv, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const int cId = tsv.Col("id"), cLemma = tsv.Col("lemma"), cPos = tsv.Col("pos"), cCls = tsv.Col("cls"),
              cForms = tsv.Col("forms"), cExp = tsv.Col("expected"), cHost = tsv.Col("host");
    if (cId < 0 || cLemma < 0 || cForms < 0 || cExp < 0) {
        std::fprintf(stderr, "gold: need id lemma forms expected\n");
        return 1;
    }
    size_t n = 0, exact = 0, variant = 0;
    for (const auto& r : tsv.rows) {
        ++n;
        const std::string pos = cPos >= 0 && r[cPos] != "-" ? r[cPos] : "";
        const std::string cls = cCls >= 0 && r[cCls] != "-" ? r[cCls] : "";
        const std::string host = cHost >= 0 && r[cHost] != "-" ? r[cHost] : "";
        const std::string got = ConjugateForms(t, r[cLemma], pos, cls, Words(r[cForms]), host);
        const std::vector<std::string> alts = Split(r[cExp], '|');
        if (got == alts[0]) {
            ++exact;
            if (verbose) std::printf("OK\t%s\t%s\n", r[cId].c_str(), got.c_str());
            continue;
        }
        bool alt = false;
        for (size_t k = 1; k < alts.size(); ++k)
            if (got == alts[k]) alt = true;
        if (alt) {
            ++variant;
            std::printf("VARIANT\t%s\t%s\t%s\t%s\t%s\n", r[cId].c_str(), r[cLemma].c_str(), r[cForms].c_str(), got.c_str(),
                        r[cExp].c_str());
        } else {
            std::printf("DIFF\t%s\t%s\t%s\tgot=%s\texpected=%s\n", r[cId].c_str(), r[cLemma].c_str(), r[cForms].c_str(),
                        got.c_str(), r[cExp].c_str());
        }
    }
    std::printf("TOTAL\t%zu\texact\t%zu\tvariant\t%zu\tagree\t%.2f%%\n", n, exact, variant,
                n ? 100.0 * static_cast<double>(exact + variant) / static_cast<double>(n) : 0.0);
    return exact + variant == n ? 0 : 1;
}

int Gen(const Tables& t, const std::string& path, const GenOptions& opt) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 1;
    }
    std::string line;
    size_t n = 0, same = 0, withExp = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        json::Value v;
        std::string err;
        if (!json::Parse(line, v, err)) {
            std::fprintf(stderr, "json: %s\n", err.c_str());
            return 1;
        }
        LineIR ir;
        if (!LineFromJson(v, ir, err)) {
            std::fprintf(stderr, "ir: %s\n", err.c_str());
            return 1;
        }
        const GenResult g = Generate(t, ir.kir2, ir.reg, ir.lev, opt);
        ++n;
        const std::string id = v.Str("case", std::to_string(n));
        if (const json::Value* e = v.Get("out")) {
            ++withExp;
            if (e->str == g.out) ++same;
            std::printf("%s\t%s\t%s\t%s\n", e->str == g.out ? "OK" : "DIFF", id.c_str(), g.out.c_str(), e->str.c_str());
        } else {
            std::printf("%s\t%s\n", id.c_str(), g.out.c_str());
        }
        for (const auto& note : g.notes) std::fprintf(stderr, "%s: %s\n", id.c_str(), note.c_str());
    }
    if (withExp) std::fprintf(stderr, "cases %zu, same %zu\n", withExp, same);
    return same == withExp ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string data = HD8GEN_DATA_DIR;
    std::vector<std::string> args(argv + 1, argv + argc);
    GenOptions opt;
    bool verbose = false;
    while (!args.empty() && args[0].rfind("--", 0) == 0) {
        if (args[0] == "--data" && args.size() > 1) {
            data = args[1];
            args.erase(args.begin(), args.begin() + 2);
        } else if (args[0] == "--attach-aux") {
            opt.attachAux = true;
            args.erase(args.begin());
        } else if (args[0] == "--uncontracted") {
            opt.uncontracted = true;
            args.erase(args.begin());
        } else if (args[0] == "-v") {
            verbose = true;
            args.erase(args.begin());
        } else {
            return Usage();
        }
    }
    if (args.empty()) return Usage();
    std::string err;
    auto tables = LoadTables(data + "/ko", err);
    if (!tables) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (args[0] == "conj" && args.size() >= 3) {
        std::vector<std::string> forms(args.begin() + 2, args.end());
        std::printf("%s\n", ConjugateForms(*tables, args[1], "", "", forms, "", opt).c_str());
        return 0;
    }
    if (args[0] == "gold" && args.size() == 2) return Gold(*tables, args[1], verbose);
    if (args[0] == "gen" && args.size() == 2) return Gen(*tables, args[1], opt);
    return Usage();
}
