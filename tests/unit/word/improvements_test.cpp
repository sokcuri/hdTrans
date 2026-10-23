#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/engine/driver.h"
#include "core/word/analyze.h"
#include "core/word/flush.h"
#include "hdtest.h"

HD_TEST(word_improvements_defaults_and_reference) {
    using hdTrans::word::Improvements;
    using hdTrans::word::Settings;
    constexpr std::string_view defaultOn[] = {
        "extraBuiltin", "literalCleanup", "imperativeEu", "causativePassive", "exclamationNa", "dialectForms",
        "expandedGroups", "semanticAlternatives", "nonterminalBoundary", "cgRules", "kanjiNumbers",
    };
    const Improvements defaults;
    const Improvements reference = Improvements::Reference();
    const Settings defaultSettings;
    const Settings referenceSettings = Settings::Reference();
#define HDTRANS_CHECK_IMPROVEMENT(name, defaultValue) { \
    const bool enabled = std::find(std::begin(defaultOn), std::end(defaultOn), std::string_view(#name)) != \
                         std::end(defaultOn); \
    HD_CHECK_EQ(defaults.name, enabled); \
    HD_CHECK_EQ(defaultSettings.improvements.name, enabled); \
    HD_CHECK(!reference.name); \
    HD_CHECK(!referenceSettings.improvements.name); \
}
    HDTRANS_IMPROVEMENTS(HDTRANS_CHECK_IMPROVEMENT)
#undef HDTRANS_CHECK_IMPROVEMENT
    HD_CHECK(referenceSettings.sessionSenses == nullptr);
}

namespace {

void CheckImprovement(bool hdTrans::word::Improvements::* flag, const std::vector<hdTrans::dict::File>& files,
                      const std::string& input, std::string_view disabled, std::string_view enabled) {
    HD_REQUIRE(disabled != enabled);
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    for (const bool active : {false, true}) {
        hdTrans::internal::TranslationOptions options;
        options.wordImprovements = hdTrans::word::Improvements::Reference();
        options.wordImprovements.*flag = active;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, options);
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(active ? enabled : disabled));
    }
}

}  // namespace

HD_TEST(word_improvements_per_level_retry_depth) {
    CheckImprovement(&hdTrans::word::Improvements::perLevelRetries,
        {{0, "base.tsv",
          "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
          "え\tD\tN110\t\t\tGN\t0\t1\n"}},
        "ええええぬ。", "D.", "에에에에누.");
}

HD_TEST(word_improvements_desire_window_output) {
    CheckImprovement(&hdTrans::word::Improvements::desireWindow,
        {{0, "plain.tsv", "寝る\t자다\tv1\n"},
         {0, "base.tsv",
          "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
          "V\tB\tS580\t\t\tGN\t0\t1\n"
          "V\t2고\tS5:0\t\t\tGN\t0\t2\n"
          "たかった\t싶었다\tN110\t\t\tGN\t0\t3\n"}},
        "寝たかった。", "잠 싶었다.", "자고 싶었다.");
}

HD_TEST(word_improvements_causative_passive_output) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "plain.tsv", "書く\t쓰다\tv\n"},
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
         "V\t5다\tS560\t\t\tGN\t0\t1\n"},
    };
    CheckImprovement(&hdTrans::word::Improvements::causativePassive, files,
                     "書かされた。", "서.", "쓰게 되었다.");
    CheckImprovement(&hdTrans::word::Improvements::causativePassive, files,
                     "書かせられた。", "서.", "쓰게 되었다.");
}

HD_TEST(word_improvements_kanji_numbers_output) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
         "五十\t오십\tN110\t\t\tGN\t0\t1\n"
         "日\t일\tN110\t\tCt\tGN\t0\t2\n"
         "百\t백\tN110\t\tNj\tGN\t0\t3\n"},
    };
    CheckImprovement(&hdTrans::word::Improvements::kanjiNumbers, files, "五十日", "오십일", "50일");
    CheckImprovement(&hdTrans::word::Improvements::kanjiNumbers, files, "百二十日", "백이10일", "120일");
}

