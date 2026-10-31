#pragma once
// The Korean generator: lemma, class and abstract endings and particles to text, with spacing
// (ko/*.tsv), and its inverse for one word (analysis by synthesis).
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/register/ir.h"
#include "core/register/tables.h"

namespace hdTrans::reg {

struct PredicateInfo {
    std::string pos, cls;
    bool ae = false, honSi = false;
};
// The pos and class of a predicate lemma ("가다", or "가다:REG" with the class).
PredicateInfo LookupPredicate(const Tables& t, const std::string& lemma);

// A predicate in one word: the lemma with the endings, each an eomi.tsv id or a literal notation
// ("-었-", "-어요", "(으)면"). pos and cls may be empty (looked up). host: the word before a copula.
std::string ConjugateForms(const Tables& t, const std::string& lemma, const std::string& pos, const std::string& cls,
                           const std::vector<std::string>& forms, const std::string& host = "");

// A particle notation (이/가, (으)로, (이)나, 도) after a word.
std::string RealizeParticle(const std::string& form, std::u32string_view prev);

// The ending form (eomi.tsv notation) the generator writes a V node with at level; honIncl: the form holds -시-.
std::string EndingForm(const Tables& t, const Node& n, Level level, Persona persona, bool& honIncl);

// The text of nodes at the levels of reg (one per V node) and of the utterance.
std::string Generate(const Tables& t, const std::vector<Node>& nodes, const std::vector<Reg>& reg, const UttLevel& utt);

// One reading of a predicate word: a V node with lemma, ctype, mood, tense, modal, rel and eomi.
struct Bundle {
    Node node;
    Level level = Level::none;  // the level a final ending carries
    bool hon = false;
};

enum class Want : uint8_t { any, final_, conn, adn, nom };

struct AnalyzeHints {
    Want want = Want::any;
    Mood mood = Mood::decl;  // preferred among finals with the same surface
    std::string host;        // the word before a copula
};

// The readings of a paradigm (every ending of eomi.tsv, with -었-, -겠- and -시- on each) for one pos, and
// what the surface of each one ends in; made once and shared.
struct ParadigmShape {
    struct Reading {
        uint32_t row = 0;   // eomi.tsv row
        uint32_t tail = 0;  // tails index, kAnyTail when the ending does not end the surface
        Mood mood = Mood::decl;
        uint8_t pre = 0;    // 1 -었-, 2 -겠-, 4 -시-
    };
    static constexpr uint32_t kAnyTail = 0xFFFFFFFF;
    std::vector<Reading> readings;
    std::vector<std::u32string> tails;
};
// copula: the lemma is 이다, whose ending's first syllable changes after a vowel.
const ParadigmShape& ShapeOf(const Tables& t, const std::string& pos, bool copula);

// The paradigm of one lemma after host; a surface is generated only for the readings whose ending can end
// the word looked up, and once.
class ParadigmIndex {
public:
    ParadigmIndex(const Tables& t, const std::string& lemma, const std::string& pos, const std::string& cls,
                  const std::string& host);
    // The readings whose surface is surface, in paradigm order.
    std::vector<Bundle> Find(const std::string& surface);
    // Every reading with its surface, in paradigm order.
    std::vector<std::pair<std::string, Bundle>> All();

private:
    Bundle BundleOf(const ParadigmShape::Reading& r) const;
    const std::string& SurfaceOf(std::size_t i);

    const Tables& t_;
    std::string lemma_, pos_, cls_, host_;
    const ParadigmShape* shape_ = nullptr;
    std::vector<std::string> surfaces_;
    std::vector<bool> realized_;
};

// Orders readings that share one surface, best first.
void RankBundles(std::vector<Bundle>& bundles, const AnalyzeHints& hints);

}  // namespace hdTrans::reg
