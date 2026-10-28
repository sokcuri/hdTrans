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

// Whether vowel ("u", "o"), after the vowel prev and before the vowel next of a bare vowel
// mora ("" for none), is a long vowel that a Japanese name drops: ou, oo and uu, but not a
// u before e (いのうえ 이노우에).
bool NameLongVowel(std::string_view prev, std::string_view vowel, std::string_view next);

// hangul without the long vowels that NameLongVowel drops, one pair at a time (쿄우이치로우
// 쿄이치로, 이노우에 kept); oo also drops the second o of oo (오오노 오노), which dictionary rows of
// names keep (LEX-29).
std::string WithoutNameLongVowels(std::string_view hangul, bool oo);

// Whether the Korean korean, spaces left out, only spells the sound of kana as a name is written
// (Preset::Fan or FanShort; パラパラ 파라파라, フタバ 후타바), or with loanword as a loanword is
// written too (Preset::Official): such a row gives the word no meaning of its own.
bool SpellsSound(std::string_view kana, std::string_view korean, bool loanword);

// The kana reading of one word (hiragana or katakana) in Hangul under p; "" when
// reading is empty or holds anything other than kana. singleLongPair limits FanShort
// shortening to the non-overlapping pairs of NameLongVowel.
std::string FromKana(std::string_view reading, Preset p, bool singleLongPair = false);

}  // namespace hdTrans::text
