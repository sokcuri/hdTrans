#include "core/dict/pack.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

#include "core/dict/classes.h"
#include "core/output/families.h"
#include "core/text/utf8.h"
#include "core/userdict/compile.h"

namespace hdTrans::dict {

namespace {

using userdict::internal::Split;
using userdict::internal::TrimSpace;

// The lines of text with their 1-based numbers, without blank and "#" lines.
template <class Fn> void Lines(std::string_view text, Fn fn) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    int n = 0;
    for (std::string_view line : Split(text, '\n')) {
        n++;
        while (line.ends_with('\r')) line.remove_suffix(1);
        if (const std::string_view t = TrimSpace(line); t.empty() || t.starts_with("#")) continue;
        fn(n, line);
    }
}

// The model Korean of a row that only names its word (voice, polarity, tense): any
// target of the pos compiles the same key.
std::string_view ModelTarget(std::string_view pos) {
    if (pos == "adj") return "좋다";
    if (pos == "na") return "조용하다";
    if (pos == "v" || pos == "v1" || pos == "v5") return "하다";
    return "것";
}

bool ParseSlot(std::string_view s, PackSlot& slot) {
    for (const PackSlot v : {PackSlot::Sense, PackSlot::Name, PackSlot::Voice, PackSlot::Polarity, PackSlot::Tense}) {
        if (s == PackSlotName(v)) {
            slot = v;
            return true;
        }
    }
    return false;
}

// The plain-format POS of a pack pos (the lexicon writes vs, vk, pn).
std::string_view PlainPos(std::string_view pos) {
    if (pos == "vs" || pos == "vk" || pos == "vz") return "v";
    if (pos == "pn" || pos == "suf" || pos == "pref" || pos == "num") return "n";
    return pos;
}

std::mutex registryMutex;
struct RegistryData {
    std::vector<PackRowLabel> rows;
    std::map<std::string, int, std::less<>> ids;  // file, line, id and target: a reloaded row keeps its id
};
RegistryData& Registry() {
    static RegistryData data;
    return data;
}

int Register(const PackRow& r) {
    const std::string key = r.file + '\n' + std::to_string(r.line) + '\n' + r.id + '\n' + r.written;
    const std::lock_guard<std::mutex> lock(registryMutex);
    RegistryData& d = Registry();
    if (const auto it = d.ids.find(key); it != d.ids.end()) return it->second;
    d.rows.push_back({r.id, PackSlotName(r.slot), r.written});
    const int id = static_cast<int>(d.rows.size());
    d.ids.emplace(key, id);
    return id;
}

// The class digit of the words of a pack pos (part code byte 1), for the rows of lemma * and @class.
char PosDigit(std::string_view pos) {
    if (pos == "v") return '4';
    if (pos == "adj") return '7';
    if (pos == "na") return '8';
    if (pos == "p") return '2';
    if (pos == "adv") return '3';
    if (pos == "intj") return '0';
    if (pos == "n") return '1';
    return 0;
}

// The values a cue atom of these slots may take.
bool KnownValue(std::string_view slot, std::string_view v) {
    static constexpr std::string_view kForms[] = {"ta", "neg", "nja", "te", "te-request", "vol", "cop", "attr", "adv"};
    static constexpr std::string_view kVoices[] = {"none", "passive", "potential", "respectful", "spontaneous"};
    static constexpr std::string_view kRegisters[] = {"polite", "casual", "plain"};
    auto in = [&](const auto& set) { return std::find(std::begin(set), std::end(set), v) != std::end(set); };
    if (slot == "form") return in(kForms);
    if (slot == "voice") return in(kVoices);
    if (slot == "register") return in(kRegisters);
    return true;
}

// The 0.7.x notation of a Korean particle that follows a slot: the pair forms the composer
// picks by the slot's last syllable.
std::string ParticleNotation(std::string_view p) {
    static constexpr std::pair<std::string_view, std::string_view> kPairs[] = {
        {"을", "(을)를"}, {"를", "(을)를"}, {"이", "(이)가"}, {"가", "(이)가"},   {"은", "(은)는"}, {"는", "(은)는"},
        {"과", "(와)과"}, {"와", "(와)과"}, {"으로", "(으)로"}, {"로", "(으)로"}, {"이나", "(이)나"}, {"나", "(이)나"},
        {"이랑", "(이)랑"}, {"랑", "(이)랑"}};
    for (const auto& [plain, notation] : kPairs)
        if (p == plain) return std::string(notation);
    return std::string(p);
}

// Whether s is a Korean particle a slot may take in a colloc target.
bool SlotParticle(std::string_view s) {
    static constexpr std::string_view kParticles[] = {"을", "를", "이", "가", "은", "는", "과", "와", "으로", "로",
                                                      "이나", "나", "이랑", "랑", "에", "에게", "에게서", "에서",
                                                      "의", "도", "만", "부터", "까지", "보다", "처럼", "한테"};
    return std::find(std::begin(kParticles), std::end(kParticles), s) != std::end(kParticles);
}

std::string CompileColloc(std::string_view pattern, std::string_view target, std::string_view kclass, const Dict* base,
                          userdict::Families& families, Colloc& c) {
    const std::size_t open = pattern.find("{X");
    if (const std::size_t brace = pattern.find('{'); brace != std::string_view::npos && brace != open) {
        const std::size_t close = pattern.find('}', brace);
        return "a slot is {X} or {X:@class}, not " +
               userdict::internal::Quote(pattern.substr(brace, close == std::string_view::npos ? close : close - brace + 1));
    }
    c.slot = open != std::string_view::npos;
    if (!c.slot) {
        for (const std::string_view one : Split(pattern, '|'))
            if (!one.empty()) c.befores.emplace_back(one);
    } else {
        const std::size_t close = pattern.find('}', open);
        if (close == std::string_view::npos) return "the slot of " + userdict::internal::Quote(pattern) + " is not closed";
        const std::string_view inside = pattern.substr(open + 2, close - open - 2);
        if (!inside.empty() && !(inside.starts_with(":@") && inside.size() > 2))
            return "a slot is {X} or {X:@class}, not " + userdict::internal::Quote(pattern.substr(open, close - open + 1));
        if (!inside.empty()) c.slotClass = inside.substr(1);
        c.befores.emplace_back(pattern.substr(0, open));
        c.after = pattern.substr(close + 1);
        if (pattern.find('|') != std::string_view::npos) return "a pattern with a slot has one spelling";
        if (c.after.find("{X") != std::string::npos) return "a colloc pattern has one slot";
    }
    if (c.befores.empty() || (c.befores.size() == 1 && c.befores[0].empty() && c.after.empty())) return "an empty colloc pattern";
    if (c.slot && c.after.empty()) return "a colloc pattern ends with words after its slot, not with the slot";
    std::string_view rest = target;
    if (c.slot) {
        const std::size_t at = target.find("{X}");
        if (at == std::string_view::npos) return "the target " + userdict::internal::Quote(target) + " has no {X}";
        c.lead = TrimSpace(target.substr(0, at));
        if (!c.lead.empty() && c.befores[0].empty())
            return "the target " + userdict::internal::Quote(target) + " has Korean before {X} but the pattern starts with the slot";
        rest = target.substr(at + 3);
        c.spaced = rest.starts_with(' ');
        // a particle right after the slot's Korean goes with the word after the slot
        const std::size_t space = rest.find(' ');
        if (const std::string_view first = rest.substr(0, space); SlotParticle(first)) {
            c.particle = ParticleNotation(first);
            rest = space == std::string_view::npos ? std::string_view() : rest.substr(space + 1);
        }
    } else if (target.find("{X") != std::string_view::npos) {
        return "the target " + userdict::internal::Quote(target) + " has {X} but the pattern has no slot";
    }
    rest = TrimSpace(rest);
    if (rest.empty() && c.particle.empty()) return "the target " + userdict::internal::Quote(target) + " says nothing";
    c.headRaw = rest;
    if (rest.ends_with("다")) {
        // the phrase as a verb, an adjective and a な adjective; a class the phrase does not conjugate as stays empty
        const struct {
            std::string_view jp, pos;
            std::string* out;
        } models[] = {{"見る", "v1", &c.headVerb}, {"良い", "adj", &c.headAdjective}, {"静か", "na", &c.headNa}};
        for (const auto& m : models) {
            std::vector<userdict::internal::Rec> recs;
            const std::string err = userdict::internal::Compile(m.jp, rest, m.pos, kclass, false, base, families, recs);
            if (err.empty() && !recs.empty()) *m.out = recs[0].target;
            // a verb whose class the Korean does not show must be named (pos v5:규칙)
            if (m.out == &c.headVerb && err.ends_with("name the class")) return err + " (pos POS:class)";
        }
    }
    return {};
}

}  // namespace

