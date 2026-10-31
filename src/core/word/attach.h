#pragma once
// Attachment annotations on the item chain of a sentence (docs/packs.md): which clause each item
// belongs to, which items are arguments of which predicate, and the features of each clause. The
// engine does not make them; a provider may (the analyzer of a host program), and pack conditions
// read them when it does. Without a provider the packs layer reads its own nearer context.
#include <string>
#include <utility>
#include <vector>

#include "core/tokenizer/token.h"

namespace hdTrans::word {

struct Chain;

// An argument of a clause's predicate.
struct AttachArg {
    std::string role;   // subj obj dat loc from with to topic cmp …
    std::string how;    // marked topic bare omitted relhead …
    std::string lemma;  // the argument noun (an omitted one too)
    std::string cls;    // its class (@person …), "" for none
    int item = -1;      // the argument's item in the chain, -1 when omitted or outside the sentence
};

struct AttachClause {
    int predItem = -1;  // the item holding the predicate
    int parent = -1;    // the clause it depends on, -1 for a main clause
    // Features of the clause as names and values (link, tense, pol, negfn, voice, force, vol, subj …).
    std::vector<std::pair<std::string, std::string>> features;
    std::vector<AttachArg> args;
};

struct AttachItem {
    int clause = -1;              // the clause whose span holds the item
    bool pred = false;            // the item holds that clause's predicate
    int argOf = -1, headItem = -1;  // as an argument: its clause and that clause's predicate item
    std::string role, how;
    int modifies = -1;            // the noun item it modifies
    int modKind = 0;              // 1: X の Y, 2: an attributive word or clause
};

// One per item of the chain, in chain order, and the clauses of the sentence.
struct Attachment {
    std::vector<AttachItem> items;
    std::vector<AttachClause> clauses;
};

// Fills out (which arrives empty) for chain, whose items stand in tokens: one AttachItem per item of
// the chain, every item counted; false leaves the packs layer to its own context.
struct AttachProvider {
    void* context = nullptr;
    bool (*attach)(void* context, const tokenizer::Tokens& tokens, const Chain& chain, Attachment& out) = nullptr;
};

}  // namespace hdTrans::word
