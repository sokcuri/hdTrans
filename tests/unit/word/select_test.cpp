#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "core/dict/snapshot.h"
#include "core/word/flush.h"
#include "core/word/document_senses.h"
#include "core/word/select.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::shared_ptr<const dict::Snapshot> Dictionary() {
    const auto loaded = dict::Load({{0, "base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"}});
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

struct Candidate {
    std::string_view target;
    std::string_view part = "N110";
    std::string_view semantic = {};
    std::string_view grammar = {};
};

struct SelectLine {
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Worker worker;
    rules::Program program;
    word::Flusher flusher;

    explicit SelectLine(const dict::Snapshot& snapshot, word::Settings settings = word::Settings::Reference())
        : worker(tokens, snapshot, {}, settings, arena, {}),
          flusher{worker, program, snapshot.OutputFamilies()} {}

    word::Group* MakeGroup(std::string_view surface, std::initializer_list<Candidate> candidates) {
        word::Group* group = worker.NewGroup(nullptr, '0');
        group->surface = surface;
        group->length = static_cast<int>(surface.size());
        for (const Candidate& candidate : candidates) {
            word::Record record;
            record.target = candidate.target;
            record.partCode = candidate.part;
            record.semantic = candidate.semantic;
            record.attr = candidate.grammar;
            group->entries.push_back(worker.NewEntry(record));
        }
        return group;
    }

    word::Item& Add(std::string_view surface, std::initializer_list<Candidate> candidates,
                    word::Selection choice = {word::Provenance::FirstXxx}, int selected = 0) {
        word::Group* group = MakeGroup(surface, candidates);
        word::Item* it = worker.NewItem(worker.NewDetail(word::kDetailInfo, 0), group, group->surface, 0, 0);
        it->SelectCandidate(selected, choice);
        it->prev = worker.chain.tail;
        if (it->prev != nullptr) it->prev->next = it;
        else worker.chain.head = it;
        worker.chain.tail = it;
        return *it;
    }
};

void CheckChoice(word::Selection choice, word::Provenance layer, int id = -1) {
    HD_CHECK_EQ(static_cast<int>(choice.layer), static_cast<int>(layer));
    HD_CHECK_EQ(choice.id, id);
}

void CheckTargets(const word::Item& it, std::initializer_list<std::string_view> targets) {
    HD_CHECK_EQ(it.group->entries.size(), targets.size());
    int slot = 0;
    for (const auto target : targets) HD_CHECK_EQ(std::string(it.EntryText(slot++)), std::string(target));
}

int EqualScore(const word::Item&, int, const word::SelectContext&) { return 0; }

int PopularityScore(const word::Item& it, int candidate, const word::SelectContext&) {
    return it.EntryText(candidate) == "인기" ? 10 : 0;
}

int ReputationScore(const word::Item& it, int candidate, const word::SelectContext&) {
    return it.EntryText(candidate) == "명성" ? 100 : 0;
}

int JotScore(const word::Item& it, int candidate, const word::SelectContext& context) {
    return &context.snapshot == &it.w->snapshot && &context.chain == &it.w->chain &&
           it.EntryText(candidate) == "적" ? 100 : 0;
}

}  // namespace

HD_TEST(word_select_empty_scorers) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::Settings settings = word::Settings::Reference();
    HD_CHECK(settings.scorers.empty());
    HD_CHECK(!settings.selectShadow);
    HD_CHECK(!settings.selectRules);
    SelectLine line(*snapshot, settings);
    word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {word::Provenance::Digit, 17});
    word::Select(line.flusher);
    HD_CHECK_EQ(it.selected, 0);
    CheckChoice(it.choice, word::Provenance::Digit, 17);
    CheckTargets(it, {"유명세", "인기"});
    HD_CHECK(it.shadowChoices.empty());
}

HD_TEST(word_select_eligible_layers) {
    struct Case {
        word::Provenance layer;
        bool defaults;
        bool rules;
    };
    const Case cases[] = {
        {word::Provenance::FirstXxx, true, true},
        {word::Provenance::Digit, false, true},
        {word::Provenance::LocalRule, false, true},
        {word::Provenance::GlobalRule, false, true},
        {word::Provenance::Fixed, false, false},
        {word::Provenance::Sense, false, false},
        {word::Provenance::SenseDefault, false, false},
        {word::Provenance::Single, false, false},
        {word::Provenance::Rewrite, false, false},
        {word::Provenance::Initial, false, false},
        {word::Provenance::Scorer, false, false},
        {word::Provenance::None, false, false},
    };
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const Case& c : cases) {
        for (bool rules : {false, true}) {
            word::Settings settings = word::Settings::Reference();
            settings.scorers = scorers;
            settings.selectRules = rules;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {c.layer, 41});
            word::Select(line.flusher);
            const bool changed = rules ? c.rules : c.defaults;
            HD_CHECK_EQ(it.selected, changed ? 1 : 0);
            CheckChoice(it.choice, changed ? word::Provenance::Scorer : c.layer, changed ? 0 : 41);
            CheckTargets(it, {"유명세", "인기"});
            for (const word::Entry* entry : it.group->entries) CheckChoice(entry->targetChoice, word::Provenance::None);
            HD_CHECK(it.shadowChoices.empty());
        }
    }
}

