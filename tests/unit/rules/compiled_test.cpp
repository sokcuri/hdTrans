// The compiled rules make the calls of the interpreter: for every answer script, the same
// calls with the same arguments in the same order, and the same result.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/engine/rules.h"
#include "core/rules/compiled.h"
#include "hdtest.h"
#include "rulec/oracle.h"

using namespace hdTrans;

namespace {

// HDTRANS_RULE_PATHS raises the number of paths compared per rule.
long long PathCap() {
    const char* v = std::getenv("HDTRANS_RULE_PATHS");
    return v != nullptr && std::atoll(v) > 0 ? std::atoll(v) : 1 << 14;
}

void Compare(const rules::Program& p, uint32_t root, rules::compiled::RuleCode code, const std::string& name, long long cap,
             long long& paths, long long& capped) {
    rulec::Oracle oracle;
    const auto a = oracle.Paths([&](rules::Item* it) { return rules::RunRule(p, root, it); }, cap);
    const auto b = oracle.Paths([&](rules::Item* it) { return code(it); }, cap);
    paths += a.paths;
    capped += a.capped;
    if (a.paths != b.paths || a.hash != b.hash)
        hdtest::Fail(__FILE__, __LINE__, name + ": compiled code differs from the interpreter (" + std::to_string(a.paths) +
                                             " vs " + std::to_string(b.paths) + " paths)");
}

}  // namespace

// The compiled code attaches to the program the dictionary assembles, since a local rule
// is a row's condition. The shipped dictionaries are needed, so the test skips without
// HDTRANS_DICTS.
HD_TEST(rules_compiled_attached) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    HD_REQUIRE(generated->hasRules);
    const rules::Program& p = generated->rules;
    HD_REQUIRE(internal::RulesLoaded());
    std::size_t local = 0, global = 0;
    for (const auto code : p.localCode) local += code != nullptr;
    for (const auto code : p.globalCode) global += code != nullptr;
    std::size_t tableLocal = 0, tableGlobal = 0;
    for (const auto& r : rules::compiled::LocalRules()) tableLocal += r.code != nullptr;
    for (const auto& r : rules::compiled::GlobalRules()) tableGlobal += r.code != nullptr;
    // Every compiled rule matches its record; an edited record would fall back to its nodes.
    HD_CHECK_EQ(local, tableLocal);
    HD_CHECK_EQ(global, tableGlobal);
    HD_CHECK(global > 0);
}

HD_TEST(rules_compiled_same_calls) {
    const rules::Program& p = internal::RulesProgram();
    const long long cap = PathCap();
    long long paths = 0, capped = 0, rulesCompared = 0;
    for (std::size_t i = 0; i < p.localCode.size(); i++) {
        if (!p.localCode[i]) continue;
        Compare(p, p.localRules[i], p.localCode[i], "L " + std::to_string(i), cap, paths, capped);
        rulesCompared++;
    }
    for (std::size_t i = 0; i < p.globalCode.size(); i++) {
        if (!p.globalCode[i]) continue;
        Compare(p, p.globalRules[i].root, p.globalCode[i], "G " + std::to_string(i), cap, paths, capped);
        rulesCompared++;
    }
    HD_CHECK(paths > 0);
    if (std::getenv("HDTRANS_RULE_PATHS"))
        std::fprintf(stderr, "compiled rules: %lld rules, %lld paths, %lld rules stopped at %lld paths\n", rulesCompared,
                     paths, capped, cap);
}

// An edited record keeps its index but loses its code.
HD_TEST(rules_compiled_hash) {
    HD_CHECK(rules::compiled::RecordHash("L 1 ||0") != rules::compiled::RecordHash("L 1 ||0 "));
    const auto global = rules::compiled::GlobalRules();
    HD_REQUIRE(!global.empty() && global[0].code != nullptr);
    HD_CHECK_EQ(global[0].hash, rules::compiled::RecordHash("G 1 1 997 1 ||1 &&4 S+0== \"屋\" S-1== \"各部\" D-1= \"\" D+0= \"각 방\""));
}
