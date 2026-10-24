// Word lists of the spoken-form normalizer (spoken_forms.cpp), kept by hand. Every
// stem kKataStems writes is a stem of the dictionary (normalize_test checks it); one
// whose hiragana would split other words (ださ of 出さない) stays in katakana.
#pragma once
#include <string_view>

namespace hdTrans::pipeline::internal {

struct KataStem {
    std::string_view kata;
    std::string_view hira;
};

inline constexpr KataStem kKataStems[] = {
    {"イラつ", "いらつ"},
    {"ショボ", "しょぼ"},
    {"ウルサ", "うるさ"},
    {"ウザ", "うざ"},
    {"ダサ", "ダサ"},
    {"エグ", "えぐ"},
    {"ダル", "だる"},
    {"ヒド", "ひど"},
    {"コワ", "こわ"},
    {"ウマ", "うま"},
    {"マズ", "まず"},
    {"セコ", "せこ"},
    {"ムズ", "むず"},
    {"ツラ", "つら"},
    {"エラ", "えら"},
};

inline constexpr std::u32string_view kKataStemStarts = U"イショウダエヒコマセムツ";

inline constexpr std::string_view kKataAdjectiveEnding = "イ";

inline constexpr std::string_view kKataEndings[] = {
    "い",
    "イ",
    "く",
    "かっ",
    "かろ",
    "け",
    "さ",
    "すぎ",
    "そう",
    "む",
    "み",
    "ま",
    "め",
    "ん",
};

inline constexpr std::string_view kJaneQuestionAfter[] = {
    "の",
    "か",
    "よ",
    "だろ",
    "って",
};

inline constexpr std::string_view kParticleNeeAfter[] = {
    "だろ",
    "でしょ",
    "よ",
    "って",
    "し",
};

inline constexpr std::string_view kNotAppealStems[] = {
    "嫌い",
    "きらい",
    "きれい",
    "みたい",
    "くらい",
    "ぐらい",
    "せい",
    "てる",
    "でる",
};

inline constexpr std::string_view kClippedAdjectives[] = {
    "怖",
    "寒",
    "痛",
    "熱",
    "暑",
    "重",
    "早",
    "速",
    "高",
    "狭",
    "臭",
    "硬",
    "固",
    "甘",
    "眩し",
    "遅",
    "深",
    "長",
    "短",
    "近",
    "遠",
    "細",
    "太",
    "汚",
    "酷",
    "小さ",
    "美味し",
    "旨",
    "凄",
    "弱",
    "強",
    "安",
    "暗",
    "広",
    "軽",
    "薄",
    "濃",
    "冷た",
    "すご",
    "うざ",
    "きも",
    "ださ",
    "えぐ",
    "ひど",
    "でか",
    "かわい",
    "しょぼ",
    "さむ",
    "こわ",
};

inline constexpr std::string_view kNegativeNStems[] = {
    "許さ",
    "わから",
    "しら",
    "かまわ",
};

inline constexpr std::string_view kNegativeEStems[] = {
    "でき",
    "得",
    "足り",
    "られ",
    "させ",
    "言え",
    "見え",
    "聞こえ",
};

inline constexpr std::string_view kNegativeNeeKa[] = {
    "動",
    "書",
    "聞",
    "行",
    "効",
    "届",
    "働",
    "開",
    "抜",
    "泣",
    "驚",
    "歩",
    "気付",
    "敵わ",
};

inline constexpr std::string_view kNegativeNeeTa[] = {
    "待",
    "持",
    "立",
    "勝",
    "打",
    "育",
    "保",
};

inline constexpr std::string_view kNegativeNeeStems[] = {
    "死な",
    "かまわ",
    "構わ",
    "わから",
    "わかん",
    "分かん",
    "しら",
    "いら",
    "たまら",
    "たまん",
    "つまら",
    "つまん",
    "くだら",
    "くだん",
    "でき",
    "出来",
    "すま",
    "仕方",
    "しかた",
    "しょうが",
    "しょーが",
    "ありえ",
    "あり得",
    "見え",
    "聞こえ",
};

inline constexpr std::string_view kNegativeNeeKu[] = {
    "面白",
    "悪",
    "痛",
    "怖",
    "寒",
    "熱",
    "暑",
    "強",
    "弱",
    "早",
    "遅",
    "高",
    "安",
    "重",
    "軽",
    "長",
    "短",
    "近",
    "遠",
    "多",
    "少",
    "狭",
    "広",
    "硬",
    "甘",
    "辛",
    "うま",
    "すご",
    "かわい",
    "おもしろ",
};

inline constexpr std::string_view kKansaiNegativeRa[] = {
    "知",
    "入",
    "帰",
    "要",
    "走",
    "乗",
    "取",
    "作",
    "守",
    "足",
    "分か",
    "変わ",
};

inline constexpr std::string_view kKansaiNegativeStems[] = {
    "あら",
    "あれ",
    "せえ",
    "でき",
    "出来",
    "わから",
    "しれ",
    "知れ",
    "おら",
    "いら",
};

inline constexpr std::string_view kToruEndings[] = {
    "る",
    "った",
    "らん",
    "れ",
};

inline constexpr std::string_view kExclamationNaStems[] = {
    "気もする",
    "そうなる",
    "ことになる",
    "事になる",
    "なかなかやる",
    "違う",
    "困る",
    "助かる",
    "見える",
    "聞こえる",
    "落ち着く",
};

}  // namespace hdTrans::pipeline::internal
