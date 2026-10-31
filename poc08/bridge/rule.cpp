#include "rule.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

#include "core/output/inflect.h"
#include "bridge.h"
#include "core/word/item.h"
#include "hd8gen/retro.h"
#include "ir/file.h"
#include "ir/json.h"
#include "ir/utf8.h"

namespace hd08::bridge {

namespace {

using hdTrans::output::internal::FindKoreanFamily;
using hdTrans::output::internal::InflectionClass;
using hdTrans::output::internal::KoreanFamily;
using hdTrans::output::internal::KoreanPartOfSpeech;

std::u32string U32(std::string_view s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) out.push_back(NextCodePoint(s, i));
    return out;
}

std::string U8(std::u32string_view s) {
    std::string out;
    for (const char32_t c : s) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

bool Hangul(char32_t c) { return c >= 0xAC00 && c <= 0xD7A3; }
bool AllHangul(std::u32string_view s) { return std::all_of(s.begin(), s.end(), Hangul); }
bool Kana(char32_t c) { return (c >= 0x3041 && c <= 0x3093) || (c >= 0x30A1 && c <= 0x30F6); }
// the semantic code of person nouns in the 0.7.x dictionaries (先生, 友達, 彼, 母)
constexpr std::string_view kPersonCode = "VHn";
// the semantic code of proper names (王 as a name, 日本人)
constexpr std::string_view kNameCode = "QPN";
// the part code of common nouns (N110); proper nouns are N130, pronouns N120
constexpr std::string_view kCommonNounCode = "110";

bool Space(char32_t c) { return c == ' ' || c == 0x3000 || c == '\t' || c == '\r' || c == '\n'; }

// One pre-IR item, written as the retrofit reads it.
struct PreItem {
    std::string k, t, p, m, lemma, pos, cls, row, rest, marker;
    bool bare = false, gap = false, stutter = false, glue = false, hasBare = false, person = false, thing = false;
};

}  // namespace

struct RuleData {
    std::map<std::u32string, std::string> punctJa;                        // mark → kind
    std::map<std::u32string, std::pair<std::string, std::u32string>> punctKo;  // Korean mark → kind, mark
    std::set<char32_t> sym;
    std::set<std::u32string> jaParticles, copulaNot, deps, glueLemmas, impEnds, partAlts;
    std::set<char32_t> impTail, impClose;
    char32_t stutterMark = 0;
    std::u32string copulaAlt, copulaLemma, lemmaEnd;
    std::vector<std::pair<char32_t, char32_t>> quotes, tags;
    std::set<char32_t> captionMarks;  // half-width marks of broadcast captions (｡ ｢)
    struct Keigo {
        std::string lemma, reading, next, notForm;  // reading: prefix; next: the lemmas that must follow; "" any
    };
    std::vector<Keigo> keigo;
    std::vector<std::pair<std::string, std::string>> keigoPrefix;  // 御 + verb (+ に) + 成る / 為る / 致す
    std::vector<std::string> commandForms;  // conjugation forms of a command (命令形: 見ろ, 追え, 捕らえろ)
    std::vector<std::string> rewrites;      // the retrofit's rewrite rules (W) that are on, each gated alone
    std::shared_ptr<const hd8gen::RetroSystem> retro;
};

bool KeigoSource(const RuleData& d, const std::vector<SourceToken>& toks) {
    for (size_t i = 0; i < toks.size(); i++) {
        const SourceToken& t = toks[i];
        for (const auto& k : d.keigo) {
            if (t.lemma != k.lemma) continue;
            if (!k.reading.empty() && t.reading.rfind(k.reading, 0) != 0) continue;
            if (!k.next.empty()) {  // one lemma, or several in a row separated by spaces (せる られる)
                size_t j = i + 1, a = 0;
                bool ok = true;
                while (ok && a <= k.next.size()) {
                    const size_t sp = k.next.find(' ', a);
                    const std::string want = k.next.substr(a, sp == std::string::npos ? std::string::npos : sp - a);
                    ok = j < toks.size() && toks[j].lemma == want;
                    j++;
                    if (sp == std::string::npos) break;
                    a = sp + 1;
                }
                if (!ok) continue;
            }
            if (!k.notForm.empty() && t.cform.find(k.notForm) != std::string::npos) continue;
            return true;
        }
        // prefix + verb stem, then に (お待ちになる) or not (お伺いする), then the row's verb
        if (!t.prefix || i + 2 >= toks.size() || !toks[i + 1].verb) continue;
        size_t v = i + 2;
        if (toks[v].lemma == "に" && v + 1 < toks.size()) v++;
        for (const auto& [pre, verb] : d.keigoPrefix)
            if (t.lemma == pre && toks[v].lemma == verb) return true;
    }
    return false;
}

namespace {

// (이)가: the alternation a particle notation starts with, and the rest; 0 when s does not start with one.
size_t PartPrefix(const RuleData& d, std::u32string_view s, std::u32string& alt, std::u32string& word) {
    if (s.size() < 3 || s[0] != U'(') return 0;
    const size_t close = s.find(U')');
    if (close == std::u32string_view::npos) return 0;
    alt = std::u32string(s.substr(1, close - 1));
    if (!d.partAlts.count(alt)) return 0;
    size_t e = close + 1;
    while (e < s.size() && Hangul(s[e])) e++;
    if (e == close + 1) return 0;
    word = std::u32string(s.substr(close + 1, e - close - 1));
    return e;
}

std::u32string Notation(std::u32string_view alt, std::u32string_view word) {
    return U"(" + std::u32string(alt) + U")" + std::u32string(word);
}

void PlainWords(const RuleData& d, std::u32string t, std::vector<PreItem>& out) {
    // glosses in parentheses next to Hangul are dropped: 사과(애플) → 사과
    for (size_t i = 0; i < t.size();) {
        if (t[i] != U'(') {
            i++;
            continue;
        }
        size_t close = i + 1;
        while (close < t.size() && (Hangul(t[close]) || t[close] == U' ')) close++;
        if (close >= t.size() || t[close] != U')' || close == i + 1) {
            i++;
            continue;
        }
        const bool before = i > 0 && Hangul(t[i - 1]);
        const bool after = close + 1 < t.size() && Hangul(t[close + 1]);
        // (이)가, (으)로 opening a word is a particle notation, not a gloss: dropping it left 가/로 as nouns
        // (것이 아니다 → 거이 아니다, 것으로 → 거으로)
        const bool particle = after && !before && d.partAlts.count(t.substr(i + 1, close - i - 1));
        if ((before || after) && !particle) t.erase(i, close - i + 1);
        else i++;
    }
    size_t start = 0;
    while (start <= t.size()) {
        size_t sp = t.find(U' ', start);
        if (sp == std::u32string::npos) sp = t.size();
        const std::u32string w = t.substr(start, sp - start);
        start = sp + 1;
        if (w.empty()) {
            if (sp == t.size()) break;
            continue;
        }
        std::u32string alt, word;
        PreItem it;
        if (PartPrefix(d, w, alt, word) == w.size()) {
            it.k = "P";
            it.t = U8(w);
        } else if (const auto pk = d.punctKo.find(w); pk != d.punctKo.end()) {
            it.k = "Punct";
            it.p = pk->second.first;
            it.m = U8(pk->second.second);
        } else if (AllHangul(w)) {
            it.k = "N";
            it.t = U8(w);
        } else {
            it.k = "Raw";
            it.t = U8(w);
        }
        out.push_back(it);
        if (sp == t.size()) break;
    }
}

const char* ClassName(InflectionClass c) {
    switch (c) {
    case InflectionClass::Regular: return "";
    case InflectionClass::Yeo: return "HA";
    case InflectionClass::Digeut: return "D";
    case InflectionClass::Bieup: return "B";
    case InflectionClass::Siot: return "S";
    case InflectionClass::Hieuh: return "H";
    case InflectionClass::Reu: return "REU";
    case InflectionClass::Reo: return "REO";
    case InflectionClass::U: return "U";
    case InflectionClass::Eu: return "EU";
    case InflectionClass::Rieul: return "L";
    case InflectionClass::Existential: return "EXIST";
    case InflectionClass::Copula: return "COP";
    }
    return "";
}

// 0.7.x's target and ending of one word as items.
std::vector<PreItem> ItemsOfTarget(const RuleData& d, const std::u32string& t, const std::u32string& ending,
                                   const std::u32string& jsurf) {
    std::vector<PreItem> out;
    if (t.empty() || t == U" ") return out;
    if (const auto pk = d.punctKo.find(t); pk != d.punctKo.end() && ending.empty()) {
        PreItem it;
        it.k = "Punct";
        it.p = pk->second.first;
        const auto pj = d.punctJa.find(jsurf);
        it.m = U8(pj != d.punctJa.end() ? jsurf : pk->second.second);
        out.push_back(it);
        return out;
    }
    // a marker family at the end: stem<&XX@>
    if (t.size() >= 4 && t.back() == U'@' && (t[t.size() - 4] == U'&' || t[t.size() - 4] == U'$' || t[t.size() - 4] == U'%')) {
        const std::string key = U8(t.substr(t.size() - 4));
        if (const KoreanFamily* f = FindKoreanFamily(key)) {
            const std::u32string prefix = t.substr(0, t.size() - 4);
            std::vector<std::u32string> words;
            for (size_t a = 0;;) {
                const size_t sp = prefix.find(U' ', a);
                words.push_back(prefix.substr(a, sp == std::u32string::npos ? std::u32string::npos : sp - a));
                if (sp == std::u32string::npos) break;
                a = sp + 1;
            }
            std::u32string head = words.back();
            for (size_t w = 0; w + 1 < words.size(); w++) PlainWords(d, words[w], out);
            std::u32string alt, word;
            if (const size_t e = PartPrefix(d, head, alt, word)) {
                PreItem p;
                p.k = "P";
                p.t = U8(Notation(alt, word));
                out.push_back(p);
                head = head.substr(e);
            }
            const std::u32string stem = U32(f->stem);
            if (stem.find(U'(') != std::u32string::npos || !AllHangul(stem + head)) {
                PreItem r;
                r.k = "Raw";
                r.t = U8(t);
                out.push_back(r);
                return out;
            }
            std::string pos = f->partOfSpeech == KoreanPartOfSpeech::Verb ? "V" : "A";
            if (f->inflectionClass == InflectionClass::Existential) pos = "X";
            std::string cls = ClassName(f->inflectionClass);
            if (cls == "B" && (f->flags & hdTrans::output::internal::kBieupWa)) cls = "BWA";
            std::u32string lemma = head + stem + d.lemmaEnd;
            if (cls == "COP") {
                if (!head.empty()) {
                    PreItem n;
                    n.k = "N";
                    n.t = U8(head);
                    out.push_back(n);
                }
                lemma = d.copulaLemma;
                pos = "C";
            }
            PreItem v;
            v.k = "V";
            v.lemma = U8(lemma);
            v.pos = pos;
            v.cls = cls;
            v.row = ending.empty() ? "1" : U8(ending.substr(0, 1));
            v.rest = ending.empty() ? "" : U8(ending.substr(1));
            v.marker = U8(t.substr(t.size() - 4, 1));
            v.bare = (f->flags & hdTrans::output::internal::kBareDeclarative) != 0;
            v.hasBare = true;
            out.push_back(v);
            return out;
        }
    }
    // copula notations: (이)다, (이)야, (이었)였지만 — not the particles (이)가, (이)나 …
    if (t.size() >= 4 && t[0] == U'(' && !d.copulaNot.count(t) && ending.empty()) {
        const size_t close = t.find(U')');
        if (close != std::u32string::npos && close + 1 < t.size() && t.compare(1, d.copulaAlt.size(), d.copulaAlt) == 0) {
            const std::u32string inside = t.substr(1, close - 1);
            const std::u32string vowel = t.substr(close + 1);
            if (AllHangul(inside) && AllHangul(vowel)) {
                const std::u32string cons = inside.size() == 1 ? inside + vowel : inside + vowel.substr(1);
                PreItem v;
                v.k = "V";
                v.lemma = U8(d.copulaLemma);
                v.pos = "C";
                v.cls = "COP";
                v.row = "1";
                v.rest = U8(cons.substr(1));
                v.marker = "&";
                v.hasBare = true;
                out.push_back(v);
                return out;
            }
        }
    }
    std::u32string alt, word;
    if (const size_t e = PartPrefix(d, t, alt, word)) {
        PreItem p;
        p.k = "P";
        p.t = U8(Notation(alt, word));
        out.push_back(p);
        std::u32string rest = t.substr(e);
        while (!rest.empty() && rest.front() == U' ') rest.erase(0, 1);
        while (!rest.empty() && rest.back() == U' ') rest.pop_back();
        if (!rest.empty()) PlainWords(d, rest, out);
        return out;
    }
    if (!ending.empty()) {
        PreItem r;
        r.k = "Raw";
        r.t = U8(t + ending);
        out.push_back(r);
        return out;
    }
    if (d.jaParticles.count(jsurf) && t.size() <= 4 && AllHangul(t)) {
        PreItem p;
        p.k = "P";
        p.t = U8(t);
        out.push_back(p);
        return out;
    }
    PlainWords(d, t, out);
    return out;
}

struct Row {
    int sent = 0, seg = 0;
    std::u32string surf, target, ending;
    bool person = false, thing = false;
};

std::vector<PreItem> ConvertLine(const RuleData& d, const std::u32string& ja, const std::vector<Row>& rows) {
    // pass 1: source positions of the items whose surface is in the line
    size_t cursor = 0;
    std::vector<long> pos;
    for (const Row& r : rows) {
        long p = -1;
        if (!r.surf.empty()) {
            const size_t f = ja.find(r.surf, cursor);
            if (f != std::u32string::npos && f - cursor <= 60) p = static_cast<long>(f);
        }
        pos.push_back(p);
        if (p >= 0) cursor = static_cast<size_t>(p) + r.surf.size();
    }
    std::vector<std::pair<size_t, size_t>> covered;
    for (size_t n = 0; n < rows.size(); n++)
        if (pos[n] >= 0) covered.push_back({static_cast<size_t>(pos[n]), static_cast<size_t>(pos[n]) + rows[n].surf.size()});
    auto cov = [&](size_t i) {
        return std::any_of(covered.begin(), covered.end(), [&](const auto& c) { return c.first <= i && i < c.second; });
    };
    // uncovered marks, symbols and stutters in source order
    std::vector<std::pair<size_t, std::vector<PreItem>>> runs;
    for (size_t i = 0; i < ja.size(); i++) {
        if (cov(i)) continue;
        const char32_t c = ja[i];
        if (const auto pj = d.punctJa.find(std::u32string(1, c)); pj != d.punctJa.end()) {
            PreItem it;
            it.k = "Punct";
            it.p = pj->second;
            it.m = U8(std::u32string(1, c));
            it.gap = true;
            runs.push_back({i, {it}});
            continue;
        }
        if (d.sym.count(c)) {
            PreItem it;
            it.k = "Raw";
            it.t = U8(std::u32string(1, c));
            it.gap = true;
            runs.push_back({i, {it}});
            continue;
        }
        if (Kana(c) && i + 2 < ja.size() && ja[i + 1] == d.stutterMark && ja[i + 2] == c) {
            PreItem it;
            it.k = "Raw";
            it.t = "\x01";
            it.stutter = true;
            runs.push_back({i, {it}});
        }
    }
    std::vector<PreItem> out;
    size_t ri = 0;
    const Row* prev = nullptr;
    for (size_t n = 0; n < rows.size(); n++) {
        const Row& r = rows[n];
        const long p = pos[n];
        if (p >= 0) {
            while (ri < runs.size() && runs[ri].first < static_cast<size_t>(p)) {
                for (auto& it : runs[ri].second) out.push_back(it);
                ri++;
            }
        } else if (prev != nullptr && (r.seg > prev->seg + 1 || r.sent != prev->sent)) {
            long next = -1;
            for (size_t j = n + 1; j < rows.size(); j++)
                if (pos[j] >= 0) {
                    next = pos[j];
                    break;
                }
            if (ri < runs.size() && (next < 0 || runs[ri].first < static_cast<size_t>(next))) {
                for (auto& it : runs[ri].second) out.push_back(it);
                ri++;
            }
        }
        std::vector<PreItem> its = ItemsOfTarget(d, r.target, r.ending, r.surf);
        // a target of several nouns (모리시타 씨) is one word: the class goes on its last noun
        for (size_t q = its.size(); q-- > 0;)
            if (its[q].k == "N") {
                its[q].person = r.person;
                its[q].thing = r.thing;
                break;
            }
        if (p < 0) {
            // a mark the chain wrote itself (? for ？): it stands for the next uncovered mark of its kind
            for (PreItem& it : its) {
                if (it.k != "Punct") continue;
                for (size_t j = ri; j < runs.size(); j++) {
                    auto& rr = runs[j].second;
                    if (rr.size() == 1 && rr[0].k == "Punct" && rr[0].p == it.p) {
                        it.m = rr[0].m;
                        runs.erase(runs.begin() + static_cast<long>(j));
                        break;
                    }
                }
            }
        }
        if (d.punctJa.count(r.surf))
            for (PreItem& it : its)
                if (it.k == "Punct") it.m = U8(r.surf);
        for (auto& it : its) out.push_back(it);
        prev = &r;
    }
    while (ri < runs.size()) {
        for (auto& it : runs[ri].second) out.push_back(it);
        ri++;
    }
    return out;
}

void Glue(const RuleData& d, std::vector<PreItem>& items, const std::u32string& ctext) {
    size_t cur = 0;
    long prevEnd = -1;
    std::string prevKind;
    for (PreItem& it : items) {
        const std::string& k = it.k;
        if ((k == "N" || k == "Raw" || k == "P") && !it.t.empty()) {
            std::u32string t = U32(it.t);
            if (k == "P" && !t.empty() && t[0] == U'(') {
                const size_t close = t.find(U')');
                if (close != std::u32string::npos) t = t.substr(close + 1);
            }
            const size_t p = t.empty() ? std::u32string::npos : ctext.find(t, cur);
            if (p != std::u32string::npos && p - cur < 40) {
                if ((k == "N" || k == "Raw") && (prevKind == "N" || prevKind == "Raw") && static_cast<long>(p) == prevEnd &&
                    !d.deps.count(t))
                    it.glue = true;
                prevEnd = static_cast<long>(p + t.size());
                cur = static_cast<size_t>(prevEnd);
                prevKind = k;
                continue;
            }
        }
        if (k == "V" && (prevKind == "N" || prevKind == "Raw") && prevEnd >= 0 && static_cast<size_t>(prevEnd) < ctext.size() &&
            ctext[static_cast<size_t>(prevEnd)] != U' ' && d.glueLemmas.count(U32(it.lemma)))
            it.glue = true;
        prevKind = k != "Punct" ? k : std::string();
        prevEnd = -1;
    }
}

bool Imperative(const RuleData& d, const std::u32string& ja) {
    std::u32string s = ja;
    while (!s.empty() && Space(s.back())) s.pop_back();
    if (!s.empty() && d.impClose.count(s.back())) s.pop_back();
    while (!s.empty() && (d.impTail.count(s.back()) || Space(s.back()))) s.pop_back();
    for (const auto& e : d.impEnds)
        if (s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0) return true;
    return false;
}

void Field(std::string& o, const char* k, const std::string& v) {
    o += ",\"";
    o += k;
    o += "\":";
    AppendJsonString(o, v);
}

std::string ItemJson(const PreItem& it) {
    std::string o = "{\"k\":";
    AppendJsonString(o, it.k);
    if (!it.t.empty() || it.k == "N" || it.k == "Raw" || it.k == "P") Field(o, "t", it.t);
    if (it.person) o += ",\"person\":true";
    if (it.thing) o += ",\"thing\":true";
    if (!it.p.empty()) Field(o, "p", it.p);
    if (it.k == "Punct") Field(o, "m", it.m);
    if (it.k == "V") {
        Field(o, "lemma", it.lemma);
        Field(o, "pos", it.pos);
        Field(o, "cls", it.cls);
        Field(o, "row", it.row);
        Field(o, "rest", it.rest);
        Field(o, "marker", it.marker);
        o += std::string(",\"bare\":") + (it.bare ? "true" : "false");
    }
    if (it.gap) o += ",\"gap\":1";
    if (it.stutter) o += ",\"stutter\":1";
    if (it.glue) o += ",\"glue\":1";
    o += '}';
    return o;
}

// The text after an optional speaker tag ((name), 【name】, name「) and white space.
std::u32string AfterTag(const RuleData& d, std::u32string s) {
    while (!s.empty() && Space(s.front())) s.erase(0, 1);
    for (const auto& [open, close] : d.tags) {
        if (s.empty() || s.front() != open) continue;
        const size_t e = s.find(close);
        if (e != std::u32string::npos && e <= 24) {
            s = s.substr(e + 1);
            while (!s.empty() && Space(s.front())) s.erase(0, 1);
        }
        break;
    }
    return s;
}

std::vector<std::u32string> Words(const std::u32string& s) {
    std::vector<std::u32string> w;
    std::u32string cur;
    for (char32_t c : s) {
        if (c == U' ') {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

std::u32string Join(const std::vector<std::u32string>& w, size_t a, size_t b) {
    std::u32string o;
    for (size_t k = a; k < b; k++) {
        if (!o.empty()) o += U' ';
        o += w[k];
    }
    return o;
}

// Carries what the rewrite rules changed (r0 → r1, the retrofit's line without and with them) onto 0.7.x's own
// text o: each changed run of words of r0 is looked up in o with spaces ignored (it must start and end on word
// boundaries and occur once) and replaced by r1's words. False when a run is not found, so the line keeps 0.7.x's
// text: the retrofit's other differences from 0.7.x (its own spacing, a word it reads differently) never move.
bool Transplant(const std::string& o8, const std::string& r08, const std::string& r18, std::string& out) {
    const std::u32string o = U32(o8);
    const std::vector<std::u32string> w0 = Words(U32(r08)), w1 = Words(U32(r18));
    const size_t n = w0.size(), m = w1.size();
    if (n > 400 || m > 400) return false;
    // longest common subsequence of words
    std::vector<std::vector<uint16_t>> L(n + 1, std::vector<uint16_t>(m + 1, 0));
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            L[i][j] = w0[i] == w1[j] ? static_cast<uint16_t>(L[i + 1][j + 1] + 1) : std::max(L[i + 1][j], L[i][j + 1]);
    struct Hunk {
        size_t a0, a1, b0, b1;
    };
    std::vector<Hunk> hunks;
    size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && w0[i] == w1[j]) {
            i++, j++;
            continue;
        }
        Hunk h{i, i, j, j};
        while ((i < n || j < m) && !(i < n && j < m && w0[i] == w1[j])) {
            if (j >= m || (i < n && L[i + 1][j] >= L[i][j + 1])) i++;
            else j++;
        }
        h.a1 = i, h.b1 = j;
        hunks.push_back(h);
    }
    if (hunks.empty()) return false;
    // o without spaces, with the position of each kept character
    std::u32string so;
    std::vector<size_t> at;
    for (size_t k = 0; k < o.size(); k++)
        if (o[k] != U' ') so += o[k], at.push_back(k);
    auto boundary = [&](size_t k) { return k == 0 || k >= o.size() || o[k - 1] == U' ' || o[k] == U' '; };
    std::u32string res;
    size_t done = 0;  // o is copied up to here
    for (const Hunk& h : hunks) {
        // a pure insertion is anchored on the word before it
        const size_t a0 = h.a0 == h.a1 && h.a0 > 0 ? h.a0 - 1 : h.a0;
        const size_t b0 = h.a0 == h.a1 && h.a0 > 0 ? h.b0 - 1 : h.b0;
        if (a0 == h.a1) return false;
        std::u32string key;
        for (size_t k = a0; k < h.a1; k++) key += w0[k];
        const size_t f = so.find(key);
        if (f == std::u32string::npos || so.find(key, f + 1) != std::u32string::npos) return false;
        // the run must be the same occurrence as in r0: once in r0 too, and with r0's neighbouring words (or the line's
        // start and end) right around it in o, spaces ignored
        std::u32string s0r;
        for (const auto& w : w0) s0r += w;
        if (const size_t g = s0r.find(key); g == std::u32string::npos || s0r.find(key, g + 1) != std::u32string::npos) return false;
        if (a0 == 0 ? f != 0 : (f < w0[a0 - 1].size() || so.compare(f - w0[a0 - 1].size(), w0[a0 - 1].size(), w0[a0 - 1]) != 0)) return false;
        const size_t fe = f + key.size();
        if (h.a1 == n ? fe != so.size() : so.compare(fe, w0[h.a1].size(), w0[h.a1]) != 0) return false;
        const size_t s0 = at[f], s1 = at[f + key.size() - 1] + 1;
        if (s0 < done || !boundary(s0) || !(s1 >= o.size() || o[s1] == U' ')) return false;
        // the retrofit may space more than 0.7.x (돌려 버렸다 for 돌려버렸다) but never less (우선, 들 → 우선,들)
        const auto spaces = [](std::u32string_view x) { return std::count(x.begin(), x.end(), U' '); };
        if (spaces(std::u32string_view(o).substr(s0, s1 - s0)) > static_cast<long>(h.a1 - a0 - 1)) return false;
        res += o.substr(done, s0 - done);
        std::u32string rep = Join(w1, b0, h.b1);
        if (rep.empty()) {
            // a deleted word takes one space with it
            if (s1 < o.size() && o[s1] == U' ') done = s1 + 1;
            else {
                while (!res.empty() && res.back() == U' ') res.pop_back();
                done = s1;
            }
            continue;
        }
        res += rep;
        done = s1;
    }
    res += o.substr(done);
    out = U8(res);
    return true;
}

}  // namespace

std::shared_ptr<const RuleData> LoadRule(const std::string& dataRoot, std::string& err) {
    auto d = std::make_shared<RuleData>();
    const std::string path = dataRoot + "/bridge/data/reader.tsv";
    std::string text;
    if (!ReadWholeFile(path, text)) {
        err = "cannot read " + path;
        return nullptr;
    }
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string_view row(text.data() + start, end - start);
        start = end + 1;
        if (row.empty() || row[0] == '#') continue;
        std::vector<std::u32string> c;
        for (size_t a = 0;;) {
            const size_t tab = row.find('\t', a);
            c.push_back(U32(row.substr(a, tab == std::string_view::npos ? std::string_view::npos : tab - a)));
            if (tab == std::string_view::npos) break;
            a = tab + 1;
        }
        const std::string kind = U8(c[0]);
        if (c.size() < 2 || c[1].empty()) continue;
        if (kind == "punct-ja" && c.size() >= 3) d->punctJa[c[1]] = U8(c[2]);
        else if (kind == "punct-ko" && c.size() >= 4) d->punctKo[c[1]] = {U8(c[2]), c[3] == U"-" ? std::u32string() : c[3]};
        else if (kind == "sym") d->sym.insert(c[1][0]);
        else if (kind == "ja-particle") d->jaParticles.insert(c[1]);
        else if (kind == "copula-not") d->copulaNot.insert(c[1]);
        else if (kind == "dep") d->deps.insert(c[1]);
        else if (kind == "glue-lemma") d->glueLemmas.insert(c[1]);
        else if (kind == "imp-end") d->impEnds.insert(c[1]);
        else if (kind == "imp-tail") d->impTail.insert(c[1][0]);
        else if (kind == "imp-close") d->impClose.insert(c[1][0]);
        else if (kind == "part-alt") d->partAlts.insert(c[1]);
        else if (kind == "stutter-mark") d->stutterMark = c[1][0];
        else if (kind == "copula-alt") d->copulaAlt = c[1];
        else if (kind == "copula-lemma") d->copulaLemma = c[1];
        else if (kind == "lemma-end") d->lemmaEnd = c[1];
        else if (kind == "quote" && c.size() >= 3) d->quotes.push_back({c[1][0], c[2][0]});
        else if (kind == "tag" && c.size() >= 3) d->tags.push_back({c[1][0], c[2][0]});
        else if (kind == "caption-mark") d->captionMarks.insert(c[1][0]);
        else if (kind == "keigo") {
            auto col = [&](size_t k) { return k < c.size() && c[k] != U"-" ? U8(c[k]) : std::string(); };
            d->keigo.push_back({U8(c[1]), col(2), col(3), col(4)});
        } else if (kind == "keigo-prefix" && c.size() >= 3) d->keigoPrefix.push_back({U8(c[1]), U8(c[2])});
        else if (kind == "command-form") d->commandForms.push_back(U8(c[1]));
        else if (kind == "rewrite") d->rewrites.push_back(U8(c[1]));
        else {
            err = path + ": unknown row kind " + kind;
            return nullptr;
        }
    }
    // measurement switch for the one-rule-at-a-time gate: HD08_RULE_REWRITES=none | ID,ID replaces the list
    if (const char* env = std::getenv("HD08_RULE_REWRITES")) {
        d->rewrites.clear();
        const std::string v = env;
        for (size_t a = 0; a < v.size();) {
            size_t c = v.find(',', a);
            if (c == std::string::npos) c = v.size();
            if (c > a && v.substr(a, c - a) != "none") d->rewrites.push_back(v.substr(a, c - a));
            a = c + 1;
        }
    }
    d->retro = hd8gen::LoadRetro(dataRoot + "/generator/data", err);
    if (!d->retro) return nullptr;
    // a rewrite id must name a rule (the retrofit selects rules by id prefix, so a typo would leave it silently off)
    const std::vector<std::string> known = hd8gen::RetroRuleIds(*d->retro);
    for (const std::string& id : d->rewrites)
        if (std::find(known.begin(), known.end(), id) == known.end()) {
            err = path + ": rewrite " + id + " is not a rule of generator/data/ko/rewrite/rules.tsv";
            return nullptr;
        }
    return d;
}

void RuleCapture::Clear() {
    items.clear();
    cleaned.clear();
    line = -1;
}

void RuleCapture::Line(int l) {
    line = l;
    if (l < 0) return;
    if (items.size() <= static_cast<size_t>(l)) {
        items.resize(static_cast<size_t>(l) + 1);
        cleaned.resize(static_cast<size_t>(l) + 1);
    }
    items[static_cast<size_t>(l)].clear();  // a pass over the line starts again
    cleaned[static_cast<size_t>(l)].clear();
}

void RuleCapture::Resolved(int chunk, int sentence, const hdTrans::word::Chain& chain) {
    if (line < 0) return;
    auto& v = items[static_cast<size_t>(line)];
    for (const hdTrans::word::Item* it = chain.head; it != nullptr; it = it->next) {
        Item x;
        x.chunk = chunk;
        x.sentence = sentence;
        x.token = it->tokenIndex;
        if (it->group != nullptr) x.surface = it->group->surface;
        x.target = std::string(it->EntryText(it->selected));
        if (it->group != nullptr)
            if (const hdTrans::word::Entry* e = it->group->At(it->selected)) {
                const bool name = e->user != nullptr && (e->user->name || e->user->nameLayer);
                x.person = e->attr.find(kPersonCode) != std::string_view::npos || (e->user != nullptr && e->user->name);
                // a katakana word may be a name the dictionaries do not know (コヨイ, ナオ): never a known thing
                const std::u32string su = U32(x.surface);
                const bool kana = !su.empty() && std::all_of(su.begin(), su.end(), [](char32_t c) { return (c >= 0x30A1 && c <= 0x30FE) || (c >= 0xFF66 && c <= 0xFF9F); });
                // a word with a person or name sense among its candidates (千里: 천리, and the name 치사토) is not a
                // known thing either
                bool personSense = false;
                for (const hdTrans::word::Entry* o : it->group->entries)
                    if (o != nullptr && (o->attr.find(kPersonCode) != std::string_view::npos || o->attr.find(kNameCode) != std::string_view::npos ||
                                         (o->user != nullptr && (o->user->name || o->user->nameLayer))))
                        personSense = true;
                x.thing = !x.person && !name && !kana && !personSense && e->cls == 'N' && e->code == kCommonNounCode &&
                          e->attr.find(kNameCode) == std::string_view::npos;
            }
        if (const hdTrans::word::Group* g = it->DirectGroup())
            if (const hdTrans::word::Entry* e = g->At(it->DirectIndex())) x.ending = e->dst;
        v.push_back(std::move(x));
    }
}

void RuleCapture::Cleaned(const hdTrans::output::Pieces& pieces) {
    if (line < 0) return;
    cleaned[static_cast<size_t>(line)] += hdTrans::output::String(pieces);
}

bool DialogueLine(const RuleData& d, const std::string& ja) {
    const std::u32string s = AfterTag(d, U32(ja));
    if (s.empty()) return false;
    return std::any_of(d.quotes.begin(), d.quotes.end(), [&](const auto& q) { return s.front() == q.first; });
}

bool CaptionDocument(const RuleData& d, const std::vector<std::string>& ja) {
    size_t lines = 0, tagged = 0, quoted = 0, shortLines = 0, timing = 0, marked = 0;
    for (const std::string& l : ja) {
        std::u32string s = U32(l);
        while (!s.empty() && Space(s.front())) s.erase(0, 1);
        while (!s.empty() && Space(s.back())) s.pop_back();
        if (s.empty()) continue;
        lines++;
        if (std::any_of(d.quotes.begin(), d.quotes.end(), [&](const auto& q) { return s.find(q.first) != std::u32string::npos; }))
            quoted++;
        // a speaker tag in parentheses: (name)text
        for (const auto& [open, close] : d.tags) {
            if (s.front() != open || (open != U'(' && open != U'（')) continue;
            const size_t e = s.find(close);
            if (e != std::u32string::npos && e <= 24 && e + 1 < s.size()) tagged++;
            break;
        }
        if (s.size() <= 20) shortLines++;
        if (std::any_of(s.begin(), s.end(), [&](char32_t c) { return d.captionMarks.count(c) > 0; })) marked++;
        // timing marks: 00:01:02,345 or -->
        for (size_t i = 0; i + 4 < s.size(); i++) {
            const auto dig = [](char32_t c) { return c >= U'0' && c <= U'9'; };
            if ((dig(s[i]) && dig(s[i + 1]) && s[i + 2] == U':' && dig(s[i + 3]) && dig(s[i + 4])) ||
                (s[i] == U'-' && s[i + 1] == U'-' && s[i + 2] == U'>')) {
                timing++;
                break;
            }
        }
    }
    if (lines == 0) return false;
    if (timing * 5 >= lines) return true;
    if (tagged * 5 >= lines || marked * 5 >= lines) return true;
    return shortLines * 2 >= lines && quoted * 10 <= lines;
}

std::string PreLine(const RuleData& d, size_t i, const std::string& ja, const std::string& out075, const RuleCapture& cap,
                    bool dialogue, bool keigo, bool command) {
    std::vector<Row> rows;
    if (i < cap.items.size()) {
        for (const auto& x : cap.items[i]) {
            Row r;
            r.sent = x.sentence;
            r.seg = x.token;
            r.surf = U32(x.surface);
            r.target = U32(x.target);
            r.ending = U32(x.ending);
            r.person = x.person;
            r.thing = x.thing;
            rows.push_back(std::move(r));
        }
    }
    const std::u32string ja32 = U32(ja);
    std::vector<PreItem> items = ConvertLine(d, ja32, rows);
    if (i < cap.cleaned.size()) Glue(d, items, U32(cap.cleaned[i]));
    std::string o = "{\"line\":" + std::to_string(i + 1);
    Field(o, "ja", ja);
    Field(o, "out075", out075);
    o += ",\"items\":[";
    for (size_t k = 0; k < items.size(); k++) {
        if (k) o += ',';
        o += ItemJson(items[k]);
    }
    o += "]";
    o += std::string(",\"imp\":") + (Imperative(d, ja32) ? "true" : "false");
    size_t lead = 0;
    while (lead < out075.size() && out075[lead] == ' ') lead++;
    Field(o, "lead", out075.substr(0, lead));
    Field(o, "speaker", "");
    Field(o, "scene", "doc");
    Field(o, "work", "doc");
    o += std::string(",\"dialogue\":") + (dialogue ? "true" : "false");
    if (keigo) o += ",\"keigo\":true";
    if (command) o += ",\"command\":true";
    o += '}';
    return o;
}

void RuleRewrite(const RuleData& d, const std::vector<std::string>& ja, const RuleCapture& cap, std::vector<std::string>& out,
                 RuleCounts& counts, std::string* preOut, const Bridge* bridge) {
    counts.lines += ja.size();
    if (CaptionDocument(d, ja)) {
        counts.captionDocs++;
        return;
    }
    std::vector<bool> dialogue(ja.size());
    std::vector<std::string> pre;
    for (size_t i = 0; i < ja.size(); i++) {
        dialogue[i] = DialogueLine(d, ja[i]);
        counts.dialogue += dialogue[i];
        std::vector<SourceToken> toks;
        if (dialogue[i] && bridge != nullptr) toks = AnalyzeSource(bridge, ja[i]);
        const bool keigo = KeigoSource(d, toks);
        counts.keigo += keigo;
        // a word in the imperative form (見ろ, 追え, 黙ってろ): 0.7.x's 해라체 commands stay (봐라, 쫓아라)
        bool command = false;
        for (const SourceToken& t : toks)
            for (const std::string& f : d.commandForms)
                if (t.cform.rfind(f, 0) == 0) command = true;
        std::string o = i < out.size() ? out[i] : std::string();
        if (!o.empty() && o.back() == '\r') o.pop_back();
        pre.push_back(PreLine(d, i, ja[i], o, cap, dialogue[i], keigo, command));
    }
    if (preOut)
        for (const std::string& l : pre) *preOut += l + "\n";
    std::vector<hd8gen::RetroLine> lines, rewritten;
    std::string err;
    // the retrofit's speech-level pass runs without rewrite rules; the rules that passed their gate run in a
    // second pass, and their effect is all that differs between the two
    hd8gen::RetroOptions opt;
    opt.only = {"-none-"};
    // 합쇼체 is never lowered: 합쇼 → 해요 tied in game and lost in fiction on two dev slices; 해라 → 해 is the gain
    opt.keepHapsyo = true;
    if (!hd8gen::RunRetro(*d.retro, pre, opt, lines, nullptr, err)) return;
    if (!d.rewrites.empty()) {
        hd8gen::RetroOptions wopt = opt;
        wopt.only = d.rewrites;
        if (!hd8gen::RunRetro(*d.retro, pre, wopt, rewritten, nullptr, err)) rewritten.clear();
    }
    for (size_t i = 0; i < lines.size() && i < out.size(); i++) {
        if (!lines[i].ok) continue;
        counts.read++;
        const bool cr = !out[i].empty() && out[i].back() == '\r';
        const std::string o075 = cr ? out[i].substr(0, out[i].size() - 1) : out[i];
        // a rewrite rule changes the line: taken whole where the line is replaced anyway; elsewhere only the words
        // the rules changed move onto 0.7.x's text (so nothing but the rule moves)
        const hd8gen::RetroLine* w = nullptr;
        // a rule must not change a sentence end's speech level (ends: the levels after the rewrite); a line whose sentences
        // end at different levels gets no rewrite
        if (i < rewritten.size() && rewritten[i].ok && !rewritten[i].fired.empty() && rewritten[i].out != lines[i].out &&
            !lines[i].mixedLevels && rewritten[i].ends == lines[i].ends)
            w = &rewritten[i];
        const bool level = dialogue[i] && !lines[i].mixedLevels && lines[i].levelChanged;
        if (w != nullptr && !level) {
            // the line keeps 0.7.x's text except for what the rules changed
            std::string t;
            if (lines[i].out == o075) t = w->out;
            else if (!Transplant(o075, lines[i].out, w->out, t)) {
                counts.rewriteBlocked++;
                w = nullptr;
            }
            if (w != nullptr) {
                if (t != o075) counts.rewritten++;
                out[i] = t + (cr ? "\r" : "");
                continue;
            }
        }
        if (!dialogue[i]) continue;
        // only a change of speech level is worth the rewrite: a line at the same level keeps 0.7.x's text, and
        // so does a line whose sentences would end at different levels
        if (lines[i].mixedLevels) {
            counts.mixed++;
            continue;
        }
        if (!lines[i].levelChanged) {
            counts.sameLevel += lines[i].out != o075;
            continue;
        }
        const std::string& lo = w != nullptr ? w->out : lines[i].out;
        if (w != nullptr) counts.rewritten++;
        if (lo != o075) counts.replaced++;
        out[i] = lo + (cr ? "\r" : "");
    }
}

}  // namespace hd08::bridge
