// N: layout normalization. Width (half-width katakana and punctuation) and ruby or parenthesised readings.
// Spaces are kept; the lattice treats them as boundary candidates.
#pragma once

#include <string_view>
#include <vector>

#include "hd8/analyzer.h"

namespace hd8::ana::internal {

struct NChar {
  char32_t c;
  uint32_t sb, se;  // source bytes this character stands for
};

void Normalize(std::string_view src, std::vector<NChar>* out, std::vector<NormEdit>* edits);

}  // namespace hd8::ana::internal
