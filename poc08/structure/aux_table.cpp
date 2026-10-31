#include <algorithm>
#include <cstring>

#include "structure.h"

namespace hd8 {
namespace {

std::vector<std::string_view> Split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    size_t start = 0;
    for (;;) {
        size_t p = s.find(sep, start);
        if (p == std::string_view::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
}

std::vector<std::string> Alts(std::string_view v) {
    std::vector<std::string> out;
    if (v.empty() || v == "*" || v == "-") return out;
    for (auto a : Split(v, '|')) out.emplace_back(a);
    return out;
}

// Calls fn(fields) for every data row after the header; skips comments and blank lines.
template <class Fn>
void Rows(std::string_view text, Fn fn) {
    bool header = true;
    for (auto line : Split(text, '\n')) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        if (header) {
            header = false;
            continue;
        }
        fn(Split(line, '\t'));
    }
}

template <class E, size_t N>
int Lookup(std::string_view v, const char* const (&names)[N]) {
    for (size_t i = 0; i < N; ++i)
        if (v == names[i]) return static_cast<int>(i);
    return -1;
}

const char* const kLinkNames[] = {"final", "te", "tara", "to", "ba", "nara", "node", "kara", "noni", "tame", "you-ni",
                                  "nagara", "tari", "adn", "nom", "quote", "ga", "temo", "shi", "zu", "renyo", "time"};
const char* const kTenseNames[] = {"nonpast", "past"};
const char* const kLinkSubNames[] = {"", "uchi", "toki", "mae", "ato", "mama"};
const char* const kAspectNames[] = {"none", "te-iru", "te-aru", "te-shimau", "te-oku", "te-kuru", "te-iku", "te-miru"};
const char* const kPolNames[] = {"pos", "neg"};
const char* const kNegFnNames[] = {"none", "plain", "rhetorical", "prohibitive", "obligation", "potential-neg",
                                   "archaic-vol", "double"};
const char* const kVoiceNames[] = {"active", "rare", "passive", "potential", "honorific", "spontaneous", "causative",
                                   "caus-pass"};
const char* const kBeneNames[] = {"none", "give-out", "give-in", "receive"};
const char* const kForceNames[] = {"decl", "q", "imp", "prohib", "prop", "excl", "request"};
const char* const kPoliteNames[] = {"plain", "desu-masu", "sonkei", "kenjo", "gozaimasu"};
const char* const kCaseNames[] = {"none", "ga", "wo", "ni", "de", "to", "kara", "made", "yori", "he", "no", "wa", "mo",
                                  "nado", "quote"};
const char* const kRoleNames[] = {"subj", "obj", "dat", "loc", "from", "with", "to", "cmp", "topic", "head"};
const char* const kHowNames[] = {"marked", "topic", "relhead", "omitted"};
const char* const kModNames[] = {"explanatory", "conjecture", "likeness", "hearsay", "want", "must", "may",
                                 "volitional", "advice", "exclam", "try", "cannot"};
const char* const kJregNames[] = {"casual-final", "old", "ssu", "desuwa", "archaic", "rough"};
const char* const kKindNames[] = {"form", "voice", "tense", "link", "cop", "modal", "polite", "neg", "aspect", "jreg",
                                  "quote", "bene", "force", "aux", "light", "case", "topic", "focus", "final", "nom",
                                  "fn", "suffix", "prefix", "punct"};
const char* const kRNames[] = {"", "renyo", "final", "adn", "vol", "pot", "cond", "cop", "cop-te", "cop-ni", "cop-ja",
                               "daro", "neg", "neg-n", "zu", "rare", "caus", "sou", "you", "te", "wa", "mo", "eval",
                               "eval-neg", "eval-ok", "sumu", "irareru", "naru", "light", "ni", "de", "to", "quote",
                               "shika", "ka", "nomi", "q", "fin-no", "na-prohib", "jan", "mono", "wake", "hazu",
                               "tokoro", "hou", "say", "compound", "hon-prefix", "comma", "end", "close", "open",
                               "adj-ku", "kaneru", "absorb-neg", "excl"};
const char* const kWcNames[] = {"", "noun", "noun-adv", "propn", "person", "num", "na-aux", "pron", "verb", "verb-dep",
                                "adj", "adj-dep", "na", "adv", "adn", "conj", "intj", "filler", "aux", "p-case",
                                "p-topic", "p-adv", "p-conj", "p-final", "p-nom", "pref", "suf-n", "suf-count",
                                "suf-v", "suf-adj", "suf-na", "comma", "period", "open", "close", "sym", "space"};
const char* const kFormNames[] = {"", "mizen", "renyo", "renyo-onbin", "renyo-fused", "renyo-ni", "shushi", "rentai",
                                  "katei", "katei-fused", "meirei", "ishi", "gokan", "izen"};

bool ParseSets(std::string_view text, AuxSets& s, std::string& err) {
    if (text.empty() || text == "-") return true;
    for (auto item : Split(text, ';')) {
        if (item.empty()) continue;
        bool add = false;
        size_t eq = item.find("+=");
        if (eq != std::string_view::npos) {
            add = true;
        } else {
            eq = item.find('=');
        }
        if (eq == std::string_view::npos) {
            err = "bad set item: " + std::string(item);
            return false;
        }
        std::string_view key = item.substr(0, eq), val = item.substr(eq + (add ? 2 : 1));
        int v = -1;
        if (add && key == "mod") {
            v = Lookup<int>(val, kModNames);
            if (v >= 0) s.mod |= 1u << v;
        } else if (add && key == "jreg") {
            v = Lookup<int>(val, kJregNames);
            if (v >= 0) s.jreg = static_cast<uint16_t>(s.jreg | (1u << v));
        } else if (key == "tense") {
            s.tense = static_cast<int8_t>(v = Lookup<int>(val, kTenseNames));
        } else if (key == "aspect") {
            s.aspect = static_cast<int8_t>(v = Lookup<int>(val, kAspectNames));
        } else if (key == "pol") {
            s.pol = static_cast<int8_t>(v = Lookup<int>(val, kPolNames));
        } else if (key == "voice") {
            s.voice = static_cast<int8_t>(v = Lookup<int>(val, kVoiceNames));
        } else if (key == "bene") {
            s.bene = static_cast<int8_t>(v = Lookup<int>(val, kBeneNames));
        } else if (key == "force") {
            s.force = static_cast<int8_t>(v = Lookup<int>(val, kForceNames));
        } else if (key == "polite") {
            s.polite = static_cast<int8_t>(v = Lookup<int>(val, kPoliteNames));
        } else if (key == "link") {
            size_t colon = val.find(':');
            if (colon != std::string_view::npos) {
                s.linkSub = static_cast<int8_t>(Lookup<int>(val.substr(colon + 1), kLinkSubNames));
                if (s.linkSub <= 0) {
                    err = "unknown link subtype: " + std::string(item);
                    return false;
                }
                val = val.substr(0, colon);
            }
            s.link = static_cast<int8_t>(v = Lookup<int>(val, kLinkNames));
        } else if (key == "kase") {
            s.kase = static_cast<int8_t>(v = Lookup<int>(val, kCaseNames));
        } else if (key == "role") {
            v = Lookup<int>(val, kRNames);
            if (v > 0) s.role = static_cast<R>(v);
        }
        if (v < 0) {
            err = "unknown value: " + std::string(item);
            return false;
        }
    }
    return true;
}

bool StartsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool EndsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

// The lemma without its last UTF-8 code point.
std::string_view Stem(std::string_view lemma) {
    if (lemma.empty()) return lemma;
    size_t i = lemma.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(lemma[i]) & 0xC0) == 0x80) --i;
    return lemma.substr(0, i);
}

