#pragma once
// Analysis by synthesis: the IR features of one predicate word whose lemma is known, found by generating every
// ending of eomi.tsv (with -시-, -었-, -겠-) and keeping the bundles whose surface equals the word. Used to
// read 0.7.x output into the IR (retrofit) and by the round-trip check of the ending decomposition.
#include <string>
#include <vector>

#include "hd8gen/generator.h"
#include "hd8gen/kir.h"

namespace hd8gen {

struct Bundle {
    Node node;          // kind V with lemma, ctype, mood, tense, modal, rel, eomi (family or id) set
    Level level = Level::none;  // the level the ending carries (final endings), none otherwise
    bool hon = false;
    std::string row;    // eomi row id that matched
};

enum class Want : uint8_t { any, final_, conn, adn, nom };

struct AnalyzeHints {
    Want want = Want::any;
    Mood mood = Mood::decl;  // preferred mood among finals with the same surface
    std::string host;        // the word before a copula
};

// All bundles whose generated surface equals `surface`, best first. pos/cls may be empty (looked up).
std::vector<Bundle> AnalyzeWord(const Tables&, const std::string& lemma, const std::string& pos, const std::string& cls,
                                const std::string& surface, const AnalyzeHints& = {});

// Every bundle of one lemma with its surface (the full paradigm over eomi.tsv), for callers that look up many
// words of the same lemma. host: the word before a copula ("" otherwise).
std::vector<std::pair<std::string, Bundle>> Paradigm(const Tables&, const std::string& lemma, const std::string& pos,
                                                     const std::string& cls, const std::string& host = "");
// Orders the bundles that share one surface by the hints, best first.
void RankBundles(std::vector<Bundle>&, const AnalyzeHints&);

// Generates the surface of one bundle (the inverse check).
std::string Realize(const Tables&, const Bundle&, const std::string& host = "");

}  // namespace hd8gen
