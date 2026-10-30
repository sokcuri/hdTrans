// hd8retro: the retrofit experiment. Reads 0.7.x --why output converted to pre-IR lines (convert.py), reads it
// into kir nodes (analysis by synthesis with the generator), then runs R (speech level), W (rewrite rules) and
// G (generation), and writes one JSON line per input line.
//
//   hd8retro [--data DIR] [--level auto|strict|keep] [--rules all|none|ID,ID] [--skip ID,ID] PRE.jsonl > OUT.jsonl
//
// --level keep: the level each 0.7.x ending had; strict: 0.7.x meaning (plain → 해라, です/ます → 합쇼);
// auto: the register model. Lines whose words could not all be read keep the 0.7.x output (fallback).
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "../src/hangul.h"
#include "../src/ir_json.h"
#include "../src/json.h"
#include "../src/kir_text.h"
#include "../src/tables.h"
#include "hd8gen/analyze.h"
#include "hd8gen/generator.h"
#include "hd8gen/register.h"
#include "hd8gen/rewrite.h"

#ifndef HD8GEN_DATA_DIR
#define HD8GEN_DATA_DIR "data"
#endif

using namespace hd8gen;

namespace {

struct RowForm {
    std::string form, mode;
};

struct Retro {
    const Tables& t;
    std::map<std::pair<char, char>, RowForm> rows;
    std::vector<std::string> deps;      // dependent nouns, longest first
    std::vector<std::string> auxLemmas; // predicates that follow inside a 0.7.x ending row
    std::map<std::string, std::string> particleKase;  // surface → kase (or lex)
    std::unordered_map<std::string, std::unordered_multimap<std::string, Bundle>> paradigms;
    size_t words = 0, fallbacks = 0;
    bool debug = false;

