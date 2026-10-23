#pragma once
// The state of the word stage over one chunk: the tokens, the lookup context the
// lookups carry from token to token, and the chain of the current sentence.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/word/analyze.h"
#include "core/word/item.h"
#include "core/word/journal.h"
#include "core/word/util.h"

namespace hdTrans::word {

// Lookup modes.
enum LookupModeNumber : int {
    kModeGeneral = 0,
    kModeAdjectivalNounEnding = 1,
    kModeAdjectiveEnding = 2,
    kModeVerbEnding = 3,
    kModePrefixVerb = 4,
};

struct Lookup {
    Record* records = nullptr;
    int count = 0;  // the candidate count of the lookup, which the checks test
};

class Worker {
public:
    Worker(const tokenizer::Tokens& tokens, const dict::Snapshot& snapshot, const dict::LookupContext& lookup,
           const Settings& settings, Arena& arena, LookupObserver observer);

    const tokenizer::Tokens& tokens;
    const dict::Snapshot& snapshot;
    dict::LookupContext lookupContext;
    Settings settings;
    Arena& arena;
    LookupObserver observer;

    std::vector<uint8_t> spaced;  // Analysis::spaced
    Chain chain;
    Selection ruleChoice;  // active rule, including assignments to neighbouring items
    Journal journal;       // the writes of the rule running now
    const void* ruleItem = nullptr;  // the item whose rule is running
    int current = -1;  // the token being processed
    int insertedLength = 0;
    int candidateRetryCount = 0;
    std::string_view transformSurface;
    bool fault = false;

    // Token j as the token loop sees it while processing current.
    tokenizer::TokenView Tok(int j) const;
    // Text read by ending lookahead, optionally canonical before the token is visited.
    std::string_view NextText(int j) const;
    int TokenCount() const { return static_cast<int>(tokens.size()); }
    int Span(int j) const { return tokens[static_cast<std::size_t>(j)].span; }
    // Token j as typed once visited (j before current): "" for a token emptied or
    // never visited.
    std::string_view Typed(int j) const;
    // The depth of 「」 and 『』 quotes open at the start of token j, from settings.quoteDepth
    // and the raw text of the tokens before it; computed once for all tokens.
    int QuoteDepth(int j);

    // Per-token step for token i (control markers are merged by the tokenizer). Sets
    // last to the last token it consumed and returns whether token i ends a sentence.
    bool Dispatch(int i, int& last);

    // Dictionary lookup with the plain-entry filters of the word stage.
    Lookup DoLookup(std::string_view query, int mode);
    bool SuVerb(std::string_view stem);
    // Whether after, the text after the stem in い of a わ-row verb written in kanji
    // (彷徨い), is the て or past た that only a く verb in い takes there (彷徨いて is 彷徨く
    // in て). The desire forms (たい, 言いてえ) and a て the sound stage put before an ellipsis
    // (戦い…) do not count.
    bool TeAfterWaStem(std::string_view stem, std::string_view after) const;

    // Allocation; every field is set.
    Record* NewRecord();
    Entry* NewEntry(const Record& r);
    Entry* NewEntry();
    Group* NewGroup(Record* records, char mode);
    Detail* NewDetail(int cls, int token);
    Item* NewItem(Detail* d, Group* g, std::string_view text, int start, int token);

    Item* InsertLateCandidate(std::string_view text, int offset, int token, Record* records);
    Item* QueryPrefixTable(std::string_view text, int offset, int token);
    bool MatchDictionary(std::string_view following, std::string_view source, char first, char second);
    bool GodanImperative(std::string_view form) const;
    bool SentenceFinal(std::string_view rest, int token, bool comma) const;
    bool EndingApplies(const Entry& e, std::string_view rest, int token) const;
    bool CutsWord(std::string_view before, std::string_view ending, std::string_view after) const;
    uint8_t Register(const dict::UserEntry& u) const;
    Record* CreateSyntheticRecord(int token, std::string_view source, bool emptyTarget);
    bool Transliterate(std::string_view s, std::string& out);

private:
    std::vector<int> quoteDepths_;  // QuoteDepth by token, empty until first asked

