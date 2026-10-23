#include "core/names/ruby.h"

#include <cctype>
#include <vector>

#include "core/names/chars.h"
#include "core/names/registry.h"

namespace hdTrans::names {

namespace internal {

namespace {

// Advances pos over up to max code points for which pred holds; returns how many.
template <class Pred>
std::size_t Take(std::string_view s, std::size_t& pos, std::size_t max, Pred pred) {
    std::size_t n = 0;
    while (n < max && pos < s.size()) {
        const text::Rune r = RuneAt(s, pos);
        if (!pred(r.cp)) break;
        pos += r.size;
        n++;
    }
    return n;
}

bool Lit(std::string_view s, std::size_t& pos, std::string_view lit) {
    if (!s.substr(pos).starts_with(lit)) return false;
    pos += lit.size();
    return true;
}

// The byte offset after n code points of s from pos.
std::size_t Advance(std::string_view s, std::size_t pos, std::size_t n) {
    for (std::size_t i = 0; i < n && pos < s.size(); i++) pos += RuneAt(s, pos).size;
    return pos;
}

bool NotRubyBar(char32_t c) { return c != U'|' && c != U'｜' && c != U'《' && c != U'》'; }
bool NotBracket(char32_t c) { return c != U'《' && c != U'》'; }
bool NotAngle(char32_t c) { return c != U'<'; }
bool IsRubyKanji(char32_t c) { return IsKanji(c); }
bool IsGlossKatakana(char32_t c) { return (c >= 0x30A1 && c <= 0x30FA) || c == 0x30FC; }
bool IsGlossKana(char32_t c) { return (c >= 0x3041 && c <= 0x3096) || c == 0x30FC || c == 0x30FB || c == ' ' || c == 0x3000; }

// [|｜]([^|｜《》]{1,20})《([^《》]{1,30})》
bool BarRuby(std::string_view s, std::size_t pos, RubyMatch& m) {
    std::size_t p = pos;
    if (p >= s.size()) return false;
    const char32_t lead = RuneAt(s, p).cp;
    if (lead != U'|' && lead != U'｜') return false;
    p += RuneAt(s, p).size;
    const std::size_t baseStart = p;
    const std::size_t n = Take(s, p, 21, NotRubyBar);
    const std::size_t baseEnd = p;
    if (n < 1 || n > 20 || !Lit(s, p, "《")) return false;
    const std::size_t readStart = p;
    const std::size_t k = Take(s, p, 31, NotBracket);
    const std::size_t readEnd = p;
    if (k < 1 || k > 30 || !Lit(s, p, "》")) return false;
    m.begin = pos;
    m.end = p;
    m.base = s.substr(baseStart, baseEnd - baseStart);
    m.reading = s.substr(readStart, readEnd - readStart);
    return true;
}

// An opening parenthesis at p, full-width or not; p is moved past it.
bool OpenParen(std::string_view s, std::size_t& p) { return Lit(s, p, "（") || Lit(s, p, "("); }
bool CloseParen(std::string_view s, std::size_t& p) { return Lit(s, p, "）") || Lit(s, p, ")"); }
bool NotParen(char32_t c) { return c != U'（' && c != U'）' && c != U'(' && c != U')'; }

// [|｜]([一-龥々〆ヶぁ-ゖァ-ヺーA-Za-z0-9]{1,20})[（(]([ぁ-ゖァ-ヺー・]{1,30})[）)]: a bar
// that also separates (メニュー | 運行情報(...)) leaves a space or a mark before the
// parenthesis.
bool BarParenRuby(std::string_view s, std::size_t pos, RubyMatch& m) {
    std::size_t p = pos;
    if (p >= s.size()) return false;
    const char32_t lead = RuneAt(s, p).cp;
    if (lead != U'|' && lead != U'｜') return false;
    p += RuneAt(s, p).size;
    const std::size_t baseStart = p;
    const std::size_t n = Take(s, p, 21, [](char32_t c) {
        return IsKanji(c) || IsHiragana(c) || IsKatakana(c) || (c < 0x80 && std::isalnum(static_cast<int>(c)) != 0);
    });
    const std::size_t baseEnd = p;
    if (n < 1 || n > 20 || !OpenParen(s, p)) return false;
    const std::size_t readStart = p;
    const std::size_t k = Take(s, p, 31, [](char32_t c) { return IsHiragana(c) || IsKatakana(c) || c == U'・'; });
    const std::size_t readEnd = p;
    if (k < 1 || k > 30 || !CloseParen(s, p)) return false;
    m.begin = pos;
    m.end = p;
    m.base = s.substr(baseStart, baseEnd - baseStart);
    m.reading = s.substr(readStart, readEnd - readStart);
    return true;
}

// ([一-龥々〆ヶ]+)《([^《》]{1,30})》
bool KanjiRuby(std::string_view s, std::size_t pos, RubyMatch& m) {
    std::size_t p = pos;
    const std::size_t n = Take(s, p, s.size(), IsRubyKanji);
    const std::size_t baseEnd = p;
    if (n < 1 || !Lit(s, p, "《")) return false;
    const std::size_t readStart = p;
    const std::size_t k = Take(s, p, 31, NotBracket);
    const std::size_t readEnd = p;
    if (k < 1 || k > 30 || !Lit(s, p, "》")) return false;
    m.begin = pos;
    m.end = p;
    m.base = s.substr(pos, baseEnd - pos);
    m.reading = s.substr(readStart, readEnd - readStart);
    return true;
}

// (?:<rp>[^<]*</rp>)? at p: skipped when present.
void SkipRp(std::string_view s, std::size_t& p) {
    std::size_t q = p;
    if (!Lit(s, q, "<rp>")) return;
    const std::size_t close = s.find('<', q);
    if (close != std::string_view::npos && s.substr(close).starts_with("</rp>")) p = close + 5;
}

// <ruby>(?:<rb>)?([^<]{1,20})(?:</rb>)?(?:<rp>[^<]*</rp>)?<rt>([^<]{1,30})</rt>(?:<rp>[^<]*</rp>)?</ruby>
bool HtmlRuby(std::string_view s, std::size_t pos, RubyMatch& m) {
    std::size_t p = pos;
    if (!Lit(s, p, "<ruby>")) return false;
    Lit(s, p, "<rb>");
    const std::size_t baseStart = p;
    const std::size_t n = Take(s, p, 21, NotAngle);
    const std::size_t baseEnd = p;
    if (n < 1 || n > 20) return false;
    Lit(s, p, "</rb>");
    SkipRp(s, p);
    if (!Lit(s, p, "<rt>")) return false;
    const std::size_t readStart = p;
    const std::size_t k = Take(s, p, 31, NotAngle);
    const std::size_t readEnd = p;
    if (k < 1 || k > 30 || !Lit(s, p, "</rt>")) return false;
    SkipRp(s, p);
    if (!Lit(s, p, "</ruby>")) return false;
    m.begin = pos;
    m.end = p;
    m.base = s.substr(baseStart, baseEnd - baseStart);
    m.reading = s.substr(readStart, readEnd - readStart);
    return true;
}

// ([一-龥々〆ヶ]{1,6}[ァ-ヺー]{0,12}|[ァ-ヺー]{2,12})[（(]([ぁ-ゖー・ 　]{2,16})[）)]
// The base alternatives are tried in order, each with its counts from the longest.
bool GlossAt(std::string_view s, std::size_t pos, RubyMatch& m) {
    std::vector<std::size_t> baseEnds;
    std::vector<std::size_t> kanjiEnds{pos};
    for (std::size_t p = pos; kanjiEnds.size() <= 6 && p < s.size() && IsKanji(RuneAt(s, p).cp);) {
        p += RuneAt(s, p).size;
        kanjiEnds.push_back(p);
    }
    for (std::size_t k = kanjiEnds.size(); k-- > 1;) {
        std::vector<std::size_t> kataEnds{kanjiEnds[k]};
        for (std::size_t p = kanjiEnds[k]; kataEnds.size() <= 12 && p < s.size() && IsGlossKatakana(RuneAt(s, p).cp);) {
            p += RuneAt(s, p).size;
            kataEnds.push_back(p);
        }
        for (std::size_t j = kataEnds.size(); j-- > 0;) baseEnds.push_back(kataEnds[j]);
    }
    std::vector<std::size_t> kataOnly{pos};
    for (std::size_t p = pos; kataOnly.size() <= 12 && p < s.size() && IsGlossKatakana(RuneAt(s, p).cp);) {
        p += RuneAt(s, p).size;
        kataOnly.push_back(p);
    }
    for (std::size_t j = kataOnly.size(); j-- > 2;) baseEnds.push_back(kataOnly[j]);

    for (const std::size_t baseEnd : baseEnds) {
        if (baseEnd >= s.size()) continue;
        std::size_t q = baseEnd;
        if (s.substr(q).starts_with("（")) {
            q += std::string_view("（").size();
        } else if (s[q] == '(') {
            q++;
        } else {
            continue;
        }
        const std::size_t readStart = q;
        const std::size_t readMax = Take(s, q, 16, IsGlossKana);
        for (std::size_t len = readMax; len >= 2; len--) {
            const std::size_t e = Advance(s, readStart, len);
            if (e >= s.size()) continue;
            std::size_t close = 0;
            if (s.substr(e).starts_with("）")) {
                close = std::string_view("）").size();
            } else if (s[e] == ')') {
                close = 1;
            } else {
                continue;
            }
            m.begin = pos;
            m.end = e + close;
            m.base = s.substr(pos, baseEnd - pos);
            m.reading = s.substr(readStart, e - readStart);
            return true;
        }
    }
    return false;
}

bool IsSmallKana(char32_t c) { return InRunes(c, "ぁぃぅぇぉゃゅょゎゕゖァィゥェォャュョヮヵヶ"); }

}  // namespace

std::size_t ReadingMorae(std::string_view s) {
    bool hiragana = false, katakana = false, gap = true;
    std::size_t morae = 0;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = RuneAt(s, i);
        i += r.size;
        if (r.cp == U'・' || r.cp == U' ' || r.cp == U'　') {
            if (gap) return 0;
            gap = true;
            continue;
        }
        gap = false;
        if (r.cp == U'ー') {
            morae++;
        } else if (IsHiragana(r.cp)) {
            hiragana = true;
            if (!IsSmallKana(r.cp)) morae++;
        } else if (IsKatakana(r.cp) && r.cp != U'ヶ') {
            katakana = true;
            if (!IsSmallKana(r.cp)) morae++;
        } else {
            return 0;
        }
    }
    if (gap || (hiragana && katakana)) return 0;
    return morae;
}

namespace {

// ([一-龥々〆ヶ]+)[（(]([ぁ-ゖー・ 　]{1,16}|[ァ-ヺー・ 　]{1,16})[）)], at most 4 morae for each
// kanji and one more; or ([一-龥々〆ヶ]+)「([ぁ-ゖー]{1,16})」 before the hiragana the word
// goes on with (全「すべ」て), or before a particle after one kanji (心「こころ」に; not a
// name quoted after a noun, 戦艦「やまと」の, nor a quote before と).
bool ReadingAt(std::string_view s, std::size_t pos, RubyMatch& m) {
    std::size_t p = pos;
    const std::size_t kanji = Take(s, p, s.size(), IsRubyKanji);
    const std::size_t baseEnd = p;
    if (kanji < 1) return false;
    const bool bracket = Lit(s, p, "「");
    if (!bracket && !OpenParen(s, p)) return false;
    const std::size_t readStart = p;
    const std::size_t k = Take(s, p, 17, [](char32_t c) { return NotParen(c) && c != U'「' && c != U'」'; });
    const std::size_t readEnd = p;
    if (k < 1 || k > 16 || !(bracket ? Lit(s, p, "」") : CloseParen(s, p))) return false;
    const std::string_view reading = s.substr(readStart, readEnd - readStart);
    if (bracket) {
        if (!AllRunes(reading, [](char32_t c) { return IsHiragana(c) || c == U'ー'; })) return false;
        const char32_t next = p < s.size() ? RuneAt(s, p).cp : 0;
        if (!IsHiragana(next) || next == U'と') return false;
        if (kanji > 1 && InRunes(next, "のにをはがでへもかや")) return false;
    }
    const std::size_t morae = ReadingMorae(reading);
    if (morae < 1 || morae > 4 * kanji + 1) return false;
    m.begin = pos;
    m.end = p;
    m.base = s.substr(pos, baseEnd - pos);
    m.reading = reading;
    return true;
}

}  // namespace

bool FindReading(std::string_view s, std::size_t from, RubyMatch& m) {
    for (std::size_t pos = from; pos < s.size(); pos += RuneAt(s, pos).size) {
        // A match starts a kanji run.
        if (pos > 0 && IsRubyKanji(RuneBefore(s, pos).cp)) continue;
        if (ReadingAt(s, pos, m)) return true;
    }
    return false;
}

bool FindRuby(std::string_view s, std::size_t from, RubyMatch& m) {
    for (std::size_t pos = from; pos < s.size(); pos += RuneAt(s, pos).size) {
        if (BarRuby(s, pos, m) || BarParenRuby(s, pos, m) || KanjiRuby(s, pos, m) || HtmlRuby(s, pos, m)) return true;
    }
    return false;
}

bool FindGloss(std::string_view s, std::size_t from, RubyMatch& m) {
    for (std::size_t pos = from; pos < s.size(); pos += RuneAt(s, pos).size) {
        if (GlossAt(s, pos, m)) return true;
    }
    return false;
}

}  // namespace internal

std::string StripRuby(std::string_view line) {
    if (line.find("《") == std::string_view::npos && line.find('<') == std::string_view::npos &&
        line.find('|') == std::string_view::npos && line.find("｜") == std::string_view::npos) {
        return std::string(line);
    }
    std::string out;
    std::size_t at = 0;
    internal::RubyMatch m;
    for (std::size_t from = 0; internal::FindRuby(line, from, m); from = m.end) {
        out.append(line.substr(at, m.begin - at));
        out.append(m.base);
        at = m.end;
    }
    out.append(line.substr(at));
    return out;
}

}  // namespace hdTrans::names
