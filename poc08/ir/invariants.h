#pragma once
// Meaning invariants I1–I8 (DESIGN §7), checked on features, not text, after G. G appends the
// result to line.flags; the driver runs the same check as a canary over any build.
#include <string>
#include <vector>

#include "ir.h"

namespace hd08 {

struct InvariantData {
    std::vector<std::string> lexicalNegatives;  // Korean lemmas that carry a negation (I1)
};

// Reads <data>/ir/data/invariants.tsv. A missing file leaves the lists empty.
bool LoadInvariantData(const std::string& dataRoot, InvariantData& out, std::string& err);

// Appends one Flag per violation to out (kind and inv set; src in Line.src bytes; node = kir2 id or 0).
// Escapes a construction declares in its T decision's `because`: "pol=keep|absorb|flip" (I1),
// "tense=cx" (I2), "force=cx" (I3), "voice=cx" (I4).
void CheckInvariants(const Doc& doc, const Line& line, const InvariantData& data, std::vector<Flag>& out);

// Flag kinds that make a line not covered (PoC plan §4.3), and evidence kinds likewise.
bool IsRiskFlag(uint8_t kind);
bool IsRiskEvidence(Ev kind);

}  // namespace hd08
