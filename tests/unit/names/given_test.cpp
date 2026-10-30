// Given names of one kanji and a 郎 core: the reading of the kanji from the name dictionaries
// first (the other 郎 names, the kanji alone as a name, the head of the names it starts), then the
// prefixes of common names and the first on reading.
#include <string>

#include "core/dict/snapshot.h"
#include "core/names/given.h"
#include "core/text/kana.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

const dict::Snapshot* Dictionary() {
    static const dict::LoadResult loaded = dict::Load({
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "恭二郎\t쿄우지로우\tN130\tQPN\tWb\tGN\t1\n"},
        {0, "names.tsv",
         "#! layer=names\n"
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "矢一郎\t야이치로\tN130\tQPNVHn\tWb\tGN\t2\n"
         "矢次郎\t야지로\tN130\tQPNVHn\tWb\tGN\t3\n"
         "一郎\t이치로\tN130\tQPNVHn\tWb\tGN\t4\n"
         "呉\t쿠레\tN130\tQPNVHn\tWa\tGN\t5\n"
         "鷹\t매\tN130\tQPNVHn\tWb\tGN\t6\n"},
    });
    return loaded.snapshot.get();
}

const names::RoCore& Core(std::string_view kanji) {
    for (const names::RoCore& c : names::RoCores()) {
        if (c.kanji == kanji) return c;
    }
    return names::RoCores().front();
}

}  // namespace

HD_TEST(names_kanji_tables) {
    HD_CHECK_EQ(std::string(names::internal::OnReading(U'凶')), std::string("きょう"));
    HD_CHECK_EQ(std::string(names::internal::OnReading(U'あ')), std::string());
    HD_CHECK_EQ(std::string(names::internal::HeadReading(U'塩')), std::string("しお"));
}

HD_TEST(names_ro_given_name) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    // The kanji as the other given names of the name layer read it.
    HD_CHECK_EQ(names::RoGivenName(*snap, "矢", Core("三郎")), std::string("야사부로"));
    // The base dictionary's, without the drawn-out ou.
    HD_CHECK_EQ(names::RoGivenName(*snap, "恭", Core("一郎")), std::string("쿄이치로"));
    // The kanji alone as a name, unless the head of the names it starts reads it otherwise.
    HD_CHECK_EQ(names::RoGivenName(*snap, "呉", Core("一郎")), std::string("쿠레이치로"));
    HD_CHECK_EQ(names::RoGivenName(*snap, "鷹", Core("一郎")), std::string("타카이치로"));
    // The head of the names it starts.
    HD_CHECK_EQ(names::RoGivenName(*snap, "塩", Core("二郎")), std::string("시오지로"));
    // A prefix of common names.
    HD_CHECK_EQ(names::RoGivenName(*snap, "鶴", Core("太郎")), std::string("츠루타로"));
    // The first on reading.
    HD_CHECK_EQ(names::RoGivenName(*snap, "凶", Core("一郎")), std::string("쿄이치로"));
    HD_CHECK_EQ(names::RoGivenName(*snap, "あ", Core("一郎")), std::string());
}

HD_TEST(names_without_long_vowels) {
    HD_CHECK_EQ(text::WithoutNameLongVowels("쿄우이치로우", false), std::string("쿄이치로"));
    // A u before e starts 上 (井上一郎 이노우에 이치로우).
    HD_CHECK_EQ(text::WithoutNameLongVowels("이노우에 이치로우", false), std::string("이노우에 이치로"));
    // The oo of a dictionary row stays unless asked (大島 오오시마).
    HD_CHECK_EQ(text::WithoutNameLongVowels("오오시마 사부로우", false), std::string("오오시마 사부로"));
    HD_CHECK_EQ(text::WithoutNameLongVowels("오오시마", true), std::string("오시마"));
    HD_CHECK_EQ(text::WithoutNameLongVowels("슈우우", false), std::string("슈우"));
}
