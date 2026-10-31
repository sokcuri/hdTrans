// Input repair at the analyzer stage (ja/repair.tsv): the switch for StageA and the substitution of the
// repaired spans into the text another engine reads.
#pragma once

#include <string>
#include <string_view>

namespace hd08 {

struct AnalyzerData;

// Turns input repair on for RepairText (EnableRepair) or for StageA as well (EnableStageRepair, the native route:
// repaired tokens get Token.repair and the Repaired flag). Call once after LoadAnalyzer, before any stage runs;
// false when the analyzer data has no ja/repair.tsv. The bridge also declares these (the analyzer stub defines them).
bool EnableRepair(AnalyzerData* data);
bool EnableStageRepair(AnalyzerData* data);
bool RepairEnabled(const AnalyzerData& data);

// src with only its repaired spans replaced by their repaired spelling. Returns false, and leaves out
// alone, when the analyzer repaired nothing (always the case for clean text). `why`, when given,
// receives one line per repair ("rp.dakuten か→が on ドアが") after the check rows that fired.
bool RepairText(const AnalyzerData& data, std::string_view src, std::string* out, std::string* why);

}  // namespace hd08
