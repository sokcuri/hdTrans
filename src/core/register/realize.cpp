// Nodes and levels to text: negation and nominalization expand into nodes, then the nodes are written
// left to right with conjugation, particles, spoken fusions, spacing and marks.
#include <unordered_map>

#include "core/register/conj.h"
#include "core/register/generate.h"
#include "core/register/hangul.h"

namespace hdTrans::reg {

using namespace hangul;

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

const char* PunctName(Punct p) {
    static constexpr const char* kNames[] = {"none", "comma", "pause", "end", "q", "excl",
                                             "qopen", "qclose", "ellipsis", "dash", "mark"};
    return kNames[static_cast<int>(p)];
}

std::string_view PosChain(char pos) {
    switch (pos) {
        case 'P': return "PX*";
        case 'X': return "X*";
        case 'N': return "NCA*";
        case 'C': return "CA*";
        case 'V': return "V*";
        case 'A': return "A*";
        default: return "*";
    }
}

const EomiRow* FindEomi(const Tables& t, const std::string& slot, const std::string& family, const std::string& mood,
                        const std::string& level, char pos) {
    const auto range = t.eomiBySlotFamily.equal_range(slot + "|" + family);
    for (const char p : PosChain(pos)) {
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
    const auto range = t.eomiById.equal_range(id);
    for (const char p : PosChain(pos))
        for (auto it = range.first; it != range.second; ++it) {
            const EomiRow& r = t.eomi[it->second];
            if (r.pos.size() == 1 && r.pos[0] == p && (level.empty() || r.level == "-" || r.level == level)) return &r;
        }
    for (const char p : PosChain(pos))
        for (auto it = range.first; it != range.second; ++it) {
            const EomiRow& r = t.eomi[it->second];
            if (r.pos.size() == 1 && r.pos[0] == p) return &r;
        }
    return nullptr;
}

// A node with the register it is written at.
struct XNode {
    Node n;
    Level level = Level::haera;
    Persona persona = Persona::none;
    bool hon = false;
    bool spoken = false;
};

// Moves the final features of from onto a new auxiliary node and leaves from a connective.
XNode SplitAux(XNode& from, const std::string& auxLemma, const std::string& conn, const std::string& auxPos) {
    XNode a = from;
    a.n.lemma = auxLemma;
    a.n.kcls.clear();
    a.n.vpos = auxPos;
    a.n.aux = true;
    a.n.glue = false;
    from.n.ctype = CType::conn;
    from.n.eomi = conn;
    from.n.rel.clear();
    from.n.tense = Tense::nonpast;
    from.n.modal.clear();
    from.n.neg = Neg::none;
    from.n.mood = Mood::decl;
    from.hon = false;
    return a;
}

std::vector<XNode> Expand(const Tables& t, const std::vector<Node>& in, const std::vector<Reg>& reg, const UttLevel& utt) {
    std::unordered_map<uint32_t, const Reg*> byNode;
    for (const auto& r : reg) byNode[r.node] = &r;
    std::vector<XNode> out;
    for (const Node& n : in) {
        XNode x;
        x.n = n;
        x.level = utt.level == Level::none ? Level::haera : utt.level;
        x.persona = utt.persona;
        x.spoken = !utt.narration;
        if (const auto r = byNode.find(n.id); r != byNode.end()) {
            if (r->second->level != Level::none) x.level = r->second->level;
            if (r->second->persona != Persona::none) x.persona = r->second->persona;
            x.hon = r->second->hon;
        }
        if (n.kind != Kind::V) {
            out.push_back(x);
            continue;
        }
        const std::string pos = !n.vpos.empty() ? n.vpos : LookupPredicate(t, n.lemma).pos;
        std::vector<XNode> chain{x};
        if (x.n.neg == Neg::jiAnh || x.n.neg == Neg::jiMot) {
            const std::string lpos = x.n.aux ? LookupPredicate(t, x.n.lemma).pos : pos;
            const std::string apos = (lpos == "A" || lpos == "C" || lpos == "N" || lpos == "X") ? "A" : "V";
            const bool mot = x.n.neg == Neg::jiMot;
            XNode a = SplitAux(chain.back(), t.G(mot ? "aux.neg-mot" : "aux.neg"), t.G("conn.neg"),
                               mot ? (lpos == "A" ? "A" : "V") : apos);
            a.n.neg = Neg::none;
            chain.push_back(a);
        } else if (x.n.neg == Neg::jiMa) {
            XNode& last = chain.back();
            last.n.eomi = t.G("family.jima");
            last.n.mood = Mood::imp;
            last.n.ctype = CType::final_;
            last.n.neg = Neg::none;
        }
        XNode& tail = chain.back();
        const bool geot = tail.n.ctype == CType::nom && tail.n.rel == "geot";
        if (geot) {
            tail.n.ctype = CType::adn;
            tail.n.rel = tail.n.tense == Tense::past ? "past" : "nonpast";
            tail.n.tense = Tense::nonpast;
        }
        for (const XNode& c : chain) out.push_back(c);
        if (geot) {
            XNode g = x;
            g.n = Node{};
            g.n.kind = Kind::N;
            g.n.lemma = t.G("nom.geot");
            g.n.dep = true;
            g.n.id = x.n.id;
            out.push_back(g);
        }
    }
    return out;
}

struct Writer {
    std::string out;
    std::u32string word;  // the current word (since the last space)
    bool noSpace = true;  // the next item attaches (line start, after an opening quote or a prefix)
    std::vector<OutSpan>* spans = nullptr;
    void Emit(const std::string& s, bool attach, uint32_t node) {
        if (s.empty()) return;
        if (!attach && !noSpace) out.push_back(' ');
        if (!attach) word.clear();
        if (spans != nullptr) spans->push_back({out.size(), out.size() + s.size(), node});
        out += s;
        const std::u32string u = ToU32(s);
        const std::size_t sp = u.rfind(U' ');
        if (sp != std::u32string::npos) word = u.substr(sp + 1);
        else word += u;
        noSpace = false;
    }
    // Moves a final onto the last syllable written (거 + 인데 → 건데).
    void FuseLastFinal(int fin) {
        if (out.empty()) return;
        std::u32string u = ToU32(out);
        if (!IsSyllable(u.back())) return;
        u.back() = WithFinal(u.back(), fin);
        out = ToU8(u);
        if (!word.empty() && IsSyllable(word.back())) word.back() = WithFinal(word.back(), fin);
    }
};

const SpokenRow* SpokenFor(const Tables& t, const std::string& host, const Node* next, const std::string& nextCls) {
    std::string key = "-";
    if (next && next->kind == Kind::P) key = next->kase;
    else if (next && next->kind == Kind::V && nextCls == "COP") key = "COP";
    // text written onto the noun that the generator does not realize itself (것 + 이다): the bare form would leave 거이다
    if (key == "-" && next && next->glue && (next->kind == Kind::Raw || next->kind == Kind::N)) return nullptr;
    for (const auto& r : t.spoken)
        if (r.host == host && r.next == key) return &r;
    if (key != "-" && next && next->kind == Kind::P) return nullptr;  // a particle without a row keeps the noun
    if (key == "-")
        for (const auto& r : t.spoken)
            if (r.host == host && r.next == "-") return &r;
    return nullptr;
}

std::string ParticleText(const Tables& t, const Node& p, std::u32string_view prev, bool hon, bool spoken) {
    if (p.kase.empty() || p.kase == "lex") return RealizeParticle(p.lemma, prev);
    for (const auto& r : t.particles) {
        if (r.kase != p.kase) continue;
        bool ok = true;
        for (const auto& w : r.when) {
            if (w == "anim") ok = false;
            if (w == "hon" && !hon) ok = false;
            if (w == "spoken" && !spoken) ok = false;
        }
        if (ok) return RealizeParticle(r.form, prev);
    }
    return RealizeParticle(p.lemma.empty() ? p.kase : p.lemma, prev);
}

}  // namespace

std::string EndingForm(const Tables& t, const Node& n, Level atLevel, Persona persona, bool& honIncl) {
    const std::string pos = !n.vpos.empty() ? n.vpos : LookupPredicate(t, n.lemma).pos;
    // the final ending follows the last prefinal: after 었/겠 it is looked up as P (갔다, not 갔는다)
    const bool prefinalP = (n.tense == Tense::past || n.modal == "gess") && n.ctype != CType::adn && n.ctype != CType::stem;
    const char p = prefinalP ? 'P' : (pos.empty() ? 'V' : pos[0]);
    const std::string level = Name(atLevel);
    const std::string mood = MoodName(n.mood);
    const EomiRow* row = nullptr;
    honIncl = false;
    switch (n.ctype) {
        case CType::final_: {
            std::string family = "plain";
            if (!n.eomi.empty() && n.eomi[0] != '-') family = n.eomi;
            if (!n.eomi.empty() && n.eomi[0] == '-') row = FindEomiById(t, n.eomi, p, level);
            if (!row && family == "plain" && persona != Persona::none) {
                for (const auto& pr : t.persona)
                    if (pr.persona == Name(persona) && pr.level == level && (pr.mood == "-" || pr.mood == mood)) {
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
            if (!row) row = FindEomiById(t, t.G("conn.default"), p, "");
            break;
        case CType::adn: {
            std::string rel = n.rel;
            if (rel.empty()) rel = n.tense == Tense::past ? "past" : "nonpast";
            if (rel == "nonpast" && n.tense == Tense::past) rel = "past";
            row = FindEomi(t, "adn", rel, "", "", p);
            break;
        }
        case CType::nom:
            row = FindEomi(t, "nom", n.rel.empty() ? "m" : n.rel, "", "", p);
            break;
        case CType::quote:
            row = FindEomi(t, "quote", !n.rel.empty() ? n.rel : mood, "", "", p);
            break;
        case CType::stem:
            return "~";
    }
    if (!row) return t.G("final.fallback");
    honIncl = row->honIncl;
    return row->form;
}

namespace {

// The text of one V node (one word, or several when the ending spans words).
std::string RealizeV(const Tables& t, const XNode& x, std::u32string_view host, bool fuseHost, int& fuseFinal) {
    return WriteV(t, LemmaOf(t, x.n.lemma), x.n, x.level, x.persona, x.hon, host, fuseHost, fuseFinal);
}

}  // namespace

std::vector<std::string> FormsOf(const Tables& t, const Node& n, const std::string& finalForm, bool honIncl, bool hon) {
    std::vector<std::string> forms;
    if (hon && !honIncl) forms.push_back(t.G("pre.hon"));
    // past on an adnominal only before -ㄹ (갔을); the other adnominals carry tense in their row (간, 갔던)
    if (n.tense == Tense::past && n.ctype != CType::stem && (n.ctype != CType::adn || n.rel == "fut"))
        forms.push_back(t.G("pre.past"));
    if (n.modal == "gess") forms.push_back(t.G("pre.gess"));
    forms.push_back(finalForm);
    return forms;
}

LemmaInfo LemmaOf(const Tables& t, const std::string& lemma) {
    LemmaInfo l;
    l.lemma = lemma;
    l.bare = lemma.substr(0, lemma.find(':'));
    l.info = LookupPredicate(t, lemma);
    l.bareInfo = l.bare == lemma ? l.info : LookupPredicate(t, l.bare);
    return l;
}

std::string WriteV(const Tables& t, const LemmaInfo& lemma, const Node& n, Level level, Persona persona, bool hon,
                   std::u32string_view host, bool fuseHost, int& fuseFinal) {
    bool honIncl = false;
    const std::string finalForm = EndingForm(t, n, level, persona, honIncl);
    return WriteV(t, lemma, n, finalForm, honIncl, hon, host, fuseHost, fuseFinal);
}

std::string WriteV(const Tables& t, const LemmaInfo& lemma, const Node& n, const std::string& finalForm, bool honIncl,
                   bool hon, std::u32string_view host, bool fuseHost, int& fuseFinal) {
    const std::string pos = !n.vpos.empty() ? n.vpos : lemma.info.pos;
    const std::string cls = !n.kcls.empty() ? n.kcls : lemma.info.cls;
    const std::vector<std::string> forms = FormsOf(t, n, finalForm, honIncl, hon);
    const std::string hostArg = cls == "COP" ? ToU8(host) : std::string();
    std::string text = ConjugateForms(t, lemma, pos, cls, forms, hostArg);
    if (cls == "COP") {
        // ConjugateForms returns the host and the copula: the copula part
        if (text.starts_with(hostArg)) text = text.substr(hostArg.size());
        if (fuseHost) {
            // 거 + 인데 → 건데: the final of 인/일/입 moves onto the host
            const std::u32string c = ToU32(text);
            if (!c.empty() && IsSyllable(c[0]) && Initial(c[0]) == kInitIeung && Medial(c[0]) == I &&
                (Final(c[0]) == kFinNieun || Final(c[0]) == kFinRieul || Final(c[0]) == kFinBieup)) {
                fuseFinal = Final(c[0]);
                text = ToU8(c.substr(1));
            }
        }
    }
    return text;
}

std::string ConjugateForms(const Tables& t, const std::string& lemma, const std::string& pos, const std::string& cls,
                           const std::vector<std::string>& forms, const std::string& host) {
    return ConjugateForms(t, LemmaOf(t, lemma), pos, cls, forms, host);
}

std::string ConjugateForms(const Tables& t, const LemmaInfo& lemma, const std::string& posIn, const std::string& clsIn,
                           const std::vector<std::string>& forms, const std::string& host) {
    const PredicateInfo& info = lemma.info;
    const std::string pos = posIn.empty() ? info.pos : posIn;
    const std::string cls = clsIn.empty() ? info.cls : clsIn;
    const std::string& bare = lemma.bare;
    Stem s = StemOf(bare, pos, cls, lemma.bareInfo);
    std::u32string hostText = ToU32(host);
    const bool cop = cls == "COP";
    if (cop) {
        // 학생이다, -적이다: the noun before 이다 is the host
        const std::u32string u = ToU32(bare);
        if (u.size() > 2) hostText += u.substr(0, u.size() - 2);
        s.text = std::u32string(1, kI);
    }
    const std::u32string prefix = cop ? hostText : std::u32string();
    for (std::size_t i = 0; i < forms.size(); ++i) {
        std::string f = forms[i];
        bool prefinal = false, hon = false;
        const EomiRow* row = nullptr;
        if (t.eomiById.contains(f)) row = FindEomiById(t, f, s.pos, "");
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
            hon = f == ToU8(EuMark() + std::u32string(1, Syl(9, I)));  // (으)시
        }
        const std::u32string r = cop && i == 0 ? CopulaAfter(t, hostText, ToU32(f)) : Attach(t, s, ToU32(f));
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

std::string Generate(const Tables& t, const std::vector<Node>& nodes, const std::vector<Reg>& reg, const UttLevel& utt,
                     std::vector<OutSpan>* spans) {
    const std::vector<XNode> xs = Expand(t, nodes, reg, utt);
    Writer w;
    w.spans = spans;
    bool fusePending = false;
    Kind prevKind = Kind::Punct;
    bool prevAdn = false;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const XNode& x = xs[i];
        const Node& n = x.n;
        const Node* next = i + 1 < xs.size() ? &xs[i + 1].n : nullptr;
        std::string nextCls;
        if (next && next->kind == Kind::V) nextCls = !next->kcls.empty() ? next->kcls : LookupPredicate(t, next->lemma).cls;
        switch (n.kind) {
            case Kind::N: {
                std::string text = n.lemma;
                bool consumeNext = false;
                const SpokenRow* sr = SpokenFor(t, n.lemma, next, nextCls);
                if (sr && !x.spoken && sr->mode != "always" && sr->mode != "always-fuse") sr = nullptr;
                if (sr) {
                    text = sr->result;
                    if (sr->mode == "fuse" || sr->mode == "always-fuse") fusePending = true;
                    else if (next && next->kind == Kind::P && sr->next != "-") consumeNext = true;
                }
                bool attach = n.glue;
                const auto wr = t.words.find("N:" + n.lemma);
                // dependent nouns and counters are spaced by 한글 맞춤법 whatever the input's spacing (하는 대로)
                const bool spacingClass = wr != t.words.end() && wr->second.kind == "dep";
                if (wr != t.words.end() && (!n.glue || spacingClass)) {
                    if (spacingClass) attach = false;
                    const std::string& a = wr->second.attach;
                    if (a == "always") attach = true;
                    else if (a == "after-digit") attach = !w.word.empty() && w.word.back() >= U'0' && w.word.back() <= U'9';
                    else if (a == "after-noun") attach = prevKind == Kind::N || prevKind == Kind::Raw || (prevKind == Kind::P && !prevAdn);
                }
                w.Emit(text, attach, n.id);
                if (wr != t.words.end() && wr->second.attach == "next") w.noSpace = true;
                if (consumeNext) ++i;
                prevKind = Kind::N;
                prevAdn = false;
                break;
            }
            case Kind::P: {
                // a particle after a comma keeps its space (부수입이다, 라고): it cannot attach to a mark
                const bool afterComma = i > 0 && xs[i - 1].n.kind == Kind::Punct && xs[i - 1].n.punct == Punct::comma;
                w.Emit(ParticleText(t, n, w.word, x.hon, x.spoken), !afterComma, n.id);
                prevKind = Kind::P;
                break;
            }
            case Kind::V: {
                const std::string cls = !n.kcls.empty() ? n.kcls : LookupPredicate(t, n.lemma).cls;
                const bool cop = cls == "COP" && ToU32(n.lemma).size() <= 2;
                int fin = 0;
                const std::string text = RealizeV(t, x, w.word, fusePending, fin);
                if (fin) w.FuseLastFinal(fin);
                fusePending = false;
                bool attach = cop || n.glue;
                if (n.aux) {
                    const auto wr = t.words.find("V:" + n.lemma);
                    if (wr != t.words.end() && wr->second.attach == "always") attach = true;
                }
                w.Emit(text, attach, n.id);
                prevKind = Kind::V;
                prevAdn = n.ctype == CType::adn;
                break;
            }
            case Kind::Punct: {
                const PunctRow* pr = nullptr;
                const std::string kind = PunctName(n.punct);
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
                if (pr && pr->jaMark == "-" && !n.jaMark.empty()) {
                    // a run of one mark (……, ―――, !!) keeps its length
                    const std::u32string m = ToU32(n.jaMark);
                    bool same = m.size() > 1;
                    for (const char32_t ch : m) same = same && ch == m[0];
                    if (same && ToU32(text).size() == 1) {
                        std::string run;
                        for (std::size_t k = 0; k < m.size(); ++k) run += text;
                        text = run;
                    }
                }
                const std::string attachMode = pr ? pr->attach : "left";
                w.Emit(text, attachMode == "left" || attachMode == "both" || n.glue, n.id);
                if (attachMode == "right" || attachMode == "both") w.noSpace = true;
                prevKind = Kind::Punct;
                break;
            }
            case Kind::Raw:
                w.Emit(n.lemma, n.glue, n.id);
                prevKind = Kind::Raw;
                break;
        }
    }
    return w.out;
}

}  // namespace hdTrans::reg
