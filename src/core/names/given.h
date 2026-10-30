#pragma once
// Given names of one kanji and a 郎 core (凶一郎, 矢三郎) that the dictionaries lack, and the
// same names written in kana (キョウイチロウ, きょういちろう).
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "core/dict/snapshot.h"

namespace hdTrans::names {

// A 郎 core of a given name: its spellings in kanji, katakana and hiragana and its reading.
struct RoCore {
    std::string_view kanji, katakana, hiragana, kana;
};

// The cores in kanji (一郎, 一朗, 二郎, 次郎, 太郎 ...), katakana with ウ or ー (イチロウ,
// イチロー) and hiragana; a hiragana core only where no verb form ends in it (いちろう, not
// the たろう of 言ったろう). The hiragana or katakana field is empty where no such spelling
// is read.
std::span<const RoCore> RoCores();

// The Korean of the given name kanji + core: the kanji as the name dictionaries read it, first in
// the given names that join it to another core (矢一郎 야이치로: 矢 야), then standing alone as a
// name (呉 쿠레, 雫 시즈쿠) unless the names that start with it read it otherwise (鷹 매 beside
// 鷹野 たかの), then at the head of those names (name_heads.tsv: 塩 しお), then in the prefixes of
// common names (鶴 つる), and only for a kanji the dictionaries leave unread its first on reading
// (kanji_on.tsv: 凶 きょう); then the core. "" when none applies. Names are written without long
// vowels (Preset::FanShort).
std::string RoGivenName(const dict::Snapshot& snap, std::string_view kanji, const RoCore& core);

namespace internal {

// The kana of kanji in a table of builtin/extra (KANJI <TAB> KANA rows, # comments): "" for
// none.
std::string_view HeadReading(char32_t kanji);
std::string_view OnReading(char32_t kanji);

}  // namespace internal

}  // namespace hdTrans::names
