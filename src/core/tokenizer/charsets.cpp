#include "core/tokenizer/charsets.h"

#include <algorithm>

namespace hdTrans::tokenizer {

bool IsNonKanjiHan(char32_t c) {
    const auto t = internal::NonKanjiHanTable();
    return std::binary_search(t.begin(), t.end(), c);
}

bool IsCp932Symbol(char32_t c) {
    const auto t = internal::Cp932SymbolTable();
    return std::binary_search(t.begin(), t.end(), c);
}

bool IsSjisHighLead(char32_t c) {
    const auto t = internal::SjisHighLeadTable();
    return std::binary_search(t.begin(), t.end(), c);
}

bool IsJapaneseHan(char32_t c) {
    const auto bit = [](std::span<const uint64_t> bits, char32_t at) { return (bits[at / 64] >> (at % 64) & 1) != 0; };
    if (c < 0x3400) return true;
    if (c >= 0x3400 && c < 0xA000) return bit(internal::JapaneseHanBmpBits(), c - 0x3400);
    if (c >= 0xF900 && c < 0xFB00) return bit(internal::JapaneseHanCompatBits(), c - 0xF900);
    const auto t = internal::JapaneseHanSupplementaryTable();
    return std::binary_search(t.begin(), t.end(), c);
}

}  // namespace hdTrans::tokenizer
