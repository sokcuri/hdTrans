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
    HD_CHECK_EQ(Katakana("スムーズ"), std::string("스무즈"));
    HD_CHECK_EQ(Katakana("チーム"), std::string("팀"));
    // ム closes the syllable before it unless ー or a small vowel follows.
    HD_CHECK_EQ(Katakana("リアムーゼ"), std::string("리아무제"));
    HD_CHECK_EQ(Katakana("アムーレ"), std::string("아무레"));
    HD_CHECK_EQ(Katakana("ゲーム"), std::string("게임"));
    HD_CHECK_EQ(Katakana("ハムスター"), std::string("함스타"));
    // A syllable read with ャ, ュ or ョ takes the final of ン, ッ or ム after it; ジャ, ジュ,
    // ジョ, チュ and チョ are written without the glide.
    HD_CHECK_EQ(Katakana("ニャンポコ"), std::string("냔포코"));
    HD_CHECK_EQ(Katakana("シャンティ"), std::string("샨티"));
    HD_CHECK_EQ(Katakana("ジュース"), std::string("주스"));
    HD_CHECK_EQ(Katakana("チョコ"), std::string("초코"));
    // A long u, e or o before the final ル takes it as ㄹ; a long a or i does not.
    HD_CHECK_EQ(Katakana("ツール"), std::string("툴"));
    HD_CHECK_EQ(Katakana("ゴール"), std::string("골"));
    HD_CHECK_EQ(Katakana("メール"), std::string("메일"));
    HD_CHECK_EQ(Katakana("ダミール"), std::string("다미르"));
    // A u written with ㅡ keeps it before the final ㄹ; グ after ン stays before リ or a short ル.
    HD_CHECK_EQ(Katakana("テーブル"), std::string("테이블"));
    HD_CHECK_EQ(Katakana("シングル"), std::string("싱글"));
    HD_CHECK_EQ(Katakana("シャングリラ"), std::string("샹그리라"));
    HD_CHECK_EQ(Katakana("ロングソード"), std::string("롱소드"));
}

HD_TEST(kana_hiragana_cases) {
    HD_CHECK_EQ(TransliterateHiragana("さくら"), std::string("사쿠라"));
    HD_CHECK_EQ(TransliterateHiragana("かんぱい"), std::string("칸파이"));
    HD_CHECK_EQ(TransliterateHiragana("１ａ"), std::string("1a"));
    HD_CHECK_EQ(TransliterateHiragana("ゔぁいおりん"), std::string("바이오린"));
    HD_CHECK_EQ(TransliterateHiragana("ゔゕゖ"), std::string("부카케"));
}
