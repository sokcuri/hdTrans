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

// The cast-blind contract of evaluation: when HD08_CAST_OFF names a TSV file, name rows whose
// Japanese is a surface of its first column are dropped.
std::set<std::string, std::less<>> CastOff() {
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

}  // namespace

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
    const std::set<std::string, std::less<>> castOff = CastOff();
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
        const std::string_view pos = PlainPos(col(f, "pos")), target = col(f, "target");
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
        for (const std::string_view atom : Split(r.colloc || cond == "*" ? std::string_view() : cond, ' ')) {
            if (atom.empty()) continue;
            const std::size_t eq = atom.find('=');
            if (eq == std::string_view::npos || eq == 0 || eq + 1 == atom.size())
                return fail("cue atom " + userdict::internal::Quote(atom) + ", want slot=value");
            PackAtom a;
            a.slot = atom.substr(0, eq);
            constexpr std::string_view kSlots[] = {"subj", "obj",  "dat", "loc",  "with", "from",     "to",       "arg",
                                                   "head", "mod",  "pred", "next", "after", "form",   "register", "domain",
                                                   "main.vol", "voice", "final"};
            if (std::find(std::begin(kSlots), std::end(kSlots), a.slot) == std::end(kSlots))
                return fail("cue slot " + userdict::internal::Quote(a.slot) + " is not one of the cue DSL");
            for (const std::string_view v : Split(atom.substr(eq + 1), '|')) {
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
            // read for the format, not applied: no key
            r.target = target;
            r.global = Register(r);
            table.rows.push_back(std::move(r));
            return;
        }
        for (const std::string_view jp : Split(lemma, '|')) {
            if (r.slot == PackSlot::Name && castOff.contains(jp)) continue;
            std::string_view kr = word && !keep ? target : ModelTarget(pos);
            bool adjective = word && kr.ends_with("하다") && pos == "n";
            if (adjective) kr = kr.substr(0, kr.size() - std::string_view("하다").size());
            adjective = adjective && !kr.empty();
            std::vector<userdict::internal::Rec> recs;
            if (std::string err = userdict::internal::Compile(jp, kr, unconjugated ? "n" : pos, "", false, base, families, recs);
                !err.empty() || recs.empty())
                return fail(err.empty() ? "no record" : err);
            PackRow one = r;
            if (!lexeme.empty()) {
                std::vector<userdict::internal::Rec> own;
                if (std::string err = userdict::internal::Compile(jp, lexeme, pos, "", false, base, families, own);
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
    for (const PackRow& r : table.rows) {
        for (const PackAtom& a : r.atoms)
            for (const std::string& v : a.values)
                if (v.starts_with("@") && !declared(v))
                    errors.push_back({r.file, r.line, "the class " + v + " is not a class of the pack-classes files"});
    }
}

}  // namespace hdTrans::dict
