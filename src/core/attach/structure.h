#pragma once
// C (bunsetsu and heads) and L (clauses, features, arguments) over one analysis path.
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/attach/ir_view.h"

namespace hdTrans::attach {

enum class Wc : uint8_t {  // word class from data/ud.tsv
    none, noun, noun_adv, propn, person, num, na_aux, pron, verb, verb_dep, adj, adj_dep, na, adv, adn, conj,
    intj, filler, aux, p_case, p_topic, p_adv, p_conj, p_final, p_nom, pref, suf_n, suf_count, suf_v, suf_adj,
    suf_na, comma, period, open, close, sym, space
};

enum class Form : uint8_t {  // inflection class from data/forms.tsv
    none, mizen, renyo, renyo_onbin, renyo_fused, renyo_ni, shushi, rentai, katei, katei_fused, meirei, ishi,
    gokan, izen
};

// Roles that aux.tsv rows give to morphemes; the clause folder reads them.
enum class R : uint8_t {
    none, renyo, final, adn, vol, pot, cond, cop, cop_te, cop_ni, cop_ja, daro, neg, neg_n, zu, rare, caus, sou,
    you, te, wa, mo, eval, eval_neg, eval_ok, sumu, irareru, naru, light, ni, de, to, quote, shika, ka, nomi, q,
    fin_no, na_prohib, jan, mono, wake, hazu, tokoro, hou, say, compound, hon_prefix, comma, end, close, open,
    adj_ku, kaneru, absorb_neg, excl
};

enum class AuxKind : uint8_t { form, voice, tense, link, cop, modal, polite, neg, aspect, jreg, quote, bene, force,
                               aux, light, kase, topic, focus, final, nom, fn, suffix, prefix, punct };

// The feature assignments of one aux.tsv row; -1 = not set.
struct AuxSets {
    int8_t tense = -1, aspect = -1, pol = -1, voice = -1, bene = -1, force = -1, polite = -1, link = -1, kase = -1;
    int8_t linkSub = -1;  // LinkSub of link=time
    uint32_t mod = 0;
    uint16_t jreg = 0;
    R role = R::none;
};

struct AuxRow {
    uint32_t id = 0;  // interned "aux:<id>"
    std::vector<std::string> lemmas, surfs, uds, forms, ctypes;
    std::vector<std::pair<std::string, std::string>> afters;
    AuxKind kind = AuxKind::aux;
    AuxSets sets;
    int spec = 0;
};

struct StructureData {
    std::vector<AuxRow> aux;
    std::vector<std::pair<std::string, Wc>> ud;      // longest prefix wins
    std::vector<std::pair<std::string, Form>> forms; // longest prefix wins
    // data/heads.tsv: rule heads. A row's dep atoms test the dependent bunsetsu; the most specific matching row
    // names the head it takes (see heads.cpp). Level rows map a clause link to its scope level (A < B < C < D).
    struct HeadAtom {
        std::string key;
        std::vector<std::string> vals;
        bool neg = false;
    };
    struct HeadRule {
        std::string id;
        std::vector<HeadAtom> dep;
        std::string head;  // noun | pred | pred>=A|B|C|D | next | final | level=A|B|C|D
    };
    std::vector<HeadRule> headRules;
    // data/zero.tsv: constraint rules for the class of an omitted subject (Z). when atoms test clause facts; the
    // most specific matching row gives the class (speaker addressee third generic none).
    struct ZeroRule {
        std::string id, cls;
        std::vector<HeadAtom> when;
    };
    std::vector<ZeroRule> zeroRules;
    std::unordered_map<std::string, std::string> persons;     // data/persons.tsv: lemma → speaker|addressee|third
    std::unordered_set<std::string> noBareObject;  // data/bareobj.tsv: verbs a bare noun before them is not the object of
};

// data/bareobj.tsv rows (lemma, note) into out.noBareObject.
void LoadBareObject(std::string_view tsv, StructureData& out);

// Per-token facts shared by C and L.
struct TokInfo {
    Wc wc = Wc::none;
    Form form = Form::none;
    const AuxRow* row = nullptr;   // function-morpheme row, if any
    const AuxRow* frow = nullptr;  // inflection row of the token's own form
    R role = R::none;              // role passed on to the next token
};

// A verb that is part of a compound particle (によって, について): a case, not a predicate.
inline bool IsParticleVerb(const TokInfo& t) { return t.row != nullptr && t.row->kind == AuxKind::kase && !t.row->afters.empty(); }

// Loads the tables from their TSV text. Returns false and sets err on a malformed row.
bool LoadStructureData(std::string_view auxTsv, std::string_view udTsv, std::string_view formsTsv,
                       Strings& strings, StructureData& out, std::string& err);

std::vector<TokInfo> Annotate(const StructureData& data, const Strings& strings, const std::vector<Token>& toks);

// C: bunsetsu, heads and head2. Decisions record low-margin heads.
void Chunk(const StructureData& data, const Strings& strings, const std::vector<TokInfo>& info, Path& path);
void AttachHeads(const StructureData& data, const Strings& strings, const std::vector<TokInfo>& info, Path& path);

// For every bunsetsu, the last bunsetsu of its sentence (a period outside brackets ends a sentence).
std::vector<int> SentenceEnds(const std::vector<TokInfo>& info, const Path& path);

// Per-bunsetsu facts the head model reads.
enum class Dt : uint8_t { adn, topic, pred, kase, adv, conj, other };
const char* DtName(Dt d);
const char* CaseName(Case k);
struct HeadFacts {
    Dt dt = Dt::other;
    bool hasNoun = false, hasPred = false, endsSentence = false, comma = false, opens = false, closes = false;
    bool kaseTopic = false;
    Case kase = Case::none;
    int depthIn = 0, depthOut = 0, sentEnd = 0;
    std::string last, form, cwc, clemma;
    std::string link;  // the clause link the bunsetsu ends in (aux.tsv link names), "adn", "renyo" or "final"
    char level = 0;    // scope level of that link (data/heads.tsv level rows), 0 = not a clause end
    bool nextNoun = false;  // the next bunsetsu is a noun
};

// data/heads.tsv rows into out.headRules; false and err on a malformed row.
bool LoadHeadRules(std::string_view tsv, StructureData& out, std::string& err);
std::vector<HeadFacts> ComputeHeadFacts(const StructureData& data, const Strings& strings, const std::vector<TokInfo>& info,
                                        const Path& path);

// L: clauses with features and arguments.
void BuildClauses(const StructureData& data, Strings& strings, const std::vector<TokInfo>& info, Path& path,
                  std::vector<Decision>& decisions);

// The class of an omitted subject (Referent.kind values: none overt speaker addressee third generic).
void LoadPersons(std::string_view tsv, StructureData& out);
// data/zero.tsv rule rows into out.zeroRules; false and err on a malformed row.
bool LoadZeroRules(std::string_view tsv, StructureData& out, std::string& err);
// The omitted-subject class of clause ci by data/zero.tsv: "overt" when the clause has a subject argument, else
// the class of the most specific matching row ("" when none matches); rule gets the row id.
std::string ZeroByRules(const StructureData& d, const Strings& st, const std::vector<TokInfo>& info, const Path& path,
                        int ci, bool dialogue, std::string& rule);
const char* ZeroName(uint8_t kind);
bool ParseZeroClass(std::string_view s, uint8_t& kind);

// Names of enum values, as the attachment features write them (cl.link=te, main.force=imp).
const char* Name(Wc v);
const char* Name(Link v);
const char* Name(LinkSub v);
const char* Name(Tense v);
const char* Name(Aspect v);
const char* Name(Pol v);
const char* Name(NegFn v);
const char* Name(Voice v);
const char* Name(Bene v);
const char* Name(Force v);
const char* Name(Polite v);
const char* Name(Case v);
const char* Name(Role v);
const char* Name(ArgHow v);
std::vector<const char*> ModalityNames(uint32_t bits);
std::vector<const char*> JregNames(uint16_t bits);

}  // namespace hdTrans::attach
