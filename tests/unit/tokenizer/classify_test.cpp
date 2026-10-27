#include "core/tokenizer/classify.h"

#include "hdtest.h"

using namespace hdTrans::tokenizer;

HD_TEST(tokenizer_classes) {
    struct Case {
        char32_t c;
        int want;
    } cases[] = {
        {U'a', 0},   {U'ゔ', 13}, {U'ゖ', 13}, {U'ヷ', 14}, {U'ヺ', 14}, {U'ヽ', 0},   {U'ゝ', 0},  {U'㐂', 24},
        {U'𠀋', 24}, {U'丽', 24}, {U'한', 0},  {U'😀', 0},  {U'Я', 0},   {U'〇', 24}, {U'々', 24}, {U'仝', 0},
        {U'髙', 0},  {U'高', 24}, {U'ー', 14}, {U'・', 24}, {U'ぁ', 13}, {U'ァ', 14}, {U'、', 0},
    };
    for (const Case& c : cases) HD_CHECK_EQ(Class(c.c), c.want);
    HD_CHECK_EQ(ClassFlags(U'ー', 8), 24);
    HD_CHECK_EQ(ClassFlags(U'ー', 12), 14);
    HD_CHECK_EQ(ClassFlags(U'・', 8), 24);
    HD_CHECK_EQ(ClassFlags(U'々', 8), 24);
    HD_CHECK_EQ(ClassFlags(U'あ', 5), 0);
    HD_CHECK_EQ(ClassFlags(U'亜', 6), 0);
    HD_CHECK_EQ(ClassFlags(U'ア', 7), 14);

    for (const char32_t c : {U'한', U'😀', U'ї', U'ゔ', U'ゕ', U'ゖ', U'ä'}) HD_CHECK(Foreign(c));
    for (const char32_t c : {U'a', U'\x01', U'ｱ', U'ヷ', U'ヺ', U'あ', U'亜', U'丽', U'Я', U'。', U'ー', U'＃'}) {
        HD_CHECK(!Foreign(c));
    }
}

HD_TEST(tokenizer_signed_number) {
    for (const char* s : {"50%", "100$", "+1", "-5", "1~6", "3-3-6", "3.5%", "1,000$"}) HD_CHECK(SignedNumber(s));
    for (const char* s : {"10:30", "3/20", "86-", "/14", "$100", "#1", "%", "", "+", "a1%"}) HD_CHECK(!SignedNumber(s));
}
