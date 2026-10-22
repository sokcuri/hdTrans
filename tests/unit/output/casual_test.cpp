#include <cstdint>
#include <string>
#include <vector>

#include "core/output/compose.h"
#include "hdtest.h"

using namespace hdTrans::output;

HD_TEST(output_spoken_infinitive) {
    const struct {
        const char* form;
        const char* want;
    } cases[] = {
        {"주어", "줘"},       {"두어", "둬"},     {"되어", "돼"},     {"되었", "됐"},     {"주었", "줬"},
        {"보아", "봐"},       {"보았", "봤"},     {"꼬아", "꽈"},     {"쏘았", "쐈"},     {"바꾸어", "바꿔"},
        {"피어", "피어"},     {"아니어", "아니어"}, {"모았", "모았"}, {"놓아", "놓아"},   {"먹어", "먹어"},
        {"해", "해"},         {"어", "어"},       {"", ""},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::SpokenInfinitive(c.form), std::string(c.want));
}

HD_TEST(output_quotes_after) {
    const auto at = [](QuoteState from, const char* text) {
        const QuoteState q = QuotesAfter(from, text);
        return std::to_string(q.depth) + "/" + std::to_string(q.count);
    };
    HD_CHECK_EQ(at({}, "地の文"), "0/0");
    HD_CHECK_EQ(at({}, "「行く"), "1/1");
    HD_CHECK_EQ(at({}, "「行く」と言った"), "0/1");
    HD_CHECK_EQ(at({}, "「『雨』だ」「晴れ」"), "0/2");
    HD_CHECK_EQ(at({}, "」閉じるだけ"), "0/0");
    HD_CHECK_EQ(at({1, 1}, "続き」「次"), "1/2");
    HD_CHECK_EQ(at({2, 1}, "』」"), "0/1");
}

HD_TEST(output_polite_quotes) {
    const auto show = [](const std::vector<bool>& v) {
        std::string s;
        for (const bool b : v) s += b ? '1' : '0';
        return s;
    };
    HD_CHECK_EQ(show(PoliteQuotes("「고마워요. 살았어」", 0)), "01");
    HD_CHECK_EQ(show(PoliteQuotes("「달라. 그렇지 않아」「좋네요~!」", 0)), "001");
    HD_CHECK_EQ(show(PoliteQuotes("「필요. 중요!」", 0)), "00");
    HD_CHECK_EQ(show(PoliteQuotes("「『알겠습니다』라고 했어」", 0)), "00");
    HD_CHECK_EQ(show(PoliteQuotes("그는 웃었어요. 「몰라」", 0)), "10");
    HD_CHECK_EQ(show(PoliteQuotes("갑니다」라고 했다", 1)), "01");
}

HD_TEST(output_spoken_words) {
    const std::vector<std::uint8_t> casual = {0, kCasualSpeech};
    const struct {
        const char* line;
        const char* want;
    } cases[] = {
        {"「그것은 달라. 하는 것을 봤어」", "「그건 달라. 하는 걸 봤어」"},
        {"「그런 것이 아니야. 이것이야」", "「그런 게 아니야. 이거야」"},
        {"「무엇인가 먹고 있네. 무엇을 해?」", "「뭔가 먹고 있네. 뭘 해?」"},
        {"「무엇인가가 문제야. 무엇인가를 몰라」", "「무엇인가가 문제야. 무엇인가를 몰라」"},
        {"「어느것은 사물이야. 『그것은』」", "「어느것은 사물이야. 『그것은』」"},
        {"그것은 「그것은」", "그것은 「그건」"},
        {"「갈 것 같아. 그런 것 싫어」", "「갈 것 같아. 그런 거 싫어」"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(SpokenWords(c.line, 0, casual), std::string(c.want));
    HD_CHECK_EQ(SpokenWords("「그것은 하는 것은」", 0, {0, 0}), std::string("「그건 하는 건」"));
    HD_CHECK_EQ(SpokenWords("「이것으로 됩니다」", 0, {0, kPoliteStyle}), std::string("「이걸로 됩니다」"));
    // 4: a quote in the written style.
    HD_CHECK_EQ(SpokenWords("「그것은」", 0, {0, 4}), std::string("「그것은」"));
    HD_CHECK_EQ(SpokenWords("그것은 몰라」", 1, casual), std::string("그건 몰라」"));
}