    const std::unordered_multimap<std::string, Bundle>& Para(const std::string& lemma, const std::string& pos,
                                                             const std::string& cls, const std::string& host) {
        const std::string key = lemma + "|" + pos + "|" + cls + "|" + host;
        auto it = paradigms.find(key);
        if (it != paradigms.end()) return it->second;
        auto& m = paradigms[key];
        for (auto& [s, b] : Paradigm(t, lemma, pos, cls, host)) m.emplace(s, b);
        return m;
    }
    std::vector<Bundle> Find(const std::string& lemma, const std::string& pos, const std::string& cls, const std::string& surface,
                             const AnalyzeHints& h) {
        const auto& m = Para(lemma, pos, cls, h.host);
        std::vector<Bundle> out;
        auto r = m.equal_range(surface);
        for (auto it = r.first; it != r.second; ++it) out.push_back(it->second);
        RankBundles(out, h);
        return out;
    }
};

std::u32string U(const std::string& s) { return hangul::ToU32(s); }

bool StartsWithStr(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }

// The notation of a 0.7.x row + the first chunk of its rest.
std::string RowNotation(const Retro& r, char marker, char code, const std::string& first, const std::string& pos, bool bare) {
    const char m = marker == '&' ? '&' : '$';
    if (m == '$' && code == '4' && (pos == "V" || pos == "X")) return "는" + first;
    auto it = r.rows.find({m, code});
    if (it == r.rows.end()) return "";
    const RowForm& f = it->second;
    if (f.mode == "v4") {
        if (pos != "V" || bare) return first.empty() ? "~" : first;
        const std::u32string u = U(first);
        if (!u.empty() && (u[0] == U'다' || u[0] == U'대' || u[0] == U'단' || u[0] == U'답'))
            return "는" + first + "/ㄴ" + first;
        return "는" + first;
    }
    if (f.mode == "sup") return "습" + first + "/ㅂ" + first;
    if (pos == "X" && f.form == "(으)ㄴ") return "던" + first;  // 0.7.x: 있던, 없던
    if (f.mode == "bare-a" && first.empty()) return "(으)~";
    if (code == '1' && m == '&' && pos == "V" && !bare) {
        const std::u32string u = U(first);
        if (!u.empty() && u[0] == U'다' && (u.size() == 1 || u[1] == U'고' || u[1] == U'는'))
            return "는" + first + "/ㄴ" + first;
    }
    if (f.form == "~") return first.empty() ? "~" : first;
    return f.form + first;
}

bool IsHangulWord(const std::string& s) {
    const std::u32string u = U(s);
    if (u.empty()) return false;
    for (char32_t c : u)
        if (!hangul::IsSyllable(c)) return false;
    return true;
}

Node MakeNode(Kind k, const std::string& lemma) {
    Node n;
    n.kind = k;
    n.lemma = lemma;
    return n;
}

// A word of a 0.7.x ending row after the first: a dependent noun (+ particle or copula) or a predicate.
bool ReadRestWord(Retro& r, const std::string& w, Want wantLast, Mood mood, std::vector<Node>& out) {
    for (const auto& d : r.deps) {
        if (!StartsWithStr(w, d)) continue;
        const std::string rem = w.substr(d.size());
        Node n = MakeNode(Kind::N, d);
        n.dep = true;
        if (rem.empty()) {
            out.push_back(n);
            return true;
        }
        auto pk = r.particleKase.find(rem);
        if (pk != r.particleKase.end()) {
            out.push_back(n);
            Node p = MakeNode(Kind::P, "");
            if (pk->second.rfind("lex:", 0) == 0) {
                p.kase = "lex";
                p.lemma = pk->second.substr(4);
            } else {
                p.kase = pk->second;
            }
            out.push_back(p);
            return true;
        }
        AnalyzeHints h;
        h.want = wantLast;
        h.mood = mood;
        h.host = d;
        auto bs = r.Find("이다", "C", "COP", rem, h);
        if (!bs.empty()) {
            out.push_back(n);
            Node v = bs[0].node;
            v.ctype = bs[0].node.ctype;
            out.push_back(v);
            out.back().decision = static_cast<uint32_t>(bs[0].level);  // carried for --level keep
            return true;
        }
    }
    const std::u32string u = U(w);
    if (u.empty() || !hangul::IsSyllable(u[0])) return false;
    for (const auto& lemma : r.auxLemmas) {
        const std::u32string lu = U(lemma);
        if (lu.empty() || hangul::Initial(lu[0]) != hangul::Initial(u[0])) continue;
        AnalyzeHints h;
        h.want = wantLast;
        h.mood = mood;
        auto bs = r.Find(lemma, "", "", w, h);
        if (bs.empty()) continue;
        Node v = bs[0].node;
        v.aux = true;
        v.decision = static_cast<uint32_t>(bs[0].level);
        out.push_back(v);
        return true;
    }
    return false;
}

// Reads one 0.7.x predicate item into nodes. Returns false when a word could not be read.
bool ReadPredicate(Retro& r, const json::Value& it, Want want, Mood mood, const std::string& host, std::vector<Node>& out) {
    std::string lemma = it.Str("lemma");
    std::string pos = it.Str("pos"), cls = it.Str("cls");
    const PredicateInfo info = LookupPredicate(r.t, lemma);
    const bool known = r.t.lemmaPos.count(lemma) || r.t.stems.count(lemma);
    if (known || pos.empty()) pos = info.pos;
    if (cls.empty() || known) cls = info.cls;
    const std::string marker = it.Str("marker", "&");
    const std::string row = it.Str("row", "1");
    const std::string rest = it.Str("rest");
    const bool bare = it.Bool("bare");
    std::vector<std::string> chunks = Split(rest, ' ');
    const std::string first = chunks[0];
    const std::string note = RowNotation(r, marker[0], row.empty() ? '1' : row[0], first, pos, bare);
    if (note.empty()) return false;
    const bool cop = cls == "COP";
    std::string w1;
    if (cop) w1 = ConjugateForms(r.t, "이다", "C", "COP", {note}, host.empty() ? "3" : host);
    else w1 = ConjugateForms(r.t, lemma, pos, cls, {note});
    if (cop) {
        const std::string h = host.empty() ? "3" : host;
        if (w1.rfind(h, 0) == 0) w1 = w1.substr(h.size());
    }
    const bool more = chunks.size() > 1;
    AnalyzeHints h;
    h.mood = mood;
    h.want = want;
    if (more) {
        bool depNext = false;
        for (const auto& d : r.deps)
            if (StartsWithStr(chunks[1], d)) depNext = true;
        h.want = depNext ? Want::adn : Want::conn;
    }
    if (cop) h.host = host.empty() ? "3" : host;
    ++r.words;
    std::vector<Bundle> bs = cop ? r.Find("이다", "C", "COP", w1, h) : r.Find(lemma, pos, cls, w1, h);
    if (!bs.empty()) {
        Node v = bs[0].node;
        if (cop) {
            v.lemma = "이다";
            v.kcls = "COP";
            v.vpos = "C";
        }
        v.decision = static_cast<uint32_t>(bs[0].level);
        out.push_back(v);
    } else {
        // an auxiliary written onto the -어 form (가버렸다, 만들어졌다)
        const std::string a = ConjugateForms(r.t, lemma, pos, cls, {"어"});
        bool split = false;
        if (!a.empty() && w1.rfind(a, 0) == 0 && w1.size() > a.size()) {
            std::vector<Node> tail;
            if (ReadRestWord(r, w1.substr(a.size()), more ? Want::conn : want, mood, tail) && !tail.empty() && tail[0].kind == Kind::V) {
                Node v = MakeNode(Kind::V, lemma);
                v.vpos = pos;
                v.kcls = cls;
                v.ctype = CType::conn;
                v.eomi = "-어";
                out.push_back(v);
                tail[0].glue = tail[0].lemma == "지다";
                for (auto& n : tail) out.push_back(n);
                split = true;
            }
        }
        if (!split) {
            ++r.fallbacks;
            if (r.debug) std::fprintf(stderr, "UNREAD\tfirst\t%s\t%s\t%s\t%s\t%s\t%s\n", lemma.c_str(), pos.c_str(), cls.c_str(), row.c_str(), rest.c_str(), w1.c_str());
            Node raw = MakeNode(Kind::Raw, w1);
            raw.glue = cop;
            out.push_back(raw);
            for (size_t k = 1; k < chunks.size(); ++k)
                if (!chunks[k].empty()) out.push_back(MakeNode(Kind::Raw, chunks[k]));
            return false;
        }
    }
    for (size_t k = 1; k < chunks.size(); ++k) {
        if (chunks[k].empty()) continue;
        ++r.words;
        const Want wk = k + 1 < chunks.size() ? Want::conn : want;
        std::vector<Node> ns;
        if (!ReadRestWord(r, chunks[k], wk, mood, ns)) {
            ++r.fallbacks;
            if (r.debug) std::fprintf(stderr, "UNREAD\trest\t%s\t%s\t%s\t%s\t%s\t%s\n", lemma.c_str(), pos.c_str(), cls.c_str(), row.c_str(), rest.c_str(), chunks[k].c_str());
            out.push_back(MakeNode(Kind::Raw, chunks[k]));
            continue;
        }
        for (auto& n : ns) out.push_back(n);
    }
    // fold negation auxiliaries into the predicate: -지 않다, -지 못하다, -지 말다
    for (size_t k = 0; k + 1 < out.size(); ++k) {
        Node& v = out[k];
        Node& a = out[k + 1];
        if (v.kind != Kind::V || a.kind != Kind::V || v.ctype != CType::conn || v.eomi != "-지") continue;
        Neg neg = Neg::none;
        if (a.lemma == "않다") neg = Neg::jiAnh;
        else if (a.lemma == "못하다") neg = Neg::jiMot;
        else if (a.lemma == "말다") neg = Neg::jiMa;
        if (neg == Neg::none) continue;
        const bool vaux = v.aux;
        const std::string lem = v.lemma, vp = v.vpos, kc = v.kcls;
        v = a;
        v.lemma = lem;
        v.vpos = vp;
        v.kcls = kc;
        v.aux = vaux;
        v.neg = neg;
        if (neg == Neg::jiMa) {
            v.mood = Mood::imp;
            v.ctype = CType::final_;
            v.eomi.clear();
        }
        out.erase(out.begin() + static_cast<long>(k) + 1);
    }
    return true;
}

Punct PunctOf(const std::string& p) {
    const char* names[] = {"none", "comma", "pause", "end", "q", "excl", "qopen", "qclose", "ellipsis", "dash", "mark"};
    for (int i = 0; i < 11; ++i)
        if (p == names[i]) return static_cast<Punct>(i);
    return Punct::mark;
}

}  // namespace

