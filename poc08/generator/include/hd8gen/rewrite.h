#pragma once
// W: Korean rewrite rules over the morpheme IR (DESIGN.md §3.5). Rules are TSV rows with a pattern of node
// matchers, a condition over features and the discourse state, and actions that edit nodes and features
// only. Each rule has fix and keep cases in a sibling .cases.tsv file; the lint checks them.
#include <memory>
#include <string>
#include <vector>

#include "hd8gen/generator.h"
#include "hd8gen/kir.h"

namespace hd8gen {

struct RuleSet;

// Loads rules.tsv and rules.cases.tsv from dir.
std::shared_ptr<const RuleSet> LoadRules(const std::string& dir, std::string& err);

struct Fired {
    std::string rule;
    uint32_t node = 0;  // origin of the first matched node
};

struct RewriteResult {
    std::vector<Node> kir2;
    std::vector<Reg> reg;
    std::vector<Fired> fired;
};

// Runs the phases disc then ko. kir is not edited; kir2 nodes keep the origin of the node they came from.
// `only`, when not empty, limits the run to the rules whose id starts with one of its entries.
RewriteResult Rewrite(const RuleSet&, const Tables&, const std::vector<Node>& kir, const std::vector<Reg>& reg,
                      const std::vector<UttLevel>& lev, const std::vector<std::string>& only = {},
                      const std::vector<std::string>& skip = {});

struct CaseResult {
    std::string rule, kind, input, expected, got;
    bool fired = false, pass = false;
};
struct LintReport {
    std::vector<std::string> problems;  // empty when clean
    std::vector<CaseResult> cases;
};
// Runs every case through all rules and the generator, and checks: each rule has a fix and a keep case, each fix
// case fires its rule and generates the expected text, each keep case does not fire it, and no two rules of a
// phase fire on the same node of a case with different actions (overlap).
LintReport Lint(const RuleSet&, const Tables&);

std::vector<std::string> RuleIds(const RuleSet&);

}  // namespace hd8gen
