// Native T (DESIGN-v3 §3): rule rows in one DSL, factorised by grammatical dimension (data/xfer/<dim>.tsv), matched
// over the analysis with the most specific row winning, plus the level lexicons for content words. A sentence is
// covered when every token is consumed by a row or by a lexicon entry; the route reads that.
//
// Row columns: id spec match when then ko st prov note (cases in <file>.cases.tsv: id fix|keep ja ko src).
//   match  atoms over consecutive tokens, "-" for none:
//            {N} {V} {A} {Na} {X} {C}   a content token (noun, verb, い-adjective, な-adjective, any predicate head,
//                                       any content word), optionally {N:@class}; {NP} a noun or a nominaliser
//                                       (の, こと); slots are not consumed
//            lemma | lemma/pos | a|b/pos   a token by its analyzer lemma (and IR pos); consumed
//            [fact,fact]                a token whose aux.tsv rows set every fact (pol=neg, kase=wo, aux.p.wo,
//                                       kind=punct, form.role=adn); consumed
//            a trailing ? makes an atom optional
//   when   conditions joined by spaces, each path=v|v or path!=v ("absent", "present" test for a value):
//            utt (dialogue | narration), utt.level (S's source level); cl.<feature> and main.<feature> (tense pol negFn force polite link linkSub
//            aspect voice bene mod; args = the roles of its overt arguments; has = a lemma of the clause's tokens;
//            endform = the form its predicate ends in); mainpred.<key> the main clause's predicate word;
//            <slot>.<key> and pred.<key>, head.<key>, next.<key>, prev.<key> with key
//            lemma pos form ko cls kcls vpos trans (+ when its frame takes an object) or a lexicon feature
//   then   assignments target.field=value joined by ";" (target: a slot, or pred = the clause's last predicate node);
//          line.route=fallback holds the line for 0.7.x (a shape the native rows do not translate yet); pred?.f sets
//          the field only when the clause has a predicate node
//   ko     node templates joined by spaces, inserted at the row's first consumed token: Kind:lemma{field=value,flag};
//          P:<kase> or P:=<particle>, Punct:<kind>; _ in a lemma is a space
// Specificity, compared left to right: lemma atoms (match literals, facts naming an aux.tsv row, lemma/ko/has
// conditions), consumed tokens (a row whose consumed morphemes contain another's is the more specific one), class
// depth, feature atoms (match facts and other conditions), matched atoms. Accepted greedily by specificity; a candidate that
// shares a consumed token with an accepted one is dropped; per node field the most specific assignment wins.
#pragma once

#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ir/stages.h"

namespace hd08::nat {

struct Atom {
    enum class K : uint8_t { Slot, Lemma, Fact } k = K::Lemma;
    std::string slot, cls;               // Slot: N V A Na X C; class atom
    std::vector<std::string> lemmas;     // Lemma: alternatives
    std::string pos;                     // Lemma: IR pos, "" any
    std::vector<std::string> facts;      // Fact: all must hold
    bool optional = false;
};

struct Cond {
    std::string target, key;             // utt | cl | main | slot | pred | head | next | prev ; feature key
    bool negate = false;
    std::vector<std::string> values;
};

struct Assign {
    std::string target, field, value;
};

struct KoNode {
    std::string kind, lemma;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct Row {
    std::string id, spec, file, st, prov, note, match, when, then, ko;
    std::vector<Atom> atoms;
    std::vector<Cond> conds;
    std::vector<Assign> sets;
    std::vector<KoNode> nodes;
    std::array<int, 5> specificity{};
    size_t order = 0;
    std::string phrase;  // a lexicon phrase (lexicon/lex/phrases-curriculum.tsv): its Japanese pattern
};

struct Case {
    std::string id, kind, ja, ko, src, file;
};

struct Sense {
    std::string ko, cue, frame, st, prov;
    std::map<std::string, std::string> feat;
    int sense = 1;
    bool stand = false;  // sense 1 of a level list, replaced by the lemma's sense rows
};
struct Entry {
    std::string ja, yomi, pos, file;
    std::map<std::string, std::string> feat;  // features of the lemma (level lists): vol asp te cls anim …
    std::vector<Sense> senses;
};

struct Data {
    std::vector<Row> rows;
    std::vector<Case> cases;
    std::vector<Entry> entries;
    std::unordered_map<std::string, std::vector<size_t>> byLemma;   // ja → entries
    std::unordered_map<std::string, std::string> classParent;      // @class → parent ("" root)
    std::map<int, std::array<std::string, 3>> numerals;            // value → native, adnominal, Sino-Korean
    std::map<std::string, int> digits, units, alone;               // kanji digits and units; counters read alone
    std::string predicateEnd;                                      // the ending of a Korean predicate lemma
    std::string lightVerb;                                         // the Korean light verb (noun + it is a verb)
    std::unordered_map<std::string, size_t> supp;                  // Korean lemma → its supp row
    std::unordered_set<std::string> koNouns;                       // Korean glosses of noun entries (공부, 이야기)
    std::vector<std::string> errors;
    const StructureData* structure = nullptr;  // aux.tsv reading of tokens (owned by TransferData)

    int ClassDepth(const std::string& cls) const;
    bool IsA(const std::string& cls, const std::string& ancestor) const;
};

// Reads data/xfer/*.tsv under transferDir, the level lexicons (lexicon/lex/levels/*.tsv, then the sense rows of
// lexicon/lex/lexicon/curriculum.tsv) and the classes (lexicon/lex/classes.tsv) under dataRoot. Errors go to d.errors (a row with an error does not load).
void Load(const std::string& dataRoot, Data& d);

// Spec ids defined in the markdown files under specDir (headings and table cells of the form AREA-N, N5-H3).
std::vector<std::string> SpecIds(const std::string& specDir);

// Writes line.kir from the line's analysis, appends the decisions, sets line.route (1 native when covered, else 2)
// and line.unmatched.
void Transfer(const Data& d, Doc& doc, Line& line);

}  // namespace hd08::nat
