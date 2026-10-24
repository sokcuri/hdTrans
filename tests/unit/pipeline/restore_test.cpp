#include "core/pipeline/restore.h"

#include <string>
#include <vector>

#include "core/text/utf8.h"
#include "hdtest.h"

using namespace hdTrans;
using output::Pieces;
using output::SpanOpen;
using output::SpanText;
using output::SpanWord;
using pipeline::kDropShift;

namespace {

std::string Show(const Pieces& p) {
    std::string s = "{";
    for (const auto& x : p) s += "{\"" + x.text + "\", " + std::to_string(static_cast<int>(x.span)) + "}";
    return s + "}";
}

// The kana of s shifted as the composer writes the kana it drops.
std::string Dropped(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        text::AppendRune(out, r.cp + kDropShift);
        i += r.size;
    }
    return out;
}

struct PiecesCase {
    Pieces typed, out, want;
};

}  // namespace

HD_TEST(pipeline_drop_added_spaces) {
    const std::vector<PiecesCase> cases = {
        {{{"「世話になった。また来る」"}}, {{"「 신세를 졌다. 또 온다」"}}, {{"「신세를 졌다. 또 온다」"}}},
        {{{"故に、エルフは狩りをしない。"}}, {{" 고로, 엘프는 사냥을 하지 않는다."}}, {{"고로, 엘프는 사냥을 하지 않는다."}}},
        // Spaces typed at the start or after an opening bracket stay.
        {{{" 故に"}}, {{" 고로"}}, {{" 고로"}}},
        {{{"「 世話"}}, {{"「 신세"}}, {{"「 신세"}}},
        // Line by line; a different line count leaves the text as it is.
        {{{"「あ」\n「い」"}}, {{"「 아」\n 「 이」"}}, {{"「아」\n「이」"}}},
        {{{"あ\nい"}}, {{" 아 이"}}, {{" 아 이"}}},
        // Spans are left as they are and stop the trim at the start.
        {{{"「"}, {"우와", SpanWord}, {"…柔らかい」"}},
         {{"「 "}, {"우와", SpanWord}, {"…부드럽다」"}},
         {{"「"}, {"우와", SpanWord}, {"…부드럽다」"}}},
        {{{"<b>", SpanOpen}, {"「あ」"}}, {{"<b>", SpanOpen}, {" 「 아」"}}, {{"<b>", SpanOpen}, {" 「아」"}}},
        {{{"딸랑", SpanWord}}, {{"( 딸랑", SpanWord}, {" ( 소리"}}, {{"( 딸랑", SpanWord}, {" (소리"}}},
        // Spaces before a closing bracket go unless the line typed white space before one.
        {{{"『ございません。』"}}, {{"「없습니다. 」"}}, {{"「없습니다.」"}}},
        {{{"（呪いはねえ）"}}, {{"(저주는 말이야 )"}}, {{"(저주는 말이야)"}}},
        {{{"《お待たせしました！》"}}, {{"《기다렸습니다! 》"}}, {{"《기다렸습니다!》"}}},
        {{{"＜準備中です。＞"}}, {{"<준비 중입니다. >"}}, {{"<준비 중입니다.>"}}},
        {{{"「 Hello 」鬼"}}, {{"「 Hello 」귀신 )"}}, {{"「 Hello 」귀신 )"}}},
        // A ” opens the quote it does not close, and a > closes only after a <.
        {{{"、”プロの人”と"}}, {{", ”프로의 사람 ”과"}}, {{", ”프로의 사람”과"}}},
        {{{"します。＞＞"}}, {{"합니다. >>"}}, {{"합니다. >>"}}},
    };
    for (const auto& c : cases) {
        const Pieces got = pipeline::DropAddedSpaces(c.typed, c.out);
        HD_CHECK_EQ(Show(got), Show(c.want));
    }
}

