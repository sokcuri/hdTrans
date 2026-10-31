#include <algorithm>
#include <string>

#include "structure.h"

namespace hd8 {
namespace {

bool IsNounLike(Wc w) {
    return w == Wc::noun || w == Wc::noun_adv || w == Wc::propn || w == Wc::person || w == Wc::num || w == Wc::pron;
}
bool IsVerbal(Wc w) { return w == Wc::verb || w == Wc::verb_dep || w == Wc::adj || w == Wc::adj_dep; }
bool IsPunct(Wc w) {
    return w == Wc::comma || w == Wc::period || w == Wc::open || w == Wc::close || w == Wc::sym || w == Wc::space;
}

R RowRole(const TokInfo& t) { return t.row != nullptr ? t.row->sets.role : R::none; }

bool IsAuxPredicateRole(R r) {
    return r == R::eval || r == R::eval_neg || r == R::eval_ok || r == R::sumu || r == R::irareru ||
           r == R::absorb_neg;
}

bool HasPredToken(const std::vector<TokInfo>& info, const Bunsetsu& b) {
    for (int i = b.first; i <= b.last; ++i) {
        Wc w = info[i].wc;
        if (w == Wc::aux && RowRole(info[i]) == R::cop_ni && i > b.first &&
            (info[i - 1].wc == Wc::na || IsNounLike(info[i - 1].wc)))
            continue;  // 密かに, 静かに: adverbial
        if (IsParticleVerb(info[i])) continue;
        if (IsVerbal(w) || w == Wc::aux || w == Wc::na_aux || w == Wc::suf_adj || w == Wc::suf_v) return true;
    }
    return false;
}

bool HasLinkRow(const std::vector<TokInfo>& info, const Bunsetsu& b) {
    for (int i = b.first; i <= b.last; ++i)
        if (info[i].row != nullptr && info[i].row->sets.link >= 0) return true;
    return false;
}

// Whether bunsetsu x continues the predicate of the clause that ends at x-1 (auxiliary predicates,
// formal nouns, negation after は).
bool Continues(const std::vector<TokInfo>& info, const Path& path, int x, int prevContentRole) {
    const Bunsetsu& prevBun = path.buns[x - 1];
    for (int i = prevBun.first; i <= prevBun.last; ++i)
        if (info[i].wc == Wc::period) return false;
    const Bunsetsu& b = path.buns[x];
    const TokInfo& c = info[b.content];
    R role = RowRole(c);
    if (c.row == nullptr) {
        return c.wc == Wc::na_aux;
    }
    if (IsAuxPredicateRole(role)) return true;
    if (c.row->kind == AuxKind::neg && !c.row->afters.empty()) return true;  // わけでは|ない
    if (role == R::pot && prevContentRole == static_cast<int>(R::nomi)) return true;  // ことが|できる
    if (c.row->kind == AuxKind::fn) {
        if (HasLinkRow(info, b) || HasPredToken(info, b)) return true;
        if (x + 1 < static_cast<int>(path.buns.size())) {
            const TokInfo& n = info[path.buns[x + 1].content];
            if (n.row != nullptr && (IsAuxPredicateRole(RowRole(n)) || (n.row->kind == AuxKind::neg && !n.row->afters.empty()) ||
                                     (RowRole(n) == R::pot && role == R::nomi)))
                return true;
        }
    }
    return false;
}

struct Fold {
    Clause& c;
    const std::vector<TokInfo>& info;
    const Strings& st;
    const std::vector<Token>& toks;
    int negs = 0;           // negations that count for polarity
    bool absorbNext = false;
    bool sawCond = false, sawTeWa = false, sawTeMo = false, sawEval = false, sawEvalOk = false, sawNomi = false;
    bool sawCopJa = false, sawZu = false, sawIrareru = false, sawSumu = false, sawNegN = false, sawFnNeg = false;
    bool rare = false, caus = false, pot = false, sawQ = false, sawExcl = false, sawJan = false, sawNaProhib = false;
    bool sawVol = false, honPrefix = false, sawNaru = false, sawLight = false, negTe = false;
    int lastLink = -1, lastForce = -1, lastLinkSub = -1;
    int negBeforeCond = 0, negAfterEval = 0, negAfterCopJa = 0;
    int predBeforeCopJa = -1;  // word class before じゃ/で when it follows a predicate (rhetorical)
    int lastInflected = -1;

