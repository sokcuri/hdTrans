// hd8gen: command-line driver of the 0.8 Korean side (generator, register, rewrite) for tests and experiments.
//
//   hd8gen [--data DIR] conj LEMMA FORM...          one predicate: 가다 -었- -어요 → 갔어요
//   hd8gen [--data DIR] batch < LINES              lemma pos cls host forms per line (TAB) → one result per line
//   hd8gen [--data DIR] gold FILE.tsv               gold paradigm set: id lemma pos cls forms expected
//   hd8gen [--data DIR] gen FILE.jsonl              IR lines (kir2 + reg + lev [+ out]) → text
#include <cstdio>
#include <cstdlib>
#include <map>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "hd8gen/generator.h"
#include "hd8gen/register.h"
#include "hd8gen/rewrite.h"
#include "../src/kir_text.h"
#include "../src/ir_json.h"
#include "../src/json.h"
#include "../src/tables.h"

#ifndef HD8GEN_DATA_DIR
#define HD8GEN_DATA_DIR "data"
#endif

using namespace hd8gen;

namespace {

int Usage() {
    std::fprintf(stderr, "usage: hd8gen [--data DIR] conj LEMMA FORM... | batch | gold FILE.tsv | gen FILE.jsonl\n");
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

// One conjugation per stdin line: lemma TAB pos TAB cls TAB host TAB forms ("-" = none); one result per line.
int Batch(const Tables& t, const GenOptions& opt) {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string> f = Split(line, '\t');
        if (f.size() != 5) {
            std::fprintf(stderr, "batch: need 5 fields: %s\n", line.c_str());
            return 1;
        }
        for (auto& s : f)
            if (s == "-") s.clear();
        std::printf("%s\n", ConjugateForms(t, f[0], f[1], f[2], Words(f[4]), f[3], opt).c_str());
        std::fflush(stdout);  // a caller may keep one process and read each answer before the next question
    }
    return 0;
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

// Speech-level agreement on game lines: JSON lines {doc, work, ja, speaker, dialogue, ref_level[, scored]}.
// Lines of one work form one document; doc changes are scene boundaries.
int LevelEval(const RegisterTables& rt, const std::string& path, const RegOptions& ro, bool scoredOnly, bool dump) {
    std::ifstream in(path);
    if (!in) return 1;
    std::vector<json::Value> rows;
    std::string line;
    while (std::getline(in, line)) {
        json::Value v;
        std::string err;
        if (!json::Parse(line, v, err)) {
            std::fprintf(stderr, "json: %s\n", err.c_str());
            return 1;
        }
        rows.push_back(std::move(v));
    }
    std::map<std::pair<std::string, std::string>, size_t> cm;
    size_t n = 0, ok = 0, i = 0;
    while (i < rows.size()) {
        size_t j = i;
        const std::string work = rows[i].Str("work");
        while (j < rows.size() && rows[j].Str("work") == work) ++j;
        std::vector<RegLine> lines;
        uint32_t scene = 0;
        std::string lastDoc;
        for (size_t k = i; k < j; ++k) {
            RegLine l;
            l.ja = rows[k].Str("ja");
            l.speaker = rows[k].Str("speaker");
            l.dialogue = rows[k].Bool("dialogue");
            if (rows[k].Str("doc") != lastDoc) {
                ++scene;
                lastDoc = rows[k].Str("doc");
            }
            l.scene = scene;
            lines.push_back(l);
        }
        const std::vector<RegDecision> ds = DecideLevels(rt, lines, ro);
        for (size_t k = i; k < j; ++k) {
            const json::Value& r = rows[k];
            const std::string ref = r.Str("ref_level");
            if (!r.Bool("dialogue") || ref.empty() || ref == "none") continue;
            if (scoredOnly && r.Get("scored") && !r.Bool("scored")) continue;
            const std::string pred = Name(ds[k - i].level);
            ++n;
            if (pred == ref) ++ok;
            ++cm[{pred, ref}];
            if (dump) std::printf("%s\t%s\t%s\t%s\t%s\n", pred.c_str(), ref.c_str(), ds[k - i].key.c_str(),
                                  r.Str("speaker").c_str(), r.Str("ja").c_str());
        }
        i = j;
    }
    std::fprintf(stderr, "lines %zu agree %zu (%.2f%%)\n", n, ok, n ? 100.0 * static_cast<double>(ok) / static_cast<double>(n) : 0.0);
    const char* L[] = {"haera", "hae", "haeyo", "hapsyo"};
    std::fprintf(stderr, "pred\\ref haera hae haeyo hapsyo\n");
    for (const char* a : L) {
        std::fprintf(stderr, "%-7s", a);
        for (const char* b : L) std::fprintf(stderr, " %6zu", cm[{a, b}]);
        std::fprintf(stderr, "\n");
    }
    return 0;
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
    if (args[0] == "batch" && args.size() == 1) return Batch(*tables, opt);
    if (args[0] == "gold" && args.size() == 2) return Gold(*tables, args[1], verbose);
    if (args[0] == "gen" && args.size() == 2) return Gen(*tables, args[1], opt);
    if (args[0] == "lint" || args[0] == "rw") {
        auto rs = LoadRules(data + "/ko/rewrite", err);
        if (!rs) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        if (args[0] == "lint") {
            const LintReport rep = Lint(*rs, *tables);
            size_t pass = 0;
            for (const auto& c : rep.cases) pass += c.pass;
            for (const auto& p : rep.problems) std::printf("PROBLEM\t%s\n", p.c_str());
            std::printf("rules %zu, cases %zu, cases passing %zu, problems %zu\n", RuleIds(*rs).size(), rep.cases.size(), pass,
                        rep.problems.size());
            return rep.problems.empty() ? 0 : 1;
        }
        if (args.size() < 3) return Usage();
        std::vector<Node> nodes;
        if (!ParseKirText(args[2], nodes, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        UttLevel u;
        u.narration = args[1] == "narr";
        if (!u.narration) ParseLevel(args[1], u.level);
        else u.level = Level::haera;
        const RewriteResult rr = Rewrite(*rs, *tables, nodes, {}, {u});
        for (const auto& f : rr.fired) std::printf("fired %s at %u\n", f.rule.c_str(), f.node);
        std::printf("%s\n%s\n", KirText(rr.kir2).c_str(), Generate(*tables, rr.kir2, rr.reg, {u}, opt).out.c_str());
        return 0;
    }
    if (args[0] == "level" && args.size() >= 2) {
        auto rt = LoadRegisterTables(data + "/register", err);
        if (!rt) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        RegOptions ro;
        bool scored = false, dump = false;
        for (size_t k = 2; k < args.size(); ++k) {
            if (args[k] == "--scored") scored = true;
            else if (args[k] == "--dump") dump = true;
            else if (args[k] == "--no-profile") ro.profiles = false;
            else if (args[k].rfind("--decay=", 0) == 0) ro.decay = std::atof(args[k].c_str() + 8);
            else if (args[k].rfind("--threshold=", 0) == 0) ro.threshold = std::atof(args[k].c_str() + 12);
        }
        return LevelEval(*rt, args[1], ro, scored, dump);
    }
    return Usage();
}
