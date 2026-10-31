// N: layout normalization. Width (half-width katakana and punctuation) and ruby or parenthesised readings.
// Spaces are kept; the lattice treats them as boundary candidates.
#pragma once

#include <string_view>
#include <vector>

#include "core/attach/analyzer.h"

namespace hdTrans::attach::ana::internal {

struct NChar {
    char32_t c;
    uint32_t sb, se;  // source bytes this character stands for
};

void Normalize(std::string_view src, std::vector<NChar>* out, std::vector<NormEdit>* edits);

}  // namespace hdTrans::attach::ana::internal
