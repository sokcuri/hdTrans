#include "ir_json.h"

namespace hd8gen {

namespace {
template <typename E, size_t K>
bool FromName(const std::string& s, const char* const (&names)[K], E& out) {
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
const char* const kSems[] = {"person", "place", "time", "counter", "num", "sino", "native", "animate"};
}  // namespace

const char* KindName(Kind k) { return kKinds[static_cast<int>(k)]; }
const char* CTypeName(CType c) { return kCTypes[static_cast<int>(c)]; }
const char* AspectName(Aspect a) { return kAspects[static_cast<int>(a)]; }
const char* NegName(Neg n) { return kNegs[static_cast<int>(n)]; }
const char* MoodName2(Mood m) { return kMoods[static_cast<int>(m)]; }
const char* PunctName(Punct p) { return kPuncts[static_cast<int>(p)]; }

bool NodeFromJson(const json::Value& v, Node& n, std::string& err) {
    n = Node{};
    n.id = static_cast<uint32_t>(v.Num("id"));
    n.origin = static_cast<uint32_t>(v.Num("origin", n.id));
    if (!FromName(v.Str("kind", "N"), kKinds, n.kind)) {
        err = "bad kind " + v.Str("kind");
        return false;
    }
    n.lemma = v.Str("lemma");
    n.kcls = v.Str("kcls");
    n.vpos = v.Str("vpos");
    if (const json::Value* s = v.Get("sem")) {
        for (const auto& x : s->arr)
            for (size_t i = 0; i < 8; ++i)
                if (x.str == kSems[i]) n.sem |= 1u << i;
    }
    n.tense = v.Str("tense") == "past" ? Tense::past : Tense::nonpast;
    if (v.Get("aspect") && !FromName(v.Str("aspect"), kAspects, n.aspect)) {
        err = "bad aspect " + v.Str("aspect");
        return false;
    }
    if (v.Get("mood") && !FromName(v.Str("mood"), kMoods, n.mood)) {
        err = "bad mood " + v.Str("mood");
        return false;
    }
    std::string ct = v.Str("ctype", "final");
    const size_t colon = ct.find(':');
    if (colon != std::string::npos) {
        n.rel = ct.substr(colon + 1);
        ct = ct.substr(0, colon);
    }
    if (!FromName(ct, kCTypes, n.ctype)) {
        err = "bad ctype " + ct;
        return false;
    }
    if (v.Get("rel")) n.rel = v.Str("rel");
    n.eomi = v.Str("eomi");
    n.modal = v.Str("modal");
    if (v.Get("neg") && !FromName(v.Str("neg"), kNegs, n.neg)) {
        err = "bad neg " + v.Str("neg");
        return false;
    }
    n.aux = v.Bool("aux");
    n.dep = v.Bool("dep");
    n.passive = v.Bool("passive");
    n.causative = v.Bool("causative");
    n.glue = v.Bool("glue");
    n.kase = v.Str("kase");
    if (v.Get("punct") && !FromName(v.Str("punct"), kPuncts, n.punct)) {
        err = "bad punct " + v.Str("punct");
        return false;
    }
    n.jaMark = v.Str("jaMark");
    if (const json::Value* s = v.Get("src"); s && s->arr.size() == 2) {
        n.src.begin = static_cast<uint32_t>(s->arr[0].num);
        n.src.end = static_cast<uint32_t>(s->arr[1].num);
    }
    n.clause = static_cast<uint16_t>(v.Num("clause"));
    n.utt = static_cast<uint16_t>(v.Num("utt"));
    return true;
}

json::Value NodeToJson(const Node& n) {
    json::Value v;
    v.t = json::Value::T::Obj;
    v.Set("id", json::N(n.id));
    if (n.origin != n.id) v.Set("origin", json::N(n.origin));
    v.Set("kind", json::S(KindName(n.kind)));
    if (!n.lemma.empty()) v.Set("lemma", json::S(n.lemma));
    if (!n.kcls.empty()) v.Set("kcls", json::S(n.kcls));
    if (!n.vpos.empty()) v.Set("vpos", json::S(n.vpos));
    if (n.sem) {
        json::Value s;
        s.t = json::Value::T::Arr;
        for (size_t i = 0; i < 8; ++i)
            if (n.sem & (1u << i)) s.arr.push_back(json::S(kSems[i]));
        v.Set("sem", s);
    }
    if (n.kind == Kind::V) {
        if (n.tense == Tense::past) v.Set("tense", json::S("past"));
        if (n.aspect != Aspect::simple) v.Set("aspect", json::S(AspectName(n.aspect)));
        if (n.mood != Mood::decl) v.Set("mood", json::S(MoodName2(n.mood)));
        v.Set("ctype", json::S(CTypeName(n.ctype)));
        if (!n.rel.empty()) v.Set("rel", json::S(n.rel));
        if (!n.eomi.empty()) v.Set("eomi", json::S(n.eomi));
        if (!n.modal.empty()) v.Set("modal", json::S(n.modal));
        if (n.neg != Neg::none) v.Set("neg", json::S(NegName(n.neg)));
        if (n.aux) v.Set("aux", json::B(true));
    }
    if (n.dep) v.Set("dep", json::B(true));
    if (n.glue) v.Set("glue", json::B(true));
    if (!n.kase.empty()) v.Set("kase", json::S(n.kase));
    if (n.kind == Kind::Punct) v.Set("punct", json::S(PunctName(n.punct)));
    if (!n.jaMark.empty()) v.Set("jaMark", json::S(n.jaMark));
    if (n.utt) v.Set("utt", json::N(n.utt));
    if (n.clause) v.Set("clause", json::N(n.clause));
    return v;
}

bool LineFromJson(const json::Value& v, LineIR& out, std::string& err, const char* nodesKey) {
    out = LineIR{};
    const json::Value* k = v.Get(nodesKey);
    if (!k) k = v.Get("kir");
    if (!k) {
        err = std::string("no ") + nodesKey;
        return false;
    }
    for (const auto& x : k->arr) {
        Node n;
        if (!NodeFromJson(x, n, err)) return false;
        out.kir2.push_back(n);
    }
    if (const json::Value* r = v.Get("reg"))
        for (const auto& x : r->arr) {
            Reg g;
            g.node = static_cast<uint32_t>(x.Num("node"));
            if (!ParseLevel(x.Str("level", "none"), g.level) || !ParsePersona(x.Str("persona", "none"), g.persona)) {
                err = "bad reg";
                return false;
            }
            g.hon = x.Bool("hon");
            out.reg.push_back(g);
        }
    if (const json::Value* l = v.Get("lev"))
        for (const auto& x : l->arr) {
            UttLevel u;
            u.utt = static_cast<uint16_t>(x.Num("utt"));
            if (!ParseLevel(x.Str("level", "none"), u.level) || !ParsePersona(x.Str("persona", "none"), u.persona)) {
                err = "bad lev";
                return false;
            }
            u.narration = x.Bool("narration");
            out.lev.push_back(u);
        }
    return true;
}

json::Value RegToJson(const Reg& r) {
    json::Value v;
    v.t = json::Value::T::Obj;
    v.Set("node", json::N(r.node));
    v.Set("level", json::S(Name(r.level)));
    v.Set("persona", json::S(Name(r.persona)));
    v.Set("hon", json::B(r.hon));
    return v;
}
json::Value LevToJson(const UttLevel& u) {
    json::Value v;
    v.t = json::Value::T::Obj;
    v.Set("utt", json::N(u.utt));
    v.Set("level", json::S(Name(u.level)));
    v.Set("persona", json::S(Name(u.persona)));
    if (u.narration) v.Set("narration", json::B(true));
    return v;
}

}  // namespace hd8gen