bool LemmaOk(const AuxRow& r, std::string_view lemma) {
    if (r.lemmas.empty()) return true;
    for (const auto& x : r.lemmas) {
        if (!x.empty() && x[0] == '~') {
            if (EndsWith(lemma, std::string_view(x).substr(1))) return true;
        } else if (x == lemma) {
            return true;
        }
    }
    return false;
}

bool SurfOk(const AuxRow& r, std::string_view surf, std::string_view lemma) {
    if (r.surfs.empty()) return true;
    static constexpr std::string_view kStem = "@stem+";
    for (const auto& x : r.surfs) {
        if (StartsWith(x, kStem)) {
            std::string_view stem = Stem(lemma);
            std::string want = std::string(stem) + x.substr(kStem.size());
            if (!stem.empty() && StartsWith(surf, want)) return true;
        } else if (x == surf) {
            return true;
        }
    }
    return false;
}

bool AnyPrefix(const std::vector<std::string>& ps, std::string_view s) {
    if (ps.empty()) return true;
    for (const auto& p : ps)
        if (StartsWith(s, p)) return true;
    return false;
}

struct TokStr {
    std::string_view surf, lemma, ud, cform, ctype;
};

TokStr Str(const Strings& st, const Token& t) {
    return {st.Get(t.surf), st.Get(t.lemma), st.Get(t.ud), st.Get(t.cform), st.Get(t.ctype)};
}

