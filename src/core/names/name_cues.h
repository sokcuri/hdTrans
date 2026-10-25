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
};

// The name-layer keys with a person sense that text cues as names, in order of first
// occurrence, each once (strong when one of its cues is). A key the dictionary reads as a
// longer word at that place, a person noun or a pronoun is not cued.
std::vector<NameCue> NameCues(const dict::Snapshot& snap, std::string_view text);

}  // namespace hdTrans::names
