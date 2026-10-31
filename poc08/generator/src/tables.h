#pragma once
// In-memory form of data/ko/*.tsv.
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "hd8gen/generator.h"

namespace hd8gen {

struct TsvTable {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
    std::vector<int> lines;
    int Col(const std::string& name) const;
};
// Reads a TSV file: `#` lines are comments, the first other line is the header.
bool ReadTsv(const std::string& path, TsvTable& out, std::string& err);
std::vector<std::string> Split(const std::string& s, char sep);

struct EomiRow {
    std::string id, slot, family, mood, level, pos, form;
    bool honIncl = false, words = false;
};
struct IrrRow {
    std::string cls, before;
    std::vector<std::string> stem, ending;
};
struct ContractRow {
    char32_t stemSyllable = 0;  // a whole syllable (하, 시) or 0
    int stemVowel = -1, endVowel = -1, result = -1;  // result -1: keep both
    std::string mode;                                // must default never
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

struct Tables {
    std::vector<EomiRow> eomi;
    std::multimap<std::string, size_t> eomiBySlotFamily;  // "slot|family" → row
    std::multimap<std::string, size_t> eomiById;
    std::vector<IrrRow> irregular;
    std::vector<ContractRow> contract;
    std::vector<ParticleRow> particles;
    std::unordered_map<std::string, StemRow> stems;
    size_t maxStemLen = 0;  // in bytes
    std::unordered_map<std::string, std::string> lemmaPos;
    std::vector<ClassRule> classRules;
    std::unordered_map<std::string, WordRow> words;
    std::vector<SpokenRow> spoken;
    std::vector<PunctRow> punct;
    std::vector<PersonaRow> persona;
    std::unordered_map<std::string, std::string> grammar;
    std::unordered_map<std::string, std::string> lemmaClasses;  // lemma → its classes (ko/classes.tsv), | joined
    std::vector<std::pair<std::string, std::string>> suffixClasses;  // (lemma ending, class): rows ~ending
    std::set<std::string> classNames;
    // the classes of a lemma, | joined (exact rows, then lemma endings)
    std::string ClassesOf(const std::string& lemma) const;
    const std::string& G(const std::string& name) const;
};

}  // namespace hd8gen
