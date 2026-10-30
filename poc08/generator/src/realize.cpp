// G: kir2 + reg → text. Expands aspect, negation and nominalization into nodes, then walks the nodes left
// to right: conjugation, particles, spoken fusions, spacing and punctuation.
#include <unordered_map>

#include "conj.h"
#include "hangul.h"
#include "hd8gen/generator.h"
#include "tables.h"

namespace hd8gen {

using namespace hangul;

const char* Name(Level l) {
    switch (l) {
        case Level::haera: return "haera";
        case Level::hae: return "hae";
        case Level::haeyo: return "haeyo";
        case Level::hapsyo: return "hapsyo";
        case Level::hao: return "hao";
        case Level::hage: return "hage";
        default: return "none";
    }
}
const char* Name(Persona p) {
    switch (p) {
        case Persona::rough: return "rough";
        case Persona::oldMale: return "old-male";
        case Persona::lady: return "lady";
        case Persona::formal: return "formal";
        case Persona::pompous: return "pompous";
        case Persona::archaic: return "archaic";
        case Persona::grand: return "grand";
        default: return "none";
    }
}
bool ParseLevel(const std::string& s, Level& out) {
    for (Level l : {Level::none, Level::haera, Level::hae, Level::haeyo, Level::hapsyo, Level::hao, Level::hage})
        if (s == Name(l)) {
            out = l;
            return true;
        }
    return false;
}
bool ParsePersona(const std::string& s, Persona& out) {
    for (Persona p : {Persona::none, Persona::rough, Persona::oldMale, Persona::lady, Persona::formal, Persona::pompous,
                      Persona::archaic, Persona::grand})
        if (s == Name(p)) {
            out = p;
            return true;
        }
    return false;
}

namespace {

const char* MoodName(Mood m) {
    switch (m) {
        case Mood::q: return "q";
        case Mood::imp: return "imp";
        case Mood::prop: return "prop";
        case Mood::excl: return "excl";
        default: return "decl";
    }
}

std::vector<char> PosChain(char pos) {
    switch (pos) {
        case 'P': return {'P', 'X', '*'};
        case 'X': return {'X', '*'};
        case 'N': return {'N', 'C', 'A', '*'};
        case 'C': return {'C', 'A', '*'};
        case 'V': return {'V', '*'};
        case 'A': return {'A', '*'};
        default: return {'*'};
    }
}

const EomiRow* FindEomi(const Tables& t, const std::string& slot, const std::string& family, const std::string& mood,
                        const std::string& level, char pos) {
    auto range = t.eomiBySlotFamily.equal_range(slot + "|" + family);
    for (char p : PosChain(pos)) {
        for (auto it = range.first; it != range.second; ++it) {
            const EomiRow& r = t.eomi[it->second];
            if (r.pos.size() != 1 || r.pos[0] != p) continue;
            if (!mood.empty() && r.mood != "-" && r.mood != mood) continue;
            if (!level.empty() && r.level != "-" && r.level != level) continue;
            return &r;
        }
    }
    return nullptr;
}

const EomiRow* FindEomiById(const Tables& t, const std::string& id, char pos, const std::string& level) {
    auto range = t.eomiById.equal_range(id);
    for (char p : PosChain(pos))
        for (auto it = range.first; it != range.second; ++it) {
            const EomiRow& r = t.eomi[it->second];
            if (r.pos.size() == 1 && r.pos[0] == p && (level.empty() || r.level == "-" || r.level == level)) return &r;
        }
    for (char p : PosChain(pos))
        for (auto it = range.first; it != range.second; ++it) {
            const EomiRow& r = t.eomi[it->second];
            if (r.pos.size() == 1 && r.pos[0] == p) return &r;
        }
    return nullptr;
}

struct XNode {  // expanded node with its register
    Node n;
    Level level = Level::haera;
    Persona persona = Persona::none;
    bool hon = false;
    bool spoken = false;
    bool chainMain = false;  // a main verb followed by its auxiliary (-어 + aux): 47항 붙임 candidate
};

const char* AspectAux(Aspect a, const char** conn) {
    *conn = "-어";
    switch (a) {
        case Aspect::prog: *conn = "-고"; return "있다";
        case Aspect::result: return "있다";
        case Aspect::compl_: return "버리다";
        case Aspect::dirCome: return "오다";
        case Aspect::dirGo: return "가다";
        case Aspect::try_: return "보다";
        case Aspect::give: return "주다";
        default: return nullptr;
    }
}

std::vector<XNode> Expand(const Tables& t, const std::vector<Node>& in, const std::vector<Reg>& reg,
                          const std::vector<UttLevel>& lev) {
    std::unordered_map<uint32_t, const Reg*> byNode;
    for (const auto& r : reg) byNode[r.node] = &r;
    std::unordered_map<uint16_t, const UttLevel*> byUtt;
    for (const auto& u : lev) byUtt[u.utt] = &u;
    std::vector<XNode> out;
    for (const Node& n : in) {
        XNode x;
        x.n = n;
        if (x.n.origin == 0) x.n.origin = n.id;
        auto u = byUtt.find(n.utt);
        if (u != byUtt.end()) {
            x.level = u->second->level == Level::none ? Level::haera : u->second->level;
            x.persona = u->second->persona;
            x.spoken = !u->second->narration;
        }
        auto r = byNode.find(n.id);
        if (r == byNode.end() && n.origin) r = byNode.find(n.origin);
        if (r != byNode.end()) {
            if (r->second->level != Level::none) x.level = r->second->level;
            if (r->second->persona != Persona::none) x.persona = r->second->persona;
            x.hon = r->second->hon;
        }
        if (n.kind != Kind::V) {
            out.push_back(x);
            continue;
        }
        const PredicateInfo info = LookupPredicate(t, n.lemma);
        const std::string pos = !n.vpos.empty() ? n.vpos : info.pos;
        if (n.neg == Neg::an || n.neg == Neg::mot) {
            XNode adv = x;
            adv.n = Node{};
            adv.n.kind = Kind::Adv;
            adv.n.lemma = n.neg == Neg::an ? "안" : "못";
            adv.n.id = adv.n.origin = x.n.origin;
            adv.n.utt = n.utt;
            out.push_back(adv);
            x.n.neg = Neg::none;
        }
        std::vector<XNode> chain{x};
        const char* conn = nullptr;
        if (const char* aux = AspectAux(n.aspect, &conn)) {
            XNode a = x;
            a.n.lemma = aux;
            a.n.kcls.clear();
            a.n.vpos.clear();
            a.n.aux = true;
            a.n.aspect = Aspect::simple;
            if (std::string(aux) == "있다" && x.hon) {
                a.n.lemma = "계시다";
                a.hon = false;
                a.n.vpos = "V";
            }
            XNode& m = chain.back();
            m.n.aspect = Aspect::simple;
            m.n.ctype = CType::conn;
            m.n.eomi = conn;
            m.n.tense = Tense::nonpast;
            m.n.modal.clear();
            m.n.neg = Neg::none;
            m.hon = false;
            m.chainMain = std::string(conn) == "-어";
            chain.push_back(a);
        }
        XNode& last = chain.back();
        if (last.n.neg == Neg::jiAnh || last.n.neg == Neg::jiMot) {
            XNode a = last;
            const std::string lpos = last.n.aux ? LookupPredicate(t, last.n.lemma).pos : pos;
            a.n.lemma = last.n.neg == Neg::jiAnh ? "않다" : "못하다";
            a.n.vpos = (lpos == "A" || lpos == "C" || lpos == "N") ? "A" : "V";
            if (a.n.lemma == "못하다") a.n.vpos = lpos == "A" ? "A" : "V";
            a.n.kcls.clear();
            a.n.aux = true;
            a.n.neg = Neg::none;
            last.n.ctype = CType::conn;
            last.n.eomi = "-지";
            last.n.tense = Tense::nonpast;
            last.n.modal.clear();
            last.n.neg = Neg::none;
            last.hon = false;
            chain.push_back(a);
        } else if (last.n.neg == Neg::jiMa) {
            last.n.eomi = "jima";
            last.n.mood = Mood::imp;
            last.n.ctype = CType::final_;
            last.n.neg = Neg::none;
        }
        XNode& tail = chain.back();
        if (tail.n.ctype == CType::nom && tail.n.rel == "geot") {
            tail.n.ctype = CType::adn;
            tail.n.rel = tail.n.tense == Tense::past ? "past" : "nonpast";
            tail.n.tense = Tense::nonpast;
            for (auto& c : chain) out.push_back(c);
            XNode g = x;
            g.n = Node{};
            g.n.kind = Kind::N;
            g.n.lemma = "것";
            g.n.dep = true;
            g.n.id = g.n.origin = x.n.origin;
            g.n.utt = n.utt;
            out.push_back(g);
            continue;
        }
        for (auto& c : chain) out.push_back(c);
    }
    return out;
}

struct Writer {
    std::string out;
    std::vector<OutSpan> spans;
    std::u32string word;  // the current word (since the last space)
    bool noSpace = true;  // the next item attaches (line start, after an opening quote or a prefix)
    void Emit(const std::string& s, bool attach, uint32_t node) {
        if (s.empty()) return;
        if (!attach && !noSpace) {
            out.push_back(' ');
            word.clear();
        }
        if (!attach && noSpace) word.clear();
        const auto begin = static_cast<uint32_t>(out.size());
        out += s;
        spans.push_back({begin, static_cast<uint32_t>(out.size()), node});
        const std::u32string u = ToU32(s);
        const size_t sp = u.rfind(U' ');
        if (sp != std::u32string::npos) word = u.substr(sp + 1);
        else word += u;
        noSpace = false;
    }
    // replaces the text of the last span (spoken fusion of a host with the copula)
    void FuseLastFinal(int fin) {
        if (out.empty()) return;
        std::u32string u = ToU32(out);
        if (!IsSyllable(u.back())) return;
        u.back() = WithFinal(u.back(), fin);
        const std::string s = ToU8(u);
        if (!spans.empty()) spans.back().end = static_cast<uint32_t>(s.size());
        out = s;
        if (!word.empty() && IsSyllable(word.back())) word.back() = WithFinal(word.back(), fin);
    }
};

const SpokenRow* SpokenFor(const Tables& t, const std::string& host, const Node* next, const std::string& nextCls) {
    std::string key = "-";
    if (next && next->kind == Kind::P) key = next->kase;
    else if (next && next->kind == Kind::V && nextCls == "COP") key = "COP";
    for (const auto& r : t.spoken) {
        if (r.host != host) continue;
        if (r.next == key) return &r;
    }
    if (key != "-" && next && next->kind == Kind::P) return nullptr;  // a particle without a row: keep
    if (key == "-" || key == "COP") {
        for (const auto& r : t.spoken)
            if (r.host == host && r.next == "-" && key == "-") return &r;
    }
    return nullptr;
}

std::string ParticleText(const Tables& t, const Node& p, std::u32string_view prev, bool anim, bool hon, bool spoken) {
    if (p.kase.empty() || p.kase == "lex") return RealizeParticle(p.lemma, prev);
    for (const auto& r : t.particles) {
        if (r.kase != p.kase) continue;
        bool ok = true;
        for (const auto& w : r.when) {
            if (w == "anim" && !anim) ok = false;
            if (w == "hon" && !hon) ok = false;
            if (w == "spoken" && !spoken) ok = false;
        }
        if (ok) return RealizeParticle(r.form, prev);
    }
    return RealizeParticle(p.lemma.empty() ? p.kase : p.lemma, prev);
}

}  // namespace

std::string EomiForm(const Tables& t, const std::string& slot, const std::string& family, const std::string& mood,
                     const std::string& level, const std::string& pos, bool* honIncl) {
    const EomiRow* r = FindEomi(t, slot, family, mood, level, pos.empty() ? 'V' : pos[0]);
    if (honIncl) *honIncl = r && r->honIncl;
    return r ? r->form : std::string();
}

std::string Particle(const Tables& t, const std::string& prevWord, const std::string& kaseOrLemma, bool anim, bool hon,
                     bool spoken) {
    Node p;
    p.kind = Kind::P;
    bool isKase = false;
    for (const auto& r : t.particles)
        if (r.kase == kaseOrLemma) isKase = true;
    if (isKase) p.kase = kaseOrLemma;
    else {
        p.kase = "lex";
        p.lemma = kaseOrLemma;
    }
    return ParticleText(t, p, ToU32(prevWord), anim, hon, spoken);
}

std::string ConjugateForms(const Tables& t, const std::string& lemma, const std::string& posIn, const std::string& clsIn,
                           const std::vector<std::string>& forms, const std::string& host, const GenOptions& opt) {
    const PredicateInfo info = LookupPredicate(t, lemma);
    const std::string pos = posIn.empty() ? info.pos : posIn;
    const std::string cls = clsIn.empty() ? info.cls : clsIn;
    Stem s = StemOf(t, lemma.substr(0, lemma.find(':')), pos, cls);
    std::u32string hostText = ToU32(host);
    bool cop = cls == "COP";
    if (cop) {
        // 학생이다 / -적이다: the noun before 이다 is the host
        std::u32string u = ToU32(lemma.substr(0, lemma.find(':')));
        if (u.size() > 2) hostText += u.substr(0, u.size() - 2);
        s.text = U"이";
    }
    std::u32string prefix = cop ? hostText : std::u32string();
    for (size_t i = 0; i < forms.size(); ++i) {
        std::string f = forms[i];
        bool prefinal = false, hon = false;
        const EomiRow* row = nullptr;
        auto range = t.eomiById.equal_range(f);
        if (range.first != range.second) row = FindEomiById(t, f, s.pos, "");
        if (row) {
            prefinal = row->slot == "hon" || row->slot == "tense" || row->slot == "modal";
            hon = row->slot == "hon";
            f = row->form;
        } else {
            if (f.size() > 1 && f.back() == '-') {
                prefinal = true;
                f.pop_back();
            }
            if (!f.empty() && f[0] == '-') f = f.substr(1);
            hon = f == "(으)시";
        }
        std::u32string r;
        if (cop && i == 0) r = CopulaAfter(t, hostText, ToU32(f), opt);
        else r = Attach(t, s, ToU32(f), opt);
        if (!prefinal && i + 1 == forms.size()) return ToU8(prefix + r);
        s.text = r;
        s.pos = hon ? s.pos : 'P';
        if (hon && s.pos == 'C') s.pos = 'A';
        s.cls = "REG";
        s.ae = false;
        s.honPrefinal = hon;
    }
    return ToU8(prefix + s.text);
}

namespace {

// Realizes one V node to its text (one word, or several when the ending form spans words).
std::string RealizeV(const Tables& t, const XNode& x, std::u32string_view host, bool fuseHost, int* fuseFinal,
                     std::vector<std::string>& notes, const GenOptions& opt) {
    const Node& n = x.n;
    const PredicateInfo info = LookupPredicate(t, n.lemma);
    const std::string pos = !n.vpos.empty() ? n.vpos : info.pos;
    const std::string cls = !n.kcls.empty() ? n.kcls : info.cls;
    // the final ending follows the last prefinal: after 었/겠 it is looked up as P (갔다, not 갔는다)
    const bool prefinalP = (n.tense == Tense::past || n.modal == "gess") && n.ctype != CType::adn && n.ctype != CType::stem;
    const char p = prefinalP ? 'P' : (pos.empty() ? 'V' : pos[0]);
    const std::string level = Name(x.level);
    const std::string mood = MoodName(n.mood);
    std::vector<std::string> forms;
    bool honIncl = false;
    std::string finalForm;
    const EomiRow* row = nullptr;
    switch (n.ctype) {
        case CType::final_: {
            std::string family = "plain";
            if (!n.eomi.empty() && n.eomi[0] != '-') family = n.eomi;
            if (!n.eomi.empty() && n.eomi[0] == '-') row = FindEomiById(t, n.eomi, p, level);
            if (!row && family == "plain" && x.persona != Persona::none) {
                for (const auto& pr : t.persona)
                    if (pr.persona == Name(x.persona) && pr.level == level && (pr.mood == "-" || pr.mood == mood)) {
                        row = FindEomi(t, "final", pr.family, mood, level, p);
                        if (row) break;
                    }
            }
            if (!row) row = FindEomi(t, "final", family, mood, level, p);
            if (!row && family != "plain") row = FindEomi(t, "final", "plain", mood, level, p);
            break;
        }
        case CType::conn:
            if (!n.eomi.empty()) row = FindEomiById(t, n.eomi, p, "");
            if (!row && !n.rel.empty()) row = FindEomi(t, "conn", n.rel, "", "", p);
            if (!row) row = FindEomiById(t, "-고", p, "");
            break;
        case CType::adn: {
            std::string rel = n.rel;
            if (rel.empty()) rel = n.tense == Tense::past ? "past" : "nonpast";
            row = FindEomi(t, "adn", rel, "", "", p);
            break;
        }
        case CType::nom:
            row = FindEomi(t, "nom", n.rel.empty() ? "m" : n.rel, "", "", p);
            break;
        case CType::quote:
            row = FindEomi(t, "quote", n.rel == "adn" ? "adn" : mood, "", "", p);
            break;
        case CType::stem:
            finalForm = "~";
            break;
    }
    if (row) {
        finalForm = row->form;
        honIncl = row->honIncl;
    } else if (finalForm.empty()) {
        notes.push_back("no ending row for " + n.lemma + " ctype " + std::to_string(static_cast<int>(n.ctype)) + " " +
                        n.eomi + " " + level + " " + mood);
        finalForm = "다";
    }
    if (x.hon && !honIncl) forms.push_back("-시-");
    if (n.tense == Tense::past && n.ctype != CType::adn && n.ctype != CType::stem) forms.push_back("-었-");
    if (n.modal == "gess") forms.push_back("-겠-");
    forms.push_back(finalForm);
    std::string hostArg;
    if (cls == "COP") hostArg = ToU8(host);
    std::string text = ConjugateForms(t, n.lemma, pos, cls, forms, hostArg, opt);
    if (cls == "COP") {
        // ConjugateForms returns host + copula; keep the copula part
        const std::string h = ToU8(host);
        std::u32string u = ToU32(n.lemma);
        std::string inner;
        if (u.size() > 2) inner = ToU8(u.substr(0, u.size() - 2));
        if (text.rfind(h, 0) == 0) text = text.substr(h.size());
        (void)inner;
        if (fuseHost && fuseFinal) {
            // 거 + 인데 → 건데: the jamo of 인/일/입 moves onto the host
            std::u32string c = ToU32(text);
            if (!c.empty() && IsSyllable(c[0]) && Initial(c[0]) == kInitIeung && Medial(c[0]) == I && Final(c[0]) != 0 &&
                (Final(c[0]) == kFinNieun || Final(c[0]) == kFinRieul || Final(c[0]) == kFinBieup)) {
                *fuseFinal = Final(c[0]);
                text = ToU8(c.substr(1));
            }
        }
    }
    return text;
}

}  // namespace

GenResult Generate(const Tables& t, const std::vector<Node>& kir2, const std::vector<Reg>& reg,
                   const std::vector<UttLevel>& lev, const GenOptions& opt) {
    GenResult res;
    const std::vector<XNode> xs = Expand(t, kir2, reg, lev);
    Writer w;
    bool fusePending = false;
    Kind prevKind = Kind::Punct;
    bool prevAdn = false;
    for (size_t i = 0; i < xs.size(); ++i) {
        const XNode& x = xs[i];
        const Node& n = x.n;
        const Node* next = i + 1 < xs.size() ? &xs[i + 1].n : nullptr;
        std::string nextCls;
        if (next && next->kind == Kind::V) nextCls = !next->kcls.empty() ? next->kcls : LookupPredicate(t, next->lemma).cls;
        switch (n.kind) {
            case Kind::N: {
                std::string text = n.lemma;
                bool consumeNext = false;
                if (x.spoken) {
                    if (const SpokenRow* sr = SpokenFor(t, n.lemma, next, nextCls)) {
                        text = sr->result;
                        if (sr->mode == "fuse") fusePending = true;
                        else if (next && next->kind == Kind::P && sr->next != "-") consumeNext = true;
                    }
                }
                bool attach = n.glue;
                auto wr = t.words.find("N:" + n.lemma);
                if (wr != t.words.end()) {
                    const std::string& a = wr->second.attach;
                    if (a == "always") attach = true;
                    else if (a == "after-digit") attach = !w.word.empty() && w.word.back() >= U'0' && w.word.back() <= U'9';
                    else if (a == "after-noun") attach = prevKind == Kind::N || prevKind == Kind::Raw || (prevKind == Kind::P && !prevAdn);
                }
                w.Emit(text, attach, n.origin);
                if (wr != t.words.end() && wr->second.attach == "next") w.noSpace = true;
                if (consumeNext) ++i;
                prevKind = Kind::N;
                prevAdn = false;
                break;
            }
            case Kind::P: {
                const bool anim = (n.sem & kAnimate) != 0 || (i > 0 && (xs[i - 1].n.sem & (kPerson | kAnimate)) != 0);
                const bool hon = x.hon;
                w.Emit(ParticleText(t, n, w.word, anim, hon, x.spoken), true, n.origin);
                prevKind = Kind::P;
                break;
            }
            case Kind::V: {
                const std::string cls = !n.kcls.empty() ? n.kcls : LookupPredicate(t, n.lemma).cls;
                const bool cop = cls == "COP" && ToU32(n.lemma).size() <= 2;
                int fin = 0;
                const std::string text = RealizeV(t, x, w.word, fusePending, &fin, res.notes, opt);
                if (fin) w.FuseLastFinal(fin);
                fusePending = false;
                bool attach = cop || n.glue;
                if (n.aux) {
                    auto wr = t.words.find("V:" + n.lemma);
                    if (wr != t.words.end() && wr->second.attach == "always") attach = true;
                    if (opt.attachAux && i > 0 && xs[i - 1].chainMain) attach = true;
                }
                w.Emit(text, attach, n.origin);
                prevKind = Kind::V;
                prevAdn = n.ctype == CType::adn;
                break;
            }
            case Kind::Punct: {
                const PunctRow* pr = nullptr;
                const char* kinds[] = {"none", "comma", "pause", "end", "q", "excl", "qopen", "qclose", "ellipsis", "dash", "mark"};
                const std::string kind = kinds[static_cast<int>(n.punct)];
                for (const auto& r : t.punct)
                    if (r.kind == kind && r.jaMark == n.jaMark) {
                        pr = &r;
                        break;
                    }
                if (!pr)
                    for (const auto& r : t.punct)
                        if (r.kind == kind && r.jaMark == "-") {
                            pr = &r;
                            break;
                        }
                std::string text = pr ? pr->ko : n.jaMark;
                if (text == "-") text = n.jaMark;
                const std::string attachMode = pr ? pr->attach : "left";
                w.Emit(text, attachMode == "left" || n.glue, n.origin);
                if (attachMode == "right") w.noSpace = true;
                prevKind = Kind::Punct;
                break;
            }
            case Kind::Raw:
                w.Emit(n.lemma, n.glue, n.origin);
                prevKind = Kind::Raw;
                break;
            default:  // Adv Det Intj
                w.Emit(n.lemma, n.glue, n.origin);
                prevKind = n.kind;
                break;
        }
    }
    res.out = w.out;
    res.spans = w.spans;
    return res;
}

}  // namespace hd8gen
