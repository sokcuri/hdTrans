#pragma once
// The regeneration: each line of the translation rebuilt as nodes on its own text (the words the reader
// finds are taken apart by analysis by synthesis, the text between them stays), the levels of
// levels.h set on its predicates, and the line written again.
#include <string>
#include <vector>

#include "core/register/reader.h"
#include "core/register/tables.h"

namespace hdTrans::reg {

struct RegenOptions {
    // The levels of spec/register.md; false keeps the level each predicate has in the translation.
    bool levels = true;
};

struct RegenLine {
    std::string out;  // the regenerated line, or the translation where it could not be read
    std::string gen;  // the regenerated line
    bool ok = false;  // every word was read
    bool levelChanged = false;  // the last sentence end of the line (or of one of its quotes) changes level
    bool mixedLevels = false;   // the sentence ends would be at different levels
    std::string spec, key;      // the rule and cue key of the line's level
};

// Regenerates the lines whose build flag is set; the levels are decided over all lines (one document).
std::vector<RegenLine> Regenerate(const Data& d, const std::vector<PreLine>& lines, const std::vector<bool>& build,
                                  const RegenOptions& options);

}  // namespace hdTrans::reg
