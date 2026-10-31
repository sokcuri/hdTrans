#pragma once
// --why records (DESIGN §6) and the per-line --json summary: risk, evidence, flags, coverage.
#include <string>
#include <vector>

#include "driver.h"

namespace hd08 {

struct LineReport {
    std::vector<Flag> flags;          // line.flags plus canary flags G did not write
    uint32_t outBytes = 0;            // non-space bytes of out
    uint32_t tracedBytes = 0;         // of those, inside a span whose node resolves to a decision
    uint32_t evidence[9] = {};        // decisions per Ev (Count(F::ev) entries)
    uint32_t multi = 0;               // decisions with more than one candidate
    uint32_t multiContrib = 0;        // of those, with contributions (G11)
    uint32_t stU = 0;                 // T decisions on a lexicon row with st=u
    float risk = 0;                   // the highest prior among the line's decisions and flags
    const Decision* top = nullptr;    // the decision that gave risk
    bool covered = false;             // PoC plan §4.3
};

LineReport Report(const Engine& engine, const Doc& doc, const Line& line);

// The --why JSON Lines of one line: one record per output morpheme, then the decisions no
// morpheme points to, then the flags.
std::string WhyRecords(const Engine& engine, const Doc& doc, const Line& line, const LineReport& report);

// The --json object of one line.
std::string LineSummaryJson(const Doc& doc, const Line& line, const LineReport& report);

}  // namespace hd08
