#include "lexicon.h"

#include "hdtest.h"

#include <string>

namespace {

hd8::lex::Lexicon& Fixture(bool& ok) {
    static hd8::lex::Lexicon lex;
    static bool loaded = false, good = false;
    if (!loaded) {
        std::string err;
        good = lex.Load(HD8_LEX_FIXTURE, err);
        if (!good) std::fprintf(stderr, "%s\n", err.c_str());
        loaded = true;
    }
    ok = good;
    return lex;
}

using hd8::lex::Pos;

HD_TEST(lexicon_finds_lemma_ids) {
    bool ok = false;
    auto& lex = Fixture(ok);
    HD_REQUIRE(ok);
    HD_CHECK_EQ(lex.EntryCount(), size_t{6});
    const int32_t toru = lex.Find("取る", "とる", Pos::v5);
    HD_REQUIRE(toru >= 0);
    HD_CHECK_EQ(lex.Senses(static_cast<uint32_t>(toru)).size(), size_t{3});
    HD_CHECK(lex.Find("取る", "トル", Pos::v5) == toru);   // katakana reading
    HD_CHECK(lex.Find("取る", "とる", Pos::v1) == toru);   // verb class family
    HD_CHECK(lex.Find("取る", "とる", Pos::n) < 0);
    HD_CHECK(lex.Find("撮る", "とる", Pos::v5) < 0);
    const int32_t kata = lex.Find("方", "かた", Pos::n), hou = lex.Find("方", "ほう", Pos::n);
    HD_REQUIRE(kata >= 0 && hou >= 0);
    HD_CHECK(kata != hou);
    HD_CHECK_EQ(std::string(lex.Str(lex.Senses(static_cast<uint32_t>(kata))[0].ko)), std::string("분"));
    HD_CHECK_EQ(lex.FindSpelling("方").size(), size_t{2});
    HD_CHECK_EQ(lex.RowId(static_cast<uint32_t>(toru), 2), std::string("lex:取る/とる/v5#2"));
}

HD_TEST(lexicon_reads_senses_and_cues) {
    bool ok = false;
    auto& lex = Fixture(ok);
    HD_REQUIRE(ok);
    const auto senses = lex.Senses(static_cast<uint32_t>(lex.Find("取る", "とる", Pos::v5)));
    HD_REQUIRE(senses.size() == 3);
    HD_CHECK(senses[0].nAtom == 0);
    HD_CHECK(static_cast<hd8::lex::KClass>(senses[0].kcls) == hd8::lex::KClass::REG);
    const auto atoms = lex.Atoms(senses[1]);
    HD_REQUIRE(atoms.size() == 1);
    HD_CHECK(static_cast<hd8::lex::Slot>(atoms[0].slot) == hd8::lex::Slot::obj);
    const auto alts = lex.Alts(atoms[0]);
    HD_REQUIRE(alts.size() == 3);
    HD_CHECK_EQ(std::string(lex.Str(alts[0].value)), std::string("写真"));
    HD_CHECK(static_cast<hd8::lex::AltKind>(alts[2].kind) == hd8::lex::AltKind::cls);
    const auto third = lex.Atoms(senses[2]);
    HD_REQUIRE(third.size() == 2);
    HD_CHECK(static_cast<hd8::lex::AltKind>(lex.Alts(third[1])[0].kind) == hd8::lex::AltKind::absent);
    const auto kiku = lex.Senses(static_cast<uint32_t>(lex.Find("聞く", "きく", Pos::v5)));
    HD_REQUIRE(kiku.size() == 1);
    HD_CHECK(static_cast<hd8::lex::KClass>(kiku[0].kcls) == hd8::lex::KClass::D);
    HD_CHECK_EQ(std::string(lex.Str(kiku[0].ko)), std::string("듣다:ㄷ"));
    const auto dekiru = lex.Senses(static_cast<uint32_t>(lex.Find("出来る", "できる", Pos::v1)));
    HD_REQUIRE(dekiru.size() == 1);
    HD_CHECK(static_cast<hd8::lex::KClass>(dekiru[0].kcls) == hd8::lex::KClass::EXIST);
    HD_CHECK_EQ(std::string(lex.Str(dekiru[0].frame)), std::string("が=을"));
    const int32_t lu = lex.Find("宿題提出", "しゅくだいていしゅつ", Pos::n);
    HD_REQUIRE(lu >= 0);
    HD_CHECK(lex.Entry(static_cast<uint32_t>(lu)).flags & hd8::lex::kEntryLongUnit);
}

HD_TEST(lexicon_classes_and_phrases) {
    bool ok = false;
    auto& lex = Fixture(ok);
    HD_REQUIRE(ok);
    const int32_t hat = lex.ClassIndex("@headwear"), cloth = lex.ClassIndex("@clothing");
    const int32_t person = lex.ClassIndex("@person");
    HD_REQUIRE(hat >= 0 && cloth >= 0 && person >= 0);
    HD_CHECK(lex.IsA(hat, cloth));
    HD_CHECK(lex.IsA(hat, lex.ClassIndex("@entity")));
    HD_CHECK(!lex.IsA(cloth, hat));
    HD_CHECK(!lex.IsA(person, cloth));
    HD_CHECK(lex.ClassIndex("@nothing") < 0);
    const auto ph = lex.PhrasesStartingWith("年を取る");
    HD_REQUIRE(ph.size() == 1);
    HD_CHECK_EQ(std::string(lex.Str(ph[0].ko)), std::string("나이를 먹다"));
    HD_CHECK(lex.PhrasesStartingWith("無い").empty());
}

HD_TEST(lexicon_names_pack) {
    hd8::lex::Lexicon names;
    std::string err;
    HD_REQUIRE(names.Load(HD8_LEX_FIXTURE_NAMES, err));
    HD_CHECK_EQ(names.NameCount(), size_t{1});
    const auto tokyo = names.FindNames("東京");
    HD_REQUIRE(tokyo.size() == 1);
    HD_CHECK_EQ(std::string(names.Str(tokyo[0].ko)), std::string("도쿄"));
    HD_CHECK(static_cast<hd8::lex::NameType>(tokyo[0].type) == hd8::lex::NameType::place);
    HD_CHECK(names.FindNames("大阪").empty());
}

HD_TEST(lexicon_rejects_bad_input) {
    hd8::lex::Lexicon lex;
    std::string err;
    HD_CHECK(!lex.LoadBytes({}, err));
    HD_CHECK(!lex.LoadBytes(std::vector<uint8_t>(64, 0), err));
    std::vector<uint8_t> head = {'H', 'D', '8', 'L', 'E', 'X', 0, 0, 1, 0, 0, 0, 9, 0, 0, 0};
    HD_CHECK(!lex.LoadBytes(head, err));
    HD_CHECK(!lex.Load("/nonexistent/lexicon.hdlx", err));
    HD_CHECK_EQ(hd8::lex::Hiragana("トル・カタカナ"), std::string("とる・かたかな"));
}

}  // namespace

int main() {
    int failed = 0;
    for (const auto& t : hdtest::Registry()) {
        hdtest::Cur() = {};
        t.fn();
        if (hdtest::Cur().failures) {
            ++failed;
            std::fprintf(stderr, "FAIL %s\n", t.name);
        }
    }
    std::printf("%zu tests, %d failed\n", hdtest::Registry().size(), failed);
    return failed ? 1 : 0;
}
