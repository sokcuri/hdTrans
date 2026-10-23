#include "core/output/inflect.h"

#include <cstdint>
#include <array>
#include <bit>
#include <set>

#include "core/output/markers.h"
#include "hdtest.h"

using namespace hdTrans::output::internal;

HD_TEST(inflect_classes_and_flags) {
    using Class = InflectionClass;
    using Pos = KoreanPartOfSpeech;
    struct Case {
        const char* stem;
        Class cls;
        uint16_t flags;
        Pos pos;
        char marker;
        char code;
        const char* expected;
    };
    const Case cases[] = {
        {"먹", Class::Regular, 0, Pos::Verb, '&', '0', "먹는"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '2', "먹으"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '3', "먹을"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '4', "먹는"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '5', "먹었"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '6', "먹어"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '7', "먹습"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '8', "먹은"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '9', "먹읍"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', 'B', "먹음"},
        {"가", Class::Regular, 0, Pos::Verb, '&', '4', "간"},
        {"가", Class::Regular, 0, Pos::Verb, '&', '5', "갔"},
        {"가", Class::Regular, 0, Pos::Verb, '&', '6', "가"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '0', "사는"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '2', "살"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '3', "살"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '7', "삽"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '8', "산"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', 'A', "사"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', 'B', "삶"},
        {"긋", Class::Siot, 0, Pos::Verb, '&', '2', "그으"},
        {"긋", Class::Siot, 0, Pos::Verb, '&', '6', "그어"},
        {"노랗", Class::Hieuh, 0, Pos::Adjective, '$', '4', "노란"},
        {"노랗", Class::Hieuh, 0, Pos::Adjective, '$', '5', "노랬"},
        {"파랗", Class::Hieuh, 0, Pos::Adjective, '$', '6', "파래"},
        {"하얗", Class::Hieuh, 0, Pos::Adjective, '$', '6', "하얘"},
        {"푸", Class::U, 0, Pos::Verb, '&', '6', "퍼"},
        {"쓰", Class::Eu, 0, Pos::Verb, '&', '6', "써"},
        {"아프", Class::Eu, 0, Pos::Adjective, '$', '6', "아파"},
        {"프", Class::Eu, kBrightEu, Pos::Adjective, '$', '6', "파"},
        {"듣", Class::Digeut, 0, Pos::Verb, '&', '2', "들으"},
        {"듣", Class::Digeut, 0, Pos::Verb, '&', '6', "들어"},
        {"줍", Class::Bieup, 0, Pos::Verb, '&', '2', "주우"},
        {"줍", Class::Bieup, 0, Pos::Verb, '&', '3', "주울"},
        {"줍", Class::Bieup, 0, Pos::Verb, '&', '6', "주워"},
        {"돕", Class::Bieup, kBieupWa, Pos::Verb, '&', '6', "도와"},
        {"하", Class::Yeo, 0, Pos::Verb, '&', '5', "했"},
        {"하", Class::Yeo, 0, Pos::Verb, '&', '6', "해"},
        {"푸르", Class::Reo, 0, Pos::Adjective, '$', '6', "푸르러"},
        {"모르", Class::Reu, 0, Pos::Verb, '&', '6', "몰라"},
        {"부르", Class::Reu, 0, Pos::Verb, '&', '6', "불러"},
        {"있", Class::Existential, 0, Pos::Adjective, '$', '4', "있는"},
        {"없", Class::Existential, 0, Pos::Adjective, '%', '4', "없는"},
        {"이", Class::Copula, 0, Pos::Adjective, '&', '0', "인"},
        {"이", Class::Copula, 0, Pos::Adjective, '&', '5', "(이었)였"},
        {"이", Class::Copula, 0, Pos::Adjective, '&', '6', "(이어)여"},
        {"보", Class::Regular, 0, Pos::Verb, '&', '6', "봐"},
        {"보", Class::Regular, kNoContraction, Pos::Verb, '&', '6', "보아"},
        {"주", Class::Regular, 0, Pos::Verb, '&', '6', "줘"},
        {"주", Class::Regular, kNoContraction, Pos::Verb, '&', '6', "주어"},
        {"지", Class::Regular, 0, Pos::Verb, '&', '6', "져"},
        {"이", Class::Regular, 0, Pos::Verb, '&', '6', "이어"},
        {"이", Class::Regular, kContractIeung, Pos::Verb, '&', '6', "여"},
        {"깨", Class::Regular, 0, Pos::Verb, '&', '6', "깨어"},
        {"깨", Class::Regular, kDropAe, Pos::Verb, '&', '6', "깨"},
        {"세", Class::Regular, 0, Pos::Verb, '&', '6', "세어"},
        {"세", Class::Regular, kDropE, Pos::Verb, '&', '6', "세"},
        {"되", Class::Regular, 0, Pos::Verb, '&', '6', "되어"},
        {"뛰", Class::Regular, 0, Pos::Verb, '&', '6', "뛰어"},
        {"먹", Class::Regular, kFormalHortative, Pos::Verb, '&', '9', "먹습"},
        {"멀", Class::Rieul, kPastHortative, Pos::Adjective, '&', '9', "멀었습"},
        {"있", Class::Regular, kBareDeclarative, Pos::Verb, '&', '4', "있"},
        {"멀", Class::Rieul, 0, Pos::Adjective, '&', '0', "먼"},
        {"멀", Class::Rieul, 0, Pos::Adjective, '&', '4', "멀"},
        {"않", Class::Regular, 0, Pos::Verb, '$', '4', "않는"},
        {"살", Class::Rieul, 0, Pos::Verb, '%', '4', "사는"},
        {"높", Class::Regular, 0, Pos::Adjective, '$', '4', "높은"},
        {"높", Class::Regular, 0, Pos::Adjective, '$', '8', "높음"},
        {"가", Class::Regular, 0, Pos::Verb, '&', 'C', ""},
        {"가", Class::Regular, 0, Pos::Verb, '$', '0', ""},
        {"가", Class::Regular, 0, Pos::Verb, '%', '9', ""},
        {"가", Class::Regular, 0, Pos::Verb, '?', '1', ""},
        {"", Class::Regular, 0, Pos::Verb, '&', '1', ""},
        {"A", Class::Regular, 0, Pos::Verb, '&', '1', ""},
        {"가\xed", Class::Regular, 0, Pos::Verb, '&', '1', ""},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(InflectStem(c.stem, c.pos, c.cls, c.flags, c.marker, c.code), std::string(c.expected));
    }
}

HD_TEST(inflect_membership_and_exceptions) {
    for (const auto& c : {std::pair{"&LT@", '5'}, {"&CJ@", 'C'}, {"$DH@", '9'}, {"$J6H", '8'}}) {
        const auto* family = FindKoreanFamily(c.first);
        HD_REQUIRE(family != nullptr);
        HD_CHECK(!Inflect(*family, c.second).empty());
    }
    const auto* missingNominal = FindKoreanFamily("&LA@");
    HD_REQUIRE(missingNominal != nullptr);
    HD_CHECK(Inflect(*missingNominal, 'B').empty());
    HD_CHECK(!InflectStem(missingNominal->stem, missingNominal->partOfSpeech, missingNominal->inflectionClass,
                         missingNominal->flags, '&', 'B').empty());
    HD_CHECK(FindKoreanFamily("&zz@") == nullptr);
    HD_CHECK(FindKoreanFamily("&A1@1") == nullptr);
    HD_CHECK_EQ(InflectionFormBit('C'), uint16_t{4096});
    HD_CHECK_EQ(InflectionFormBit('D'), uint16_t{0});
    HD_CHECK_EQ(InflectionFormBit('\0'), uint16_t{0});
}

namespace {

using hdTrans::output::DollarMarkerRow;
using hdTrans::output::PercentMarkerRow;
using hdTrans::output::SourceMarkerRow;

std::string_view MarkerRowFor(std::string_view key) {
    switch (key[0]) {
        case '&': return SourceMarkerRow(key);
        case '$': return DollarMarkerRow(key);
        case '%': return PercentMarkerRow(key);
    }
    return {};
}

void CheckRow(std::string_view key, std::string_view actual, std::string_view expected) {
    if (actual != expected) {
        hdtest::Fail(__FILE__, __LINE__, std::string(key) + ": " + hdtest::Show(std::string(actual)) +
                                       " != " + hdtest::Show(std::string(expected)));
    }
}

}  // namespace

HD_TEST(inflect_grill_polite_slot_and_hardening_keeps) {
    const auto* grill = FindKoreanFamily("&J2@");
    HD_REQUIRE(grill != nullptr);
    const struct { char code; const char* suffix; const char* expected; } cases[] = {
        {'7', "니다", "굽습니다"},
        {'7', "니까", "굽습니까"},
        {'7', "니다만", "굽습니다만"},
        {'1', "다", "굽다"},
        {'5', "다", "구웠다"},
        {'6', "", "구워"},
        {'1', "고", "굽고"},
        {'1', "지", "굽지"},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(Inflect(*grill, c.code) + c.suffix, std::string(c.expected));
    }
    HD_CHECK_EQ(std::string(SourceMarkerRow("&J2@7")), std::string("굽습"));

    const auto* hard = FindKoreanFamily("$F2@");
    HD_REQUIRE(hard != nullptr);
    HD_CHECK_EQ(Inflect(*hard, '1') + "다", std::string("굳다"));
    HD_CHECK_EQ(Inflect(*hard, '7') + "니다", std::string("굳습니다"));
    const auto* other = FindKoreanFamily("&CS@");
    HD_REQUIRE(other != nullptr);
    HD_CHECK_EQ(Inflect(*other, '7'), std::string("굽"));
}

HD_TEST(inflect_all_marker_rows_consistency) {
    const std::array tables = {SourceMarkerTable(), DollarMarkerTable(), PercentMarkerTable()};
    std::set<std::string_view> keys;
    size_t rowCount = 0;
    for (size_t table = 0; table < tables.size(); ++table) {
        for (const auto& row : tables[table]) {
            const std::string_view key = row.key;
            HD_REQUIRE(key.size() == 5);
            const auto* family = FindKoreanFamily(key.substr(0, 4));
            HD_REQUIRE(family != nullptr);
            keys.insert(family->key);
            CheckRow(key, Inflect(*family, key[4]), row.text);
            CheckRow(key, MarkerRowFor(key), row.text);
            ++rowCount;
        }
    }
    HD_CHECK_EQ(keys.size(), KoreanFamilies().size());

    size_t exceptions = 0, exceptionFamilies = 0, formCount = 0;
    for (const auto& family : KoreanFamilies()) {
        HD_CHECK(keys.contains(family.key));
        formCount += static_cast<size_t>(std::popcount(family.forms));
        if (!family.exceptions.empty()) ++exceptionFamilies;
        std::set<char> codes;
        for (const auto& exception : family.exceptions) {
            HD_CHECK(codes.insert(exception.code).second);
            HD_CHECK((family.forms & InflectionFormBit(exception.code)) != 0);
            HD_CHECK(!exception.surface.empty());
            const auto computed = InflectStem(family.stem, family.partOfSpeech, family.inflectionClass,
                                              family.flags, family.key[0], exception.code);
            HD_CHECK(computed != exception.surface);
            ++exceptions;
        }
    }
    HD_CHECK_EQ(formCount, rowCount);
    std::printf("inflect: %zu family keys, %zu row keys, %zu necessary exception rows in %zu families\n",
                keys.size(), rowCount, exceptions, exceptionFamilies);
}

HD_TEST(inflect_all_form_membership_consistency) {
    for (const auto& family : KoreanFamilies()) {
        for (int byte = 0; byte < 256; ++byte) {
            const char code = static_cast<char>(byte);
            const std::string key = std::string(family.key) + code;
            const auto row = Inflect(family, code);
            CheckRow(key, row, MarkerRowFor(key));
            HD_CHECK_EQ(!row.empty(), (family.forms & InflectionFormBit(code)) != 0);
            if (family.key[0] != '&') HD_CHECK(SourceMarkerRow(key).empty());
            if (family.key[0] != '$') HD_CHECK(DollarMarkerRow(key).empty());
            if (family.key[0] != '%') HD_CHECK(PercentMarkerRow(key).empty());
        }
    }
}

HD_TEST(inflect_frozen_rows) {
    const struct { const char* key; const char* surface; } cases[] = {
        {"&A1@0", "가는"},
        {"&A1@5", "갔"},
        {"&A1@6", "가"},
        {"&CJ@C", "줘"},
        {"&LA@A", "어"},
        {"&LA@B", ""},
        {"&LT@0", "빠른"},
        {"&LT@4", "빠르"},
        {"&LT@5", "높았"},
        {"&LT@B", "높음"},
        {"&JB@0", "(듣)묻는"},
        {"&JB@6", "(들)물어"},
        {"&Mi@5", "덜"},
        {"&Mi@6", "덜"},
        {"&Mj@5", "뻤"},
        {"&Mj@6", "뻐"},
        {"&J8@5", "뵈었"},
        {"&J8@6", "뵈어"},
        {"$A5@6", "뻐"},
        {"$A9@6", "커"},
        {"$AA@6", "파"},
        {"$AB@6", "해"},
        {"$AH@5", "(이었)였"},
        {"$AH@6", "(이어)여"},
        {"$C1@6", "빨라"},
        {"$C2@6", "푸르러"},
        {"$C3@5", "노랐"},
        {"$C3@6", "노래"},
        {"$FA@4", "없는"},
        {"$FB@4", "있는"},
        {"$G1@6", "까워"},
        {"$DH@9", "않은"},
        {"$J6@8", ""},
        {"$J6H8", "금"},
        {"$Ky@6", "늦어"},
        {"%EH@1", "(이)"},
        {"%EH@6", "(이어)여"},
        {"%DK@5", "여웠"},
        {"%DK@6", "여워"},
        {"%C5@6", "래"},
    };
    for (const auto& c : cases) {
        const auto* family = FindKoreanFamily(std::string_view(c.key).substr(0, 4));
        HD_REQUIRE(family != nullptr);
        CheckRow(c.key, Inflect(*family, c.key[4]), c.surface);
        CheckRow(c.key, MarkerRowFor(c.key), c.surface);
    }
}

// The 으 stem of the past-state family 했 keeps 으 (결혼했으니까).
HD_TEST(inflect_past_state_eu_stem) {
    const auto* family = FindKoreanFamily("&NC@");
    HD_REQUIRE(family != nullptr);
    HD_CHECK_EQ(Inflect(*family, 'A') + "니까", std::string("했으니까"));
    HD_CHECK_EQ(Inflect(*family, '5') + "다", std::string("했다"));
}
