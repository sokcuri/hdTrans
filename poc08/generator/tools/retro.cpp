// hd8retro: the retrofit experiment on pre-IR lines (tools/retro_convert.py output); one JSON line per input
// line on stdout.
//
//   hd8retro [--data DIR] [--level auto|strict|keep] [--rules all|none|ID,ID] [--skip ID,ID] PRE.jsonl > OUT.jsonl
//
// --level keep: the level each 0.7.x ending had; strict: 0.7.x meaning (plain → 해라, です/ます → 합쇼);
// auto: the register model. Lines whose words could not all be read keep the 0.7.x output (fallback).
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "../src/json.h"
#include "hd8gen/retro.h"

#ifndef HD8GEN_DATA_DIR
#define HD8GEN_DATA_DIR "data"
#endif

using namespace hd8gen;

namespace {
std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t c = s.find(',', a);
        out.push_back(s.substr(a, c == std::string::npos ? std::string::npos : c - a));
        if (c == std::string::npos) return out;
        a = c + 1;
    }
}
}  // namespace

int main(int argc, char** argv) {
    std::string data = HD8GEN_DATA_DIR, input;
    RetroOptions opt;
    std::string rulesArg = "all", skipArg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--level" && i + 1 < argc) opt.level = argv[++i];
        else if (a == "--rules" && i + 1 < argc) rulesArg = argv[++i];
        else if (a == "--skip" && i + 1 < argc) skipArg = argv[++i];
        else if (a == "--debug") opt.debug = true;
        else if (a == "--rebuild") opt.align = false;
        else input = a;
    }
    if (rulesArg == "none") opt.only = {"-none-"};
    else if (rulesArg != "all") opt.only = SplitComma(rulesArg);
    if (!skipArg.empty()) opt.skip = SplitComma(skipArg);
    std::string err;
    const auto sys = LoadRetro(data, err);
    if (!sys) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::ifstream in(input);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", input.c_str());
        return 1;
    }
    std::vector<std::string> pre;
    for (std::string line; std::getline(in, line);) pre.push_back(line);
    std::vector<RetroLine> out;
    RetroStats st;
    if (!RunRetro(*sys, pre, opt, out, &st, err)) {
        std::fprintf(stderr, "json: %s\n", err.c_str());
        return 1;
    }
    for (size_t i = 0; i < out.size(); ++i) {
        json::Value v;
        json::Parse(pre[i], v, err);
        const RetroLine& r = out[i];
        json::Value o;
        o.t = json::Value::T::Obj;
        o.Set("line", json::N(v.Num("line")));
        o.Set("ja", json::S(v.Str("ja")));
        o.Set("out075", json::S(v.Str("out075")));
        o.Set("out", json::S(r.out));
        o.Set("gen", json::S(r.gen));
        o.Set("ok", json::B(r.ok));
        o.Set("level", json::S(r.level));
        o.Set("key", json::S(r.key));
        json::Value fired;
        fired.t = json::Value::T::Arr;
        for (const auto& f : r.fired) fired.arr.push_back(json::S(f));
        o.Set("fired", fired);
        o.Set("kir", json::S(r.kir));
        std::printf("%s\n", json::Write(o).c_str());
    }
    if (opt.align)
        std::fprintf(stderr, "aligned items %zu, items kept as 0.7.x text %zu (%.1f%%)\n", st.aligned, st.skipped,
                     100.0 * static_cast<double>(st.skipped) / static_cast<double>(st.aligned + st.skipped + 1));
    std::fprintf(stderr, "lines %zu, read fully %zu (%.1f%%); predicate words %zu, unread %zu (%.1f%%), nouns kept literally %zu, literal predicates read %zu\n",
                 st.lines, st.linesOk, st.lines ? 100.0 * static_cast<double>(st.linesOk) / static_cast<double>(st.lines) : 0.0,
                 st.words, st.fallbacks, st.words ? 100.0 * static_cast<double>(st.fallbacks) / static_cast<double>(st.words) : 0.0,
                 st.literal, st.recovered);
    return 0;
}
