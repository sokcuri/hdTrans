#pragma once
#include <cstdint>
#include <string_view>

namespace hd08 {

// Decodes the UTF-8 code point at s[i] and advances i; a stray byte decodes as itself.
inline uint32_t NextCodePoint(std::string_view s, size_t& i) {
    const auto b = static_cast<unsigned char>(s[i]);
    int n = b < 0x80 ? 0 : b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
    uint32_t cp = n == 0 ? b : n == 1 ? (b & 0x1Fu) : n == 2 ? (b & 0x0Fu) : (b & 0x07u);
    i++;
    while (n-- > 0 && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3Fu);
    return cp;
}

}  // namespace hd08
