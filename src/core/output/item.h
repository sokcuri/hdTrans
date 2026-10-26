#pragma once
// What the compose stage reads and edits on the word items of a sentence after the
// resolve passes.
#include <string>

namespace hdTrans::output {

// The class of an item's conjugation detail.
enum class DetailClass : int {
    None = 0,     // a word without conjugation
    Yougo = 1,    // a verb
    Keiyo = 2,    // an i-adjective
    Dakeiyo = 3,  // an adjectival noun
};

// A candidate translation or ending of an item.
struct ResolvedEntry {
    std::string code;   // part code without its major tag ("110" of "N110")
    std::string gattr;  // grammar attributes ("WaZ7")
    std::string text;   // Korean target; conjugating stems end in a marker ("먹&A1@"), endings start
                        // with their form code ("5다")
};

// One item of the chain, in chain order: an item's neighbours are the items before
// and after it in the vector.
struct ResolvedItem {
    int tokenIndex = 0;
    int offset = 0;  // bytes into its token's text
    int length = 0;  // bytes of the token text the item covers
    std::string surface;

    int selected = -1;       // the selected entry, -1 none
    int selectionFlag = 0;
    DetailClass detailClass = DetailClass::None;
    int detailKind = -1;     // conjugation kind; -1 without a detail
    int detailRecord = 0;    // the verb branch folded into a kind-7 ending; 0 without a detail
    int directIndex = -1;    // the selected ending; -1 without an ending group or selection

    ResolvedEntry entry;     // the selected entry (empty when selected is outside the entries)
    ResolvedEntry first;     // entry 0 (equal to entry when selected is 0; empty without entries)
    ResolvedEntry ending;    // the selected ending (empty when directIndex is -1)

    bool spaceAfter = false;     // a source space after an isolated interjection stays a word boundary
    bool fixed = false;          // entry 0 comes from a plain dictionary: its targets are not reassigned
    std::string endingSurface;   // source text of the ending group, "" without one
    int endingLength = 0;        // bytes of the ending group's source key, 0 without one
    bool ownedEnding = false;    // the ending group comes from a plain dictionary
    // Some entry other than entry 0 and the selected one holds a Hangul syllable (the
    // entries up to the first one without code and text).
    bool otherHangul = false;
    std::string appended;  // Korean written right after the item, outside its count and conjugation
};

}  // namespace hdTrans::output