    void Insert(Item* it);
    void BuildTokenTransformText(int i);
    void BuildSyntheticTransform(std::string_view surface, int token, int offset, bool emptyTarget);
    std::string FilterKana(std::string_view text) const;
    // The record of a numeral, ASCII digits or punctuation at source; digits: a kanji number
    // that is not looked up in the dictionary.
    Record* LookupFallbackRecord(int token, std::string_view source, int left, int right, bool digits = false);
    // afterMyriad: the text before source ends with 万 or 億.
    Record* ProbeNumericPrefix(int token, std::string_view source, bool afterMyriad = false);
    void BuildSurfaceTransform(int token, std::string_view surface);
    bool TranslateCandidateAt(std::string_view full, int cursor, int token, Item* parent);
    bool SpellsNumber(Record* records, const Record& number, std::string_view text);
    bool VerbalNounAtCounter(Record* records, const Record& number, std::string_view text);
    bool ProcessCandidateRecord(std::string_view full, int cursor, int token, Record* records, Item* parent);
    bool AnchorAround(int i) const;
    Item* ProtectedItem(int token);
    Item* HonorificItem(std::string_view full, int cursor, int token, Item* parent);

    // Lookup filters.
    bool Skip(const dict::UserEntry& u, std::string_view query) const;
    bool EndRowFits(const dict::UserEntry& u, std::string_view query) const;
    bool TrailsOff(std::string_view rest) const;
    bool Asks(std::string_view rest) const;
    bool PatternFits(const dict::UserEntry& u, std::string_view query) const;
    bool KanaBounded(std::string_view key, std::string_view query) const;
    bool KanaWord(const dict::UserEntry& u, std::string_view query) const;
    void ApplyConstraints(dict::Candidates& found, std::string_view query);
    void DropBoundUtterances(Lookup& l, std::string_view query);
    void DropNegatedAdverbs(Lookup& l, std::string_view query);
    void DropFinalAdnominals(Lookup& l, std::string_view query);
    void DropModalSplits(Lookup& l, std::string_view query);
    bool SplitsModal(const Record& r, std::string_view query, bool afterKa) const;
    bool SplitsKashira(const Record& r, std::string_view query) const;
    bool SplitsVerb(const Record& r, std::string_view query) const;
    bool PersonSuffixAt(std::string_view s) const;
    // Whether text starts with an honorific that HonorificItem reads after a name.
    static bool StartsWithHonorific(std::string_view text);
    bool VerbUtterance(const Record& r) const;
    void TransliterateGlosses(Record* records);
};

// Matches the conjugation of a verb record at text[offset:]: walks the records of the
// same length from records on, sets d.patternKind and d.kind (the branch), and returns
// whether one matched; stop is the record it stopped at.
bool MatchConjugation(Detail& d, std::string_view text, int offset, Record* records, Record*& stop);
bool MatchKind(Detail& d, std::string_view tail, std::string_view recordText, const Record& record);

// The conjugation analysis of a verb with key and kind (part code byte 3) on tail, the
// text after the key: the branch (endings attaching to it have part code byte 2
// '0'+branch) and the length of the conjugation pattern; false when no branch matches.
bool AnalyseVerbTail(std::string_view key, char kind, std::string_view tail, int& branch, int& length);

// Whether a verb record of conjugation class sub may join a group whose first record
// has class first, for conjugation branch mode.
bool VerbClassJoins(char mode, char first, char sub);

// Whether text ends in a noun: a kanji, katakana, a letter, a digit, a closing
// bracket or a pronoun written in hiragana.
bool NounBefore(std::string_view text);

// A plain-dictionary ending group.
bool OwnedGroup(const Group* g);

}  // namespace hdTrans::word
