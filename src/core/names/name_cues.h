#pragma once
// The cues of a line that show a word of the name layer to be a person's name rather than
// the common word the dictionary has for it (村雨 무라사메, not 소나기).
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"

namespace hdTrans::names {

struct NameCue {
    std::string key;
    // A cue that names the word wherever the document writes it: a title or ship type before
    // it, a suffix or honorific after it, a speaker label, a call to action or a phrase of
    // naming, for a noun of two characters or more that names no time, weather, sky or place.
    // Any other cue (a self-introduction in です or よ, a call, a list of names, or a cue of a
    // word a later line may mean in its common sense) holds in its line only.
    bool strong = false;
    // A cue that would be strong but names a word of time, weather, the sky or a place, or one a
    // later line may mean in its common sense: it holds for the document only where two lines of
    // the document cue it so (駆逐艦夕立 and 夕立、突撃する).
    bool recurring = false;
    // An honorific after the word (皐月さん) on a key of two characters or more that is no hiragana
    // nor a katakana spelling of a native word: it holds for the document (spec LEX-4820).
    bool person = false;
    // A predicate of a person (玉藻がはにかむ, 皐月の顔) on such a key: it holds for the document
    // where two lines of the document cue it so, as a recurring cue does.
    bool acted = false;
    // Any of the cues above before their limits (a title, suffix, honorific, speaker label, call, phrase
    // of naming or predicate of a person): the evidence that names a common word for the document on two
    // lines (spec LEX-5175).
    bool evidence = false;
};

// The name-layer keys with a person sense that text cues as names, in order of first
// occurrence, each once (strong when one of its cues is). A key the dictionary reads as a
// longer word at that place, a person noun or a pronoun is not cued.
std::vector<NameCue> NameCues(const dict::Snapshot& snap, std::string_view text);

// Whether the occurrence of key between before and after (the line's text around it) stands where a
// person stands: before an honorific, as a speaker label or a call, before の and a part of a person,
// or as the subject or topic of what a person does or says (spec LEX-5175).
bool PersonSlotAt(const dict::Snapshot& snap, std::string_view key, std::string_view before, std::string_view after);

// Whether after opens the value of a field of a status window: a colon and a value with no kana or kanji
// (力：35, 敏捷：Ｂ+; spec LEX-5175).
bool StatusFieldAt(std::string_view after);

// Whether rest, after spaces, opens the value of a field up to the next space: a number, a percent, a level
// or a grade (35, ５０％, Lv.3, Ｂ+, SS); not punctuation, an ellipsis or a word (……, ！？, OK).
bool StatusValueAt(std::string_view rest);

// Whether after keeps a common word in its common sense: a start of name.common-after (朝日を浴びる,
// 力を込める), a status field, or が, は or も and a predicate of name.common-predicate in the clause
// (敏捷が特筆して高い; spec LEX-5175).
bool CommonSenseAt(std::string_view after);

// Whether a start of name.common-after that ends a word there begins after (朝日を浴びて, 力の伸び).
bool CommonAfterAt(std::string_view after);

// Whether key, all kanji, is a common word of the dictionary: a sense that is no name or place (朝日
// 아침 해, 力 힘, 敏捷 민첩).
bool CommonWord(const dict::Snapshot& snap, std::string_view key);

}  // namespace hdTrans::names
