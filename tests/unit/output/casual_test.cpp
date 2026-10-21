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