HD_TEST(word_select_senses_run_first) {
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    HD_REQUIRE(snapshot->Senses().contains("声"));
    for (bool triggered : {false, true}) {
        for (bool matching : {false, true}) {
            for (bool scoring : {false, true}) {
                word::Settings settings = word::Settings::Reference();
                if (scoring) settings.scorers = scorers;
                settings.selectRules = true;
                SelectLine line(*snapshot, settings);
                if (triggered) {
                    line.Add("彼女", {{"그녀"}}, {word::Provenance::Single});
                    line.Add("の", {{"의", "P200"}}, {word::Provenance::Single});
                }
                word::Item& it = matching
                    ? line.Add("声", {{"소리"}, {"목소리"}, {"음"}}, {word::Provenance::LocalRule, 2})
                    : line.Add("声", {{"소리"}, {"음"}}, {word::Provenance::LocalRule, 2});
                word::Select(line.flusher);
                const word::Provenance layer = triggered ? word::Provenance::Sense : word::Provenance::SenseDefault;
                const int row = triggered ? 107 : 923;
                HD_CHECK_EQ(it.selected, matching ? 1 : 0);
                CheckChoice(it.choice, layer, row);
                if (matching) CheckTargets(it, {"소리", "목소리", "음"});
                else CheckTargets(it, {"목소리", "음"});
                CheckChoice(it.group->At(it.selected)->targetChoice,
                            matching ? word::Provenance::None : layer, matching ? -1 : row);
                HD_CHECK(it.shadowChoices.empty());
            }
        }
    }
}

HD_TEST(word_select_ties) {
    struct Case {
        std::initializer_list<Candidate> candidates;
        int current;
        int selected;
    };
    const Case cases[] = {
        {{{"인기"}, {"명성"}, {"평판"}}, 2, 2},
        {{{"유명세"}, {"인기"}, {"명성"}}, 0, 1},
        {{{"인기"}, {"명성"}, {"유명세"}}, 2, 0},
    };
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const Case& c : cases) {
        word::Settings settings = word::Settings::Reference();
        settings.scorers = scorers;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("人気", c.candidates, {word::Provenance::FirstXxx, 23}, c.current);
        word::Select(line.flusher);
        HD_CHECK_EQ(it.selected, c.selected);
        const bool changed = c.current != c.selected;
        CheckChoice(it.choice, changed ? word::Provenance::Scorer : word::Provenance::FirstXxx, changed ? 0 : 23);
    }
}

HD_TEST(word_select_scorer_order) {
    const word::Scorer equal{"equal", EqualScore};
    const word::Scorer popularity{"popularity", PopularityScore};
    const word::Scorer reputation{"reputation", ReputationScore};
    struct Case {
        word::Scorer scorers[3];
        int selected;
        int scorer;
    };
    const Case cases[] = {
        {{equal, popularity, reputation}, 1, 1},
        {{popularity, reputation, equal}, 1, 0},
        {{reputation, popularity, equal}, 2, 0},
        {{equal, equal, equal}, 0, -1},
        {{{"disabled", nullptr}, equal, popularity}, 1, 2},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const Case& c : cases) {
        word::Settings settings = word::Settings::Reference();
        settings.scorers = c.scorers;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}, {"명성"}});
        word::Select(line.flusher);
        HD_CHECK_EQ(it.selected, c.selected);
        CheckChoice(it.choice, c.scorer < 0 ? word::Provenance::FirstXxx : word::Provenance::Scorer, c.scorer);
    }
}

HD_TEST(word_select_shadow_all_scorers_without_mutation) {
    const word::Scorer scorers[] = {
        {"equal", EqualScore}, {"shortest", word::ShortestTargetScore}, {"jot", JotScore},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::Settings settings = word::Settings::Reference();
    settings.shadowScorers = scorers;
    settings.selectShadow = true;
    SelectLine line(*snapshot, settings);
    word::Item& it = line.Add("書", {{"기입하", "M421"}, {"적", "M422"}, {"기록하", "M421"}},
                              {word::Provenance::FirstXxx, 19}, 2);
    it.AssignEntrySurface(0, "쓰", {word::Provenance::Rewrite, 8});
    it.detail->cls = word::kDetailVerb;
    it.detail->group = line.MakeGroup("く", {{"4다", "S500"}, {"4는다", "S500"}});
    it.detail->ResetDirect(it.detail->group->entries.size());
    it.SetDirectIndex(1, {word::Provenance::GlobalRule, 37});
    it.compDI = 1;
    it.compUmiDI = 0;
    word::Select(line.flusher);
    HD_CHECK_EQ(it.selected, 2);
    CheckChoice(it.choice, word::Provenance::FirstXxx, 19);
    CheckTargets(it, {"쓰", "적", "기록하"});
    CheckChoice(it.group->At(0)->targetChoice, word::Provenance::Rewrite, 8);
    CheckChoice(it.group->At(1)->targetChoice, word::Provenance::None);
    CheckChoice(it.group->At(2)->targetChoice, word::Provenance::None);
    HD_CHECK_EQ(it.compDI, 1);
    HD_CHECK_EQ(it.compUmiDI, 0);
    HD_CHECK_EQ(it.DirectIndex(), 1);
    CheckChoice(it.directChoice, word::Provenance::GlobalRule, 37);
    HD_CHECK_EQ(std::string(it.DirectText(0)), std::string("4다"));
    HD_CHECK_EQ(std::string(it.DirectText(1)), std::string("4는다"));
    for (const word::Entry* entry : it.DirectGroup()->entries) CheckChoice(entry->targetChoice, word::Provenance::None);
    HD_REQUIRE(it.shadowChoices.size() == 3);
    struct Expected {
        const char* name;
        int candidate;
        int currentScore;
        int score;
        bool winner;
        const char* target;
    };
    const Expected expected[] = {
        {"equal", 2, 0, 0, false, "기록하"},
        {"shortest", 0, -3, -1, true, "쓰"},
        {"jot", 1, 0, 100, false, "적"},
    };
    for (std::size_t i = 0; i < it.shadowChoices.size(); i++) {
        const auto& choice = it.shadowChoices[i];
        const auto& want = expected[i];
        HD_CHECK_EQ(std::string(choice.scorer), std::string(want.name));
        HD_CHECK_EQ(choice.from, 2);
        HD_CHECK_EQ(choice.candidate, want.candidate);
        HD_CHECK_EQ(choice.currentScore, want.currentScore);
        HD_CHECK_EQ(choice.score, want.score);
        HD_CHECK(choice.eligible);
        HD_CHECK_EQ(choice.winner, want.winner);
        HD_CHECK_EQ(choice.target, std::string(want.target));
    }
}

HD_TEST(word_select_shadow_blocked_recommendations) {
    const word::Provenance layers[] = {
        word::Provenance::Digit, word::Provenance::LocalRule, word::Provenance::GlobalRule,
        word::Provenance::Sense, word::Provenance::SenseDefault, word::Provenance::Single,
        word::Provenance::Rewrite, word::Provenance::None,
    };
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}, {"popularity", PopularityScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (word::Provenance layer : layers) {
        for (bool rules : {false, true}) {
            word::Settings settings = word::Settings::Reference();
            settings.shadowScorers = scorers;
            settings.selectShadow = true;
            settings.selectRules = rules;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {layer, 43});
            word::Select(line.flusher);
            HD_CHECK_EQ(it.selected, 0);
            CheckChoice(it.choice, layer, 43);
            CheckTargets(it, {"유명세", "인기"});
            HD_REQUIRE(it.shadowChoices.size() == 2);
            const bool eligible = rules && (layer == word::Provenance::Digit ||
                                            layer == word::Provenance::LocalRule || layer == word::Provenance::GlobalRule);
            for (std::size_t i = 0; i < it.shadowChoices.size(); i++) {
                const auto& choice = it.shadowChoices[i];
                HD_CHECK_EQ(std::string(choice.scorer), std::string(scorers[i].name));
                HD_CHECK_EQ(choice.from, 0);
                HD_CHECK_EQ(choice.candidate, 1);
                HD_CHECK_EQ(choice.currentScore, i == 0 ? -3 : 0);
                HD_CHECK_EQ(choice.score, i == 0 ? -2 : 10);
                HD_CHECK_EQ(choice.eligible, eligible);
                HD_CHECK_EQ(choice.winner, eligible && i == 0);
                HD_CHECK_EQ(choice.target, std::string("인기"));
            }
        }
    }
}

