#pragma once
// The tables of the speech-level stage (src/core/register/data/), compiled in and read once.
//   ko/        the Korean generator: endings, conjugation, particles, lemmas, spacing, marks
//   register/  the level decision: cues, levels (spec/register.md), voices, addressee -시-
//   retro/     words of the word stage's endings and particle notations the reader takes apart
//   reader.tsv the reader of the translation and the dialogue and caption detectors
//   keigo.tsv  the keigo and command-form rows of reader.tsv as the word stage's words spell them
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/register/ir.h"

namespace hdTrans::reg {

struct EomiRow {
    std::string id, slot, family, mood, level, pos, form;
    bool honIncl = false, words = false;
};
struct IrrRow {
    std::string cls, before;
    std::vector<std::string> stem, ending;
};
struct ContractRow {
    char32_t stemSyllable = 0;                       // a whole syllable (하, 시) or 0
    int stemVowel = -1, endVowel = -1, result = -1;  // result -1: keep both
    std::string mode;
    bool initIeung = false, nextYo = false, prefinal = false;
};
struct ParticleRow {
    std::string id, kase, form;
    std::vector<std::string> when;
};
struct StemRow {
    std::string lemma, pos, cls;
    bool ae = false, whole = false, honSi = false;
};
struct ClassRule {
    std::string kind, value, pos, cls;
};
struct WordRow {
    std::string lemma, kind, attach;
};
struct SpokenRow {
    std::string host, next, result, mode;
};
struct PunctRow {
    std::string kind, jaMark, ko, attach;
};
struct PersonaRow {
    std::string persona, level, mood, family;
};

// ko/*.tsv.
struct Tables {
    std::vector<EomiRow> eomi;
    std::multimap<std::string, std::size_t> eomiBySlotFamily;  // "slot|family" → row
    std::multimap<std::string, std::size_t> eomiById;
    std::vector<IrrRow> irregular;
    std::vector<ContractRow> contract;
    std::vector<ParticleRow> particles;
    std::unordered_map<std::string, StemRow> stems;
    std::unordered_map<std::string, std::string> lemmaPos;
    std::vector<ClassRule> classRules;
    std::unordered_map<std::string, WordRow> words;  // "N:" or "V:" + lemma
    std::vector<SpokenRow> spoken;
    std::vector<PunctRow> punct;
    std::vector<PersonaRow> persona;
    std::unordered_map<std::string, std::string> grammar;
    // The grammar.tsv value of name, "" when there is none.
    const std::string& G(std::string_view name) const;
};

// register/*.tsv.
struct CueRow {
    std::string cue, mode;
    std::u32string text, notAfter;
    std::vector<std::u32string> notBefore;  // text that must not follow (男で|すまない: not です)
};
struct LevelRow {
    std::string key, spec;
    Level level = Level::hae;
    bool profile = false;
    bool keep = false;
    enum class Unquoted { no, yes, strong } unquoted = Unquoted::no;
};
struct TailRow {
    std::u32string text, notAfter;
    bool strong = false;
};
struct VoiceRow {
    std::string type;
    std::u32string key;
    Persona persona = Persona::none;
};
struct AuthRow {
    std::vector<std::vector<std::u32string>> parts;  // every part (one of its alternatives) must occur
    bool speaker = false;                            // marks the speaker in the scene, not only the line
    std::u32string notAfter;                         // characters that must not come right before a part
};
struct RegisterTables {
    std::vector<VoiceRow> voices;
    std::vector<CueRow> cues;
    std::vector<TailRow> tails;
    std::vector<std::u32string> quotatives;
    std::vector<std::u32string> exclaim;
    std::vector<AuthRow> auth;
    std::map<std::string, LevelRow> levels;
    std::vector<std::string> honExclude;
    std::vector<std::pair<std::string, std::u32string>> honBack;  // sonkei | give, Japanese text
};

// retro/words.tsv and the lemma index of ko/lemmas.tsv.
struct RetroWords {
    std::vector<std::string> deps;       // dependent nouns, longest first
    std::vector<std::string> auxLemmas;  // predicates that follow inside an ending row
    std::map<std::string, std::string> particleKase;  // surface → kase or lex:form
    std::map<std::string, std::string> jima;          // 마, 마라 … → level
    std::map<std::string, std::string> fused;         // 건 걸 게 겁 → what fused onto 거
    std::unordered_map<char32_t, std::vector<std::string>> lemmasByFirst;  // longest first
};

// reader.tsv.
struct Reader {
    std::map<std::u32string, std::string> punctJa;                              // mark → kind
    std::map<std::u32string, std::pair<std::string, std::u32string>> punctKo;  // Korean mark → kind, mark
    std::set<char32_t> sym;
    std::set<std::u32string> jaParticles, copulaNot, deps, glueLemmas, impEnds, partAlts;
    std::set<char32_t> impTail, impClose;
    char32_t stutterMark = 0;
    std::u32string copulaAlt, copulaLemma, lemmaEnd;
    std::vector<std::pair<char32_t, char32_t>> quotes, tags;
    std::set<char32_t> captionMarks;
    std::vector<std::string> keigo;        // the lemmas of the keigo rows
    std::vector<std::string> keigoPrefix;  // prefix+verb of the keigo-prefix rows
    std::vector<std::string> commandForms;
};

// keigo.tsv: one row of reader.tsv as the source spells the word.
struct KeigoWord {
    enum class Kind { keigo, prefix, command } kind = Kind::keigo;
    std::vector<std::u32string> start, notStart;
};

struct Data {
    Tables ko;
    RegisterTables levels;
    RetroWords retro;
    Reader reader;
    std::vector<KeigoWord> keigo;
};

// The compiled-in tables; null when one of them does not read (DataError says why).
const Data* LoadedData();
const std::string& DataError();

// Splits s at every sep.
std::vector<std::string> Split(std::string_view s, char sep);

}  // namespace hdTrans::reg