std::set<std::string, std::less<>> CastOffNames() {
    std::set<std::string, std::less<>> out;
    const char* path = std::getenv("HD08_CAST_OFF");
    if (path == nullptr || *path == 0) return out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        const std::string_view first = TrimSpace(std::string_view(line).substr(0, line.find('\t')));
        if (!first.empty() && !first.starts_with("#") && first != "surface") out.emplace(first);
    }
    return out;
}

PackRowLabel PackRowById(int global) {
    const std::lock_guard<std::mutex> lock(registryMutex);
    const std::vector<PackRowLabel>& rows = Registry().rows;
    if (global < 1 || static_cast<std::size_t>(global) > rows.size()) return {};
    return rows[static_cast<std::size_t>(global) - 1];
}

const char* PackSlotName(PackSlot slot) {
    switch (slot) {
        case PackSlot::Sense: return "sense";
        case PackSlot::Name: return "name";
        case PackSlot::Voice: return "voice";
        case PackSlot::Polarity: return "polarity";
        case PackSlot::Tense: return "tense";
    }
    return "sense";
}

int PackTable::Depth(std::string_view cls) const {
    int depth = 0;
    std::string_view c = cls;
    for (int guard = 0; guard < 32 && !c.empty(); guard++) {
        const auto it = parent.find(c);
        if (it == parent.end()) return depth;
        depth++;
        c = it->second;
    }
    return depth;
}