HD_TEST(pipeline_restore_brackets) {
    const std::vector<PiecesCase> cases = {
        {{{"サラが『目的地』に"}}, {{"사라가 「목적지」에"}}, {{"사라가 『목적지』에"}}},
        {{{"（アラーム！？）"}}, {{"(알람!)"}}, {{"（알람!）"}}},
        // A form written more often than typed is left as it is.
        {{{"『あ』"}}, {{"「아」「이」"}}, {{"「아」「이」"}}},
        // The n-th form takes the n-th bracket typed in it.
        {{{"「あ」『い』"}}, {{"「아」「이」"}}, {{"「아」『이』"}}},
        {{{"〝あ〟’"}}, {{"˝아〃＇"}}, {{"“아”’"}}},
        {{{"「あ」"}}, {{"『아』"}}, {{"『아』"}}},
        {{{"『"}, {"딸랑", SpanWord}, {"』"}},
         {{"「"}, {"「딸랑」", SpanWord}, {"」"}},
         {{"『"}, {"「딸랑」", SpanWord}, {"』"}}},
        {{{"『あ』\n「い」"}}, {{"「아」\n「이」"}}, {{"『아』\n「이」"}}},
        {{{"『あ』\n「い」"}}, {{"「아」「이」"}}, {{"『아』「이」"}}},
    };
    for (const auto& c : cases) {
        const Pieces got = pipeline::RestoreBrackets(c.typed, c.out);
        HD_CHECK_EQ(Show(got), Show(c.want));
    }
}

HD_TEST(pipeline_korean_forms) {
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"（알람!）", "(알람!)"},
        {"제일의“문제아”", "제일의 “문제아”"},
        {"「“아”", "「“아”"},
        {"　아", " 아"},
        {"아　“이", "아 “이"},
        {"＜아＞［이］｛우｝", "<아>[이]{우}"},
        {"아““", "아 ““"},
        {"아,“이”“우”", "아, “이” “우”"},
        {"\t“아", "\t“아"},
        {"“아", "“아"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(pipeline::KoreanForms(c.in), std::string(c.want));
}

HD_TEST(pipeline_read_kana) {
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"きょどきょど", "쿄도쿄도"},
        {"ぬちゃっ", "누챳"},
        {"んむっ", "음뭇"},
        {"ぷぷっ", "푸풋"},
        {"なぁ", "나아"},
        {"ふーー", "후우우"},
        {"ぅぅ", "우우"},
        {"えぃ", "에이"},
        {"ふぁっ", "팟"},
        {"ん", "응"},
        {"んっ", "읏"},
        {"んか", "응카"},
        {"んぱ", "음파"},
        {"んた", "은타"},
        {"んんふ", "응응후"},
        {"ぼっち", "봇치"},
        {"ゐゑヰヱ", "이에이에"},
        {"ヌチャッ", "누챳"},
        {"なぁぁぁぁぁぁ", "나아아아아"},
        {"ふーーーーーー", "후우우우우"},
        {"っち", "치"},
        {"ょ", "요"},
        {"ぅゅ", "유"},
        {"ゔ", "부"},
        {"ぁりゔ", "아리부"},
        {"ー", ""},
        {"ッ", ""},
        // Kana the kana reading does not know are read by the katakana transliteration.
        {"ふゎ", "후"},
        {"ふゎぁ", "후"},
        {"うゎぁぁ", "우"},
        {"はわゎゎゎ", "하와"},
        {"ヷ", "바"},
        // Kept when that writes nothing.
        {"ゎ", "ゎ"},
        {"・", "・"},
        {"ゝ", "ゝ"},
        {"ヵヶ", "ヵヶ"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(pipeline::internal::ReadKana(c.in), std::string(c.want));
}

HD_TEST(pipeline_write_dropped) {
    HD_CHECK_EQ(pipeline::WriteDropped("「" + Dropped("ぷぷっ") + "」"), std::string("「푸풋」"));
    HD_CHECK_EQ(pipeline::WriteDropped(Dropped("はむ") + "는 " + Dropped("ふゎ") + "~"), std::string("하무는 후~"));
    HD_CHECK_EQ(pipeline::WriteDropped("그냥"), std::string("그냥"));
}

