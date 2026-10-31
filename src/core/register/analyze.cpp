// Analysis by synthesis: the readings of a predicate word, found by generating the endings of eomi.tsv.
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>

#include "core/register/generate.h"
#include "core/register/hangul.h"

namespace hdTrans::reg {

namespace {

// The surface of one reading after host.
std::string Realize(const Tables& t, const Bundle& b, const std::string& host) {
    std::vector<Node> nodes;
    if (!host.empty()) {
        Node h;
        h.kind = Kind::Raw;
        h.lemma = host;
        h.id = 1;
        nodes.push_back(h);
    }
    Node v = b.node;
    v.id = 2;
    v.glue = false;
    nodes.push_back(v);
    Reg r;
    r.node = 2;
    r.level = b.level == Level::none ? Level::haera : b.level;
    r.hon = b.hon;
    UttLevel u;
    u.level = r.level;
    u.narration = true;  // no spoken fusions inside one word
    std::string out = Generate(t, nodes, {r}, u);
    if (!host.empty() && out.starts_with(host)) out = out.substr(host.size());
    if (!out.empty() && out[0] == ' ') out = out.substr(1);
    return out;
}

bool ParseMood(std::string_view s, Mood& m) {
    static constexpr std::string_view kNames[] = {"decl", "q", "imp", "prop", "excl"};
    for (int i = 0; i < 5; ++i)
        if (s == kNames[i]) {
            m = static_cast<Mood>(i);
            return true;
        }
    return false;
}

Bundle MakeBundle(const Tables& t, const ParadigmShape::Reading& c, const std::string& lemma, const std::string& pos,
                  const std::string& cls) {
    const EomiRow& row = t.eomi[c.row];
    Bundle b;
    Node& v = b.node;
    v.kind = Kind::V;
    v.lemma = lemma;
    v.vpos = pos;
    v.kcls = cls;
    v.tense = (c.pre & 1) != 0 ? Tense::past : Tense::nonpast;
    v.modal = (c.pre & 2) != 0 ? "gess" : "";
    v.mood = c.mood;
    b.hon = (c.pre & 4) != 0;
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
    } else {
        v.ctype = CType::stem;
    }
    return b;
}

// What every surface written with an ending form ends in, common to its alternatives (습니다/ㅂ니다 → 니다).
// The first syllable of a form joins the stem when it starts with ㅇ and ㅓ (어, 었), a consonant jamo or
// (으), and after the copula always; the rest is written as it is.
std::u32string FixedTail(const std::string& form, bool copula) {
    std::u32string tail;
    bool first = true;
    for (const std::string& alt : Split(form, '/')) {
        std::u32string u = hangul::ToU32(alt);
        bool joins = copula;
        if (u.starts_with(hangul::EuMark())) {
            u.erase(0, 3);
            joins = true;
        }
        if (!u.empty()) {
            const char32_t c = u[0];
            if (c == U'~' || hangul::IsConsonantJamo(c) ||
                (hangul::IsSyllable(c) && hangul::Initial(c) == hangul::kInitIeung && hangul::Medial(c) == hangul::EO))
                joins = true;
            if (joins) u.erase(0, 1);
        }
        if (first) {
            tail = u;
            first = false;
            continue;
        }
        std::size_t n = 0;
        while (n < tail.size() && n < u.size() && tail[tail.size() - 1 - n] == u[u.size() - 1 - n]) n++;
        tail = tail.substr(tail.size() - n);
    }
    return tail;
}

ParadigmShape MakeShape(const Tables& t, const std::string& pos, bool copula) {
    ParadigmShape shape;
    std::unordered_map<std::string, uint32_t> tailOfForm;
    for (std::size_t r = 0; r < t.eomi.size(); r++) {
        const EomiRow& row = t.eomi[r];
        if (row.slot == "hon" || row.slot == "tense" || row.slot == "modal" || row.words) continue;
        if (row.slot != "final" && row.slot != "conn" && row.slot != "adn" && row.slot != "nom" && row.slot != "quote" &&
            row.slot != "stem")
            continue;
        std::vector<Mood> moods;
        if (row.slot == "final") {
            Mood m = Mood::decl;
            if (row.mood == "-") moods = {Mood::decl, Mood::q};
            else if (ParseMood(row.mood, m)) moods = {m};
        } else {
            moods = {Mood::decl};
        }
        for (const Mood mood : moods)
            for (int pre = 0; pre < 8; ++pre) {
                const bool past = (pre & 1) != 0, gess = (pre & 2) != 0;
                if (row.slot == "stem" && (past || gess)) continue;
                if ((row.slot == "adn" || row.slot == "nom") &&
                    (gess || (past && row.family != "fut" && row.family != "m" && row.family != "gi")))
                    continue;
                ParadigmShape::Reading c;
                c.row = static_cast<uint32_t>(r);
                c.mood = mood;
                c.pre = static_cast<uint8_t>(pre);
                c.tail = ParadigmShape::kAnyTail;
                const Bundle b = MakeBundle(t, c, "", pos, "");
                // the -것 nominal is written with its noun after it
                if (b.node.ctype != CType::nom || b.node.rel != "geot") {
                    bool honIncl = false;
                    const Level level = b.level == Level::none ? Level::haera : b.level;
                    const std::string form = EndingForm(t, b.node, level, Persona::none, honIncl);
                    auto at = tailOfForm.find(form);
                    if (at == tailOfForm.end()) {
                        const std::u32string tail = FixedTail(form, copula);
                        const auto index = static_cast<uint32_t>(
                            std::find(shape.tails.begin(), shape.tails.end(), tail) - shape.tails.begin());
                        if (index == shape.tails.size()) shape.tails.push_back(tail);
                        at = tailOfForm.emplace(form, index).first;
                    }
                    c.tail = at->second;
                }
                shape.readings.push_back(c);
            }
    }
    return shape;
}

}  // namespace