HD_TEST(word_improvements_kanji_numbers_myriads_and_words) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
         "日\t일\tN110\t\tCt\tGN\t0\t1\n"
         "万\t만\tN110\t\tNj\tGN\t0\t2\n"
         "十五日\t십오일\tN110\t\t\tGN\t0\t3\n"
         "四十九日\t사십구일재\tN110\t\t\tGN\t0\t4\n"
         "二十日\t스무날\tN110\t\t\tGN\t0\t5\n"
         "番\t번\tN110\t\tCt\tGN\t0\t6\n"
         "十八番\t십팔번\tN110\t\t\tGN\t0\t7\n"
         "十八番\t장기\tN110\t\t\tGN\t0\t8\n"
         "艦\t함\tN110\t\t\tGN\t0\t9\n"
         "分\t분\tN110\t\tCt\tGN\t0\t10\n"
         "十二分\t십이분\tN110\t\t\tGN\t0\t11\n"
         "十二分\t충분%A1@\tG8U0\t\t\tGN\t0\t12\n"
         "で\t에\tP211\t\t\tGN\t0\t13\n"
         "に\t에\tP211\t\t\tGN\t0\t14\n"},
    };
    // A number that goes on after 万 is one number, written in groups of four digits.
    CheckImprovement(&hdTrans::word::Improvements::kanjiNumbers, files, "一万二千三百日", "1만2천3백일", "1만 2300일");
    // A word that spells the number and its counter reads digits; one read otherwise keeps its reading.
    CheckImprovement(&hdTrans::word::Improvements::kanjiNumbers, files, "十五日", "십오일", "15일");
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    // A word with a sense of its own beside the spelled one keeps its reading where it stands
    // alone, and reads the number inside a compound.
    for (const auto& [input, expected] : {std::pair<std::string, std::string_view>{"四十九日", "사십구일재"},
                                          std::pair<std::string, std::string_view>{"二十日", "스무날"},
                                          std::pair<std::string, std::string_view>{"十八番", "십팔번"},
                                          std::pair<std::string, std::string_view>{"十八番艦", "18번함"},
                                          // An adjectival noun beside the spelled word keeps the
                                          // word before に, and reads the count before other words.
                                          std::pair<std::string, std::string_view>{"十二分に", "십이분에"},
                                          std::pair<std::string, std::string_view>{"十二分で", "12분에"}}) {
        hdTrans::internal::TranslationOptions options;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, options);
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(expected));
    }
}

// A count word whose counter starts a verbal noun that goes on past it (1回 of 1回復) gives
// way to the number and the noun; where the noun goes on with a kanji the count stays
// (1回復活 한 번 부활).
HD_TEST(word_count_word_before_verbal_noun) {
    const auto loaded = hdTrans::dict::Load({{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "1回\t1회\tN110\t\tCtNjNd\tGN\t0\t1\n"
        "回\t회\tN110\t\tCt\tGN\t0\t2\n"
        "回復\t회복\tN110\t\tHd\tGN\t0\t3\n"
        "復\t갚&AA@\tM439\t\t\tGN\t0\t4\n"
        "復活\t부활\tN110\t\tHd\tGN\t0\t5\n"
        "。\t.\tE000\t\t\tGN\t0\t6\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    for (const auto& [input, expected] : {std::pair<std::string, std::string_view>{"1回復した。", "1 회복."},
                                          std::pair<std::string, std::string_view>{"1回復活。", "한 번 부활."}}) {
        hdTrans::internal::TranslationOptions options;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, options);
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(expected));
    }
}

HD_TEST(word_improvements_forward_terminal_order) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "高\t$D3@\tH7N0\t\t\tGN\t0\t1\n"
        "い\t4\tT500\t\t\tGN\t0\t2\n"
        "い\t1다\tT500\t\t\tGN\t0\t3\n"
        "、\t,\tE000\t\t\tGN\t0\t4\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    for (const bool forward : {false, true}) {
        tokenizer::Tokens tokens;
        word::Arena arena;
        auto settings = word::Settings::Reference();
        settings.improvements.forwardTerminal = forward;
        word::Worker worker(tokens, *loaded.snapshot, {}, settings, arena, {});
        std::vector<word::Item*> predicates;
        // The first clause depends on the ending already selected for the second.
        for (int i = 0; i < 5; i++) {
            const bool predicate = i % 2 == 0;
            const std::string_view text = predicate ? "高い" : "、";
            const auto found = worker.DoLookup(text, word::kModeGeneral);
            HD_REQUIRE(found.records != nullptr);
            auto* group = worker.NewGroup(found.records, ' ');
            auto* detail = worker.NewDetail(predicate ? word::kDetailKeiyo : word::kDetailInfo, -1);
            if (predicate) {
                const auto endings = worker.DoLookup("い", word::kModeAdjectiveEnding);
                HD_REQUIRE(endings.records != nullptr);
                detail->kind = '0';
                detail->group = worker.NewGroup(endings.records, ' ');
                HD_REQUIRE(detail->group->entries.size() == 2);
                detail->ResetDirect(2);
                detail->selected = 0;
            }
            auto* item = worker.NewItem(detail, group, text, 0, -1);
            item->selected = 0;
            item->prev = worker.chain.tail;
            if (worker.chain.tail != nullptr) worker.chain.tail->next = item;
            else worker.chain.head = item;
            worker.chain.tail = item;
            if (predicate) predicates.push_back(item);
        }
        rules::Program program;
        word::Flusher flusher{worker, program, loaded.snapshot->OutputFamilies()};
        word::PreferTerminal(flusher);
        std::string endings;
        for (const auto* item : predicates) {
            if (!endings.empty()) endings += '|';
            endings += item->DirectText(item->DirectIndex());
        }
        HD_CHECK_EQ(endings, std::string(forward ? "4|1다|1다" : "1다|1다|1다"));
    }
}