bool AfterOk(const AuxRow& r, const TokStr* prev, R prevRole, Wc prevWc) {
    if (r.afters.empty()) return true;
    if (prev == nullptr) return false;
    for (const auto& [k, v] : r.afters) {
        if (k == "prev" && prev->lemma == v) return true;
        if (k == "prevsurf" && prev->surf == v) return true;
        if (k == "prevud" && StartsWith(prev->ud, v)) return true;
        if (k == "prevform" && StartsWith(prev->cform, v)) return true;
        if (k == "prevverb" && (prevWc == Wc::verb || prevWc == Wc::verb_dep) && StartsWith(prev->cform, v)) return true;
        if (k == "prevrole" && v == kRNames[static_cast<int>(prevRole)]) return true;
    }
    return false;
}

const AuxRow* Best(const StructureData& d, const TokStr& t, const TokStr* prev, R prevRole, Wc prevWc, bool formRows) {
    const AuxRow* best = nullptr;
    for (const auto& r : d.aux) {
        if ((r.kind == AuxKind::form) != formRows) continue;
        if (!LemmaOk(r, t.lemma) || !SurfOk(r, t.surf, t.lemma)) continue;
        if (!AnyPrefix(r.uds, t.ud) || !AnyPrefix(r.forms, t.cform) || !AnyPrefix(r.ctypes, t.ctype)) continue;
        if (!AfterOk(r, prev, prevRole, prevWc)) continue;
        if (best == nullptr || r.spec > best->spec) best = &r;
    }
    return best;
}

template <class E>
E LongestPrefix(const std::vector<std::pair<std::string, E>>& table, std::string_view s, E none) {
    size_t bestLen = 0;
    E best = none;
    for (const auto& [p, v] : table)
        if (p.size() > bestLen && StartsWith(s, p)) {
            bestLen = p.size();
            best = v;
        }
    return best;
}

}  // namespace

bool LoadStructureData(std::string_view auxTsv, std::string_view udTsv, std::string_view formsTsv,
                       std::string_view headTsv, Strings& strings, StructureData& out, std::string& err) {
    bool ok = true;
    Rows(auxTsv, [&](const std::vector<std::string_view>& f) {
        if (!ok) return;
        if (f.size() < 9) {
            err = "aux row has fewer than 9 fields: " + std::string(f.empty() ? "" : f[0]);
            ok = false;
            return;
        }
        AuxRow r;
        r.id = strings.Intern("aux:" + std::string(f[0]));
        r.lemmas = Alts(f[1]);
        r.surfs = Alts(f[2]);
        r.uds = Alts(f[3]);
        r.forms = Alts(f[4]);
        r.ctypes = Alts(f[5]);
        for (const auto& a : Alts(f[6])) {
            size_t eq = a.find('=');
            r.afters.emplace_back(a.substr(0, eq), eq == std::string::npos ? "" : a.substr(eq + 1));
        }
        int k = Lookup<int>(f[7], kKindNames);
        if (k < 0) {
            err = "unknown aux kind: " + std::string(f[7]);
            ok = false;
            return;
        }
        r.kind = static_cast<AuxKind>(k);
        r.setsText = std::string(f[8]);
        if (!ParseSets(f[8], r.sets, err)) {
            err = std::string(f[0]) + ": " + err;
            ok = false;
            return;
        }
        size_t udLen = 0;
        for (const auto& u : r.uds) udLen = std::max(udLen, u.size());
        r.spec = (r.afters.empty() ? 0 : 800) + (r.surfs.empty() ? 0 : 400) + (r.forms.empty() ? 0 : 200) +
                 (r.ctypes.empty() ? 0 : 200) + (r.lemmas.empty() ? 0 : 200) + static_cast<int>(udLen);
        out.aux.push_back(std::move(r));
    });
    if (!ok) return false;
    Rows(udTsv, [&](const std::vector<std::string_view>& f) {
        if (!ok || f.size() < 2) return;
        int v = Lookup<int>(f[1], kWcNames);
        if (v <= 0) {
            err = "unknown word class: " + std::string(f[1]);
            ok = false;
            return;
        }
        out.ud.emplace_back(std::string(f[0]), static_cast<Wc>(v));
    });
    Rows(formsTsv, [&](const std::vector<std::string_view>& f) {
        if (!ok || f.size() < 2) return;
        int v = Lookup<int>(f[1], kFormNames);
        if (v <= 0) {
            err = "unknown form class: " + std::string(f[1]);
            ok = false;
            return;
        }
        out.forms.emplace_back(std::string(f[0]), static_cast<Form>(v));
    });
    Rows(headTsv, [&](const std::vector<std::string_view>& f) {
        if (!ok || f.size() < 2) return;
        out.headWeights.emplace_back(std::string(f[0]), std::strtof(std::string(f[1]).c_str(), nullptr));
    });
    std::sort(out.headWeights.begin(), out.headWeights.end());
    return ok;
}