HD_TEST(word_select_skips_fixed_invalid_and_small_groups) {
    struct Case {
        int count;
        int selected;
        bool fixed;
    };
    const Case cases[] = {{2, 0, true}, {2, -1, false}, {2, 2, false}, {0, 0, false}, {1, 0, false}};
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const Case& c : cases) {
        for (bool shadow : {false, true}) {
            word::Settings settings = word::Settings::Reference();
            if (shadow) settings.shadowScorers = scorers;
            else settings.scorers = scorers;
            settings.selectShadow = shadow;
            settings.selectRules = true;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {word::Provenance::FirstXxx, 29}, c.selected);
            it.group->At(0)->fixed = c.fixed;
            it.group->entries.resize(static_cast<std::size_t>(c.count));
            word::Select(line.flusher);
            HD_CHECK_EQ(it.selected, c.selected);
            CheckChoice(it.choice, word::Provenance::FirstXxx, 29);
            HD_CHECK(it.shadowChoices.empty());
        }
    }
}

HD_TEST(word_select_same_class_digit_only) {
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (bool shadow : {false, true}) {
        for (bool compatible : {false, true}) {
            word::Settings settings = word::Settings::Reference();
            if (shadow) settings.shadowScorers = scorers;
            else settings.scorers = scorers;
            settings.selectShadow = shadow;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add("人気", {{"유명세", "N110"}, {"인", "M421"}, {"", ""}, {"인기", "N130"}});
            if (!compatible) it.group->entries.resize(3);
            word::Select(line.flusher);
            const int best = compatible ? 3 : 0;
            HD_CHECK_EQ(it.selected, shadow ? 0 : best);
            CheckChoice(it.choice, !shadow && compatible ? word::Provenance::Scorer : word::Provenance::FirstXxx,
                        !shadow && compatible ? 0 : -1);
            if (shadow) {
                HD_REQUIRE(it.shadowChoices.size() == 1);
                const auto& choice = it.shadowChoices[0];
                HD_CHECK_EQ(choice.from, 0);
                HD_CHECK_EQ(choice.candidate, best);
                HD_CHECK_EQ(choice.currentScore, -3);
                HD_CHECK_EQ(choice.score, compatible ? -2 : -3);
                HD_CHECK(choice.eligible);
                HD_CHECK_EQ(choice.winner, compatible);
            } else HD_CHECK(it.shadowChoices.empty());
        }
    }
}

HD_TEST(word_select_shortest_target_counts_scalars_without_mutation) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    SelectLine line(*snapshot);
    word::Item& it = line.Add("文字", {{"한국어"}, {"가"}, {"가"}, {"한\U00020000"}, {""}},
                              {word::Provenance::Digit, 31}, 1);
    it.compDI = 3;
    const word::SelectContext context{*snapshot, line.worker.chain};
    const int expected[] = {-3, -1, -2, -2, 0};
    for (int candidate = 0; candidate < 5; candidate++) {
        HD_CHECK_EQ(word::ShortestTargetScore(it, candidate, context), expected[candidate]);
    }
    HD_CHECK_EQ(word::ShortestTargetScore(it, -1, context), 0);
    HD_CHECK_EQ(word::ShortestTargetScore(it, 5, context), 0);
    HD_CHECK_EQ(it.selected, 1);
    CheckChoice(it.choice, word::Provenance::Digit, 31);
    HD_CHECK_EQ(it.compDI, 3);
    CheckTargets(it, {"한국어", "가", "가", "한\U00020000", ""});
    for (const word::Entry* entry : it.group->entries) CheckChoice(entry->targetChoice, word::Provenance::None);
    CheckChoice(it.directChoice, word::Provenance::None);
    HD_CHECK(it.shadowChoices.empty());
}

