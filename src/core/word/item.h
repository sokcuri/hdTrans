#pragma once
// The word items of a sentence: dictionary candidates (records), the candidate group
// and conjugation detail of each item, and the item chain. Offsets and lengths are
// byte offsets into UTF-8 token texts. Codes and targets stay the strings the
// dictionaries hold; the accessors read them byte by byte.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/user_text.h"
#include "core/rules/rules.h"
#include "core/text/lifetime.h"
#include "core/word/provenance.h"

namespace hdTrans::word {

class Worker;

// One dictionary candidate. Candidates form a singly linked list through child, in
// the order the lookup returned them; filters relink it.
struct Record {
    std::string_view source;  // the matched source text; "" for the sentinel queries
    std::string target;
    std::string_view partCode;  // "N110"; byte 0 is the major tag
    std::string_view semantic;  // 3-byte semantic codes
    std::string attr;           // 2-byte grammar codes
    std::string_view dictCode;  // "GN", "SO", ...
    int ruleNo = 0;
    int id = 0;
    // The plain or name-layer entry the record was built from; null for base and
    // user-dictionary records.
    const dict::UserEntry* user = nullptr;
    Record* child = nullptr;
};

// One candidate translation of a word.
struct Entry {
    std::string dst;            // target text
    std::string_view code;      // the part code without its major tag ("110")
    std::string_view attr;      // semantic codes
    std::string_view field;     // dictionary code
    std::string gattr;          // grammar codes; markers are appended at run time
    char cls = 0;               // the major tag ('N', 'M', ...), 0 for none
    int ruleId = -1;            // local rule number
    bool fixed = false;         // no rule selects another entry or changes the target
    const dict::UserEntry* user = nullptr;  // the plain entry, null for base records
    Selection targetChoice;  // a rule or rewrite that changed this candidate's target
    bool neighbourAssigned = false;  // a rule running on another item assigned the target
};

// The candidates of one surface: the leading records of a list that share the first
// record's source and class digit and are all plain or all base records.
// records holds the rest of the list after the configured group limit.
struct Group {
    std::string surface;
    int length = 0;  // length of the surface the group was built from
    std::vector<Entry*> entries;
    Record* records = nullptr;

    // Entry i; null outside the group (-1 for none).
    Entry* At(int i) const {
        return i < 0 || static_cast<std::size_t>(i) >= entries.size() ? nullptr : entries[static_cast<std::size_t>(i)];
    }
};

// Detail classes of an item.
enum DetailClass : int {
    kDetailInfo = 0,     // no conjugation part (TWordInfo)
    kDetailVerb = 1,     // verb with a conjugation ending (TVerbWord)
    kDetailKeiyo = 2,    // adjective (TKeiyoWord)
    kDetailDakeiyo = 3,  // adjectival noun (TDakeiyoWord)
};

constexpr int kReferenceDirectSlots = 100;
constexpr int kDirectSlots = 1000;

// The conjugation state of an item. Info details only carry the next token; the
// others also hold the group of the conjugation ending (direct group) and its
// selection.
struct Detail {
    int cls = kDetailInfo;
    int nextType = 0;             // type of the token after the item's token
    std::string_view nextText;    // its text when the detail was made
    bool nonterminalBoundary = false;
    bool tailLengthCheck = false;
    bool causativePassive = false;

    // The conjugation branch 0..17 of a verb; '0', 'x' or 'X' for adjectives; -1 unset.
    int kind = -1;
    int record = 0;  // verb: 1 passive, 2 causative, 4 potential, 5 causative-passive
    int punct = 0;   // punctuation class after the ending
    Group* group = nullptr;
    std::vector<uint8_t> classes;  // punctuation class per direct entry
    std::vector<uint8_t> enabled;  // direct entries still possible
    int resultCount = 0;
    int selected = -1;  // selected direct entry, -1 none
    int scanOffset = 0;

    // verbs
    int patternKind = -1;    // conjugation table row
    int patternMatched = 0;  // 1: matched by the prefix table; patternLength is the matched length
    int patternLength = 0;

    bool Yougo() const { return cls != kDetailInfo; }
    void ResetDirect(std::size_t count, bool value = true) {
        classes.assign(count, 0);
        enabled.assign(count, value);
    }
    void ResetEnabled(bool value) {
        for (uint8_t& e : enabled) e = value;
    }
    // The punctuation class of the text after a word: 1 comma, 2 period or end of
    // sentence, 3 '?', 4 '!', 5 ')', 6 closing quote or bracket, 7 ':' or '<', 0
    // other. Empty text is classified by the next token.
    int ClassifyTrailingPunctuation(std::string_view text) const;
};

// One word of the sentence chain.
class Item final : public rules::Item {
public:
    Worker* w = nullptr;
    Group* group = nullptr;
    Detail* detail = nullptr;
    int tokenIndex = 0;
    int start = 0;          // byte offset of the word in text
    std::string_view text;  // the token text the word was found in
    int length = 0;         // bytes consumed, the conjugation ending included
    int selected = -1;
    Selection choice;
    Selection directChoice;
    std::vector<ShadowChoice> shadowChoices;
    Item* prev = nullptr;
    Item* next = nullptr;
    int compDI = -1;
    int compUmiDI = -1;
    int selectionFlag = 0;  // set by the output stage
    bool adjectival = false;
    bool lexical = false;

