#include "core/attach/lattice.h"

#include <algorithm>

#include "core/attach/utf8.h"

namespace hdTrans::attach::ana {

using internal::IsHiragana;
using internal::IsKanji;
using internal::IsKatakana;
using internal::IsSpace;
using internal::ToKatakana;

namespace {

bool IsKana(char32_t c) { return IsHiragana(c) || IsKatakana(c); }
bool IsLong(char32_t c) { return c == U'ー' || c == U'〜' || c == U'～'; }

// Vowel of a kana (a i u e o as 1..5), 0 if none. Small vowels count as their vowel.
int VowelChar(char32_t c) {
    switch (ToKatakana(c)) {
        case U'ア':
        case U'ァ': return 1;
        case U'イ':
        case U'ィ': return 2;
        case U'ウ':
        case U'ゥ': return 3;
        case U'エ':
        case U'ェ': return 4;
        case U'オ':
        case U'ォ': return 5;
        default: return 0;
    }
}

// Calls fn(len, group, pform) for every lexicon form matching a prefix of s[0..n).
template <class Fn>
void LookupLex(const Model& m, const char32_t* s, size_t n, Fn fn) {
    uint32_t node = 0;
    for (size_t k = 0; k < n; ++k) {
        node = m.trie.Child(node, s[k]);
        if (!node) return;
        const auto& nd = m.trie.nodes[node];
        for (uint32_t g = nd.first; g < nd.first + nd.count; ++g) {
            const auto& grp = m.groups[g];
            const auto& p = m.paradigms[grp.paradigm];
            for (uint32_t f = p.first; f < p.first + p.count; ++f) {
                const auto& pf = m.pforms[f];
                size_t end = k + 1 + pf.sufLen;
                if (end > n) continue;
                bool ok = true;
                for (uint32_t j = 0; j < pf.sufLen; ++j)
                    if (m.cps[pf.suf + j] != s[k + 1 + j]) {
                        ok = false;
                        break;
                    }
                if (ok) fn(end, g, f);
            }
        }
    }
}

template <class Fn>
void LookupLong(const Model& m, const char32_t* s, size_t n, Fn fn) {
    if (m.longs.empty()) return;
    uint32_t node = 0;
    for (size_t k = 0; k < n; ++k) {
        node = m.longTrie.Child(node, s[k]);
        if (!node) return;
        const auto& nd = m.longTrie.nodes[node];
        for (uint32_t i = nd.first; i < nd.first + nd.count; ++i) {
            const auto& l = m.longs[i];
            if (l.paradigm == UINT32_MAX) {
                fn(k + 1, i, UINT32_MAX);
                continue;
            }
            const auto& p = m.paradigms[l.paradigm];
            for (uint32_t f = p.first; f < p.first + p.count; ++f) {
                const auto& pf = m.pforms[f];
                size_t end = k + 1 + pf.sufLen;
                if (end > n) continue;
                bool ok = true;
                for (uint32_t j = 0; j < pf.sufLen && ok; ++j) ok = m.cps[pf.suf + j] == s[k + 1 + j];
                if (ok) fn(end, i, f);
            }
        }
    }
}

char32_t FirstReading(const Model& m, const Arc& a, const Workspace& ws) {
    if (a.kind != ArcKind::lex) return 0;
    const auto& g = m.groups[a.ref];
    if (g.readKind == 0) return ToKatakana(ws.core[a.b]);
    if (g.readKind == 1) {
        auto r = m.S(g.read);
        if (r.empty()) r = m.S(m.pforms[a.form].read);
        if (r.empty()) return 0;
        size_t i = 0;
        return internal::DecodeUtf8(r, i);
    }
    return 0;
}

bool CondHolds(const Model::Cond& c, const Workspace& ws, const Arc& a);
bool CondAll(const std::vector<Model::Cond>& cs, const Workspace& ws, uint32_t b, uint32_t e) {
    Arc a;
    a.b = static_cast<uint16_t>(b);
    a.e = static_cast<uint16_t>(e);
    for (auto& c : cs)
        if (!CondHolds(c, ws, a)) return false;
    return true;
}

bool CondHolds(const Model::Cond& c, const Workspace& ws, const Arc& a) {
    size_t n = ws.core.size();
    bool r = false;
    if (c.what == 0) {
        for (auto& alt : c.alts)
            if (alt.size() <= a.b && std::equal(alt.begin(), alt.end(), ws.core.begin() + (a.b - alt.size()))) r = true;
    } else if (c.what == 1) {
        for (auto& alt : c.alts)
            if (a.e + alt.size() <= n && std::equal(alt.begin(), alt.end(), ws.core.begin() + a.e)) r = true;
    } else if (c.what == 2) {
        r = (a.b == 0 || ws.spaceBefore[a.b] ? ccEnd : ws.cc[a.b - 1]) & c.mask;
    } else {
        r = (a.e >= n || ws.spaceBefore[a.e] ? ccEnd : ws.cc[a.e]) & c.mask;
    }
    return c.neg ? !r : r;
}

bool Selects(const Model& m, const Model::Constraint& c, const Workspace& ws, const Arc& a) {
    uint16_t cls = a.rcls;
    if (a.kind == ArcKind::lex) cls = m.pforms[a.form].cls;
    std::string_view lemma;
    if (a.kind == ArcKind::lex)
        lemma = m.S(m.groups[a.ref].lemma);
    else if (a.kind == ArcKind::longUnit)
        lemma = m.S(m.longs[a.ref].lemma);
    if (!c.lemma.empty() && lemma != c.lemma) return false;
    Pos pos = a.kind == ArcKind::longUnit ? m.longs[a.ref].pos : m.classPos[cls];
    if (c.pos >= 0 && static_cast<int>(pos) != c.pos) return false;
    const auto& k = m.classes[cls];
    if (!c.cform.empty() && m.cformNames[k.cform].compare(0, c.cform.size(), c.cform) != 0) return false;
    if (!c.ud.empty() && m.udNames[k.ud].compare(0, c.ud.size(), c.ud) != 0) return false;
    if (!c.surf.empty()) {
        auto s = internal::ToU32(c.surf);
        if (s.size() != static_cast<size_t>(a.e - a.b) || !std::equal(s.begin(), s.end(), ws.core.begin() + a.b))
            return false;
    }
    return true;
}

void IndexArcs(Workspace* ws) {
    size_t n = ws->core.size();
    auto& A = ws->arcs;
    ws->beginStart.assign(n + 2, 0);
    ws->endStart.assign(n + 2, 0);
    for (auto& a : A) {
        if (a.removedBy >= 0) continue;
        ++ws->beginStart[a.b + 1];
        ++ws->endStart[a.e + 1];
    }
    for (size_t i = 1; i < n + 2; ++i) {
        ws->beginStart[i] += ws->beginStart[i - 1];
        ws->endStart[i] += ws->endStart[i - 1];
    }
    ws->byBegin.assign(ws->beginStart[n + 1], 0);
    ws->byEnd.assign(ws->endStart[n + 1], 0);
    std::vector<uint32_t> bi(ws->beginStart.begin(), ws->beginStart.end() - 1);
    std::vector<uint32_t> ei(ws->endStart.begin(), ws->endStart.end() - 1);
    for (uint32_t i = 0; i < A.size(); ++i) {
        if (A[i].removedBy >= 0) continue;
        ws->byBegin[bi[A[i].b]++] = i;
        ws->byEnd[ei[A[i].e]++] = i;
    }
}

}  // namespace

void BuildLattice(const Model& m, Workspace* ws, const Options& opt) {
    auto& core = ws->core;
    core.clear();
    ws->coreN.clear();
    ws->arcs.clear();
    std::vector<uint8_t> sp;  // space run before core char: 0 none, 1 single space, 2 more
    uint8_t pending = 0;
    for (uint32_t i = 0; i < ws->nchars.size(); ++i) {
        char32_t c = ws->nchars[i].c;
        if (IsSpace(c)) {
            pending = pending ? 2 : 1;
            continue;
        }
        core.push_back(c);
        ws->coreN.push_back(i);
        sp.push_back(pending);
        pending = 0;
    }
    size_t n = core.size();
    ws->spaceBefore.assign(n + 1, 0);
    ws->joinable.assign(n + 1, 0);
    ws->cc.resize(n);
    for (size_t i = 0; i < n; ++i) {
        ws->spaceBefore[i] = sp[i] != 0 && i > 0;
        ws->cc[i] = CharClassOf(core[i]);
    }
    // spaced runs: single spaces between single kanji or kana (通 知 表)
    for (size_t i = 1; i < n; ++i) {
        if (sp[i] != 1) continue;
        // the character before the space stands alone, and so does the one after it or the one before that
        bool leftSingle = i == 1 || sp[i - 1] != 0;
        bool rightSingle = i + 1 >= n || sp[i + 1] != 0;
        bool runBefore = leftSingle && i >= 2 && sp[i - 1] == 1 && (i == 2 || sp[i - 2] != 0);
        auto jp = [&](size_t k) { return IsKanji(core[k]) || IsKana(core[k]); };
        if (leftSingle && (rightSingle || runBefore) && jp(i - 1) && jp(i)) ws->joinable[i] = 1;
    }
    ws->nextSpace.assign(n + 1, static_cast<uint32_t>(n + 1));
    for (size_t i = n; i-- > 0;)
        ws->nextSpace[i] = ws->spaceBefore[i + 1] ? static_cast<uint32_t>(i + 1) : ws->nextSpace[i + 1];

    // Checks an arc against spaces; returns false if it crosses a non-joinable one, else adds join costs.
    auto spaceOk = [&](Arc& a) {
        if (ws->nextSpace[a.b] >= a.e) return true;
        for (uint32_t k = ws->nextSpace[a.b]; k < a.e; k = ws->nextSpace[k]) {
            if (!ws->joinable[k]) return false;
            a.cost += m.joinCost;
            a.joined = true;
        }
        return true;
    };

    std::vector<uint8_t> hasLex(n + 1, 0);
    for (size_t i = 0; i < n; ++i) {
        LookupLex(m, core.data() + i, n - i, [&](size_t len, uint32_t g, uint32_t f) {
            Arc a;
            a.b = static_cast<uint16_t>(i);
            a.e = static_cast<uint16_t>(i + len);
            a.lcls = a.rcls = m.pforms[f].cls;
            a.cost = m.groups[g].cost;
            a.kind = ArcKind::lex;
            a.ref = g;
            a.form = f;
            if (!spaceOk(a)) return;
            hasLex[i] = 1;
            ws->arcs.push_back(a);
        });
        if (opt.longUnits)
            LookupLong(m, core.data() + i, n - i, [&](size_t len, uint32_t li, uint32_t f) {
                const auto& l = m.longs[li];
                Arc a;
                a.b = static_cast<uint16_t>(i);
                a.e = static_cast<uint16_t>(i + len);
                a.lcls = l.lcls;
                a.rcls = f == UINT32_MAX ? l.rcls : m.pforms[f].cls;
                a.form = f;
                a.cost = l.cost;
                a.kind = ArcKind::longUnit;
                a.ref = li;
                a.flags = LongUnit;
                if (!spaceOk(a)) return;
                ws->arcs.push_back(a);
            });
        // unknown words
        uint8_t ci = m.Primary(core[i]);
        const auto& cat = m.cats[ci];
        if (cat.invoke || !hasLex[i]) {
            uint32_t bit = 1u << ci;
            size_t run = 1;
            while (i + run < n && run < 24 && (m.Mask(core[i + run]) & bit) && !ws->spaceBefore[i + run]) ++run;
            auto addUnk = [&](size_t len) {
                for (uint32_t k = 0; k < cat.arcs.size(); ++k) {
                    const auto& u = cat.arcs[k];
                    Arc a;
                    a.b = static_cast<uint16_t>(i);
                    a.e = static_cast<uint16_t>(i + len);
                    a.lcls = a.rcls = u.cls;
                    a.cost = u.cost + u.perChar * static_cast<int64_t>(len);
                    a.kind = ArcKind::unk;
                    a.ref = (static_cast<uint32_t>(ci) << 8) | k;
                    a.flags = Unknown;
                    ws->arcs.push_back(a);
                }
            };
            if (cat.group) addUnk(run);
            for (size_t len = 1; len <= cat.length && len <= run; ++len)
                if (!(cat.group && len == run)) addUnk(len);
            if (!cat.group && cat.length == 0) addUnk(1);
        }
    }

    // derived arcs (ja/derive.tsv)
    if (!m.rules.empty() && n > 0) {
        struct Site {
            uint32_t p, len;  // replaced core range
            std::u32string_view to;
            int16_t rule;
            bool elong;
        };
        std::vector<Site> sites;
        for (int16_t ri = 0; ri < static_cast<int16_t>(m.rules.size()); ++ri) {
            const auto& r = m.rules[static_cast<size_t>(ri)];
            if (r.kind == 2) continue;
            if (r.kind == 0) {
                for (size_t p = 0; p + r.from.size() <= n; ++p) {
                    if (!std::equal(r.from.begin(), r.from.end(), core.begin() + p)) continue;
                    if (ws->nextSpace[p] < p + r.from.size()) continue;
                    if (!CondAll(r.cond, *ws, static_cast<uint32_t>(p), static_cast<uint32_t>(p + r.from.size())))
                        continue;
                    sites.push_back({static_cast<uint32_t>(p), static_cast<uint32_t>(r.from.size()), r.to, ri, false});
                }
            } else {
                // elongation: ー runs after hiragana, or runs of the same vowel letter, collapse to the first letter
                for (size_t p = 1; p < n; ++p) {
                    if (ws->spaceBefore[p]) continue;
                    size_t q = p;
                    if (IsLong(core[p]) && IsHiragana(core[p - 1])) {
                        while (q < n && IsLong(core[q]) && !ws->spaceBefore[q]) ++q;
                    } else if (VowelChar(core[p]) && VowelChar(core[p]) == VowelChar(core[p - 1]) &&
                               (p < 2 || VowelChar(core[p - 2]) != VowelChar(core[p]))) {
                        while (q < n && VowelChar(core[q]) == VowelChar(core[p]) && !ws->spaceBefore[q]) ++q;
                        if (q - p < 2) q = p;  // three or more letters: いい and ええ are words
                    }
                    if (q > p && !CondAll(r.cond, *ws, static_cast<uint32_t>(p), static_cast<uint32_t>(q))) q = p;
                    if (q > p) sites.push_back({static_cast<uint32_t>(p), static_cast<uint32_t>(q - p), {}, ri, true});
                    // a vowel run may also keep two letters, for words that end in a doubled vowel (かわいいいい → かわいい)
                    if (q > p + 1 && !IsLong(core[p]))
                        sites.push_back({static_cast<uint32_t>(p + 1), static_cast<uint32_t>(q - p - 1), {}, ri, true});
                }
            }
        }
        if (!sites.empty()) {
            size_t nSites = sites.size();
            for (size_t i = 0; i < n; ++i) {
                // sites usable from start i
                std::vector<size_t> near;
                for (size_t s = 0; s < nSites; ++s)
                    if (sites[s].p >= i + (sites[s].elong || sites[s].to.empty() ? 1 : 0) && sites[s].p < i + 10)
                        near.push_back(s);
                if (near.empty()) continue;
                std::vector<size_t> elongs, lits;
                for (size_t s : near) (sites[s].elong ? elongs : lits).push_back(s);
                // views: one elongation; one literal; one literal with one elongation (みてえええ). A derived
                // word holds one lengthening at most (まーまー is no まま).
                std::vector<std::vector<size_t>> views;
                for (size_t e : elongs) views.push_back({e});
                for (size_t s : lits) {
                    views.push_back({s});
                    for (size_t e : elongs) {
                        if (sites[e].p < sites[s].p + sites[s].len && sites[s].p < sites[e].p + sites[e].len) continue;
                        if (sites[e].p < sites[s].p)
                            views.push_back({e, s});
                        else
                            views.push_back({s, e});
                    }
                }
                size_t limit = std::min(n, i + 24);
                for (auto& v : views) {
                    auto& buf = ws->buf;
                    auto& map = ws->bufMap;
                    auto& der = ws->derived;
                    buf.clear();
                    map.clear();
                    der.clear();
                    size_t pos = i;
                    int64_t extra = 0;
                    int16_t lastRule = -1;
                    bool anyElong = false, anyLit = false;
                    size_t firstMod = SIZE_MAX;
                    bool firstIsDeletion = false, firstIsElong = false;
                    for (size_t s : v) {
                        const auto& st = sites[s];
                        if (st.p < pos) continue;
                        for (; pos < st.p; ++pos) {
                            buf.push_back(core[pos]);
                            map.push_back(static_cast<uint32_t>(pos));
                            der.push_back(0);
                        }
                        if (buf.size() < firstMod) {
                            firstMod = buf.size();
                            firstIsDeletion = st.to.empty();
                            firstIsElong = st.elong;
                        }
                        for (char32_t c : st.to) {
                            buf.push_back(c);
                            map.push_back(st.p);
                            der.push_back(1);
                        }
                        pos = st.p + st.len;
                        extra += m.deriveCost;
                        lastRule = st.rule;
                        (st.elong ? anyElong : anyLit) = true;
                    }
                    for (; pos < limit; ++pos) {
                        buf.push_back(core[pos]);
                        map.push_back(static_cast<uint32_t>(pos));
                        der.push_back(0);
                    }
                    map.push_back(static_cast<uint32_t>(pos));
                    LookupLex(m, buf.data(), buf.size(), [&](size_t len, uint32_t g, uint32_t f) {
                        // an elongation may end the arc; any other derivation must lie inside it
                        if (len < firstMod || (len == firstMod && !(firstIsDeletion && firstIsElong))) return;
                        if (len < buf.size() && der[len] && der[len - 1]) return;  // ends inside a replacement
                        if (len < 2) return;  // a one-letter result is never a derived word
                        Arc a;
                        a.b = static_cast<uint16_t>(i);
                        a.e = static_cast<uint16_t>(map[len]);
                        if (a.e <= a.b) return;
                        a.lcls = a.rcls = m.pforms[f].cls;
                        a.cost = m.groups[g].cost + extra;
                        a.kind = ArcKind::lex;
                        a.ref = g;
                        a.form = f;
                        a.rule = lastRule;
                        a.flags = static_cast<uint16_t>((anyLit ? Spoken : 0) | (anyElong ? Elongated : 0));
                        if (!spaceOk(a)) return;
                        ws->arcs.push_back(a);
                    });
                }
            }
        }
    }

    // stutters: a kana repeated as the first mora of the next word (べっ別に, わ、私)
    if (m.stutterCls && m.stutterRule >= 0) {
        size_t nArcs = ws->arcs.size();
        std::vector<uint32_t> firstAt(n + 1, UINT32_MAX);
        for (uint32_t a = 0; a < nArcs; ++a)
            if (ws->arcs[a].kind == ArcKind::lex && firstAt[ws->arcs[a].b] == UINT32_MAX) firstAt[ws->arcs[a].b] = a;
        for (size_t i = 0; i + 2 < n; ++i) {
            if (!IsKana(core[i])) continue;
            size_t len = 0;
            char32_t sep = core[i + 1];
            if (sep == U'っ' || sep == U'ッ')
                len = 2;
            else if (sep == U'、' || sep == U'…' || sep == U'‥')
                len = 1;
            if (!len) continue;
            size_t k = i + 2;
            bool match = false;
            for (uint32_t a = 0; a < nArcs && !match; ++a) {
                const Arc& x = ws->arcs[a];
                if (x.b != k || x.kind != ArcKind::lex || x.e - x.b < 1) continue;
                if (x.e - x.b == 1 && IsKana(core[k])) continue;
                if (FirstReading(m, x, *ws) == ToKatakana(core[i])) match = true;
            }
            if (!match) continue;
            Arc a;
            a.b = static_cast<uint16_t>(i);
            a.e = static_cast<uint16_t>(i + len);
            a.lcls = a.rcls = m.stutterCls;
            a.cost = m.deriveCost;
            a.kind = ArcKind::stutter;
            a.flags = Spoken;
            a.rule = m.stutterRule;
            if (spaceOk(a)) ws->arcs.push_back(a);
        }
    }

    // cut rows: the cost of a token boundary between two character classes goes to the arc that starts there
    if (!m.cuts.empty()) {
        std::vector<int64_t> cut(n + 1, 0);
        // positions inside a run of three or more letters with the same vowel (かわいいいい, すごーー)
        std::vector<uint8_t> inRun(n + 1, 0);
        for (size_t p = 0; p < n;) {
            size_t q = p + 1;
            int v = IsKana(core[p]) ? VowelChar(core[p]) : 0;
            while (v && q < n && !ws->spaceBefore[q] && (VowelChar(core[q]) == v || IsLong(core[q]))) ++q;
            if (v && q - p >= 3)
                for (size_t k = p + 1; k < q; ++k) inRun[k] = 1;
            p = q;
        }
        for (size_t p = 1; p < n; ++p) {
            if (ws->spaceBefore[p]) continue;
            for (auto& c : m.cuts) {
                if (c.left & ccVowelRun) {
                    if (inRun[p]) cut[p] += c.cost;
                    continue;
                }
                if ((ws->cc[p - 1] & c.left) && (ws->cc[p] & c.right)) cut[p] += c.cost;
            }
        }
        for (auto& a : ws->arcs) a.cost += cut[a.b];
    }

    // constraints remove arcs; a rule that would leave a start position without arcs is ignored there
    if (!m.constraints.empty()) {
        for (auto& a : ws->arcs)
            for (size_t ci = 0; ci < m.constraints.size(); ++ci) {
                const auto& c = m.constraints[ci];
                if (!Selects(m, c, *ws, a)) continue;
                bool when = true, unless = false;
                for (auto& x : c.when) when = when && CondHolds(x, *ws, a);
                if (!c.unless.empty()) {
                    unless = true;
                    for (auto& x : c.unless) unless = unless && CondHolds(x, *ws, a);
                }
                if (when && !unless) {
                    a.removedBy = static_cast<int16_t>(ci);
                    break;
                }
            }
        std::vector<uint8_t> alive(n + 1, 0);
        for (auto& a : ws->arcs)
            if (a.removedBy < 0) alive[a.b] = 1;
        for (auto& a : ws->arcs)
            if (a.removedBy >= 0 && !alive[a.b]) a.removedBy = -1;
    }
    IndexArcs(ws);
}

void RunViterbi(const Model& m, Workspace* ws, bool backward) {
    size_t n = ws->core.size();
    auto& A = ws->arcs;
    ws->path.clear();
    if (n == 0) {
        ws->best = 0;
        return;
    }
    for (size_t pos = 0; pos < n; ++pos) {
        for (uint32_t k = ws->beginStart[pos]; k < ws->beginStart[pos + 1]; ++k) {
            Arc& a = A[ws->byBegin[k]];
            int64_t best = kInf;
            int32_t bp = -1;
            if (pos == 0) {
                best = m.Conn(0, a.lcls);
            } else {
                for (uint32_t j = ws->endStart[pos]; j < ws->endStart[pos + 1]; ++j) {
                    const Arc& p = A[ws->byEnd[j]];
                    if (p.fwd >= kInf) continue;
                    int64_t v = p.fwd + m.Conn(p.rcls, a.lcls);
                    if (v < best) {
                        best = v;
                        bp = static_cast<int32_t>(ws->byEnd[j]);
                    }
                }
            }
            a.fwd = best >= kInf ? kInf : best + a.cost;
            a.bp = bp;
        }
    }
    int64_t best = kInf;
    int32_t last = -1;
    for (uint32_t j = ws->endStart[n]; j < ws->endStart[n + 1]; ++j) {
        const Arc& p = A[ws->byEnd[j]];
        if (p.fwd >= kInf) continue;
        int64_t v = p.fwd + m.Conn(p.rcls, 0);
        if (v < best) {
            best = v;
            last = static_cast<int32_t>(ws->byEnd[j]);
        }
    }
    ws->best = best;
    for (int32_t a = last; a >= 0; a = A[static_cast<size_t>(a)].bp) ws->path.push_back(static_cast<uint32_t>(a));
    std::reverse(ws->path.begin(), ws->path.end());
    if (!backward) return;
    for (size_t pos = n; pos-- > 0;) {
        for (uint32_t k = ws->beginStart[pos]; k < ws->beginStart[pos + 1]; ++k) {
            Arc& a = A[ws->byBegin[k]];
            int64_t bw = kInf;
            int32_t np = -1;
            if (a.e == n) {
                bw = m.Conn(a.rcls, 0);
            } else {
                for (uint32_t j = ws->beginStart[a.e]; j < ws->beginStart[a.e + 1]; ++j) {
                    const Arc& q = A[ws->byBegin[j]];
                    if (q.bwd >= kInf) continue;
                    int64_t v = m.Conn(a.rcls, q.lcls) + q.cost + q.bwd;
                    if (v < bw) {
                        bw = v;
                        np = static_cast<int32_t>(ws->byBegin[j]);
                    }
                }
            }
            a.bwd = bw;
            a.np = np;
        }
    }
}

void SubPath(const Model& m, const Workspace& wsr, uint16_t s, uint16_t t, uint16_t startCls, uint16_t endCls,
             std::vector<uint32_t>* out) {
    const Workspace* ws = &wsr;
    auto& A = ws->arcs;
    out->clear();
    std::vector<int64_t> f(A.size(), kInf);
    std::vector<int32_t> bp(A.size(), -1);
    for (size_t pos = s; pos < t; ++pos) {
        for (uint32_t k = ws->beginStart[pos]; k < ws->beginStart[pos + 1]; ++k) {
            uint32_t ai = ws->byBegin[k];
            const Arc& a = A[ai];
            if (a.e > t || a.kind == ArcKind::longUnit) continue;
            int64_t best = kInf;
            int32_t b = -1;
            if (pos == s) {
                best = m.Conn(startCls, a.lcls);
            } else {
                for (uint32_t j = ws->endStart[pos]; j < ws->endStart[pos + 1]; ++j) {
                    uint32_t pi = ws->byEnd[j];
                    if (f[pi] >= kInf || A[pi].b < s) continue;
                    int64_t v = f[pi] + m.Conn(A[pi].rcls, a.lcls);
                    if (v < best) {
                        best = v;
                        b = static_cast<int32_t>(pi);
                    }
                }
            }
            if (best < kInf) {
                f[ai] = best + a.cost;
                bp[ai] = b;
            }
        }
    }
    int64_t best = kInf;
    int32_t last = -1;
    for (uint32_t j = ws->endStart[t]; j < ws->endStart[t + 1]; ++j) {
        uint32_t pi = ws->byEnd[j];
        if (f[pi] >= kInf || A[pi].b < s) continue;
        int64_t v = f[pi] + m.Conn(A[pi].rcls, endCls);
        if (v < best) {
            best = v;
            last = static_cast<int32_t>(pi);
        }
    }
    for (int32_t a = last; a >= 0; a = bp[static_cast<size_t>(a)]) out->push_back(static_cast<uint32_t>(a));
    std::reverse(out->begin(), out->end());
}

}  // namespace hdTrans::attach::ana