HD_TEST(word_neighbour_candidate_slots) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (bool preserve : {false, true}) {
        word::Settings settings = word::Settings::Reference();
        settings.improvements.preserveNeighbourSlots = preserve;
        SelectLine line(*snapshot, settings);
        auto& left = line.Add("普通", {{"보통"}});
        auto& right = line.Add("名前", {{"이름"}});
        left.compDI = 0;
        left.compUmiDI = 3;
        right.compDI = 2;
        right.compUmiDI = 4;
        HD_CHECK(left.GetObject(1) == &right);
        HD_CHECK_EQ(right.compDI, preserve ? 2 : -1);
        HD_CHECK_EQ(right.compUmiDI, preserve ? 4 : -1);
        HD_CHECK(left.GetObject(0) == &left);
        HD_CHECK_EQ(left.compDI, 0);
        HD_CHECK_EQ(left.compUmiDI, 3);
        HD_CHECK(right.GetObject(-1) == &left);
        HD_CHECK_EQ(left.compDI, preserve ? 0 : -1);
        HD_CHECK_EQ(left.compUmiDI, preserve ? 3 : -1);
    }
}

HD_TEST(document_senses_trusted_layers) {
    const word::Provenance layers[] = {
        word::Provenance::LocalRule, word::Provenance::Sense,
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto layer : layers) {
        word::SessionSenses senses;
        word::Settings settings;
        settings.improvements.documentSenses = true;
        settings.sessionSenses = &senses;
        {
            SelectLine first(*snapshot, settings);
            first.Add("人気", {{"유명세"}, {"인기"}}, {layer, 7}, 1);
            word::Item& sameLine = first.Add("人気", {{"유명세"}, {"인기"}});
            word::Select(first.flusher);
            HD_CHECK_EQ(sameLine.selected, 0);
            HD_CHECK(senses.choices.empty());
            HD_CHECK_EQ(senses.pending.at("人気").target, std::string("인기"));
        }
        senses.FinishLine();
        HD_CHECK(senses.pending.empty());
        SelectLine second(*snapshot, settings);
        word::Item& it = second.Add("人気", {{"유명세"}, {"인기"}});
        word::Select(second.flusher);
        HD_CHECK_EQ(it.selected, 1);
        CheckChoice(it.choice, word::Provenance::Scorer, 0);
        CheckTargets(it, {"유명세", "인기"});
    }
}

HD_TEST(document_senses_only_frequency_defaults) {
    const word::Provenance layers[] = {
        word::Provenance::FirstXxx, word::Provenance::Digit,
        word::Provenance::LocalRule, word::Provenance::GlobalRule,
        word::Provenance::Sense, word::Provenance::SenseDefault,
        word::Provenance::Single, word::Provenance::Initial,
        word::Provenance::Rewrite, word::Provenance::Fixed,
        word::Provenance::Scorer, word::Provenance::None,
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto layer : layers) {
        word::SessionSenses senses;
        senses.choices.emplace("人気", word::SessionSense{"인기", "N110"});
        word::Settings settings;
        settings.improvements.documentSenses = true;
        settings.sessionSenses = &senses;
        settings.selectRules = true;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {layer, 41});
        word::Select(line.flusher);
        const bool changed = layer == word::Provenance::FirstXxx;
        HD_CHECK_EQ(it.selected, changed ? 1 : 0);
        CheckChoice(it.choice, changed ? word::Provenance::Scorer : layer, changed ? 0 : 41);
    }
}

HD_TEST(document_senses_do_not_learn_defaults_or_fixed_items) {
    const word::Provenance layers[] = {
        word::Provenance::GlobalRule, word::Provenance::SenseDefault,
        word::Provenance::FirstXxx, word::Provenance::Digit,
        word::Provenance::Single, word::Provenance::Initial,
        word::Provenance::Rewrite, word::Provenance::Fixed,
        word::Provenance::Scorer, word::Provenance::None,
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto layer : layers) {
        word::SessionSenses senses;
        word::Settings settings;
        settings.improvements.documentSenses = true;
        settings.sessionSenses = &senses;
        SelectLine line(*snapshot, settings);
        line.Add("人気", {{"유명세"}, {"인기"}}, {layer}, 1);
        word::Select(line.flusher);
        senses.FinishLine();
        HD_CHECK(senses.choices.empty());
    }
    word::SessionSenses senses;
    word::Settings settings;
    settings.improvements.documentSenses = true;
    settings.sessionSenses = &senses;
    SelectLine line(*snapshot, settings);
    word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {word::Provenance::LocalRule}, 1);
    it.group->At(0)->fixed = true;
    word::Select(line.flusher);
    senses.FinishLine();
    HD_CHECK(senses.choices.empty());
}

