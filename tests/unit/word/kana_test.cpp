#include <string>
#include <string_view>

#include "core/word/kana.h"
#include "hdtest.h"

using namespace hdTrans::word;

namespace {

// TransliterateKatakana as text: "fault" when a context rule fails.
std::string Katakana(std::string_view in) {
    std::string out;
    return TransliterateKatakana(in, out) ? out : std::string("fault");
}

}  // namespace

HD_TEST(kana_katakana_cases) {
    HD_CHECK_EQ(Katakana("コンピューター"), std::string("콘퓨타"));
    HD_CHECK_EQ(Katakana("メント"), std::string("먼트"));
    HD_CHECK_EQ(Katakana("ベッド"), std::string("벳드"));
    HD_CHECK_EQ(Katakana("ー"), std::string(""));
    HD_CHECK_EQ(Katakana("aアb"), std::string("a아b"));
    HD_CHECK_EQ(Katakana("イージーなユー"), std::string("이지な유"));
    HD_CHECK_EQ(Katakana("ヷルキューレ"), std::string("바르큐레"));
    HD_CHECK_EQ(Katakana("ヸヹヺ"), std::string("비베보"));
    HD_CHECK_EQ(Katakana("ティーン"), std::string("틴"));
    HD_CHECK_EQ(Katakana("ヴァン"), std::string("반"));
    HD_CHECK_EQ(Katakana("チェーンソー"), std::string("첸소"));
    HD_CHECK_EQ(Katakana("チャンス"), std::string("찬스"));
    HD_CHECK_EQ(Katakana("ルーク"), std::string("루크"));
    HD_CHECK_EQ(Katakana("ルール"), std::string("룰"));
    HD_CHECK_EQ(Katakana("ルーン"), std::string("룬"));
    HD_CHECK_EQ(Katakana("ビッグ"), std::string("빅"));
}

HD_TEST(kana_hiragana_cases) {
    HD_CHECK_EQ(TransliterateHiragana("さくら"), std::string("사쿠라"));
    HD_CHECK_EQ(TransliterateHiragana("かんぱい"), std::string("칸파이"));
    HD_CHECK_EQ(TransliterateHiragana("１ａ"), std::string("1a"));
    HD_CHECK_EQ(TransliterateHiragana("ゔぁいおりん"), std::string("바이오린"));
    HD_CHECK_EQ(TransliterateHiragana("ゔゕゖ"), std::string("부카케"));
}
