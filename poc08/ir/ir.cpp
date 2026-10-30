#include "ir.h"

#include <array>
#include <span>

namespace hd08 {

namespace {

#define HD08_NAME(id, name) std::string_view(name),
#define HD08_TABLE(var, list) constexpr std::string_view var[] = {list(HD08_NAME)};
HD08_TABLE(kEv, HD08_EV)
HD08_TABLE(kPos, HD08_POS)
HD08_TABLE(kTokFlag, HD08_TOKFLAG)
HD08_TABLE(kEdit, HD08_EDIT)
HD08_TABLE(kSrcLevel, HD08_SRCLEVEL)
HD08_TABLE(kCase, HD08_CASE)
HD08_TABLE(kBunType, HD08_BUNTYPE)
HD08_TABLE(kBunFlag, HD08_BUNFLAG)
HD08_TABLE(kRole, HD08_ROLE)
HD08_TABLE(kHow, HD08_HOW)
HD08_TABLE(kLink, HD08_LINK)
HD08_TABLE(kTense, HD08_TENSE)
HD08_TABLE(kJAspect, HD08_JASPECT)
HD08_TABLE(kPol, HD08_POL)
HD08_TABLE(kNegFn, HD08_NEGFN)
HD08_TABLE(kVoice, HD08_VOICE)
HD08_TABLE(kBene, HD08_BENE)
HD08_TABLE(kModality, HD08_MODALITY)
HD08_TABLE(kForce, HD08_FORCE)
HD08_TABLE(kPolite, HD08_POLITE)
HD08_TABLE(kJReg, HD08_JREG)
HD08_TABLE(kKK, HD08_KK)
HD08_TABLE(kKCls, HD08_KCLS)
HD08_TABLE(kSem, HD08_SEM)
HD08_TABLE(kKAspect, HD08_KASPECT)
HD08_TABLE(kMood, HD08_MOOD)
HD08_TABLE(kCType, HD08_CTYPE)
HD08_TABLE(kRel, HD08_REL)
HD08_TABLE(kKNeg, HD08_KNEG)
HD08_TABLE(kJaFact, HD08_JAFACT)
HD08_TABLE(kKase, HD08_KASE)
HD08_TABLE(kPunct, HD08_PUNCT)
HD08_TABLE(kLevel, HD08_LEVEL)
HD08_TABLE(kPersona, HD08_PERSONA)
HD08_TABLE(kFlag, HD08_FLAG)
HD08_TABLE(kSt, HD08_ST)
HD08_TABLE(kRefKind, HD08_REFKIND)
HD08_TABLE(kTimeSub, HD08_TIMESUB)

std::span<const std::string_view> Table(F field) {
    switch (field) {
    case F::ev: return kEv;
    case F::pos: return kPos;
    case F::tokflag: return kTokFlag;
    case F::edit: return kEdit;
    case F::srclevel: return kSrcLevel;
    case F::kase_ja: return kCase;
    case F::buntype: return kBunType;
    case F::bunflag: return kBunFlag;
    case F::role: return kRole;
    case F::how: return kHow;
    case F::link: return kLink;
    case F::tense: return kTense;
    case F::jaspect: return kJAspect;
    case F::pol: return kPol;
    case F::negfn: return kNegFn;
    case F::voice: return kVoice;
    case F::bene: return kBene;
    case F::modality: return kModality;
    case F::force: return kForce;
    case F::polite: return kPolite;
    case F::jreg: return kJReg;
    case F::kk: return kKK;
    case F::kcls: return kKCls;
    case F::sem: return kSem;
    case F::kaspect: return kKAspect;
    case F::mood: return kMood;
    case F::ctype: return kCType;
    case F::rel: return kRel;
    case F::kneg: return kKNeg;
    case F::jafact: return kJaFact;
    case F::kase: return kKase;
    case F::punct: return kPunct;
    case F::level: return kLevel;
    case F::persona: return kPersona;
    case F::flag: return kFlag;
    case F::st: return kSt;
    case F::refkind: return kRefKind;
    case F::timesub: return kTimeSub;
    }
    return {};
}

}  // namespace

std::string_view Name(F field, unsigned value) {
    const auto t = Table(field);
    return value < t.size() ? t[value] : std::string_view();
}

bool Parse(F field, std::string_view name, unsigned& value) {
    const auto t = Table(field);
    for (unsigned i = 0; i < t.size(); i++) {
        if (t[i] == name) {
            value = i;
            return true;
        }
    }
    return false;
}

bool IsBits(F field) {
    return field == F::tokflag || field == F::bunflag || field == F::modality || field == F::jreg ||
           field == F::sem || field == F::jafact;
}

unsigned Count(F field) { return static_cast<unsigned>(Table(field).size()); }

Strings::Strings() {
    all.emplace_back();
    index.emplace(std::string_view(all.back()), 0u);
}

Strings::Strings(const Strings& other) : all(other.all) { Reindex(); }

Strings& Strings::operator=(const Strings& other) {
    if (this != &other) {
        all = other.all;
        Reindex();
    }
    return *this;
}

void Strings::Reindex() {
    index.clear();
    for (uint32_t i = 0; i < all.size(); i++) index.emplace(std::string_view(all[i]), i);
}

uint32_t Strings::Intern(std::string_view s) {
    if (const auto it = index.find(s); it != index.end()) return it->second;
    const auto id = static_cast<uint32_t>(all.size());
    all.emplace_back(s);
    index.emplace(std::string_view(all.back()), id);
    return id;
}

uint32_t Strings::Find(std::string_view s) const {
    const auto it = index.find(s);
    return it == index.end() ? 0u : it->second;
}

const KNode* FindNode(const std::vector<KNode>& nodes, uint32_t id) {
    if (id == 0) return nullptr;
    // Nodes are usually numbered 1..n in order; fall back to a scan otherwise.
    if (id <= nodes.size() && nodes[id - 1].id == id) return &nodes[id - 1];
    for (const KNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

}  // namespace hd08
