#include <algorithm>
#include <vector>

#include "core/pipeline/protect.h"
#include "core/pipeline/restore.h"
#include "core/pipeline/runes.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/tokenizer/canonical.h"
#include "core/tokenizer/charsets.h"

namespace hdTrans::pipeline::internal {

namespace {

using output::SpanKind;


struct Match {
    size_t length = 0;
    SpanKind kind = output::SpanOpen;
};

bool IsAsciiLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }
bool IsAsciiAlnum(char c) { return IsAsciiLetter(c) || IsAsciiDigit(c); }
bool IsHexDigit(char c) { return IsAsciiDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

bool EqualFoldAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
        if (x != y) return false;
    }
    return true;
}

// <BR> and <LI> are left to the tokenizer, which ends the sentence at them. Brackets around text
// with sentence marks (<Come On！ジャパニーズエール>) are no tag.
Match TagSpan(std::string_view s) {
    if (s.size() < 2 || s[0] != '<' || !(IsAsciiLetter(s[1]) || s[1] == '!' || s[1] == '/' || s[1] == '?')) return {};
    const size_t end = s.find_first_of("<>", 1);
    if (end == std::string_view::npos || s[end] != '>') return {};
    const std::string_view tag = s.substr(0, end + 1);
    if (text::RuneCount(tag) > 1024 || EqualFoldAscii(tag, "<BR>") || EqualFoldAscii(tag, "<LI>")) return {};
    for (const std::string_view mark : {"。", "、", "！", "？", "「", "」", "…"}) {
        if (tag.find(mark) != std::string_view::npos) return {};
    }
    return {tag.size(), s[1] == '/' ? output::SpanClose : output::SpanOpen};
}

// [name], [/name] or [name=value]: a letter, then letters, digits and _; the value
// holds no ] and no line feed. The same in braces ({rb}, {/rt}, {k=-5.0}), the tags of
// script markup.
Match BbCodeSpan(std::string_view s) {
    size_t i = 0;
    if (i >= s.size() || (s[i] != '[' && s[i] != '{')) return {};
    const char close = s[i] == '[' ? ']' : '}';
    i++;
    if (i < s.size() && s[i] == '/') i++;
    if (i >= s.size() || !IsAsciiLetter(s[i])) return {};
    i++;
    while (i < s.size() && (IsAsciiAlnum(s[i]) || s[i] == '_')) i++;
    if (i < s.size() && s[i] == '=') {
        const size_t end = s.find_first_of(close == ']' ? "]\n" : "}\n", i + 1);
        if (end != std::string_view::npos && s[end] == close) i = end;
    }
    if (i >= s.size() || s[i] != close) return {};
    const std::string_view m = s.substr(0, i + 1);
    if (text::RuneCount(m) > 128) return {};
    return {m.size(), m[1] == '/' ? output::SpanClose : output::SpanOpen};
}

// The length of the run of characters satisfying in at s[i], when it is min to max
// long and ';' follows it; 0 otherwise.
template <class Pred>
size_t EntityRun(std::string_view s, size_t i, size_t min, size_t max, Pred in) {
    size_t n = 0;
    while (i + n < s.size() && in(s[i + n])) n++;
    if (n < min || n > max || i + n >= s.size() || s[i + n] != ';') return 0;
    return n;
}

// &name; (a letter and 1 to 31 letters or digits), &#ddd; (1 to 7 digits) or &#xhh;
// (1 to 6 hex digits).
Match EntitySpan(std::string_view s) {
    if (s.empty() || s[0] != '&' || s.size() < 2) return {};
    if (IsAsciiLetter(s[1])) {
        if (const size_t n = EntityRun(s, 2, 1, 31, IsAsciiAlnum)) return {2 + n + 1, output::SpanOpen};
        return {};
    }
    if (s[1] != '#') return {};
    if (const size_t n = EntityRun(s, 2, 1, 7, IsAsciiDigit)) return {2 + n + 1, output::SpanOpen};
    if (s.size() > 2 && s[2] == 'x') {
        if (const size_t n = EntityRun(s, 3, 1, 6, IsHexDigit)) return {3 + n + 1, output::SpanOpen};
    }
    return {};
}

Match MarkerSpan(std::string_view s, std::span<const std::string> markers) {
    size_t n = 0;
    for (const std::string& m : markers) {
        if (m.size() > n && s.starts_with(m)) n = m.size();
    }
    return {n, output::SpanOpen};
}

// The stutter marks of one stutter with their separators, within its first 64 bytes.
Match StutterSpan(std::string_view s) {
    std::vector<text::Rune> rs;
    for (std::string_view w = s.substr(0, 64); !w.empty();) {
        rs.push_back(text::DecodeRune(w));
        w.remove_prefix(rs.back().size);
    }
    std::u32string cps;
    for (const text::Rune& r : rs) cps.push_back(r.cp);
    size_t n = 0;
    while (n < rs.size() && (rs[n].cp == kStutterMark || rs[n].cp == kStutterRest)) n += 1 + StutterSeparator(cps, n + 1);
    size_t bytes = 0;
    for (size_t k = 0; k < n; k++) bytes += rs[k].size;
    return {bytes, output::SpanOpen};
}

