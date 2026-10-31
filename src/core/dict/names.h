#pragma once
// Name-layer helpers of loading (names.cpp).
#include <array>
#include <cstddef>
#include <string_view>

#include "core/dict/codes.h"
#include "core/dict/dict.h"
#include "core/dict/layer.h"

namespace hdTrans::dict::internal {

// Honorifics that follow a person's name.
inline constexpr std::array<std::string_view, 11> kNameHonorifics = {"さん", "ちゃん", "くん", "君", "様", "さま",
                                                                     "先輩", "先生", "殿", "氏", "たん"};

// Place-name semantic codes: QCT (city or region) or QNN (country). VPl alone is a
// class of common nouns (우물, 시장).
bool PlaceSense(const SemCodes& sem);

struct NameKeyCounts {
    int punct = 0;
    int inflected = 0;
    int longKorean = 0;
};

// Marks deleted the name-layer rows whose key holds 、 or 。 or is an inflected word,
// or whose Korean has more than twice as many syllables as the key has characters,
// plus four (longer ones render a work or a description), and counts them by kind. A
// person row of two hiragana or more is kept though its key reads as an inflected word
// (つぐみ, のぞみ): it takes part only where the key is confirmed.
// Cuts the title marks (!, ☆ and the like) off the end of the Korean of name-layer rows whose
// key ends in a Japanese character, which the text lacks there (ぼっちざろっく 봇치 더 록).
// Returns the number of rows changed.
int DropTargetMarks(Row* rows, std::size_t count);

NameKeyCounts DropNameKeys(Row* rows, std::size_t count, const Dict& base, const CodeTable& codes);

}  // namespace hdTrans::dict::internal
