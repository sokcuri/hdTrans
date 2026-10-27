#pragma once
// Character sets of the CP932 repertoire used by the character classes.
#include <cstdint>
#include <span>

namespace hdTrans::tokenizer {

// The Han characters of CP932 that are not kanji: the IBM and NEC extension kanji and
// 仝 々. Kanji are the Han characters outside this set.
bool IsNonKanjiHan(char32_t c);

// The double-byte characters of CP932 that are neither Han characters nor kana:
// letters of other scripts, box drawing, full-width ASCII, symbols.
bool IsCp932Symbol(char32_t c);

// The characters whose Shift-JIS code has a lead byte of 0xE0 or above: the kanji of
// JIS X 0208 rows 63-84 and the NEC and IBM extension characters.
bool IsSjisHighLead(char32_t c);

// The Han characters Japanese text is written in: those of JIS X 0213 (planes 1 and 2)
// and of CP932, and every character below U+3400 (々 〆 〇, radicals). A Han character outside
// it is a Chinese form (们, 说, 这).
bool IsJapaneseHan(char32_t c);

namespace internal {

// Sorted, each character once.
std::span<const char32_t> NonKanjiHanTable();
std::span<const char32_t> Cp932SymbolTable();
std::span<const char32_t> SjisHighLeadTable();
std::span<const uint64_t> JapaneseHanBmpBits();
std::span<const uint64_t> JapaneseHanCompatBits();
std::span<const char32_t> JapaneseHanSupplementaryTable();

}  // namespace internal

}  // namespace hdTrans::tokenizer
