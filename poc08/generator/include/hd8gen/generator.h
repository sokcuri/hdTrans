#pragma once
// G: the Korean generator. Lemma + class + abstract endings and particles → surface, with spacing.
#include <memory>
#include <string>
#include <vector>

#include "hd8gen/kir.h"

namespace hd8gen {

struct Tables;  // ko/*.tsv, loaded once and shared read-only

// Loads every table of data/ko/ from dir. Returns null and sets err on a malformed row.
std::shared_ptr<const Tables> LoadTables(const std::string& dir, std::string& err);

struct GenOptions {
    bool attachAux = false;     // 한글 맞춤법 제47항 붙임: 해버렸다 instead of 해 버렸다
    bool uncontracted = false;  // 되어, 하여, 보아 instead of 돼, 해, 봐
};

struct GenResult {
    std::string out;
    std::vector<OutSpan> spans;     // out bytes → node id
    std::vector<std::string> notes; // generator problems (unknown ending, no row): empty when clean
};

// Generates one line from kir2 and the register decisions of R.
GenResult Generate(const Tables&, const std::vector<Node>& kir2, const std::vector<Reg>& reg,
                   const std::vector<UttLevel>& lev, const GenOptions& = {});

// A predicate in one word: lemma (with 다) + endings by id or by literal notation (e.g. "-었-", "-어요",
// "(으)면"). pos/cls may be empty (looked up). Used by tests and by the retrofit converter.
struct PredicateInfo { std::string pos, cls; bool ae = false, honSi = false; };
PredicateInfo LookupPredicate(const Tables&, const std::string& lemma);
std::string ConjugateForms(const Tables&, const std::string& lemma, const std::string& pos, const std::string& cls,
                           const std::vector<std::string>& forms, const std::string& host = "",
                           const GenOptions& = {});

// The form of an eomi row: slot + family + mood + level + pos, with the pos fallback chain. Empty if none.
std::string EomiForm(const Tables&, const std::string& slot, const std::string& family, const std::string& mood,
                     const std::string& level, const std::string& pos, bool* honIncl = nullptr);

// A particle after a word: kase (subj …) or a lexical particle in notation ("이/가", "(으)로").
std::string Particle(const Tables&, const std::string& prevWord, const std::string& kaseOrLemma, bool anim = false,
                     bool hon = false, bool spoken = false);

}  // namespace hd8gen
