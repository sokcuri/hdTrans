#pragma once
// The bridge between the 0.7.x engine and the 0.8 components (PIVOT): it implements the hook
// points of src/core/engine/hooks.h with the poc08 stages and keeps the side IR of every
// sentence for --why and --stages.
//   normalize  the analyzer's N replaces the 0.7.x normalization;
//   segment    the analyzer's tokens decide where 0.7.x words may end;
//   generate   0.7.x's resolved sentence is lifted to kir (lift.h), the deciders (D) overwrite
//              voice and connective slots, and R, W and G write the sentence;
//   decide     D runs (otherwise kir keeps 0.7.x's choices);
//   rule       0.8-rule: segment, and the generator's retrofit on dialogue lines (bridge/rule.h);
//   reselect   D inside 0.7.x: where a decider overrides 0.7.x (voice, ように, ところで), the item takes
//              the ending candidate 0.7.x already had whose class is the decided one, and the
//              sentence is composed again (no generator).
// A part whose component is a stub is off whatever is asked.
#include <cstdint>
#include <string>
#include <memory>
#include <string_view>
#include <vector>

#include "core/engine/hooks.h"
#include "ir/stages.h"
#include "rule.h"

namespace hd08::bridge {

struct Options {
    Config cfg;
    bool normalize = false, segment = false, generate = false, decide = false, reselect = false;
    bool rule = false;    // 0.8-rule: segment plus the retrofit on dialogue (bridge/rule.h; run by the CLI)
    bool sideIr = false;  // keep the side IR records (--why, --stages); costs time
    bool lift = false;    // lift every sentence even when no part needs it (to count the misses)
};

// Reads a --poc08 list: "all", "none", or names from normalize, segment, generate, decide.
bool ParseParts(std::string_view list, Options& out, std::string& err);

struct Counts {
    uint64_t sentences = 0;
    uint64_t generated = 0;      // written by the generator
    uint64_t kept = 0;           // generate asked, 0.7.x composition kept
    uint64_t liftedItems = 0;
    uint64_t failedItems = 0;    // items the lift could not express (lift.tsv misses)
    uint64_t normalized = 0;     // lines the analyzer normalised differently from the input
    uint64_t segmentedChunks = 0;
    uint64_t refits = 0;         // 0.7.x words that gave way to the segmentation (counted by fits)
    uint64_t points = 0;         // reselect: decider points read
    uint64_t overrides = 0;      // reselect: points where the decider overrode 0.7.x
    uint64_t reselected = 0;     // reselect: overrides an ending candidate of 0.7.x realised
};


struct Bridge;
struct RuleData;
Bridge* Create(const Options& options, std::string& err);
void Destroy(Bridge* bridge);
const hdTrans::hooks::Hooks* HooksOf(const Bridge* bridge);
// The parts actually on, as "normalize,segment,…" ("" for none), and the stub components.
std::string ActiveParts(const Bridge* bridge);
Counts CountsOf(const Bridge* bridge);
// The side IR collected so far, and cleared: --why records and ir.jsonl lines.
std::string TakeWhy(Bridge* bridge);
std::string TakeStages(Bridge* bridge);
// The overrides reselect made or could not make, as TSV (line, family, 0.7.x class, decided class,
// realised, the old and the new ending), one per sentence pass (the last pass per sentence counts).
std::string TakeReselections(Bridge* bridge);
// The lift misses by key, most frequent first, as TSV (kind, key, count), at most limit rows.
std::string LiftMisses(const Bridge* bridge, size_t limit);
// The 0.8-rule tables (bridge/rule.h) when the rule part is on, else null.
std::shared_ptr<const RuleData> RuleDataOf(const Bridge* bridge);
// The analyzer's tokens of one source text (UniDic lemma, reading, conjugation form; empty without an analyzer).
std::vector<SourceToken> AnalyzeSource(const Bridge* bridge, std::string_view text);

}  // namespace hd08::bridge