// A drawl span: kDrawlMark, kLeadDrawlMark or kPastLeadDrawlMark, one or more pause marks
// (…‥・―─) and the same mark.
Match DrawlSpan(std::string_view s) {
    const text::Rune open = text::DecodeRune(s);
    if (open.cp != kDrawlMark && open.cp != kLeadDrawlMark && open.cp != kPastLeadDrawlMark) return {};
    for (size_t n = open.size; n < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(n));
        n += r.size;
        if (r.cp == open.cp) return {n > 2 * open.size ? n : 0, output::SpanOpen};
        if (!RuneIn(r.cp, U"…‥・―─")) return {};
    }
    return {};
}

// An emphasis mark: kEmphasisOpen and kStressMark open a span, kEmphasisClose, kLengthenMark
// and a run of kDrawnMark or a cry mark (output::IsCryMark), with the kClipMark of a clipped one,
// close one.
Match EmphasisSpan(std::string_view s) {
    const text::Rune r = text::DecodeRune(s);
    if (r.cp == kEmphasisOpen || r.cp == output::kStressMark) return {r.size, output::SpanOpen};
    if (r.cp == kEmphasisClose || r.cp == kLengthenMark || r.cp == output::kSilentComma) return {r.size, output::SpanClose};
    if (r.cp == output::kDrawnMark || output::IsCryMark(r.cp)) {
        size_t n = 0;
        while (n < s.size() && text::DecodeRune(s.substr(n)).cp == r.cp) n += r.size;
        if (n < s.size() && text::DecodeRune(s.substr(n)).cp == output::kClipMark) n += text::DecodeRune(s.substr(n)).size;
        return {n, output::SpanClose};
    }
    return {};
}

bool IsHangul(char32_t r) {
    return (r >= 0x1100 && r <= 0x11FF) || (r >= 0x3131 && r <= 0x318E) || (r >= 0xA960 && r <= 0xA97F) ||
           (r >= 0xAC00 && r <= 0xD7A3) || (r >= 0xD7B0 && r <= 0xD7FF) || (r >= 0xFFA0 && r <= 0xFFDC);
}

bool IsHangulOrMask(char32_t r) {
    return IsHangul(r) || IsMaskRune(r) || r == output::kVowelMark || r == output::kConsonantMark || r == output::kRieulMark;
}

// Hangul runs, with the masks and particle marks in them or after them (자○, 오○니),
// joined by ASCII or full-width spaces.
Match HangulSpan(std::string_view s) {
    if (!IsHangul(text::DecodeRune(s).cp)) return {0, output::SpanWord};
    size_t n = RunOf(s, IsHangulOrMask);
    while (n > 0) {
        const size_t gap = RunOf(s.substr(n), [](char32_t r) { return r == U' ' || r == U'　'; });
        const size_t more = IsHangul(text::DecodeRune(s.substr(n + gap)).cp) ? RunOf(s.substr(n + gap), IsHangulOrMask) : 0;
        if (gap == 0 || more == 0) break;
        n += gap + more;
    }
    return {n, output::SpanWord};
}

// The Chinese run of runs (ChineseRuns) that starts at the start of s, before is the text
// before s; it must end within s.
Match ChineseSpan(std::string_view before, std::string_view s, std::span<const std::pair<size_t, size_t>> runs) {
    const auto it = std::lower_bound(runs.begin(), runs.end(), std::pair<size_t, size_t>{before.size(), 0});
    if (it == runs.end() || it->first != before.size() || it->second - it->first > s.size()) return {};
    return {it->second - it->first, output::SpanWord};
}

// A run of foreign characters other than white space, or of ◯, which has no Korean form
// of its own; a ゔ in it takes the small vowel after it (ゔぁ, ゔぉ).
Match ForeignSpan(std::string_view s) {
    size_t n = 0;
    for (;;) {
        const size_t k = RunOf(s.substr(n), [](char32_t r) { return r == U'◯' || (!text::IsSpace(r) && IsForeign(r)); });
        if (k == 0) break;
        n += k;
        if (!s.substr(0, n).ends_with("ゔ")) break;
        const text::Rune r = text::DecodeRune(s.substr(n));
        if (!RuneIn(r.cp, U"ぁぃぅぇぉ")) break;
        n += r.size;
    }
    return {n, output::SpanWord};
}

