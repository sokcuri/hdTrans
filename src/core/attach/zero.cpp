// The class of an omitted subject: speaker, addressee, third, generic or none, by the constraint rules of
// data/zero.tsv over clause facts (honorifics, giving and receiving, mood) and the person words of the clause's
// topic (data/persons.tsv).
#include <algorithm>
#include <cstdlib>
#include <string>

#include "core/attach/structure.h"

namespace hdTrans::attach {
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

}  // namespace

const char* ZeroName(uint8_t kind) { return kind < 6 ? kZeroNames[kind] : "none"; }

bool ParseZeroClass(std::string_view s, uint8_t& kind) {
    for (uint8_t k = 0; k < 6; ++k)
        if (s == kZeroNames[k]) {
            kind = k;
            return true;
        }
    return false;
}

bool LoadZeroRules(std::string_view tsv, StructureData& out, std::string& err) {
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
        for (size_t q = 0;;) {
            size_t t = line.find('\t', q);
            f.push_back(line.substr(q, t == std::string::npos ? std::string::npos : t - q));
            if (t == std::string::npos) break;
            q = t + 1;
        }
        if (f.size() < 4) {
            err = "zero.tsv: short row " + line;
            return false;
        }
        StructureData::ZeroRule r;
        r.id = f[0];
        r.cls = f[3];
        for (size_t a = 0; f[2] != "*" && a <= f[2].size();) {
            size_t amp = f[2].find('&', a);
            std::string t = f[2].substr(a, amp == std::string::npos ? std::string::npos : amp - a);
            size_t lo = t.find_first_not_of(' '), hi = t.find_last_not_of(' ');
            t = lo == std::string::npos ? std::string() : t.substr(lo, hi - lo + 1);
            StructureData::HeadAtom at;
            if (!t.empty() && t[0] == '!') {
                at.neg = true;
                t = t.substr(1);
            }
            size_t eq = t.find('=');
            at.key = t.substr(0, eq);
            if (eq != std::string::npos)
                for (size_t b = eq + 1;;) {
                    size_t bar = t.find('|', b);
                    at.vals.push_back(t.substr(b, bar == std::string::npos ? std::string::npos : bar - b));
                    if (bar == std::string::npos) break;
                    b = bar + 1;
                }
            static const char* const kKeys[] = {"force", "mod", "polite", "bene", "voice", "sp", "lemma", "pol", "tense",
                                                "link", "aspect", "obj", "dat", "person", "jreg"};
            if (std::find(std::begin(kKeys), std::end(kKeys), at.key) == std::end(kKeys)) {
                err = "zero.tsv: unknown key " + at.key + " in " + r.id;
                return false;
            }
            r.when.push_back(std::move(at));
            if (amp == std::string::npos) break;
            a = amp + 1;
        }
        out.zeroRules.push_back(std::move(r));
    }
    return true;
}

std::string ZeroByRules(const StructureData& d, const Strings& st, const std::vector<TokInfo>& info, const Path& path,
                        int ci, bool dialogue, std::string& rule) {
    rule.clear();
    const Clause& c = path.clauses[ci];
    std::string person = "-";
    bool obj = false, dat = false;
    for (const auto& a : c.args) {
        std::string cls;
        if ((a.role == Role::subj || (a.role == Role::topic && a.how != static_cast<uint8_t>(ArgHow::relhead))) &&
            a.how != static_cast<uint8_t>(ArgHow::omitted) && a.bun != 0xFFFF) {
            if (a.role == Role::subj) {
                rule = "zero:overt";
                return "overt";
            }
            if (IsPersonLike(d, st, info, path, a, cls)) person = cls;
        }
        obj |= a.role == Role::obj;
        dat |= a.role == Role::dat;
    }
    // inside a quote: a bracket opens before the predicate and is not closed before it
    const Bunsetsu& pb = path.buns[c.pred];
    int depth = 0;
    for (int i = 0; i < pb.content; ++i) {
        if (info[i].wc == Wc::open) ++depth;
        if (info[i].wc == Wc::close && depth > 0) --depth;
    }
    const std::string sp = depth > 0 || dialogue ? "Q" : "N";
    const std::vector<const char*> mods = ModalityNames(c.modality);
    const std::vector<const char*> jregs = JregNames(c.jreg);
    const std::string lemma = st.Get(path.toks[pb.content].lemma);
    auto holds = [&](const StructureData::HeadAtom& a) {
        bool r = false;
        auto in = [&](const std::string& v) { return std::find(a.vals.begin(), a.vals.end(), v) != a.vals.end(); };
        if (a.key == "force") r = in(Name(c.force));
        else if (a.key == "polite") r = in(Name(c.polite));
        else if (a.key == "bene") r = in(Name(c.bene));
        else if (a.key == "voice") r = in(Name(c.voice));
        else if (a.key == "pol") r = in(Name(c.pol));
        else if (a.key == "tense") r = in(Name(c.tense));
        else if (a.key == "link") r = in(Name(c.link));
        else if (a.key == "aspect") r = in(Name(c.aspect));
        else if (a.key == "sp") r = in(sp);
        else if (a.key == "lemma") r = in(lemma);
        else if (a.key == "person") r = in(person);
        else if (a.key == "obj") r = obj;
        else if (a.key == "dat") r = dat;
        else if (a.key == "mod") {
            for (const char* m : mods) r = r || in(m);
        } else if (a.key == "jreg") {
            for (const char* j : jregs) r = r || in(j);
        }
        return a.neg ? !r : r;
    };
    const StructureData::ZeroRule* best = nullptr;
    for (const auto& z : d.zeroRules) {
        bool all = true;
        for (const auto& a : z.when) all = all && holds(a);
        if (all && (best == nullptr || z.when.size() > best->when.size())) best = &z;
    }
    if (best == nullptr) return "";
    rule = "zero:" + best->id;
    return best->cls;
}

}  // namespace hdTrans::attach