    // Entry accessors: i is an entry of the group; outside it (-1) they read empty.
    std::string_view EntryText(int i) const;
    std::string_view Code(int i) const;
    std::string_view Attr(int i) const;
    std::string_view GAttr(int i) const;
    int RuleID(int i) const;
    void SelectCandidate(int i, Selection source);
    void AssignEntrySurface(int i, std::string_view s, Selection source = {Provenance::Rewrite});

    // The detail kind of a verb or adjective, -1 for Info details.
    int DetailKind() const { return detail->Yougo() ? detail->kind : -1; }
    int DetailRecord() const { return detail->Yougo() ? detail->record : 0; }
    int DetailSelectedIndex() const { return detail->Yougo() ? detail->selected : -1; }

    // The conjugation-ending group, null if none.
    Group* DirectGroup() const { return detail->Yougo() ? detail->group : nullptr; }
    int DirectIndex() const { return DirectGroup() ? detail->selected : -1; }
    void SetDirectIndex(int i, Selection source = {Provenance::Rewrite});
    int DirectCount() const { return DirectGroup() ? static_cast<int>(DirectGroup()->entries.size()) : 0; }
    std::string_view DirectSurface() const { return DirectGroup() ? std::string_view(DirectGroup()->surface) : ""; }
    int DirectLength() const { return DirectGroup() ? DirectGroup()->length : 0; }
    // Direct entry i; null without a direct group or outside it.
    Entry* DirectEntry(int i) const { return DirectGroup() ? DirectGroup()->At(i) : nullptr; }
    bool DirectEnabled(int i) const { return DirectGroup() && i >= 0 && static_cast<std::size_t>(i) < detail->enabled.size() && detail->enabled[i]; }
    std::string_view DirectText(int i) const;
    std::string_view DirectCode(int i) const;
    std::string_view DirectAttr(int i) const;
    std::string_view DirectGAttr(int i) const;
    int DirectRuleID(int i) const;
    int DirectClassifier(int i) const;
    void AssignDirectSlot(int i, std::string_view s, Selection source = {Provenance::Rewrite});

    // Appends marker to the grammar codes of the selected entry, or of every entry
    // when none is selected.
    void AppendRuleCandidateMarker(std::string_view marker);

    // Whether no rule may change the item's selection and target.
    bool Fixed() const;
    // A plain-dictionary name or a word span.
    bool IsNameItem() const;
    // The end of the item in its token as Insert compares it with an end in another
    // token: a word span item ends at 1.
    int CrossEnd() const;

    // The J digit of entry i against the punctuation after the word: 1 match, -1 no
    // match or no attribute.
    int CheckJ(int i) const;
    // CheckJ for direct entry i against its recorded class: 1 match, 0 no match, -1
    // no attribute.
    int CheckDirectJ(int i) const;

    // The item delta items along the chain (leftwards for delta < 0), skipping
    // parenthesised groups and, rightwards from a ruby base, its reading; a found neighbour gets compDI and compUmiDI -1, the item
    // itself (delta 0) keeps this item's.
    Item* Object(int delta);