int main(int argc, char** argv) {
    std::string data = HD8GEN_DATA_DIR, levelMode = "auto", rulesArg = "all", skipArg;
    std::string input;
    bool debugFlag = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--level" && i + 1 < argc) levelMode = argv[++i];
        else if (a == "--rules" && i + 1 < argc) rulesArg = argv[++i];
        else if (a == "--skip" && i + 1 < argc) skipArg = argv[++i];
        else if (a == "--debug") debugFlag = true;
        else input = a;
    }
    std::string err;
    auto tables = LoadTables(data + "/ko", err);
    auto rtab = tables ? LoadRegisterTables(data + "/register", err) : nullptr;
    auto rules = rtab ? LoadRules(data + "/ko/rewrite", err) : nullptr;
    if (!rules) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    Retro r{*tables, {}, {}, {}, {}, {}, 0, 0, debugFlag};
    {
        TsvTable rt;
        if (!ReadTsv(data + "/retro/rows.tsv", rt, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        for (const auto& row : rt.rows) r.rows[{row[0][0], row[1][0]}] = {row[2], row[3]};
        TsvTable lt;
        if (!ReadTsv(data + "/retro/words.tsv", lt, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        for (const auto& row : lt.rows) {
            if (row[0] == "dep") r.deps.push_back(row[1]);
            else if (row[0] == "pred") r.auxLemmas.push_back(row[1]);
            else if (row[0] == "particle") r.particleKase[row[1]] = row[2];
        }
        std::stable_sort(r.deps.begin(), r.deps.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    }
    std::vector<std::string> only, skip;
    if (rulesArg == "none") only = {"-none-"};
    else if (rulesArg != "all") only = Split(rulesArg, ',');
    if (!skipArg.empty()) skip = Split(skipArg, ',');
    std::ifstream in(input);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", input.c_str());
        return 1;
    }
    std::vector<json::Value> rows;
    std::string line;
    while (std::getline(in, line)) {
        json::Value v;
        if (!json::Parse(line, v, err)) {
            std::fprintf(stderr, "json: %s\n", err.c_str());
            return 1;
        }
        rows.push_back(std::move(v));
    }
    // R over the whole input: one document per work (or the whole file), scenes from "scene"
    std::vector<RegLine> rl;
    std::map<std::string, uint32_t> sceneIds;
    for (const auto& v : rows) {
        RegLine l;
        l.ja = v.Str("ja");
        l.speaker = v.Str("speaker");
        l.dialogue = v.Bool("dialogue");
        const std::string sc = v.Str("work") + "|" + v.Str("scene");
        auto it = sceneIds.find(sc);
        if (it == sceneIds.end()) it = sceneIds.emplace(sc, static_cast<uint32_t>(sceneIds.size() + 1)).first;
        l.scene = it->second;
        rl.push_back(l);
    }
    const std::vector<RegDecision> decisions = DecideLevels(*rtab, rl, {});
    size_t linesOk = 0;
    for (size_t li = 0; li < rows.size(); ++li) {
        const json::Value& v = rows[li];
        const json::Value* items = v.Get("items");
        std::vector<Node> nodes;
        bool lineOk = items && !items->arr.empty();
        if (items) {
            for (size_t k = 0; k < items->arr.size(); ++k) {
                const json::Value& it = items->arr[k];
                const std::string kind = it.Str("k");
                // hints from the next item
                Want want = Want::final_;
                Mood mood = Mood::decl;
                for (size_t j = k + 1; j < items->arr.size(); ++j) {
                    const std::string nk = items->arr[j].Str("k");
                    if (nk == "Punct") {
                        const std::string p = items->arr[j].Str("p");
                        if (p == "q") mood = Mood::q;
                        if (p == "comma") want = Want::conn;
                        if (p == "qclose" || p == "ellipsis" || p == "mark" || p == "dash") continue;
                        break;
                    }
                    if (nk == "N" || nk == "Raw") want = Want::adn;
                    else if (nk == "V") want = Want::conn;
                    else if (nk == "P") want = Want::nom;
                    break;
                }
                if (kind == "N" || kind == "Raw") {
                    Node n = MakeNode(kind == "N" && IsHangulWord(it.Str("t")) ? Kind::N : Kind::Raw, it.Str("t"));
                    n.glue = it.Bool("glue");
                    nodes.push_back(n);
                } else if (kind == "P") {
                    const std::string t = it.Str("t");
                    Node p = MakeNode(Kind::P, "");
                    auto pk = r.particleKase.find(t);
                    if (pk != r.particleKase.end() && pk->second.rfind("lex:", 0) != 0) p.kase = pk->second;
                    else {
                        p.kase = "lex";
                        p.lemma = pk != r.particleKase.end() ? pk->second.substr(4) : t;
                    }
                    nodes.push_back(p);
                } else if (kind == "Punct") {
                    Node p = MakeNode(Kind::Punct, "");
                    p.punct = PunctOf(it.Str("p"));
                    p.jaMark = it.Str("m");
                    nodes.push_back(p);
                } else if (kind == "V") {
                    std::string host;
                    if (!nodes.empty() && (nodes.back().kind == Kind::N || nodes.back().kind == Kind::Raw)) host = nodes.back().lemma;
                    std::vector<Node> vs;
                    if (!ReadPredicate(r, it, want, mood, host, vs)) lineOk = false;
                    if (!vs.empty()) vs[0].glue = vs[0].glue || it.Bool("glue");
                    for (auto& n : vs) nodes.push_back(n);
                }
            }
        }
        uint32_t id = 0;
        for (auto& n : nodes) n.id = n.origin = ++id;
        // R
        RegDecision d = decisions[li];
        std::vector<Reg> reg;
        UttLevel u;
        u.utt = 0;
        u.narration = d.narration || !rl[li].dialogue;
        if (levelMode == "auto") {
            const bool first = d.firstPerson;
            u.level = u.narration ? Level::haera : d.level;
            u.persona = d.persona;
            reg = AssignRegister(nodes, d, first, tables.get());
            if (u.narration)
                for (auto& g : reg) g.level = Level::haera;
        } else {
            // keep: the level 0.7.x's own ending had; strict: same (0.7.x strict output is what it wrote)
            for (const auto& n : nodes)
                if (n.kind == Kind::V) {
                    Reg g;
                    g.node = n.id;
                    g.level = static_cast<Level>(n.decision);
                    reg.push_back(g);
                }
            u.level = Level::haera;
        }
        for (auto& n : nodes) n.decision = 0;
        const RewriteResult rw = Rewrite(*rules, *tables, nodes, reg, {u}, only, skip);
        const GenResult g = Generate(*tables, rw.kir2, rw.reg, {u});
        if (lineOk) ++linesOk;
        json::Value o;
        o.t = json::Value::T::Obj;
        o.Set("line", json::N(v.Num("line")));
        o.Set("ja", json::S(v.Str("ja")));
        o.Set("out075", json::S(v.Str("out075")));
        o.Set("out", json::S(lineOk ? g.out : v.Str("out075")));
        o.Set("gen", json::S(g.out));
        o.Set("ok", json::B(lineOk));
        o.Set("level", json::S(Name(u.level)));
        o.Set("key", json::S(d.key));
        json::Value fired;
        fired.t = json::Value::T::Arr;
        for (const auto& f : rw.fired) fired.arr.push_back(json::S(f.rule));
        o.Set("fired", fired);
        o.Set("kir", json::S(KirText(rw.kir2)));
        std::printf("%s\n", json::Write(o).c_str());
    }
    std::fprintf(stderr, "lines %zu, read fully %zu (%.1f%%); predicate words %zu, unread %zu (%.1f%%)\n", rows.size(), linesOk,
                 rows.empty() ? 0.0 : 100.0 * static_cast<double>(linesOk) / static_cast<double>(rows.size()), r.words, r.fallbacks,
                 r.words ? 100.0 * static_cast<double>(r.fallbacks) / static_cast<double>(r.words) : 0.0);
    return 0;
}
