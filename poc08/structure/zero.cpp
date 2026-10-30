// The class of an omitted subject: speaker, addressee, third, generic or none. A linear model over named
// features (data/zero.tsv, fitted on labelled clauses) decides; the features are clause facts, the person
// words of overt subjects nearby (data/persons.tsv) and the speech context.
#include <algorithm>
#include <cstdlib>
#include <string>

#include "structure.h"

namespace hd8 {
namespace {

const char* const kZeroNames[] = {"none", "overt", "speaker", "addressee", "third", "generic"};

bool IsPersonLike(const StructureData& d, const Strings& st, const std::vector<TokInfo>& info, const Path& path,
                  const Arg& a, std::string& cls) {
    if (a.bun == 0xFFFF || a.how == static_cast<uint8_t>(ArgHow::omitted)) return false;
    const Bunsetsu& b = path.buns[a.bun];
    auto it = d.persons.find(st.Get(path.toks[b.content].lemma));
    if (it != d.persons.end()) {
        cls = it->second;
        return true;
    }
    Wc w = info[b.content].wc;
    if (w == Wc::person || w == Wc::propn) {
        cls = "third";
        return true;
    }
    return false;
}

void Add(std::vector<std::string>& out, std::string f) { out.push_back(std::move(f)); }

}  // namespace

std::string LineSubjectClass(const StructureData& d, const Strings& st, const std::vector<TokInfo>& info,
                             const Path& path) {
    std::string last;
    for (const auto& c : path.clauses)
        for (const auto& a : c.args) {
            std::string cls;
            if ((a.role == Role::subj || a.role == Role::topic) && IsPersonLike(d, st, info, path, a, cls)) last = cls;
        }
    return last;
}

void ZeroFeatures(const StructureData& d, const Strings& st, const std::vector<TokInfo>& info, const Path& path,
                  int ci, const ZeroContext& ctx, std::vector<std::string>& out) {
    out.clear();
    const Clause& c = path.clauses[ci];
    const Bunsetsu& pb = path.buns[c.pred];
    const Bunsetsu& lb = path.buns[c.lastBun];
    // Inside a quote: a bracket opens before the predicate and is not closed before it.
    int depth = 0;
    for (int i = 0; i < pb.content; ++i) {
        if (info[i].wc == Wc::open) ++depth;
        if (info[i].wc == Wc::close && depth > 0) --depth;
    }
    const bool q = depth > 0 || ctx.dialogue;
    const std::string sp = q ? "Q" : "N";
    Add(out, "bias");
    Add(out, "sp=" + sp);
    Add(out, "sp=" + sp + "|docfp=" + (ctx.firstPersonNarration ? "1" : "0"));
    Add(out, std::string("force=") + Name(c.force) + "|sp=" + sp);
    Add(out, std::string("link=") + Name(c.link) + "|sp=" + sp);
    Add(out, std::string("tense=") + Name(c.tense) + "|sp=" + sp);
    if (c.polite != Polite::plain) Add(out, std::string("polite=") + Name(c.polite));
    if (c.bene != Bene::none) Add(out, std::string("bene=") + Name(c.bene) + "|force=" + Name(c.force));
    if (c.voice != Voice::active) Add(out, std::string("voice=") + Name(c.voice) + "|sp=" + sp);
    if (c.aspect != Aspect::none) Add(out, std::string("aspect=") + Name(c.aspect));
    if (c.pol == Pol::neg) Add(out, std::string("negfn=") + Name(c.negFn));
    for (const char* m : ModalityNames(c.modality)) {
        Add(out, std::string("mod=") + m + "|sp=" + sp);
        Add(out, std::string("mod=") + m + "|force=" + Name(c.force));
    }
    for (const char* j : JregNames(c.jreg)) Add(out, std::string("jreg=") + j);
    const std::string lemma = st.Get(path.toks[pb.content].lemma);
    Add(out, "lem=" + lemma);
    Add(out, "lem=" + lemma + "|sp=" + sp);
    Add(out, std::string("cwc=") + Name(info[pb.content].wc) + "|sp=" + sp);
    // The last morpheme of the clause (a final particle, an ending row) and the one before it.
    int last = lb.last;
    while (last > pb.content && (info[last].wc == Wc::period || info[last].wc == Wc::close || info[last].wc == Wc::comma ||
                                 info[last].wc == Wc::sym))
        --last;
    if (last > pb.content && info[last].row != nullptr) {
        Add(out, "end=" + st.Get(info[last].row->id) + "|sp=" + sp);
        if (info[last - 1].row != nullptr) Add(out, "end2=" + st.Get(info[last - 1].row->id));
    }
    // Overt person-like subjects: on the clause's parent chain, else earlier in the line, else the context.
    std::string par = "-", earlier = "-";
    for (int p = c.parent, guard = 0; p >= 0 && guard < 64; p = path.clauses[p].parent, ++guard) {
        for (const auto& a : path.clauses[p].args) {
            std::string cls;
            if ((a.role == Role::subj || a.role == Role::topic) && IsPersonLike(d, st, info, path, a, cls)) {
                par = cls;
                break;
            }
        }
        if (par != "-") break;
    }
    for (int k = 0; k < ci; ++k)
        for (const auto& a : path.clauses[k].args) {
            std::string cls;
            if ((a.role == Role::subj || a.role == Role::topic) && IsPersonLike(d, st, info, path, a, cls))
                earlier = cls;
        }
    Add(out, "par=" + par + "|sp=" + sp);
    Add(out, "earlier=" + earlier + "|sp=" + sp);
    Add(out, "prev=" + (ctx.previousSubject.empty() ? std::string("-") : ctx.previousSubject) + "|sp=" + sp);
    // Any argument at all (an object or a dative but no subject).
    bool obj = false, dat = false;
    for (const auto& a : c.args) {
        obj |= a.role == Role::obj;
        dat |= a.role == Role::dat;
    }
    Add(out, std::string("args=") + (obj ? "o" : "-") + (dat ? "d" : "-") + "|sp=" + sp);
}

ZeroResult ClassifyZero(const StructureData& d, const std::vector<std::string>& feats) {
    ZeroResult r;
    float score[6] = {0, 0, 0, 0, 0, 0};
    for (const auto& f : feats) {
        auto it = d.zeroWeights.find(f);
        if (it == d.zeroWeights.end()) continue;
        for (int k = 0; k < 6; ++k) score[k] += it->second[k];
    }
    int best = 2, second = -1;
    for (int k = 2; k < 6; ++k) {
        if (score[k] > score[best]) best = k;
    }
    for (int k = 0; k < 6; ++k) {
        if (k == best || k == 1) continue;
        if (second < 0 || score[k] > score[second]) second = k;
    }
    // class 0 (none) competes too
    if (score[0] > score[best]) {
        second = best;
        best = 0;
    }
    r.kind = static_cast<uint8_t>(best);
    r.score = score[best];
    r.margin = second >= 0 ? score[best] - score[second] : 0;
    r.runnerUp = static_cast<uint8_t>(second < 0 ? 0 : second);
    return r;
}

const char* ZeroName(uint8_t kind) { return kind < 6 ? kZeroNames[kind] : "none"; }

bool ParseZeroClass(std::string_view s, uint8_t& kind) {
    for (uint8_t k = 0; k < 6; ++k)
        if (s == kZeroNames[k]) {
            kind = k;
            return true;
        }
    return false;
}

}  // namespace hd8
