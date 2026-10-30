#pragma once
// The bridge between the 0.7.x engine and the 0.8 components (PIVOT): it implements the hook
// points of src/core/engine/hooks.h with the poc08 stages and keeps the side IR of every
// sentence for --why and --stages.
//   normalize  the analyzer's N replaces the 0.7.x normalization;
//   segment    the analyzer's tokens decide where 0.7.x words may end;
//   generate   0.7.x's resolved sentence is lifted to kir (lift.h), the deciders (D) overwrite
//              voice and connective slots, and R, W and G write the sentence;
//   decide     D runs (otherwise kir keeps 0.7.x's choices).
// A part whose component is a stub is off whatever is asked.
#include <cstdint>
#include <string>
#include <string_view>

#include "core/engine/hooks.h"
#include "ir/stages.h"

namespace hd08::bridge {

struct Options {
    Config cfg;
    bool normalize = false, segment = false, generate = false, decide = false;
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
};

struct Bridge;
Bridge* Create(const Options& options, std::string& err);
void Destroy(Bridge* bridge);
const hdTrans::hooks::Hooks* HooksOf(const Bridge* bridge);
// The parts actually on, as "normalize,segment,…" ("" for none), and the stub components.
std::string ActiveParts(const Bridge* bridge);
Counts CountsOf(const Bridge* bridge);
// The side IR collected so far, and cleared: --why records and ir.jsonl lines.
std::string TakeWhy(Bridge* bridge);
std::string TakeStages(Bridge* bridge);
// The lift misses by key, most frequent first, as TSV (kind, key, count), at most limit rows.
std::string LiftMisses(const Bridge* bridge, size_t limit);

}  // namespace hd08::bridge
