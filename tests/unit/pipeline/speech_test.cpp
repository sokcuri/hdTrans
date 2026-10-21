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
}
