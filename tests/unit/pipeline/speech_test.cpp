#include "core/pipeline/speech.h"

#include <cstdint>
#include <string>
#include <vector>

#include "hdtest.h"

using namespace hdTrans::pipeline;

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

HD_TEST(pipeline_casual_quotes) {
    const struct {
        const char* line;
        const char* want;  // one digit per quote, 0 for the text outside quotes
    } cases[] = {
        {"「違う。そうじゃないよ」", "01"},
        {"「行くね」「寒いわ」「嘘じゃん」", "0111"},
        {"「そうさ」「行かないもん」「明日来るって」「晴れるかな」「どうかしら」", "011111"},
        {"「食べちゃった」「見てる」「言っとく」「知らねえ」", "01111"},
        {"「違う」「明日行く」「そうだ」「行くぞ」", "00000"},
        // Old commands, calls and stiff speech.
        {"「やめよ」「部下たちよ」「分かるかね」「分かっているのだよ」「我々は勝つよ」", "000000"},
        {"「思うが……」「であるよ」", "000"},
        // Not contractions: ちょっとくらい, おねえさん, 待って.
        {"「ちょっとくらい」「おねえさんだ」「待って」", "0000"},
        // A quote inside a quote is not its own text.
        {"「『行くよ』と書いてある」", "00"},
        {"地の文だよ。「行く」", "00"},
    };
    for (const auto& c : cases) {
        std::string got;
        for (const bool b : CasualQuotes(c.line)) got += b ? '1' : '0';
        HD_CHECK_EQ(got, std::string(c.want));
    }
    HD_CHECK_EQ(std::string(CasualQuotes("寒いね」", 1)[1] ? "1" : "0"), "1");
}
