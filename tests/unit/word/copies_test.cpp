// A unit said two or more times is read copy by copy: no word of one copy takes the start of
// the next, and a word of the dictionary that spells all the copies keeps them.
#include <string>
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
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
    // Past the depth limit of the word search every copy is still read (no kana left).
    std::string many;
    for (int i = 0; i < 301; i++) many += "かお";
    hdTrans::internal::Memory memory;
    const std::vector<std::string> lines = {many};
    const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
    HD_REQUIRE(out.size() == 1);
    HD_CHECK(out[0].find("か") == std::string::npos && out[0].find("お") == std::string::npos);
}
