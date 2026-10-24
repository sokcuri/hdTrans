#include <algorithm>
#include <chrono>
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
    HD_CHECK_EQ(Katakana("アムール"), std::string("아무르"));
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
    // After ム, ヌ or ス (no English long u with l follows m, n or s) the long u keeps 르;
    // エール after a or i is the vowel e before 르, after u, o, ー or at the start the ei of
    // English words.
    HD_CHECK_EQ(Katakana("マンスール"), std::string("만수르"));
    HD_CHECK_EQ(Katakana("バイコヌール"), std::string("바이코누르"));
    HD_CHECK_EQ(Katakana("コルミエール"), std::string("코르미에르"));
    HD_CHECK_EQ(Katakana("ベルディエール"), std::string("베르디에르"));
    HD_CHECK_EQ(Katakana("イエール"), std::string("이에르"));
    HD_CHECK_EQ(Katakana("エール"), std::string("에일"));
    HD_CHECK_EQ(Katakana("ジンジャーエール"), std::string("진자에일"));
    HD_CHECK_EQ(Katakana("ホエール"), std::string("호에일"));
    HD_CHECK_EQ(Katakana("スクール"), std::string("스쿨"));
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

// An edit moves only the cells between it and the one before it, so a run sixteen times as long
// takes about sixteen times as long to transliterate, whether edits keep the number of cells (メー)
// or drop marks (ラーメン, ビッグ), and many short runs do too.
HD_TEST(kana_katakana_run_time_grows_with_its_length) {
    auto fastest = [](std::string_view unit, int n, std::string& out) {
        std::string in;
        for (int i = 0; i < n; i++) in += unit;
        double best = 0;
        for (int i = 0; i < 5; i++) {
            const auto start = std::chrono::steady_clock::now();
            if (!TransliterateKatakana(in, out)) out = "fault";
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            best = i == 0 ? seconds : std::min(best, seconds);
        }
        return best;
    };
    struct Shape {
        std::string_view unit, read;
    };
    for (const Shape& shape : {Shape{"メー", "메이"}, Shape{"ラーメン", "라멘"}, Shape{"ビッグ", "빅"}, Shape{"aア", "a아"}}) {
        std::string shortOut, longOut;
        const double shortRun = fastest(shape.unit, 2000, shortOut);
        const double longRun = fastest(shape.unit, 32000, longOut);
        std::string expected;
        for (int i = 0; i < 32000; i++) expected += shape.read;
        HD_CHECK_EQ(longOut, expected);
        HD_CHECK(longRun < 64 * shortRun);
    }
}
