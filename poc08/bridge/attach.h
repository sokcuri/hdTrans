// Attachment annotations on the 0.7.x token chain: which clause each chain item belongs to, which items are
// arguments of which predicate (with their case and role), and each clause's features and subject class.
// The side IR's C, L and Z stages provide them; this maps them onto the items of one sentence, so that packs
// (frame, voice, negation rows) applied to the chain can read a verb's arguments and a noun's governing verb.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ir/ir.h"

namespace hd08::bridge {

struct ItemArg {
    uint8_t role = 0;      // Role (F::role): subj obj dat loc from with to cmp topic head …
    uint8_t how = 0;       // ArgHow (F::how): marked topic relhead omitted bare
    uint8_t kase = 0;      // the Japanese case particle (F::kase_ja) of the argument bunsetsu
    int item = -1;         // the argument's content item, -1 when it lies outside the sentence's items
    uint32_t lemma = 0;    // interned in the Doc: the argument noun (compound nouns joined)
    uint32_t cls = 0;      // interned class (@person …) or 0
};

struct ClauseAttach {
    int predItem = -1;     // the item holding the clause's predicate content token
    int parent = -1;       // index of the clause it depends on (Clause.parent), -1 for a main clause
    const Clause* clause = nullptr;  // the side IR clause (link, tense, pol, negFn, voice, force, subj, beneTo)
    std::vector<ItemArg> args;
};

struct ItemAttach {
    int clause = -1;       // the clause whose span holds the item's content token
    bool pred = false;     // the item holds that clause's predicate
    // When the item is (the content of) an argument: the clause and predicate item it is an argument of.
    int argOf = -1, headItem = -1;
    uint8_t role = 0, how = 0, kase = 0;
    bool isArg = false;
    // When the item modifies a noun: that noun's item, and how (1: X の Y, 2: an attributive word or clause).
    int modifies = -1;
    uint8_t modKind = 0;
};

struct Attachment {
    std::vector<ItemAttach> items;   // one per chain item, in chain order
    std::vector<ClauseAttach> clauses;
};

// sent is the sentence's side line (its toks, buns and clauses, as the bridge copies them per sentence);
// itemSpans are the chain items' source spans in sent.src, in chain order.
Attachment AttachItems(const Line& sent, std::span<const Span> itemSpans);

// One JSON object per sentence for authors and tests: items with their text and attachment, clauses with
// their predicate, link, polarity, voice, subject class and arguments.
std::string AttachmentJson(const Doc& doc, const Line& sent, std::span<const Span> itemSpans, const Attachment& a);

}  // namespace hd08::bridge
