#include "core/register/reader.h"

#include <algorithm>

#include "core/output/inflect.h"
#include "core/register/hangul.h"
#include "core/word/item.h"

namespace hdTrans::reg {

namespace {

using hangul::ToU32;
using hangul::ToU8;
using output::internal::FindKoreanFamily;
using output::internal::InflectionClass;
using output::internal::KoreanFamily;
using output::internal::KoreanPartOfSpeech;
using K = PreItem::Kind;

bool AllHangul(std::u32string_view s) { return std::all_of(s.begin(), s.end(), hangul::IsSyllable); }
bool Kana(char32_t c) { return (c >= 0x3041 && c <= 0x3093) || (c >= 0x30A1 && c <= 0x30F6); }
bool Space(char32_t c) { return c == ' ' || c == 0x3000 || c == '\t' || c == '\r' || c == '\n'; }

// (이)가: the alternation a particle notation starts with, and the word after it; 0 when s starts with none.
std::size_t PartPrefix(const Reader& d, std::u32string_view s, std::u32string& alt, std::u32string& word) {
    if (s.size() < 3 || s[0] != U'(') return 0;
    const std::size_t close = s.find(U')');
    if (close == std::u32string_view::npos) return 0;
    alt = std::u32string(s.substr(1, close - 1));
    if (!d.partAlts.contains(alt)) return 0;
    std::size_t e = close + 1;
    while (e < s.size() && hangul::IsSyllable(s[e])) e++;
    if (e == close + 1) return 0;
    word = std::u32string(s.substr(close + 1, e - close - 1));
    return e;
}

std::u32string Notation(std::u32string_view alt, std::u32string_view word) {
    return U"(" + std::u32string(alt) + U")" + std::u32string(word);
}

PreItem Item(K kind, std::string t = {}) {
    PreItem it;
    it.kind = kind;
    it.t = std::move(t);
    return it;
}

void PlainWords(const Reader& d, std::u32string t, std::vector<PreItem>& out) {
    // glosses in parentheses next to Hangul are dropped: 사과(애플) → 사과
    for (std::size_t i = 0; i < t.size();) {
        if (t[i] != U'(') {
            i++;
            continue;
        }
        std::size_t close = i + 1;
        while (close < t.size() && (hangul::IsSyllable(t[close]) || t[close] == U' ')) close++;
        if (close >= t.size() || t[close] != U')' || close == i + 1) {
            i++;
            continue;
        }
        const bool before = i > 0 && hangul::IsSyllable(t[i - 1]);
        const bool after = close + 1 < t.size() && hangul::IsSyllable(t[close + 1]);
        // (이)가, (으)로 opening a word is a particle notation, not a gloss
        const bool particle = after && !before && d.partAlts.contains(t.substr(i + 1, close - i - 1));
        if ((before || after) && !particle) t.erase(i, close - i + 1);
        else i++;
    }
    for (std::size_t start = 0; start <= t.size();) {
        std::size_t sp = t.find(U' ', start);
        if (sp == std::u32string::npos) sp = t.size();
        const std::u32string w = t.substr(start, sp - start);
        start = sp + 1;
        if (w.empty()) {
            if (sp == t.size()) break;
            continue;
        }
        std::u32string alt, word;
        if (PartPrefix(d, w, alt, word) == w.size()) {
            out.push_back(Item(K::P, ToU8(w)));
        } else if (const auto pk = d.punctKo.find(w); pk != d.punctKo.end()) {
            PreItem it = Item(K::Punct);
            it.p = pk->second.first;
            out.push_back(it);
        } else {
            out.push_back(Item(AllHangul(w) ? K::N : K::Raw, ToU8(w)));
        }
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

// The target and ending of one word as items.
std::vector<PreItem> ItemsOfTarget(const Reader& d, const std::u32string& t, const std::u32string& ending,
                                   const std::u32string& jsurf) {
    std::vector<PreItem> out;
    if (t.empty() || t == U" ") return out;
    if (const auto pk = d.punctKo.find(t); pk != d.punctKo.end() && ending.empty()) {
        PreItem it = Item(K::Punct);
        it.p = pk->second.first;
        out.push_back(it);
        return out;
    }
    // a marker family at the end: stem<&XX@>
    if (t.size() >= 4 && t.back() == U'@' &&
        (t[t.size() - 4] == U'&' || t[t.size() - 4] == U'$' || t[t.size() - 4] == U'%')) {
        if (const KoreanFamily* f = FindKoreanFamily(ToU8(t.substr(t.size() - 4)))) {
            const std::u32string prefix = t.substr(0, t.size() - 4);
            std::vector<std::u32string> words;
            for (std::size_t a = 0;;) {
                const std::size_t sp = prefix.find(U' ', a);
                words.push_back(prefix.substr(a, sp == std::u32string::npos ? std::u32string::npos : sp - a));
                if (sp == std::u32string::npos) break;
                a = sp + 1;
            }
            std::u32string head = words.back();
            for (std::size_t w = 0; w + 1 < words.size(); w++) PlainWords(d, words[w], out);
            std::u32string alt, word;
            if (const std::size_t e = PartPrefix(d, head, alt, word)) {
                out.push_back(Item(K::P, ToU8(Notation(alt, word))));
                head = head.substr(e);
            }
            const std::u32string stem = ToU32(f->stem);
            if (stem.find(U'(') != std::u32string::npos || !AllHangul(stem + head)) {
                out.push_back(Item(K::Raw, ToU8(t)));
                return out;
            }
            std::string pos = f->partOfSpeech == KoreanPartOfSpeech::Verb ? "V" : "A";
            if (f->inflectionClass == InflectionClass::Existential) pos = "X";
            std::string cls = ClassName(f->inflectionClass);
            if (cls == "B" && (f->flags & output::internal::kBieupWa)) cls = "BWA";
            std::u32string lemma = head + stem + d.lemmaEnd;
            if (cls == "COP") {
                if (!head.empty()) out.push_back(Item(K::N, ToU8(head)));
                lemma = d.copulaLemma;
                pos = "C";
            }
            PreItem v = Item(K::V);
            v.lemma = ToU8(lemma);
            v.pos = pos;
            v.cls = cls;
            v.rest = ending.empty() ? "" : ToU8(ending.substr(1));
            out.push_back(v);
            return out;
        }
    }
    // copula notations (이)다, (이)야, (이었)였지만; not the particles (이)가, (이)나 …
    if (t.size() >= 4 && t[0] == U'(' && !d.copulaNot.contains(t) && ending.empty()) {
        const std::size_t close = t.find(U')');
        if (close != std::u32string::npos && close + 1 < t.size() && t.compare(1, d.copulaAlt.size(), d.copulaAlt) == 0) {
            const std::u32string inside = t.substr(1, close - 1);
            const std::u32string vowel = t.substr(close + 1);
            if (AllHangul(inside) && AllHangul(vowel)) {
                const std::u32string cons = inside.size() == 1 ? inside + vowel : inside + vowel.substr(1);
                PreItem v = Item(K::V);
                v.lemma = ToU8(d.copulaLemma);
                v.pos = "C";
                v.cls = "COP";
                v.rest = ToU8(cons.substr(1));
                out.push_back(v);
                return out;
            }
        }
    }
    std::u32string alt, word;
    if (const std::size_t e = PartPrefix(d, t, alt, word)) {
        out.push_back(Item(K::P, ToU8(Notation(alt, word))));
        std::u32string rest = t.substr(e);
        while (!rest.empty() && rest.front() == U' ') rest.erase(0, 1);
        while (!rest.empty() && rest.back() == U' ') rest.pop_back();
        if (!rest.empty()) PlainWords(d, rest, out);
        return out;
    }
    if (!ending.empty()) {
        out.push_back(Item(K::Raw, ToU8(t + ending)));
        return out;
    }
    if (d.jaParticles.contains(jsurf) && t.size() <= 4 && AllHangul(t)) {
        out.push_back(Item(K::P, ToU8(t)));
        return out;
    }
    PlainWords(d, t, out);
    return out;
}

struct Row {
    int sent = 0, seg = 0;
    std::u32string surf, target, ending;
};

std::vector<PreItem> ConvertLine(const Reader& d, const std::u32string& ja, const std::vector<Row>& rows) {
    // the source positions of the items whose surface is in the line
    std::size_t cursor = 0;
    std::vector<long> pos;
    for (const Row& r : rows) {
        long p = -1;
        if (!r.surf.empty()) {
            const std::size_t f = std::u32string_view(ja).substr(0, cursor + 60 + r.surf.size()).find(r.surf, cursor);
            if (f != std::u32string::npos) p = static_cast<long>(f);
        }
        pos.push_back(p);
        if (p >= 0) cursor = static_cast<std::size_t>(p) + r.surf.size();
    }
    std::vector<bool> covered(ja.size());
    for (std::size_t n = 0; n < rows.size(); n++)
        if (pos[n] >= 0)
            for (std::size_t k = static_cast<std::size_t>(pos[n]); k < static_cast<std::size_t>(pos[n]) + rows[n].surf.size(); k++)
                covered[k] = true;
    const auto cov = [&](std::size_t i) { return covered[i]; };
    // uncovered marks, symbols and stutters in source order
    std::vector<std::pair<std::size_t, PreItem>> runs;
    for (std::size_t i = 0; i < ja.size(); i++) {
        if (cov(i)) continue;
        const char32_t c = ja[i];
        if (const auto pj = d.punctJa.find(std::u32string(1, c)); pj != d.punctJa.end()) {
            PreItem it = Item(K::Punct);
            it.p = pj->second;
            it.gap = true;
            runs.push_back({i, it});
            continue;
        }
        if (d.sym.contains(c)) {
            PreItem it = Item(K::Raw, ToU8(std::u32string(1, c)));
            it.gap = true;
            runs.push_back({i, it});
            continue;
        }
        if (Kana(c) && i + 2 < ja.size() && ja[i + 1] == d.stutterMark && ja[i + 2] == c) runs.push_back({i, Item(K::Raw, "\x01")});
    }
    std::vector<PreItem> out;
    std::size_t ri = 0;
    const Row* prev = nullptr;
    for (std::size_t n = 0; n < rows.size(); n++) {
        const Row& r = rows[n];
        const long p = pos[n];
        if (p >= 0) {
            while (ri < runs.size() && runs[ri].first < static_cast<std::size_t>(p)) out.push_back(runs[ri++].second);
        } else if (prev != nullptr && (r.seg > prev->seg + 1 || r.sent != prev->sent)) {
            long next = -1;
            for (std::size_t j = n + 1; j < rows.size(); j++)
                if (pos[j] >= 0) {
                    next = pos[j];
                    break;
                }
            if (ri < runs.size() && (next < 0 || runs[ri].first < static_cast<std::size_t>(next))) out.push_back(runs[ri++].second);
        }
        const std::vector<PreItem> its = ItemsOfTarget(d, r.target, r.ending, r.surf);
        if (p < 0) {
            // a mark the chain wrote itself (? for ？) stands for the next uncovered mark of its kind
            for (const PreItem& it : its) {
                if (it.kind != K::Punct) continue;
                for (std::size_t j = ri; j < runs.size(); j++) {
                    if (runs[j].second.kind == K::Punct && runs[j].second.p == it.p) {
                        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(j));
                        break;
                    }
                }
            }
        }
        out.insert(out.end(), its.begin(), its.end());
        prev = &r;
    }
    while (ri < runs.size()) out.push_back(runs[ri++].second);
    return out;
}

bool Imperative(const Reader& d, std::u32string s) {
    while (!s.empty() && Space(s.back())) s.pop_back();
    if (!s.empty() && d.impClose.contains(s.back())) s.pop_back();
    while (!s.empty() && (d.impTail.contains(s.back()) || Space(s.back()))) s.pop_back();
    return std::any_of(d.impEnds.begin(), d.impEnds.end(), [&](const std::u32string& e) { return s.ends_with(e); });
}

// The text after an optional speaker tag ((name), 【name】) and white space.
std::u32string AfterTag(const Reader& d, std::u32string s) {
    while (!s.empty() && Space(s.front())) s.erase(0, 1);
    for (const auto& [open, close] : d.tags) {
        if (s.empty() || s.front() != open) continue;
        const std::size_t e = s.find(close);
        if (e != std::u32string::npos && e <= 24) {
            s = s.substr(e + 1);
            while (!s.empty() && Space(s.front())) s.erase(0, 1);
        }
        break;
    }
    return s;
}

// The speaker tag of a line (【筧】「…」 → 筧), "" when it has none.
std::string SpeakerTag(const Reader& d, std::u32string s) {
    while (!s.empty() && Space(s.front())) s.erase(0, 1);
    for (const auto& [open, close] : d.tags) {
        if (s.empty() || s.front() != open) continue;
        const std::size_t e = s.find(close);
        if (e != std::u32string::npos && e > 1 && e <= 24 && e + 1 < s.size()) return ToU8(s.substr(1, e - 1));
        break;
    }
    return "";
}

// Whether text at p starts with an entry of nots; an entry's _ marks p.
bool NotAt(std::u32string_view text, std::size_t p, const std::vector<std::u32string>& nots) {
    for (const std::u32string& n : nots) {
        const std::size_t mark = n.find(U'_');
        const std::size_t back = mark == std::u32string::npos ? 0 : mark;
        if (back > p) continue;
        std::u32string whole = n;
        if (mark != std::u32string::npos) whole.erase(mark, 1);
        if (text.substr(p - back).starts_with(whole)) return true;
    }
    return false;
}

bool StartsAny(std::u32string_view text, std::size_t p, const std::vector<std::u32string>& spellings) {
    for (const std::u32string& s : spellings)
        if (text.substr(p).starts_with(s)) return true;
    return false;
}

bool Kanji(char32_t c) { return (c >= 0x4E00 && c <= 0x9FFF) || c == U'々'; }
// The kana that end the 連用形 of a verb: the い and え rows.
bool StemEnd(char32_t c) {
    constexpr std::u32string_view kEnds = U"いきぎしじちぢにひびぴみりえけげせぜてでねへべぺめれ";
    return kEnds.find(c) != std::u32string_view::npos;
}

// Whether a verb the word stage read stands in the imperative: its conjugation branch is the imperative
// (見ろ, 追え), or it is the e-stem of a godan verb with only marks after it (追い込めぇ, 戻れっ), which the
// potential stem never is.
bool ImperativeVerb(const std::vector<Capture::Item>& items, std::size_t i) {
    const Capture::Item& it = items[i];
    if (!it.verb) return false;
    if (it.kind == 11 || it.kind == 12) return true;
    if (it.kind != 4) return false;
    constexpr std::u32string_view kMarks = U"ぁぃぅぇぉっッーｰ～〜！!？?…‥、。";
    const std::u32string end = ToU32(it.sourceEnding);
    if (!std::all_of(end.begin(), end.end(), [&](char32_t c) { return kMarks.find(c) != std::u32string_view::npos; }))
        return false;
    if (!end.empty() || i + 1 >= items.size()) return true;
    const std::u32string next = ToU32(items[i + 1].surface);
    return next.empty() || !Kana(next[0]);
}

// The keigo and command checks of a dialogue line: reader.tsv's keigo, keigo-prefix and command-form rows
// as keigo.tsv spells them in the source, and the verbs the word stage read in the imperative.
void KeigoAndCommand(const Data& d, std::u32string_view text, const std::vector<Capture::Item>& items, bool& keigo,
                     bool& command) {
    for (std::size_t i = 0; i < items.size(); i++)
        if (ImperativeVerb(items, i)) command = true;
    for (const KeigoWord& w : d.keigo) {
        bool& flag = w.kind == KeigoWord::Kind::command ? command : keigo;
        for (std::size_t p = 0; p < text.size() && !flag; p++) {
            if (w.kind == KeigoWord::Kind::prefix) {
                // お or 御, kanji, one kana of the い or え row, then the row's verb
                if (text[p] != U'お' && text[p] != U'御') continue;
                std::size_t q = p + 1;
                while (q < text.size() && Kanji(text[q])) q++;
                if (q == p + 1 || q >= text.size() || !StemEnd(text[q])) continue;
                q++;
                if (StartsAny(text, q, w.start) && !NotAt(text, q, w.notStart)) flag = true;
                continue;
            }
            if (StartsAny(text, p, w.start) && !NotAt(text, p, w.notStart)) flag = true;
        }
    }
}

}  // namespace

void Capture::Line(int piece) {
    int l = piece;
    bool first = true;
    if (!pieceLine.empty()) {
        l = piece >= 0 && static_cast<std::size_t>(piece) < pieceLine.size() ? pieceLine[static_cast<std::size_t>(piece)] : -1;
        first = piece <= 0 || pieceLine[static_cast<std::size_t>(piece) - 1] != l;
    }
    // the line left keeps only the room its items take
    if (line_ >= 0 && line_ != l && static_cast<std::size_t>(line_) < lines_.size()) {
        LineItems& left = lines_[static_cast<std::size_t>(line_)];
        left.items.shrink_to_fit();
        left.text.shrink_to_fit();
    }
    line_ = l;
    if (l < 0) return;
    LineItems& now = At(static_cast<std::size_t>(l));
    if (first) {  // a pass over the line starts again
        now.items.clear();
        now.text.clear();
    }
    passStart_ = now.items.size();
    passText_ = now.text.size();
}

void Capture::Pass() {
    if (line_ < 0 || static_cast<std::size_t>(line_) >= lines_.size()) return;
    LineItems& now = lines_[static_cast<std::size_t>(line_)];
    now.items.resize(passStart_);
    now.text.resize(passText_);
}

Capture::LineItems& Capture::At(std::size_t l) {
    if (lines_.size() <= l) lines_.resize(l + 1);
    return lines_[l];
}

void Capture::Add(std::size_t l, const Item& item) {
    LineItems& line = At(l);
    Packed p;
    p.sentence = item.sentence;
    p.token = item.token;
    p.at = static_cast<uint32_t>(line.text.size());
    p.kind = static_cast<int16_t>(item.kind);
    p.verb = item.verb;
    for (const auto& [text, length] : {std::pair{&item.surface, &p.surface}, {&item.target, &p.target},
                                       {&item.ending, &p.ending}, {&item.sourceEnding, &p.sourceEnding}}) {
        line.text += *text;
        *length = static_cast<uint32_t>(text->size());
    }
    line.items.push_back(p);
}

std::vector<Capture::Item> Capture::Items(std::size_t l) const {
    std::vector<Item> out;
    if (l >= lines_.size()) return out;
    const LineItems& line = lines_[l];
    out.reserve(line.items.size());
    for (const Packed& p : line.items) {
        Item x;
        x.sentence = p.sentence;
        x.token = p.token;
        x.kind = p.kind;
        x.verb = p.verb;
        std::size_t at = p.at;
        for (const auto& [text, length] : {std::pair{&x.surface, p.surface}, {&x.target, p.target},
                                           {&x.ending, p.ending}, {&x.sourceEnding, p.sourceEnding}}) {
            *text = line.text.substr(at, length);
            at += length;
        }
        out.push_back(std::move(x));
    }
    return out;
}

void Capture::Drop(std::size_t l) {
    if (l < lines_.size()) lines_[l] = LineItems{};
}

void Capture::Resolved(int sentence, const word::Chain& chain) {
    if (line_ < 0) return;
    if (!wanted.empty() && (static_cast<std::size_t>(line_) >= wanted.size() || !wanted[static_cast<std::size_t>(line_)]))
        return;
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) {
        Item x;
        x.sentence = sentence;
        x.token = it->tokenIndex;
        if (it->group != nullptr) x.surface = it->group->surface;
        x.target = std::string(it->EntryText(it->selected));
        if (const word::Group* g = it->DirectGroup())
            if (const word::Entry* e = g->At(it->DirectIndex())) x.ending = e->dst;
        x.verb = it->detail != nullptr && it->detail->cls == word::kDetailVerb;
        if (x.verb) {
            x.kind = it->DetailKind();
            x.sourceEnding = std::string(it->DirectSurface());
        }
        Add(static_cast<std::size_t>(line_), x);
    }
}

bool DialogueLine(const Reader& r, std::string_view ja) {
    const std::u32string s = AfterTag(r, ToU32(ja));
    return !s.empty() && std::any_of(r.quotes.begin(), r.quotes.end(), [&](const auto& q) { return s.front() == q.first; });
}

bool CaptionDocument(const Reader& r, const std::vector<std::string>& ja) {
    std::size_t lines = 0, tagged = 0, quoted = 0, shortLines = 0, timing = 0, marked = 0;
    const auto dig = [](char32_t c) { return c >= U'0' && c <= U'9'; };
    for (const std::string& l : ja) {
        std::u32string s = ToU32(l);
        while (!s.empty() && Space(s.front())) s.erase(0, 1);
        while (!s.empty() && Space(s.back())) s.pop_back();
        if (s.empty()) continue;
        lines++;
        if (std::any_of(r.quotes.begin(), r.quotes.end(), [&](const auto& q) { return s.find(q.first) != std::u32string::npos; }))
            quoted++;
        // a speaker tag in parentheses: (name)text
        for (const auto& [open, close] : r.tags) {
            if (s.front() != open || (open != U'(' && open != U'（')) continue;
            const std::size_t e = s.find(close);
            if (e != std::u32string::npos && e <= 24 && e + 1 < s.size()) tagged++;
            break;
        }
        if (s.size() <= 20) shortLines++;
        if (std::any_of(s.begin(), s.end(), [&](char32_t c) { return r.captionMarks.contains(c); })) marked++;
        // timing marks: 00:01:02,345 or -->
        for (std::size_t i = 0; i + 4 < s.size(); i++) {
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

std::string LineSpeaker(const Reader& r, const std::string& ja) { return SpeakerTag(r, ToU32(ja)); }

PreLine ReadLine(const Data& data, std::size_t i, const std::string& ja, const std::string& out, const Capture& capture,
                 bool dialogue, uint32_t scene) {
    const Reader& d = data.reader;
    PreLine line;
    line.ja = ja;
    line.out = out;
    line.scene = scene;
    line.dialogue = dialogue;
    const std::u32string ja32 = ToU32(ja);
    line.speaker = SpeakerTag(d, ja32);  // a tagged line without a quote is dialogue too
    if (!dialogue) return line;  // only dialogue lines are written again
    std::vector<Row> rows;
    if (i < capture.Lines()) {
        const std::vector<Capture::Item> items = capture.Items(i);
        for (const auto& x : items) rows.push_back({x.sentence, x.token, ToU32(x.surface), ToU32(x.target), ToU32(x.ending)});
        KeigoAndCommand(data, ja32, items, line.keigo, line.command);
    }
    line.items = ConvertLine(d, ja32, rows);
    line.imp = Imperative(d, ja32);
    return line;
}

}  // namespace hdTrans::reg
