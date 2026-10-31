#include <iterator>
#include "kir_text.h"

#include "ir_json.h"
#include "tables.h"

namespace hd8gen {

namespace {
template <typename E, size_t K>
bool FromNames(const std::string& s, const char* const (&names)[K], E& out) {
    for (size_t i = 0; i < K; ++i)
        if (s == names[i]) {
            out = static_cast<E>(i);
            return true;
        }
    return false;
}
const char* const kKinds[] = {"N", "P", "V", "Adv", "Det", "Intj", "Punct", "Raw"};
const char* const kCTypes[] = {"final", "conn", "adn", "nom", "quote", "stem"};
const char* const kAspects[] = {"simple", "prog", "result", "compl", "dir-come", "dir-go", "try", "give"};
const char* const kMoods[] = {"decl", "q", "imp", "prop", "excl"};
const char* const kNegs[] = {"none", "an", "ji-anh", "mot", "ji-mot", "ji-ma"};
const char* const kPuncts[] = {"none", "comma", "pause", "end", "q", "excl", "qopen", "qclose", "ellipsis", "dash", "mark"};
const char* const kSems[] = {"person", "place", "time", "counter", "num", "sino", "native", "animate", "thing"};
bool Flag(const std::string& v) { return v == "1" || v == "true" || v == "yes"; }
}  // namespace

bool SetFeature(Node& n, const std::string& k, const std::string& v) {
    if (k == "lemma") n.lemma = v;
    else if (k == "kind") return FromNames(v, kKinds, n.kind);
    else if (k == "tense") n.tense = v == "past" ? Tense::past : Tense::nonpast;
    else if (k == "aspect") return FromNames(v, kAspects, n.aspect);
    else if (k == "mood") return FromNames(v, kMoods, n.mood);
    else if (k == "ctype") {
        std::string c = v;
        const size_t colon = c.find(':');
        if (colon != std::string::npos) {
            n.rel = c.substr(colon + 1);
            c = c.substr(0, colon);
        }
        return FromNames(c, kCTypes, n.ctype);
    } else if (k == "rel") n.rel = v == "-" ? "" : v;
    else if (k == "eomi") n.eomi = v == "-" ? "" : v;
    else if (k == "modal") n.modal = v == "-" ? "" : v;
    else if (k == "neg") return FromNames(v, kNegs, n.neg);
    else if (k == "kase") n.kase = v;
    else if (k == "aux") n.aux = Flag(v);
    else if (k == "dep") n.dep = Flag(v);
    else if (k == "glue") n.glue = Flag(v);
    else if (k == "passive") n.passive = Flag(v);
    else if (k == "causative") n.causative = Flag(v);
    else if (k == "potential") n.potential = Flag(v);
    else if (k == "spont") n.spont = Flag(v);
    else if (k == "vpos") n.vpos = v;
    else if (k == "kcls") n.kcls = v;
    else if (k == "jaMark") n.jaMark = v;
    else if (k == "punct") return FromNames(v, kPuncts, n.punct);
    else if (k == "utt") n.utt = static_cast<uint16_t>(std::atoi(v.c_str()));
    else if (k == "clause") n.clause = static_cast<uint16_t>(std::atoi(v.c_str()));
    else if (k == "sem") {
        n.sem = 0;
        for (const auto& s : Split(v, '|'))
            for (size_t i = 0; i < std::size(kSems); ++i)
                if (s == kSems[i]) n.sem |= 1u << i;
    } else return false;
    return true;
}

std::string GetFeature(const Node& n, const std::string& k) {
    if (k == "lemma") return n.lemma;
    if (k == "kind") return kKinds[static_cast<int>(n.kind)];
    if (k == "tense") return n.tense == Tense::past ? "past" : "nonpast";
    if (k == "aspect") return kAspects[static_cast<int>(n.aspect)];
    if (k == "mood") return kMoods[static_cast<int>(n.mood)];
    if (k == "ctype") return kCTypes[static_cast<int>(n.ctype)];
    if (k == "rel") return n.rel;
    if (k == "eomi") return n.eomi;
    if (k == "modal") return n.modal;
    if (k == "neg") return kNegs[static_cast<int>(n.neg)];
    if (k == "kase") return n.kase;
    if (k == "aux") return n.aux ? "1" : "0";
    if (k == "dep") return n.dep ? "1" : "0";
    if (k == "glue") return n.glue ? "1" : "0";
    if (k == "passive") return n.passive ? "1" : "0";
    if (k == "causative") return n.causative ? "1" : "0";
    if (k == "potential") return n.potential ? "1" : "0";
    if (k == "spont") return n.spont ? "1" : "0";
    if (k == "vpos") return n.vpos;
    if (k == "kcls") return n.kcls;
    if (k == "jaMark") return n.jaMark;
    if (k == "punct") return kPuncts[static_cast<int>(n.punct)];
    if (k == "sem") {
        std::string s;
        for (size_t i = 0; i < std::size(kSems); ++i)
            if (n.sem & (1u << i)) s += (s.empty() ? "" : "|") + std::string(kSems[i]);
        return s;
    }
    return "";
}

bool ParseKirText(const std::string& text, std::vector<Node>& out, std::string& err) {
    out.clear();
    uint32_t id = 0;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) break;
        // token: Kind:lemma{feats} ; lemma may hold spaces only inside {}
        size_t j = i;
        int depth = 0;
        while (j < text.size() && (depth > 0 || text[j] != ' ')) {
            if (text[j] == '{') ++depth;
            if (text[j] == '}') --depth;
            ++j;
        }
        const std::string tok = text.substr(i, j - i);
        i = j;
        const size_t colon = tok.find(':');
        if (colon == std::string::npos) {
            err = "no kind in " + tok;
            return false;
        }
        Node n;
        n.id = n.origin = ++id;
        if (!FromNames(tok.substr(0, colon), kKinds, n.kind)) {
            err = "bad kind in " + tok;
            return false;
        }
        std::string rest = tok.substr(colon + 1);
        std::string feats;
        const size_t brace = rest.find('{');
        if (brace != std::string::npos) {
            feats = rest.substr(brace + 1, rest.size() - brace - 2);
            rest = rest.substr(0, brace);
        }
        if (n.kind == Kind::P) {
            if (rest.rfind("lex=", 0) == 0) {
                n.kase = "lex";
                n.lemma = rest.substr(4);
            } else {
                n.kase = rest;
            }
        } else if (n.kind == Kind::Punct) {
            if (!FromNames(rest, kPuncts, n.punct)) {
                err = "bad punct " + rest;
                return false;
            }
        } else {
            n.lemma = rest;
        }
        if (!feats.empty())
            for (const auto& kv : Split(feats, ',')) {
                const size_t eq = kv.find('=');
                const std::string k = eq == std::string::npos ? kv : kv.substr(0, eq);
                const std::string v = eq == std::string::npos ? "1" : kv.substr(eq + 1);
                if (!SetFeature(n, k, v)) {
                    err = "bad feature " + kv + " in " + tok;
                    return false;
                }
            }
        out.push_back(n);
    }
    return true;
}