bool PackTable::Member(std::string_view lemma, std::string_view cls) const {
    const auto it = classes.find(lemma);
    if (it == classes.end()) return false;
    for (const std::string& own : it->second) {
        std::string_view c = own;
        for (int guard = 0; guard < 32 && !c.empty(); guard++) {
            if (c == cls) return true;
            const auto p = parent.find(c);
            if (p == parent.end()) break;
            c = p->second;
        }
    }
    return false;
}

void ParsePackRows(std::string_view file, std::string_view text, const Dict* base, userdict::Families& families,
                   PackTable& table, std::vector<PackError>& errors) {
    std::vector<std::string> head;
    const std::set<std::string, std::less<>> castOff = CastOffNames();
    auto col = [&](const std::vector<std::string_view>& f, std::string_view name) -> std::string_view {
        for (std::size_t i = 0; i < head.size() && i < f.size(); i++)
            if (head[i] == name) return TrimSpace(f[i]);
        return {};
    };
    Lines(text, [&](int n, std::string_view line) {
        const std::vector<std::string_view> f = Split(line, '\t');
        if (head.empty()) {
            for (const std::string_view h : f) head.emplace_back(TrimSpace(h));
            for (const std::string_view need : {"id", "lemma", "pos", "cond", "target"})
                if (std::find(head.begin(), head.end(), need) == head.end())
                    errors.push_back({std::string(file), n, "the header has no column " + std::string(need)});
            return;
        }
        auto fail = [&](std::string message) { errors.push_back({std::string(file), n, std::move(message)}); };
        PackRow r;
        r.id = col(f, "id");
        r.file = file;
        r.spec = col(f, "spec");
        r.prov = col(f, "prov");
        r.line = n;
        r.written = col(f, "target");
        std::string_view lemma = col(f, "lemma");
        // pos:class names the Korean conjugation class of the target (v5:규칙), as the sense table does
        const std::string_view posColumn = col(f, "pos");
        const std::size_t colon = posColumn.find(':');
        const std::string_view kclass = colon == std::string_view::npos ? std::string_view() : posColumn.substr(colon + 1);
        const std::string_view pos = PlainPos(posColumn.substr(0, colon)), target = col(f, "target");
        // A lexicon id (ja/yomi/pos) names its written form.
        if (const std::size_t slash = lemma.find('/'); slash != std::string_view::npos) lemma = lemma.substr(0, slash);
        if (r.id.empty() || lemma.empty() || pos.empty() || target.empty()) return fail("empty id, lemma, pos or target");
        if (const auto [at, fresh] = table.ids.emplace(r.id, std::string(file) + ":" + std::to_string(n)); !fresh)
            return fail("row id " + userdict::internal::Quote(r.id) + " is also at " + at->second);
        if (const std::string_view slot = col(f, "slot"); !slot.empty() && !ParseSlot(slot, r.slot))
            return fail("slot " + userdict::internal::Quote(slot) + ", want sense, name, voice, polarity or tense");
        if (const std::string_view kind = col(f, "kind"); !kind.empty() && kind != "frame" && kind != "colloc")
            return fail("kind " + userdict::internal::Quote(kind) + ", want frame or colloc");
        r.colloc = col(f, "kind") == "colloc";
        if (pos != "n" && pos != "v" && pos != "v1" && pos != "v5" && pos != "adj" && pos != "na" && pos != "p" &&
            pos != "adv" && pos != "intj")
            return fail("pos " + userdict::internal::Quote(pos) +
                        ", want n, pn, suf, pref, num, v, vs, vk, v1, v5, adj, na, adv, intj or p");
        if (const std::string_view pri = col(f, "pri"); !pri.empty()) {
            for (const char c : pri)
                if ((c < '0' || c > '9') && c != '-') return fail("pri " + userdict::internal::Quote(pri) + " is not a number");
            r.pri = std::atoi(std::string(pri).c_str());
        }
        const std::string_view cond = col(f, "cond");
        // A colloc row's cue atoms (column cue) are read on the last word of its span.
        const std::string_view cue = r.colloc ? col(f, "cue") : cond;
        for (const std::string_view atom : Split(cue == "*" ? std::string_view() : cue, ' ')) {
            if (atom.empty()) continue;
            const std::size_t eq = atom.find('=');
            if (eq == std::string_view::npos || eq == 0 || eq + 1 == atom.size())
                return fail("cue atom " + userdict::internal::Quote(atom) + ", want slot=value");
            PackAtom a;
            a.slot = atom.substr(0, eq);
            constexpr std::string_view kSlots[] = {"subj", "obj",  "dat", "loc",  "with", "from",     "to",       "arg",
                                                   "head", "mod",  "pred", "next", "after", "form",   "register", "domain",
                                                   "main.vol", "voice", "final"};
            // with an attachment also: how, <argument slot>.how, cl.<feature>, main.<feature>
            static constexpr std::string_view kArgs[] = {"subj", "obj", "dat", "loc", "with", "from", "to", "arg"};
            const std::size_t dot = a.slot.find('.');
            const std::string_view first = std::string_view(a.slot).substr(0, dot);
            const std::string_view tail = dot == std::string::npos ? std::string_view() : std::string_view(a.slot).substr(dot + 1);
            const bool attached = a.slot == "how" || ((first == "cl" || first == "main") && !tail.empty()) ||
                                  (tail == "how" && std::find(std::begin(kArgs), std::end(kArgs), first) != std::end(kArgs));
            if (!attached && std::find(std::begin(kSlots), std::end(kSlots), a.slot) == std::end(kSlots))
                return fail("cue slot " + userdict::internal::Quote(a.slot) + " is not one of the cue DSL");
            for (const std::string_view v : Split(atom.substr(eq + 1), '|')) {
                if (attached && (v == "-" || v == "*"))
                    return fail("cue value " + userdict::internal::Quote(v) + " of " + a.slot + ": the attachment atoms take values");
                if (v.empty() || (v != "-" && v != "*" && !KnownValue(a.slot, v)))
                    return fail("cue value " + userdict::internal::Quote(v) + " of " + a.slot + " is not one the host reads");
                a.values.emplace_back(v);
            }
            if (a.slot == "voice" && (r.slot == PackSlot::Sense || r.slot == PackSlot::Name))
                return fail("a voice= atom is read on voice, polarity and tense rows only");
            r.atoms.push_back(std::move(a));
        }
        const bool word = r.slot == PackSlot::Sense || r.slot == PackSlot::Name;
        // A voice target is a reading, with the Korean verb that says it after a colon (passive:혼나다).
        const std::string_view voice = r.slot == PackSlot::Voice ? target.substr(0, target.find(':')) : target;
        const std::string_view lexeme =
            r.slot == PackSlot::Voice && target.find(':') != std::string_view::npos ? target.substr(target.find(':') + 1)
                                                                                    : std::string_view();
        if (r.slot == PackSlot::Voice && voice != "passive" && voice != "potential" && voice != "respectful" &&
            voice != "spontaneous" && voice != "active")
            return fail("voice " + userdict::internal::Quote(voice) +
                        ", want passive, potential, respectful, spontaneous or active");
        if (r.slot == PackSlot::Voice && target.find(':') != std::string_view::npos &&
            (lexeme.empty() || (pos != "v" && pos != "v1" && pos != "v5")))
            return fail("a voice verb (" + userdict::internal::Quote(target) + ") needs a verb row and a Korean verb");
        // agent: the particle a voice row writes on its verb's agent phrase (先生に叱られた 선생님한테; a leading (이)
        // follows the final consonant, (이)가)
        r.agent = col(f, "agent");
        if (!r.agent.empty() && r.slot != PackSlot::Voice) return fail("agent is read on voice rows only");
        for (const char32_t ch : userdict::internal::Runes(r.agent.starts_with("(이)") ? r.agent.substr(5) : r.agent))
            if ((ch < 0xAC00 || ch > 0xD7A3) && ch != U' ')
                return fail("agent " + userdict::internal::Quote(r.agent) + " is not a Korean particle in Hangul");
        // A target - keeps the slot as the chain decided it: the row wins over less specific rows and writes nothing.
        const bool keep = target == "-";
        if ((r.slot == PackSlot::Polarity || r.slot == PackSlot::Tense) && !keep &&
            output::kFormCodes.find(target.front()) == std::string_view::npos)
            return fail("ending " + userdict::internal::Quote(target) + " does not start with a form code (0-9, A, B)");
        if (!word || keep) r.target = r.slot == PackSlot::Voice ? voice : target;
        const bool unconjugated = pos == "p" || pos == "adv";
        // lemma * is every word of the pos, lemma @class every word of that pack class (CheckPackClasses).
        if (lemma == "*" || lemma.starts_with("@")) {
            if (word && !keep) return fail("a sense or name row names its lemma (lemma * and @class write endings, voices or -)");
            if (r.slot == PackSlot::Voice && !lexeme.empty()) return fail("a voice verb needs a lemma");
            if (lemma.starts_with("@")) r.lemmaClass = lemma;
            r.classDigit = PosDigit(pos);
            if (r.classDigit == 0)
                return fail("pos " + userdict::internal::Quote(pos) + " of lemma * or @class, want v (not v1, v5), adj, na, n, p, adv or intj");
            r.global = Register(r);
            table.open[OpenIndex(r.slot, r.classDigit)].push_back(static_cast<int>(table.rows.size()));
            table.rows.push_back(std::move(r));
            return;
        }
        if (r.colloc) {
            Colloc c;
            if (std::string err = CompileColloc(cond, target, kclass, base, families, c); !err.empty()) return fail(err);
            c.row = static_cast<int>(table.rows.size());
            r.target = target;
            r.global = Register(r);
            table.rows.push_back(std::move(r));
            for (std::size_t b = 0; b < c.befores.size(); b++) {
                const std::string_view before = c.befores[b];
                const int pattern = static_cast<int>(table.collocPatterns.size());
                table.collocPatterns.emplace_back(static_cast<int>(table.collocs.size()), static_cast<int>(b));
                if (before.empty()) table.collocsSlotFirst.push_back(pattern);
                else table.collocsByLead[std::string(before.substr(0, hdTrans::text::DecodeRune(before).size))].push_back(pattern);
            }
            table.collocs.push_back(std::move(c));
            return;
        }
        for (const std::string_view jp : Split(lemma, '|')) {
            if (r.slot == PackSlot::Name && castOff.contains(jp)) continue;
            std::string_view kr = word && !keep ? target : ModelTarget(pos);
            bool adjective = word && kr.ends_with("하다") && pos == "n";
            if (adjective) kr = kr.substr(0, kr.size() - std::string_view("하다").size());
            adjective = adjective && !kr.empty();
            std::vector<userdict::internal::Rec> recs;
            if (std::string err = userdict::internal::Compile(jp, kr, unconjugated ? "n" : pos, word ? kclass : std::string_view(), false, base, families, recs);
                !err.empty() || recs.empty())
                return fail(err.empty() ? "no record" : err);
            PackRow one = r;
            if (!lexeme.empty()) {
                std::vector<userdict::internal::Rec> own;
                if (std::string err = userdict::internal::Compile(jp, lexeme, pos, kclass, false, base, families, own);
                    !err.empty() || own.empty())
                    return fail(err.empty() ? "no record for the voice verb" : err);
                one.lexeme = own[0].target;
            }
            one.key = recs[0].key;
            one.classDigit = pos == "p" ? '2' : pos == "adv" ? '3' : recs[0].part.size() > 1 ? recs[0].part[1] : 0;
            if (word && !keep) one.target = recs[0].target;
            one.adjective = adjective;
            one.global = Register(one);
            table.byKey[one.key].push_back(static_cast<int>(table.rows.size()));
            table.rows.push_back(std::move(one));
        }
    });
}

