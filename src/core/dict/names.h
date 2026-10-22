#pragma once
// Name-layer helpers of loading (names.cpp).
#include <cstddef>

#include "core/dict/codes.h"
#include "core/dict/dict.h"
#include "core/dict/layer.h"

namespace hdTrans::dict::internal {

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
NameKeyCounts DropNameKeys(Row* rows, std::size_t count, const Dict& base, const CodeTable& codes);

}  // namespace hdTrans::dict::internal