HD_TEST(document_senses_first_trusted_target_and_exact_key) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::SessionSenses senses;
    word::Settings settings;
    settings.improvements.documentSenses = true;
    settings.sessionSenses = &senses;
    {
        SelectLine first(*snapshot, settings);
        first.Add("人気", {{"유명세"}, {"인기"}}, {word::Provenance::LocalRule}, 1);
        first.Add("人気", {{"유명세"}, {"인기"}}, {word::Provenance::LocalRule}, 0);
        word::Select(first.flusher);
    }
    senses.FinishLine();
    {
        SelectLine second(*snapshot, settings);
        word::Item& it = second.Add("人気", {{"유명세"}, {"인기"}}, {word::Provenance::GlobalRule}, 0);
        word::Select(second.flusher);
        HD_CHECK_EQ(it.selected, 0);
    }
    senses.FinishLine();
    SelectLine third(*snapshot, settings);
    word::Item& reordered = third.Add("人気", {{"인기"}, {"유명세"}}, {word::Provenance::FirstXxx}, 1);
    word::Item& other = third.Add("人気者", {{"유명세"}, {"인기"}});
    word::Select(third.flusher);
    HD_CHECK_EQ(reordered.selected, 0);
    HD_CHECK_EQ(other.selected, 0);
    HD_CHECK_EQ(senses.choices.at("人気").target, std::string("인기"));
}

HD_TEST(document_senses_ties_missing_and_incompatible_targets) {
    struct Case {
        std::initializer_list<Candidate> candidates;
        int current;
        int selected;
    };
    const Case cases[] = {
        {{{"인기"}, {"인기"}}, 1, 1},
        {{{"유명세"}, {"명성"}}, 0, 0},
        {{{"유명세", "N110"}, {"인기", "M421"}}, 0, 0},
        {{{"인기"}, {"유명세"}, {"인기"}}, 1, 0},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto& c : cases) {
        word::SessionSenses senses;
        senses.choices.emplace("人気", word::SessionSense{"인기", "N110"});
        word::Settings settings;
        settings.improvements.documentSenses = true;
        settings.sessionSenses = &senses;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("人気", c.candidates, {word::Provenance::FirstXxx, 19}, c.current);
        word::Select(line.flusher);
        HD_CHECK_EQ(it.selected, c.selected);
        CheckChoice(it.choice, c.current == c.selected ? word::Provenance::FirstXxx : word::Provenance::Scorer,
                    c.current == c.selected ? 19 : 0);
    }
}

HD_TEST(document_senses_flag_and_reference_mode) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    HD_CHECK(!word::Improvements{}.documentSenses);
    HD_CHECK(!word::Improvements::Reference().documentSenses);
    for (const bool reference : {false, true}) {
        word::SessionSenses senses;
        senses.choices.emplace("人気", word::SessionSense{"인기", "N110"});
        word::Settings settings = reference ? word::Settings::Reference() : word::Settings{};
        if (!reference) settings.improvements.documentSenses = false;
        settings.sessionSenses = &senses;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}});
        line.Add("評判", {{"평판"}, {"명성"}}, {word::Provenance::LocalRule}, 1);
        word::Select(line.flusher);
        HD_CHECK_EQ(it.selected, 0);
        HD_CHECK(senses.pending.empty());
    }
}

HD_TEST(document_senses_shadow_and_scorer_order) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    const word::Scorer scorers[] = {{"reputation", ReputationScore}};
    for (const bool shadow : {false, true}) {
        word::SessionSenses senses;
        senses.choices.emplace("人気", word::SessionSense{"인기", "N110"});
        word::Settings settings;
        settings.improvements.documentSenses = true;
        settings.sessionSenses = &senses;
        settings.selectShadow = shadow;
        settings.selectRules = true;
        settings.scorers = scorers;
        if (shadow) settings.shadowScorers = scorers;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}, {"명성"}}, {word::Provenance::FirstXxx, 9});
        word::Select(line.flusher);
        HD_CHECK_EQ(it.selected, 1);
        CheckTargets(it, {"유명세", "인기", "명성"});
        HD_CHECK(senses.pending.empty());
        HD_CHECK_EQ(senses.choices.at("人気").target, std::string("인기"));
        if (shadow) {
            HD_REQUIRE(it.shadowChoices.size() == 2);
            const auto& choice = it.shadowChoices[0];
            HD_CHECK_EQ(std::string(choice.scorer), std::string("document-senses"));
            HD_CHECK_EQ(choice.from, 0);
            HD_CHECK_EQ(choice.candidate, 1);
            HD_CHECK_EQ(choice.currentScore, 0);
            HD_CHECK_EQ(choice.score, 1);
            HD_CHECK(choice.eligible && choice.winner);
            HD_CHECK(!it.shadowChoices[1].winner);
        } else HD_CHECK(it.shadowChoices.empty());
        CheckChoice(it.choice, word::Provenance::Scorer, 0);
    }
}

HD_TEST(document_senses_score_is_pure) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::SessionSenses senses;
    senses.choices.emplace("人気", word::SessionSense{"인기", "N110"});
    word::Settings settings;
    settings.improvements.documentSenses = true;
    settings.sessionSenses = &senses;
    SelectLine line(*snapshot, settings);
    word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}});
    const word::SelectContext context{*snapshot, line.worker.chain};
    for (const int candidate : {-1, 0, 1, 2}) {
        HD_CHECK_EQ(word::DocumentSenseScore(it, candidate, context), candidate == 1 ? 1 : 0);
    }
    HD_CHECK_EQ(it.selected, 0);
    HD_CHECK(it.shadowChoices.empty());
    HD_CHECK(senses.pending.empty());
    HD_CHECK_EQ(senses.choices.size(), std::size_t{1});
    CheckTargets(it, {"유명세", "인기"});
}

