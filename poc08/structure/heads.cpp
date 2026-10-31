#include <algorithm>
#include <cmath>
#include <string>

#include "structure.h"

namespace hd8 {
namespace {

constexpr float kHeadTau = 1.0f;  // head2 is kept when the margin is below this

bool IsNounLike(Wc w) {
    return w == Wc::noun || w == Wc::noun_adv || w == Wc::propn || w == Wc::person || w == Wc::num || w == Wc::pron;
}

}  // namespace

const char* DtName(Dt d) {
    switch (d) {
        case Dt::adn: return "adn";
        case Dt::topic: return "top";
        case Dt::pred: return "pred";
        case Dt::kase: return "case";
        case Dt::adv: return "adv";
        case Dt::conj: return "conj";
        default: return "other";
    }
}

const char* CaseName(Case k) {
    static const char* const names[] = {"none", "ga", "wo", "ni", "de", "to", "kara", "made", "yori", "he", "no",
                                        "wa", "mo", "nado", "quote"};
    return names[static_cast<int>(k)];
}

namespace {

const char* WcName(Wc w) {
    static const char* const names[] = {"", "noun", "noun-adv", "propn", "person", "num", "na-aux", "pron", "verb",
                                        "verb-dep", "adj", "adj-dep", "na", "adv", "adn", "conj", "intj", "filler",
                                        "aux", "p-case", "p-topic", "p-adv", "p-conj", "p-final", "p-nom", "pref",
                                        "suf-n", "suf-count", "suf-v", "suf-adj", "suf-na", "comma", "period", "open",
                                        "close", "sym", "space"};
    return names[static_cast<int>(w)];
}

const char* FormName(Form f) {
    static const char* const names[] = {"", "mizen", "renyo", "renyo-onbin", "renyo-fused", "renyo-ni", "shushi",
                                        "rentai", "katei", "katei-fused", "meirei", "ishi", "gokan", "izen"};
    return names[static_cast<int>(f)];
}

bool IsPunct(Wc w) {
    return w == Wc::comma || w == Wc::period || w == Wc::open || w == Wc::close || w == Wc::sym || w == Wc::space;
}

const char* DistBucket(int d) { return d == 1 ? "1" : d == 2 ? "2" : d == 3 ? "3" : d <= 5 ? "4-5" : "6+"; }
const char* Count(int c) { return c == 0 ? "0" : c == 1 ? "1" : "2+"; }

float Weight(const StructureData& d, const std::string& f) {
    auto it = std::lower_bound(d.headWeights.begin(), d.headWeights.end(), f,
                               [](const std::pair<std::string, float>& a, const std::string& b) { return a.first < b; });
    return it != d.headWeights.end() && it->first == f ? it->second : 0.0f;
}

// The hand rule of the rule chunker (B): adnominal → nearest noun, topic/conjunction/comma → the
// sentence-final predicate, everything else → nearest predicate.
int RuleHead(const std::vector<HeadFacts>& facts, int i) {
    const int n = facts[i].sentEnd + 1;
    for (int j = i + 1; j < n; ++j) {
        if (facts[i].dt == Dt::topic || facts[i].dt == Dt::conj) {
            if (j == n - 1 || (facts[j].hasPred && facts[j].endsSentence)) return j;
            continue;
        }
        if (facts[i].dt == Dt::adn ? facts[j].hasNoun : facts[j].hasPred) return j;
        if (j == n - 1) return j;
    }
    return n - 1;
}

bool AtomHolds(const StructureData::HeadAtom& a, const HeadFacts& f) {
    bool r = false;
    if (a.key == "comma") r = f.comma;
    else if (a.key == "nextnoun") r = f.nextNoun;
    else if (a.key == "pred") r = f.hasPred;
    else {
        std::string v = a.key == "dt" ? DtName(f.dt) : a.key == "kase" ? CaseName(f.kase) : a.key == "link" ? f.link
                        : a.key == "cwc" ? f.cwc : a.key == "last" ? f.last : a.key == "form" ? f.form : std::string();
        r = std::find(a.vals.begin(), a.vals.end(), v) != a.vals.end();
    }
    return a.neg ? !r : r;
}

// Whether candidate c is the kind of head a rule asks for (the sentence-final bunsetsu always is).
bool Takes(const std::string& head, const HeadFacts& c, bool last) {
    if (last || head == "next") return true;
    if (head == "noun") return c.hasNoun;
    if (head == "pred") return c.hasPred;
    if (head.compare(0, 6, "pred>=") == 0) {
        if (!c.hasPred) return false;
        if (c.dt == Dt::adn) return head[6] <= 'B';  // an attributive clause holds A and B clauses (行って取ってきた本)
        char lv = c.level ? c.level : (c.link == "final" ? 'D' : 0);
        return lv >= head[6];
    }
    return c.hasPred && c.endsSentence;  // final: a predicate that ends a sentence
}

// Right to left: each bunsetsu takes the first candidate on the head chain from its right neighbour (so the
// tree stays projective) that its most specific rule accepts; a candidate past the close of a quote the
// dependent sits in is not taken.
void RuleHeads(const StructureData& data, const std::vector<HeadFacts>& facts, Path& path) {
    const int total = static_cast<int>(path.buns.size());
    for (int i = total - 1; i >= 0; --i) {
        if (facts[i].sentEnd == i) continue;
        const StructureData::HeadRule* best = nullptr;
        for (const auto& r : data.headRules) {
            if (r.head.compare(0, 6, "level=") == 0) continue;
            bool all = true;
            for (const auto& a : r.dep) all = all && AtomHolds(a, facts[i]);
            if (all && (best == nullptr || r.dep.size() > best->dep.size())) best = &r;
        }
        const std::string head = best != nullptr ? best->head : "pred";
        const int end = facts[i].sentEnd;
        int pick = -1;
        for (int j = i + 1; j != -1 && j <= end; j = path.buns[j].head) {
            const bool outside = facts[j].depthIn < facts[i].depthOut;  // past the close of the dependent's quote
            if (!outside && Takes(head, facts[j], j == end)) {
                pick = j;
                break;
            }
            pick = j;  // the top of the chain if nothing takes it
            if (path.buns[j].head == -1) break;
        }
        Bunsetsu& b = path.buns[i];
        b.head = static_cast<int16_t>(pick >= 0 ? pick : end);
        b.headMargin = 0;
        b.head2 = -1;
    }
}

}  // namespace

bool LoadHeadRules(std::string_view tsv, StructureData& out, std::string& err) {
    size_t p = 0;
    bool header = false;
    while (p < tsv.size()) {
        size_t e = tsv.find('\n', p);
        std::string line(tsv.substr(p, e == std::string_view::npos ? std::string_view::npos : e - p));
        p = e == std::string_view::npos ? tsv.size() : e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (!header) {
            header = true;
            continue;
        }
        std::vector<std::string> f;
        size_t q = 0;
        while (true) {
            size_t t = line.find('\t', q);
            f.push_back(line.substr(q, t == std::string::npos ? std::string::npos : t - q));
            if (t == std::string::npos) break;
            q = t + 1;
        }
        if (f.size() < 4) {
            err = "heads.tsv: short row " + line;
            return false;
        }
        StructureData::HeadRule r;
        r.id = f[0];
        r.head = f[3];
        const std::string& dep = f[2];
        size_t a = 0;
        while (dep != "*" && a <= dep.size()) {
            size_t amp = dep.find('&', a);
            std::string t = dep.substr(a, amp == std::string::npos ? std::string::npos : amp - a);
            size_t lo = t.find_first_not_of(' '), hi = t.find_last_not_of(' ');
            t = lo == std::string::npos ? std::string() : t.substr(lo, hi - lo + 1);
            StructureData::HeadAtom at;
            if (!t.empty() && t[0] == '!') {
                at.neg = true;
                t = t.substr(1);
            }
            size_t eq = t.find('=');
            at.key = t.substr(0, eq);
            if (eq != std::string::npos) {
                std::string v = t.substr(eq + 1);
                size_t b = 0;
                while (true) {
                    size_t bar = v.find('|', b);
                    at.vals.push_back(v.substr(b, bar == std::string::npos ? std::string::npos : bar - b));
                    if (bar == std::string::npos) break;
                    b = bar + 1;
                }
            }
            static const char* const kKeys[] = {"dt", "kase", "link", "cwc", "last", "form", "comma", "nextnoun", "pred"};
            if (std::find(std::begin(kKeys), std::end(kKeys), at.key) == std::end(kKeys)) {
                err = "heads.tsv: unknown key " + at.key + " in " + r.id;
                return false;
            }
            r.dep.push_back(std::move(at));
            if (amp == std::string::npos) break;
            a = amp + 1;
        }
        out.headRules.push_back(std::move(r));
    }
    return true;
}

std::vector<int> SentenceEnds(const std::vector<TokInfo>& info, const Path& path) {
    const int n = static_cast<int>(path.buns.size());
    std::vector<int> ends(n, n - 1);
    int depth = 0, start = 0;
    for (int b = 0; b < n; ++b) {
        bool end = false;
        for (int i = path.buns[b].first; i <= path.buns[b].last; ++i) {
            if (info[i].wc == Wc::open) ++depth;
            if (info[i].wc == Wc::close && depth > 0) --depth;
            if (info[i].wc == Wc::period && depth == 0) end = true;
        }
        if (end || b == n - 1) {
            for (int k = start; k <= b; ++k) ends[k] = b;
            start = b + 1;
        }
    }
    return ends;
}

std::vector<HeadFacts> ComputeHeadFacts(const StructureData& data, const Strings& st, const std::vector<TokInfo>& info,
                                        const Path& path) {
    const int n = static_cast<int>(path.buns.size());
    std::vector<HeadFacts> out(n);
    std::vector<int> ends = SentenceEnds(info, path);
    int depth = 0;
    for (int b = 0; b < n; ++b) {
        const Bunsetsu& bu = path.buns[b];
        HeadFacts& f = out[b];
        f.sentEnd = ends[b];
        f.kase = bu.kase;
        f.cwc = WcName(info[bu.content].wc);
        Wc cw = info[bu.content].wc;
        if (cw == Wc::verb || cw == Wc::verb_dep || cw == Wc::adj || cw == Wc::adj_dep)
            f.clemma = st.Get(path.toks[bu.content].lemma);
        f.depthIn = depth;
        int lastTok = -1;
        for (int i = bu.first; i <= bu.last; ++i) {
            Wc w = info[i].wc;
            if (w == Wc::open) {
                ++depth;
                f.opens = true;
            }
            if (w == Wc::close) {
                if (depth > 0) --depth;
                f.closes = true;
            }
            if (IsNounLike(w)) f.hasNoun = true;
            if ((w == Wc::verb || w == Wc::verb_dep || w == Wc::adj || w == Wc::adj_dep || w == Wc::aux) &&
                !IsParticleVerb(info[i]))
                f.hasPred = true;
            if (w == Wc::comma) f.comma = true;
            if (w == Wc::period) f.endsSentence = true;
            if (!IsPunct(w)) lastTok = i;
        }
        f.depthOut = depth;
        if (lastTok < 0) lastTok = bu.content;
        for (int i = lastTok; i >= bu.first && f.link.empty(); --i) {
            const AuxRow* r = info[i].row;
            if (r != nullptr && r->sets.link >= 0) f.link = Name(static_cast<Link>(r->sets.link));
            if (r == nullptr && info[i].wc != Wc::aux) break;
        }
        const TokInfo& lt = info[lastTok];
        for (int i = lastTok; i >= bu.first; --i)
            if (info[i].form != Form::none) {
                f.form = FormName(info[i].form);
                break;
            }
        Wc lw = lt.wc;
        bool function = lw == Wc::p_case || lw == Wc::p_topic || lw == Wc::p_adv || lw == Wc::p_conj ||
                        lw == Wc::p_final || lw == Wc::p_nom || lw == Wc::aux || lw == Wc::na_aux;
        f.last = function && lt.row != nullptr ? st.Get(lt.row->id) : std::string(WcName(lw));
        if (lt.form != Form::none) f.last += "/" + std::string(FormName(lt.form));
        Case k = bu.kase;
        if (bu.type == 'C') f.dt = Dt::conj;
        else if (bu.type == 'I') f.dt = Dt::topic;
        else if (k == Case::no || cw == Wc::adn) f.dt = Dt::adn;
        else if (k == Case::wa || k == Case::mo) f.dt = Dt::topic;
        else if ((bu.flags & Topic) && k != Case::none) f.dt = Dt::kase, f.kaseTopic = true;
        else if (f.hasPred || bu.type == 'P' || bu.type == 'Q') {
            bool adn = lt.form == Form::rentai && (lw == Wc::verb || lw == Wc::verb_dep || lw == Wc::adj ||
                                                    lw == Wc::adj_dep || lw == Wc::aux);
            if (lt.row != nullptr && lt.row->sets.link >= 0) adn = false;
            f.dt = adn ? Dt::adn : Dt::pred;
            if (lw == Wc::p_case || lw == Wc::p_adv) f.dt = Dt::kase;
        } else if (k != Case::none) f.dt = Dt::kase;
        else if (cw == Wc::adv) f.dt = Dt::adv;
        else if (f.comma) f.dt = Dt::topic;
        else f.dt = Dt::kase;
        if (f.link.empty()) {
            if (f.dt == Dt::adn) f.link = "adn";
            else if (f.hasPred && (f.endsSentence || b == f.sentEnd || f.form == "shushi" || f.form == "meirei" ||
                                   f.form == "ishi"))
                f.link = "final";
            else if (f.hasPred && (f.form == "renyo" || f.form == "renyo-onbin")) f.link = "renyo";
        }
    }
    for (int b = 0; b + 1 < n; ++b) out[b].nextNoun = out[b + 1].hasNoun && !out[b + 1].hasPred;
    for (auto& f : out) {
        f.level = 0;
        for (const auto& r : data.headRules) {
            if (r.head.compare(0, 6, "level=") != 0 || r.dep.empty()) continue;
            const auto& a = r.dep[0];
            if (a.key == "link" && std::find(a.vals.begin(), a.vals.end(), f.link) != a.vals.end()) f.level = r.head[6];
        }
    }
    return out;
}

void HeadFeatures(const std::vector<HeadFacts>& facts, const std::vector<int>& heads, int dep, int cand,
                  std::vector<std::string>& out) {
    out.clear();
    // Whether cand already governs a bunsetsu with the same case (two を on one verb are rare).
    bool sameCase = false;
    int kids = 0;
    for (int k = dep + 1; k < cand; ++k)
        if (heads[k] == cand) {
            ++kids;
            if (facts[k].kase == facts[dep].kase && facts[dep].kase != Case::none && facts[dep].kase != Case::no)
                sameCase = true;
        }
    const HeadFacts& d = facts[dep];
    const HeadFacts& c = facts[cand];
    const int n = d.sentEnd + 1;
    int preds = 0, commas = d.comma ? 1 : 0, topics = 0, nouns = 0, between = cand - dep - 1;
    bool firstPred = true, firstNoun = true, closeBetween = false;
    for (int k = dep + 1; k < cand; ++k) {
        const HeadFacts& m = facts[k];
        preds += m.hasPred;
        commas += m.comma;
        topics += m.dt == Dt::topic;
        nouns += m.hasNoun;
        if (m.hasPred) firstPred = false;
        if (m.hasNoun) firstNoun = false;
        if (m.closes && m.depthOut < d.depthOut) closeBetween = true;
    }
    if (d.closes && d.depthOut < d.depthIn) closeBetween = true;
    std::string dt = DtName(d.dt), ct = DtName(c.dt);
    std::string dist = DistBucket(cand - dep);
    std::string pb = Count(preds);
    std::string last = cand == n - 1 ? "L" : "-";
    std::string cpred = c.hasPred ? "P" : "-";
    std::string cnoun = c.hasNoun ? "N" : "-";
    std::string near = std::string(firstPred && c.hasPred ? "p" : "-") + (firstNoun && c.hasNoun ? "n" : "-");
    std::string quote = closeBetween ? "X" : (c.depthIn > d.depthOut ? "in" : "=");
    out.push_back("bias");
    out.push_back("dt=" + dt + "|cp=" + cpred + "|cn=" + cnoun);
    out.push_back("dt=" + dt + "|near=" + near);
    out.push_back("dt=" + dt + "|near=" + near + "|last=" + last);
    out.push_back("dt=" + dt + "|cp=" + cpred + "|dist=" + dist);
    out.push_back("dt=" + dt + "|ct=" + ct + "|last=" + last);
    out.push_back("dt=" + dt + "|quote=" + quote + "|last=" + last);
    out.push_back("dl=" + d.last + "|near=" + near);
    out.push_back("dl=" + d.last + "|cp=" + cpred + "|cn=" + cnoun);
    out.push_back("dl=" + d.last + "|dist=" + dist);
    out.push_back("dl=" + d.last + "|preds=" + pb);
    out.push_back("dl=" + d.last + "|ct=" + ct);
    out.push_back("dl=" + d.last + "|cl=" + c.last);
    out.push_back("dl=" + d.last + "|cf=" + c.form + "|ct=" + ct);
    out.push_back("dl=" + d.last + "|last=" + last + "|preds=" + pb);
    out.push_back("dt=" + dt + "|preds=" + pb + "|cp=" + cpred);
    out.push_back("dt=" + dt + "|nouns=" + Count(nouns) + "|cn=" + cnoun);
    out.push_back("dt=" + dt + "|commas=" + Count(commas) + "|last=" + last);
    out.push_back("dt=" + dt + "|commas=" + Count(commas) + "|preds=" + pb);
    out.push_back("dt=" + dt + "|topics=" + Count(topics) + "|last=" + last);
    out.push_back("dt=" + dt + "|cend=" + (c.endsSentence ? "1" : "0") + "|cp=" + cpred);
    out.push_back("dt=" + dt + "|cwc=" + c.cwc + "|cf=" + c.form);
    out.push_back("dl=" + d.last + "|cwc=" + c.cwc);
    out.push_back("dwc=" + d.cwc + "|cwc=" + c.cwc + "|dist=" + dist);
    out.push_back("dt=" + dt + "|cl=" + c.last + "|last=" + last);
    out.push_back("dcomma=" + std::string(d.comma ? "1" : "0") + "|dt=" + dt + "|preds=" + pb);
    out.push_back("df=" + d.form + "|cf=" + c.form + "|dist=" + dist);
    out.push_back("between=" + std::to_string(std::min(between, 6)) + "|dt=" + dt + "|last=" + last);
    out.push_back("samecase=" + std::string(sameCase ? "1" : "0") + "|dl=" + d.last);
    out.push_back("kids=" + std::string(Count(kids)) + "|dt=" + dt + "|cp=" + cpred);
    out.push_back("dtop=" + std::string(d.kaseTopic ? "1" : "0") + "|dl=" + d.last + "|last=" + last);
    // Rank of the candidate among the predicates (or nouns) to the right, and richer conjunctions.
    int predRank = 0, nounRank = 0;
    for (int k = dep + 1; k <= cand; ++k) {
        predRank += facts[k].hasPred;
        nounRank += facts[k].hasNoun;
    }
    std::string pr = c.hasPred ? std::to_string(std::min(predRank, 4)) : "-";
    std::string nr = c.hasNoun ? std::to_string(std::min(nounRank, 4)) : "-";
    out.push_back("dl=" + d.last + "|prank=" + pr + "|last=" + last);
    out.push_back("dt=" + dt + "|nrank=" + nr + "|prank=" + pr);
    out.push_back("dl=" + d.last + "|cl=" + c.last + "|dist=" + dist);
    out.push_back("dl=" + d.last + "|cwc=" + c.cwc + "|near=" + near);
    out.push_back("dl=" + d.last + "|ct=" + ct + "|commas=" + Count(commas));
    out.push_back("df=" + d.form + "|cl=" + c.last + "|prank=" + pr);
    out.push_back("dwc=" + d.cwc + "|dl=" + d.last + "|cp=" + cpred + "|prank=" + pr);
    out.push_back("dl=" + d.last + "|kids=" + Count(kids) + "|cp=" + cpred);
    out.push_back("dl=" + d.last + "|quote=" + quote + "|cp=" + cpred);
    if (!c.clemma.empty()) {
        out.push_back("dl=" + d.last + "|clem=" + c.clemma);
        out.push_back("dt=" + dt + "|clem=" + c.clemma + "|near=" + near);
    }
}

void AttachHeads(const StructureData& data, const Strings& st, const std::vector<TokInfo>& info, Path& path,
                 std::vector<Decision>& decisions, Strings& interned) {
    const int total = static_cast<int>(path.buns.size());
    if (total == 0) return;
    std::vector<HeadFacts> facts = ComputeHeadFacts(data, st, info, path);
    for (int i = 0; i < total; ++i)
        if (facts[i].sentEnd == i) {
            path.buns[i].head = -1;
            path.buns[i].head2 = -1;
            path.buns[i].headMargin = 0;
        }
    if (!data.headRules.empty()) {
        RuleHeads(data, facts, path);
        return;
    }
    if (data.headWeights.empty()) {
        for (int i = 0; i < total; ++i) {
            if (facts[i].sentEnd == i) continue;
            path.buns[i].head = static_cast<int16_t>(RuleHead(facts, i));
            path.buns[i].headMargin = 0;
            path.buns[i].head2 = -1;
        }
        return;
    }
    // Right to left; a candidate must lie on the head chain from i+1, so the tree stays projective.
    std::vector<std::string> feats;
    std::vector<int> heads(total, -1);
    for (int i = total - 1; i >= 0; --i) {
        if (facts[i].sentEnd == i) continue;
        float best = -1e30f, second = -1e30f;
        int bestJ = i + 1, secondJ = -1;
        for (int j = i + 1; j != -1 && j <= facts[i].sentEnd; j = path.buns[j].head) {
            HeadFeatures(facts, heads, i, j, feats);
            float s = 0;
            for (const auto& f : feats) s += Weight(data, f);
            if (s > best) {
                second = best;
                secondJ = bestJ == j ? -1 : bestJ;
                if (best < -1e29f) secondJ = -1;
                best = s;
                bestJ = j;
            } else if (s > second) {
                second = s;
                secondJ = j;
            }
            if (path.buns[j].head == -1) break;
        }
        Bunsetsu& b = path.buns[i];
        b.head = static_cast<int16_t>(bestJ);
        heads[i] = bestJ;
        b.headMargin = secondJ < 0 ? 99.0f : best - second;
        b.head2 = secondJ >= 0 && b.headMargin < kHeadTau ? static_cast<int16_t>(secondJ) : -1;
        if (b.head2 >= 0) {
            Decision d;
            d.stage = 'C';
            d.kind = Ev::Default;
            d.rule = interned.Intern("code:AttachHeads");
            d.src = Span{path.toks[b.first].src.begin, path.toks[b.last].src.end};
            d.score = best;
            d.margin = b.headMargin;
            d.nAlt = 2;
            d.because.push_back(interned.Intern("head b" + std::to_string(i) + "→b" + std::to_string(bestJ) +
                                                " (b" + std::to_string(secondJ) + ")"));
            decisions.push_back(std::move(d));
        }
    }
}

}  // namespace hd8
