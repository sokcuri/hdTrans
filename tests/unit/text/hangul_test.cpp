#include <cstdint>
#include "core/text/hangul.h"

#include "hdtest.h"

using namespace hdTrans::text;

HD_TEST(hangul_syllables) {
    struct Case {
        char32_t r;
        int initial, vowel, final;
    };
    const Case cases[] = {
        {U'가', 0, 0, kFinalNone}, {U'간', 0, 0, kFinalNieun}, {U'물', 6, 13, kFinalRieul},
        {U'함', kInitialHieuh, 0, kFinalMieum}, {U'힣', 18, 20, 27},
    };
    for (const auto& c : cases) {
        const auto s = Split(c.r);
        HD_REQUIRE(s.has_value());
        HD_CHECK_EQ(s->initial, c.initial);
        HD_CHECK_EQ(s->vowel, c.vowel);
        HD_CHECK_EQ(s->final, c.final);
        HD_CHECK_EQ(static_cast<uint32_t>(Join(s->initial, s->vowel, s->final)), static_cast<uint32_t>(c.r));
    }
    for (char32_t r = kBase; r <= kLast; r++) {
        const auto s = Split(r);
        HD_REQUIRE(s.has_value());
        HD_REQUIRE(Join(s->initial, s->vowel, s->final) == r);
        HD_REQUIRE(Final(r) == s->final);
        HD_REQUIRE(SetFinal(SetFinal(r, 0), s->final) == r);
    }
    HD_CHECK(!Split(kBase - 1).has_value());
    HD_CHECK(!Split(kLast + 1).has_value());
    HD_CHECK_EQ(Final(U'A'), -1);
    HD_CHECK(SetFinal(U'A', 4) == U'A');
    HD_CHECK(SetFinal(U'가', kFinalNieun) == U'간');
}

HD_TEST(hangul_final_class) {
    struct Case {
        char32_t r;
        int cls;
    };
    const Case cases[] = {{U'가', 0}, {U'갈', 1}, {U'각', 2}, {U'똠', 2}, {U'뷁', 2}, {U'」', 2}, {U'A', 2}, {U'😀', 2}};
    for (const auto& c : cases) HD_CHECK_EQ(FinalClass(c.r), c.cls);
}

HD_TEST(hangul_same_sound) {
    struct Case {
        const char* a;
        const char* b;
        bool same;
    };
    const Case cases[] = {
        {"가이토", "카이토", true}, {"쓰바사", "츠바사", true}, {"유키", "유우키", true}, {"도쿄", "토우쿄우", true},
        {"호조", "호죠", true}, {"게이코", "케이코", true}, {"솜이", "타쿠미", false}, {"엄마", "마마", false}, {"일", "이루", false},
    };
    for (const auto& c : cases) {
        if (SameSound(c.a, c.b) != c.same) hdtest::Fail(__FILE__, __LINE__, std::string("SameSound(") + c.a + ", " + c.b + ")");
    }
}

HD_TEST(hangul_same_sound_vowel_pairs) {
    const struct { const char* a; const char* b; bool same; } cases[] = {
        {"져", "저", true}, {"쳐", "처", true}, {"쪄", "저", true},
        {"져", "자", false}, {"쳐", "차", false},
        {"쟈", "자", true}, {"챠", "차", true}, {"죠", "조", true}, {"쵸", "초", true},
        {"쥬", "주", true}, {"츄", "추", true},
        {"와", "오", false}, {"와", "와아", false}, {"겨", "거", false},
        {"김", "기", false}, {"저", "처", true}, {"져!", "저!", true},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(SameSound(c.a, c.b), c.same);
        HD_CHECK_EQ(SameSound(c.b, c.a), c.same);
    }
}

HD_TEST(hangul_all_syllables) {
    HD_CHECK(AllSyllables("쿄이치로"));
    HD_CHECK(!AllSyllables("쿄 이치로"));
    HD_CHECK(!AllSyllables("쿄 이치로"));
    HD_CHECK(!AllSyllables("ㄱ"));
    HD_CHECK(!AllSyllables(""));
}
