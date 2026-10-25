#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/engine.h"
#include "core/engine/driver.h"
#include "core/word/document_senses.h"
#include "core/word/item.h"
#include "hdtest.h"
#include "rule_fixture.h"

using namespace hdTrans;

namespace {

const std::string kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "東京\t동경\tN130\tQPNXXX\t\tGN\t0\t1\n"
    "東京\t도쿄\tN130\tQCT\t\tGN\t9\t2\n"
    "市\t시\tN110\tXXX\t\tGN\t0\t3\n";

std::shared_ptr<const dict::Snapshot> Dictionary() {
    const auto loaded = dict::Load({{0, "base.tsv", fixture::WithRuleBodies(kRows)}});
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

void Remember(word::SessionSenses& memory, std::string_view key, std::string_view target = "target",
              std::string_view partCode = "N110") {
    word::Entry entry;
    entry.dst = target;
    entry.cls = partCode.front();
    entry.code = partCode.substr(1);
    word::Group group;
    group.surface = key;
    group.entries.push_back(&entry);
    word::Item it;
    it.group = &group;
    it.selected = 0;
    it.choice = {word::Provenance::LocalRule};
    memory.Remember(it);
}

struct Choices {
    std::vector<word::Selection> choices;
    std::size_t scored = 0, wouldChange = 0;
};

void Observe(void* context, int, int, const word::Chain& chain) {
    auto& observed = *static_cast<Choices*>(context);
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) {
        if (it->group->surface == "東京") observed.choices.push_back(it->choice);
        for (const auto& choice : it->shadowChoices) {
            if (std::string_view(choice.scorer) != "document-senses") continue;
            observed.scored++;
            if (choice.winner) observed.wouldChange++;
        }
    }
}

struct DictionaryFolder {
    std::filesystem::path path;

    DictionaryFolder() {
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error);
        if (error) return;
        path = root / ("hdtrans-document-senses-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(path, error)) {
            path.clear();
            return;
        }
        std::ofstream file(path / "base.tsv", std::ios::binary);
        file << fixture::WithRuleBodies(kRows);
        HD_CHECK(file.good());
    }

    ~DictionaryFolder() {
        std::error_code error;
        if (!path.empty()) std::filesystem::remove_all(path, error);
    }
};

}  // namespace

HD_TEST(document_senses_two_line_rule_document) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const bool reference : {false, true}) {
        const auto generated = internal::NewGenerated(*snapshot);
        Choices observed;
        internal::StageObserver observer;
        observer.context = &observed;
        observer.resolved = Observe;
        internal::TranslationOptions options;
        options.reference = reference;
        options.wordImprovements.documentSenses = true;
        const std::string input[] = {"東京、", "東京。"};
        const auto out = internal::TranslateDocument(*snapshot, *generated, input, observer, options);
        HD_REQUIRE(out.size() == 2);
        HD_CHECK_EQ(out[0], std::string("도쿄,"));
        HD_CHECK_EQ(out[1], std::string(reference ? "동경." : "도쿄."));
        HD_REQUIRE(observed.choices.size() == 2);
        HD_CHECK(observed.choices[0].layer == word::Provenance::LocalRule);
        HD_CHECK(observed.choices[1].layer == (reference ? word::Provenance::FirstXxx : word::Provenance::Scorer));
    }
}

HD_TEST(document_senses_no_same_line_or_future_learning) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    const auto generated = internal::NewGenerated(*snapshot);
    internal::TranslationOptions options;
    options.wordImprovements.documentSenses = true;
    const std::string sameLine[] = {"東京、東京。"};
    const auto first = internal::TranslateDocument(*snapshot, *generated, sameLine, {}, options);
    HD_CHECK_EQ(first[0], std::string("도쿄, 동경."));
    const std::string future[] = {"東京。", "東京、", "東京。"};
    const auto out = internal::TranslateDocument(*snapshot, *generated, future, {}, options);
    HD_REQUIRE(out.size() == 3);
    HD_CHECK_EQ(out[0], std::string("동경."));
    HD_CHECK_EQ(out[1], std::string("도쿄,"));
    HD_CHECK_EQ(out[2], std::string("도쿄."));
}

HD_TEST(document_senses_session_reset_isolation_and_reference) {
    DictionaryFolder dictionary;
    HD_REQUIRE(!dictionary.path.empty());
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.Open({dictionary.path.string()}));
    Session first(engine), independent(engine), reference(engine, true);
    // A sense chosen in one call does not reach the next.
    HD_CHECK_EQ(first.Translate("東京、"), std::string("도쿄,"));
    HD_CHECK_EQ(first.Translate("東京。"), std::string("동경."));
    HD_CHECK_EQ(independent.Translate("東京。"), std::string("동경."));
    HD_CHECK_EQ(reference.Translate("東京、\n東京。"), std::string("도쿄,\n동경."));
    first.Reset();
    HD_CHECK_EQ(first.Translate("東京。"), std::string("동경."));
    HD_CHECK(engine.Reload());
    first.Reset();
    HD_CHECK_EQ(first.Translate("東京。"), std::string("동경."));
    Engine referenceEngine(true, CacheWrite::Off);
    HD_REQUIRE(referenceEngine.Open({dictionary.path.string()}));
    Session inherited(referenceEngine);
    HD_CHECK_EQ(inherited.Translate("東京、\n東京。"), std::string("도쿄,\n동경."));
}

HD_TEST(document_senses_shadow_consistency) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    const auto generated = internal::NewGenerated(*snapshot);
    const std::string lines[] = {"東京、", "東京。", "東京、東京。"};
    for (const bool reference : {false, true}) {
        internal::TranslationOptions options;
        options.reference = reference;
        const auto expected = internal::TranslateDocument(*snapshot, *generated, lines, {}, options);
        HD_REQUIRE(expected.size() == 3);
        HD_CHECK_EQ(expected[0], "도쿄,");
        HD_CHECK_EQ(expected[1], "동경.");
        HD_CHECK_EQ(expected[2], "도쿄, 동경.");
        Choices observed;
        internal::StageObserver observer;
        observer.context = &observed;
        observer.resolved = Observe;
        options.selectShadow = true;
        const auto out = internal::TranslateDocument(*snapshot, *generated, lines, observer, options);
        HD_CHECK(out == expected);
        HD_REQUIRE(observed.choices.size() == 4);
        HD_CHECK(observed.choices[0].layer == word::Provenance::LocalRule);
        HD_CHECK(observed.choices[1].layer == word::Provenance::FirstXxx);
        if (reference) {
            HD_CHECK_EQ(observed.scored, std::size_t{0});
        } else {
            HD_CHECK(observed.scored > 0);
            HD_CHECK(observed.wouldChange > 0);
        }
    }
}

HD_TEST(document_senses_first_target_wins) {
    for (const bool committed : {false, true}) {
        word::SessionSenses memory;
        Remember(memory, "zz-oldest", "first");
        Remember(memory, "aa-next");
        if (committed) memory.FinishLine();
        Remember(memory, "zz-oldest", "replacement", "N130");
        const auto& first = (committed ? memory.choices : memory.pending).at("zz-oldest");
        HD_CHECK_EQ(first.target, std::string("first"));
        HD_CHECK_EQ(first.partCode, std::string("N110"));
        if (committed) HD_CHECK(memory.pending.empty());
    }
}