const ParadigmShape& ShapeOf(const Tables& t, const std::string& pos, bool copula) {
    static std::mutex mu;
    static std::map<std::tuple<const Tables*, std::string, bool>, std::unique_ptr<const ParadigmShape>> shapes;
    std::lock_guard<std::mutex> lock(mu);
    auto& shape = shapes[{&t, pos, copula}];
    if (!shape) shape = std::make_unique<const ParadigmShape>(MakeShape(t, pos, copula));
    return *shape;
}

ParadigmIndex::ParadigmIndex(const Tables& t, const std::string& lemma, const std::string& posIn, const std::string& clsIn,
                             const std::string& host)
    : t_(t), lemma_(lemma), host_(host) {
    const PredicateInfo info = LookupPredicate(t, lemma);
    pos_ = posIn.empty() ? info.pos : posIn;
    cls_ = clsIn.empty() ? info.cls : clsIn;
    shape_ = &ShapeOf(t, pos_, cls_ == "COP");
}

Bundle ParadigmIndex::BundleOf(const ParadigmShape::Reading& r) const { return MakeBundle(t_, r, lemma_, pos_, cls_); }

const std::string& ParadigmIndex::SurfaceOf(std::size_t i) {
    auto it = surfaces_.find(i);
    if (it == surfaces_.end()) it = surfaces_.emplace(i, Realize(t_, BundleOf(shape_->readings[i]), host_)).first;
    return it->second;
}

std::vector<Bundle> ParadigmIndex::Find(const std::string& surface) {
    const std::u32string word = hangul::ToU32(surface);
    std::vector<bool> fits(shape_->tails.size());
    for (std::size_t k = 0; k < fits.size(); k++) fits[k] = word.ends_with(shape_->tails[k]);
    std::vector<Bundle> out;
    for (std::size_t i = 0; i < shape_->readings.size(); i++) {
        const ParadigmShape::Reading& r = shape_->readings[i];
        if (r.tail != ParadigmShape::kAnyTail && !fits[r.tail]) continue;
        if (SurfaceOf(i) == surface) out.push_back(BundleOf(r));
    }
    return out;
}

std::vector<std::pair<std::string, Bundle>> ParadigmIndex::All() {
    std::vector<std::pair<std::string, Bundle>> out;
    for (std::size_t i = 0; i < shape_->readings.size(); i++) out.emplace_back(SurfaceOf(i), BundleOf(shape_->readings[i]));
    return out;
}

void RankBundles(std::vector<Bundle>& bundles, const AnalyzeHints& h) {
    const auto score = [&](const Bundle& b) {
        const Node& v = b.node;
        int s = 0;
        const bool isFinal = v.ctype == CType::final_;
        switch (h.want) {
            case Want::final_: s += isFinal ? 20 : 0; break;
            case Want::conn: s += v.ctype == CType::conn ? 20 : (v.ctype == CType::quote ? 10 : 0); break;
            // a word inside the sentence that reads both as a final ending and as a quote (파기한다고 선언) is the quote
            case Want::adn:
                s += v.ctype == CType::adn ? 20 : (v.ctype == CType::conn ? 15 : (v.ctype == CType::quote ? 10 : 0));
                break;
            case Want::nom:
                s += v.ctype == CType::nom ? 20
                     : (v.ctype == CType::conn || v.ctype == CType::quote ? 10 : (v.ctype == CType::adn ? 5 : 0));
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
        if (b.hon) s -= 6;  // -시- only when no reading without it gives the word (주세요 is the imperative)
        if (v.ctype == CType::stem) s -= 10;
        return s;
    };
    std::stable_sort(bundles.begin(), bundles.end(), [&](const Bundle& a, const Bundle& b) { return score(a) > score(b); });
}

}  // namespace hdTrans::reg
