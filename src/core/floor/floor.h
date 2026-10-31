#pragma once
// The floor stage (spec/floor.md): checks that find Korean no correct translation contains in the
// composed words of a sentence, read with the engine's own items, and the readings the retry rules
// out to look for one without such a fault.
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/output/item.h"
#include "core/word/analyze.h"

namespace hdTrans::floor {

// One item of a composed sentence as the checks read it.
struct Word {
    const output::ResolvedItem* item = nullptr;
    std::string_view ko;      // its Korean as composed and spaced, without the space after it
    bool space = false;       // a space follows it
    std::string_view source;  // the Japanese it covers in its token (its conjugation ending included)
};

// A fault a check found.
struct Flag {
    std::string_view check;  // spec id (FLR-n)
    int word = -1;           // the word whose reading is at fault
    int last = -1;           // the last word of the span
    std::string ja;          // the span in Japanese and in Korean
    std::string ko;
};

// The faults of the words of one sentence, left to right.
void Check(std::span<const Word> words, std::vector<Flag>& out);

// What the retry compares of the words that read a span of the Japanese (spec FLR-1).
struct Reading {
    int words = 0;      // words with Korean
    int unread = 0;     // words without a dictionary reading: a kanji read by its reading, kana spelled out
    int dropped = 0;    // words of the text whose Korean is empty (kana that are no particle)
};

// The Reading of the words that overlap bytes [start, end) of the text of token; token -1: of all
// the words.
Reading ReadingOf(std::span<const Word> words, int token, int start, int end);

// The reading of w as a veto of the word stage.
word::Veto VetoOf(const Word& w);

// The words as one text for a report: per word its token and offset, part code, grammar codes,
// conjugation class, ending, Japanese, Korean and whether a space follows, separated by " | ".
std::string Describe(std::span<const Word> words);

}  // namespace hdTrans::floor