HD_TEST(document_senses_content_keys_and_exact_part_code) {
    struct Case {
        const char* key;
        const char* earlierPart;
        const char* currentPart;
        const char* proposedPart;
        bool changed;
    };
    const Case cases[] = {
        {"王", "N110", "N110", "N110", false},
        {"こと", "N110", "N110", "N110", false},
        {"ぐー", "N110", "N110", "N110", false},
        {"に向か", "M421", "M421", "M421", true},
        {"人気", "N110", "N110", "N110", true},
        {"人気", "N130", "N110", "N130", false},
        {"人気", "N110", "N110", "N130", false},
        {"人気", "N110", "M110", "N110", false},
        {"人気", "P200", "P200", "P200", false},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto& c : cases) {
        word::SessionSenses senses;
        word::Settings settings;
        settings.improvements.documentSenses = true;
        settings.sessionSenses = &senses;
        {
            SelectLine first(*snapshot, settings);
            first.Add(c.key, {{"유명세", c.earlierPart}, {"인기", c.earlierPart}},
                      {word::Provenance::LocalRule}, 1);
            word::Select(first.flusher);
        }
        senses.FinishLine();
        SelectLine second(*snapshot, settings);
        word::Item& it = second.Add(c.key, {{"유명세", c.currentPart}, {"인기", c.proposedPart}});
        word::Select(second.flusher);
        HD_CHECK_EQ(it.selected, c.changed ? 1 : 0);
    }
}

HD_TEST(document_senses_shadow_observes_disabled_improvement) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::SessionSenses senses;
    senses.choices.emplace("人気", word::SessionSense{"인기", "N110"});
    word::Settings settings;
    HD_CHECK(!settings.improvements.documentSenses);
    settings.selectShadow = true;
    settings.sessionSenses = &senses;
    SelectLine line(*snapshot, settings);
    word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}});
    word::Select(line.flusher);
    HD_CHECK_EQ(it.selected, 0);
    HD_REQUIRE(it.shadowChoices.size() == 1);
    HD_CHECK(it.shadowChoices[0].winner);
    HD_CHECK_EQ(std::string(it.shadowChoices[0].scorer), std::string("document-senses"));
}

HD_TEST(document_senses_lexical_stem) {
    struct Case { std::string_view target, stem; };
    const Case cases[] = {
        {"", ""}, {"   ", ""}, {"인기", "인기"}, {"사과", "사과"},
        {"구&AV@", "구"}, {"요구&AV@5다", "요구"}, {"예%C9@", "예"},
        {"할 수 $FA@", ""}, {"&HN@", ""}, {" 5할 수 구&AV@suffix ", "구"},
        {"A요구&AV@", "요구"}, {"할 수&AV@", ""}, {"할 수확&AV@", "할 수확"},
        {"(이)라고 말&AV@", "말"}, {"(이)라고 &AV@", ""},
        {"(을)를 향&AV@", "향"}, {"(으)로 향&AV@", "향"},
        {"해 주&AE@", "주"}, {"해결&AV@", "해결"},
        {"이번에", "이번"}, {"이번에는 ", "이번"}, {"서울에서", "서울"},
        {"마음을", "마음"}, {"마음 은", "마음"}, {"나무", "나무"},
        {"단어&broken", "단어&broken"}, {"단어%A@", "단어%A@"}, {"R&D", "R&D"},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(std::string(word::DocumentSenseStem(c.target)), std::string(c.stem));
    }
}

HD_TEST(document_senses_reject_grammar_only_changes) {
    struct Case { const char* key; const char* earlier; const char* current; };
    const Case cases[] = {
        {"出来", "&HN@", "할 수 &HN@"},
        {"出来な", "$FA@", "할 수 $FA@"},
        {"今度", "이번에", "이번"},
        {"と言", "(이)라고 &AV@", "(이)라고 말&AV@"},
        {"に向か", "(을)를 향&AV@", "(으)로 향&AV@"},
        {"下さ", "해 주&AE@", "주&AE@"},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto& c : cases) {
        for (const bool reverse : {false, true}) {
            word::SessionSenses senses;
            const auto earlier = reverse ? c.current : c.earlier;
            const auto current = reverse ? c.earlier : c.current;
            senses.choices.emplace(c.key, word::SessionSense{earlier, "M421"});
            word::Settings settings;
            settings.improvements.documentSenses = true;
            settings.sessionSenses = &senses;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add(c.key, {{current, "M421"}, {earlier, "M421"}});
            word::Select(line.flusher);
            HD_CHECK_EQ(it.selected, 0);
        }
    }
}

HD_TEST(document_senses_keep_current_target_form) {
    struct Case {
        const char* earlier;
        const char* current;
        const char* proposed;
        bool changed;
    };
    const Case cases[] = {
        {"구&AV@", "요구&AV@", "구&AV@", true},
        {"구&AV@", "5할 수 요구&AV@tail", "5할 수 구&AV@tail", true},
        {"구&AV@", "요구&AV@", "구$AV@", false},
        {"구&AV@", "요구&AV@", "구&CV@", false},
        {"구&AV@", "요구&AV@", "구&A1@", false},
        {"구&AV@", "5요구&AV@", "4구&AV@", false},
        {"구&AV@", "요구&AV@tail", "구&AV@other", false},
        {"구&AV@", "할 수 요구&AV@", "구&AV@", false},
        {"기분", "마음에", "기분", false},
        {"기분에", "마음에는", "기분에는", true},
        {"구&AV@", "요구&AV@", "요구&AV@", false},
        {"추구&AV@", "요구&AV@", "구&AV@", false},
        {"&AV@", "요구&AV@", "구&AV@", false},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto& c : cases) {
        word::SessionSenses senses;
        senses.choices.emplace("求め", word::SessionSense{c.earlier, "M421"});
        word::Settings settings;
        settings.improvements.documentSenses = true;
        settings.sessionSenses = &senses;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("求め", {{c.current, "M421"}, {c.proposed, "M421"}});
        word::Select(line.flusher);
        HD_CHECK_EQ(it.selected, c.changed ? 1 : 0);
        CheckTargets(it, {c.current, c.proposed});
    }
}

