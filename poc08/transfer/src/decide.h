// Deciders at decision points (PIVOT §4): られる voice and the connectives ため, ように, ところで.
//
// A decider is either a table of hand-written rule rows (kind=rules: the voice table, spec VC-*) or a
// table of weights over named IR features (multinomial logistic regression, trained offline on corpus
// silver). Both read the same named features and 0.7.5's own decision (p075=<class>).
// - Rules: the most specific row whose conditions hold decides; its class overrides 0.7.5's when they
//   differ (KEEP keeps it; @default is the verb's default reading from <family>/verbs.tsv).
// - Weights: the decider overrides 0.7.5 only when its top class differs and p(top) − p(0.7.5's class) > tau.
// Every decision lists what decided it (the rule row, or each feature's contribution) and the 0.7.5
// decision it kept or overrode, for --why.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ir_view.h"
#include "transfer.h"

namespace hd08::xfer {

// A condition of a rule row: a feature present (`neg`), a feature with one of the values (`vlem=見る|見える`),
// or the negation of either (`!neg`, `!n1=ない`).
struct RuleAtom {
  bool negate = false;
  std::string key;
  std::vector<std::string> values;  // empty: the feature `key` itself
};

struct DecideRule {
  std::string id, spec, cls;        // cls: a class, KEEP, or @default
  std::vector<RuleAtom> atoms;
  int lemmas = 0;                   // vlem/vnoun atoms: compared first
  size_t order = 0;                 // file order: the earlier row wins a tie
};

// A verb of the voice table: the readings its (ら)れる allows and the one it takes without a cue.
struct DecideVerb {
  std::string lemma, pos;           // pos: v5 v1 vk vs or *
  std::vector<std::string> allow;
  std::string dflt;
};

struct Decider {
  std::string family;
  bool rules = false;                           // kind=rules: rows and verbs, no weights
  std::vector<DecideRule> rows;
  std::vector<DecideVerb> verbs;                           // voice | tame | youni | tokorode
  std::vector<std::string> classes;             // PASSIVE POTENTIAL … (the feature slot values)
  std::vector<float> bias;                      // per class
  std::map<std::string, std::vector<float>> w;  // feature → weight per class
  float tau = 0;                                // override margin
  std::string id;                               // table id for Decision.rule (dec:<family>@<version>)
};

struct DecideData {
  std::map<std::string, Decider> byFamily;
  std::vector<std::string> errors;
};

// Loads data/decide/*.tsv under dir. A weight file holds `#! decider family=… tau=… id=…`, a `classes` line,
// `bias` lines and `feature<TAB>class<TAB>weight` rows (zero weights omitted). A rule file holds
// `#! decider family=… kind=rules id=…`, a `classes` line, then rows `id spec when class st prov note` (when:
// atoms joined by spaces, "-" for none); its verbs are in decide/<family>/verbs.tsv
// (`lemma pos allow default spec note`, lemma alternatives joined by |).
bool LoadDeciders(const std::string& dir, DecideData& d);

// A decision point: the token that carries the decision (the れる/られる auxiliary, the ため / よう /
// ところ noun) and its family.
struct DecisionPoint {
  int tok = -1;
  std::string family;
};

// Decision points of a line, found from the function-word classes of the support tables.
std::vector<DecisionPoint> FindPoints(const Table& t, const LineIR& v);

// Named features of a decision point (the same code produces training and run-time features).
std::vector<std::string> Features(const Table& t, const LineIR& v, const DecisionPoint& p);

struct Contribution {
  std::string feature;
  float weight = 0;                             // weight of the feature for the chosen class
};

struct DeciderResult {
  std::string chosen, base, top;                // chosen class, 0.7.5's class, the model's top class
  bool overridden = false;
  float pTop = 0, pBase = 0, margin = 0;
  std::vector<Contribution> because;            // largest contributions to the chosen class first
  std::string rule;                             // rules: the id of the deciding row ("" when none held)
};

// Decides one point. base075 is 0.7.5's decision for the point as a class name ("" or NONE if 0.7.5
// realised none of the classes).
DeciderResult Decide(const Decider& d, const std::vector<std::string>& features, const std::string& base075);

}  // namespace hd08::xfer
