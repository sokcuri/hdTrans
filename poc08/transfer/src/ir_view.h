// The part of the IR (IR.md §3–§5) the construction matcher reads, as a per-line view with the enum
// names of IR.md §7. The matcher compares them with table values; MakeView builds it from hd08::Line.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ir/ir.h"

namespace hd08::xfer {

struct Span {
  uint32_t begin = 0, end = 0;
};

struct Tok {                 // IR Token (writer A); `cls` stands in for the lexicon class of a noun
  Span src;
  std::string surf, lemma, reading, pos, ud, ctype, cform, cls;
  bool spaceBefore = false;
  int bun = -1;              // derived: bunsetsu index
};

struct Bun {                 // IR Bunsetsu (writer C)
  int first = 0, last = 0, content = 0, head = -1, head2 = -1;
  float headMargin = 0;
  char type = 'N';           // N P A C I Q
  std::string kase;          // none ga wo ni de to kara made yori he no wa mo nado quote
  bool topic = false, commaAfter = false, quoteClose = false;
  int clause = -1;           // derived: clause index
};

struct Arg {                 // IR Arg
  std::string role, lemma, cls, how;
  int bun = -1;
};

struct Clause {              // IR Clause (writer L)
  int pred = -1, firstBun = 0, lastBun = 0, parent = -1;
  std::string link, tense, aspect, pol, negFn, voice, bene, force, polite;
  std::vector<std::string> modality, jreg;
  std::vector<Arg> args;
  std::vector<int> consumed, unfolded;
};

struct LineIR {
  std::string key, src;
  std::vector<Tok> toks;
  std::vector<Bun> buns;
  std::vector<Clause> clauses;
};

struct Table;
// Builds the view of path 0 of a line. Noun classes come from the arguments' cls when L set them, else
// from the transfer stand-in class table (until the lexicon provides classes).
LineIR MakeView(const hd08::Doc& doc, const hd08::Line& line, const Table& t);

}  // namespace hd08::xfer