HD_TEST(document_senses_find_remembered_stem_in_current_form) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::SessionSenses senses;
    word::Settings settings;
    settings.improvements.documentSenses = true;
    settings.sessionSenses = &senses;
    {
        SelectLine first(*snapshot, settings);
        first.Add("求め", {{"요구&AV@", "M421"}, {"구&AV@", "M421"}}, {word::Provenance::Sense}, 1);
        word::Select(first.flusher);
    }
    senses.FinishLine();
    SelectLine second(*snapshot, settings);
    word::Item& it = second.Add("求め", {{"5할 수 요구&AV@", "M421"}, {"구&AV@", "M421"},
                                       {"5할 수 구&AV@", "M421"}, {"5할 수 구&AV@", "M421"}});
    word::Select(second.flusher);
    HD_CHECK_EQ(it.selected, 2);
    CheckChoice(it.choice, word::Provenance::Scorer, 0);
    HD_CHECK_EQ(senses.choices.at("求め").target, std::string("구&AV@"));
}

HD_TEST(word_selected_assignment_rule_provenance) {
    const struct { bool global; bool neighbour; bool succeeds; int selected; } cases[] = {
        {false, false, true, 0}, {false, false, true, 1}, {true, false, true, 1},
        {false, true, true, 1}, {true, true, false, 1}, {false, false, false, 1},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto& c : cases) {
        SelectLine line(*snapshot);
        auto& anchor = line.Add("東京", {{"동경", "N130"}, {"도쿄", "N110"}}, {word::Provenance::FirstXxx, 17}, c.selected);
        auto& neighbour = line.Add("大阪", {{"대판", "N130"}, {"오사카", "N110"}}, {word::Provenance::FirstXxx, 17}, c.selected);
        auto& affected = c.neighbour ? neighbour : anchor;
        anchor.compDI = c.selected;
        for (auto* entry : affected.group->entries) {
            entry->attr = "QPN";
            entry->gattr = "J7";
        }
        const std::string body = "||1 &&2 D+" + std::string(c.neighbour ? "1" : "0") +
                                 "= \"수도\" S+0== \"" + (c.succeeds ? "東京" : "missing") + "\"\n";
        const std::string flat = c.global ? "rules 1 0 1\nG 50 1 1015 1 " + body
                                          : "rules 1 1 0\nL 0 " + body;
        rules::Program program;
        rules::LoadError error{};
        HD_REQUIRE(rules::Load(flat, program, error));
        const bool result = c.global ? rules::RunGlobal(program, &anchor, true, '1', false)
                                     : rules::RunLocal(program, &anchor, 0);
        HD_CHECK_EQ(result, c.succeeds);
        HD_CHECK_EQ(affected.selected, c.selected);
        const auto layer = c.global ? word::Provenance::GlobalRule : word::Provenance::LocalRule;
        const int id = c.global ? 50 : 0;
        // A rule that fails writes nothing: the assignment before its false condition
        // is taken back, so the word reads as it did before the rule ran.
        const bool wrote = c.succeeds;
        CheckChoice(affected.choice, wrote ? layer : word::Provenance::FirstXxx, wrote ? id : 17);
        // The selected entry was given targets, not a target choice.
        CheckChoice(affected.group->At(c.selected)->targetChoice, wrote ? layer : word::Provenance::None,
                    wrote ? id : -1);
        CheckChoice(affected.group->At(1 - c.selected)->targetChoice, word::Provenance::None);
        CheckChoice(line.worker.ruleChoice, word::Provenance::None);
        const char* untouched = c.neighbour ? (c.selected == 0 ? "오사카" : "대판")
                                           : (c.selected == 0 ? "도쿄" : "동경");
        const char* chosen = c.neighbour ? (c.selected == 0 ? "대판" : "오사카")
                                        : (c.selected == 0 ? "동경" : "도쿄");
        HD_CHECK_EQ(std::string(affected.EntryText(c.selected)), std::string(wrote ? "수도" : chosen));
        HD_CHECK_EQ(std::string(affected.EntryText(1 - c.selected)), std::string(untouched));
        HD_CHECK_EQ(std::string(affected.Code(0)), std::string("130"));
        HD_CHECK_EQ(std::string(affected.Code(1)), std::string("110"));
        for (int i : {0, 1}) {
            HD_CHECK_EQ(std::string(affected.Attr(i)), std::string("QPN"));
            HD_CHECK_EQ(std::string(affected.GAttr(i)), std::string("J7"));
        }
    }
}

HD_TEST(word_punctuation_digit_matches) {
    const struct { const char* attrs; const char* tail; int matches; } cases[] = {
        {"J0", "あ", 1}, {"J1", ",", 1}, {"J2", ".", 1}, {"J3", "?", 1},
        {"J4", "!", 1}, {"J5", ")", 1}, {"J6", "]", 1}, {"J7", ":", 1}, {"J7", "<", 1},
        {"J7", "。", -1}, {"J8", ":", -1}, {"NJ7Wa", ":", 1}, {"J7J1", ":", 1},
        {"J8J7", ":", -1}, {"JsJ7", ":", -1}, {"J", ":", -1}, {"", ":", -1},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto& c : cases) {
        SelectLine line(*snapshot);
        auto& item = line.Add("東京", {{"도쿄"}});
        const std::string text = std::string("東京") + c.tail;
        item.text = text;
        item.group->At(0)->gattr = c.attrs;
        HD_CHECK_EQ(item.CheckJ(0), c.matches);
        HD_CHECK_EQ(item.CheckJ(-1), -1);
    }
}

