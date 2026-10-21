#include "core/pipeline/speech.h"

#include <cstdint>
#include <string>
#include <vector>

#include "hdtest.h"

using namespace hdTrans::pipeline;

HD_TEST(pipeline_speaker_line) {
    const struct {
        const char* line;
        bool want;
    } cases[] = {
        {"母: もう寝なさい", true},
        {"ユキ二号: 行くよ！", true},
        {"母:もう寝なさい", false},
        {"設定：音量を上げる", false},
        {"残り時間: 10分", false},
        {"時刻: 9:30", false},
        {"とても長い名前の人物です: こんにちは", false},
        {"彼は言った: 「行く」", false},
        {"https://example.com/", false},
    };
    for (const auto& c : cases) HD_CHECK_EQ(SpeakerLine(c.line), c.want);
}

HD_TEST(pipeline_marked_speech) {
    const struct {
        const char* line;
        bool want;
    } cases[] = {
        {"明日は晴れるよ。", true},
        {"そうだろ？", true},
        {"もう行っちゃった。", true},
        {"何だっけ", true},
        {"行くぞー！", true},
        {"彼は先生になった。", false},
        {"彼女が服を捨てる。", false},
        {"雨が降っていた。窓を閉めた。", false},
        {"　窓を閉めた。寒いからさ。", false},
        {"「行くよ」と彼は言った。", false},
        {"『明日』は来るよ。", false},
        {"", false},
    };
    for (const auto& c : cases) HD_CHECK_EQ(MarkedSpeech(c.line), c.want);
}

HD_TEST(pipeline_quote_styles) {
    const struct {
        const char* line;
        std::vector<std::uint8_t> want;
    } cases[] = {
        {"「行くよ」と彼は言った。", {0, 0}},
        {"「ありがとうございます」", {0, kPoliteSpeech}},
        {"「わしはここで待っておる」", {0, kOldSpeech}},
        {"『雨が降る』", {0, kWritten}},
        {"想「おも」い出", {0, kReading | kCited}},
        {"「すぐ消える」魔法を使った。", {0, kCited}},
        {"彼は「雨が降る」ということを知った。", {0, kCited}},
        {"彼のモットーは「従う」だ。", {0, kCited}},
        {"ハル「眠い」「もう寝る」", {0, 0, 0}},
        {"「行こう」「帰ろう」という声", {0, kCited, kCited}},
        {"彼は「行く」と言った。「そうか」", {0, kCited, 0}},
        {"そうです。「違う」", {kPoliteSpeech, 0}},
    };
    auto show = [](const std::vector<std::uint8_t>& v) {
        std::string s;
        for (const std::uint8_t b : v) s += std::to_string(b) + " ";
        return s;
    };
    for (const auto& c : cases) HD_CHECK_EQ(show(QuoteStyles(c.line)), show(c.want));
    // A line inside a quote an earlier line opened: that quote is the first one.
    HD_CHECK_EQ(show(QuoteStyles("明日も行く」と言った。「次", 1)), show({0, 0, 0}));
    HD_CHECK_EQ(show(QuoteStyles("ございます」", 1)), show({0, kPoliteSpeech}));
    HD_CHECK_EQ(show(QuoteStyles("すぐ消える」魔法", 1)), show({0, kCited}));
}
