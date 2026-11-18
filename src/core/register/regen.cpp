#include "core/register/regen.h"

#include <algorithm>
#include <memory>
#include <unordered_map>

#include "core/register/generate.h"
#include "core/register/hangul.h"
#include "core/register/levels.h"

namespace hdTrans::reg {

namespace {

using hangul::ToU32;
using hangul::ToU8;

// A host that ends in the same sound as host: the copula reads only that (학생이야, 친구야, A야, 1이야). A host
// whose last sound is not known (a kana word) stays itself.
std::string SoundOf(const std::string& host) {
    switch (hangul::EndSound(ToU32(host))) {
        case 'V': return "가";
        case 'C': return "각";
        case 'L': return "갈";
        default: return host;
    }
}

// The readings of the words of one call, by lemma, pos, class, host and surface.
struct Words {
    static constexpr std::size_t kKeptLemmas = 1024, kKeptWords = 32768;
    const Tables& t;
    const RetroWords& rw;
    std::unordered_map<std::string, std::unique_ptr<ParadigmIndex>> paradigms;
    std::unordered_map<std::string, std::pair<ParadigmIndex*, std::vector<uint32_t>>> found;

    std::vector<Bundle> Find(const std::string& lemma, const std::string& pos, const std::string& cls,
                             const std::string& surface, const AnalyzeHints& h) {
        // the copula's forms depend on its host only through the host's last sound (학생이야, 친구야)
        const std::string host = cls == "COP" ? SoundOf(h.host) : h.host;
        const std::string key = lemma + "|" + pos + "|" + cls + "|" + host;
        auto f = found.find(key + "|" + surface);
        if (f == found.end()) {
            // the readings are kept for the lemmas of a few hundred lines, so that a long document's memory stays bounded
            if (paradigms.size() >= kKeptLemmas || found.size() >= kKeptWords) {
                found.clear();
                paradigms.clear();
            }
            auto& index = paradigms[key];
            if (!index) index = std::make_unique<ParadigmIndex>(t, lemma, pos, cls, host);
            f = found.emplace(key + "|" + surface, std::make_pair(index.get(), index->Find(surface))).first;
        }
        std::vector<Bundle> out;
        for (const uint32_t r : f->second.second) out.push_back(f->second.first->At(r));
        RankBundles(out, h);
        return out;
    }
};

// The pos and class a predicate item is read with: the tables decide, except between verb and adjective,
// where the marker family the word stage conjugated the word with decides (우수하다고, not 우수한다고).
void ItemPosClass(const Tables& t, const std::string& lemma, std::string& pos, std::string& cls) {
    const std::string itemPos = pos;
    const PredicateInfo info = LookupPredicate(t, lemma);
    const bool known = t.lemmaPos.contains(lemma) || t.stems.contains(lemma);
    if (known || pos.empty()) pos = info.pos;
    if (cls.empty() || known) cls = info.cls;
    if ((itemPos == "V" || itemPos == "A") && (pos == "V" || pos == "A")) pos = itemPos;
}

bool IsHangulWord(const std::string& s) {
    const std::u32string u = ToU32(s);
    return !u.empty() && std::all_of(u.begin(), u.end(), hangul::IsSyllable);
}

Node MakeNode(Kind k, const std::string& lemma) {
    Node n;
    n.kind = k;
    n.lemma = lemma;
    return n;
}

// A word of an ending row after the first: a dependent noun (with a particle or the copula) or a predicate.
bool ReadRestWord(Words& r, const std::string& w, Want wantLast, Mood mood, std::vector<Node>& out,
                  const std::string& mainPos = "") {
    const Tables& t = r.t;
    {
        // 건 걸 게 겁…: 거 fused with a particle or the copula (것 here; the generator fuses it again in speech)
        const std::u32string u = ToU32(w);
        const auto f = u.empty() ? r.rw.fused.end() : r.rw.fused.find(ToU8(u.substr(0, 1)));
        if (f != r.rw.fused.end()) {
            Node n = MakeNode(Kind::N, t.G("nom.geot"));
            n.dep = true;
            if (f->second == "cop" || u.size() > 1) {
                // 건가, 건데, 겁니다: 거 + 이(ㄴ/ㄹ/ㅂ)…: the copula with its jamo restored
                const std::u32string cop =
                    std::u32string(1, hangul::Syl(hangul::kInitIeung, hangul::I, hangul::Final(u[0]))) + u.substr(1);
                AnalyzeHints h;
                h.want = wantLast;
                h.mood = mood;
                h.host = "3";
                const auto bs = r.Find(t.G("lemma.cop"), "C", "COP", ToU8(cop), h);
                if (!bs.empty()) {
                    out.push_back(n);
                    Node v = bs[0].node;
                    v.was = bs[0].level;
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
    for (const auto& d : r.rw.deps) {
        if (!w.starts_with(d)) continue;
        const std::string rem = w.substr(d.size());
        Node n = MakeNode(Kind::N, d);
        n.dep = true;
        if (rem.empty()) {
            out.push_back(n);
            return true;
        }
        const auto pk = r.rw.particleKase.find(rem);
        if (pk != r.rw.particleKase.end()) {
            out.push_back(n);
            Node p = MakeNode(Kind::P, "");
            if (pk->second.starts_with("lex:")) {
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
        const auto bs = r.Find(t.G("lemma.cop"), "C", "COP", rem, h);
        if (!bs.empty()) {
            out.push_back(n);
            Node v = bs[0].node;
            v.was = bs[0].level;
            out.push_back(v);
            return true;
        }
    }
    const std::u32string u = ToU32(w);
    if (u.empty() || !hangul::IsSyllable(u[0])) return false;
    for (const auto& lemma : r.rw.auxLemmas) {
        const std::u32string lu = ToU32(lemma);
        if (lu.empty() || hangul::Initial(lu[0]) != hangul::Initial(u[0])) continue;
        AnalyzeHints h;
        h.want = wantLast;
        h.mood = mood;
        const bool negAux = lemma == t.G("aux.neg") || lemma == t.G("aux.neg-mot");
        auto bs = r.Find(lemma, negAux && !mainPos.empty() ? (mainPos == "V" ? "V" : "A") : "", "", w, h);
        if (bs.empty() && negAux) bs = r.Find(lemma, "", "", w, h);
        if (bs.empty()) continue;
        Node v = bs[0].node;
        v.aux = true;
        v.was = bs[0].level;
        out.push_back(v);
        return true;
    }
    return false;
}

// A word the word stage wrote whole (입니다 after a noun, 부탁드립니다): the copula or a known lemma when it
// ends like a sentence (다, 요, 까, 죠).
bool ReadLiteral(Words& r, const std::string& text, const std::string& host, Want want, Mood mood, Node& out) {
    const Tables& t = r.t;
    const std::u32string u = ToU32(text);
    if (u.size() < 2) return false;
    if (ToU32(t.G("lit.ends")).find(u.back()) == std::u32string::npos) return false;
    AnalyzeHints h;
    h.want = want;
    h.mood = mood;
    if (!host.empty()) {
        h.host = host;
        const auto bs = r.Find(t.G("lemma.cop"), "C", "COP", text, h);
        if (!bs.empty()) {
            out = bs[0].node;
            out.glue = true;
            out.was = bs[0].level;
            return true;
        }
        h.host.clear();
    }
    const auto it = r.rw.lemmasByFirst.find(u[0]);
    if (it == r.rw.lemmasByFirst.end()) return false;
    for (const auto& lemma : it->second) {
        const std::u32string lu = ToU32(lemma);
        const std::u32string stem = lu.substr(0, lu.size() - 1);
        if (stem.size() > u.size() || stem.size() + 4 < u.size()) continue;
        if (u.compare(0, stem.size() - 1, stem, 0, stem.size() - 1) != 0) continue;
        const auto bs = r.Find(lemma, "", "", text, h);
        if (bs.empty()) continue;
        out = bs[0].node;
        out.was = bs[0].level;
        return true;
    }
    return false;
}

bool IsSpace(char32_t c) { return c == U' ' || c == 0x3000 || c == U'\t'; }
bool IsMarkChar(char32_t c) {
    switch (c) {
        case U',': case U'.': case U'?': case U'!': case 0x2026: case 0x300C: case 0x300D: case 0x300E: case 0x300F:
        case U'(': case U')': case U'~': case 0x2015: case 0x2500: case 0x00B7: case 0xFF5E: case 0x2014:
            return true;
        default:
            return false;
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

// The text of a gap as Raw words and Punct marks (each mark keeps its own character); the gap starts at
// offset in the translation.
void GapNodes(const std::u32string& g, std::size_t offset, bool glueFirst, std::vector<Node>& out) {
    std::size_t i = 0;
    bool spaceBefore = !glueFirst;
    while (i < g.size()) {
        if (IsSpace(g[i])) {
            spaceBefore = true;
            ++i;
            continue;
        }
        if (IsMarkChar(g[i])) {
            std::size_t j = i;
            while (j < g.size() && g[j] == g[i]) ++j;
            Node p = MakeNode(Kind::Punct, "");
            p.punct = PunctOfChar(g[i]);
            p.jaMark = ToU8(g.substr(i, j - i));
            p.glue = !spaceBefore;
            p.from = static_cast<uint32_t>(offset + i);
            p.to = static_cast<uint32_t>(offset + j);
            out.push_back(p);
            spaceBefore = false;
            i = j;
            continue;
        }
        std::size_t j = i;
        while (j < g.size() && !IsSpace(g[j]) && !IsMarkChar(g[j])) ++j;
        Node n = MakeNode(Kind::Raw, ToU8(g.substr(i, j - i)));
        n.glue = !spaceBefore;
        n.from = static_cast<uint32_t>(offset + i);
        n.to = static_cast<uint32_t>(offset + j);
        out.push_back(n);
        spaceBefore = false;
        i = j;
    }
}

std::size_t WordEnd(const std::u32string& o, std::size_t p) {
    std::size_t e = p;
    while (e < o.size() && hangul::IsSyllable(o[e])) ++e;
    return e;
}

// The line is rebuilt on its translation: every item found there in order becomes nodes (and is
// generated again); the text between found items stays as it is (Raw nodes, marks as Punct).
struct Aligner {
    const std::u32string& o;
    std::vector<Node>& nodes;
    std::size_t c = 0;  // cursor in o
    std::size_t aligned = 0;
    static constexpr std::size_t kWindow = 40;

    void Gap(std::size_t to) {
        if (to > c) {
            const bool glue = c > 0 && !IsSpace(o[c - 1]);
            GapNodes(o.substr(c, to - c), c, glue && !nodes.empty(), nodes);
        }
        c = to;
    }
    bool SpaceBefore(std::size_t p) const { return p == 0 || IsSpace(o[p - 1]); }
    bool Text(const std::string& t, Kind k) {
        const std::u32string u = ToU32(t);
        if (u.empty()) return false;
        const std::size_t p = std::u32string_view(o).substr(0, c + kWindow + u.size()).find(u, c);
        if (p == std::u32string::npos) return false;
        Gap(p);
        Node n = MakeNode(k, t);
        n.glue = !SpaceBefore(p);
        n.from = static_cast<uint32_t>(p);
        n.to = static_cast<uint32_t>(p + u.size());
        nodes.push_back(n);
        c = p + u.size();
        ++aligned;
        return true;
    }
};

// -지 않다, -지 못하다, -지 말다 folded into the predicate.
void FoldNegation(const Tables& t, std::vector<Node>& nodes) {
    for (std::size_t k = 0; k + 1 < nodes.size(); ++k) {
        Node& v = nodes[k];
        const Node& x = nodes[k + 1];
        if (v.kind != Kind::V || x.kind != Kind::V || v.ctype != CType::conn || v.eomi != t.G("conn.neg")) continue;
        Neg neg = Neg::none;
        if (x.lemma == t.G("aux.neg")) neg = Neg::jiAnh;
        else if (x.lemma == t.G("aux.neg-mot")) neg = Neg::jiMot;
        else if (x.lemma == t.G("aux.prohib")) neg = Neg::jiMa;
        if (neg == Neg::none) continue;
        const std::string lem = v.lemma, vp = v.vpos, kc = v.kcls;
        const bool vaux = v.aux, vglue = v.glue;
        const uint32_t vfrom = std::min(v.from, x.from), vto = std::max(v.to, x.to);
        v = x;
        v.from = vfrom;
        v.to = vto;
        v.lemma = lem;
        v.vpos = vp;
        v.kcls = kc;
        v.aux = vaux;
        v.glue = vglue;
        v.neg = neg;
        // -지 말아 before the 주다 of a request (말하지 말아 줘, 말아 줘요, 말아 주세요) joins it as the 말다 does;
        // the 말다 that ends its clause (말하지 마) or comes before another word is the prohibitive order itself
        const bool request = x.ctype == CType::conn && k + 2 < nodes.size() && nodes[k + 2].kind == Kind::V &&
                             nodes[k + 2].lemma == t.G("aux.give");
        if (neg == Neg::jiMa && !request) {
            v.mood = Mood::imp;
            v.ctype = CType::final_;
            v.eomi.clear();
        }
        nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(k) + 1);
    }
}

// Finds a predicate item in the translation and reads it, with the other words of its ending row.
bool AlignPredicate(Words& r, Aligner& a, const PreItem& it, Want want, Mood mood) {
    const Tables& t = r.t;
    const std::string& lemma = it.lemma;
    std::string pos = it.pos, cls = it.cls;
    ItemPosClass(t, lemma, pos, cls);
    const bool cop = cls == "COP";
    std::string rest = it.rest;
    for (std::size_t open = rest.find('('); open != std::string::npos; open = rest.find('(')) {  // 면(자) → 면
        const std::size_t close = rest.find(')', open);
        if (close == std::string::npos) break;
        rest.erase(open, close - open + 1);
    }
    const std::vector<std::string> chunks = Split(rest, ' ');
    std::size_t extra = 0;
    for (std::size_t k = 1; k < chunks.size(); ++k)
        if (!chunks[k].empty()) ++extra;
    std::string host;
    if (cop && !a.nodes.empty() && (a.nodes.back().kind == Kind::N || a.nodes.back().kind == Kind::Raw))
        host = a.nodes.back().lemma;
    const std::size_t limit = std::min(a.o.size(), a.c + (cop ? 1 : Aligner::kWindow));
    for (std::size_t p = a.c; p < limit; ++p) {
        if (!hangul::IsSyllable(a.o[p])) continue;
        if (!cop && p > a.c && hangul::IsSyllable(a.o[p - 1]) && !a.nodes.empty() && a.nodes.back().kind != Kind::N) continue;
        const std::size_t e = WordEnd(a.o, p);
        const std::string w = ToU8(a.o.substr(p, e - p));
        AnalyzeHints h;
        h.want = extra ? Want::conn : want;
        h.mood = (e < a.o.size() && a.o[e] == U'?') ? Mood::q : mood;
        std::vector<Node> got;
        std::vector<Bundle> bs;
        if (cop) {
            h.host = host.empty() ? "3" : host;
            bs = r.Find(t.G("lemma.cop"), "C", "COP", w, h);
        } else {
            bs = r.Find(lemma, pos, cls, w, h);
            if (bs.empty() && pos == "V") bs = r.Find(lemma, "A", cls, w, h);
            if (bs.empty() && pos == "A") bs = r.Find(lemma, "V", cls, w, h);
        }
        if (!bs.empty()) {
            Node v = bs[0].node;
            if (cop) {
                v.lemma = t.G("lemma.cop");
                v.kcls = "COP";
                v.vpos = "C";
            }
            v.was = bs[0].level;
            got.push_back(v);
        } else if (!cop) {
            // an auxiliary written onto the -어 form (가버렸다, 만들어졌다)
            const std::string av = ConjugateForms(t, lemma, pos, cls, {t.G("conn.aux")});
            if (!av.empty() && w.starts_with(av) && w.size() > av.size()) {
                std::vector<Node> tail;
                if (ReadRestWord(r, w.substr(av.size()), extra ? Want::conn : want, mood, tail, pos) && !tail.empty() &&
                    tail[0].kind == Kind::V) {
                    Node v = MakeNode(Kind::V, lemma);
                    v.vpos = pos;
                    v.kcls = cls;
                    v.ctype = CType::conn;
                    v.eomi = t.G("conn.aux");
                    got.push_back(v);
                    tail[0].glue = tail[0].lemma == t.G("voice.aux");
                    for (auto& n : tail) got.push_back(n);
                }
            }
        }
        if (got.empty()) continue;
        a.Gap(p);
        got[0].glue = got[0].glue || (!a.SpaceBefore(p) && !cop);
        for (auto& n : got) {
            n.from = static_cast<uint32_t>(p);
            n.to = static_cast<uint32_t>(e);
            a.nodes.push_back(n);
        }
        a.c = e;
        ++a.aligned;
        // the other words of the ending row, as the translation wrote them
        for (std::size_t k = 0; k < extra; ++k) {
            std::size_t q = a.c;
            while (q < a.o.size() && IsSpace(a.o[q])) ++q;
            const std::size_t qe = WordEnd(a.o, q);
            if (qe == q) break;
            std::vector<Node> ns;
            const Want wk = k + 1 < extra ? Want::conn : want;
            const std::string ww = ToU8(a.o.substr(q, qe - q));
            const auto jm = r.rw.jima.find(ww);
            if (jm != r.rw.jima.end() && !a.nodes.empty() && a.nodes.back().kind == Kind::V &&
                a.nodes.back().eomi == t.G("conn.neg")) {
                Node& v = a.nodes.back();  // -지 마: the prohibitive of the predicate
                v.neg = Neg::jiMa;
                v.mood = Mood::imp;
                v.ctype = CType::final_;
                v.eomi.clear();
                Level lv = Level::hae;
                ParseLevel(jm->second, lv);
                v.was = lv;
                v.to = static_cast<uint32_t>(qe);
                a.c = qe;
                continue;
            }
            if (!ReadRestWord(r, ww, wk, (qe < a.o.size() && a.o[qe] == U'?') ? Mood::q : mood, ns, pos)) break;
            for (auto& n : ns) {
                n.from = static_cast<uint32_t>(q);
                n.to = static_cast<uint32_t>(qe);
                a.nodes.push_back(n);
            }
            a.c = qe;
        }
        return true;
    }
    return false;
}

// Builds a line from its items aligned with its translation; false when nothing aligned.
bool BuildAligned(Words& r, const PreLine& line, std::vector<Node>& nodes) {
    const Tables& t = r.t;
    const std::u32string o = ToU32(line.out);
    Aligner a{o, nodes};
    const std::vector<PreItem>& items = line.items;
    for (std::size_t k = 0; k < items.size(); ++k) {
        const PreItem& it = items[k];
        if (it.gap) continue;
        // hints from the next item
        Want want = Want::final_;
        Mood mood = Mood::decl;
        for (std::size_t j = k + 1; j < items.size(); ++j) {
            if (items[j].gap && items[j].kind != PreItem::Kind::Punct) continue;
            const PreItem::Kind nk = items[j].kind;
            if (nk == PreItem::Kind::Punct) {
                const std::string& p = items[j].p;
                if (p == "q") mood = Mood::q;
                if (p == "comma") want = Want::conn;
                if (p == "qclose" || p == "ellipsis" || p == "mark" || p == "dash") continue;
                break;
            }
            if (nk == PreItem::Kind::N || nk == PreItem::Kind::Raw) want = Want::adn;
            else if (nk == PreItem::Kind::V) want = Want::conn;
            else if (nk == PreItem::Kind::P) want = Want::nom;
            break;
        }
        if (it.kind == PreItem::Kind::N || it.kind == PreItem::Kind::Raw) {
            const Kind nk = it.kind == PreItem::Kind::N && IsHangulWord(it.t) ? Kind::N : Kind::Raw;
            if (a.Text(it.t, nk) && nk == Kind::N) {
                // a word written whole (입니다, 부탁드립니다) may be a predicate
                Node lit;
                std::string host;
                if (nodes.size() >= 2 && nodes.back().glue) host = nodes[nodes.size() - 2].lemma;
                if (ReadLiteral(r, it.t, host, want, mood, lit)) {
                    lit.glue = nodes.back().glue;
                    lit.from = nodes.back().from;
                    lit.to = nodes.back().to;
                    nodes.back() = lit;
                }
            }
        } else if (it.kind == PreItem::Kind::P) {
            const auto pk = r.rw.particleKase.find(it.t);
            std::string form = it.t;
            Node p = MakeNode(Kind::P, "");
            if (pk != r.rw.particleKase.end() && !pk->second.starts_with("lex:")) {
                p.kase = pk->second;
                for (const auto& row : t.particles)
                    if (row.kase == p.kase && row.when.empty()) form = row.form;
            } else {
                p.kase = "lex";
                p.lemma = pk != r.rw.particleKase.end() ? pk->second.substr(4) : it.t;
                form = p.lemma;
            }
            // the particle as the translation wrote it after the word before it
            std::u32string prev;
            for (std::size_t q = nodes.size(); q-- > 0;) {
                if (nodes[q].kind == Kind::Punct) continue;
                const std::u32string lu = ToU32(nodes[q].lemma);
                const bool word = std::any_of(lu.begin(), lu.end(), [](char32_t ch) {
                    return hangul::IsSyllable(ch) || (ch >= U'0' && ch <= U'9') || (ch >= U'A' && ch <= U'z');
                });
                if (word) {
                    prev = lu;
                    break;
                }
            }
            const std::u32string ru = ToU32(RealizeParticle(form, prev));
            if (!ru.empty() && o.compare(a.c, ru.size(), ru) == 0) {
                p.from = static_cast<uint32_t>(a.c);
                p.to = static_cast<uint32_t>(a.c + ru.size());
                nodes.push_back(p);
                a.c += ru.size();
                ++a.aligned;
            }
        } else if (it.kind == PreItem::Kind::V) {
            // the source asks (逃げて！, 忘れてください): its last predicate is an imperative, not a statement
            bool lastV = true;
            for (std::size_t j = k + 1; j < items.size(); ++j)
                if (items[j].kind == PreItem::Kind::V) lastV = false;
            if (lastV && want == Want::final_ && line.imp) mood = Mood::imp;
            AlignPredicate(r, a, it, want, mood);
        }
        // marks come from the translation's own text (gaps)
    }
    a.Gap(o.size());
    // after every item: the 주다 of a request may be an item of its own after -지 말아 (말하지 말아 + 줘)
    FoldNegation(t, nodes);
    return a.aligned > 0;
}

// Whether s holds one of the |-separated strings of list.
bool ContainsAny(const std::string& s, const std::string& list) {
    for (const auto& w : Split(list, '|'))
        if (!w.empty() && s.find(w) != std::string::npos) return true;
    return false;
}

bool EndsWithAny(const std::string& s, const std::string& list) {
    for (const auto& e : Split(list, '|'))
        if (s.ends_with(e)) return true;
    return false;
}

// A copula read with its noun as one predicate (것이다): the noun and 이다 apart, so that speech fuses them
// (거야) as it does when they come apart.
void SplitNounCopula(const Tables& t, std::vector<Node>& nodes) {
    const std::string cop = t.G("lemma.cop");
    for (std::size_t q = 0; q < nodes.size(); ++q) {
        const Node& n = nodes[q];
        if (n.kind != Kind::V || n.lemma.size() <= cop.size() || !n.lemma.ends_with(cop)) continue;
        const std::string host = n.lemma.substr(0, n.lemma.size() - cop.size());
        bool fuses = false;
        for (const auto& sr : t.spoken)
            if (sr.host == host && sr.next == "COP") fuses = true;
        if (!fuses) continue;
        Node noun = MakeNode(Kind::N, host);
        noun.dep = true;
        noun.glue = n.glue;
        noun.from = n.from;
        noun.to = n.to;
        Node v = n;
        v.lemma = cop;
        v.kcls = "COP";
        v.vpos = "C";
        v.glue = true;
        nodes[q] = v;
        nodes.insert(nodes.begin() + static_cast<std::ptrdiff_t>(q), noun);
        ++q;
    }
}

// REG-25: a word with the past -았-/-었-/-였- written out after a vowel (보았습니다, 꾸었습니다, 하였습니다) in the
// contracted spelling the generator writes (봤습니다, 꿨습니다, 했습니다); other words as they are.
std::string ContractPast(const std::string& s) {
    std::u32string u = ToU32(s);
    const auto syl = [](char32_t c) { return c >= 0xAC00 && c <= 0xD7A3; };
    for (std::size_t i = 1; i < u.size(); ++i) {
        const char32_t p = u[i - 1], c = u[i];
        if (!syl(p) || (p - 0xAC00) % 28 != 0 || (c != U'았' && c != U'었' && c != U'였')) continue;
        const uint32_t cho = (p - 0xAC00) / 588, jung = (p - 0xAC00) / 28 % 21;
        int to = -1;
        if (c == U'았' && jung == 8) to = 9;                     // ㅗ + 았 → ㅘ
        else if (c == U'었' && jung == 13) to = 14;              // ㅜ + 었 → ㅝ
        else if (c == U'었' && jung == 20) to = 6;               // ㅣ + 었 → ㅕ
        else if (c == U'었' && jung == 11) to = 10;              // ㅚ + 었 → ㅙ
        else if (c == U'였' && jung == 0 && cho == 18) to = 1;   // 하 + 였 → 했
        if (to < 0) continue;
        u[i - 1] = static_cast<char32_t>(0xAC00 + (cho * 21 + static_cast<uint32_t>(to)) * 28 + 20);
        u.erase(i, 1);
    }
    return ToU8(u);
}

// REG-25: a sentence-final word in 합쇼체 the alignment left unread (a phrase row such as 정말입니까 or 어떻습니까, the
// 있습니다 of 수 있습니다, 이릅니다, 보았습니다) is read as the predicate the tables know, or as a noun with the copula,
// so that its level can change like any other sentence end.
void ReadHapsyoFinals(Words& r, const Tables& t, std::vector<Node>& nodes) {
    for (std::size_t q = 0; q < nodes.size(); ++q) {
        const Node n = nodes[q];
        if ((n.kind != Kind::Raw && n.kind != Kind::N) || !IsHangulWord(n.lemma)) continue;
        if (q + 1 < nodes.size() && nodes[q + 1].kind != Kind::Punct) continue;
        const std::u32string w = ToU32(n.lemma);
        if (w.size() < 3 || (!std::u32string_view(w).ends_with(U"니다") && !std::u32string_view(w).ends_with(U"니까"))) continue;
        const char32_t b = w[w.size() - 3];
        if ((b - 0xAC00) % 28 != 17) continue;
        const bool question = w.back() == U'까';
        AnalyzeHints h;
        h.want = Want::final_;
        h.mood = question ? Mood::q : Mood::decl;
        const std::string surface = ContractPast(n.lemma);
        // a predicate: the stem before 습니다, or the syllable before ㅂ니다 without its ㅂ; -았-/-었- written out first
        std::u32string stem = b == U'습' ? w.substr(0, w.size() - 3) : w.substr(0, w.size() - 3) + std::u32string(1, b - 17);
        if (stem.size() >= 2 && (stem.back() == U'았' || stem.back() == U'었' || stem.back() == U'였')) stem.pop_back();
        std::vector<Bundle> found;
        std::string lemma = ToU8(stem) + "다", pos, cls;
        if (!stem.empty() && (t.stems.contains(lemma) || t.lemmaPos.contains(lemma))) {
            ItemPosClass(t, lemma, pos, cls);
            found = r.Find(lemma, pos, cls, surface, h);
        }
        std::erase_if(found, [](const Bundle& x) { return x.level != Level::hapsyo || x.node.ctype != CType::final_; });
        if (!found.empty()) {
            Node v = found.front().node;
            v.kind = Kind::V;
            v.from = n.from;
            v.to = n.to;
            v.glue = n.glue;
            v.was = Level::hapsyo;
            nodes[q] = v;
            continue;
        }
        // a noun with the copula (정말입니까, 사실이었습니다)
        for (const std::u32string_view cop : {std::u32string_view(U"이었습니"), std::u32string_view(U"였습니"), std::u32string_view(U"입니")}) {
            const std::size_t at = w.size() - 1 - cop.size();
            if (w.size() < cop.size() + 2 || std::u32string_view(w).substr(at, cop.size()) != cop) continue;
            const std::string host = ToU8(w.substr(0, at)), copText = ToU8(w.substr(at));
            if (host.empty() || !IsHangulWord(host) || t.stems.contains(host + "다")) break;
            h.host = host;
            std::vector<Bundle> c = r.Find(t.G("lemma.cop"), "C", "COP", copText, h);
            std::erase_if(c, [](const Bundle& x) { return x.level != Level::hapsyo || x.node.ctype != CType::final_; });
            if (c.empty()) break;
            Node noun = MakeNode(Kind::N, host);
            noun.glue = n.glue;
            noun.from = n.from;
            noun.to = n.from + static_cast<uint32_t>(at);
            Node v = c.front().node;
            v.kind = Kind::V;
            v.lemma = t.G("lemma.cop");
            v.kcls = "COP";
            v.vpos = "C";
            v.glue = true;
            v.from = noun.to;
            v.to = n.to;
            v.was = Level::hapsyo;
            nodes[q] = v;
            nodes.insert(nodes.begin() + static_cast<std::ptrdiff_t>(q), noun);
            ++q;
            break;
        }
    }
}

// REG-21: whether the predicate at q stands alone in its clause: before it, back to a mark or the line start,
// only its own verbs (나와 버린다) and an adverb of time (이제, 또). A verb giving (해 줄게) is no declaration.
bool SingleVerb(const std::vector<Node>& nodes, std::size_t q) {
    if (nodes[q].lemma == "주다") return false;
    for (std::size_t k = q; k > 0; --k) {
        const Node& p = nodes[k - 1];
        if (p.kind == Kind::Punct) return true;
        // its own verbs: one it is written onto (도망쳐 간다) or the same verb said again (간다 간다)
        if (p.kind == Kind::V && (p.ctype == CType::conn || p.lemma == nodes[q].lemma)) continue;
        if (p.kind == Kind::N || p.kind == Kind::Raw) {
            bool adverb = false;
            for (const char* a : {"이제", "이젠", "또", "곧", "다시", "벌써", "바로"})
                if (p.lemma == a) adverb = true;
            if (adverb) continue;
        }
        return false;
    }
    return true;
}

// Whether a quote of the source is left open (「… without its 」).
bool OpenQuote(const std::string& ja) {
    int a = 0, b = 0;
    for (const char32_t c : ToU32(ja)) {
        if (c == U'「') ++a;
        else if (c == U'」') --a;
        else if (c == U'『') ++b;
        else if (c == U'』') --b;
    }
    return a != 0 || b != 0;
}

// REG-12, REG-25: the sentences of the source's quote; sentences end at a run of 。．！？ outside an inner quote.
// Empty unless the line is one quote (its first quote, closed where its depth comes back to 0, with no quote after it).
std::vector<std::u32string> SourceSentences(const std::string& ja) {
    const std::u32string u = ToU32(ja);
    const std::size_t open = u.find_first_of(U"「『");
    if (open == std::u32string::npos) return {};
    std::size_t end = std::u32string::npos;
    for (std::size_t i = open, depth = 0; i < u.size(); ++i) {
        if (u[i] == U'「' || u[i] == U'『') ++depth;
        if ((u[i] == U'」' || u[i] == U'』') && depth > 0 && --depth == 0) {
            end = i;
            break;
        }
    }
    if (end == std::u32string::npos || u.find_first_of(U"「『", end) != std::u32string::npos) return {};
    const auto isEnd = [](char32_t c) { return c == U'。' || c == U'．' || c == U'！' || c == U'？' || c == U'!' || c == U'?'; };
    std::vector<std::u32string> out;
    std::u32string cur;
    int depth = 0;
    for (std::size_t i = open + 1; i < end; ++i) {
        const char32_t c = u[i];
        if (c == U'「' || c == U'『') ++depth;
        if ((c == U'」' || c == U'』') && depth > 0) --depth;
        if (depth == 0 && isEnd(c)) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// REG-12: the end of each sentence of the source's quote, the marks after it and a sentence-final よ, ね left out.
std::vector<std::u32string> SourceSentenceEnds(const std::string& ja) {
    std::vector<std::u32string> out = SourceSentences(ja);
    for (auto& e : out) {
        while (!e.empty() && std::u32string_view(U"ー〜～っッ…‥・ 　").find(e.back()) != std::u32string_view::npos) e.pop_back();
        for (const std::u32string_view tail : {std::u32string_view(U"よね"), std::u32string_view(U"よ"), std::u32string_view(U"ね")})
            if (e.size() > tail.size() && std::u32string_view(e).ends_with(tail)) {
                e.resize(e.size() - tail.size());
                break;
            }
    }
    return out;
}

// REG-27: whether s holds one of the |-separated 합쇼체 endings of list (니다, 니까) after a syllable closed with ㅂ
// (있습니다만, 합니까), not the -니까 of a reason or a conjunction (곤란하니까, 그러니까).
bool HoldsHapsyo(const std::u32string& s, const std::string& list) {
    for (const auto& w : Split(list, '|')) {
        const std::u32string e = ToU32(w);
        if (e.empty()) continue;
        for (std::size_t at = s.find(e); at != std::u32string::npos; at = s.find(e, at + 1))
            if (at > 0 && hangul::IsSyllable(s[at - 1]) && (s[at - 1] - 0xAC00) % 28 == 17) return true;
    }
    return false;
}

// REG-23, REG-27: whether a source sentence end is the explanatory んです after the plain form of a predicate
// (来たんです, いるんです, 好きなんです, 行くんです, 遊ぶんです), not a noun in ん (おばあちゃんです, ハヤテさんです,
// イチくんです, じゅうぶんです): く not after katakana, ぶ only after a kanji.
bool ExplanatoryEnd(std::u32string_view e) {
    if (!e.ends_with(U"んです") || e.size() < 4) return false;
    const char32_t c = e[e.size() - 4];
    const char32_t before = e.size() >= 5 ? e[e.size() - 5] : U'\0';
    const bool katakana = before >= U'ァ' && before <= U'ヿ';
    const bool kanji = (before >= U'一' && before <= U'鿿') || before == U'々';
    if (c == U'く') return !katakana;
    if (c == U'ぶ') return kanji;
    return std::u32string_view(U"るただいなうすつぬむぐ").find(c) != std::u32string_view::npos;
}

// REG-25: the source sentences split again at a run of …/‥ right after a polite sentence end (楽しかったです……本当に).
std::vector<std::u32string> PoliteSentences(const RegisterTables& rt, const std::vector<std::u32string>& sents) {
    std::vector<std::u32string> out;
    for (const auto& s : sents) {
        std::size_t start = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] != U'…' && s[i] != U'‥') continue;
            std::size_t j = i;
            while (j < s.size() && (s[j] == U'…' || s[j] == U'‥')) ++j;
            if (i > start && j < s.size() && CuesOfSentence(rt, "「" + ToU8(s.substr(start, i - start)) + "」").pend) {
                out.push_back(s.substr(start, i - start));
                start = j;
            }
            i = j - 1;
        }
        if (start < s.size()) out.push_back(s.substr(start));
    }
    return out;
}

// REG-27: whether a source sentence has a subject marked with が after a noun (雨が, 誰が; not ですが, ながら).
bool OtherSubject(std::u32string_view s) {
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i] != U'が') continue;
        const char32_t p = s[i - 1];
        const bool kanji = (p >= U'一' && p <= U'鿿') || p == U'々';
        const bool katakana = (p >= U'ァ' && p <= U'ヺ') || p == U'ー';
        const bool pronoun = p == U'れ' && i >= 2 && std::u32string_view(U"こそあど").find(s[i - 2]) != std::u32string_view::npos;
        if (kanji || katakana || pronoun) return true;
    }
    return false;
}

// REG-26: whether the quote addresses an audience, a master or royalty (source.address), each term matched as a
// word: not inside a longer katakana word or before the stem of a verb (マスターする, マスターズ).
bool Addressed(const std::string& ja, const Tables& t) {
    const std::u32string u = ToU32(ja);
    const std::u32string notBefore = ToU32(t.G("source.address-notbefore"));
    for (const auto& w : Split(t.G("source.address"), '|')) {
        const std::u32string e = ToU32(w);
        if (e.empty()) continue;
        const bool kata = (e.back() >= U'ァ' && e.back() <= U'ヺ') || e.back() == U'ー';
        for (std::size_t at = u.find(e); at != std::u32string::npos; at = u.find(e, at + 1)) {
            const std::size_t n = at + e.size();
            if (n < u.size() && notBefore.find(u[n]) != std::u32string::npos) continue;
            if (kata && n < u.size() && ((u[n] >= U'ァ' && u[n] <= U'ヺ') || u[n] == U'ー')) continue;
            return true;
        }
    }
    return false;
}

// The text Generate wrote for each node, by node id (bytes; the spaces between a node's pieces included).
std::vector<std::pair<std::size_t, std::size_t>> NodeTexts(std::size_t count, const std::vector<OutSpan>& spans) {
    std::vector<std::pair<std::size_t, std::size_t>> out(count + 1, {std::string::npos, 0});
    for (const OutSpan& sp : spans) {
        if (sp.node > count) continue;
        auto& r = out[sp.node];
        r.first = std::min(r.first, sp.begin);
        r.second = std::max(r.second, sp.end);
    }
    return out;
}

// The translation with only its sentence-final predicates written again: every other word stays as the
// word stage wrote it. A predicate is written again only when the same reading at its old level gives back
// the old text (the reading is sure); otherwise, or when nothing can be placed, ok turns false.
std::string Splice(const Tables& t, const std::string& before, const std::vector<Node>& nodes, const std::vector<Reg>& reg,
                   const UttLevel& u, const std::string& g, const std::vector<OutSpan>& spans,
                   const std::vector<bool>& nested, bool& ok) {
    const std::u32string o = ToU32(before);
    const auto texts = NodeTexts(nodes.size(), spans);
    const auto newText = [&](uint32_t id) {
        const auto& r = texts[id];
        return r.first == std::string::npos ? std::string() : g.substr(r.first, r.second - r.first);
    };
    const auto oldText = [&](const Node& n) { return ToU8(o.substr(n.from, n.to - n.from)); };
    // the groups to write again: a final predicate, with a dependent noun fused onto it (것 + 이야 → 거야)
    struct Group {
        std::size_t first, last;
    };
    const auto levelOf = [&](uint32_t id) {
        for (const auto& r : reg)
            if (r.node == id) return r.level;
        return Level::none;
    };
    const auto plain = [](Level l) { return l == Level::haera || l == Level::hae; };
    std::vector<Group> groups;
    for (std::size_t q = 0; q < nodes.size(); ++q) {
        const Node& n = nodes[q];
        if (n.kind != Kind::V || n.ctype != CType::final_ || nested[q] || n.to <= n.from) continue;
        if (newText(n.id) == oldText(n)) continue;
        const Level target = levelOf(n.id);
        // a final family (-ㄴ걸, -지, -네) read at a level that is not polite keeps its form in 해체 and 해라체: a
        // change of level there is none, and a form such as 온걸 may be read at another level than it has (하오체)
        const bool polite = n.was == Level::haeyo || n.was == Level::hapsyo;
        if (!n.eomi.empty() && n.eomi == n.wasEomi && n.was != Level::none && !polite && plain(target)) continue;
        // REG-10: a predicate with -시- for the listener (주세요, 계십니까) is never lowered
        if ((n.was == Level::haeyo || n.was == Level::hapsyo) && plain(target)) {
            if (ContainsAny(oldText(n), t.G("listener.hon"))) {
                ok = false;
                return before;
            }
        }
        // a sentence end right before another word (개요 정도는, 역시다 엘 군) is not sure to be one: the line stays;
        // the same predicate said again (아팠다 아팠다) is
        if (q + 1 < nodes.size() && nodes[q + 1].kind != Kind::Punct &&
            !(nodes[q + 1].kind == Kind::V && nodes[q + 1].lemma == n.lemma)) {
            ok = false;
            return before;
        }
        // with the words written into the same text (강해 + 진다) and a noun the copula fuses with (것 + 이다)
        std::size_t first = q;
        while (first > 0) {
            const Node& p = nodes[first - 1];
            if (p.kind == Kind::Punct || p.to <= p.from) break;
            bool fuses = false;
            if (p.kind == Kind::N && p.to == n.from)
                for (const auto& sr : t.spoken)
                    if (sr.host == p.lemma && sr.next == "COP") fuses = true;
            if (p.to <= n.from && !fuses) break;
            --first;
        }
        groups.push_back({first, q});
    }
    if (groups.empty()) return before;
    // the same readings at the old levels must give back the old text
    std::vector<Reg> old = reg;
    std::vector<Node> oldNodes = nodes;
    for (const Group& gr : groups) {
        for (auto& r : old)
            if (r.node == nodes[gr.last].id) {
                r.level = nodes[gr.last].was;
                r.hon = false;
            }
        oldNodes[gr.last].eomi = nodes[gr.last].wasEomi;
    }
    std::vector<OutSpan> oldSpans;
    const std::string og = Generate(t, oldNodes, old, u, &oldSpans);
    const auto oldTexts = NodeTexts(nodes.size(), oldSpans);
    // the word stage may have written a form the generator fuses in speech (것이다 for 거다): the old text is
    // also given back without spoken fusions
    UttLevel written = u;
    written.narration = true;
    std::vector<OutSpan> writtenSpans;
    std::string wg;
    std::vector<std::pair<std::size_t, std::size_t>> writtenTexts;
    const auto regenAt = [&](const auto& tx, const std::string& text, std::size_t a, std::size_t b) {
        std::size_t begin = std::string::npos, end = 0;
        for (std::size_t k = a; k <= b; ++k) {
            const auto& r = tx[nodes[k].id];
            if (r.first == std::string::npos) continue;
            begin = std::min(begin, r.first);
            end = std::max(end, r.second);
        }
        return begin == std::string::npos ? std::string() : text.substr(begin, end - begin);
    };
    std::u32string result = o;
    for (auto gr = groups.rbegin(); gr != groups.rend(); ++gr) {
        const uint32_t from = nodes[gr->first].from, to = nodes[gr->last].to;
        const std::string was = ToU8(o.substr(from, to - from));
        // the past written out (보았습니다) is given back in the generator's contracted spelling (봤습니다)
        const std::string wasContracted = ContractPast(was);
        if (regenAt(oldTexts, og, gr->first, gr->last) != was && regenAt(oldTexts, og, gr->first, gr->last) != wasContracted) {
            if (wg.empty()) {
                wg = Generate(t, oldNodes, old, written, &writtenSpans);
                writtenTexts = NodeTexts(nodes.size(), writtenSpans);
            }
            if (regenAt(writtenTexts, wg, gr->first, gr->last) != was && regenAt(writtenTexts, wg, gr->first, gr->last) != wasContracted) {
                ok = false;
                return before;
            }
        }
        result.replace(from, to - from, ToU32(regenAt(texts, g, gr->first, gr->last)));
    }
    return ToU8(result);
}

// The readings of a thread's calls, kept between calls (as bounded as within one), so that a caller sending
// one line per call (a text hook) does not conjugate the same lemmas again for each line. They are kept by
// the load of the tables, not by their address alone: tables loaded again may take the address of tables let
// go, which the readings kept point into. Tables of no load (loadId 0) get new readings for each call.
Words& ThreadWords(const Data& d) {
    thread_local std::unique_ptr<Words> words;
    thread_local std::uint64_t load = 0;
    if (!words || d.loadId == 0 || load != d.loadId || &words->t != &d.ko || &words->rw != &d.retro) {
        words = std::make_unique<Words>(Words{d.ko, d.retro, {}, {}});
        load = d.loadId;
    }
    return *words;
}

}  // namespace

struct Regenerator::State {
    Words& r;
    std::vector<RegDecision> decided;  // the decisions made here, when the options give none for the lines
    const std::vector<RegDecision>* decisions = nullptr;
};

Regenerator::Regenerator(const Data& d, const std::vector<RegLine>& lines, const RegenOptions& options)
    : d_(d), options_(options), state_(std::make_unique<State>(State{ThreadWords(d), {}, nullptr})) {
    state_->decisions = options.decisions;
    if (state_->decisions == nullptr || state_->decisions->size() != lines.size()) {
        state_->decided = DecideLevels(d.levels, lines);
        state_->decisions = &state_->decided;
    }
}

Regenerator::~Regenerator() = default;

RegenLine Regenerator::Line(std::size_t li, const PreLine& line) {
    const Data& d = d_;
    const RegenOptions& options = options_;
    const Tables& t = d.ko;
    Words& r = state_->r;
    const std::vector<RegDecision>& decisions = *state_->decisions;
    RegenLine out;
    {
        std::vector<Node> nodes;
        bool lineOk = BuildAligned(r, line, nodes);
        SplitNounCopula(t, nodes);
        {
            const RegDecision& d0 = decisions[li];
            if (options.levels && !d0.narration && (d0.level == Level::haeyo || d0.level == Level::hapsyo) && !SourceSentences(line.ja).empty())
                ReadHapsyoFinals(r, t, nodes);
        }
        uint32_t id = 0;
        for (auto& n : nodes) {
            n.id = ++id;
            n.wasEomi = n.eomi;
        }
        // REG-12: a command read where none can be leaves the line as it is: an adjective has no imperative
        // (진지하게 is the adverb), and a command right before another predicate (선택하지 말았으면 좋겠다) is none
        for (std::size_t q = 0; q < nodes.size(); ++q) {
            const Node& n = nodes[q];
            if (n.kind != Kind::V || n.ctype != CType::final_ || n.mood != Mood::imp) continue;
            const std::string pos = !n.vpos.empty() ? n.vpos : LookupPredicate(t, n.lemma).pos;
            // a negated command is a prohibition (-지 마), never -지 않게: 하시지 않게 is the connective
            const bool negated = n.neg == Neg::jiAnh;
            if (pos == "A" || negated || (q + 1 < nodes.size() && nodes[q + 1].kind == Kind::V)) lineOk = false;
        }
        const RegDecision& dec = decisions[li];
        // REG-1: narration after a quote, or a quote left open, keeps the line as the word stage wrote it
        if (options.levels && (dec.narration || OpenQuote(line.ja))) lineOk = false;
        // REG-15: a line of several quotes has a level per quote; quoteOf maps a node to its quote
        std::vector<std::size_t> quoteOf(nodes.size(), 0);
        std::vector<bool> nested(nodes.size(), false);  // inside a quote within the quote: another voice
        {
            int depth = 0;
            std::size_t k = 0;
            bool opened = false;
            for (std::size_t q = 0; q < nodes.size(); ++q) {
                if (nodes[q].kind == Kind::Punct && nodes[q].punct == Punct::qopen) {
                    if (depth == 0 && opened) ++k;
                    opened = true;
                    ++depth;
                }
                quoteOf[q] = k;
                nested[q] = depth >= 2 || (dec.holdQuote && depth >= 1);
                if (nodes[q].kind == Kind::Punct && nodes[q].punct == Punct::qclose && depth > 0) --depth;
            }
        }
        // per quote only when the levels were decided per quote and the translation has as many quotes;
        // when the numbers differ the levels cannot be placed and the line stays
        const bool perQuote = dec.quotes.size() >= 2 && !nodes.empty() && quoteOf.back() + 1 == dec.quotes.size();
        if (dec.quotes.size() >= 2 && !perQuote) lineOk = false;
        if (!perQuote) std::fill(quoteOf.begin(), quoteOf.end(), 0);
        // REG-12: -ㄹ까 before 하다 (추가할까 하고) is the construction "think of", not a sentence end; -다니 right
        // before a noun quotes what the noun says (얻게 되다니 소문)
        const auto thinkOf = [&](std::size_t q) {
            if (nodes[q].kind != Kind::V || nodes[q].ctype != CType::final_ || q + 1 >= nodes.size()) return false;
            const Node& nx = nodes[q + 1];
            if (nodes[q].eomi == "dani") return nx.kind == Kind::N || nx.kind == Kind::Raw;
            if (nodes[q].eomi != "lkka") return false;
            if (nx.kind != Kind::V && nx.kind != Kind::N && nx.kind != Kind::Raw) return false;
            for (const char* h : {"하", "합", "한", "했", "해"})
                if (nx.lemma.starts_with(h)) return true;
            return false;
        };
        const auto quoteAt = [&](std::size_t q) -> const RegDecision::Quote& {
            return dec.quotes[std::min(quoteOf[q], dec.quotes.size() - 1)];
        };
        // the last sentence end of each quote (a final V node, not nested, not -ㄹ까 하다, not before a comma)
        std::vector<std::size_t> lastEnd(nodes.empty() ? 1 : quoteOf.back() + 1, nodes.size());
        std::vector<bool> lowered(nodes.size(), false);  // REG-25: the sentence ends lowered to 해요체
        std::vector<bool> tailEnd(nodes.size(), false);  // REG-25: of them, a last predicate before an afterthought (즐거웠어요, 정말)
        for (std::size_t q = 0; q < nodes.size(); ++q) {
            const bool comma = q + 2 < nodes.size() && nodes[q + 1].kind == Kind::Punct && nodes[q + 1].punct == Punct::comma;
            if (nodes[q].kind == Kind::V && nodes[q].ctype == CType::final_ && !nested[q] && !thinkOf(q) && !comma) lastEnd[quoteOf[q]] = q;
        }
        std::vector<Reg> reg;
        UttLevel u;
        u.narration = dec.narration;
        if (options.levels) {
            u.level = u.narration ? Level::haera : dec.level;
            u.persona = dec.persona;
            reg = AssignRegister(d.levels, t, nodes, dec);
            if (u.narration)
                for (auto& g : reg) g.level = Level::haera;
            if (perQuote && !u.narration)
                for (auto& g : reg)
                    for (std::size_t q = 0; q < nodes.size(); ++q)
                        if (nodes[q].id == g.node) {
                            g.level = quoteAt(q).level;
                            if (g.level != Level::haeyo && g.level != Level::hapsyo) g.hon = false;
                        }
            // REG-25: inside one quote, a sentence the source ends in です or ます (REG-3) is 해요체 where the word stage
            // wrote 합쇼체 (기쁩니다 → 기뻐요). Keigo, a set greeting, old or stiff speech and a source.address cue set the
            // level of the whole quote (REG-4, REG-10): such a quote stays as the word stage wrote it. The explanatory
            // んです of REG-23 is one such sentence.
            std::vector<bool> lower(nodes.size(), false);
            const bool politeLine = dec.level == Level::haeyo || dec.level == Level::hapsyo;
            // a message in 『』 (a screen, a sign, a letter) keeps its levels
            const std::u32string ja32 = ToU32(line.ja);
            const std::size_t firstQuote = ja32.find_first_of(U"「『");
            const bool message = firstQuote != std::u32string::npos && ja32[firstQuote] == U'『';
            std::vector<std::u32string> jaSents;
            if (!u.narration && !perQuote && !dec.keep && politeLine && !message)
                jaSents = PoliteSentences(d.levels, SourceSentences(line.ja));
            // per source sentence: whether it ends in です or ます, or does so before an afterthought after 、
            // (楽しかったです、本当に)
            std::vector<bool> pend(jaSents.size(), false), pendBeforeTail(jaSents.size(), false);
            std::vector<bool> firstPerson(jaSents.size(), false);
            bool quoteHeld = false;
            if (!jaSents.empty()) {
                const SentenceCues c = CuesOfSentence(d.levels, line.ja);
                quoteHeld = line.keigo || c.set || c.keigo || c.arch || c.stiff || Addressed(line.ja, t);
            }
            for (std::size_t s = 0; s < jaSents.size() && !quoteHeld; ++s) {
                const SentenceCues c = CuesOfSentence(d.levels, "「" + ToU8(jaSents[s]) + "」");
                // keigo, a set phrase, a report, old speech or a tic at any sentence end sets the level of the quote
                if (c.set || c.keigo || c.arch || c.stiff) quoteHeld = true;
                pend[s] = c.pend;
                firstPerson[s] = c.first;
                const std::size_t comma = jaSents[s].find_last_of(U"、，");
                if (!c.pend && comma != std::u32string::npos && comma > 0)
                    pendBeforeTail[s] = CuesOfSentence(d.levels, "「" + ToU8(jaSents[s].substr(0, comma)) + "」").pend;
            }
            if (!jaSents.empty() && !quoteHeld) {
                // a run of …/‥ right after a polite sentence end ends a sentence too, as in the source (PoliteSentences)
                const auto boundary = [&](std::size_t k) {
                    if (nodes[k].kind != Kind::Punct || nested[k]) return false;
                    if (nodes[k].punct == Punct::end || nodes[k].punct == Punct::q || nodes[k].punct == Punct::excl) return true;
                    if (nodes[k].punct != Punct::ellipsis || k == 0) return false;
                    const Node& p = nodes[k - 1];
                    return p.kind == Kind::V && p.ctype == CType::final_ && (p.was == Level::hapsyo || p.was == Level::haeyo);
                };
                // the translation's sentences (a run of marks ends one) must be the source's, one for one
                // (a run of marks before any word, 「！？　…, ends none)
                std::size_t runs = 0;
                bool tail = false;
                std::vector<std::size_t> sentenceOf(nodes.size(), 0);
                for (std::size_t k = 0; k < nodes.size(); ++k) {
                    if (boundary(k)) {
                        if (tail) ++runs;
                        tail = false;
                    } else if (nodes[k].kind != Kind::Punct) {
                        tail = true;
                    }
                    sentenceOf[k] = runs;
                }
                // the speaker's will or an announcement keeps 합쇼체: a statement of an action verb in the present
                // (気をつけます 조심합니다, not 조심해요, which asks; the promise -ㄹ게요 is another reading), with -겠-
                // (頑張ります 노력하겠습니다), and a proposal (頑張りましょう 힘냅시다). The explanatory んです tells
                // circumstances, not a will (REG-23: 行くんです 가요), so its action verb is lowered.
                const std::vector<std::u32string> jaEnds = SourceSentenceEnds(line.ja);
                const auto willed = [&](std::size_t q) {
                    const Node& n = nodes[q];
                    if (n.mood == Mood::prop || (n.modal == "gess" && n.mood != Mood::q)) return true;
                    if (sentenceOf[q] < jaEnds.size() && ExplanatoryEnd(jaEnds[sentenceOf[q]])) return false;
                    const std::string pos = !n.vpos.empty() ? n.vpos : LookupPredicate(t, n.lemma).pos;
                    if (n.mood != Mood::decl || n.tense != Tense::nonpast || pos != "V" || n.kcls == "EXIST") return false;
                    // no will: a verb of state or cognition (모르다, 되다), an obligation (-아야 하다)
                    for (const auto& w : Split(t.G("lower.not-will"), '|'))
                        if (n.lemma == w) return false;
                    for (std::size_t k = q; k-- > 0;) {
                        if (nodes[k].kind == Kind::Punct) continue;
                        if (nodes[k].kind == Kind::V && nodes[k].ctype == CType::conn && EndsWithAny(nodes[k].eomi, t.G("lower.obligation")))
                            return false;
                        break;
                    }
                    // an event with a subject other than the speaker (雨が降ります 비가 와요)
                    const std::size_t s = sentenceOf[q];
                    return !(s < jaSents.size() && !firstPerson[s] && OtherSubject(jaSents[s]));
                };
                const auto keepEomi = [&](const std::string& e) {
                    const auto keep = Split(t.G("lower.keep-eomi"), '|');
                    return !e.empty() && std::find(keep.begin(), keep.end(), e) != keep.end();
                };
                if (runs + (tail ? 1 : 0) == jaSents.size()) {
                    const std::u32string out32 = ToU32(line.out);
                    for (std::size_t q = 0; q < nodes.size(); ++q) {
                        const Node& n = nodes[q];
                        if (n.kind != Kind::V || n.ctype != CType::final_ || nested[q] || thinkOf(q) || n.to <= n.from) continue;
                        bool end = true;  // the sentence's last word
                        bool laterV = false;  // a predicate after it in the sentence
                        for (std::size_t k = q + 1; k < nodes.size() && !boundary(k); ++k) {
                            if (nodes[k].kind != Kind::Punct) end = false;
                            if (nodes[k].kind == Kind::V) laterV = true;
                        }
                        // or the last predicate, an afterthought without one after a comma (즐거웠어요, 정말)
                        const bool afterthought = !end && !laterV && q + 1 < nodes.size() && nodes[q + 1].kind == Kind::Punct &&
                                                  nodes[q + 1].punct == Punct::comma && sentenceOf[q] < pend.size() &&
                                                  pendBeforeTail[sentenceOf[q]];
                        if (!(end || afterthought) || sentenceOf[q] >= pend.size() || !(pend[sentenceOf[q]] || afterthought) ||
                            n.was != Level::hapsyo)
                            continue;
                        // one level in a sentence: another word in it written in 합쇼체 (뛰어나고 있습니다만, …) keeps its end
                        bool other = false;
                        for (std::size_t k = 0; k < nodes.size(); ++k)
                            if (k != q && sentenceOf[k] == sentenceOf[q] && nodes[k].kind != Kind::Punct && nodes[k].to > nodes[k].from &&
                                HoldsHapsyo(out32.substr(nodes[k].from, nodes[k].to - nodes[k].from), t.G("mixed.hapsyo")))
                                other = true;
                        if (!other && !keepEomi(n.wasEomi) && !willed(q)) {
                            lower[q] = true;
                            if (afterthought) {
                                tailEnd[q] = true;
                                if (lastEnd[quoteOf[q]] >= nodes.size() || lastEnd[quoteOf[q]] < q) lastEnd[quoteOf[q]] = q;
                            }
                        }
                    }
                }
            }
            lowered = lower;
            // a predicate written with -시- for the listener keeps it (하십시오 → 하세요, 하시겠습니까 → 하실래요)
            for (auto& g : reg)
                for (std::size_t q = 0; q < nodes.size(); ++q)
                    if (lower[q] && nodes[q].id == g.node) {
                        g.level = Level::haeyo;
                        Node old = nodes[q];
                        old.eomi = old.wasEomi;
                        bool honIncl = false;
                        EndingForm(t, old, old.was, Persona::none, honIncl);
                        if (honIncl) g.hon = true;
                    }
            // a word kept with a level the reader could not take apart: changing the others would mix levels
            // (REG-12; a Hangul word kept literally counts too: 아닐까 합니다; a noun in 요 is no polite end)
            bool mixed = false;
            for (const auto& n : nodes) {
                if (n.kind != Kind::Raw && n.kind != Kind::N) continue;
                for (const char* key : {"mixed.hapsyo", n.kind == Kind::Raw ? "mixed.haeyo" : "mixed.word"})
                    if (EndsWithAny(n.lemma, t.G(key))) mixed = true;
            }
            // REG-12: where a quote's last sentence end is already at the quote's level, an inner sentence end takes that
            // level only where its source sentence ends in a negative (間に合わない。急げよ 늦어. 서둘러), read on the source's
            // sentence ends, one for one with the translation's when the counts agree; another plain -다 before it may be an
            // exclamation, a declaration or an authoritative statement that keeps it
            const std::vector<std::u32string> sourceEnds = !u.narration && !perQuote ? SourceSentenceEnds(line.ja) : std::vector<std::u32string>{};
            std::vector<std::size_t> sentenceAt(nodes.size(), 0);
            std::size_t sentences = 0;
            {
                bool tail = false;
                for (std::size_t k = 0; k < nodes.size(); ++k) {
                    const bool boundary = nodes[k].kind == Kind::Punct && !nested[k] &&
                                          (nodes[k].punct == Punct::end || nodes[k].punct == Punct::q || nodes[k].punct == Punct::excl);
                    if (boundary) {
                        if (tail) ++sentences;
                        tail = false;
                    } else if (nodes[k].kind != Kind::Punct) {
                        tail = true;
                    }
                    sentenceAt[k] = sentences;
                }
                if (tail) ++sentences;
            }
            const bool aligned = !sourceEnds.empty() && sentences == sourceEnds.size();
            const auto sourceEndIs = [&](std::size_t q, const char* key) {
                return aligned && sentenceAt[q] < sourceEnds.size() && EndsWithAny(ToU8(sourceEnds[sentenceAt[q]]), t.G(key));
            };
            const auto negativeSource = [&](std::size_t q) {
                return sourceEndIs(q, "source.inner-negative") && !sourceEndIs(q, "source.inner-negative-not") &&
                       !sourceEndIs(q, "source.inner-keep");
            };
            // only where the last sentence end is already at the quote's level: when it changes too, the whole quote
            // changes as before. A 합쇼체 end lowered by REG-25 counts only after a sentence end of the quote already
            // written in 해요체 (무리해요. … 많다. … 아니에요 → 많아요, REG-23); else the plain sentences before it keep
            // their ends and so the quote (바꾸어 본다. 그거면 된다고 생각합니다, not 바꾸어 봐요)
            const auto lastChanges = [&](std::size_t q) {
                const std::size_t last = lastEnd[quoteOf[q]];
                if (last >= nodes.size()) return false;
                if (lower[last]) {
                    for (std::size_t k = 0; k < last; ++k)
                        if (quoteOf[k] == quoteOf[last] && nodes[k].kind == Kind::V && nodes[k].ctype == CType::final_ &&
                            nodes[k].was == Level::haeyo)
                            return true;
                    return false;
                }
                for (const auto& g : reg)
                    if (g.node == nodes[last].id) return g.level != nodes[last].was;
                return false;
            };
            const auto innerKept = [&](std::size_t q) {
                const Node& n = nodes[q];
                if (n.ctype != CType::final_ || q == lastEnd[quoteOf[q]] || lastChanges(q)) return false;
                // the でしょう(ね) of a guess in a casual quote is the casual guess too (受け持っていたのでしょうね。 맡고 있었겠지)
                const bool guess = n.was == Level::haeyo && sourceEndIs(q, "source.casual-guess");
                return !negativeSource(q) && !guess;
            };
            for (std::size_t q = 0; q < nodes.size(); ++q) {
                if (nodes[q].kind != Kind::V) continue;
                const Level was = nodes[q].was;
                // a final ending inside the sentence (놀다니, 저희들…) keeps its level: no 요 mid-sentence
                const bool inner = !tailEnd[q] && nodes[q].ctype == CType::final_ && q + 2 < nodes.size() &&
                                   nodes[q + 1].kind == Kind::Punct && nodes[q + 1].punct == Punct::comma;
                // in a caption scene a plain command written as -어라 (来い → 와라) stays one
                const bool command = nodes[q].mood == Mood::imp && was == Level::haera;
                // REG-10: an honorific or humble source keeps the polite level (합쇼체, or 해요체 for ご覧になって)
                const bool keigo = line.keigo && (was == Level::hapsyo || was == Level::haeyo) && !lower[q];
                // REG-12: a question asked by its ending alone (아닙니까.): another level would stop asking
                bool askedByEnding = false;
                if (nodes[q].mood == Mood::q && nodes[q].ctype == CType::final_) {
                    askedByEnding = true;
                    for (std::size_t k = q + 1; k < nodes.size(); ++k) {
                        if (nodes[k].kind != Kind::Punct) break;
                        if (nodes[k].punct == Punct::q) askedByEnding = false;
                    }
                }
                // REG-10: 합쇼체 is never lowered, nor 해요체 raised to 합쇼체, but for the plain polite sentences of REG-25
                Level target = Level::none;
                for (const auto& g : reg)
                    if (g.node == nodes[q].id) target = g.level;
                const bool hapsyo = was == Level::hapsyo && !(lower[q] && target == Level::haeyo);
                const bool raise = was == Level::haeyo && target == Level::hapsyo;
                // REG-12: the source's last sentence is plain although the line has です/ます inside (そうですね、…
                // かもしれない): the plain end is not raised to a polite one
                const bool politeInside = dec.key == "polite-any" || dec.key == "quotes-polite-any";
                const bool raisePlain = politeInside && (was == Level::haera || was == Level::hae) &&
                                        (target == Level::haeyo || target == Level::hapsyo);
                // REG-11: the source commands with an imperative verb (見ろ, 追え): the 해라체 command stays (봐라)
                const bool jaCommand = line.command && nodes[q].mood == Mood::imp && was == Level::haera;
                // REG-12: the source trails off after a て-form (言われて……): the -어 is not a command to raise
                const bool trail = dec.trail && nodes[q].ctype == CType::final_ && nodes[q].mood == Mood::imp;
                // a polite clause trailing off after a conjunction (そうじゃないですけど……) keeps its form: no 요 after
                // a trailing conjunction
                bool lastWord = true;
                for (std::size_t k = q + 1; k < nodes.size(); ++k)
                    if (nodes[k].kind != Kind::Punct) lastWord = false;
                const bool trailing = dec.trailConj && lastWord;
                // REG-21: a declaration of one verb (出るっ, もう、イクよっ！) keeps -ㄴ다
                const bool declared = dec.declBang && lastWord && was == Level::haera && nodes[q].ctype == CType::final_ &&
                                      nodes[q].mood == Mood::decl && nodes[q].tense == Tense::nonpast && SingleVerb(nodes, q);
                const bool keep = (perQuote ? quoteAt(q).keep : dec.keep) || thinkOf(q) || nested[q] || trail || trailing ||
                                  declared || (innerKept(q) && !lower[q]);
                // REG-21: a plain 주다, or 보다 after a verb or for てみる, at the end (빌려 준다, 해 본다) has in 해체 the
                // form that asks (빌려 줘, 해 봐): the speaker's own will (てあげる, てみる, お前に貸してやる) is a promise, -ㄹ게;
                // any other keeps -다 (殺してやる: a threat, not 죽여 줄게)
                const bool auxSee = nodes[q].lemma == "보다" &&
                                    (dec.giveDecl || (q > 0 && nodes[q - 1].kind == Kind::V && nodes[q - 1].ctype == CType::conn));
                const bool give = (nodes[q].lemma == "주다" || auxSee) && nodes[q].ctype == CType::final_ &&
                                  nodes[q].mood == Mood::decl && nodes[q].tense == Tense::nonpast && was == Level::haera &&
                                  nodes[q].eomi.empty() && lastWord && target == Level::hae;
                const bool held = mixed || inner || keep || keigo || askedByEnding || hapsyo || raise || jaCommand ||
                                  (command && dec.key.starts_with("scene-speech")) || (give && !dec.giveDecl);
                if (!held && give) nodes[q].eomi = "lge";
                // REG-3: in a lady's polite quote (polite-fem) a plain end the source closes with かな muses to oneself:
                // it stays plain (売っても良いかな？ 팔아도 좋을까?)
                const bool musing = dec.key == "polite-fem" && sourceEndIs(q, "source.musing") &&
                                    (was == Level::haera || was == Level::hae);
                if (!held && (raisePlain || musing)) {
                    // the plain end stays plain, at the dialogue level of REG-2 (해), not raised to 해요
                    for (auto& g : reg)
                        if (g.node == nodes[q].id) {
                            g.level = Level::hae;
                            g.hon = false;
                        }
                    continue;
                }
                if (!held) continue;
                for (auto& g : reg)
                    if (g.node == nodes[q].id && was != Level::none) {
                        g.level = was;
                        g.hon = false;
                    }
            }
        } else {
            // the level each predicate's ending had
            for (const auto& n : nodes)
                if (n.kind == Kind::V) {
                    Reg g;
                    g.node = n.id;
                    g.level = n.was;
                    reg.push_back(g);
                }
            u.level = Level::haera;
        }
        // REG-12: where the last sentence end of a quote keeps its plain 해라체 (a declaration, -ㄴ가, a -라 command), the
        // sentence ends before it keep theirs too: one level for the quote
        if (options.levels && !dec.narration) {
            const std::size_t quotes = nodes.empty() ? 1 : quoteOf.back() + 1;
            for (std::size_t qi = 0; qi < quotes; ++qi) {
                const std::size_t last = qi < lastEnd.size() ? lastEnd[qi] : nodes.size();
                if (last == nodes.size()) continue;
                // a command in 해라 -라 (持ってけ 가져가라) is plain whatever level the reader gives it
                const bool command = nodes[last].eomi == "ra";
                if (nodes[last].was != Level::haera && !command) continue;
                Level lastNow = Level::none;
                for (const auto& g : reg)
                    if (g.node == nodes[last].id) lastNow = g.level;
                if (lastNow != Level::haera && !command) continue;
                for (std::size_t q = 0; q < last; ++q) {
                    if (quoteOf[q] != qi || nodes[q].kind != Kind::V || nodes[q].ctype != CType::final_ || nodes[q].was != Level::haera)
                        continue;
                    for (auto& g : reg)
                        if (g.node == nodes[q].id) {
                            g.level = Level::haera;
                            g.hon = false;
                        }
                }
            }
        }
        // REG-18: an exclamation with な on a predicate (寒いなぁ) ends in 해체 -네 (춥네): the last sentence end
        bool neChanged = false;
        if (options.levels && dec.exclaimNe && !dec.narration) {
            for (std::size_t q = nodes.size(); q-- > 0;) {
                if (nodes[q].kind != Kind::V || nodes[q].ctype != CType::final_ || nested[q] || thinkOf(q)) continue;
                Level now = Level::none;
                for (const auto& g : reg)
                    if (g.node == nodes[q].id) now = g.level;
                bool lastWord = true;
                for (std::size_t k = q + 1; k < nodes.size(); ++k)
                    if (nodes[k].kind != Kind::Punct) lastWord = false;
                if (now == Level::hae && nodes[q].mood == Mood::decl && nodes[q].eomi.empty() && lastWord) {
                    nodes[q].wasEomi = nodes[q].eomi;
                    nodes[q].eomi = "ne";
                    neChanged = true;
                }
                break;
            }
        }
        // the level of each sentence end before and after: whether the line changes level, and whether its
        // sentences would end at different levels (per quote when the line has several)
        bool levelChanged = neChanged, mixedLevels = false;
        const std::size_t quoteCount = nodes.empty() ? 1 : quoteOf.back() + 1;
        for (std::size_t qi = 0; qi < quoteCount; ++qi) {
            Level end = Level::none;
            // a sentence end of the quote (a V node, not a Raw word) changes level; REG-12: every sentence end of a quote
            // takes the quote's level, so an inner one that changes changes the quote too (늦는다. 서둘러 → 늦어. 서둘러)
            bool lastChanged = false;
            std::vector<Level> innerPolite;
            for (std::size_t q = 0; q < nodes.size(); ++q) {
                const Node& n = nodes[q];
                if (quoteOf[q] != qi) continue;
                if (n.kind != Kind::V || n.ctype != CType::final_ || thinkOf(q) || nested[q]) continue;
                Level now = Level::none;
                for (const auto& g : reg)
                    if (g.node == n.id) now = g.level;
                if (now == Level::none) continue;
                const bool inner = !tailEnd[q] && q + 2 < nodes.size() && nodes[q + 1].kind == Kind::Punct && nodes[q + 1].punct == Punct::comma;
                if (!inner && n.was != Level::none && n.was != now) lastChanged = true;
                if (inner) {
                    if (now == Level::haeyo || now == Level::hapsyo) innerPolite.push_back(now);
                } else if (end == Level::none) {
                    end = now;
                } else if (end != now) {
                    mixedLevels = true;
                }
            }
            for (const Level l : innerPolite)
                if (end != Level::none && l != end) mixedLevels = true;
            // REG-12: the quote's last word must be that sentence end; a later word (그래서?, 사실이야? 유필리아, a
            // word not taken apart, a connective) ends it at a level not known here
            // REG-25: but an afterthought of nouns after a lowered last predicate (즐거웠어요. 정말) is no sentence end
            const std::size_t lastV = qi < lastEnd.size() ? lastEnd[qi] : nodes.size();
            bool afterthought = lastV < nodes.size() && lowered[lastV];
            for (std::size_t k = lastV + 1; afterthought && k < nodes.size(); ++k)
                if (quoteOf[k] == qi && nodes[k].kind != Kind::Punct &&
                    ((nodes[k].kind != Kind::N && nodes[k].kind != Kind::P) || EndsWithAny(nodes[k].lemma, "다|까|요|죠")))
                    afterthought = false;
            for (std::size_t q = nodes.size(); q-- > 0 && !afterthought;) {
                if (quoteOf[q] != qi || nodes[q].kind == Kind::Punct) continue;
                if (nodes[q].kind != Kind::V || nodes[q].ctype != CType::final_ || nested[q] || thinkOf(q)) lastChanged = false;
                break;
            }
            levelChanged = levelChanged || lastChanged;
        }
        std::vector<OutSpan> spans;
        std::string g = Generate(t, nodes, reg, u, &spans);
        const std::string spliced = lineOk ? Splice(t, line.out, nodes, reg, u, g, spans, nested, lineOk) : line.out;
        // a stutter (ど、どういう) repeats the first syllable of the next word: \x01, 어떤 → 어, 어떤
        for (std::size_t pos = g.find('\x01'); pos != std::string::npos; pos = g.find('\x01')) {
            std::size_t q = pos + 1;
            while (q < g.size() && (g[q] == ',' || g[q] == ' ')) ++q;
            std::string syl;
            if (q < g.size()) {
                const std::u32string rest = ToU32(std::string_view(g).substr(q));
                if (!rest.empty() && hangul::IsSyllable(rest[0])) syl = ToU8(rest.substr(0, 1));
            }
            g.replace(pos, 1, syl);
        }
        std::size_t lead = 0;
        while (lead < line.out.size() && line.out[lead] == ' ') ++lead;
        g = line.out.substr(0, lead) + g;
        RegenLine& o = out;
        o.out = lineOk ? spliced : line.out;
        o.gen = g;
        o.ok = lineOk;
        o.levelChanged = levelChanged;
        o.mixedLevels = mixedLevels;
        o.spec = dec.spec;
        o.key = dec.key;
    }
    return out;
}

std::vector<RegenLine> Regenerate(const Data& d, const std::vector<PreLine>& lines, const std::vector<bool>& build,
                                  const RegenOptions& options) {
    std::vector<RegLine> rl;
    for (const PreLine& l : lines) rl.push_back({l.ja, l.speaker, l.scene, l.dialogue});
    Regenerator regen(d, rl, options);
    std::vector<RegenLine> out(lines.size());
    for (std::size_t li = 0; li < lines.size(); ++li)
        if (li < build.size() && build[li]) out[li] = regen.Line(li, lines[li]);
    return out;
}

}  // namespace hdTrans::reg
