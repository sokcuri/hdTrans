#include "core/rules/match.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "fnv.h"
#include "hdtest.h"

using namespace hdTrans::rules;

namespace {

int FamilyIndex(const char* name) {
    const auto families = internal::AttributeFamilies();
    for (std::size_t i = 0; i < families.size(); i++) {
        if (std::strcmp(families[i].name, name) == 0) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

// The expected value is the hash of the rows the data was generated from, in order.
HD_TEST(rules_attribute_families_fingerprint) {
    const auto families = internal::AttributeFamilies();
    hdtest::Fnv h;
    h.U32(static_cast<uint32_t>(families.size()));
    for (const auto& f : families) {
        h.Str(f.name);
        for (const int p : f.parent) h.U32(static_cast<uint32_t>(p));
        for (const int e : f.expansion) h.U32(static_cast<uint32_t>(e));
    }
    HD_CHECK_EQ(families.size(), size_t{132});
    HD_CHECK_EQ(h.h, uint64_t{0xf5256a6ffb042ed1ull});
}

HD_TEST(rules_match_rule_surface) {
    const std::string ara = "あら";
    struct Case {
        std::string surface, pattern;
        bool want;
    };
    const Case cases[] = {
        {ara, ara, true},           {ara, "あ", false},       {ara, "Lあ", true},
        {ara, "Lら", false},        {ara, "Rら", true},       {ara, "11あ", true},
        {ara, "22ら", true},        {"あ", "00xあy", true},   {"xあy", "00あ", false},
        {ara, "99" + ara, true},    {"99" + ara, "99" + ara, false},
        {"anything", "L", true},    {"", "", true},           {"x", "", false},
        {"\xe3\x81", "Lあ", false}, {"Lあ", "Lあ", false},    {"(을)를", "(을)를", true},
    };
    for (const Case& c : cases) {
        if (MatchRuleSurface(c.surface, c.pattern) != c.want) {
            hdtest::Fail(__FILE__, __LINE__, "MatchRuleSurface(" + c.surface + ", " + c.pattern + ")");
        }
    }
}

HD_TEST(rules_match_rule_code) {
    struct Case {
        const char* code;
        const char* pattern;
        bool want;
    };
    const Case cases[] = {
        {"130", "13*", true},     {"131", "13*", true},     {"140", "13*", false},   {"401", "4**", true},
        {"301", "4**", false},    {"1", "1**", false},      {"1305", "130", true},   {"5:0", "58*5:0", true},
        {"581", "58*5:0", true},  {"570", "58*5:0", false}, {"501", "501561", true}, {"561", "501561", true},
        {"57", "57", true},       {"5", "57", false},       {"", "", false},         {"", "*", false},
    };
    for (const Case& c : cases) {
        if (MatchRuleCode(c.code, c.pattern) != c.want) {
            hdtest::Fail(__FILE__, __LINE__, std::string("MatchRuleCode(") + c.code + ", " + c.pattern + ")");
        }
    }
}

HD_TEST(rules_has_code) {
    HD_CHECK(HasCode("QCT", "UDrQCT"));
    HD_CHECK(!HasCode("QCT", "UDrQCx"));
    HD_CHECK(HasCode("Q**", "UDrQxy"));
    HD_CHECK(!HasCode("QCT", "xQCTyy"));
    HD_CHECK(!HasCode("QCT", "QC"));
    HD_CHECK(!HasCode("QC", "QCT"));
}

// QCT has the parent VPl; VHn expands to QNP and QPN; UAq has the parents UQf (which
// expands to QID) and VHn.
HD_TEST(rules_match_attribute_family) {
    struct Case {
        const char* family;
        const char* values;
        const char* pattern;
        bool want;
    };
    const Case cases[] = {
        {"QCT", "UDrQCT", "QCT", true}, {"QCT", "VPl", "QCT", true}, {"QCT", "QNP", "QCT", false},
        {"VHn", "QPN", "VHn", true},    {"VHn", "VHx", "VHn", false}, {"VHn", "VHx", "VH*", true},
        {"UAq", "QID", "UAq", true},    {"UAq", "QNP", "UAq", true}, {"UAq", "QCT", "UAq", false},
    };
    for (const Case& c : cases) {
        const int f = FamilyIndex(c.family);
        HD_REQUIRE(f >= 0);
        if (MatchAttributeFamily(f, c.values, c.pattern) != c.want) {
            hdtest::Fail(__FILE__, __LINE__, std::string("MatchAttributeFamily(") + c.family + ", " + c.values + ")");
        }
    }
}

HD_TEST(rules_attribute_operators) {
    struct Case {
        bool (*fn)(std::string_view, std::string_view);
        const char* values;
        const char* pattern;
        bool want;
    };
    const Case cases[] = {
        {AttrChunksIn, "UQf", "UAqUQF", false}, {AttrChunksIn, "UQf", "UAqUQf", true},
        {AttrChunksIn, "UAq", "UAqUQF", true},  {AttrChunksIn, "UAq", "UAqUQf", true},
        {AttrChunksIn, "QCT", "UAqUQf", false}, {AttrChunksIn, "UQF", "UAqUQf", false},
        {AttrChunksIn, "UDrQPN", "VHnQPN", true}, {AttrChunksIn, "UDrQPN", "VHnQCT", false},
        {AttrChunksIn, "UDrQPN", "rQP", true},    {MinAttrValues, "UDrQPN", "VHn", true},
        {MinAttrValues, "UDrQPN", "QCT", false},  {MinAttrValues, "VPl", "QCT", true},
        {MinAttrValues, "UAq", "UA*", true},      {MinAttrValues, "UAq", "XXX", false},
        {MinAttrValues, "XXX", "X**", false},     {MinAttrValues, "UAq", "UXq", true},
        {MinGobiAttrValues, "UAq", "UXq", false}, {MinGobiAttrValues, "UAq", "UAq", true},
        {GobiAttrValues, "QPN", "VHn", true},     {GobiAttrValues, "UDr", "VHn", false},
        {MinGAttrValues, "PnZ7", "Z7Ow", true},   {MinGAttrValues, "Pn", "Z7", false},
        {MinGAttrValues, "Z7", "NdZ7", true},     {MinGAttrValues, "", "Z7", false},
    };
    int n = 0;
    for (const Case& c : cases) {
        if (c.fn(c.values, c.pattern) != c.want) {
            hdtest::Fail(__FILE__, __LINE__, "case " + std::to_string(n) + ": " + c.values + " " + c.pattern);
        }
        n++;
    }
}
