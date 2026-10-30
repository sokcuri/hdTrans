#pragma once
// External word boundaries (the 0.8 analyzer). When an oracle is set, the word search keeps only
// items that end where the analyzer ends a word; a word the dictionary cannot end there keeps the
// first reading.
#include <cstdint>
#include <string_view>
#include <vector>

namespace hdTrans::word {

// Fills ends[i] for the byte offsets i of text (0..size): 0 where no word may end, else the class of
// the analyzer word that starts there ('n' noun, 'v' verb, 'a' adjective, 'k' adjectival noun,
// 'd' adverb, conjunction or interjection, 'm' adnominal, 'p' particle, 'x' auxiliary, 'f' prefix,
// 'z' suffix, 's' symbol, 'u' other) or 'e' at the end. Returns false when it has no opinion.
using BoundaryFn = bool (*)(void* ctx, std::string_view text, std::vector<uint8_t>* ends);
void SetBoundaryOracle(BoundaryFn fn, void* ctx);
bool QueryBoundaries(std::string_view text, std::vector<uint8_t>* ends);

}  // namespace hdTrans::word
