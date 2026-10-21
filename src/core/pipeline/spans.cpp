#include <algorithm>
#include <vector>

#include "core/pipeline/protect.h"
#include "core/pipeline/restore.h"
#include "core/pipeline/runes.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
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

// <BR> and <LI> are left to the tokenizer, which ends the sentence at them.
Match TagSpan(std::string_view s) {
    if (s.size() < 2 || s[0] != '<' || !(IsAsciiLetter(s[1]) || s[1] == '!' || s[1] == '/' || s[1] == '?')) return {};
    const size_t end = s.find_first_of("<>", 1);
    if (end == std::string_view::npos || s[end] != '>') return {};
    const std::string_view tag = s.substr(0, end + 1);
    if (text::RuneCount(tag) > 1024 || EqualFoldAscii(tag, "<BR>") || EqualFoldAscii(tag, "<LI>")) return {};
    return {tag.size(), s[1] == '/' ? output::SpanClose : output::SpanOpen};
}

// [name], [/name] or [name=value]: a letter, then letters, digits and _; the value
// holds no ] and no line feed.
Match BbCodeSpan(std::string_view s) {
    size_t i = 0;
    if (i >= s.size() || s[i] != '[') return {};
    i++;
    if (i < s.size() && s[i] == '/') i++;
    if (i >= s.size() || !IsAsciiLetter(s[i])) return {};
    i++;
    while (i < s.size() && (IsAsciiAlnum(s[i]) || s[i] == '_')) i++;
    if (i < s.size() && s[i] == '=') {
        const size_t close = s.find_first_of("]\n", i + 1);
        if (close != std::string_view::npos && s[close] == ']') i = close;
    }
    if (i >= s.size() || s[i] != ']') return {};
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

// The length in characters of a stutter separator at rs[i]: 、 ， or a run of … ‥ ― ─ — or
// of two or more ・.
size_t StutterSeparator(const std::vector<text::Rune>& rs, size_t i) {
    if (i >= rs.size()) return 0;
    const char32_t c = rs[i].cp;
    switch (c) {
        case U'、':
        case U'，':
            return 1;
        case U'…':
        case U'‥':
        case U'―':
        case U'─':
        case U'—':
        case U'・': {
            size_t n = i;
            while (n < rs.size() && rs[n].cp == c) n++;
            if (c == U'・' && n - i < 2) return 0;
            return n - i;
        }
        default:
            return 0;
    }
}

// The stutter marks of one stutter with their separators, within its first 64 bytes.
Match StutterSpan(std::string_view s) {
    std::vector<text::Rune> rs;
    for (std::string_view w = s.substr(0, 64); !w.empty();) {
        rs.push_back(text::DecodeRune(w));
        w.remove_prefix(rs.back().size);
    }
    size_t n = 0;
    while (n < rs.size() && (rs[n].cp == kStutterMark || rs[n].cp == kStutterRest)) n += 1 + StutterSeparator(rs, n + 1);
    size_t bytes = 0;
    for (size_t k = 0; k < n; k++) bytes += rs[k].size;
    return {bytes, output::SpanOpen};
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

enum class SpanClass { Tag, BbCode, Entity, Marker, Stutter, Hangul, Foreign };

constexpr SpanClass kClasses[] = {SpanClass::Tag,     SpanClass::BbCode, SpanClass::Entity, SpanClass::Marker,
                                  SpanClass::Stutter, SpanClass::Hangul, SpanClass::Foreign};

Match MatchClass(SpanClass c, std::string_view s, std::span<const std::string> markers) {
    switch (c) {
        case SpanClass::Tag: return TagSpan(s);
        case SpanClass::BbCode: return BbCodeSpan(s);
        case SpanClass::Entity: return EntitySpan(s);
        case SpanClass::Marker: return MarkerSpan(s, markers);
        case SpanClass::Stutter: return StutterSpan(s);
        case SpanClass::Hangul: return HangulSpan(s);
        case SpanClass::Foreign: return ForeignSpan(s);
    }
    return {};
}

struct Cut {
    size_t start = 0;
    size_t end = 0;
    SpanKind kind = output::SpanOpen;
};

}  // namespace

bool IsForeign(char32_t r) {
    if (r < 0x80 || (r >= 0xFF61 && r <= 0xFF9F) || (r >= U'ぁ' && r <= U'ん') || (r >= U'ァ' && r <= U'ヺ') ||
        r == U'ー' || r == U'—' || text::IsKanji(r)) {
        return false;
    }
    return !tokenizer::IsCp932Symbol(r);
}

output::Pieces CutSpans(std::string_view text, std::span<const std::string> markers) {
    std::vector<Cut> cuts;
    for (const SpanClass c : kClasses) {
        size_t next = 0;  // the first cut that ends after i
        for (size_t i = 0; i < text.size();) {
            while (next < cuts.size() && cuts[next].end <= i) next++;
            if (next < cuts.size() && cuts[next].start <= i) {
                i = cuts[next].end;
                continue;
            }
            const size_t free = next < cuts.size() ? cuts[next].start : text.size();
            if (const Match m = MatchClass(c, text.substr(i, free - i), markers); m.length > 0) {
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
