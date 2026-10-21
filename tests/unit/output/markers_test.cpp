#include "core/output/markers.h"

#include <cstdint>
#include <string>

#include "fnv.h"
#include "core/output/families.h"
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
    HD_CHECK_EQ(Fingerprint(internal::SourceMarkerTable(), SourceMarkerRow), uint64_t{0x81769d7ccebd5b19ull});
    HD_CHECK_EQ(Fingerprint(internal::DollarMarkerTable(), DollarMarkerRow), uint64_t{0xaee70156b09dc12bull});
    HD_CHECK_EQ(Fingerprint(internal::PercentMarkerTable(), PercentMarkerRow), uint64_t{0xa526286b32a4c467ull});
    HD_CHECK_EQ(internal::SourceMarkerTable().size(), size_t{3708});
    HD_CHECK_EQ(internal::DollarMarkerTable().size(), size_t{1145});
    HD_CHECK_EQ(internal::PercentMarkerTable().size(), size_t{824});
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
