#pragma once
// Kana readings of names written in Hangul.
#include <string>
#include <string_view>

namespace hdTrans::text {

// A kana-to-Hangul convention for names.
enum class Preset {
    // カ/タ/キャ/チャ aspirated in every position, ツ as 츠, ジャ/チャ as 쟈/챠, long
    // vowels as written (유우야, 토우마).
    Fan,
    // Fan without the long vowels ou, oo and uu (류구, 호죠); ei and ii stay (케이이치).
    FanShort,
    // 외래어 표기법: 가/다/갸/자 word-initially, 카/타/캬/차 elsewhere, ツ as 쓰, ジョ as 조,
    // チョ as 초, long vowels not written (도쿄, 오사카).
    Official,
};

// The kana reading of one word (hiragana or katakana) in Hangul under p; "" when
// reading is empty or holds anything other than kana. singleLongPair limits FanShort
// shortening to non-overlapping vowel pairs.
std::string FromKana(std::string_view reading, Preset p, bool singleLongPair = false);

}  // namespace hdTrans::text
