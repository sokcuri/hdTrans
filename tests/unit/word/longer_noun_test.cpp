// A dictionary noun that starts with an adnominal in の written from a kanji (大の字
// after 大の) is read whole when a particle, punctuation or the end follows it. A
// longer noun row (tag letter I, K or N) is read whole after a shorter noun row of
// another letter, unless it ends in hiragana and runs on into kana that start a verb
// form rather than a particle or a copula, or the shorter row is a phrase or function
// row of another letter.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_longer_noun_after_adnominal_no) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "大の\t대단한\tE0H1\t\t\tGN\t0\t1\t\n"
         "大の字\t큰대자\tI110\t\t\tGN\t0\t2\t\n"
         "字\t글자\tN110\t\t\tGN\t0\t3\t\n"
         "字幕\t자막\tN110\t\t\tGN\t0\t4\t\n"
         "に\t에\tP200\t\t\tGN\t0\t5\t\n"
         "あの\t그\tE0H1\t\t\tGN\t0\t6\t\n"
         "あの人\t저이\tN110\t\t\tGN\t0\t7\t\n"
         "人\t사람\tN110\t\t\tGN\t0\t8\t\n"
         "、\t,\tN600\t\tMk\tGN\t0\t9\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"大の字に", "큰대자에"},
        {"大の字", "큰대자"},
        {"大の字幕", "대단한 자막"},
        {"あの人に", "그 사람에"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}

HD_TEST(word_longer_noun_after_noun_of_another_letter) {
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n";
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         header +
             "治験\t임상 시험\tK110\t\t\tGN\t0\t1\t\n"
             "薬\t약\tN110\t\t\tGN\t0\t2\t\n"
             "治験薬\t임상 시험약\tN110\t\t\tGN\t0\t3\t\n"
             "例\t예\tN110\t\t\tGN\t0\t4\t\n"
             "を\t(을)를\tP200\t\t\tGN\t0\t5\t\n"
             "が\t(이)가\tP200\t\t\tGN\t0\t6\t\n"
             "化け物\t괴물\tI110\t\t\tGN\t0\t7\t\n"
             "化け物屋敷\t흉가\tN110\t\t\tGN\t0\t8\t\n"
             "屋敷\t저택\tN110\t\t\tGN\t0\t9\t\n"
             "取り分\t몫\tI110\t\t\tGN\t0\t10\t\n"
             "取り分け\t분배\tN110\t\t\tGN\t0\t11\t\n"
             "ぶり\t만\tM110\t\t\tGN\t0\t12\t\n"
             "ぶりに\t만이 되어\tN110\t\t\tGN\t0\t13\t\n"
             "に\t에\tP200\t\t\tGN\t0\t14\t\n"
             "だ\t이다\tU500\t\t\tGN\t0\t15\t\n"
             "だ\t이다\tP200\t\t\tGN\t0\t16\t\n"
             "から\t에서\tP221\t\t\tGN\t0\t17\t\n"
             "て\t1고\tS570\t\t\tGN\t0\t18\t\n"
             "て\t(이)라고\tP200\t\t\tGN\t0\t19\t\n"},
        {0, "words.tsv", "#! layer=words\n" + header + "治験例\t치험례\tN110\t\t\tGN\t0\tw1\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* word;  // the Korean of the longer row
        bool whole;
    };
    const Case cases[] = {
        {"治験薬を", "임상 시험약", true},
        {"治験例", "치험례", true},
        {"化け物屋敷", "흉가", true},
        {"取り分けが", "분배", true},
        {"取り分けだ", "분배", true},
        {"取り分けから", "분배", true},
        {"取り分けて", "분배", false},
        {"ぶりに", "만이 되어", false},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0].find(c.word) != std::string::npos, c.whole);
    }
    const std::string shorter = "治験を";
    hdTrans::internal::Memory memory;
    const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&shorter, 1}, memory, {}, {});
    HD_REQUIRE(out.size() == 1);
    HD_CHECK_EQ(out[0], std::string("임상 시험을"));
}