// A run of marks of tone (♪ and kToneMarks) that names a thing: after の and before a
// particle (あなたの♡を, 私の♪は). A particle other than を counts only before a character
// that is not hiragana, so that a word after a sentence-final の (なの♡もっと) is not one.
Match ToneNounSpan(std::string_view before, std::string_view s) {
    if (!before.ends_with("の")) return {};
    const size_t n = RunOf(s, [](char32_t r) { return r == U'♪' || RuneIn(r, kToneMarks); });
    if (n == 0) return {};
    const text::Rune particle = text::DecodeRune(s.substr(n));
    if (particle.cp == U'を') return {n, output::SpanWord};
    if (!RuneIn(particle.cp, U"がはにへでもと")) return {};
    const char32_t after = text::DecodeRune(s.substr(n + particle.size)).cp;
    if (after >= U'ぁ' && after <= U'ゖ') return {};
    return {n, output::SpanWord};
}

enum class SpanClass { Tag, BbCode, Entity, Style, Marker, Stutter, Drawl, Emphasis, Hangul, Chinese, Foreign, ToneNoun };

constexpr SpanClass kClasses[] = {SpanClass::Tag,     SpanClass::BbCode, SpanClass::Entity,   SpanClass::Style,
                                  SpanClass::Marker,  SpanClass::Stutter, SpanClass::Drawl,   SpanClass::Emphasis,
                                  SpanClass::Hangul,  SpanClass::Chinese, SpanClass::Foreign, SpanClass::ToneNoun};

Match MatchClass(SpanClass c, std::string_view before, std::string_view s, std::span<const std::string> markers,
                 std::span<const std::pair<size_t, size_t>> chinese) {
    switch (c) {
        case SpanClass::Tag: return TagSpan(s);
        case SpanClass::BbCode: return BbCodeSpan(s);
        case SpanClass::Entity: return EntitySpan(s);
        case SpanClass::Style: return {StyleCodeLength(s, before.empty() ? '\0' : before.back()), output::SpanOpen};
        case SpanClass::Marker: return MarkerSpan(s, markers);
        case SpanClass::Stutter: return StutterSpan(s);
        case SpanClass::Drawl: return DrawlSpan(s);
        case SpanClass::Emphasis: return EmphasisSpan(s);
        case SpanClass::Hangul: return HangulSpan(s);
        case SpanClass::Chinese: return ChineseSpan(before, s, chinese);
        case SpanClass::Foreign: return ForeignSpan(s);
        case SpanClass::ToneNoun: return ToneNounSpan(before, s);
    }
    return {};
}

struct Cut {
    size_t start = 0;
    size_t end = 0;
    SpanKind kind = output::SpanOpen;
};

}  // namespace

size_t StyleCodeLength(std::string_view s, char before) {
    if (s.starts_with("%f")) {
        const size_t end = s.find_first_of(";%\n", 2);
        return end != std::string_view::npos && end <= 2 + 64 && s[end] == ';' ? end + 1 : 0;
    }
    if (s.empty() || s[0] != '#' || before == '&') return 0;
    size_t n = 1;
    while (n < s.size() && n <= 8 && IsHexDigit(s[n])) n++;
    const size_t digits = n - 1;
    if (n >= s.size() || s[n] != ';' || (digits != 0 && digits != 6 && digits != 8)) return 0;
    return n + 1;
}

bool IsForeign(char32_t r) {
    if (r < 0x80 || (r >= 0xFF61 && r <= 0xFF9F) || (r >= U'ぁ' && r <= U'ん') || (r >= U'ァ' && r <= U'ヺ') ||
        r == U'ー' || r == U'—' || text::IsKanji(r) || RuneIn(r, kToneMarks)) {
        return false;
    }
    return !tokenizer::IsCp932Symbol(r) && !tokenizer::CanonicalForm(r);
}

output::Pieces CutSpans(std::string_view text, std::span<const std::string> markers) {
    std::vector<Cut> cuts;
    const std::vector<std::pair<size_t, size_t>> chinese = ChineseRuns(text);
    for (const SpanClass c : kClasses) {
        size_t next = 0;  // the first cut that ends after i
        for (size_t i = 0; i < text.size();) {
            while (next < cuts.size() && cuts[next].end <= i) next++;
            if (next < cuts.size() && cuts[next].start <= i) {
                i = cuts[next].end;
                continue;
            }
            const size_t free = next < cuts.size() ? cuts[next].start : text.size();
            if (const Match m = MatchClass(c, text.substr(0, i), text.substr(i, free - i), markers, chinese); m.length > 0) {
                cuts.push_back({i, i + m.length, m.kind});
                i += m.length;
                continue;
            }
            i += text::DecodeRune(text.substr(i)).size;
        }
        std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.start < b.start; });
    }
    output::Pieces out;
    size_t at = 0;
    for (const Cut& c : cuts) {
        output::Add(out, text.substr(at, c.start - at));
        out.push_back({std::string(text.substr(c.start, c.end - c.start)), c.kind});
        at = c.end;
    }
    output::Add(out, text.substr(at));
    return out;
}

}  // namespace hdTrans::pipeline::internal