HD_TEST(pipeline_write_stutters) {
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"「\uE000、알았다…」", "「아, 알았다…」"},
        {"「……\uE000、엘군?」", "「……에, 엘군?」"},
        {"「 \uE000，이봐」", "「이, 이봐」"},
        {"  \uE000…어…", "어…어…"},
        {"그 \uE000‥‥가다", "그 가‥‥가다"},
        {"아 \uE000、 각", "아 가,  각"},
        {"\uE000、 \uE000、아", "아,  아, 아"},
        {"\uE000・・어", "어・・어"},
        // A single ・ is no separator; a mark with no syllable after it goes.
        {"\uE000・어", "・어"},
        {"\uE000、!", "!"},
        {"\uE000、", ""},
        // A dash run is a separator.
        {"「\uE000―――싫어어엇」", "「시―――싫어어엇」"},
        // A longer piece writes its share of the word's syllables with their finals, short of
        // the whole word: 2 kana of 6 is 2 of 4 syllables, 2 of 3 is 1 of 2.
        {"「\uE000、\uE000\uE000\uE004\uE004\uE004\uE004、미안해요」", "「미, 미안, 미안해요」"},
        {"\uE000、\uE000\uE000\uE004、싫어", "시, 싫, 싫어"},
        // Marks without separators write the syllable once each.
        {"「\uE000\uE000\uE000마침내」", "「마마마마침내」"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(pipeline::WriteStutters(c.in), std::string(c.want));
}

