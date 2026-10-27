#include "core/text/kana.h"

#include "hdtest.h"

using hdTrans::text::FromKana;
using hdTrans::text::Preset;

HD_TEST(kana_caption_single_long_pair) {
    const struct { const char* reading; const char* want; } cases[] = {
        {"おおうえ", "오우에"}, {"おおうち", "오우치"}, {"こうう", "코우"},
        {"さとう", "사토"}, {"おおの", "오노"}, {"ゆうき", "유키"}, {"きょう", "쿄"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(FromKana(c.reading, Preset::FanShort, true), std::string(c.want));
    HD_CHECK_EQ(FromKana("おおうえ", Preset::Fan), "오오우에");
    HD_CHECK_EQ(FromKana("おおうえ", Preset::Fan, true), "오오우에");
    HD_CHECK_EQ(FromKana("おおうえ", Preset::FanShort), "오에");
}

HD_TEST(kana_from_kana) {
    struct Case {
        const char* reading;
        const char* want[3];  // Fan, FanShort, Official
    };
    const Case cases[] = {
        {"せいいち", {"세이이치", "세이이치", "세이치"}},
        {"りゅうせい", {"류우세이", "류세이", "류세이"}},
        {"ほうじょう", {"호우죠우", "호죠", "호조"}},
        {"なつこ", {"나츠코", "나츠코", "나쓰코"}},
        {"かのん", {"카논", "카논", "가논"}},
        {"ゆうや", {"유우야", "유야", "유야"}},
        {"かつえ", {"카츠에", "카츠에", "가쓰에"}},
        {"とうきょう", {"토우쿄우", "토쿄", "도쿄"}},
        {"おおさか", {"오오사카", "오사카", "오사카"}},
        {"さっぽろ", {"삿포로", "삿포로", "삿포로"}},
        {"かみじょう", {"카미죠우", "카미죠", "가미조"}},
        {"とうま", {"토우마", "토마", "도마"}},
        {"ピカチュウ", {"피카츄우", "피카츄", "피카추"}},
        {"ちはや", {"치하야", "치하야", "지하야"}},
        {"リン", {"린", "린", "린"}},
        {"ふみ", {"후미", "후미", "후미"}},
        {"光", {"", "", ""}},
    };
    const Preset presets[] = {Preset::Fan, Preset::FanShort, Preset::Official};
    for (const auto& c : cases) {
        for (int p = 0; p < 3; p++) HD_CHECK_EQ(FromKana(c.reading, presets[p]), std::string(c.want[p]));
    }
}
