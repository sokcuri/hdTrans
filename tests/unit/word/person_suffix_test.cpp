// A familiar suffix of honorific.after-person (たん, っち) is a word only after a name or a
// noun of persons or animals (a plural written as one word too), and not where a longer
// dictionary word starts at that noun and goes on past it or a longer noun starts at the suffix;
// elsewhere the kana around it are read. Where it is a word, a longer word other than a noun that
// starts with it (the adverb たんと) gives way to it.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_person_suffix_after_person_only) {
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n";
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         header +
             "先生\t선생님\tN110\tVHn\t\tGN\t0\t1\t\n"
             "たまご\t달걀\tI110\t\t\tGN\t0\t2\t\n"
             "たまごっち\t다마고치\tK130\t\t\tGN\t0\t3\t\n"
             "机\t책상\tN110\t\t\tGN\t0\t4\t\n"
             "たんこぶ\t혹\tI110\t\t\tGN\t0\t7\t\n"
             "子供\t아이\tN110\tVHn\t\tGN\t0\t8\t\n"
             "子供たち\t아이들\tN110\tVHn\t\tGN\t0\t9\t\n"
             "が\t(이)가\tP200\t\t\tGN\t0\t5\t\n"
             "の\t의\tP200\t\t\tGN\t0\t6\t\n"
             "と\t(와)과\tP200\t\t\tGN\t0\t10\t\n"
             "たんと\t듬뿍\tE300\t\t\tGN\t0\t11\t\n"
             "猫\t고양이\tN110\tQAA\t\tGN\t0\t12\t\n"},
        {0, "words.tsv",
         "#! layer=words\n" + header + "たん\t짱\tN110\tVHn\tTu\tGN\t0\tw1\t\n" + "っち\t치\tN110\tVHn\tTu\tGN\t0\tw2\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"先生たんが", "선생님짱이"},
        {"先生っちの", "선생님치의"},
        {"たまごっちが", "다마고치가"},
        {"先生たんこぶが", "선생님 혹이"},
        {"子供たちたんが", "아이들짱이"},
        {"子供たちたんこぶが", "아이들 혹이"},
        {"先生たんと", "선생님짱과"},
        {"猫たんと", "고양이짱과"},
        {"机がたんと", "책상이 듬뿍"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
    // After a noun of things the suffix is no word of its own.
    const std::string thing = "机たんが";
    hdTrans::internal::Memory memory;
    const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&thing, 1}, memory, {}, {});
    HD_REQUIRE(out.size() == 1);
    HD_CHECK(out[0].find("짱") == std::string::npos);
}
