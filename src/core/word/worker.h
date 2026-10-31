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

// The marks open around a place: thought marks (mark.thought-open, closed by
// mark.thought-close) and the marks of a word spelled out (output::kEmphasisOpen, closed by
// output::kEmphasisClose).
struct OpenMarks {
    int thought = 0;
    int spelled = 0;
};

// The marks open after s when open were open before it.
OpenMarks OpenMarksAfter(std::string_view s, OpenMarks open);

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
    // While set, a verb takes only an ending shorter than this one, which left text that no
    // word reads; a validity rule does not reject the shorter ending for the text that the
    // longer one would have taken (と before これ after とこ).
    std::string_view longerEnding;
    // A search that left text unread runs again with the shorter endings of verbs, and keeps
    // the verbs that a shorter ending let it read.
    bool shorterEndings = false;
    std::vector<const Item*> shortenedVerbs;
    // The word search of a token recurses once per word. At kMaxWordDepth levels it stops
    // with the words found so far, and BuildSurfaceTransform goes on at resumeCursor
    // (-1 for none) with resumeParent as the word before and resumeRetries retries used.
    static constexpr int kMaxWordDepth = 300;
    int wordDepth = 0;
    int resumeCursor = -1;
    Item* resumeParent = nullptr;
    int resumeRetries = 0;
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
    // Where token j stands in brace ruby markup: 1 between {rb} and {/rb} (the base), 2
    // between {rt} and {/rt} (the reading), 0 elsewhere; computed once for all tokens.
    int RubyPart(int j);
    // The marks open at the start of token j, from the raw text of the tokens before it;
    // computed once for all tokens.
    OpenMarks OpenMarksAt(int j);

    // Per-token step for token i (control markers are merged by the tokenizer). Sets
    // last to the last token it consumed and returns whether token i ends a sentence.
    bool Dispatch(int i, int& last);
    bool LatinOrDigits(int j) const;
    bool FilterMark(std::string_view s) const;

    // Dictionary lookup with the plain-entry filters of the word stage.
    Lookup DoLookup(std::string_view query, int mode);
    // The bytes of the copy written apart from the one next to it (tokenizer::CopySpace) that starts
    // text, a view into the current token; 0 for none.
    std::size_t CopyUnit(std::string_view text) const;
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
    bool GodanCausative(std::string_view stem) const;
    bool CausativeOfShorter(std::string_view stem) const;
    bool SentenceFinal(std::string_view rest, int token, bool comma) const;
    bool EndingApplies(const Entry& e, std::string_view rest, int token) const;
    bool CutsWord(std::string_view before, std::string_view ending, std::string_view after) const;
    bool WordAfterAdnominal(std::string_view after) const;
    uint8_t Register(const dict::UserEntry& u) const;
    Record* CreateSyntheticRecord(int token, std::string_view source, bool emptyTarget);
    bool Transliterate(std::string_view s, std::string& out);