    void Apply(const AuxSets& s, int tok, R role) {
        if (s.tense >= 0) c.tense = static_cast<Tense>(s.tense);
        if (s.aspect >= 0) c.aspect = static_cast<Aspect>(s.aspect);
        if (s.bene >= 0) c.bene = static_cast<Bene>(s.bene);
        if (s.polite >= 0 && static_cast<int>(c.polite) < s.polite) c.polite = static_cast<Polite>(s.polite);
        if (s.voice >= 0) {
            Voice v = static_cast<Voice>(s.voice);
            if (v == Voice::rare) rare = true;
            if (v == Voice::causative) caus = true;
            if (v == Voice::potential) pot = true;
        }
        c.modality |= s.mod;
        c.jreg = static_cast<uint16_t>(c.jreg | s.jreg);
        if (s.link >= 0) {
            lastLink = s.link;
            lastLinkSub = s.linkSub;
        }
        if (s.force >= 0) lastForce = s.force;
        switch (role) {
            case R::neg:
            case R::neg_n:
            case R::zu:
                if (role == R::neg_n) sawNegN = true;
                if (role == R::zu) sawZu = true;
                if (absorbNext) {
                    absorbNext = false;
                    break;
                }
                ++negs;
                if (sawEval || sawEvalOk) ++negAfterEval;
                else if (sawCopJa) ++negAfterCopJa;
                else if (!sawCond && !sawTeWa) ++negBeforeCond;
                break;
            case R::cond: sawCond = true; break;
            case R::wa: if (lastLink == static_cast<int>(Link::te) || sawCond || sawZu) sawTeWa = true; break;
            case R::mo: if (lastLink == static_cast<int>(Link::te)) sawTeMo = true; break;
            case R::eval: case R::eval_neg: sawEval = true; if (role == R::eval_neg) ++negAfterEval; break;
            case R::eval_ok: sawEvalOk = true; break;
            case R::absorb_neg: absorbNext = true; break;
            case R::nomi: sawNomi = true; break;
            case R::cop_ja: case R::cop_te:
                sawCopJa = true;
                if (tok > 0 && !sawNomi && IsVerbal(info[tok - 1].wc)) predBeforeCopJa = static_cast<int>(info[tok - 1].wc);
                if (tok > 0 && !sawNomi && info[tok - 1].wc == Wc::aux) predBeforeCopJa = static_cast<int>(Wc::aux);
                break;
            case R::irareru: sawIrareru = true; break;
            case R::sumu: sawSumu = true; break;
            case R::q: sawQ = true; break;
            case R::excl: sawExcl = true; break;
            case R::jan: sawJan = true; break;
            case R::na_prohib: sawNaProhib = true; break;
            case R::vol: sawVol = true; break;
            case R::hon_prefix: honPrefix = true; break;
            case R::naru: sawNaru = true; break;
            case R::light: sawLight = true; break;
            default: break;
        }
        if (role == R::wake || role == R::nomi) {
            // のだ / わけだ: explanatory when a copula follows (checked by the caller through sawNomi)
        }
    }
};

Case BunCase(const Bunsetsu& b) { return b.kase; }

// The clause ends in の+で (ので), which is a cause link, not an explanatory のだ.
bool linkTokIsNode(const std::vector<TokInfo>& info, const Bunsetsu& lb, int content) {
    for (int k = lb.last; k > content; --k) {
        if (IsPunct(info[k].wc)) continue;
        return RowRole(info[k]) == R::cop_te && RowRole(info[k - 1]) == R::nomi;
    }
    return false;
}

// The lemma of a bunsetsu's noun: the lemmas of the noun compound that ends at the content token (同級+生).
uint32_t NounLemma(Strings& st, const std::vector<TokInfo>& info, const Path& path, const Bunsetsu& b) {
    int c = b.content;
    int k = c;
    while (k > b.first && (IsNounLike(info[k - 1].wc) || info[k - 1].wc == Wc::pref) && IsNounLike(info[c].wc)) --k;
    if (k == c) return path.toks[c].lemma;
    std::string s;
    for (int i = k; i <= c; ++i) s += st.Get(path.toks[i].lemma);
    return st.Intern(s);
}

Role RoleOf(Case k, bool& ok, uint8_t& how) {
    ok = true;
    how = static_cast<uint8_t>(ArgHow::marked);
    switch (k) {
        case Case::ga: return Role::subj;
        case Case::wo: return Role::obj;
        case Case::ni: return Role::dat;
        case Case::de: return Role::loc;
        case Case::kara: return Role::from;
        case Case::to: return Role::with;
        case Case::made: return Role::to;
        case Case::he: return Role::to;
        case Case::yori: return Role::cmp;
        case Case::wa: case Case::mo:
            how = static_cast<uint8_t>(ArgHow::topic);
            return Role::topic;
        default:
            ok = false;
            return Role::subj;
    }
}

Decision MakeDecision(Strings& st, const Path& path, const Clause& c, Ev kind, const std::string& rule, float margin,
                      const std::vector<std::string>& because) {
    Decision d;
    d.stage = 'L';
    d.kind = kind;
    d.rule = st.Intern(rule);
    const Bunsetsu& a = path.buns[c.pred];
    const Bunsetsu& z = path.buns[c.lastBun];
    d.src = Span{path.toks[a.first].src.begin, path.toks[z.last].src.end};
    d.margin = margin;
    d.nAlt = margin > 0 ? 2 : 1;
    for (const auto& b : because) d.because.push_back(st.Intern(b));
    return d;
}

}  // namespace

void BuildClauses(const StructureData&, Strings& st, const std::vector<TokInfo>& info, Path& path,
                  std::vector<Decision>& decisions) {
    path.clauses.clear();
    const int nb = static_cast<int>(path.buns.size());
    if (nb == 0) return;
    const auto& toks = path.toks;
    // Quote depth per token from the bracket classes.
    std::vector<int> depth(toks.size(), 0);
    int dpt = 0;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (info[i].wc == Wc::open) ++dpt;
        depth[i] = dpt;
        if (info[i].wc == Wc::close && dpt > 0) --dpt;
    }
    // 1. Predicate groups: a predicate bunsetsu plus the bunsetsu that continue it.
    std::vector<int> clauseOf(nb, -1);
    for (int b = 0; b < nb; ++b) {
        const Bunsetsu& bu = path.buns[b];
        bool pred = HasPredToken(info, bu);
        bool last = b == nb - 1;
        // N の ため / N の うちに: a formal noun with a clause link heads a predicate-less clause.
        const TokInfo& ct = info[bu.content];
        bool fnLink = !pred && ct.row != nullptr && ct.row->kind == AuxKind::fn && HasLinkRow(info, bu) && b > 0 &&
                      path.buns[b - 1].kase == Case::no && clauseOf[b] < 0;
        if (!pred && !fnLink && !(last && bu.type == 'N' && IsNounLike(info[bu.content].wc))) continue;
        if (!path.clauses.empty() && clauseOf[b] < 0) {
            Clause& prev = path.clauses.back();
            if (prev.lastBun == b - 1 || (prev.lastBun == b - 2 && clauseOf[b - 1] < 0 && false)) {
                int prevRole = static_cast<int>(RowRole(info[path.buns[b - 1].content]));
                if (Continues(info, path, b, prevRole)) {
                    prev.lastBun = static_cast<uint16_t>(b);
                    clauseOf[b] = static_cast<int>(path.clauses.size()) - 1;
                    continue;
                }
            }
        }
        Clause c;
        c.pred = c.firstBun = c.lastBun = static_cast<uint16_t>(b);
        clauseOf[b] = static_cast<int>(path.clauses.size());
        path.clauses.push_back(std::move(c));
        // Formal nouns and auxiliary predicates that follow are absorbed as the loop goes on.
        for (int x = b + 1; x < nb; ++x) {
            const Bunsetsu& xb = path.buns[x];
            int prevRole = static_cast<int>(RowRole(info[path.buns[x - 1].content]));
            bool xPred = xb.type == 'P' || HasPredToken(info, xb);
            if (!Continues(info, path, x, prevRole)) break;
            // A formal noun without a predicate continues only toward a following auxiliary predicate.
            path.clauses.back().lastBun = static_cast<uint16_t>(x);
            clauseOf[x] = static_cast<int>(path.clauses.size()) - 1;
            b = x;
            (void)xPred;
            if (HasLinkRow(info, xb) && info[xb.content].row != nullptr && info[xb.content].row->kind == AuxKind::fn)
                break;
        }
    }
    const int nc = static_cast<int>(path.clauses.size());
    if (nc == 0) return;
    // 2. Fold the morphemes of each predicate group.
    for (int ci = 0; ci < nc; ++ci) {
        Clause& c = path.clauses[ci];
        Fold f{c, info, st, toks};
        const Bunsetsu& pb = path.buns[c.pred];
        const Bunsetsu& lb = path.buns[c.lastBun];
        int content = pb.content;
        int lastTok = lb.last;
        // Inflected tokens of the group; the last one decides the clause form.
        for (int k = content; k <= lastTok; ++k)
            if (info[k].form != Form::none) f.lastInflected = k;
        for (int k = pb.first; k <= lastTok; ++k) {
            const TokInfo& t = info[k];
            bool isContent = k == content;
            if (k < content) {
                if (RowRole(t) == R::hon_prefix) f.honPrefix = true;
                continue;
            }
            const AuxRow* applied = nullptr;
            if (isContent) {
                if (t.row != nullptr && (t.row->kind == AuxKind::voice || t.row->kind == AuxKind::neg ||
                                         IsAuxPredicateRole(t.row->sets.role) ||
                                         (t.row->kind == AuxKind::fn && t.row->sets.link >= 0)))
                    applied = t.row;
            } else if (t.row != nullptr && !(IsNounLike(t.wc) && t.row->kind != AuxKind::fn)) {
                applied = t.row;
            }
            if (applied != nullptr) {
                // An inflecting word after a link continues the predicate (て|もらう, なければ|ならない).
                if (!isContent && (IsVerbal(t.wc) || IsAuxPredicateRole(applied->sets.role)) && applied->sets.link < 0)
                    f.lastLink = -1;
                if (!isContent && applied->kind == AuxKind::fn) c.tense = Tense::nonpast;
                if (applied->sets.role == R::te && k > 0 && (RowRole(info[k - 1]) == R::neg)) f.negTe = true;
                f.Apply(applied->sets, k, applied->sets.role);
                c.consumed.push_back(static_cast<uint16_t>(k));
            } else if (!isContent) {
                Wc w = t.wc;
                bool grammatical = w == Wc::aux || w == Wc::p_case || w == Wc::p_conj || w == Wc::p_final ||
                                   w == Wc::p_adv || w == Wc::p_topic || w == Wc::p_nom || w == Wc::na_aux ||
                                   w == Wc::suf_v || w == Wc::suf_adj;
                if (grammatical) c.unfolded.push_back(static_cast<uint16_t>(k));
            }
            if (k == f.lastInflected && t.frow != nullptr) {
                const AuxSets& fs = t.frow->sets;
                AuxSets only;
                only.link = fs.link;
                only.force = applied != nullptr && applied->sets.force >= 0 ? -1 : fs.force;
                only.mod = applied != nullptr && applied->sets.mod != 0 ? 0 : fs.mod;
                f.Apply(only, k, fs.role);
            }
        }
        // Clause-final punctuation within the sentence.
        bool sentenceEnd = c.lastBun == nb - 1;
        int after = lastTok + 1;
        while (after < static_cast<int>(toks.size()) && IsPunct(info[after].wc)) {
            if (info[after].wc == Wc::period || info[after].wc == Wc::sym) {
                sentenceEnd = true;
                if (info[after].row != nullptr) {
                    f.Apply(info[after].row->sets, after, info[after].row->sets.role);
                }
            }
            if (info[after].wc == Wc::close) sentenceEnd = true;
            ++after;
        }
        for (int k = lb.first; k <= lb.last; ++k)
            if ((info[k].wc == Wc::period || info[k].wc == Wc::sym) && info[k].row != nullptr && k > lastTok - 1)
                f.Apply(info[k].row->sets, k, info[k].row->sets.role);
        // Voice.
        if (f.caus && f.rare) c.voice = Voice::caus_pass;
        else if (f.caus) c.voice = Voice::causative;
        else if (f.rare) c.voice = Voice::rare;
        else if (f.pot) c.voice = Voice::potential;
        if (f.honPrefix && f.sawNaru) c.polite = std::max(c.polite, Polite::sonkei);
        if (f.honPrefix && f.sawLight) c.polite = std::max(c.polite, Polite::kenjo);
        // Explanatory のだ, わけだ.
        bool nodeLink = linkTokIsNode(info, lb, content);
        if (!nodeLink && f.sawNomi && (f.sawCopJa || std::any_of(c.consumed.begin(), c.consumed.end(), [&](uint16_t k) {
                              R r = RowRole(info[k]);
                              return r == R::cop || r == R::daro;
                          })))
            c.modality |= Explanatory;
        // Link.
        Wc lastWc = info[f.lastInflected >= 0 ? f.lastInflected : content].wc;
        Form lastForm = f.lastInflected >= 0 ? info[f.lastInflected].form : Form::none;
        int linkTok = -1;
        for (int k = lastTok; k > content; --k) {
            if (IsPunct(info[k].wc)) continue;
            linkTok = k;
            break;
        }
        R lastRole = linkTok >= 0 ? RowRole(info[linkTok]) : R::none;
        Link link = Link::final;
        if (f.lastLink >= 0) link = static_cast<Link>(f.lastLink);
        if (lastRole == R::quote || (lastRole == R::to && linkTok >= 0 && info[linkTok].wc == Wc::p_case)) {
            link = Link::quote;
            // と before a predicate that takes no quote is the condition (行かないと困る).
            int hb = lb.head;
            if (lastRole == R::to && hb >= 0) {
                bool sayHead = false;
                const Bunsetsu& hbu = path.buns[hb];
                for (int k = hbu.first; k <= hbu.last; ++k)
                    if (RowRole(info[k]) == R::say) sayHead = true;
                if (!sayHead) link = Link::to;
            }
        }
        else if (lastRole == R::cop_te && linkTok > 0 && RowRole(info[linkTok - 1]) == R::nomi)
            link = Link::node;  // の+で: ので
        else if (lastRole == R::ni && f.sawZu) link = Link::zu;
        else if (lastRole == R::ni && linkTok > 0 && RowRole(info[linkTok - 1]) == R::nomi) link = Link::noni;
        else if (lastRole == R::mo && f.lastLink == static_cast<int>(Link::te)) link = Link::temo;
        else if (lastRole == R::cop_ni && (c.modality & Likeness)) link = Link::you_ni;
        else if (lastRole == R::nomi || (linkTok >= 0 && info[linkTok].wc == Wc::p_case && f.sawNomi))
            link = Link::nom;
        else if (f.lastLink < 0 || (linkTok >= 0 && (info[linkTok].row == nullptr || info[linkTok].row->sets.link < 0) &&
                                    !IsPunct(info[linkTok].wc) && info[linkTok].wc != Wc::p_final)) {
            if (f.lastLink < 0) {
                if (lastForm == Form::rentai && !sentenceEnd && lb.head >= 0 &&
                    IsNounLike(info[path.buns[lb.head].content].wc) && info[lb.last].wc != Wc::comma)
                    link = Link::adn;
                else if ((lastForm == Form::renyo || lastForm == Form::renyo_onbin) && !sentenceEnd &&
                         (IsVerbal(lastWc) || lastWc == Wc::aux))
                    link = Link::renyo;
                else if (lb.head >= 0 && !sentenceEnd && (lastForm == Form::rentai || lastForm == Form::shushi) &&
                         IsNounLike(info[path.buns[lb.head].content].wc))
                    link = Link::adn;
            }
        }
        bool commaAfter = (lb.flags & CommaAfter) != 0;
        if (link == Link::final && lb.head >= 0 && !sentenceEnd && !commaAfter) {
            if (lastForm == Form::rentai || (lastWc == Wc::aux && info[lastTok].form == Form::rentai))
                if (IsNounLike(info[path.buns[lb.head].content].wc)) link = Link::adn;
        }
        // A te-link that ends a sentence is a request (待って！); in narration it stays te.
        bool finalClause = sentenceEnd || lb.head < 0;
        // Only an action verb, not negated and not trailing off (…), makes such a request.
        bool trailing = false;
        for (int k = lastTok + 1; k < static_cast<int>(toks.size()) && IsPunct(info[k].wc); ++k)
            if (info[k].wc == Wc::sym || (info[k].row != nullptr && info[k].row->sets.role == R::end &&
                                          info[k].row->sets.force < 0 && info[k].wc != Wc::period))
                trailing = true;
        bool actionVerb = (info[content].wc == Wc::verb || info[content].wc == Wc::verb_dep) && !f.pot && !f.rare;
        if (finalClause && (link == Link::te || link == Link::renyo) && depth[lastTok] > 0 &&
            static_cast<int>(lastRole) != static_cast<int>(R::wa) && actionVerb && !trailing &&
            (f.negs == 0 || f.negTe)) {
            link = Link::final;
            if (f.lastForce < 0) f.lastForce = static_cast<int>(Force::request);
        }
        if (finalClause && (link == Link::ba || link == Link::to || link == Link::nara || link == Link::tara ||
                            (link == Link::te && f.sawTeWa)) &&
            f.negs > 0 && f.negBeforeCond > 0 && !f.sawEval) {
            // 行かなきゃ / 行かないと / 行かなくちゃ at the end of a sentence: obligation.
            link = Link::final;
            c.negFn = NegFn::obligation;
        }
        c.link = link;
        if (link == Link::time && f.lastLinkSub > 0) c.linkSub = static_cast<LinkSub>(f.lastLinkSub);
        // Polarity and its function.
        std::vector<std::string> because;
        NegFn fn = c.negFn;
        int negs = f.negs;
        bool prohibAct = false;
        if (fn == NegFn::obligation) {
            c.pol = Pol::pos;
            c.modality |= Must;
            because.push_back("neg+cond at sentence end");
        } else if (f.sawEval && f.negBeforeCond > 0 && f.negAfterEval > 0) {
            fn = NegFn::obligation;
            c.modality |= Must;
            negs = 0;
            because.push_back("neg+cond+eval+neg");
        } else if (f.sawEval && f.sawTeWa && f.negAfterEval > 0) {
            fn = NegFn::prohibitive;
            negs = 1;
            because.push_back("te+wa+eval+neg");
        } else if (f.sawEvalOk && f.sawTeMo) {
            c.modality |= May;
            because.push_back("te+mo+ok");
        }
        if (fn == NegFn::none && f.sawZu && f.sawIrareru && negs >= 2) {
            fn = NegFn::dbl;
            negs = 0;
            c.modality |= Must;
            because.push_back("zu+ni+wa+irareru+neg");
        }
        if (fn == NegFn::none && negs >= 2) {
            fn = NegFn::dbl;
            negs = 0;
            because.push_back("two negations");
        }
        bool question = f.sawQ || f.lastForce == static_cast<int>(Force::q);
        if (fn == NegFn::none && f.sawNaProhib) {
            fn = NegFn::prohibitive;
            because.push_back("shushi+na");
        }
        if (fn == NegFn::none && negs == 1) {
            fn = NegFn::plain;
            if (f.sawNegN && lastRole == R::to) {
                fn = NegFn::archaic_vol;
                negs = 0;
                c.modality |= Volitional;
                because.push_back("n+to");
            } else if (f.sawCopJa && f.negAfterCopJa > 0 && !f.sawNomi &&
                       (question || f.predBeforeCopJa >= 0)) {
                fn = NegFn::rhetorical;
                negs = 0;
                because.push_back(question ? "ja+nai+ka" : "pred+ja+nai");
            } else if (f.sawCopJa && f.negAfterCopJa > 0 && f.sawNomi) {
                if (question) {
                    fn = NegFn::rhetorical;
                    negs = 0;
                    because.push_back("no+ja+nai+q");
                } else if (finalClause && c.tense == Tense::nonpast && depth[lastTok] > 0 &&
                           (info[content].wc == Wc::verb || info[content].wc == Wc::verb_dep) && !f.pot &&
                           !f.rare && c.aspect != Aspect::te_aru) {
                    fn = NegFn::prohibitive;
                    prohibAct = true;
                    because.push_back("no+ja+nai at the end of a quoted line");
                }
            } else if ((f.pot || f.rare) && !f.caus) {
                fn = NegFn::potential_neg;
                because.push_back(f.pot ? "potential+neg" : "rare+neg");
            } else if (finalClause && f.negTe && (link == Link::final || link == Link::te) &&
                       (f.lastForce == static_cast<int>(Force::request) || f.lastForce < 0)) {
                if (f.lastForce < 0) f.lastForce = static_cast<int>(Force::request);
                fn = NegFn::prohibitive;
                because.push_back("nai+de at the end of a quoted line");
            } else if (finalClause && f.sawQ && c.polite == Polite::desu_masu && !f.sawNomi &&
                       (info[content].wc == Wc::verb || info[content].wc == Wc::verb_dep) && c.tense == Tense::nonpast) {
                fn = NegFn::rhetorical;
                negs = 0;
                c.force = Force::prop;
                because.push_back("masen+ka");
            }
        }
        if (fn == NegFn::archaic_vol || fn == NegFn::rhetorical || fn == NegFn::dbl || fn == NegFn::obligation)
            c.pol = Pol::pos;
        else
            c.pol = negs > 0 || fn == NegFn::prohibitive ? Pol::neg : Pol::pos;
        c.negFn = fn;
        // Force.
        Force force = Force::decl;
        if (finalClause) {
            if (f.lastForce >= 0) force = static_cast<Force>(f.lastForce);
            if (f.sawQ) force = Force::q;
            if (f.sawNaProhib) force = Force::prohib;
            if (prohibAct) force = Force::prohib;
            if (lastForm == Form::ishi && !(c.modality & Conjecture) && !f.sawQ) force = Force::prop;
            if (force == Force::decl && f.lastForce < 0 && lastForm == Form::meirei) force = Force::imp;
            if (c.force == Force::prop && fn == NegFn::rhetorical) force = Force::prop;
            if (force == Force::request && c.bene == Bene::give_in && lastForm == Form::meirei) force = Force::request;
        }
        c.force = force;
        if (fn != NegFn::none && fn != NegFn::plain)
            decisions.push_back(MakeDecision(st, path, c, because.size() == 1 && fn == NegFn::prohibitive && prohibAct
                                                              ? Ev::Default
                                                              : Ev::Cued,
                                             std::string("code:negFn.") + Name(fn), prohibAct ? 0.3f : 0.0f,
                                             because));
        (void)f.sawJan;
        (void)f.sawExcl;
        (void)f.sawSumu;
        (void)f.sawVol;
        (void)f.sawFnNeg;
    }
    // 3. Parents, spans and arguments.
    auto clauseOfBun = [&](int b) { return b >= 0 && b < nb ? clauseOf[b] : -1; };
    for (int ci = 0; ci < nc; ++ci) {
        Clause& c = path.clauses[ci];
        int h = path.buns[c.lastBun].head;
        int parent = -1;
        while (h >= 0) {
            if (clauseOfBun(h) >= 0 && clauseOfBun(h) != ci) {
                parent = clauseOfBun(h);
                break;
            }
            h = path.buns[h].head;
        }
        c.parent = static_cast<int16_t>(parent);
    }
    // Owner clause of every bunsetsu: the first clause on its head chain.
    std::vector<int> owner(nb, -1);
    for (int b = 0; b < nb; ++b) {
        int h = b;
        while (h >= 0 && clauseOf[h] < 0) h = path.buns[h].head;
        owner[b] = h >= 0 ? clauseOf[h] : -1;
    }
    for (int b = 0; b < nb; ++b) {
        if (owner[b] < 0) continue;
        Clause& c = path.clauses[owner[b]];
        c.firstBun = static_cast<uint16_t>(std::min<int>(c.firstBun, b));
    }
    for (int b = 0; b < nb; ++b) {
        if (clauseOf[b] >= 0) continue;
        const Bunsetsu& bu = path.buns[b];
        int h = bu.head;
        if (h < 0) continue;
        int ci = clauseOf[h];
        if (ci < 0) continue;  // depends on a noun: not an argument of a clause
        bool ok = false;
        uint8_t how = 0;
        Role role = RoleOf(BunCase(bu), ok, how);
        if (ok && (bu.flags & Topic)) how = static_cast<uint8_t>(ArgHow::topic);
        if (!ok && bu.kase == Case::no && !HasPredToken(info, path.buns[path.clauses[ci].pred])) {
            role = Role::subj;  // 平民のため: the noun of a formal-noun link
            ok = true;
        }

        if (!ok) {
            if ((bu.flags & CommaAfter) && IsNounLike(info[bu.content].wc) && bu.type == 'N') {
                role = Role::topic;
                how = static_cast<uint8_t>(ArgHow::topic);
                ok = true;
            }
        }
        if (!ok) continue;
        Arg a;
        a.role = role;
        a.bun = static_cast<uint16_t>(b);
        a.lemma = NounLemma(st, info, path, bu);
        a.cls = info[bu.content].wc == Wc::person ? st.Intern("@person") : 0;
        a.how = how;
        path.clauses[ci].args.push_back(a);
    }
    for (int ci = 0; ci < nc; ++ci) {
        Clause& c = path.clauses[ci];
        if (c.link == Link::adn) {
            int h = path.buns[c.lastBun].head;
            if (h >= 0) {
                Arg a;
                a.role = Role::head;
                a.bun = static_cast<uint16_t>(h);
                a.lemma = NounLemma(st, info, path, path.buns[h]);
                a.cls = info[path.buns[h].content].wc == Wc::person ? st.Intern("@person") : 0;
                a.how = static_cast<uint8_t>(ArgHow::relhead);
                c.args.push_back(a);
            }
        }
        // の marks the subject inside an adnominal clause (私の書いた本): a の-phrase right before the clause
        // that depends on the clause or on the noun the clause modifies.
        if (c.link == Link::adn && c.pred > 0) {
            int h = path.buns[c.lastBun].head;
            int b = c.pred - 1;
            while (b >= 0 && owner[b] == ci && path.buns[b].kase != Case::no) --b;
            bool haveSubj = std::any_of(c.args.begin(), c.args.end(), [](const Arg& a) { return a.role == Role::subj; });
            if (b >= 0 && !haveSubj && path.buns[b].kase == Case::no &&
                (path.buns[b].head == h || owner[b] == ci) && clauseOf[b] < 0) {
                Arg a;
                a.role = Role::subj;
                a.bun = static_cast<uint16_t>(b);
                a.lemma = NounLemma(st, info, path, path.buns[b]);
                a.cls = info[path.buns[b].content].wc == Wc::person ? st.Intern("@person") : 0;
                a.how = static_cast<uint8_t>(ArgHow::marked);
                c.args.push_back(a);
            }
        }
        bool hasSubj = std::any_of(c.args.begin(), c.args.end(),
                                   [](const Arg& a) { return a.role == Role::subj || a.role == Role::topic; });
        if (!hasSubj && c.link != Link::adn) {
            Arg a;
            a.role = Role::subj;
            a.how = static_cast<uint8_t>(ArgHow::omitted);
            c.args.push_back(a);
        }
        // られる is left for T; the facts it will read are recorded here.
        if (c.voice == Voice::rare || c.voice == Voice::caus_pass) {
            std::vector<std::string> because;
            for (const auto& a : c.args) {
                if (a.how == static_cast<uint8_t>(ArgHow::omitted)) continue;
                std::string s = std::string(Name(a.role)) + "=" + st.Get(a.lemma);
                if (a.cls) s += " " + st.Get(a.cls);
                because.push_back(s);
            }
            because.push_back(std::string("pol=") + Name(c.pol));
            if (c.polite != Polite::plain) because.push_back(std::string("polite=") + Name(c.polite));
            decisions.push_back(MakeDecision(st, path, c, Ev::Default, "aux:aux.reru", 0, because));
        }
    }
}

}  // namespace hd8