void LoadPersons(std::string_view tsv, StructureData& out) {
    Rows(tsv, [&](const std::vector<std::string_view>& f) {
        if (f.size() >= 2) out.persons[std::string(f[0])] = std::string(f[1]);
    });
}

void LoadZeroWeights(std::string_view tsv, StructureData& out) {
    Rows(tsv, [&](const std::vector<std::string_view>& f) {
        uint8_t k = 0;
        if (f.size() < 3 || !ParseZeroClass(f[1], k)) return;
        out.zeroWeights[std::string(f[0])][k] = std::strtof(std::string(f[2]).c_str(), nullptr);
    });
}

std::vector<TokInfo> Annotate(const StructureData& d, const Strings& st, const std::vector<Token>& toks) {
    std::vector<TokInfo> out(toks.size());
    R prevRole = R::none;
    for (size_t i = 0; i < toks.size(); ++i) {
        TokStr t = Str(st, toks[i]);
        TokStr p{};
        const TokStr* prev = nullptr;
        if (i > 0) {
            p = Str(st, toks[i - 1]);
            prev = &p;
        }
        TokInfo& ti = out[i];
        ti.wc = LongestPrefix(d.ud, t.ud, Wc::none);
        ti.form = LongestPrefix(d.forms, t.cform, Form::none);
        Wc prevWc = i > 0 ? out[i - 1].wc : Wc::none;
        ti.row = Best(d, t, prev, prevRole, prevWc, false);
        if (!t.cform.empty()) ti.frow = Best(d, t, prev, prevRole, prevWc, true);
        // A morpheme row passes its role on; an inflected word without one passes its form's role.
        if (ti.row != nullptr && ti.row->sets.role != R::none && ti.row->kind != AuxKind::voice)
            ti.role = ti.row->sets.role;
        else if (ti.frow != nullptr)
            ti.role = ti.frow->sets.role;
        else
            ti.role = R::none;
        prevRole = ti.role;
    }
    return out;
}

const char* Name(Wc v) { return kWcNames[static_cast<int>(v)]; }
const char* Name(Link v) { return kLinkNames[static_cast<int>(v)]; }
const char* Name(LinkSub v) { return kLinkSubNames[static_cast<int>(v)]; }
const char* Name(Tense v) { return kTenseNames[static_cast<int>(v)]; }
const char* Name(Aspect v) { return kAspectNames[static_cast<int>(v)]; }
const char* Name(Pol v) { return kPolNames[static_cast<int>(v)]; }
const char* Name(NegFn v) { return kNegFnNames[static_cast<int>(v)]; }
const char* Name(Voice v) { return kVoiceNames[static_cast<int>(v)]; }
const char* Name(Bene v) { return kBeneNames[static_cast<int>(v)]; }
const char* Name(Force v) { return kForceNames[static_cast<int>(v)]; }
const char* Name(Polite v) { return kPoliteNames[static_cast<int>(v)]; }
const char* Name(Case v) { return kCaseNames[static_cast<int>(v)]; }
const char* Name(Role v) { return kRoleNames[static_cast<int>(v)]; }
const char* Name(ArgHow v) { return kHowNames[static_cast<int>(v)]; }
std::vector<const char*> ModalityNames(uint32_t bits) {
    std::vector<const char*> out;
    for (size_t i = 0; i < std::size(kModNames); ++i)
        if (bits & (1u << i)) out.push_back(kModNames[i]);
    return out;
}
std::vector<const char*> JregNames(uint16_t bits) {
    std::vector<const char*> out;
    for (size_t i = 0; i < std::size(kJregNames); ++i)
        if (bits & (1u << i)) out.push_back(kJregNames[i]);
    return out;
}

}  // namespace hd8
