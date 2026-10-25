// A unit said two or more times is read copy by copy: no word of one copy takes the start of
// the next, and a word of the dictionary that spells all the copies keeps them.
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_copies_read_one_by_one) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "あい\t사랑\tN110\tXXX\t\tGN\t0\t1\t\n"
         "あいあ\t섬\tN110\tXXX\t\tGN\t0\t2\t\n"
         "はい\t네\tN110\tXXX\t\tGN\t0\t3\t\n"
         "はいはい\t네네\tN110\tXXX\t\tGN\t0\t4\t\n"
         "いろ\t색\tN110\tXXX\t\tGN\t0\t5\t\n"
         "いろいろ\t여러 가지\tN110\tXXX\t\tGN\t0\t6\t\n"
         "海辺\t해변\tN110\tXXX\t\tGN\t0\t7\t\n"
         "海辺町\t바닷가 마을\tN110\tXXX\t\tGN\t0\t8\t\n"
         "い\t위\tN110\tXXX\t\tGN\t0\t9\t\n"
         "かお\t얼굴\tN110\tXXX\t\tGN\t0\t11\t\n"
         "まだ\t아직\tE300\tXXX\t\tGN\t0\t12\t\n"
         "まだまだ\t아직도\tE300\tXXX\t\tGN\t0\t13\t\n"
         "まだま\t마다마\tN110\tXXX\t\tGN\t0\t14\t\n"
         "は\t(은)는\tP200\tXXX\t\tGN\t0\t10\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"あいあい", "사랑 사랑"},    // not あいあ + い
        {"海辺、海辺", "해변, 해변"},
        {"はいはいはい", "네네네"},   // each copy, written as one
        {"まだまだまだ", "아직 아직 아직"},  // not the word of two copies, nor まだま inside the run
        {"まだまだ", "아직도"},
        {"いろいろ", "여러 가지"},     // the word of the copies
        {"海辺、海辺町", "해변, 바닷가 마을"},  // the next copy starts a longer word
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
    // Past the depth limit of the word search every copy is still read (no kana left).
    std::string many;
    for (int i = 0; i < 301; i++) many += "かお";
    const std::vector<std::string> lines = {many};
    const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
    HD_REQUIRE(out.size() == 1);
    HD_CHECK(out[0].find("か") == std::string::npos && out[0].find("お") == std::string::npos);
}

// An adjective said again as the last predicate is a cry in 6 on every copy, each copy closed
// with ㅅ before its clipping っ; an adjective of adjective.grumble said twice with nothing
// after is a grumble in 1다 and 6. The analysis takes the dictionary's adjective stems and
// endings, so the test needs HDTRANS_DICTS.
HD_TEST(word_copies_adjective_cries) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = hdTrans::dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    auto translate = [&](std::string_view text) {
        const std::string line(text);
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
        return out.empty() ? std::string() : out[0];
    };
    HD_CHECK_EQ(translate("痛い痛い"), "아파 아파");
    HD_CHECK_EQ(translate("痛い痛いっ"), "아파 아팟");  // the clipping っ closes the last syllable
    HD_CHECK_EQ(translate("痛い、痛いっ！"), "아파, 아팟!");
    HD_CHECK_EQ(translate("痛い痛いいいいっ"), "아파 아파아아앗");  // the stretch, then ㅅ
    HD_CHECK_EQ(translate("寒い寒い"), "춥다 추워");  // a grumble
    HD_CHECK_EQ(translate("寒い寒い寒い"), "추워 추워 추워");  // three copies cry
    HD_CHECK_EQ(translate("寒い寒い！"), "추워 추워!");
    HD_CHECK_EQ(translate("痛い。"), "아프다.");  // said once
    HD_CHECK_EQ(translate("だめだめっ"), "안돼 안됏");  // a predicate without an ending, one word
    HD_CHECK_EQ(translate("だめ"), "안돼");  // LAY-11
    HD_CHECK_EQ(translate("だめだだめだ！"), "안돼 안돼!");  // the copy before another is contracted too
    HD_CHECK_EQ(translate("やだやだやだっ"), "싫어 싫어 싫엇");  // a sound of the lexicon
    HD_CHECK_EQ(translate("長い長い夜だった。"), "긴 긴 밤이었다.");  // copies before a noun
    // Copies drawn out by one or two vowel kana are copies across their cry marks.
    HD_CHECK_EQ(translate("痛いぃ、痛いぃ"), "아파, 아파");
    HD_CHECK_EQ(translate("怖いいいっ、怖いいいっ！"), "무서웟, 무서웟!");
    HD_CHECK_EQ(translate("痛いぃ……でも我慢する"), "아파……하지만 참는다");  // not the attributive
    HD_CHECK_EQ(translate("痛いぃ……いや"), "아파……아니");  // nor before a noun
    HD_CHECK_EQ(translate("痛いー、痛いー"), "아파~, 아파~");  // copies across their lengthening
    HD_CHECK_EQ(translate("（痛いー）"), "(아프다~)");  // a thought is no cry
    // Each copy clipped with its own っ is still a copy and closes with ㅅ.
    HD_CHECK_EQ(translate("だめっだめっ"), "안됏 안됏");
    HD_CHECK_EQ(translate("痛いっ、痛いっ……"), "아팟, 아팟……");
    HD_CHECK_EQ(translate("待って待ってっ"), "기다려 기다렷");
    HD_CHECK_EQ(translate("先輩っ先輩っ"), "선배 선배");  // a noun keeps its Korean
    HD_CHECK_EQ(translate("痛いっ！"), "아파!");  // said once, spoken before ！ (SPC-1)
}

// Three or more copies of a verb stem and ending that also spell a noun are a cry, each copy read
// as the noun said alone, whatever stands before or after the run (SND-2); the dictionary word
// 好き好き keeps its own reading. Needs HDTRANS_DICTS for the stems and endings.
HD_TEST(word_copies_cried_nouns) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = hdTrans::dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    auto translate = [&](std::string_view text) {
        const std::string line(text);
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
        return out.empty() ? std::string() : out[0];
    };
    HD_CHECK_EQ(translate("好き好き好き、大好き"), "좋아 좋아 좋아, 너무 좋아");  // before a comma
    HD_CHECK_EQ(translate("好き、好き、好き"), "좋아, 좋아, 좋아");
    HD_CHECK_EQ(translate("ずっと好き好き好き"), "계속 좋아 좋아 좋아");  // after a word
    HD_CHECK_EQ(translate("好き、好き、好き、大好き"), "좋아, 좋아, 좋아, 너무 좋아");  // the last copy too
    HD_CHECK_EQ(translate("好き、大好き"), "좋아해, 너무 좋아");  // said once
    HD_CHECK_EQ(translate("好き好きだ"), "각자 기호다");  // the dictionary word
    HD_CHECK_EQ(translate("迷い迷い迷い、歩いた"), "헤매어 헤매어 헤매어, 걸었다");  // a verb stem stays the verb
}
