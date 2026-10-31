#include "hd8gen/analyze.h"

#include <algorithm>

#include "tables.h"

namespace hd8gen {

std::string Realize(const Tables& t, const Bundle& b, const std::string& host) {
    std::vector<Node> nodes;
    if (!host.empty()) {
        Node h;
        h.kind = Kind::Raw;
        h.lemma = host;
        h.id = h.origin = 1;
        nodes.push_back(h);
    }
    Node v = b.node;
    v.id = v.origin = 2;
    v.glue = false;
    nodes.push_back(v);
    Reg r;
    r.node = 2;
    r.level = b.level == Level::none ? Level::haera : b.level;
    r.hon = b.hon;
    UttLevel u;
    u.level = r.level;
    u.narration = true;  // no spoken fusions inside one word
    std::string out = Generate(t, nodes, {r}, {u}).out;
    if (!host.empty() && out.rfind(host, 0) == 0) out = out.substr(host.size());
    if (!out.empty() && out[0] == ' ') out = out.substr(1);
    return out;
}

namespace {
bool ParseMood(const std::string& s, Mood& m) {
    const char* names[] = {"decl", "q", "imp", "prop", "excl"};
    for (int i = 0; i < 5; ++i)
        if (s == names[i]) {
            m = static_cast<Mood>(i);
            return true;
        }
    return false;
}
}  // namespace

std::vector<std::pair<std::string, Bundle>> Paradigm(const Tables& t, const std::string& lemma, const std::string& posIn,
                                                     const std::string& clsIn, const std::string& host) {
    const PredicateInfo info = LookupPredicate(t, lemma);
    const std::string pos = posIn.empty() ? info.pos : posIn;
    const std::string cls = clsIn.empty() ? info.cls : clsIn;
    std::vector<std::pair<std::string, Bundle>> out;
    for (const EomiRow& row : t.eomi) {
        if (row.slot == "hon" || row.slot == "tense" || row.slot == "modal" || row.words) continue;
        std::vector<Mood> moods;
        if (row.slot == "final") {
            Mood m;
            if (row.mood == "-") moods = {Mood::decl, Mood::q};
            else if (ParseMood(row.mood, m)) moods = {m};
        } else {
            moods = {Mood::decl};
        }
        for (Mood mood : moods)
            for (int pre = 0; pre < 8; ++pre) {
                const bool past = pre & 1, gess = pre & 2, hon = pre & 4;
                if (row.slot == "stem" && (past || gess)) continue;
                if ((row.slot == "adn" || row.slot == "nom") && (gess || (past && row.family != "fut" && row.family != "m" && row.family != "gi")))
                    continue;
                Bundle b;
                Node& v = b.node;
                v.kind = Kind::V;
                v.lemma = lemma;
                v.vpos = pos;
                v.kcls = cls;
                v.tense = past ? Tense::past : Tense::nonpast;
                v.modal = gess ? "gess" : "";
                v.mood = mood;
                b.hon = hon;
                b.row = row.id;
                if (row.slot == "final") {
                    v.ctype = CType::final_;
                    if (row.family != "plain") v.eomi = row.family;
                    ParseLevel(row.level, b.level);
                } else if (row.slot == "conn") {
                    v.ctype = CType::conn;
                    v.eomi = row.id;
                } else if (row.slot == "adn") {
                    v.ctype = CType::adn;
                    v.rel = row.family;
                } else if (row.slot == "nom") {
                    v.ctype = CType::nom;
                    v.rel = row.family;
                } else if (row.slot == "quote") {
                    v.ctype = CType::quote;
                    if (!ParseMood(row.family, v.mood)) v.rel = row.family;
                } else if (row.slot == "stem") {
                    v.ctype = CType::stem;
                } else {
                    continue;
                }
                out.emplace_back(Realize(t, b, host), b);
            }
    }
    return out;
}

void RankBundles(std::vector<Bundle>& bs, const AnalyzeHints& h) {
    auto score = [&](const Bundle& b) {
        const Node& v = b.node;
        int s = 0;
        const bool isFinal = v.ctype == CType::final_;
        switch (h.want) {
            case Want::final_: s += isFinal ? 20 : 0; break;
            case Want::conn: s += v.ctype == CType::conn ? 20 : (v.ctype == CType::quote ? 10 : 0); break;
            // a word inside the sentence that reads both as a final ending and as a quote (파기한다고 선언,
            // 있다고 말하면서) is the quote: the final reading would put a sentence end (and 요) mid-sentence
            case Want::adn: s += v.ctype == CType::adn ? 20 : (v.ctype == CType::conn ? 15 : (v.ctype == CType::quote ? 10 : 0)); break;
            case Want::nom:
                s += v.ctype == CType::nom ? 20 : (v.ctype == CType::conn || v.ctype == CType::quote ? 10 : (v.ctype == CType::adn ? 5 : 0));
                break;
            default: break;
        }
        if (isFinal) {
            if (v.mood == h.mood) s += 4;
            if (v.mood == Mood::q && h.mood != Mood::q) s -= 5;  // a question ending needs a question
            if (v.eomi.empty()) s += 3;
            if (b.level == Level::hao || b.level == Level::hage) s -= 8;
            if (b.level == Level::hae) s += 1;
        }
        if (b.hon) s -= 6;  // -시- only when no row without it gives the word (주세요 is the imperative, not decl + 시)
        if (v.ctype == CType::stem) s -= 10;
        return s;
    };
    std::stable_sort(bs.begin(), bs.end(), [&](const Bundle& a, const Bundle& b) { return score(a) > score(b); });
}

std::vector<Bundle> AnalyzeWord(const Tables& t, const std::string& lemma, const std::string& pos, const std::string& cls,
                                const std::string& surface, const AnalyzeHints& h) {
    std::vector<Bundle> out;
    for (auto& [s, b] : Paradigm(t, lemma, pos, cls, h.host))
        if (s == surface) out.push_back(b);
    RankBundles(out, h);
    return out;
}

}  // namespace hd8gen
