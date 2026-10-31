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
// The aux.tsv reading of a token sequence (C and L read the same): per token its morpheme row id and its form
// row id ("aux:<id>", empty when none) and the facts the rows set ("pol=neg", "mod=want", "role=cop", "kind=neg").
// T reads them to match rows over features; it writes nothing.
struct TokAux {
    std::string row, form;
    std::vector<std::string> facts;
};
std::vector<TokAux> AuxOf(const StructureData& s, const Doc& doc, const std::vector<Token>& toks);
// Z: clauses → Clause.subj, Clause.beneTo.
void StageZ(const DiscourseData& d, const Config& cfg, Doc& doc, Line& line, int path);
// T: everything above → kir, chosen.
void StageT(const TransferData& t, const Config& cfg, Doc& doc, Line& line);
// D (pivot): the gated learned deciders. kir holds 0.7.x's choices lifted to the IR (each node's
// decision is the 0.7.x one); D overwrites the voice and connective slots of kir nodes where a
// decider beats the 0.7.x choice by its margin, and appends a Decision (with contrib) for each
// override. It never adds or removes nodes.
void StageD(const TransferData& t, const Config& cfg, Doc& doc, Line& line);

// The deciders' points of a lifted line (as StageD reads it), each with the decided class, 0.7.x's class
// and whether the decider overrode it. Appends one Decision per point and changes no node.
struct PointDecision {
    std::string family, cls, base;
    Span at;
    bool overridden = false;
    uint32_t decision = 0;
};
std::vector<PointDecision> DecidePoints(const TransferData& t, const Config& cfg, Doc& doc, Line& line);
// R1 on one 0.7.x candidate: the class it realises, from facts as the bridge records them
// ("t075.voice=…", "t075.marker=…", "t075.ko=…", "t075.real=…") and the verb lemma before the point.
std::string ClassOfFacts(const TransferData& t, const std::string& family, const std::vector<std::string>& facts,
                         std::string_view jlem);
// R: one line → reg, lev. The document state of S (Doc.scenes, Doc.speakers, utts) is read-only
// here, so R runs per line, in line order, also on a line made of one sentence (the 0.7.x bridge).
void StageR(const DiscourseData& d, const Config& cfg, Doc& doc, Line& line);
// W: kir, reg → kir2.
void StageW(const GeneratorData& g, const Config& cfg, Doc& doc, Line& line);
// G: kir2, reg → out, spans, invariant flags (CheckInvariants in invariants.h).
void StageG(const GeneratorData& g, const Config& cfg, Doc& doc, Line& line);

}  // namespace hd08
