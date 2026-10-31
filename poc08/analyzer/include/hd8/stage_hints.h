// Segmentation hints of the 0.8 analyzer for the 0.7.x hook (poc08/bridge), on top of the stage data.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace hd08 {

struct AnalyzerData;

// Byte offsets of `text` where a word may end: the analyzer's token ends, plus every cut that a path whose cost
// is within `slackUnits` units (ja/prefs.tsv pf.units) of the best path makes. With a slack below one rank-3
// count, only the boundaries that connection rules or rank 1-3 preferences decided are firm.
void AnalyzerAllowedEnds(const AnalyzerData& data, std::string_view text, float slackUnits,
                         std::vector<uint32_t>* ends);

}  // namespace hd08