void ParsePackClasses(std::string_view file, std::string_view text, PackTable& table, std::vector<PackError>& errors) {
    // The header names the kind of the file: class parent (the classes, as the lexicon's
    // classes.tsv), or lemma classes (the classes of words, joined by |).
    enum { None, Classes, Members, Bad } kind = None;
    Lines(text, [&](int n, std::string_view line) {
        const std::vector<std::string_view> f = Split(line, '\t');
        const std::string_view a = TrimSpace(f[0]), b = f.size() > 1 ? TrimSpace(f[1]) : std::string_view();
        if (kind == None) {
            kind = a == "class" && b == "parent" ? Classes : a == "lemma" && b == "classes" ? Members : Bad;
            if (kind == Bad) errors.push_back({std::string(file), n, "the header is neither class, parent nor lemma, classes"});
            return;
        }
        if (kind == Bad) return;
        if (a.empty() || (kind == Classes && !a.starts_with("@"))) {
            errors.push_back({std::string(file), n, kind == Classes ? "a class starts with @" : "an empty lemma"});
            return;
        }
        if (kind == Classes) {
            table.parent[std::string(a)] = std::string(b);
            return;
        }
        std::vector<std::string>& own = table.classes[std::string(a)];
        for (const std::string_view c : Split(b, '|'))
            if (!TrimSpace(c).empty()) own.emplace_back(TrimSpace(c));
    });
}

