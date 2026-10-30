// W and G for the 0.8 driver (poc08/ir/stages.h): hd08::KNode ↔ hd8gen::Node by the IR names, then the
// rewrite rules (W) and the generator (G). Data: <data>/generator/data/ko.
#include <memory>

#include "hd8gen/generator.h"
#include "hd8gen/rewrite.h"
#include "ir/invariants.h"
#include "ir/stages.h"
#include "kir_text.h"

namespace hd08 {

struct GeneratorData {
    std::shared_ptr<const hd8gen::Tables> tables;
    std::shared_ptr<const hd8gen::RuleSet> rules;
    InvariantData invariants;
};

GeneratorData* LoadGenerator(const Config& cfg, std::string& err) {
    auto* g = new GeneratorData;
    g->tables = hd8gen::LoadTables(cfg.data + "/generator/data/ko", err);
    if (g->tables) g->rules = hd8gen::LoadRules(cfg.data + "/generator/data/ko/rewrite", err);
    if (!g->tables || !g->rules || !LoadInvariantData(cfg.data, g->invariants, err)) {
        delete g;
        return nullptr;
    }
    return g;
}
void FreeGenerator(GeneratorData* data) { delete data; }

namespace {

std::string N(F f, unsigned v) { return std::string(Name(f, v)); }

hd8gen::Node FromIr(const Doc& doc, const KNode& k) {
    hd8gen::Node n;
    n.id = k.id;
    n.origin = k.origin ? k.origin : k.id;
    hd8gen::SetFeature(n, "kind", N(F::kk, static_cast<unsigned>(k.kind)));
    n.lemma = std::string(doc.S(k.lemma));
    if (k.kind == KK::V) {
        if (k.kcls) n.kcls = N(F::kcls, k.kcls);  // REG (0) is left to the stem tables
        hd8gen::SetFeature(n, "tense", N(F::tense, k.tense));
        hd8gen::SetFeature(n, "aspect", N(F::kaspect, k.aspect));
        hd8gen::SetFeature(n, "mood", N(F::mood, k.mood));
        hd8gen::SetFeature(n, "ctype", N(F::ctype, k.ctype));
        if (k.ctype == ctype::conn) n.rel = N(F::rel, k.rel);
        hd8gen::SetFeature(n, "neg", N(F::kneg, k.neg));
        n.aux = k.aux;
        n.passive = k.passive;
        n.causative = k.causative;
        n.potential = k.potential;
        n.spont = k.spont;
    }
    n.dep = k.dep;
    n.sem = k.sem;
    if (k.kind == KK::P) {
        const std::string kase = N(F::kase, k.kase);
        n.kase = kase == "LEX" ? "lex" : kase;
    }
    if (k.kind == KK::Punct) {
        hd8gen::SetFeature(n, "punct", N(F::punct, k.punct));
        n.jaMark = std::string(doc.S(k.jaMark));
    }
    n.src = {k.src.begin, k.src.end};
    n.clause = k.clause;
    n.utt = k.utt;
    n.decision = k.decision;
    return n;
}

KNode ToIr(Doc& doc, const hd8gen::Node& n) {
    KNode k;
    k.id = n.id;
    k.origin = n.origin;
    unsigned v = 0;
    if (Parse(F::kk, hd8gen::GetFeature(n, "kind"), v)) k.kind = static_cast<KK>(v);
    k.lemma = doc.I(n.lemma);
    if (!n.kcls.empty() && Parse(F::kcls, n.kcls, v)) k.kcls = static_cast<uint8_t>(v);
    if (n.kind == hd8gen::Kind::V) {
        if (Parse(F::tense, hd8gen::GetFeature(n, "tense"), v)) k.tense = static_cast<uint8_t>(v);
        if (Parse(F::kaspect, hd8gen::GetFeature(n, "aspect"), v)) k.aspect = static_cast<uint8_t>(v);
        if (Parse(F::mood, hd8gen::GetFeature(n, "mood"), v)) k.mood = static_cast<uint8_t>(v);
        std::string ct = hd8gen::GetFeature(n, "ctype");
        if ((ct == "adn" || ct == "nom") && !n.rel.empty()) ct += ":" + n.rel;
        if (Parse(F::ctype, ct, v)) k.ctype = static_cast<uint8_t>(v);
        if (n.ctype == hd8gen::CType::conn && Parse(F::rel, n.rel, v)) k.rel = static_cast<uint8_t>(v);
        if (Parse(F::kneg, hd8gen::GetFeature(n, "neg"), v)) k.neg = static_cast<uint8_t>(v);
        k.aux = n.aux;
        k.passive = n.passive;
        k.causative = n.causative;
        k.potential = n.potential;
        k.spont = n.spont;
    }
    k.dep = n.dep;
    k.sem = n.sem;
    if (n.kind == hd8gen::Kind::P && Parse(F::kase, n.kase == "lex" ? "LEX" : n.kase, v)) k.kase = static_cast<uint8_t>(v);
    if (n.kind == hd8gen::Kind::Punct) {
        if (Parse(F::punct, hd8gen::GetFeature(n, "punct"), v)) k.punct = static_cast<uint8_t>(v);
        k.jaMark = doc.I(n.jaMark);
    }
    k.src = {n.src.begin, n.src.end};
    k.clause = n.clause;
    k.utt = n.utt;
    k.decision = n.decision;
    return k;
}

hd8gen::Level LevelOf(uint8_t l) {
    hd8gen::Level out = hd8gen::Level::haera;
    hd8gen::ParseLevel(N(F::level, l), out);
    return out;
}

void Inputs(const Doc& doc, const Line& line, const std::vector<KNode>& kir, std::vector<hd8gen::Node>& nodes,
            std::vector<hd8gen::Reg>& reg, std::vector<hd8gen::UttLevel>& lev) {
    for (const KNode& k : kir) nodes.push_back(FromIr(doc, k));
    for (const KReg& r : line.reg) {
        hd8gen::Reg g;
        g.node = r.node;
        g.level = LevelOf(r.level);
        hd8gen::ParsePersona(N(F::persona, r.persona), g.persona);
        g.hon = r.hon;
        reg.push_back(g);
    }
    for (const UttLevel& u : line.lev) {
        hd8gen::UttLevel x;
        x.utt = u.utt;
        x.level = LevelOf(u.level);
        hd8gen::ParsePersona(N(F::persona, u.persona), x.persona);
        x.narration = u.utt < line.utts.size() && line.utts[u.utt].narration;
        lev.push_back(x);
    }
}

}  // namespace

void StageW(const GeneratorData& g, const Config&, Doc& doc, Line& line) {
    std::vector<hd8gen::Node> nodes;
    std::vector<hd8gen::Reg> reg;
    std::vector<hd8gen::UttLevel> lev;
    Inputs(doc, line, line.kir, nodes, reg, lev);
    const hd8gen::RewriteResult rw = hd8gen::Rewrite(*g.rules, *g.tables, nodes, reg, lev);
    line.kir2.clear();
    for (const auto& n : rw.kir2) line.kir2.push_back(ToIr(doc, n));
    for (const auto& f : rw.fired) {
        Decision d;
        d.stage = 'W';
        d.kind = Ev::Cued;
        d.rule = doc.I("rw:" + f.rule);
        line.Decide(d);
    }
    // R's register decisions follow the rewritten nodes (a rule may set level or hon)
    for (const auto& r : rw.reg)
        for (KReg& k : line.reg)
            if (k.node == r.node) {
                unsigned v = 0;
                if (Parse(F::level, hd8gen::Name(r.level), v)) k.level = static_cast<uint8_t>(v);
                k.hon = r.hon;
            }
}

void StageG(const GeneratorData& g, const Config&, Doc& doc, Line& line) {
    std::vector<hd8gen::Node> nodes;
    std::vector<hd8gen::Reg> reg;
    std::vector<hd8gen::UttLevel> lev;
    Inputs(doc, line, line.kir2, nodes, reg, lev);
    const hd8gen::GenResult r = hd8gen::Generate(*g.tables, nodes, reg, lev);
    line.out = r.out;
    line.spans.clear();
    for (const auto& s : r.spans) line.spans.push_back({s.begin, s.end, s.node});
    CheckInvariants(doc, line, g.invariants, line.flags);
}

}  // namespace hd08