private:
    std::vector<int> quoteDepths_;  // QuoteDepth by token, empty until first asked
    std::vector<uint8_t> rubyParts_;  // RubyPart by token, empty until first asked
    std::vector<OpenMarks> openMarks_;  // OpenMarksAt by token and after the last, empty until first asked

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
    // The words of full from cursor to its end; parent is the word before cursor.
    void CoverSurface(int token, std::string_view full, int cursor, Item* parent);
    std::size_t CopyEnd(std::string_view text, std::string_view before);
    bool TranslateCandidateAt(std::string_view full, int cursor, int token, Item* parent);
    bool SpellsNumber(Record* records, const Record& number, std::string_view text);
    bool VerbalNounAtCounter(Record* records, const Record& number, std::string_view text);
    bool CountBeforeFollower(Record* records, const Record& number, std::string_view text, int token);
    bool ProcessCandidateRecord(std::string_view full, int cursor, int token, Record* records, Item* parent);
    // Whether a dictionary word starts at full[cursor:].
    bool WordAt(std::string_view full, int cursor, int token);
    // Whether the verb shorter, read from records at full[cursor:] with a shorter ending, reads the
    // same where the text ends after it.
    bool StandsAlone(std::string_view full, int cursor, int token, Record* records, const Item& shorter);
    // Text path: a country or province (noun.realm-name) with 国 before a compound of that 国
    // (noun.realm-compound, and noun.realm-province-compound after a province) gives it its 国 (美濃国境
    // 美濃 + 国境, 日本国内 日本 + 国内, 出羽国人 出羽 + 国人): the reading of
    // the name that the name with 国 starts with moves to the front, when no longer word starts
    // after the name with 国 (越前国旗頭 stays 越前国 + 旗頭).
    Record* NounBeforeCompound(Record* records, std::string_view text);
    Record* DropVetoed(Record* records, int cursor, int token) const;
    bool AnchorAround(int i) const;
    Item* ProtectedItem(int token);
    Item* HonorificItem(std::string_view full, int cursor, int token, Item* parent);
    // The word that ends where the word at cursor starts: parent, or the last word of the chain
    // when it ends there in the token or is a protected token right before it; else null.
    Item* WordBefore(int cursor, int token, Item* parent) const;
    Record* DropStraySuffixes(Record* records, std::string_view full, int cursor, int token, Item* parent);

    // Lookup filters.
    bool Skip(const dict::UserEntry& u, std::string_view query) const;
    bool EndRowFits(const dict::UserEntry& u, std::string_view query) const;
    bool TrailsOff(std::string_view rest) const;
    bool Asks(std::string_view rest) const;
    bool PatternFits(const dict::UserEntry& u, std::string_view query) const;
    bool KanaBounded(std::string_view key, std::string_view query) const;
    bool StandsApart(std::string_view key, std::string_view query) const;
    bool KanaWord(const dict::UserEntry& u, std::string_view query) const;
    bool KanaBeforeCopula(const dict::UserEntry& u, std::string_view query) const;
    void ApplyConstraints(dict::Candidates& found, std::string_view query);
    void ReadGivenNames(dict::Candidates& found, std::string_view query);
    // Whether the text head that a word follows in its token ends in something other than a noun
    // (ConstraintContext::NotAfterNoun).
    bool NotAfterNoun(std::string_view head);
    void DropBoundUtterances(Lookup& l, std::string_view query);
    void DropNegatedAdverbs(Lookup& l, std::string_view query);
    void DropFinalAdnominals(Lookup& l, std::string_view query);
    void DropOpenUtterances(Lookup& l, std::string_view query);
    void DropModalSplits(Lookup& l, std::string_view query);
    bool SplitsModal(const Record& r, std::string_view query, bool afterKa) const;
    bool SplitsKashira(const Record& r, std::string_view query) const;
    bool SplitsDeshou(const Record& r, std::string_view query) const;
    bool SplitsVerb(const Record& r, std::string_view query) const;
    bool SplitsPrefixWord(const Record& r, std::string_view query) const;
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
bool MatchKind(Detail& d, std::string_view tail, std::string_view recordText, const Record& record,
               bool potential = false);

// The conjugation analysis of a verb with key and kind (part code byte 3) on tail, the
// text after the key: the branch (endings attaching to it have part code byte 2
// '0'+branch) and the length of the conjugation pattern; false when no branch matches.
bool AnalyseVerbTail(std::string_view key, char kind, std::string_view tail, int& branch, int& length);

// Whether text starts with a particle or punctuation after a noun, or is empty.
bool NounEndAt(std::string_view text);

// Whether the adjective stem with its nominal ending is a word of the dictionary already: stem is
// also the key of a verb, whose continuative it then spells (楽し of 楽しむ: 楽しみ), or stem and
// nominal are the key of a noun (高み, 甘み).
bool VerbOrNounKey(const dict::Snapshot& snapshot, std::string_view stem, std::string_view nominal);

// The enabled endings of g, read on a form of an auxiliary, rewritten for the verb before it: head
// is the Korean of the auxiliary ending in its marker family (6 버&AA@), and the ending's class
// digit picks the form of that family (5지만 → 6 버렸지만). An ending whose class has no such form
// is disabled; false when none is left. With every, the disabled endings are rewritten too, for a
// later stage that enables one again (the auxiliary of the と form and of はる).
bool ProgressiveEndings(Group& g, Detail& d, std::string_view head, bool every = false);

// Whether a verb record of conjugation class sub may join a group whose first record
// has class first, for conjugation branch mode.
bool VerbClassJoins(char mode, char first, char sub);

// Whether text ends in a noun: a kanji, katakana, a letter, a digit, a closing
// bracket or a pronoun written in hiragana.
bool NounBefore(std::string_view text);

// A plain-dictionary ending group.
bool OwnedGroup(const Group* g);

// Whether the number at the start of text, of size bytes, is the denominator of a fraction:
// fraction.of and a number or a question word for one follow it (十分の一 10분의 1, 三分の二
// 3분의 2, 十二分の一 12분의 1), so that a word spelled with the number (十分 충분히) gives way.
bool FractionAt(std::string_view text, std::size_t size);

}  // namespace hdTrans::word
