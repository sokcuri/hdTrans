#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hd8::ana::internal {

// Decodes one code point at s[i]; advances i. Invalid bytes decode as U+FFFD, one byte each.
inline char32_t DecodeUtf8(std::string_view s, size_t& i) {
  auto b = static_cast<unsigned char>(s[i]);
  if (b < 0x80) { ++i; return b; }
  int n = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : -1;
  if (n < 0 || i + static_cast<size_t>(n) >= s.size()) { ++i; return 0xFFFD; }
  char32_t c = b & (0x3F >> n);
  for (int k = 1; k <= n; ++k) {
    auto x = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
    if ((x & 0xC0) != 0x80) { ++i; return 0xFFFD; }
    c = (c << 6) | (x & 0x3F);
  }
  i += static_cast<size_t>(n) + 1;
  return c;
}

inline void AppendUtf8(std::string& out, char32_t c) {
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

inline std::u32string ToU32(std::string_view s) {
  std::u32string out;
  size_t i = 0;
  while (i < s.size()) out += DecodeUtf8(s, i);
  return out;
}

inline std::string ToUtf8(std::u32string_view s) {
  std::string out;
  for (char32_t c : s) AppendUtf8(out, c);
  return out;
}

inline bool IsHiragana(char32_t c) { return c >= 0x3041 && c <= 0x309F; }
inline bool IsKatakana(char32_t c) { return (c >= 0x30A1 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF); }
inline bool IsKanji(char32_t c) {
  return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF) ||
         c == 0x3005 || c == 0x3006 || c == 0x30F6 || (c >= 0x20000 && c <= 0x2FFFF);
}
inline bool IsSpace(char32_t c) { return c == ' ' || c == 0x3000 || c == '\t' || c == 0xA0; }

inline char32_t ToKatakana(char32_t c) { return (c >= 0x3041 && c <= 0x3096) ? c + 0x60 : c; }

}  // namespace hd8::ana::internal
