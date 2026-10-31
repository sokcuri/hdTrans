#pragma once
// The regeneration: each line of the translation rebuilt as nodes on its own text (the words the reader
// finds are taken apart by analysis by synthesis, the text between them stays), the levels of
// levels.h set on its predicates, and the line written again.
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/register/levels.h"
#include "core/register/reader.h"
#include "core/register/tables.h"

namespace hdTrans::reg {

struct RegenOptions {
    // The levels of spec/register.md; false keeps the level each predicate has in the translation.
    bool levels = true;
    // The decisions of DecideLevels for the lines, made before the translation (DecideSource); null to
    // decide them here.
    const std::vector<RegDecision>* decisions = nullptr;
};

struct RegenLine {
    std::string out;  // the regenerated line, or the translation where it could not be read
    std::string gen;  // the regenerated line
    bool ok = false;  // every word was read
    bool levelChanged = false;  // the last sentence end of the line (or of one of its quotes) changes level
    bool mixedLevels = false;   // the sentence ends would be at different levels
    std::string spec, key;      // the rule and cue key of the line's level
};

// The regeneration of one document line by line: the levels are decided over all its lines first, then
// each line is written again on its own, so that a long document needs only one line's reading at a time.
class Regenerator {
public:
    Regenerator(const Data& d, const std::vector<RegLine>& lines, const RegenOptions& options);
    ~Regenerator();
    Regenerator(const Regenerator&) = delete;
    Regenerator& operator=(const Regenerator&) = delete;
    // Line li of the document, read as line.
    RegenLine Line(std::size_t li, const PreLine& line);

private:
    struct State;
    const Data& d_;
    RegenOptions options_;
    std::unique_ptr<State> state_;
};

// Regenerates the lines whose build flag is set; the levels are decided over all lines (one document).
std::vector<RegenLine> Regenerate(const Data& d, const std::vector<PreLine>& lines, const std::vector<bool>& build,
                                  const RegenOptions& options);

}  // namespace hdTrans::reg
