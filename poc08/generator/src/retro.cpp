// The retrofit (hd8gen/retro.h): pre-IR lines of 0.7.x output → kir (analysis by synthesis) → R, W, G.
#include "hd8gen/retro.h"

#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "conj.h"
#include "hangul.h"
#include "ir_json.h"
#include "json.h"
#include "kir_text.h"
#include "tables.h"
#include "hd8gen/analyze.h"
#include "hd8gen/generator.h"
#include "hd8gen/register.h"
#include "hd8gen/rewrite.h"

namespace hd8gen {


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
    std::map<std::string, std::string> jima;          // 마, 마라 … → level
    std::map<std::string, std::string> fused;         // 건 걸 게 겁 → what fused onto 거
    std::unordered_map<std::string, std::unordered_multimap<std::string, Bundle>> paradigms;
    size_t words = 0, fallbacks = 0, literal = 0, recovered = 0;
    std::unordered_map<char32_t, std::vector<std::string>> lemmasByFirst;  // lemma index for literal predicates
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
    if (f.mode == "bare-a" && first.empty()) return "~";
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
bool ReadRestWord(Retro& r, const std::string& w, Want wantLast, Mood mood, std::vector<Node>& out, const std::string& mainPos = "") {
    {
        // 건 걸 게 겁…: 거 fused with a particle or the copula (written 것 in the IR; G fuses it again in speech)
        const std::u32string u = U(w);
        auto f = r.fused.find(hangul::ToU8(u.substr(0, 1)));
        if (!u.empty() && f != r.fused.end()) {
            Node n = MakeNode(Kind::N, r.t.G("nom.geot"));
            n.dep = true;
            if (f->second == "cop" || u.size() > 1) {
                // 건가, 건데, 겁니다: 거 + 이(ㄴ/ㄹ/ㅂ)… — read the copula with its jamo restored
                const std::u32string cop = std::u32string(1, hangul::Syl(hangul::kInitIeung, hangul::I, hangul::Final(u[0]))) + u.substr(1);
                AnalyzeHints h;
                h.want = wantLast;
                h.mood = mood;
                h.host = "3";
                auto bs = r.Find(r.t.G("lemma.cop"), "C", "COP", hangul::ToU8(cop), h);
                if (!bs.empty()) {
                    out.push_back(n);
                    Node v = bs[0].node;
                    v.decision = static_cast<uint32_t>(bs[0].level);
                    out.push_back(v);
                    return true;
                }
            } else {
                out.push_back(n);
                Node p = MakeNode(Kind::P, "");
                p.kase = f->second;
                out.push_back(p);
                return true;
            }
        }
    }
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
        auto bs = r.Find(r.t.G("lemma.cop"), "C", "COP", rem, h);
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
        const bool negAux = lemma == r.t.G("aux.neg") || lemma == r.t.G("aux.neg-mot");
        auto bs = r.Find(lemma, negAux && !mainPos.empty() ? (mainPos == "V" ? "V" : "A") : "", "", w, h);
        if (bs.empty() && negAux) bs = r.Find(lemma, "", "", w, h);
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
    std::string rest = it.Str("rest");
    for (size_t open = rest.find('('); open != std::string::npos; open = rest.find('(')) {  // 면(자) → 면
        const size_t close = rest.find(')', open);
        if (close == std::string::npos) break;
        rest.erase(open, close - open + 1);
    }
    const bool bare = it.Bool("bare");
    std::vector<std::string> chunks = Split(rest, ' ');
    const std::string first = chunks[0];
    const std::string note = RowNotation(r, marker[0], row.empty() ? '1' : row[0], first, pos, bare);
    if (note.empty()) return false;
    const bool cop = cls == "COP";
    std::string w1;
    if (cop) w1 = ConjugateForms(r.t, r.t.G("lemma.cop"), "C", "COP", {note}, host.empty() ? "3" : host);
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
    std::vector<Bundle> bs = cop ? r.Find(r.t.G("lemma.cop"), "C", "COP", w1, h) : r.Find(lemma, pos, cls, w1, h);
    if (bs.empty() && !cop && pos == "V") bs = r.Find(lemma, "A", cls, w1, h);
    if (bs.empty() && !cop && pos == "A") bs = r.Find(lemma, "V", cls, w1, h);
    std::string trailing;  // a particle written onto the ending: 될지도, 있는지도
    if (bs.empty() && !cop) {
        for (const auto& [surf, kase] : r.particleKase) {
            if (surf.empty() || surf[0] == '(' || w1.size() <= surf.size() || w1.compare(w1.size() - surf.size(), surf.size(), surf) != 0) continue;
            if (kase.rfind("lex:", 0) != 0) continue;
            bs = r.Find(lemma, pos, cls, w1.substr(0, w1.size() - surf.size()), h);
            if (!bs.empty()) {
                trailing = kase.substr(4);
                break;
            }
        }
    }
    if (!bs.empty()) {
        Node v = bs[0].node;
        if (cop) {
            v.lemma = r.t.G("lemma.cop");
            v.kcls = "COP";
            v.vpos = "C";
        }
        v.decision = static_cast<uint32_t>(bs[0].level);
        out.push_back(v);
        if (!trailing.empty()) {
            Node p = MakeNode(Kind::P, trailing);
            p.kase = "lex";
            out.push_back(p);
        }
    } else {
        // an auxiliary written onto the -어 form (가버렸다, 만들어졌다)
        const std::string a = ConjugateForms(r.t, lemma, pos, cls, {r.t.G("conn.aux")});
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
                tail[0].glue = tail[0].lemma == r.t.G("voice.aux");
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
        auto jm = r.jima.find(chunks[k]);
        if (jm != r.jima.end() && !out.empty() && out.back().kind == Kind::V && out.back().eomi == r.t.G("conn.neg")) {
            Node& v = out.back();  // -지 마: the prohibitive of the predicate
            v.neg = Neg::jiMa;
            v.mood = Mood::imp;
            v.ctype = CType::final_;
            v.eomi.clear();
            Level lv = Level::hae;
            ParseLevel(jm->second, lv);
            v.decision = static_cast<uint32_t>(lv);
            continue;
        }
        const Want wk = k + 1 < chunks.size() ? Want::conn : want;
        std::vector<Node> ns;
        if (!ReadRestWord(r, chunks[k], wk, mood, ns, pos)) {
            if (IsHangulWord(chunks[k]) && k + 1 < chunks.size()) {
                ++r.literal;
                out.push_back(MakeNode(Kind::N, chunks[k]));
                continue;
            }
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
        if (v.kind != Kind::V || a.kind != Kind::V || v.ctype != CType::conn || v.eomi != r.t.G("conn.neg")) continue;
        Neg neg = Neg::none;
        if (a.lemma == r.t.G("aux.neg")) neg = Neg::jiAnh;
        else if (a.lemma == r.t.G("aux.neg-mot")) neg = Neg::jiMot;
        else if (a.lemma == r.t.G("aux.prohib")) neg = Neg::jiMa;
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

// A word 0.7.x wrote literally (입니다 after a noun, 부탁드립니다): read it as the copula or as a known lemma when
// it ends like a sentence (다, 요, 까, 죠).
bool ReadLiteral(Retro& r, const std::string& text, const std::string& host, Want want, Mood mood, Node& out) {
    const std::u32string u = U(text);
    if (u.size() < 2) return false;
    const char32_t last = u.back();
    const std::u32string ends = U(r.t.G("lit.ends"));
    if (ends.find(last) == std::u32string::npos) return false;
    AnalyzeHints h;
    h.want = want;
    h.mood = mood;
    if (!host.empty()) {
        h.host = host;
        auto bs = r.Find(r.t.G("lemma.cop"), "C", "COP", text, h);
        if (!bs.empty()) {
            out = bs[0].node;
            out.glue = true;
            out.decision = static_cast<uint32_t>(bs[0].level);
            return true;
        }
        h.host.clear();
    }
    auto it = r.lemmasByFirst.find(u[0]);
    if (it == r.lemmasByFirst.end()) return false;
    for (const auto& lemma : it->second) {
        const std::u32string lu = U(lemma);
        const std::u32string stem = lu.substr(0, lu.size() - 1);
        if (stem.size() > u.size() || stem.size() + 4 < u.size()) continue;
        if (u.compare(0, stem.size() - 1, stem, 0, stem.size() - 1) != 0) continue;
        auto bs = r.Find(lemma, "", "", text, h);
        if (bs.empty()) continue;
        out = bs[0].node;
        out.decision = static_cast<uint32_t>(bs[0].level);
        return true;
    }
    return false;
}


// ---------------------------------------------------------------- alignment with 0.7.x's final output
// The line is rebuilt on 0.7.x's own output: every item that can be found there in order becomes IR
// (and is regenerated); every stretch between found items stays 0.7.x text (Raw nodes, marks as Punct).
// So nothing 0.7.x did after composition is lost where the reader cannot follow it.

bool IsSpace(char32_t c) { return c == U' ' || c == 0x3000 || c == U'\t'; }
bool IsMarkChar(char32_t c) {
    switch (c) {
        case U',': case U'.': case U'?': case U'!': case 0x2026: case 0x300C: case 0x300D: case 0x300E: case 0x300F:
        case U'(': case U')': case U'~': case 0x2015: case 0x2500: case 0x00B7: case 0xFF5E: case 0x2014:
            return true;
        default: return false;
    }
}
Punct PunctOfChar(char32_t c) {
    switch (c) {
        case U',': return Punct::comma;
        case U'.': return Punct::end;
        case U'?': return Punct::q;
        case U'!': return Punct::excl;
        case 0x2026: return Punct::ellipsis;
        case 0x300C: case 0x300E: case U'(': return Punct::qopen;
        case 0x300D: case 0x300F: case U')': return Punct::qclose;
        case 0x2015: case 0x2500: case 0x2014: return Punct::dash;
        case 0x00B7: return Punct::pause;
        default: return Punct::mark;
    }
}

// Appends the 0.7.x text of a gap as Raw words and Punct marks (the marks keep their own character).
void GapNodes(const std::u32string& g, bool glueFirst, std::vector<Node>& out) {
    size_t i = 0;
    bool spaceBefore = !glueFirst;
    while (i < g.size()) {
        if (IsSpace(g[i])) {
            spaceBefore = true;
            ++i;
            continue;
        }
        if (IsMarkChar(g[i])) {
            size_t j = i;
            while (j < g.size() && g[j] == g[i]) ++j;
            Node p = MakeNode(Kind::Punct, "");
            p.punct = PunctOfChar(g[i]);
            p.jaMark = hangul::ToU8(g.substr(i, j - i));  // the 0.7.x mark itself (never empty: not a comma the source lacks)
            p.glue = !spaceBefore;
            out.push_back(p);
            spaceBefore = false;
            i = j;
            continue;
        }
        size_t j = i;
        while (j < g.size() && !IsSpace(g[j]) && !IsMarkChar(g[j])) ++j;
        Node n = MakeNode(Kind::Raw, hangul::ToU8(g.substr(i, j - i)));
        n.glue = !spaceBefore;
        out.push_back(n);
        spaceBefore = false;
        i = j;
    }
}

size_t WordEnd(const std::u32string& o, size_t p) {
    size_t e = p;
    while (e < o.size() && hangul::IsSyllable(o[e])) ++e;
    return e;
}

struct Aligner {
    Retro& r;
    const std::u32string& o;
    size_t c = 0;          // cursor in o
    std::vector<Node>& nodes;
    size_t aligned = 0, skipped = 0;
    static constexpr size_t kWindow = 40;

    void Gap(size_t to) {
        if (to > c) {
            const bool glue = c > 0 && !IsSpace(o[c - 1]);
            GapNodes(o.substr(c, to - c), glue && !nodes.empty(), nodes);
        }
        c = to;
    }
    bool SpaceBefore(size_t p) const { return p == 0 || IsSpace(o[p - 1]); }

    bool Text(const std::string& t, Kind k, bool attachOnly) {
        const std::u32string u = U(t);
        if (u.empty()) return false;
        const size_t p = o.find(u, c);
        if (p == std::u32string::npos || p - c > (attachOnly ? 0 : kWindow)) return false;
        Gap(p);
        Node n = MakeNode(k, t);
        n.glue = !SpaceBefore(p);
        nodes.push_back(n);
        c = p + u.size();
        ++aligned;
        return true;
    }
};

// Finds the predicate of a 0.7.x item in its final output and reads it (and the words of its ending row).
bool AlignPredicate(Retro& r, Aligner& a, const json::Value& it, Want want, Mood mood) {
    std::string lemma = it.Str("lemma");
    std::string pos = it.Str("pos"), cls = it.Str("cls");
    const PredicateInfo info = LookupPredicate(r.t, lemma);
    const bool known = r.t.lemmaPos.count(lemma) || r.t.stems.count(lemma);
    if (known || pos.empty()) pos = info.pos;
    if (cls.empty() || known) cls = info.cls;
    const bool cop = cls == "COP";
    std::string rest = it.Str("rest");
    for (size_t open = rest.find('('); open != std::string::npos; open = rest.find('(')) {
        const size_t close = rest.find(')', open);
        if (close == std::string::npos) break;
        rest.erase(open, close - open + 1);
    }
    const std::vector<std::string> chunks = Split(rest, ' ');
    size_t extra = 0;
    for (size_t k = 1; k < chunks.size(); ++k)
        if (!chunks[k].empty()) ++extra;
    std::string host;
    if (cop && !a.nodes.empty() && (a.nodes.back().kind == Kind::N || a.nodes.back().kind == Kind::Raw)) host = a.nodes.back().lemma;
    const size_t limit = std::min(a.o.size(), a.c + (cop ? 1 : Aligner::kWindow));
    for (size_t p = a.c; p < limit; ++p) {
        if (!hangul::IsSyllable(a.o[p])) continue;
        if (!cop && p > a.c && hangul::IsSyllable(a.o[p - 1]) && a.nodes.size() && a.nodes.back().kind != Kind::N) continue;
        const size_t e = WordEnd(a.o, p);
        const std::string w = hangul::ToU8(a.o.substr(p, e - p));
        AnalyzeHints h;
        h.want = extra ? Want::conn : want;
        h.mood = (e < a.o.size() && a.o[e] == U'?') ? Mood::q : mood;
        std::vector<Node> got;
        std::vector<Bundle> bs;
        if (cop) {
            h.host = host.empty() ? "3" : host;
            bs = r.Find(r.t.G("lemma.cop"), "C", "COP", w, h);
        } else {
            bs = r.Find(lemma, pos, cls, w, h);
            if (bs.empty() && pos == "V") bs = r.Find(lemma, "A", cls, w, h);
            if (bs.empty() && pos == "A") bs = r.Find(lemma, "V", cls, w, h);
        }
        if (!bs.empty()) {
            Node v = bs[0].node;
            if (cop) {
                v.lemma = r.t.G("lemma.cop");
                v.kcls = "COP";
                v.vpos = "C";
            }
            v.decision = static_cast<uint32_t>(bs[0].level);
            got.push_back(v);
        } else if (!cop) {
            // an auxiliary written onto the -어 form (가버렸다, 만들어졌다)
            const std::string av = ConjugateForms(r.t, lemma, pos, cls, {r.t.G("conn.aux")});
            if (!av.empty() && w.rfind(av, 0) == 0 && w.size() > av.size()) {
                std::vector<Node> tail;
                if (ReadRestWord(r, w.substr(av.size()), extra ? Want::conn : want, mood, tail, pos) && !tail.empty() &&
                    tail[0].kind == Kind::V) {
                    Node v = MakeNode(Kind::V, lemma);
                    v.vpos = pos;
                    v.kcls = cls;
                    v.ctype = CType::conn;
                    v.eomi = r.t.G("conn.aux");
                    got.push_back(v);
                    tail[0].glue = tail[0].lemma == r.t.G("voice.aux");
                    for (auto& n : tail) got.push_back(n);
                }
            }
        }
        if (got.empty()) continue;
        a.Gap(p);
        got[0].glue = got[0].glue || (!a.SpaceBefore(p) && !cop);
        for (auto& n : got) a.nodes.push_back(n);
        a.c = e;
        ++a.aligned;
        // the other words of the ending row, as 0.7.x finally wrote them
        for (size_t k = 0; k < extra; ++k) {
            size_t q = a.c;
            while (q < a.o.size() && IsSpace(a.o[q])) ++q;
            const size_t qe = WordEnd(a.o, q);
            if (qe == q) break;
            std::vector<Node> ns;
            const Want wk = k + 1 < extra ? Want::conn : want;
            const std::string ww = hangul::ToU8(a.o.substr(q, qe - q));
            auto jm = r.jima.find(ww);
            if (jm != r.jima.end() && !a.nodes.empty() && a.nodes.back().kind == Kind::V && a.nodes.back().eomi == r.t.G("conn.neg")) {
                Node& v = a.nodes.back();
                v.neg = Neg::jiMa;
                v.mood = Mood::imp;
                v.ctype = CType::final_;
                v.eomi.clear();
                Level lv = Level::hae;
                ParseLevel(jm->second, lv);
                v.decision = static_cast<uint32_t>(lv);
                a.c = qe;
                continue;
            }
            if (!ReadRestWord(r, ww, wk, (qe < a.o.size() && a.o[qe] == U'?') ? Mood::q : mood, ns, pos)) break;
            for (auto& n : ns) a.nodes.push_back(n);
            a.c = qe;
        }
        // fold -지 않다 / -지 못하다 / -지 말다 into the predicate
        for (size_t k = 0; k + 1 < a.nodes.size(); ++k) {
            Node& v = a.nodes[k];
            Node& x = a.nodes[k + 1];
            if (v.kind != Kind::V || x.kind != Kind::V || v.ctype != CType::conn || v.eomi != r.t.G("conn.neg")) continue;
            Neg neg = Neg::none;
            if (x.lemma == r.t.G("aux.neg")) neg = Neg::jiAnh;
            else if (x.lemma == r.t.G("aux.neg-mot")) neg = Neg::jiMot;
            else if (x.lemma == r.t.G("aux.prohib")) neg = Neg::jiMa;
            if (neg == Neg::none) continue;
            const std::string lem = v.lemma, vp = v.vpos, kc = v.kcls;
            const bool vaux = v.aux, vglue = v.glue;
            v = x;
            v.lemma = lem;
            v.vpos = vp;
            v.kcls = kc;
            v.aux = vaux;
            v.glue = vglue;
            v.neg = neg;
            if (neg == Neg::jiMa) {
                v.mood = Mood::imp;
                v.ctype = CType::final_;
                v.eomi.clear();
            }
            a.nodes.erase(a.nodes.begin() + static_cast<long>(k) + 1);
        }
        return true;
    }
    ++a.skipped;
    return false;
}

// Builds a line from 0.7.x's items aligned with its final output. Returns false when nothing aligned.
bool BuildAligned(Retro& r, const json::Value& v, std::vector<Node>& nodes, size_t& alignedOut, size_t& skippedOut) {
    const std::u32string o = U(v.Str("out075"));
    Aligner a{r, o, 0, nodes, 0, 0};
    const json::Value* items = v.Get("items");
    if (!items) return false;
    for (size_t k = 0; k < items->arr.size(); ++k) {
        const json::Value& it = items->arr[k];
        if (it.Bool("gap")) continue;
        const std::string kind = it.Str("k");
        Want want = Want::final_;
        Mood mood = Mood::decl;
        for (size_t j = k + 1; j < items->arr.size(); ++j) {
            if (items->arr[j].Bool("gap") && items->arr[j].Str("k") != "Punct") continue;
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
            const std::string t = it.Str("t");
            const Kind nk = kind == "N" && IsHangulWord(t) ? Kind::N : Kind::Raw;
            if (!a.Text(t, nk, false)) ++a.skipped;
            else if (nk == Kind::N) {
                // a word 0.7.x wrote whole (입니다, 부탁드립니다) may be a predicate
                Node lit;
                std::string host;
                if (nodes.size() >= 2 && nodes.back().glue) host = nodes[nodes.size() - 2].lemma;
                if (ReadLiteral(r, t, host, want, mood, lit)) {
                    lit.glue = nodes.back().glue;
                    nodes.back() = lit;
                    ++r.recovered;
                }
            }
        } else if (kind == "P") {
            const std::string t = it.Str("t");
            auto pk = r.particleKase.find(t);
            std::string form = t;
            Node p = MakeNode(Kind::P, "");
            if (pk != r.particleKase.end() && pk->second.rfind("lex:", 0) != 0) {
                p.kase = pk->second;
                for (const auto& row : r.t.particles)
                    if (row.kase == p.kase && row.when.empty()) form = row.form;
            } else {
                p.kase = "lex";
                p.lemma = pk != r.particleKase.end() ? pk->second.substr(4) : t;
                form = p.lemma;
            }
            // the particle as 0.7.x realised it after the word before it
            std::u32string prev;
            for (size_t q = nodes.size(); q-- > 0;) {
                if (nodes[q].kind == Kind::Punct) continue;
                const std::u32string lu = U(nodes[q].lemma);
                bool word = false;
                for (char32_t ch : lu) word = word || hangul::IsSyllable(ch) || (ch >= U'0' && ch <= U'9') || (ch >= U'A' && ch <= U'z');
                if (word) {
                    prev = lu;
                    break;
                }
            }
            const std::string realised = RealizeParticle(form, prev);
            const std::u32string ru = U(realised);
            if (!ru.empty() && o.compare(a.c, ru.size(), ru) == 0) {
                nodes.push_back(p);
                a.c += ru.size();
                ++a.aligned;
            } else {
                ++a.skipped;
            }
        } else if (kind == "Punct") {
            ++a.skipped;  // marks come from 0.7.x's output text (gaps)
        } else if (kind == "V") {
            // the source asks (逃げて！, 忘れてください): its last predicate is an imperative, not a statement
            bool lastV = true;
            for (size_t j = k + 1; j < items->arr.size(); ++j)
                if (items->arr[j].Str("k") == "V") lastV = false;
            if (lastV && want == Want::final_ && v.Bool("imp")) mood = Mood::imp;
            AlignPredicate(r, a, it, want, mood);
        }
    }
    a.Gap(o.size());
    alignedOut = a.aligned;
    skippedOut = a.skipped;
    return a.aligned > 0;
}

Punct PunctOf(const std::string& p) {
    const char* names[] = {"none", "comma", "pause", "end", "q", "excl", "qopen", "qclose", "ellipsis", "dash", "mark"};
    for (int i = 0; i < 11; ++i)
        if (p == names[i]) return static_cast<Punct>(i);
    return Punct::mark;
}

}  // namespace


struct RetroSystem {
    std::shared_ptr<const Tables> tables;
    std::shared_ptr<const RegisterTables> rtab;
    std::shared_ptr<const RuleSet> rules;
    std::map<std::pair<char, char>, RowForm> rows;
    std::vector<std::string> deps, auxLemmas;
    std::map<std::string, std::string> particleKase, jima, fused;
    std::unordered_map<char32_t, std::vector<std::string>> lemmasByFirst;
};

std::shared_ptr<const RetroSystem> LoadRetro(const std::string& data, std::string& err) {
    auto s = std::make_shared<RetroSystem>();
    s->tables = LoadTables(data + "/ko", err);
    if (s->tables) s->rtab = LoadRegisterTables(data + "/register", err);
    if (s->rtab) s->rules = LoadRules(data + "/ko/rewrite", err);
    if (!s->rules) return nullptr;
    TsvTable rt;
    if (!ReadTsv(data + "/retro/rows.tsv", rt, err)) return nullptr;
    for (const auto& row : rt.rows) s->rows[{row[0][0], row[1][0]}] = {row[2], row[3]};
    TsvTable lt;
    if (!ReadTsv(data + "/retro/words.tsv", lt, err)) return nullptr;
    for (const auto& row : lt.rows) {
        if (row[0] == "dep") s->deps.push_back(row[1]);
        else if (row[0] == "pred") s->auxLemmas.push_back(row[1]);
        else if (row[0] == "particle") s->particleKase[row[1]] = row[2];
        else if (row[0] == "jima") s->jima[row[1]] = row[2];
        else if (row[0] == "fused") s->fused[row[1]] = row[2];
    }
    std::stable_sort(s->deps.begin(), s->deps.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    for (const auto& kv : s->tables->lemmaPos) {
        const std::u32string lu = U(kv.first);
        if (lu.size() >= 2) s->lemmasByFirst[lu[0]].push_back(kv.first);
    }
    for (auto& kv : s->lemmasByFirst)  // longest stems first
        std::stable_sort(kv.second.begin(), kv.second.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    return s;
}

bool RunRetro(const RetroSystem& sys, const std::vector<std::string>& pre, const RetroOptions& opt,
              std::vector<RetroLine>& outLines, RetroStats* stats, std::string& err) {
    const Tables* tablesPtr = sys.tables.get();
    const RegisterTables* rtab = sys.rtab.get();
    const RuleSet* rules = sys.rules.get();
    Retro r{*tablesPtr, sys.rows, sys.deps, sys.auxLemmas, sys.particleKase, sys.jima, sys.fused, {}, 0, 0, 0, 0,
            sys.lemmasByFirst, opt.debug};
    const std::string& levelMode = opt.level;
    const bool alignMode = opt.align;
    const std::vector<std::string>& only = opt.only;
    const std::vector<std::string>& skip = opt.skip;
    size_t totalAligned = 0, totalSkipped = 0;
    std::vector<json::Value> rows;
    for (const std::string& line : pre) {
        json::Value v;
        if (!json::Parse(line, v, err)) return false;
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
    outLines.clear();
    size_t linesOk = 0;
    for (size_t li = 0; li < rows.size(); ++li) {
        const json::Value& v = rows[li];
        const json::Value* items = v.Get("items");
        std::vector<Node> nodes;
        bool lineOk = items && !items->arr.empty();
        if (alignMode) {
            size_t al = 0, sk = 0;
            lineOk = BuildAligned(r, v, nodes, al, sk);
            totalAligned += al;
            totalSkipped += sk;
            items = nullptr;
        }
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
                    std::string host;
                    if (n.glue && !nodes.empty() && (nodes.back().kind == Kind::N || nodes.back().kind == Kind::Raw)) host = nodes.back().lemma;
                    Node lit;
                    if (n.kind == Kind::N && ReadLiteral(r, n.lemma, host, want, mood, lit)) {
                        ++r.recovered;
                        lit.glue = lit.glue || n.glue;
                        nodes.push_back(lit);
                        continue;
                    }
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
        u.narration = d.narration;
        if (levelMode == "auto") {
            const bool first = d.firstPerson;
            u.level = u.narration ? Level::haera : d.level;
            u.persona = d.persona;
            reg = AssignRegister(*rtab, nodes, d, first, tablesPtr);
            if (u.narration)
                for (auto& g : reg) g.level = Level::haera;
            // a word 0.7.x wrote with a level the reader could not take apart: changing the others would mix levels
            bool mixed = false;
            for (const auto& n : nodes) {
                if (n.kind != Kind::Raw) continue;
                for (const char* key : {"mixed.hapsyo", "mixed.haeyo"})
                    for (const auto& e : Split(tablesPtr->G(key), '|'))
                        if (n.lemma.size() >= e.size() && n.lemma.compare(n.lemma.size() - e.size(), e.size(), e) == 0) mixed = true;
            }
            for (size_t q = 0; q < nodes.size(); ++q) {
                if (nodes[q].kind != Kind::V) continue;
                // a final ending inside the sentence (놀다니, 저희들…) keeps 0.7.x's level: no 요 mid-sentence
                const bool inner = nodes[q].ctype == CType::final_ && q + 2 < nodes.size() && nodes[q + 1].kind == Kind::Punct &&
                                   nodes[q + 1].punct == Punct::comma;
                // in a caption scene a plain command 0.7.x wrote as -어라 (来い → 와라) stays one
                const bool command = nodes[q].mood == Mood::imp && static_cast<Level>(nodes[q].decision) == Level::haera;
                if (!mixed && !inner && !d.keep && !(command && d.key.rfind("scene-speech", 0) == 0)) continue;
                for (auto& g : reg)
                    if (g.node == nodes[q].id && nodes[q].decision) {
                        g.level = static_cast<Level>(nodes[q].decision);
                        g.hon = false;
                    }
            }
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
        const RewriteResult rw = Rewrite(*rules, *tablesPtr, nodes, reg, {u}, only, skip);
        GenResult g = Generate(*tablesPtr, rw.kir2, rw.reg, {u});
        // a stutter (ど、どういう) repeats the first syllable of the next word: \x01, 어떤 → 어, 어떤
        for (size_t pos = g.out.find('\x01'); pos != std::string::npos; pos = g.out.find('\x01')) {
            size_t q = pos + 1;
            while (q < g.out.size() && (g.out[q] == ',' || g.out[q] == ' ')) ++q;
            std::string syl;
            if (q < g.out.size()) {
                const std::u32string rest = hangul::ToU32(g.out.substr(q));
                if (!rest.empty() && hangul::IsSyllable(rest[0])) syl = hangul::ToU8(rest.substr(0, 1));
            }
            g.out.replace(pos, 1, syl);
        }
        {
            std::string lead = v.Str("lead");
            if (alignMode) {
                const std::string o = v.Str("out075");
                size_t k = 0;
                while (k < o.size() && o[k] == ' ') ++k;
                lead = o.substr(0, k);
            }
            g.out = lead + g.out;
        }
        if (lineOk) ++linesOk;
        RetroLine o;
        o.out = lineOk ? g.out : v.Str("out075");
        o.gen = g.out;
        o.ok = lineOk;
        o.level = Name(u.level);
        o.key = d.key;
        for (const auto& f : rw.fired) o.fired.push_back(f.rule);
        o.kir = KirText(rw.kir2);
        outLines.push_back(std::move(o));
    }
    if (stats) {
        stats->lines += rows.size();
        stats->linesOk += linesOk;
        stats->words += r.words;
        stats->fallbacks += r.fallbacks;
        stats->literal += r.literal;
        stats->recovered += r.recovered;
        stats->aligned += totalAligned;
        stats->skipped += totalSkipped;
    }
    return true;
}

}  // namespace hd8gen
