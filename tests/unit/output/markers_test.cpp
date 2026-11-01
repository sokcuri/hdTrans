#include "core/output/markers.h"

#include <cstdint>
#include <string>

#include "fnv.h"
#include "core/output/families.h"
#include "core/output/korean.h"
#include "hdtest.h"

using namespace hdTrans::output;

namespace {

// Hash of the (key, row) pairs in key order, each read back through lookup.
uint64_t Fingerprint(std::span<const internal::MarkerRow> table, std::string_view (*lookup)(std::string_view)) {
    hdtest::Fnv h;
    h.U32(static_cast<uint32_t>(table.size()));
    for (const auto& r : table) {
        h.Str(r.key);
        h.Str(lookup(r.key));
    }
    return h.h;
}

uint64_t StrHash(std::string_view s) {
    hdtest::Fnv h;
    h.Str(s);
    return h.h;
}

}  // namespace

// The expected values are the hashes of the key -> row mapping each table resolves to
// for every key it holds; keys outside the tables have no row.
HD_TEST(markers_fingerprints) {
    HD_CHECK_EQ(Fingerprint(internal::SourceMarkerTable(), SourceMarkerRow), uint64_t{0xb51c53f9de3d1df0ull});
    HD_CHECK_EQ(Fingerprint(internal::DollarMarkerTable(), DollarMarkerRow), uint64_t{0xe4a5c6472975a47bull});
    HD_CHECK_EQ(Fingerprint(internal::PercentMarkerTable(), PercentMarkerRow), uint64_t{0xd51bf5c32209ed5cull});
    HD_CHECK_EQ(internal::SourceMarkerTable().size(), size_t{3720});
    HD_CHECK_EQ(internal::DollarMarkerTable().size(), size_t{1145});
    HD_CHECK_EQ(internal::PercentMarkerTable().size(), size_t{832});
    HD_CHECK_EQ(StrHash(MarkerFamilyLetters('&')), uint64_t{0x95c8e04668d0d0daull});
    HD_CHECK_EQ(StrHash(MarkerFamilyLetters('$')), uint64_t{0xd14c287b7b46a4f7ull});
    HD_CHECK_EQ(StrHash(MarkerFamilyLetters('%')), uint64_t{0xb9914f088b74446bull});
    HD_CHECK(MarkerFamilyLetters('#').empty());
}

HD_TEST(markers_tables_sorted) {
    for (const auto t : {internal::SourceMarkerTable(), internal::DollarMarkerTable(), internal::PercentMarkerTable()}) {
        for (size_t i = 1; i < t.size(); i++) HD_CHECK(std::string_view(t[i - 1].key) < std::string_view(t[i].key));
        for (const auto& r : t) HD_CHECK(*r.text != '\0');
    }
}

HD_TEST(markers_family_letters) {
    // The letters are exactly the second characters of the keys.
    struct Case {
        char marker;
        std::span<const internal::MarkerRow> table;
    };
    const Case cases[] = {{'&', internal::SourceMarkerTable()},
                          {'$', internal::DollarMarkerTable()},
                          {'%', internal::PercentMarkerTable()}};
    for (const auto& c : cases) {
        bool used[256] = {};
        for (const auto& r : c.table) used[static_cast<unsigned char>(r.key[1])] = true;
        std::string letters;
        for (int b = 0; b < 256; b++) {
            if (used[b]) letters += static_cast<char>(b);
        }
        HD_CHECK_EQ(letters, std::string(MarkerFamilyLetters(c.marker)));
    }
}

HD_TEST(markers_samples) {
    HD_CHECK_EQ(std::string(SourceMarkerRow("&A1@0")), std::string("가는"));
    HD_CHECK_EQ(std::string(SourceMarkerRow("&LA@A")), std::string("어"));
    HD_CHECK_EQ(std::string(SourceMarkerRow("&LT@0")), std::string("빠른"));
    HD_CHECK_EQ(std::string(SourceMarkerRow("&LT@4")), std::string("빠르"));
    HD_CHECK_EQ(std::string(SourceMarkerRow("&LT@5")), std::string("높았"));
    HD_CHECK_EQ(std::string(SourceMarkerRow("&LT@B")), std::string("높음"));
    HD_CHECK_EQ(std::string(PercentMarkerRow("%A1@1")), std::string("하"));
    HD_CHECK(SourceMarkerRow("%A1@1").empty());
    HD_CHECK(SourceMarkerRow("&A1@").empty());
    HD_CHECK(SourceMarkerRow("").empty());
    HD_CHECK(DollarMarkerRow("$zz@1").empty());
}

