#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

// Kana the analysis cannot place keep their text (MNG-5): a case particle between a noun and a
// placed word is read as the particle, and a sound said on its own before a comma is written as
// heard unless it ends a word of the token before. The analysis takes the dictionary's words, so
// the test needs HDTRANS_DICTS.
HD_TEST(word_unplaced_kana) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    auto translate = [&](std::string_view text) {
        const std::string line(text);
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
        return out.empty() ? std::string() : out[0];
    };
    auto has = [](const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; };
    HD_CHECK(has(translate("その変態を黙らせろ"), "변태를"));
    HD_CHECK(has(translate("女性と見まごうような輪郭"), "여성과"));
    HD_CHECK(!has(translate("鼻がぽぺるまで"), "코가 까지"));
    HD_CHECK(has(translate("「む、無理です」"), "무, 무리"));
    HD_CHECK(has(translate("す、と頭を撫でた。"), "스, "));
    HD_CHECK(!has(translate("がばり、と身体を起こした。"), "리,"));
    HD_CHECK_EQ(translate("まりやん！"), "마리잖아!");
    HD_CHECK_EQ(translate("まりです。"), "마리입니다.");
    HD_CHECK_EQ(translate("ぽぺやん！"), "포페잖아!");
    HD_CHECK(has(translate("私はまりです。"), "마리입니다"));
    HD_CHECK_EQ(translate("(まりやん)"), "(마리얀)");
    HD_CHECK_EQ(translate("まりやん≫"), "마리얀≫");
    HD_CHECK_EQ(translate("学生やん"), "학생이잖아");
    HD_CHECK_EQ(translate("書いたやん"), "썼잖아");
    HD_CHECK_EQ(translate("まりちゃん。"), "마리짱.");
    HD_CHECK(!has(translate("えへへ、どうぞです♪"), "도우조"));
}