void CheckPackClasses(PackTable& table, std::vector<PackError>& errors) {
    auto declared = [&](std::string_view c) { return table.parent.find(c) != table.parent.end(); };
    for (const auto& [cls, parent] : table.parent)
        if (!parent.empty() && !declared(parent))
            errors.push_back({"pack-classes", 0, "the parent " + parent + " of " + cls + " is not a class"});
    for (const auto& [lemma, own] : table.classes)
        for (const std::string& c : own)
            if (!declared(c)) errors.push_back({"pack-classes", 0, "the class " + c + " of " + lemma + " is not a class"});
    // The keys of the members of a lemma class: a pack class, else a set of classes.tsv (its name without @).
    const auto addKeys = [](PackRow& r, std::string_view lemma) {
        std::vector<std::string_view> keys{lemma};
        if (lemma.ends_with("する")) keys.push_back(lemma.substr(0, lemma.size() - std::string_view("する").size()));
        // without its last kana: the stem of a verb (気づ|く) or of an adjective (高|い)
        std::size_t lastStart = 0;
        for (std::size_t at = 0; at < lemma.size();) {
            const std::size_t size = text::DecodeRune(lemma.substr(at)).size;
            if (size == 0) break;
            lastStart = at;
            at += size;
        }
        if (lastStart > 0) keys.push_back(lemma.substr(0, lastStart));
        for (const std::string_view k : keys)
            if (!k.empty()) r.lemmaKeys[std::string(k)].emplace_back(lemma);
    };
    for (PackRow& r : table.rows) {
        if (r.lemmaClass.empty()) continue;
        if (declared(r.lemmaClass)) {
            for (const auto& [lemma, own] : table.classes)
                if (table.Member(lemma, r.lemmaClass)) addKeys(r, lemma);
            continue;
        }
        const ClassId builtin = ClassId::Of(std::string_view(r.lemmaClass).substr(1));
        if (builtin.data == nullptr) {
            errors.push_back({r.file, r.line, "the lemma class " + r.lemmaClass +
                                                  " is not a class of the pack-classes files nor of classes.tsv"});
            continue;
        }
        for (const std::string_view lemma : ClassMembers(builtin)) addKeys(r, lemma);
    }
    for (const Colloc& c : table.collocs)
        if (!c.slotClass.empty() && !declared(c.slotClass))
            errors.push_back({table.rows[static_cast<std::size_t>(c.row)].file, table.rows[static_cast<std::size_t>(c.row)].line,
                              "the class " + c.slotClass + " is not a class of the pack-classes files"});
    for (const PackRow& r : table.rows) {
        for (const PackAtom& a : r.atoms)
            for (const std::string& v : a.values)
                if (v.starts_with("@") && !declared(v))
                    errors.push_back({r.file, r.line, "the class " + v + " is not a class of the pack-classes files"});
    }
}

}  // namespace hdTrans::dict