std::string KirText(const std::vector<Node>& nodes) {
    std::string s;
    for (const Node& n : nodes) {
        if (!s.empty()) s += ' ';
        s += kKinds[static_cast<int>(n.kind)];
        s += ':';
        if (n.kind == Kind::P) s += n.kase == "lex" ? "lex=" + n.lemma : n.kase;
        else if (n.kind == Kind::Punct) s += kPuncts[static_cast<int>(n.punct)];
        else s += n.lemma;
        std::string f;
        auto add = [&](const std::string& k, const std::string& v) { f += (f.empty() ? "" : ",") + k + "=" + v; };
        if (n.kind == Kind::V) {
            if (n.tense == Tense::past) add("tense", "past");
            if (n.aspect != Aspect::simple) add("aspect", kAspects[static_cast<int>(n.aspect)]);
            if (n.mood != Mood::decl) add("mood", kMoods[static_cast<int>(n.mood)]);
            if (n.ctype != CType::final_) add("ctype", kCTypes[static_cast<int>(n.ctype)]);
            if (!n.rel.empty()) add("rel", n.rel);
            if (!n.eomi.empty()) add("eomi", n.eomi);
            if (!n.modal.empty()) add("modal", n.modal);
            if (n.neg != Neg::none) add("neg", kNegs[static_cast<int>(n.neg)]);
            if (n.aux) add("aux", "1");
            if (n.passive) add("passive", "1");
            if (n.causative) add("causative", "1");
            if (n.potential) add("potential", "1");
            if (n.spont) add("spont", "1");
            if (!n.vpos.empty()) add("vpos", n.vpos);
            if (!n.kcls.empty()) add("kcls", n.kcls);
        }
        if (n.dep) add("dep", "1");
        if (n.glue) add("glue", "1");
        if (n.sem) add("sem", GetFeature(n, "sem"));
        if (!n.jaMark.empty()) add("jaMark", n.jaMark);
        if (n.utt) add("utt", std::to_string(n.utt));
        if (!f.empty()) s += "{" + f + "}";
    }
    return s;
}

}  // namespace hd8gen
