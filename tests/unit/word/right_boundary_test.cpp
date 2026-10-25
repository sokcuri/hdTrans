// The Ow piece of the rules after a word closed by っ: a comma or an ellipsis after the っ ends
// the clause; other marks between do not.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_right_boundary_after_tsu) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "犬\t가\tN110\tXXX\t\tGN\t0\t1\t\n"
         "犬\t나\tN110\t\t\tGN\t2\t2\t||1 G+0== \"Ow\"\n"
         "っ\t\tQ110\t\t\tGN\t0\t3\t\n"
         "猫\t이\tN110\t\t\tGN\t0\t4\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"犬っ、猫", "나, 이"},
        {"犬っ……～猫", "나……～이"},
        {"犬っ♪～猫", "가♪～이"},
        {"犬っ★～猫", "가★～이"},
        {"犬っ猫", "가이"},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}

// A predicate before an ellipsis ends its clause only when a word that can begin one follows:
// an adverb or conjunction, a pronoun, a demonstrative, a bound noun that opens a clause
// (ところで、, そうか). An adverbial particle, an auxiliary, the particle かな, a noun it may
// modify or an attributive phrase continues it.
HD_TEST(word_right_boundary_predicate_before_ellipsis) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "高\t$D3@\tH7N0\t\t\tGN\t0\t1\t\n"
         "い\t4\tT500\t\t\tGN\t0\t2\t\n"
         "い\t1다\tT500\t\t\tGN\t2\t3\t||1 W+0== \"Ow\"\n"
         "しかも\t게다가\tE300\t\t\tGN\t0\t4\t\n"
         "まで\t까지\tP221\t\t\tGN\t0\t5\t\n"
         "すべての\t모든\tC0D1\t\t\tGN\t0\t6\t\n"
         "この\t이\tE0H1\t\t\tGN\t0\t7\t\n"
         "かも\t일지도\tP211\t\t\tGN\t0\t8\t\n"
         "ようやく\t드디어\tE300\t\t\tGN\t0\t9\t\n"
         "ように\t처럼\tP200\t\t\tGN\t0\t10\t\n"
         "かな\t일까\tP200\t\t\tGN\t0\t11\t\n"
         "かなり\t꽤\tE300\t\t\tGN\t0\t12\t\n"
         "そう\t그래\tN110\t\t\tGN\t0\t13\t\n"
         "か\t인가\tP200\t\t\tGN\t0\t14\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"高い……しかも", "높다……게다가"},
        {"高い……この", "높다……이"},
        {"高い……ようやく", "높다……드디어"},
        {"高い……まで", "높은……까지"},
        {"高い……かも", "높은……일지도"},
        {"高い……すべての", "높은……모든"},
        {"高い……ように", "높은……처럼"},
        {"高い……かな", "높은……일까"},
        {"高い……かなり", "높다……꽤"},
        {"高い……ところで、", "높다……토코로데,"},
        {"高い……そうか", "높다……그래인가"},
    };
    for (const Case& c : cases) {
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
