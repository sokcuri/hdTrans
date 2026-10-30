#pragma once
// The 0.8 analyzer as the word search's segmenter (build option HDTRANS_ANALYZER_HOOK).
#include <string>

#include "core/word/boundaries.h"

namespace hdTrans::cli {

struct AnalyzerOptions {
    float slack = 2.0f;     // a cut within this many nats of the best path stays allowed
    bool lemma = false;     // prefer the candidates the kanji key of the analyzer's word shares
    bool respell = false;   // spell kana homographs in kanji before the search (measured worse)
    bool reject = false;    // block lexicon words the analyzer rejected (measured no gain)
    bool particle = false;  // block content readings of one analyzer particle (measured no gain)
};

// Loads DIR/core.bin (and DIR/long.bin when present); null and err on failure.
const word::Segmenter* LoadAnalyzer(const std::string& dir, const AnalyzerOptions& options, std::string* err);

}  // namespace hdTrans::cli
