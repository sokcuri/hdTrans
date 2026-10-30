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

}  // namespace

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

std::vector<HeadFacts> ComputeHeadFacts(const StructureData&, const Strings& st, const std::vector<TokInfo>& info,
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
