#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/engine/rules.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/analyze.h"
#include "core/word/flush.h"
#include "core/word/worker.h"
#include "hdtest.h"
#include "rule_fixture.h"

using namespace hdTrans;

namespace {

std::shared_ptr<const dict::Snapshot> Dictionary(std::string_view rows, std::string_view plain = {}) {
    std::vector<dict::File> files{{0, "base.tsv",
        fixture::WithRuleBodies("key\tko\tpos\tsem\tgram\tdict\trule\tid\n" + std::string(rows))}};
    if (!plain.empty()) files.push_back({0, "terms.tsv", std::string(plain)});
    const dict::LoadResult loaded = dict::Load(std::move(files));
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

// The rules program the snapshot's rows assemble; the built-in one holds the global
// rules only, since a local rule is a row's condition.
const rules::Program& Rules(const dict::Snapshot& snapshot) {
    static std::vector<std::shared_ptr<internal::Generated>> kept;
    kept.push_back(internal::NewGenerated(snapshot));
    // A fixture whose rows carry no body runs the built-in program, as the engine does.
    return kept.back()->hasRules ? kept.back()->rules : internal::RulesProgram();
}

struct AnalyzedLine {
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Analysis analysis;

    bool Analyze(std::string_view text, const dict::Snapshot& snapshot, word::Settings settings = word::Settings::Reference(),
                 dict::LookupContext lookup = {}) {
        tokenizer::Tokenize(text, tokens);
        word::Analyze(tokens, snapshot, lookup, settings, arena, analysis);
        if (analysis.fault) return false;
        const word::Sentence* sentence = nullptr;
        for (const word::Sentence& s : analysis.sentences) {
            if (s.chain.head == nullptr) continue;
            if (sentence != nullptr) return false;
            sentence = &s;
        }
        if (sentence == nullptr) return false;
        word::Worker& w = *analysis.worker;
        w.chain = sentence->chain;
        w.current = sentence->current;
        w.transformSurface = sentence->transformSurface;
        return true;
    }

    word::Item* Find(std::string_view surface) const {
        for (word::Item* it = analysis.worker->chain.head; it != nullptr; it = it->next) {
            if (it->group->surface == surface) return it;
        }
        return nullptr;
    }
};

void CheckSelection(word::Selection choice, word::Provenance layer, int id = -1) {
    HD_CHECK_EQ(static_cast<int>(choice.layer), static_cast<int>(layer));
    HD_CHECK_EQ(choice.id, id);
}

}  // namespace

HD_TEST(word_provenance_defaults) {
    struct Case {
        const char* rows;
        int selected;
        word::Provenance layer;
        const char* target;
    };
    const Case cases[] = {
        {"人気\t인기\tN110\tXXX\t\tGN\t0\t1\n", 0, word::Provenance::Single, "인기"},
        {"人気\t인적\tN110\t\t\tGN\t0\t1\n"
         "人気\t인기\tN110\tXXX\t\tGN\t0\t2\n", 1, word::Provenance::FirstXxx, "인기"},
        {"人気\t인적\tN110\t\tJ1\tGN\t0\t1\n"
         "人気\t인기\tN110\t\tJ2\tGN\t0\t2\n", 1, word::Provenance::Digit, "인기"},
    };
    HD_REQUIRE(internal::RulesLoaded());
    for (const Case& c : cases) {
        const auto snapshot = Dictionary(c.rows);
        HD_REQUIRE(snapshot != nullptr);
        for (bool full : {false, true}) {
            AnalyzedLine line;
            HD_REQUIRE(line.Analyze("人気。", *snapshot));
            word::Item* it = line.Find("人気");
            HD_REQUIRE(it != nullptr);
            HD_CHECK_EQ(it->selected, -1);
            CheckSelection(it->choice, word::Provenance::None);
            CheckSelection(it->directChoice, word::Provenance::None);
            for (const word::Entry* e : it->group->entries) CheckSelection(e->targetChoice, word::Provenance::None);
            word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
            if (full) word::Flush(f);
            else word::CleanupSelectors(f);
            HD_REQUIRE(line.Find("人気") == it);
            HD_CHECK_EQ(it->selected, c.selected);
            HD_CHECK_EQ(std::string(it->EntryText(it->selected)), std::string(c.target));
            CheckSelection(it->choice, c.layer);
            CheckSelection(it->group->At(it->selected)->targetChoice, word::Provenance::None);
            CheckSelection(it->directChoice, word::Provenance::None);
        }
    }
}

HD_TEST(word_provenance_local_rule) {
    const auto snapshot = Dictionary(
        "人気\t인적\tN110\tXXX\t\tGN\t0\t1\n"
        "人気\t인기\tN110\t\t\tGN\t2\t2\n");
    HD_REQUIRE(snapshot != nullptr);
    HD_REQUIRE(internal::RulesLoaded());
    for (bool full : {false, true}) {
        AnalyzedLine line;
        HD_REQUIRE(line.Analyze("人気。", *snapshot));
        word::Item* it = line.Find("人気");
        HD_REQUIRE(it != nullptr);
        HD_REQUIRE(it->group->entries.size() == 2);
        HD_CHECK_EQ(it->RuleID(1), 2);
        word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
        if (full) word::Flush(f);
        else word::CleanupSelectors(f);
        HD_CHECK_EQ(it->selected, 1);
        HD_CHECK_EQ(std::string(it->EntryText(it->selected)), std::string("인기"));
        CheckSelection(it->choice, word::Provenance::LocalRule, 2);
        CheckSelection(it->group->At(1)->targetChoice, word::Provenance::None);
    }
}

HD_TEST(word_provenance_global_rule) {
    const auto snapshot = Dictionary(
        "東京\t도쿄\tN130\tQPNXXX\t\tGN\t0\t1\n"
        "東京\t도쿄\tN130\tQCT\t\tGN\t0\t2\n"
        "市\t시\tN110\tXXX\t\tGN\t0\t3\n");
    HD_REQUIRE(snapshot != nullptr);
    HD_REQUIRE(internal::RulesLoaded());
    for (bool full : {false, true}) {
        AnalyzedLine line;
        HD_REQUIRE(line.Analyze("東京市。", *snapshot));
        word::Item* it = line.Find("東京");
        HD_REQUIRE(it != nullptr);
        HD_REQUIRE(it->group->entries.size() == 2);
        word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
        if (full) word::Flush(f);
        else word::CleanupSelectors(f);
        HD_CHECK_EQ(it->selected, 1);
        HD_CHECK_EQ(std::string(it->EntryText(it->selected)), std::string("도쿄"));
        CheckSelection(it->choice, word::Provenance::GlobalRule, 150);
        CheckSelection(it->group->At(1)->targetChoice, word::Provenance::None);
    }
}

HD_TEST(word_provenance_senses_override_rule) {
    struct Case {
        const char* text;
        const char* target;
        bool matchingCandidate;
        word::Provenance layer;
        int row;
    };
    const Case cases[] = {
        {"彼女の声。", "소리", false, word::Provenance::Sense, 108},
        {"彼女の声。", "목소리", false, word::Provenance::Sense, 108},
        {"彼女の声。", "소리", true, word::Provenance::Sense, 108},
        {"声。", "소리", false, word::Provenance::SenseDefault, 923},
        {"声。", "목소리", false, word::Provenance::SenseDefault, 923},
        {"声。", "소리", true, word::Provenance::SenseDefault, 923},
    };
    HD_REQUIRE(internal::RulesLoaded());
    for (const Case& c : cases) {
        std::string rows = "彼女\t그녀\tN110\tVHn\t\tGN\t0\t1\n"
                           "の\t의\tP200\t\t\tGN\t0\t2\n"
                           "声\t" + std::string(c.target) + "\tN110\tXXX\t\tGN\t2\t3\n";
        if (c.matchingCandidate) rows += "声\t목소리\tN110\t\t\tGN\t0\t4\n";
        const auto snapshot = Dictionary(rows);
        HD_REQUIRE(snapshot != nullptr);
        for (bool full : {false, true}) {
            AnalyzedLine line;
            word::Settings settings = word::Settings::Reference();
            settings.text = true;
            HD_REQUIRE(line.Analyze(c.text, *snapshot, settings));
            word::Item* it = line.Find("声");
            HD_REQUIRE(it != nullptr);
            word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
            if (full) {
                word::Flush(f);
            } else {
                word::CleanupSelectors(f);
                HD_CHECK_EQ(it->selected, 0);
                HD_CHECK_EQ(std::string(it->EntryText(it->selected)), std::string(c.target));
                CheckSelection(it->choice, word::Provenance::LocalRule, 2);
                word::ChooseSenses(f);
            }
            HD_REQUIRE(line.Find("声") == it);
            HD_CHECK_EQ(it->selected, c.matchingCandidate ? 1 : 0);
            HD_CHECK_EQ(std::string(it->EntryText(it->selected)), std::string("목소리"));
            CheckSelection(it->choice, c.layer, c.row);
            const bool rewritten = !c.matchingCandidate && std::string_view(c.target) != "목소리";
            CheckSelection(it->group->At(it->selected)->targetChoice,
                           rewritten ? c.layer : word::Provenance::None, rewritten ? c.row : -1);
        }
    }
}

HD_TEST(word_provenance_fixed_ignores_senses) {
    const auto snapshot = Dictionary("声\t소리\tN110\tXXX\t\tGN\t2\t1\n", "声\t음성\tn\n");
    HD_REQUIRE(snapshot != nullptr);
    HD_REQUIRE(internal::RulesLoaded());
    for (bool full : {false, true}) {
        AnalyzedLine line;
        word::Settings settings = word::Settings::Reference();
        settings.text = true;
        dict::LookupContext lookup;
        lookup.plain = &snapshot->Plain();
        HD_REQUIRE(line.Analyze("声。", *snapshot, settings, lookup));
        word::Item* it = line.Find("声");
        HD_REQUIRE(it != nullptr);
        HD_REQUIRE(it->Fixed());
        word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
        if (full) word::Flush(f);
        else {
            word::CleanupSelectors(f);
            word::ChooseSenses(f);
        }
        HD_CHECK_EQ(it->selected, 0);
        HD_CHECK_EQ(std::string(it->EntryText(it->selected)), std::string("음성"));
        CheckSelection(it->choice, word::Provenance::Fixed);
        CheckSelection(it->group->At(0)->targetChoice, word::Provenance::None);
    }
}

HD_TEST(word_provenance_direct_selection) {
    struct Case {
        const char* text;
        const char* endings;
        int selected;
        word::Provenance layer;
        int id;
        const char* target;
    };
    const Case cases[] = {
        {"書く。", "V\t4다\tS500\tXXX\t\tGN\t0\t2\n",
         0, word::Provenance::Single, -1, "4다"},
        {"書く。", "V\t4다\tS500\t\t\tGN\t0\t2\nV\t4는다\tS500\tXXX\t\tGN\t0\t3\n",
         1, word::Provenance::FirstXxx, -1, "4는다"},
        {"書く。", "V\t4다\tS500\t\t\tGN\t0\t2\nV\t4는다\tS500\t\tJ2\tGN\t0\t3\n",
         1, word::Provenance::Digit, -1, "4는다"},
        {"書く。", "V\t4다\tS500\tXXX\t\tGN\t0\t2\nV\t4는다\tS500\t\t\tGN\t2\t3\n",
         1, word::Provenance::LocalRule, 2, "4는다"},
        {"書いて。", "V\t1고\tS570\tXXX\t\tGN\t0\t2\nV\t6\tS570\t\t\tGN\t0\t3\n",
         1, word::Provenance::GlobalRule, 330, "6"},
    };
    HD_REQUIRE(internal::RulesLoaded());
    for (const Case& c : cases) {
        const auto snapshot = Dictionary("書\t쓰\tM421\tXXX\t\tGN\t0\t1\n" + std::string(c.endings));
        HD_REQUIRE(snapshot != nullptr);
        for (bool full : {false, true}) {
            AnalyzedLine line;
            HD_REQUIRE(line.Analyze(c.text, *snapshot));
            word::Item* it = line.Find("書");
            HD_REQUIRE(it != nullptr);
            HD_REQUIRE(it->detail->Yougo());
            HD_CHECK_EQ(it->DirectIndex(), -1);
            CheckSelection(it->directChoice, word::Provenance::None);
            word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
            if (full) word::Flush(f);
            else word::CleanupSelectors(f);
            HD_CHECK_EQ(it->selected, 0);
            CheckSelection(it->choice, word::Provenance::Single);
            HD_CHECK_EQ(it->DirectIndex(), c.selected);
            HD_CHECK_EQ(std::string(it->DirectText(it->DirectIndex())), std::string(c.target));
            CheckSelection(it->directChoice, c.layer, c.id);
            HD_REQUIRE(it->DirectEntry(it->DirectIndex()) != nullptr);
            CheckSelection(it->DirectEntry(it->DirectIndex())->targetChoice, word::Provenance::None);
        }
    }
}

HD_TEST(word_provenance_pool_reuse) {
    const auto snapshot = Dictionary(
        "声\t소리\tN110\tXXX\t\tGN\t2\t1\n"
        "人気\t인기\tN110\tXXX\t\tGN\t0\t2\n"
        "書\t쓰\tM421\tXXX\tXj\tGN\t0\t3\n"
        "い\t있\tM427\tXXX\t\tGN\t0\t4\n"
        "V\t1고\tS570\tXXX\t\tGN\t0\t5\n"
        "V\t4다\tS500\tXXX\t\tGN\t0\t6\n");
    HD_REQUIRE(snapshot != nullptr);
    HD_REQUIRE(internal::RulesLoaded());
    AnalyzedLine line;
    word::Settings settings = word::Settings::Reference();
    settings.text = true;
    HD_REQUIRE(line.Analyze("声。", *snapshot, settings));
    word::Item* first = line.Find("声");
    HD_REQUIRE(first != nullptr);
    word::Entry* firstEntry = first->group->At(0);
    word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
    word::Flush(f);
    CheckSelection(first->choice, word::Provenance::SenseDefault, 923);
    CheckSelection(firstEntry->targetChoice, word::Provenance::SenseDefault, 923);

    HD_REQUIRE(line.Analyze("人気。", *snapshot));
    word::Item* noun = line.Find("人気");
    HD_REQUIRE(noun != nullptr && noun == first);
    HD_REQUIRE(noun->group->At(0) == firstEntry);
    HD_CHECK_EQ(noun->selected, -1);
    CheckSelection(noun->choice, word::Provenance::None);
    CheckSelection(noun->directChoice, word::Provenance::None);
    CheckSelection(firstEntry->targetChoice, word::Provenance::None);
    HD_CHECK_EQ(firstEntry->dst, std::string("인기"));

    HD_REQUIRE(line.Analyze("書いている。", *snapshot));
    word::Item* stem = line.Find("書");
    HD_REQUIRE(stem != nullptr);
    word::Flusher verbs{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
    word::Flush(verbs);
    CheckSelection(stem->directChoice, word::Provenance::GlobalRule, 240);
    HD_REQUIRE(stem->DirectEntry(stem->DirectIndex()) != nullptr);
    CheckSelection(stem->DirectEntry(stem->DirectIndex())->targetChoice, word::Provenance::GlobalRule, 240);

    HD_REQUIRE(line.Analyze("書く。", *snapshot));
    word::Item* reused = line.Find("書");
    HD_REQUIRE(reused != nullptr && reused == stem);
    HD_CHECK_EQ(reused->selected, -1);
    HD_CHECK_EQ(reused->DirectIndex(), -1);
    CheckSelection(reused->choice, word::Provenance::None);
    CheckSelection(reused->directChoice, word::Provenance::None);
    CheckSelection(line.analysis.worker->ruleChoice, word::Provenance::None);
    HD_REQUIRE(reused->DirectGroup() != nullptr);
    for (const word::Entry* e : reused->DirectGroup()->entries) CheckSelection(e->targetChoice, word::Provenance::None);
    HD_CHECK_EQ(std::string(reused->DirectText(0)), std::string("1고"));
}

HD_TEST(word_provenance_failed_local_assignment_is_undone) {
    const auto snapshot = Dictionary(
        "書\t쓰\tM421\tXXX\t\tGN\t0\t1\n"
        "時\t때\tN110\tXXX\t\tGN\t22\t2\n"
        "に\t에\tP200\t\t\tGN\t0\t3\n", "書く\t쓰는\tpat\n");
    HD_REQUIRE(snapshot != nullptr);
    HD_REQUIRE(internal::RulesLoaded());
    for (bool full : {false, true}) {
        AnalyzedLine line;
        dict::LookupContext lookup;
        lookup.plain = &snapshot->Plain();
        HD_REQUIRE(line.Analyze("書く時に。", *snapshot, {}, lookup));
        word::Item* it = line.Find("時");
        HD_REQUIRE(it != nullptr);
        HD_REQUIRE(it->prev != nullptr && word::OwnedGroup(it->prev->DirectGroup()));
        word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
        if (full) word::Flush(f);
        else word::CleanupSelectors(f);
        // The rule assigns 기회 and then fails on its last condition, so nothing it
        // wrote stays: the word reads as the default candidate and its selection is
        // the default one.
        HD_CHECK_EQ(it->selected, 0);
        HD_CHECK_EQ(std::string(it->EntryText(it->selected)), std::string("때"));
        CheckSelection(it->choice, word::Provenance::Single);
        CheckSelection(it->group->At(0)->targetChoice, word::Provenance::None);
        CheckSelection(it->prev->directChoice, word::Provenance::Fixed);
        it->compDI = it->selected;
        HD_CHECK(!rules::RunLocal(Rules(*snapshot), it, 22));
        CheckSelection(line.analysis.worker->ruleChoice, word::Provenance::None);
        HD_CHECK_EQ(std::string(it->EntryText(it->selected)), std::string("때"));
    }
}

HD_TEST(word_provenance_late_global_rule) {
    const auto snapshot = Dictionary(
        "書\t쓰\tM421\tXXX\tXj\tGN\t0\t1\n"
        "い\t있\tM427\tXXX\t\tGN\t0\t2\n"
        "V\t1고\tS570\tXXX\t\tGN\t0\t3\n"
        "V\t4다\tS500\tXXX\t\tGN\t0\t4\n");
    HD_REQUIRE(snapshot != nullptr);
    HD_REQUIRE(internal::RulesLoaded());
    for (bool full : {false, true}) {
        AnalyzedLine line;
        HD_REQUIRE(line.Analyze("書いている。", *snapshot));
        word::Item* stem = line.Find("書");
        word::Item* auxiliary = line.Find("い");
        HD_REQUIRE(stem != nullptr && auxiliary != nullptr);
        HD_REQUIRE(stem->next == auxiliary);
        int fired = 123;
        HD_CHECK(!rules::RunGlobal(internal::RulesProgram(), auxiliary, true, '4', false, &fired));
        HD_CHECK_EQ(fired, -1);
        word::Flusher f{*line.analysis.worker, Rules(*snapshot), snapshot->OutputFamilies()};
        if (full) word::Flush(f);
        else word::CleanupSelectors(f);
        HD_CHECK_EQ(auxiliary->selected, 0);
        HD_CHECK_EQ(std::string(auxiliary->EntryText(auxiliary->selected)), std::string());
        CheckSelection(auxiliary->choice, word::Provenance::GlobalRule, 240);
        CheckSelection(auxiliary->group->At(0)->targetChoice, word::Provenance::GlobalRule, 240);
        HD_CHECK_EQ(std::string(stem->DirectText(stem->DirectIndex())), std::string("4다"));
        CheckSelection(stem->choice, word::Provenance::Single);
        CheckSelection(stem->directChoice, word::Provenance::GlobalRule, 240);
        HD_REQUIRE(stem->DirectEntry(stem->DirectIndex()) != nullptr);
        CheckSelection(stem->DirectEntry(stem->DirectIndex())->targetChoice, word::Provenance::GlobalRule, 240);
        fired = -1;
        HD_CHECK(rules::RunGlobal(internal::RulesProgram(), auxiliary, true, '4', true, &fired));
        HD_CHECK_EQ(fired, 240);
    }
}

HD_TEST(word_unselected_semantic_alternatives) {
    const auto snapshot = Dictionary("普通\t보통\tN110\tVHn\t\tGN\t0\t1\n");
    HD_REQUIRE(snapshot != nullptr);
    for (bool alternatives : {false, true}) {
        word::Settings settings = word::Settings::Reference();
        settings.improvements.semanticAlternatives = alternatives;
        AnalyzedLine line;
        HD_REQUIRE(line.Analyze("普通", *snapshot, settings));
        word::Item* item = line.Find("普通");
        HD_REQUIRE(item != nullptr);
        HD_CHECK_EQ(item->selected, -1);
        HD_CHECK_EQ(item->CheckAttr("QPNVHn", -1), alternatives);
        HD_CHECK(item->CheckAttr("VHnQPN", -1));
        HD_CHECK(item->CheckAttr("QPNVHn", 0));
        HD_CHECK(!item->CheckAttr("", -1));
        HD_CHECK(!item->CheckAttr("QPNXXX", -1));
    }
}
