#pragma once
// Text measures of the composer: widths in the length unit of its rules and cuts at
// character boundaries.
#include <string_view>

#include "core/text/lifetime.h"

namespace hdTrans::output::internal {

// 1 for ASCII and half-width katakana (U+FF61..U+FF9F), 2 for every other character.
int Width(char32_t r);
int WidthOf(std::string_view s);

struct Cut {
    std::string_view head;
    std::string_view tail;
};

// s split before its key: the last n ASCII characters when they all are, otherwise
// the last n/2 characters.
Cut CutLast(std::string_view s HDTRANS_LIFETIMEBOUND, int n);

// The first character of s as head when it is ASCII (an ending class); otherwise head
// is empty and tail is s.
Cut FirstByte(std::string_view s HDTRANS_LIFETIMEBOUND);

// s without its first n width units; a character that straddles the cut is dropped
// too.
std::string_view SkipWidth(std::string_view s HDTRANS_LIFETIMEBOUND, int n);

// s split after its first n width units; a character that straddles the cut goes to
// the tail.
Cut CutFirst(std::string_view s HDTRANS_LIFETIMEBOUND, int n);

// The text of s from width unit start, count units long.
std::string_view MidWidth(std::string_view s HDTRANS_LIFETIMEBOUND, int start, int count);

// s[i], or 0 outside s.
inline char At(std::string_view s, int i) {
    return i < 0 || static_cast<size_t>(i) >= s.size() ? 0 : s[static_cast<size_t>(i)];
}

// The first byte of s, 0 for an empty string.
inline char First(std::string_view s) { return s.empty() ? 0 : s[0]; }

inline bool Contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// Whether s contains a Hangul syllable.
bool HasSyllable(std::string_view s);

}  // namespace hdTrans::output::internal
