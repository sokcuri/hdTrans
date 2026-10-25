#include <string>
#include <string_view>
#include <vector>

#include "core/engine/driver.h"
#include "core/word/cg.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kHeader = "key\tko\tpos\tsem\tgram\tdict\trule\tid\n";

std::shared_ptr<const dict::Snapshot> Dictionary(std::string_view rows) {
    std::vector<dict::File> files{{0, "base.tsv", std::string(kHeader) + std::string(rows)}};
    const dict::LoadResult loaded = dict::Load(std::move(files));
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

struct Picked {
    const std::vector<word::CgRule>* rules = nullptr;
    std::vector<std::string> targets;  // the replacing candidate's Korean, "-" to keep
};

void OnResolved(void* context, int, int, const word::Chain& chain) {
    auto& p = *static_cast<Picked*>(context);
    for (const word::MisreadDecision& d : word::MisreadDecisions(chain, word::BuiltinMisreadModel())) {
        const int k = word::CgReplacement(d, *p.rules);
        p.targets.push_back(k < 0 ? std::string("-") : d.candidates[static_cast<std::size_t>(k)].target);
    }
}

std::vector<std::string> Pick(const dict::Snapshot& snapshot, std::string_view text, std::string_view table) {
    const std::vector<word::CgRule> rules = word::ParseCgRules(table);
    Picked p;
    p.rules = &rules;
    internal::StageObserver observer;
    observer.context = &p;
    observer.resolved = OnResolved;
    const auto generated = internal::NewGenerated(snapshot);
    const std::string line(text);
    internal::TranslateDocument(snapshot, *generated, {&line, 1}, observer, {});
    return p.targets;
}

const char* const kRows =
    "曰く\t왈\tN110\tXXX\t\tGN\t0\t1\n"
    "曰く\t가라사대\tN110\t\t\tGN\t0\t2\n"
    "曰く\t이르길\tN110\t\t\tGN\t0\t3\n";

}  // namespace

HD_TEST(word_cg_parses_tiers_and_literals) {
    const auto rules = word::ParseCgRules(
        "# comment\n"
        "2\tREMOVE\tt=a\\sb\tkept\tsecond\n"
        "1\tSELECT\tb={x,y*}\t!ls any:snd\tfirst\n"
        "1\tKEEP\tx\t\tskipped\n");
    HD_REQUIRE(rules.size() == 2);
    HD_CHECK_EQ(rules[0].note, std::string("first"));
    HD_CHECK(rules[0].select);
    HD_REQUIRE(rules[0].target.size() == 1 && rules[0].target[0].alternatives.size() == 2);
    HD_CHECK_EQ(rules[0].target[0].alternatives[0].text, std::string("b=x"));
    HD_CHECK(rules[0].target[0].alternatives[1].prefix);
    HD_REQUIRE(rules[0].condition.size() == 2);
    HD_CHECK(rules[0].condition[0].literal.negated);
    HD_CHECK(rules[0].condition[1].kind == word::CgCondition::Kind::Any);
    HD_CHECK_EQ(rules[1].target[0].alternatives[0].text, std::string("t=a b"));
    HD_CHECK(rules[1].condition[0].kind == word::CgCondition::Kind::Kept);
}

HD_TEST(word_cg_remove_select_and_last_candidate) {
    const auto snapshot = Dictionary(kRows);
    HD_REQUIRE(snapshot != nullptr);
    struct Case {
        const char* table;
        const char* want;
    };
    const Case cases[] = {
        {"", "-"},
        {"1\tREMOVE\tchosen\tls\n", "가라사대"},
        {"1\tSELECT\tt=이르길\tst=曰く|왈\n", "이르길"},
        {"1\tSELECT\tt=이르길\t!ls\n", "-"},
        {"1\tREMOVE\tt=*\t\n", "-"},
        {"1\tREMOVE\tchosen\t\n2\tREMOVE\tt=가라사대\tnone:t=없음\n", "이르길"},
        {"1\tREMOVE\tt={가라사대,이르길}\t\n2\tREMOVE\tchosen\t\n", "-"},
        {"1\tREMOVE\tchosen\t\n1\tSELECT\tchosen\tkept\n", "가라사대"},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> got = Pick(*snapshot, "曰く、嘘。", c.table);
        HD_REQUIRE(got.size() == 1);
        HD_CHECK_EQ(got[0], std::string(c.want));
    }
}

HD_TEST(word_cg_builtin_tables_parse) {
    HD_CHECK(!word::BuiltinCgRules(true, false).empty());
    HD_CHECK(!word::BuiltinCgRules(false, true).empty());
    HD_CHECK_EQ(word::BuiltinCgRules(true, true).size(),
                word::BuiltinCgRules(true, false).size() + word::BuiltinCgRules(false, true).size());
    HD_CHECK(word::BuiltinCgRules(false, false).empty());
}
