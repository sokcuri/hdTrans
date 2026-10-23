#pragma once
// Hand-written text patterns. A pattern function tries one match of s at pos: the first
// alternative that lets the whole pattern match wins, greedy repetitions take the
// longest run that lets the rest match, ^ is position 0 and $ the end of s. Find and
// ReplaceAll scan rune by rune for the leftmost match; matches do not overlap.
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "core/text/lifetime.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline::internal {

struct Match {
    size_t begin = 0;
    size_t end = 0;
    std::string_view group[6];  // group[0] is the whole match; an unmatched group is empty
};

// Tries to match s at pos; on success sets m.end and the groups 1 and up.
using Pattern = bool (*)(std::string_view s, size_t pos, Match& m);

// The leftmost match of p in s at or after from.
bool Find(std::string_view s, size_t from, Pattern p, Match& m);

// s with every match of p replaced by replace(m); the scan resumes at the end of a match.
template <class Replace>
std::string ReplaceAll(std::string_view s, Pattern p, Replace replace) {
    std::string out;
    size_t at = 0;
    Match m;
    for (size_t from = 0; Find(s, from, p, m); from = m.end) {
        out.append(s.substr(at, m.begin - at));
        out += replace(m);
        at = m.end;
    }
    out.append(s.substr(at));
    return out;
}

// The groups of the leftmost match of p in the matched text m alone.
Match Rematch(const Match& m, Pattern p);

std::string Cat(std::initializer_list<std::string_view> parts);

inline text::Rune At(std::string_view s, size_t pos) { return text::DecodeRune(s.substr(pos)); }

inline bool In(char32_t c, std::u32string_view set) { return set.find(c) != std::u32string_view::npos; }

// Advances pos past lit when s continues with it at pos.
inline bool Lit(std::string_view s, size_t& pos, std::string_view lit) {
    if (!s.substr(pos).starts_with(lit)) return false;
    pos += lit.size();
    return true;
}

// Advances pos past one rune for which pred holds.
template <class Pred>
bool Take(std::string_view s, size_t& pos, Pred pred) {
    if (pos >= s.size()) return false;
    const text::Rune r = At(s, pos);
    if (!pred(r.cp)) return false;
    pos += r.size;
    return true;
}

inline bool OneOf(std::string_view s, size_t& pos, std::u32string_view set) {
    return Take(s, pos, [set](char32_t c) { return In(c, set); });
}

inline bool NoneOf(std::string_view s, size_t& pos, std::u32string_view set) {
    return Take(s, pos, [set](char32_t c) { return !In(c, set); });
}

// A predicate for the runes of set.
inline auto InSet(std::u32string_view set) {
    return [set](char32_t c) { return In(c, set); };
}

// A greedy repetition of min to max runes for which pred holds, at pos: rest(end) is
// tried for the longest run first, then for shorter ones; true when one succeeds.
template <class Pred, class Rest>
bool Repeat(std::string_view s, size_t pos, Pred pred, size_t min, size_t max, Rest rest) {
    std::vector<size_t> ends{pos};
    for (size_t p = pos; ends.size() <= max && Take(s, p, pred);) ends.push_back(p);
    for (size_t k = ends.size(); k-- > min;) {
        if (rest(ends[k])) return true;
    }
    return false;
}

constexpr size_t kUnbounded = static_cast<size_t>(-1);

inline std::string_view Span(std::string_view s HDTRANS_LIFETIMEBOUND, size_t from, size_t to) { return s.substr(from, to - from); }

// [ぁ-ゖ]
inline bool IsHiraganaClass(char32_t c) { return c >= 0x3041 && c <= 0x3096; }
// [一-龥々]
inline bool IsKanjiClass(char32_t c) { return (c >= 0x4E00 && c <= 0x9FA5) || c == 0x3005; }

}  // namespace hdTrans::pipeline::internal
