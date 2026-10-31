// A: lattice construction and Viterbi. Shared by the analyzer and the trainer.
#pragma once

#include <cstdint>
#include <vector>

#include "model.h"
#include "normalize.h"

namespace hd8::ana {

enum class ArcKind : uint8_t { lex, unk, longUnit, stutter };

struct Arc {
  uint16_t b = 0, e = 0;  // core positions
  uint16_t lcls = 0, rcls = 0;
  int64_t cost = 0;
  ArcKind kind = ArcKind::lex;
  bool joined = false;    // crosses a source space inside a spaced run
  uint16_t flags = 0;     // TokFlag bits
  uint32_t ref = 0;       // lex: group; unk: category << 8 | arc; long: index
  uint32_t form = 0;      // lex: index into Model::pforms
  int16_t rule = -1;      // derive rule
  int16_t removedBy = -1; // constraint
  int64_t fwd = 0, bwd = 0;
  int32_t bp = -1, np = -1;  // best previous / next arc, -1 = BOS / EOS
};

constexpr int64_t kInf = int64_t{1} << 61;

struct Workspace {
  std::vector<internal::NChar> nchars;
  std::vector<NormEdit> edits;
  std::vector<char32_t> core;          // normalized text without spaces
  std::vector<uint32_t> coreN;         // nchars index of each core char
  std::vector<uint8_t> spaceBefore;    // size n + 1
  std::vector<uint8_t> joinable;       // size n + 1
  std::vector<uint32_t> cc;            // CharClass per core char
  std::vector<uint32_t> nextSpace;     // first k > i with spaceBefore[k], size n + 1
  std::vector<Arc> arcs;
  std::vector<uint32_t> beginStart, byBegin, endStart, byEnd;
  std::vector<char32_t> buf;
  std::vector<uint32_t> bufMap;
  std::vector<uint8_t> derived;        // buffer position modified
  std::vector<uint32_t> path, path1;
  int64_t best = 0;
};

// Fills ws->core etc. from ws->nchars and builds all arcs (constraints applied, removed arcs kept with removedBy).
void BuildLattice(const Model& m, Workspace* ws, const Options& opt);
// Forward pass over the whole line; also the backward pass when `backward`. Sets ws->best and ws->path.
void RunViterbi(const Model& m, Workspace* ws, bool backward);
// Best path over core [s, t) using only short-unit arcs, entered from class `startCls` and left into `endCls`.
void SubPath(const Model& m, const Workspace& ws, uint16_t s, uint16_t t, uint16_t startCls, uint16_t endCls,
             std::vector<uint32_t>* out);

}  // namespace hd8::ana
