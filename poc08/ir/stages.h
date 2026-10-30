#pragma once
// The stage entry points. Each component library defines the functions of its stages; the
// driver links a stub (poc08/driver/stubs/) for every component whose directory has no
// CMakeLists.txt. Component data is loaded once, read-only afterwards, and shared by threads:
// a stage reads only its const data and writes only its own fields of Doc/Line (IR.md §1).
#include <string>
#include <vector>

#include "ir.h"

namespace hd08 {

enum class Speech : uint8_t { Strict, Soft, Auto, Haera, Hae, Haeyo, Hapsyo };

struct Config {
    std::string data;                    // --dicts: the data root (the poc08/ folder layout)
    std::vector<std::string> userdicts;  // --userdict files or folders
    Speech speech = Speech::Strict;
    bool wrapped = false;                // --wrapped
    bool document = true;                // false with --each: one line per Doc, no context
};

struct AnalyzerData;   // poc08/analyzer: N, A
struct DiscourseData;  // poc08/discourse: S, Z, R
struct StructureData;  // poc08/structure: C, L
struct TransferData;   // poc08/transfer (+ poc08/lexicon tables): T
struct GeneratorData;  // poc08/generator: W, G

// Load returns nullptr and sets err when the component cannot start.
AnalyzerData* LoadAnalyzer(const Config& cfg, std::string& err);
void FreeAnalyzer(AnalyzerData* data);
DiscourseData* LoadDiscourse(const Config& cfg, std::string& err);
void FreeDiscourse(DiscourseData* data);
StructureData* LoadStructure(const Config& cfg, std::string& err);
void FreeStructure(StructureData* data);
TransferData* LoadTransfer(const Config& cfg, std::string& err);
void FreeTransfer(TransferData* data);
GeneratorData* LoadGenerator(const Config& cfg, std::string& err);
void FreeGenerator(GeneratorData* data);

// N: line.src → norm, map, edits.
void StageN(const AnalyzerData& a, const Config& cfg, Doc& doc, Line& line);
// A: norm → toks, alts, alt.toks (path 1), fallback flags. forced holds S's name spans for this
// line (empty on the first run); A re-runs a line at most once.
void StageA(const AnalyzerData& a, const Config& cfg, Doc& doc, Line& line, const std::vector<ForcedSpan>& forced);
// S: the whole document → utts, mentions, scenes, speakers; appends name spans for A to forced.
void StageS(const DiscourseData& d, const Config& cfg, Doc& doc, std::vector<ForcedSpan>& forced);
// C, L and Z run once per analysis path: path 0 reads and writes the line's toks/buns/clauses,
// path 1 those of line.alt (only when line.alt.toks is not empty). PathOf(line, path) gives both.
// C: toks → buns.
void StageC(const StructureData& s, const Config& cfg, Doc& doc, Line& line, int path);
// L: toks, buns → clauses (all fields but subj and beneTo).
void StageL(const StructureData& s, const Config& cfg, Doc& doc, Line& line, int path);
// Z: clauses → Clause.subj, Clause.beneTo.
void StageZ(const DiscourseData& d, const Config& cfg, Doc& doc, Line& line, int path);
// T: everything above → kir, chosen.
void StageT(const TransferData& t, const Config& cfg, Doc& doc, Line& line);
// R: the whole document → reg, lev.
void StageR(const DiscourseData& d, const Config& cfg, Doc& doc);
// W: kir, reg → kir2.
void StageW(const GeneratorData& g, const Config& cfg, Doc& doc, Line& line);
// G: kir2, reg → out, spans, invariant flags (CheckInvariants in invariants.h).
void StageG(const GeneratorData& g, const Config& cfg, Doc& doc, Line& line);

}  // namespace hd08
