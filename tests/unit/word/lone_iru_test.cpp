// GRM-8100: a reading that ends in hiragana gives way to a shorter one when it leaves the stem い of いる behind it and
// the word after the shorter reading, no verb, takes that い with all the verb read after it.
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = hdTrans::dict::Load({{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "何かな\t무엇이지\tN110\t\t\tGN\t0\t1\n"
        "何か\t뭔가\tN110\t\t\tGN\t0\t2\n"
        "何\t무엇\tN110\t\t\tGN\t0\t3\n"
        "誰か\t누군가\tN110\t\t\tGN\t0\t4\n"
        "誰\t누구\tN110\t\t\tGN\t0\t5\n"
        "な\t$FA@\tH7U0\t\t\tGN\t0\t6\n"
        "い\t1다\tT500\t\t\tGN\t0\t7\n"
        "い\t&HN@\tM417\t\tDaVa\tGN\t0\t8\n"
        "かい\t사&AX@\tM430\t\t\tGN\t0\t9\n"
        "言\t말&AV@\tM410\t\t\tGN\t0\t10\n"
        "V\t1고\tS571\t\tJ1\tGN\t0\t11\n"
        "V\t4다\tS500\t\t\tGN\t0\t12\n"
        "V\t0\tS500\t\t\tGN\t0\t13\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    const std::string line(text);
    const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_lone_iru_gives_way_to_the_word_after_a_shorter_reading) {
    HD_CHECK_EQ(Translate("何かない"), "뭔가 없다");
}

HD_TEST(word_lone_iru_stays_where_a_verb_takes_it) {
    // The word after the shorter reading is a verb (誰 + かいる): いる stays.
    HD_CHECK(Translate("誰かいる").starts_with("누군가"));
}
