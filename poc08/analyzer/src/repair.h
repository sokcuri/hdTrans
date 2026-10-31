// Input repair (ja/repair.tsv): readings of noisy input by fixed-cost operations, tried only on spans
// whose analysis fires a check row (an unknown word, an impossible adjacency, a stray mark …).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hd8/analyzer.h"
#include "lattice.h"
#include "model.h"

namespace hd8::ana {

struct RepairRules {
  struct Op {
    std::string id;
    bool on = false;
    int32_t cost = 0;
    int32_t minLen = 0;
  };
  Op ops[static_cast<size_t>(RepairOp::count)];

  // One atom of a check condition: [!]side.field=v1|v2, side.field^=… (prefix), side.field$=… (suffix), or
  // same=surf.
  struct Atom {
    uint8_t side = 0;   // 0: t or l, 1: r
    uint8_t field = 0;
    bool neg = false, prefix = false, suffix = false;
    std::vector<std::string> vals;
  };
  using Conj = std::vector<Atom>;
  struct Check {
    std::string id;
    bool pair = false;
    bool trigger = true;  // false (valid.*): judges readings, never opens a span by itself
    bool space = false;   // tests r.space: also fires across a space (other pair checks do not)
    Conj when;
    std::vector<Conj> require;  // empty: the reading is invalid whenever `when` holds
  };
  std::vector<Check> tok, pair;
  std::string spaceInWord;  // the bad.space row: a kept space that a dictionary word spans

  // param rows
  std::vector<std::string> functionUd;  // UniDic POS prefixes of function words (fold, swap)
  std::vector<std::string> foldWordUd;  // a katakana word as written folds only into these
  std::vector<std::string> noEditUd;    // never edited by edit1 (names)
  std::vector<std::string> stopUd;      // spans read again stop at these
  std::vector<std::string> pauseUd;     // a space after these is a pause, not a stray
  std::vector<std::string> phraseEndUd; // a space after these parts two phrases (そばに いて)
  std::u32string repeatMarks;           // marks whose repeat is dropped like a letter's
  std::u32string tripleFree;            // letters whose run of three is not noise
  std::u32string laughs;                // the same after a vowel or at the start (あははは)
  std::u32string notAfterSokuon;        // letters that never follow っ
  std::u32string censor;                // marks that hide letters (ポケ〇ン): the katakana next to them stays
  std::u32string brackets;              // opening and closing brackets in pairs (「」『』…)
  std::u32string cryMarks;              // a repeated letter before these is a cry (いたたた！)
  std::u32string softVowels;            // small vowels that make a foreign sound with the letter before (ふぉ)
  std::u32string voicedWords;           // one-letter words a restored dakuten may make (が だ で ぞ ど ぜ)
  std::u32string soundEnd;              // letters after a repeated letter that make it a sound word (とてててと)

  int32_t arcBudget = 6;     // highest repair cost of one word
  int32_t windowBudget = 12; // highest repair cost of one span
};

bool ParseRepairTsv(std::string_view text, RepairRules* out, std::string* err);
std::string_view RepairOpId(const RepairRules& rules, RepairOp op);

// Repairs the best path in ws (after BuildLattice and RunViterbi): spans of ws->path where a check fires
// are read again with the operations; a span takes a repaired reading only when that reading fires no
// check. Repaired words are appended to ws->arcs (ArcKind::repair, ref into ws->rarcs) and ws->path is
// rewritten. Returns true when something was repaired. `why`, when given, receives the check rows that
// fired on the spans tried (one line each).
bool RepairPath(const Model& m, const RepairRules& rules, Workspace* ws, const Options& opt, std::string* why);

}  // namespace hd8::ana
