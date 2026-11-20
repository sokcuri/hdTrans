#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::dict {

// The edge costs of the segmentation lattice (word/segcost.cpp), read from the dictionary file of
// the layer seg-costs (dicts/seg-costs.tsv: name, value, reason). Every value is hand-set; a changed
// value is a data change. Without the file the lattice is off and the word search decides alone.
enum SegCost : int {
    kSegWord,            // every word
    kSegParticle,        // a particle spelled in kana
    kSegLoneKanji,       // a word of one kanji
    kSegLoneKana,        // a content word of one hiragana
    kSegKataLoan,        // a dictionary word of three or more katakana, in place of word
    kSegShortKataPiece,  // a dictionary word of one katakana, or a proper noun of two, inside a longer katakana run
    kSegKataSound,       // one katakana no word covers
    kSegProper,          // a proper noun row of the base dictionary not in katakana
    kSegNameRow,         // a reading of the name layer
    kSegKanaRow,         // a reading of the kana layer
    kSegStemOnly,        // a verb or adjective stem with no ending
    kSegUnknownChar,     // one character no word covers
    kSegSoundRun,        // a whole katakana run read by its sound
    kSegSoundChar,       // and per katakana of it
    kSegLatinRun,        // a run of Latin letters read as one word
    kSegLongMark,        // a ー standing alone at the start or the end of a katakana run
    kSegKataSpeechCut,   // in the reading of katakana speech (SegmentRun's scale), a word that cuts a katakana word
    kSegCostCount
};

// The names of the costs in the file, in the order of SegCost.
// The largest value a seg cost may take: a path of a long token sums many edges, and stays far
// below the lattice's infinity (INT_MAX / 4) with every value at most this.
inline constexpr int kSegCostMax = 10000;

inline constexpr std::array<std::string_view, kSegCostCount> kSegCostNames = {
    "word",       "particle",  "lone-kanji",   "lone-kana", "kata-loan",
    "short-kata-piece", "kata-sound", "proper", "name-row", "kana-row", "stem-only",
    "unknown-char", "sound-run", "sound-char", "latin-run", "long-mark", "kata-speech-cut"};

struct SegCosts {
    bool loaded = false;
    std::array<int, kSegCostCount> values{};
    int operator[](SegCost c) const { return values[static_cast<std::size_t>(c)]; }
};

// Reads the rows of a seg-costs file (its header removed) into out; every name once, each a
// whole number. Returns the problems as line and message.
std::vector<std::pair<int, std::string>> ParseSegCosts(std::string_view text, SegCosts& out);

}  // namespace hdTrans::dict