    // rules::Item
    void SetRuleContext(bool global, int number) override;
    void BeginRule() override;
    void EndRule(bool fired) override;
    rules::Item* GetObject(int delta) override;
    int GetCurrentCompDI() override { return compDI; }
    int GetCurrentCompUmiDI() override { return compUmiDI; }
    bool CheckSrc(std::string_view s) override;
    bool CheckLLSrc(rules::Item* wi) override;
    bool CheckDst(std::string_view s, int di) override;
    bool AssignDst(int di, std::string_view s) override;
    bool AssignLLDst(rules::Item* wi, int di) override;
    bool CheckPumsa(std::string_view s, int di) override;
    bool CheckAttr(std::string_view s, int di) override;
    bool MinAttrOperation(int di, std::string_view s) override;
    bool CheckLLAttr(int op, rules::Item* wi, int di) override;
    // Whether the word starts its sentence: no word before it, or only an opening bracket.
    bool StartsSentence() const;
    // Whether the word stands inside 「」 or 『』, a quote an earlier line left open included.
    bool InQuote() const;
    // Whether the word is a word span of Hangul syllables and spaces only (rule code Kh): Korean
    // the sound stage wrote or the input holds, whose part of speech is unknown; a span with a
    // mask (자○) is a noun.
    bool KoreanSpan() const;
    // Whether a question word (何, 誰, どこ, どう...) is the word or comes before it in its
    // sentence, so that a guess there asks (何をしてるんだろうな 하는 걸까).
    bool AfterQuestionWord() const;
    // Whether the conjugation ending closes with the question particle か (行くか, どうなるか), not
    // with the kana of a verb form that ends in か (行かない).
    bool EndingAsks() const;
    // Whether the word is a question word: alone, with a particle (どこから, 誰の) or a kanji
    // (何人), not an indefinite (何か, 誰も, いつでも).
    bool IsQuestionWord() const;
    bool JoinsNext() const;
    // JoinsNext, or the word ends its token and the next word starts the next token, with no
    // space between them (超 and Ａ級 of 超Ａ級).
    bool WrittenBeforeNext() const;
    // Whether candidate i reads the word as a count (rule code Nq): a number or 半, or a number
    // (kanji or digits, or 数, 何, 幾 before it) and a counter coded Ct, read with digits or a
    // Korean numeral first (三 3, 三人 세 명, 十日 열흘, 数日 며칠, 一時間半, 五分の一). 月 and 時
    // after a number name a month or an hour (五月, 三時), and a word read otherwise is no
    // count (一番 제일).
    bool ReadsCount(int i);
    // A small っ with no translation closing the word before it (行くっ」, 行くっ！): no
    // word or only punctuation follows.
    bool ClosingTsu() const;
    bool CheckGAttr(std::string_view s, int di) override;
    bool MinGAttrOperation(int di, std::string_view s) override;
    bool CheckGobiSrc(std::string_view s) override;
    bool CheckGobiDst(std::string_view s, int di) override;
    bool AssignGobiDst(int di, std::string_view s) override;
    bool AssignLLGobiDst(rules::Item* wi, int di) override;
    bool CheckGobiPumsa(std::string_view s, int di) override;
    bool CheckGobiAttr(std::string_view s, int di) override;
    bool MinGobiAttrOperation(int di, std::string_view s) override;
    bool CheckGobiGAttr(std::string_view s, int di) override;

    // Construction checks (verb.cpp, adjective.cpp).
    bool AcceptVerbContinuation(Record* following);
    bool RunDefaultTrieQuery();
    bool RunKeiyoTrieQuery();
    bool RunDakeiyoCase8Query();
    bool MitaiLike() const;
    bool StandsAlone() const;
    std::string_view PlainEnding() const;
    std::string_view BareStemEnding(std::string_view after) const;
    int ContextBefore() const;
    // The two width units before the word, looking past tokens of … and ‥; "" when
    // there are fewer.
    std::string CharBefore() const;

private:
    bool CheckSyntheticPumsa(int i, std::string_view pattern) const;
    bool CheckPumsaFallback(int i, std::string_view pattern) const;
    bool IsRuleRightBoundary() const;
    bool AssignGobi(int di, std::string_view value);
};

// The item chain of a sentence, ordered by token and byte offset, with the cursor of
// the sentence passes.
struct Chain {
    Item* head = nullptr;
    Item* tail = nullptr;
    Item* cursor = nullptr;

    void Unlink(Item* it);
    // Adds item and returns its length minus the lengths of the items it replaces.
    // With overlapping it replaces the items of the same token whose end lies inside
    // the new item (start < end <= its end), otherwise those of the same token at the
    // same start. The item goes before the first item that ends at or after its end
    // (or, of a later token, whose predecessor's CrossEnd is before it).
    int Insert(Item* item, bool overlapping);
};

// Objects of one kind, allocated in blocks that are kept for reuse.
template <class T>
class Pool {
public:
    // A default-initialised object, reused when the pool was reset.
    T* New() {
        if (used_ == size_) {
            blocks_.push_back(std::make_unique<T[]>(kBlock));
            size_ += kBlock;
        }
        T* p = &blocks_[used_ / kBlock][used_ % kBlock];
        used_++;
        return p;
    }
    void Reset() { used_ = 0; }
    std::size_t Used() const { return used_; }
    std::size_t Capacity() const { return size_; }

private:
    static constexpr std::size_t kBlock = 256;
    std::vector<std::unique_ptr<T[]>> blocks_;
    std::size_t used_ = 0, size_ = 0;
};

// The storage of the items of a chunk; reset for each chunk, keeping its blocks.
struct Arena {
    Pool<Item> items;
    Pool<Group> groups;
    Pool<Entry> entries;
    Pool<Detail> details;
    Pool<Record> records;
    // The name-layer entries of the lookups, which records point to.
    std::vector<std::vector<dict::UserEntry>> names;
    std::size_t namesUsed = 0;
    // Text built by the sentence passes that records point to; the strings do not move.
    std::vector<std::unique_ptr<std::string>> texts;

    // Keeps text until the arena is reset and returns a view of it.
    std::string_view Keep(std::string text) HDTRANS_LIFETIMEBOUND {
        texts.push_back(std::make_unique<std::string>(std::move(text)));
        return *texts.back();
    }

    void Reset() {
        items.Reset();
        groups.Reset();
        entries.Reset();
        details.Reset();
        records.Reset();
        namesUsed = 0;
        texts.clear();
    }
};

}  // namespace hdTrans::word