HD_TEST(document_senses_particle_and_prefix_stems) {
    const struct { const char* target; const char* stem; } cases[] = {
        {"학교에서는", "학교"}, {"친구에게서", "친구"}, {"집으로는", "집"},
        {"학교에는", "학교"}, {"친구에게", "친구"}, {"학교에서", "학교"}, {"집으로", "집"},
        {"집은", "집"}, {"나무는", "나무"}, {"집을", "집"}, {"나무를", "나무"},
        {"서울의", "서울"}, {"학교에", "학교"}, {"의", "의"}, {"서울의는", "서울의"},
        {"할 수 구&AV@", "구"}, {"(이)라고 말&AV@", "말"}, {"(을)를 향&AV@", "향"},
        {"(으)로 향&AV@", "향"}, {"해 주&AE@", "주"}, {"해결&AV@", "해결"},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(std::string(word::DocumentSenseStem(c.target)), std::string(c.stem));
    }
    for (char code : std::string_view("0123456789ABC")) {
        const std::string target = std::string(1, code) + "서울의&AV@5다";
        HD_CHECK_EQ(std::string(word::DocumentSenseStem(target)), std::string("서울"));
    }
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::SessionSenses senses;
    senses.choices.emplace("東京", word::SessionSense{"서울", "N130"});
    word::Settings settings;
    settings.sessionSenses = &senses;
    SelectLine line(*snapshot, settings);
    auto& item = line.Add("東京", {{"동경의", "N130"}, {"서울의", "N130"}});
    const word::SelectContext context{*snapshot, line.worker.chain};
    HD_CHECK_EQ(word::DocumentSenseScore(item, 1, context), 1);
    HD_CHECK_EQ(word::DocumentSenseScore(item, 0, context), 0);
}

HD_TEST(word_select_collocation_recipient) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    const struct {
        std::string_view semantic;
        std::string_view grammar;
        std::string_view boundary;
        bool interveningVerb;
        bool expected;
    } cases[] = {
        {"VHn", "Rc", "", false, true},
        {"XXXQPN", "Rc", "", false, true},
        {"VPl", "Rc", "", false, false},
        {"XVHn", "Rc", "", false, false},
        {"VHn", "XRc", "", false, false},
        {"VHn", "", "", false, false},
        {"VHn", "Rc", "、", false, false},
        {"VHn", "Rc", "", true, false},
    };
    for (const auto& c : cases) {
        SelectLine line(*snapshot);
        line.Add("相手", {{"상대", "N110", c.semantic}});
        auto& particle = line.Add("に", {{"에", "P201"}, {"에게", "P201"}});
        if (!c.boundary.empty()) line.Add(c.boundary, {{",", "E600"}});
        if (c.interveningVerb) line.Add("行", {{"가&AH@", "M429"}});
        line.Add("贈り物", {{"선물"}});
        line.Add("を", {{"(을)를", "P201"}});
        line.Add("教え", {{"가르&AO@", "M437", "", c.grammar}});
        word::ChooseSenses(line.flusher);
        HD_CHECK_EQ(std::string(particle.EntryText(particle.selected)), std::string(c.expected ? "에게" : "에"));
    }
    // Only the selected noun sense supplies semantic evidence.
    SelectLine line(*snapshot);
    line.Add("相手", {{"장소", "N110", "VPl"}, {"상대", "N110", "VHn"}});
    auto& particle = line.Add("に", {{"에", "P201"}, {"에게", "P201"}});
    line.Add("教え", {{"가르&AO@", "M437", "", "Rc"}});
    word::ChooseSenses(line.flusher);
    HD_CHECK_EQ(particle.selected, 0);
}

HD_TEST(word_select_collocation_object_case) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const auto particle : {"を", "が", "に"}) {
        SelectLine line(*snapshot);
        line.Add("経緯", {{"경위", "N110", "", "Oq"}});
        line.Add(particle, {{"조사", "P201"}});
        auto& verb = line.Add("聞", {{"&JB@", "M431"}});
        word::ChooseSenses(line.flusher);
        HD_CHECK_EQ(word::ItemLemma(line.flusher, &verb, verb.selected), std::string(particle == std::string_view("を") ? "묻다" : "듣다"));
    }
    SelectLine line(*snapshot);
    line.Add("響き", {{"울림", "N110", "XXXUHr"}});
    line.Add("を", {{"(을)를", "P201"}});
    auto& verb = line.Add("聞", {{"&J7@", "M431"}});
    word::ChooseSenses(line.flusher);
    HD_CHECK_EQ(word::ItemLemma(line.flusher, &verb, verb.selected), std::string("듣다"));
}

HD_TEST(word_select_collocation_passive_and_state) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    {
        SelectLine line(*snapshot);
        line.Add("紋章", {{"문장", "N110", "", "Os"}});
        line.Add("が", {{"(이)가", "P201"}});
        auto& verb = line.Add("押され", {{"밀&AA@", "M427"}});
        word::ChooseSenses(line.flusher);
        HD_CHECK_EQ(word::ItemLemma(line.flusher, &verb, verb.selected), std::string("찍히다"));
    }
    {
        SelectLine line(*snapshot);
        line.Add("正気", {{"제정신", "N110", "VHnUYi", "Or"}});
        auto& particle = line.Add("に", {{"에게", "P201"}, {"(으)로", "P201"}});
        line.Add("戻", {{"돌아&AH@", "M418", "", "Rr"}});
        word::ChooseSenses(line.flusher);
        HD_CHECK_EQ(std::string(particle.EntryText(particle.selected)), std::string("(으)로"));
    }
}