HD_TEST(pipeline_write_drawls) {
    struct Case {
        std::string_view in, want;
    };
    const Case cases[] = {
        // In a polite ending: before 습니다 습니까, before 니다 니까 시다 after ㅂ, before 지요 까요
        // 네요 군요, else before the last syllable.
        {"잘 먹겠습니다\uE005…\uE005.", "잘 먹겠…습니다."},
        {"갔습니까\uE005―\uE005?", "갔―습니까?"},
        {"최저……입니다\uE005……\uE005", "최저……입……니다"},
        {"갑시다\uE005…\uE005!", "갑…시다!"},
        {"그렇겠지요\uE005…\uE005", "그렇겠…지요"},
        {"고마워요\uE005…\uE005", "고마워…요"},
        // Before the copula or 하다: 입 합 before 니다, and for でした 이었 였 했 before 습니다.
        {"좋아합니다\uE006……\uE006\uE005……\uE005", "좋아……합……니다"},
        {"어떻습니까\uE006…\uE006?", "어떻…습니까?"},
        {"노력했습니다\uE006…\uE006", "노력했…습니다"},
        {"조용했습니다\uE007…\uE007", "조용…했습니다"},
        {"화객선이었습니다\uE007…\uE007", "화객선…이었습니다"},
        {"조용하네요\uE006…\uE006", "조용하…네요"},
        // ・ in its Korean form; the space written as if a word followed the span goes before
        // punctuation and stays before a word.
        {"받습니다\uE005・・・\uE005", "받···습니다"},
        {"갑니까 \uE005…\uE005?", "갑…니까?"},
        {"갑니까 \uE005…\uE005그는", "갑…니까 그는"},
        // One syllable or no Hangul word before.
        {"네\uE005…\uE005", "네…"},
        {"\uE005…\uE005. 아", ". 아"},
        {"abc\uE005…\uE005", "abc"},
        // A mark without a closing mark or with other text or nothing before it stays.
        {"갑니다\uE005…", "갑니다\uE005…"},
        {"가\uE005나\uE005다", "가\uE005나\uE005다"},
        {"가\uE006\uE006", "가\uE006\uE006"},
        {"가\uE007…\uE005", "가\uE007…\uE005"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(pipeline::WriteDrawls(c.in), std::string(c.want));
}

HD_TEST(pipeline_escape_private_use) {
    struct Case {
        std::string_view in, escaped;
    };
    const Case cases[] = {
        {"ボタン\uE005を押す", "ボタン\U000F0005を押す"},
        {"\uE000、あ\uE001\uF8FF", "\U000F0000、あ\U000F0001\U000F18FF"},
        {"a\uF900b", "a\uF900b"},
        {"x\xFFy", "x\xFFy"},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(pipeline::EscapePrivateUse(c.in), std::string(c.escaped));
        HD_CHECK_EQ(pipeline::UnescapePrivateUse(c.escaped), std::string(c.in));
    }
    // Restore writes the escaped input back.
    const Pieces typed = {{"ボタン"}, {"\U000F0005", SpanWord}, {"を押す"}};
    const Pieces cleaned = {{"버튼 "}, {"\U000F0005", SpanWord}, {"을 누른다"}};
    HD_CHECK_EQ(pipeline::Restore(cleaned, typed, true), std::string("버튼 \uE005을 누른다"));
}

HD_TEST(pipeline_write_emphasis) {
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"당신 같은 건, \uE008최저\uE009!", "당신 같은 건, 최·저!"},
        // Spaces between the syllables go; the syllables before the word are not joined to it.
        {"들어 오면, \uE008안 돼\uE009?", "들어 오면, 안·돼?"},
        {"제독 \uE008제독\uE009♪", "제독 제·독♪"},
        {"\uE008돈\uE009, 마니", "돈, 마니"},
        // A mark without its pair is dropped.
        {"\uE008사·실", "사·실"},
        {"그만\uE009", "그만"},
        {"그만", "그만"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(pipeline::WriteEmphasis(c.in), std::string(c.want));
}

HD_TEST(pipeline_write_stress) {
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D대단해!"), std::string("대애애단해!"));
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D정말로"), std::string("저어엉말로"));
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D절대"), std::string("저어얼대"));
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D위험했다"), std::string("위이이험했다"));
    // A final that is not held (ㄱ, ㅎ, two consonants), no final before a syllable with ㅇ, or
    // no syllable after the mark: the mark is dropped.
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D확실히"), std::string("확실히"));
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D매우"), std::string("매우"));
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D당연하지"), std::string("다아앙연하지"));
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D많이"), std::string("많이"));
    HD_CHECK_EQ(pipeline::WriteStress("\uE00D좋은"), std::string("좋은"));
    HD_CHECK_EQ(pipeline::WriteStress("\uE00DOK"), std::string("OK"));
    HD_CHECK_EQ(pipeline::WriteStress("대단해"), std::string("대단해"));
}

HD_TEST(pipeline_write_lengthening) {
    HD_CHECK_EQ(pipeline::WriteLengthening("선배\uE00A!"), std::string("선배~!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("선배~\uE00A!"), std::string("선배~!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("\uE00A선배"), std::string("~선배"));
    HD_CHECK_EQ(pipeline::WriteLengthening("군\uE00A\uE00A!"), std::string("군~~!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("군\uE00A\uE00A\uE00A!"), std::string("군~~!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("군~\uE00A\uE00A!"), std::string("군~~!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("케이짱\uE00A\uE00B!"), std::string("케이짜~앙!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("여러분\uE00A\uE00B"), std::string("여러부~운"));
    HD_CHECK_EQ(pipeline::WriteLengthening("씨\uE00A\uE00B"), std::string("씨이"));
    HD_CHECK_EQ(pipeline::WriteLengthening("선배"), std::string("선배"));
    // A drawn mark draws out the vowel of the syllable before it, the final moved last.
    HD_CHECK_EQ(pipeline::WriteLengthening("그만둬\uE00B\uE00B!"), std::string("그만둬어어!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("안 돼\uE00B"), std::string("안 돼에"));
    HD_CHECK_EQ(pipeline::WriteLengthening("안돼\uE00B\uE00B\uE00C!"), std::string("안돼에엣!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("선배님\uE00B\uE00B"), std::string("선배니이임"));
    HD_CHECK_EQ(pipeline::WriteLengthening("!\uE00B선배"), std::string("!선배"));
    // A clip mark closes the open syllable before it with ㅅ and is dropped elsewhere.
    HD_CHECK_EQ(pipeline::WriteLengthening("아파아파\uE00C!"), std::string("아파아팟!"));
    HD_CHECK_EQ(pipeline::WriteLengthening("안돼\uE00C"), std::string("안됏"));
    HD_CHECK_EQ(pipeline::WriteLengthening("없\uE00C"), std::string("없"));
    HD_CHECK_EQ(pipeline::WriteLengthening("!\uE00C"), std::string("!"));
}

HD_TEST(pipeline_drop_particle_marks) {
    HD_CHECK_EQ(pipeline::DropParticleMarks("섹○\uE001를 가○\uE002을 애○\uE003을"), std::string("섹○를 가○을 애○을"));
    HD_CHECK_EQ(pipeline::DropParticleMarks("\uE000、아"), std::string("\uE000、아"));
    HD_CHECK_EQ(pipeline::DropParticleMarks("a\xFF" "b"), std::string("a\xFF" "b"));
}

HD_TEST(pipeline_restore) {
    // A stutter span and a Hangul span come back as text in the line.
    {
        const Pieces typed = {{"「"}, {"\uE000、", SpanOpen}, {"分かった…」"}};
        const Pieces cleaned = {{"「"}, {"\uE000、", SpanOpen}, {"알았다…」"}};
        HD_CHECK_EQ(pipeline::Restore(cleaned, typed, true), std::string("「아, 알았다…」"));
    }
    {
        const Pieces typed = {{"「良かった。"}, {"그럼", SpanWord}, {"、適当に座ってください」"}};
        const Pieces cleaned = {{"「좋았다. "}, {"그럼", SpanWord}, {", 적당하게 앉아 주세요」"}};
        HD_CHECK_EQ(pipeline::Restore(cleaned, typed, true), std::string("「좋았다. 그럼, 적당하게 앉아 주세요」"));
    }
    // Brackets as typed, then the ASCII forms.
    {
        const Pieces typed = {{"（なんで氷なんぞがこんな場所に普通にあるのだ？）"}};
        const Pieces cleaned = {{"( 어째서 얼음 따위가 이런 장소에 보통으로 있다?)"}};
        const auto st = pipeline::RestoreAll(cleaned, typed, true);
        HD_CHECK_EQ(Show(st.spaced), Show({{"(어째서 얼음 따위가 이런 장소에 보통으로 있다?)"}}));
        HD_CHECK_EQ(Show(st.bracketed), Show({{"（어째서 얼음 따위가 이런 장소에 보통으로 있다?）"}}));
        HD_CHECK_EQ(Show(st.korean), Show({{"(어째서 얼음 따위가 이런 장소에 보통으로 있다?)"}}));
        HD_CHECK_EQ(st.line, std::string("(어째서 얼음 따위가 이런 장소에 보통으로 있다?)"));
    }
    {
        const Pieces typed = {{"『目的地』"}};
        const Pieces cleaned = {{"「목적지」"}};
        HD_CHECK_EQ(pipeline::Restore(cleaned, typed, true), std::string("『목적지』"));
        HD_CHECK_EQ(pipeline::Restore(cleaned, typed, false), std::string("「목적지」"));
    }
    // Dropped kana are read after the cleanup, stutters after them.
    {
        const Pieces typed = {{"「"}, {"\uE000、", SpanOpen}, {"ぷぷっ」"}};
        const Pieces cleaned = {{"「"}, {"\uE000、", SpanOpen}, {Dropped("ぷぷっ") + "」"}};
        HD_CHECK_EQ(pipeline::Restore(cleaned, typed, true), std::string("「푸, 푸풋」"));
    }
}

HD_TEST(pipeline_literal_cleanup) {
    const struct { const char* in; const char* want; } cases[] = {
        {"몇번 몇년 몇일", "몇 번 몇 년 며칠"},
        {"아펐고 괴로왔어", "아팠고 괴로웠어"},
        {"고 싶다라고 했었었다", "고 싶다고 했었다"},
        {"안되 제안되 고안된 감안되", "안되 제안되 고안된 감안되"},
        {"몇번몇번", "몇 번몇 번"},
        {"있읍니다 했읍니다 없읍니다", "있습니다 했습니다 없습니다"},
        {"모읍니다 기웁니다 읍니다", "모읍니다 기웁니다 읍니다"},
        {"나갈 때에 돌아왔을 때에는 하는 때에,", "나갈 때 돌아왔을 때는 하는 때,"},
        {"그 때에 때에 달게 함대 때에 비하면", "그 때에 때에 달게 함대 때에 비하면"},
        {"상황 때에 따라 할 때에 따라", "상황 때에 따라 할 때에 따라"},
        {"다닐 수 있고 있어", "다닐 수 있어"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(pipeline::KoreanCleanup(c.in), std::string(c.want));
    const Pieces typed{{"何度も"}, {"몇번", SpanWord}};
    const Pieces translated{{"몇번"}, {"몇번", SpanWord}};
    HD_CHECK_EQ(pipeline::RestoreAll(translated, typed, true, true).line, "몇 번몇번");
    HD_CHECK_EQ(pipeline::RestoreAll(translated, typed, true).line, "몇번몇번");
}

HD_TEST(pipeline_laughter_letters) {
    const struct { const char* in; const char* want; } cases[] = {
        {"그것은 없어 w", "그것은 없어 ㅋ"},
        {"진짜야www", "진짜야ㅋㅋㅋ"},
        {"진짜야! ww", "진짜야! ㅋㅋ"},
        {"ㅋㅋ wwww", "ㅋㅋㅋㅋ"},
        {"ㅋㅋㅋw", "ㅋㅋㅋ"},
        {"좋아. www", "좋아. www"},
        {"www", "ㅋㅋㅋ"},
        {"송wwww 그런가", "송ㅋㅋㅋㅋ 그런가"},
        {"www.example.com", "www.example.com"},
        {"w를 입력", "w를 입력"},
        {"OK www", "OK www"},
        {"twwwitter", "twwwitter"},
        {"게임의 wiki", "게임의 wiki"},
        {"3w", "3w"},
        {"\xff 진짜야www", "\xff 진짜야ㅋㅋㅋ"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(pipeline::WriteLaughter(c.in, ""), std::string(c.want));
    // With the typed text: a letter used as a word stays, laughter before the next words goes.
    HD_CHECK_EQ(pipeline::WriteLaughter("변수 w의 값", "変数wの値"), std::string("변수 w의 값"));
    HD_CHECK_EQ(pipeline::WriteLaughter("www의 의미", "wwwの意味"), std::string("www의 의미"));
    HD_CHECK_EQ(pipeline::WriteLaughter("키보드의 w키", "キーボードのwキー"), std::string("키보드의 w키"));
    HD_CHECK_EQ(pipeline::WriteLaughter("있어w미도리 씨w", "あるよなｗミドリさんｗ"), std::string("있어ㅋ미도리 씨ㅋ"));
    HD_CHECK_EQ(pipeline::WriteLaughter("웃은 wwww미안해", "笑ったｗｗｗｗゴメン"), std::string("웃은 ㅋㅋㅋㅋ미안해"));
    // Each run is read in the typed text when both have the same runs.
    const struct { const char* in; const char* typed; const char* want; } typed[] = {
        {"좋아. www", "大好き。ｗｗｗ", "좋아. ㅋㅋㅋ"},
        {"좋아―ww", "ホントだねーｗｗ", "좋아―ㅋㅋ"},
        {"≫454www≫456", "≫454ｗｗｗ≫456", "≫454ㅋㅋㅋ≫456"},
        {"맛있지 wwby", "おいしいよねｗｗby", "맛있지 ㅋㅋby"},
        {"wwwwww일을", "ｗｗｗｗｗｗ一乙", "ㅋㅋㅋㅋㅋㅋ일을"},
        {"그것은 ㅋㅋ w", "それは草ｗ", "그것은 ㅋㅋ"},
        {"ㅋㅋㅋwwwww", "ワロタwwwww", "ㅋㅋㅋㅋㅋ"},
        {"진짜냐(w)", "マジかよ（ｗ）", "진짜냐(웃음)"},
        {"ww (웃음)", "ｗｗ（笑）", "ㅋㅋ"},
        {"f(w)를 구한다", "f(w)を求める", "f(w)를 구한다"},
        {"폭(w), 높이(h)", "幅(w)、高さ(h)", "폭(w), 높이(h)"},
        {"w배에 나온다", "ｗ杯に出る", "w배에 나온다"},
        {"www.example.com", "ｗｗｗ.example.com", "www.example.com"},
        {"No...way", "No...way", "No...way"},
        {"야...w", "よ...w", "야...ㅋ"},
        {"OK www", "OK www", "OK www"},
        {"w=3으로 한다", "w=3とする", "w=3으로 한다"},
        {"@wwwboy 씨", "@wwwboyさん", "@wwwboy 씨"},
        {"ID:abcwww+", "ID:abcwww+", "ID:abcwww+"},
    };
    for (const auto& c : typed) HD_CHECK_EQ(pipeline::WriteLaughter(c.in, c.typed), std::string(c.want));
}
