// T: construction matcher over the IR (DESIGN.md §3.3, §4.3; IR.md §2, §5, §6).
//
// A construction row matches a token pattern (lemmas, typed slots, function classes) and holds when its
// `when` atoms hold over clause features, arguments and lexicon features. Among the rows that match,
// the most specific wins (matched atoms + satisfied when atoms), then `prio`, then file order. Two
// winners may not share a literal token; rows that share none coexist (ため + ている on one verb).
// A winner emits Korean node templates (`ko`) and feature settings (`set`), never Korean text.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ir_view.h"

namespace hd08::xfer {

struct Atom {
  enum class Kind : uint8_t { Lemma, Slot, FClass, Gap, NotLemma, GroupOpen, GroupClose };
  Kind kind = Kind::Lemma;
  std::vector<std::string> lemmas;   // alternatives, alias-expanded
  std::vector<std::string> classes;  // alternatives that are function classes (a|@cop)
  std::vector<std::string> pos;      // optional IR pos or UniDic POS prefix filter, alternatives
  char slot = 0;                     // V N X A
  std::vector<std::string> forms;    // slot forms: dict ta te neg pos renyou mizen katei adn vol pass caus polite bare
  std::string cls;                   // slot class prefix (@person)
  std::string fclass;                // @neg @past @cop @polite @q @end @fin @te
  bool optional = false;             // [x]
  int group = -1;                    // optional group index
};

struct Cond {
  std::string path;                  // V.vol main.tense subj.cls next.lemma final …
  bool negate = false;               // !=
  std::vector<std::string> values;   // alternatives; "-" = absent, "+" = present
};

struct Row {
  std::string id, facet, file, match, when, set, ko, pol, spec, note;
  int prio = 0;
  size_t order = 0;
  std::vector<Atom> atoms;
  std::vector<Cond> conds;
  int literals = 0, slots = 0;
  int req = -1;                      // a required literal atom (anchor for candidate positions), -1 none
};

struct Case {
  std::string id, kind, ja, ko, file;  // kind: fix | keep
};

struct FuncLine {
  std::string pos, udPrefix;          // a token matches if pos matches (or "*") and ud starts with udPrefix
  std::string cform, ctype;           // optional prefixes of the token's cform / ctype
  std::vector<std::string> lemmas;    // "*" = any
};

struct PredFeat {
  std::string vol, asp, pclass;       // vol: + | - ; asp: state activity change change-past ; pclass: motion speech …
};

struct Table {
  std::vector<Row> rows;
  std::vector<Case> cases;
  std::map<std::string, std::vector<std::string>> alias;      // written form → analyzer lemmas
  std::map<std::string, std::vector<FuncLine>> fclass;        // function-word classes
  std::vector<std::pair<std::string, std::string>> forms;     // cform prefix → form class
  std::map<std::string, PredFeat> pred;                       // lemma → volition/aspect class
  std::map<std::string, std::string> nounClass;               // stand-in noun classes (lemma or surface)
  std::vector<std::string> errors;

  // The class of a noun: a person or place name from its UniDic POS, else the stand-in table.
  std::string ClassOf(const std::string& lemma, const std::string& surf, const std::string& ud) const;
};

// Loads every data/cx/*.tsv (+ .cases.tsv) and the support tables under dir. Errors go to t.errors.
bool LoadTable(const std::string& dir, Table& t);

struct Match {
  size_t row = 0;
  int begin = 0, end = 0;            // token range [begin, end)
  std::vector<int> lits;             // tokens matched by literal atoms (consumed)
  int vTok = -1, vEnd = -1, nTok = -1;
  int score = 0, whenSat = 0;
  std::vector<std::string> because;  // facts the when atoms read
};

struct Decision {                    // IR.md §2 (stage 'T')
  std::string kind;                  // cued | default | lexical
  std::string rule;                  // cx:<id>
  Span src;
  int score = 0, margin = 0, nAlt = 0;
  std::string runnerUp;
  std::vector<std::string> because;
};

struct Result {
  std::vector<Match> chosen;         // winners, in token order
  std::vector<Decision> decisions;   // one per winner
  std::vector<Match> candidates;     // every match whose pattern and when held (for --all)
};

Result Transfer(const Table& t, const LineIR& ir);

// JSON Lines record of one line's winners: matches, decisions and the emitted node templates.
std::string ResultJson(const Table& t, const LineIR& ir, const Result& r, bool withCandidates);

}  // namespace hd08::xfer
