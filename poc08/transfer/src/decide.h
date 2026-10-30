// Learned deciders (PIVOT §4): られる voice and the connectives ため, ように, ところで.
//
// A decider is a small table of weights over named IR features (multinomial logistic regression,
// trained offline on corpus silver). It is stacked on 0.7.5's own decision: that decision is one of its
// features, and the decider overrides it only when its top class differs and
// p(top) − p(0.7.5's class) > tau. Every decision lists each feature's contribution and the 0.7.5
// decision it kept or overrode, for --why.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ir_view.h"
#include "transfer.h"

namespace hd08::xfer {

struct Decider {
  std::string family;                           // voice | tame | youni | tokorode
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

// Loads data/decide/*.tsv under dir. A file holds `#! decider family=… tau=… id=…`, a `classes` line,
// `bias` lines and `feature<TAB>class<TAB>weight` rows (zero weights omitted).
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
};

// Decides one point. base075 is 0.7.5's decision for the point as a class name ("" or NONE if 0.7.5
// realised none of the classes).
DeciderResult Decide(const Decider& d, const std::vector<std::string>& features, const std::string& base075);

}  // namespace hd08::xfer
