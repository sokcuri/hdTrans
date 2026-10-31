#pragma once
// The speech-level stage: after the word stage has translated a document, the dialogue lines whose
// speech level the rules of spec/register.md change are written again at that level. A line is a
// dialogue line when a quote opens it (after an optional speaker tag); a document that reads as
// captions is left as it is. Only lines whose last sentence end changes level and whose sentence ends
// stay at one level are replaced; 합쇼체 is never lowered and 해요체 not raised to 합쇼체.
#include <span>
#include <string>
#include <vector>

#include "core/register/ir.h"
#include "core/register/levels.h"
#include "core/register/reader.h"

namespace hdTrans::reg {

// A line the stage wrote again.
struct Change {
    std::size_t line = 0;  // in the call, from 0 after the context
    std::string source, before, after;
    std::string spec, key;  // the rule of spec/register.md and the cue key that decided the line
};

struct Switches {
    bool levels = true;  // the levels of the rules; false keeps every line as the translation wrote it
};

// The decisions of the stage for the lines of source from the Japanese alone, one per line, and which lines
// are dialogue lines, the ones it writes; both empty where it leaves the document as the translation writes
// it (no dialogue line, a document that reads as captions, or no data of the stage).
struct SourceDecisions {
    std::vector<RegDecision> lines;
    std::vector<bool> dialogue;
};
SourceDecisions DecideSource(std::span<const std::string> source);

// Rewrites out (the translation of source, one line each, each with its "\r" when the source has one).
// changes, when given, gets the lines written again. decided, when given, holds DecideSource(source).
void Retrofit(std::span<const std::string> source, const Capture& capture, std::span<std::string> out,
              const Switches& switches = {}, std::vector<Change>* changes = nullptr,
              const SourceDecisions* decided = nullptr);

}  // namespace hdTrans::reg