HD_TEST(markers_literal_keys_and_views) {
    const auto held = SourceMarkerRow("&A1@1");
    const auto* heldData = held.data();
    const auto* heldTable = internal::SourceMarkerTable().data();
    HD_CHECK_EQ(std::string(SourceMarkerRow("&CJ@C")), std::string("줘"));
    HD_CHECK_EQ(std::string(DollarMarkerRow("$DH@9")), std::string("않은"));
    HD_CHECK_EQ(std::string(DollarMarkerRow("$J6H8")), std::string("금"));
    HD_CHECK(DollarMarkerRow("$J6@8").empty());
    HD_CHECK(SourceMarkerRow("&LA@B").empty());
    HD_CHECK(SourceMarkerRow("&a1@1").empty());
    HD_CHECK(SourceMarkerRow("&A1@1x").empty());
    HD_CHECK(SourceMarkerRow(std::string_view("&A1@1\0", 6)).empty());
    HD_CHECK(DollarMarkerRow("$J6H8x").empty());
    HD_CHECK(PercentMarkerRow("%A1@1x").empty());
    for (const auto table : {internal::SourceMarkerTable(), internal::DollarMarkerTable(),
                             internal::PercentMarkerTable()}) {
        for (const auto& row : table) HD_CHECK(std::string_view(row.text).size() > 0);
    }
    HD_CHECK_EQ(std::string(held), std::string("가"));
    HD_CHECK(heldData == SourceMarkerRow("&A1@1").data());
    HD_CHECK(heldTable == internal::SourceMarkerTable().data());
}

HD_TEST(markers_generated_fallback_and_precedence) {
    Families generated;
    FamilyRows source{};
    source[kFormCodes.find('1')] = "다름";
    source[kFormCodes.find('A')] = "새";
    source[kFormCodes.find('B')] = "엄";
    HD_REQUIRE(generated.Insert('&', "&LA@", source));
    HD_CHECK_EQ(std::string(LookupSource("&LA@1", generated)), std::string("얼"));
    HD_CHECK_EQ(std::string(LookupSource("&LA@A", generated)), std::string("어"));
    HD_CHECK_EQ(std::string(LookupSource("&LA@B", generated)), std::string("엄"));
    FamilyRows dollar{};
    dollar[kFormCodes.find('8')] = "금";
    HD_REQUIRE(generated.Insert('$', "$J6@", dollar));
    HD_CHECK_EQ(std::string(LookupDollar("$J6@8", generated)), std::string("금"));
    HD_CHECK_EQ(std::string(LookupSource("$J6@8", generated)), std::string("금"));
    HD_CHECK_EQ(std::string(LookupPercent("$J6@8", generated)), std::string("금"));
    HD_CHECK_EQ(std::string(DollarMarkerRow("$J6H8")), std::string("금"));
    HD_CHECK(DollarMarkerRow("$J6@8").empty());
    HD_CHECK(SourceMarkerRow("&LA@B").empty());
}

HD_TEST(markers_dictionary_forms) {
    const Families none;
    const std::uint64_t before = UnexpandedMarkers();
    HD_CHECK_EQ(DictionaryForms("천하다 웃음", none), std::string("천하다 웃음"));
    HD_CHECK_EQ(UnexpandedMarkers(), before);
    HD_CHECK_EQ(DictionaryForms("천&AV@", none), std::string("천하다"));
    HD_CHECK_EQ(DictionaryForms("천&AV@웃음", none), std::string("천하다웃음"));
    HD_CHECK_EQ(DictionaryForms("좋아주었으면 &AV@ 있고", none), std::string("좋아주었으면 하다 있고"));
    HD_CHECK_EQ(DictionaryForms("$D3@ 새%CC@", none), std::string("높다 새하얗다"));
    HD_CHECK_EQ(DictionaryForms("올이 $FG@", none), std::string("올이 "));  // no such family
    HD_CHECK_EQ(DictionaryForms("a@b &a@ $1@ %20@x", none), std::string("a@b &a@ $1@ x"));
    HD_CHECK_EQ(DictionaryForms("@", none), std::string("@"));
    HD_CHECK_EQ(DictionaryForms("", none), std::string());
    HD_CHECK(UnexpandedMarkers() >= before + 6);
}

// StaticConjugating gives the target Conjugating gives with a static family, and an error where
// Conjugating makes a family.
HD_TEST(markers_static_conjugating) {
    for (const std::string_view lemma : {"버리다", "잊다", "들다", "싸우다"}) {
        Families generated;
        const Conjugated made = Conjugating(lemma, '&', "", generated);
        const Conjugated fixed = StaticConjugating(lemma, '&', "");
        HD_CHECK_EQ(fixed.error, std::string());
        HD_CHECK_EQ(fixed.target, made.target);
        HD_CHECK_EQ(generated.Size(), size_t{0});
    }
    Families generated;
    HD_CHECK(Conjugating("일컫다", '&', "", generated).error.empty());
    HD_CHECK(generated.Size() > 0);
    HD_CHECK(!StaticConjugating("일컫다", '&', "").error.empty());
}
