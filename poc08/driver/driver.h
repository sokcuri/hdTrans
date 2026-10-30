#pragma once
// Runs the stages of IR.md §1 in order over a document.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ir/invariants.h"
#include "ir/ir.h"
#include "ir/stages.h"

namespace hd08 {

static_assert(static_cast<unsigned>(Ev::Example) + 1 == 9, "per-Ev tables have 9 entries");

struct Engine {
    Config cfg;
    AnalyzerData* analyzer = nullptr;
    DiscourseData* discourse = nullptr;
    StructureData* structure = nullptr;
    TransferData* transfer = nullptr;
    GeneratorData* generator = nullptr;
    InvariantData invariants;
    std::array<float, 9> risk{};  // prior P(wrong) per Ev, from driver/data/risk.tsv
};

bool LoadEngine(const Config& cfg, Engine& engine, std::string& err);
void FreeEngine(Engine& engine);

// The components replaced by the driver's stubs, as "analyzer,structure,…" (empty when none).
std::string StubComponents();

// Per-stage wall time in nanoseconds, indexed by StageIndex; summed over calls.
enum StageIndex { kN, kA, kS, kA2, kC, kL, kZ, kT, kD, kR, kW, kG, kStages };
using StageTimes = std::array<uint64_t, kStages>;
const char* StageName(int stage);

struct RunOptions {
    bool skipAnalysis = false;  // lines already carry norm and toks (--toks)
    StageTimes* times = nullptr;
};

// Runs N…G over every line of doc.
void RunDoc(const Engine& engine, Doc& doc, const RunOptions& options);

}  // namespace hd08
