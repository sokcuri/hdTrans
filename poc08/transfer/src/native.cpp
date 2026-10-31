#include "native.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>

#include "ir/file.h"

namespace hd08::nat {

namespace {

std::vector<std::string> Split(std::string_view s, char c) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= s.size()) {
        size_t e = s.find(c, at);
        if (e == std::string_view::npos) e = s.size();
        out.emplace_back(s.substr(at, e - at));
        at = e + 1;
    }
    return out;
}

std::vector<std::string> Words(std::string_view s) {
    std::vector<std::string> out;
    for (std::string& w : Split(s, ' '))
        if (!w.empty()) out.push_back(std::move(w));
    return out;
}

bool Empty(const std::string& s) { return s.empty() || s == "-"; }

std::vector<std::string> ListFiles(const std::string& dir, const std::string& suffix) {
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path p(std::u8string(dir.begin(), dir.end()));
    for (std::filesystem::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& e = *it;
        if (!e.is_regular_file(ec)) continue;
        const std::u8string u = e.path().u8string();
        std::string name(u.begin(), u.end());
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Rows of a TSV file after its header line (the first line not starting with #).
template <class Fn> void Rows(const std::string& text, Fn fn) {
    bool header = false;
    size_t at = 0, lineNo = 0;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (!header) {
            header = true;
            continue;
        }
        fn(Split(line, '\t'), lineNo);
    }
}

const char* const kPos[] = {"n", "pn", "num", "suf", "pref", "v1", "v5", "vs", "vk", "vz", "adj", "na", "adv", "adn",
                            "conj", "intj", "aux", "p_case", "p_topic", "p_conj", "p_final", "p_adv", "sym", "unk"};

bool IsPosName(std::string_view s) {
    for (const char* p : kPos)
        if (s == p) return true;
    return false;
}

bool ParseAtom(std::string t, Atom& a, std::string& err) {
    if (t.size() > 1 && t.back() == '?') {
        a.optional = true;
        t.pop_back();
    }
    if (t.front() == '{') {
        if (t.back() != '}') return err = "bad slot " + t, false;
        a.k = Atom::K::Slot;
        std::string in = t.substr(1, t.size() - 2);
        const size_t colon = in.find(':');
        a.slot = in.substr(0, colon);
        if (colon != std::string::npos) a.cls = in.substr(colon + 1);
        static const std::set<std::string> slots{"N", "NP", "V", "A", "Na", "X", "C"};
        if (!slots.count(a.slot)) return err = "unknown slot " + a.slot, false;
        return true;
    }
    if (t.front() == '[') {
        if (t.back() != ']') return err = "bad fact atom " + t, false;
        a.k = Atom::K::Fact;
        a.facts = Split(t.substr(1, t.size() - 2), ',');
        return true;
    }
    a.k = Atom::K::Lemma;
    const size_t slash = t.rfind('/');
    if (slash != std::string::npos && slash > 0 && IsPosName(t.substr(slash + 1))) {
        a.pos = t.substr(slash + 1);
        t = t.substr(0, slash);
    }
    a.lemmas = Split(t, '|');
    return true;
}

bool ParseCond(const std::string& t, Cond& c, std::string& err) {
    size_t eq = t.find("!=");
    size_t vat;
    if (eq != std::string::npos) {
        c.negate = true;
        vat = eq + 2;
    } else {
        eq = t.find('=');
        if (eq == std::string::npos) return err = "bad condition " + t, false;
        vat = eq + 1;
    }
    const std::string path = t.substr(0, eq);
    const size_t dot = path.find('.');
    c.target = path.substr(0, dot);
    if (dot != std::string::npos) c.key = path.substr(dot + 1);
    c.values = Split(t.substr(vat), '|');
    static const std::set<std::string> targets{"utt", "cl", "main", "mainpred", "pred", "head", "next", "prev",
                                               "N", "NP", "V", "A", "Na", "X", "C"};
    if (!targets.count(c.target)) return err = "unknown condition target " + c.target, false;
    if (c.target != "utt" && c.key.empty()) return err = "condition without a key: " + t, false;
    if (c.target == "utt" && !c.key.empty() && c.key != "level") return err = "utt has only the key level: " + t, false;
    return true;
}

bool ParseKo(const std::string& t, KoNode& n, std::string& err) {
    const size_t colon = t.find(':');
    if (colon == std::string::npos) return err = "bad node " + t, false;
    n.kind = t.substr(0, colon);
    std::string rest = t.substr(colon + 1);
    const size_t brace = rest.find('{');
    if (brace != std::string::npos) {
        if (rest.back() != '}') return err = "bad node fields " + t, false;
        for (const std::string& f : Split(rest.substr(brace + 1, rest.size() - brace - 2), ',')) {
            if (f.empty()) continue;
            const size_t eq = f.find('=');
            n.fields.emplace_back(f.substr(0, eq), eq == std::string::npos ? "1" : f.substr(eq + 1));
        }
        rest = rest.substr(0, brace);
    }
    n.lemma = rest;
    std::replace(n.lemma.begin(), n.lemma.end(), '_', ' ');  // 더_이상: a lemma of two words
    unsigned k = 0;
    if (!Parse(F::kk, n.kind, k)) return err = "unknown node kind " + n.kind, false;
    return true;
}

std::string Strip(std::string s, std::string_view prefix) {
    if (s.starts_with(prefix)) s.erase(0, prefix.size());
    return s;
}

void LoadRows(const std::string& path, Data& d) {
    std::string text;
    if (!ReadWholeFile(path, text)) {
        d.errors.push_back("cannot read " + path);
        return;
    }
    const std::string file = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string();
    if (text.starts_with("#! numerals")) {
        Rows(text, [&](const std::vector<std::string>& f, size_t) {
            if (f.size() >= 5 && f[0] == "value") d.numerals[std::atoi(f[1].c_str())] = {f[2], f[3], f[4]};
            else if (f.size() >= 3 && (f[0] == "digit" || f[0] == "wago")) d.digits[f[1]] = std::atoi(f[2].c_str());
            else if (f.size() >= 3 && f[0] == "unit") d.units[f[1]] = std::atoi(f[2].c_str());
            else if (f.size() >= 3 && f[0] == "alone") d.alone[f[1]] = std::atoi(f[2].c_str());
            else if (f.size() >= 2 && f[0] == "predicate") d.predicateEnd = f[1];
            else if (f.size() >= 2 && f[0] == "light") d.lightVerb = f[1];
        });
        return;
    }
    if (text.starts_with("#! supp")) {
        Rows(text, [&](const std::vector<std::string>& f, size_t lineNo) {
            if (f.size() < 4) {
                d.errors.push_back(file + ":" + std::to_string(lineNo) + ": a supp row needs id spec ko neg");
                return;
            }
            Row r;
            r.id = f[0];
            r.spec = f[1];
            r.file = file;
            r.match = "-";
            r.when = "pred.ko=" + f[2];
            r.ko = f[3];
            r.then = "supp";
            r.st = f.size() > 4 ? f[4] : "";
            r.prov = f.size() > 5 ? f[5] : "";
            r.order = d.rows.size();
            d.rows.push_back(std::move(r));
        });
        return;
    }
    Rows(text, [&](const std::vector<std::string>& f, size_t lineNo) {
        const std::string where = file + ":" + std::to_string(lineNo) + ": ";
        if (f.size() < 6) {
            d.errors.push_back(where + "fewer than 6 columns");
            return;
        }
        Row r;
        r.id = f[0];
        r.spec = f[1];
        r.match = f[2];
        r.when = f[3];
        r.then = f[4];
        r.ko = f[5];
        r.st = f.size() > 6 ? f[6] : "";
        r.prov = f.size() > 7 ? f[7] : "";
        r.note = f.size() > 8 ? f[8] : "";
        r.file = file;
        std::string err;
        if (!Empty(r.match))
            for (const std::string& w : Words(r.match)) {
                Atom a;
                if (!ParseAtom(w, a, err)) break;
                r.atoms.push_back(std::move(a));
            }
        if (err.empty() && !Empty(r.when))
            for (const std::string& w : Words(r.when)) {
                Cond c;
                if (!ParseCond(w, c, err)) break;
                r.conds.push_back(std::move(c));
            }
        if (err.empty() && !Empty(r.then))
            for (const std::string& w : Split(r.then, ';')) {
                if (w.empty()) continue;
                const size_t dot = w.find('.'), eq = w.find('=');
                if (dot == std::string::npos || eq == std::string::npos || eq < dot) {
                    err = "bad assignment " + w;
                    break;
                }
                r.sets.push_back({w.substr(0, dot), w.substr(dot + 1, eq - dot - 1), w.substr(eq + 1)});
            }
        if (err.empty() && !Empty(r.ko))
            for (const std::string& w : Words(r.ko)) {
                KoNode n;
                if (!ParseKo(w, n, err)) break;
                r.nodes.push_back(std::move(n));
            }
        if (err.empty() && Empty(r.id)) err = "no id";
        if (err.empty() && Empty(r.spec)) err = "no spec id";
        if (!err.empty()) {
            d.errors.push_back(where + err);
            return;
        }
        r.order = d.rows.size();
        d.rows.push_back(std::move(r));
    });
}

void LoadCases(const std::string& path, Data& d) {
    std::string text;
    if (!ReadWholeFile(path, text)) return;
    const std::string file = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string();
    Rows(text, [&](const std::vector<std::string>& f, size_t lineNo) {
        if (f.size() < 4 || (f[1] != "fix" && f[1] != "keep")) {
            d.errors.push_back(file + ":" + std::to_string(lineNo) + ": a case is id fix|keep ja ko src");
            return;
        }
        d.cases.push_back({f[0], f[1], f[2], f[3], f.size() > 4 ? f[4] : "", file});
    });
}

// A lexicon file, by its header: sense rows (ja yomi pos sense ko cue frame feat st prov note) or a level list
// (ja yomi pos … ko …: one row per lemma, ko = sense 1, the other named columns are features of the entry). Sense
// rows replace the sense-1 stand-in a level list gave.
void LoadLexicon(const std::string& path, Data& d, bool fill = false) {
    std::string text;
    if (!ReadWholeFile(path, text)) return;
    const std::string file = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string();
    std::vector<std::string> head;
    size_t at = 0, lineNo = 0;
    static const std::set<std::string> plain{"ja", "yomi", "pos", "sense", "ko", "cue", "frame", "feat", "st",
                                             "prov", "note", "grid", "ctype", "senses",
                                             "gcls", "write", "gframe", "gflags"};
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string row = text.substr(at, e - at);
        at = e + 1;
        lineNo++;
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (row.empty() || row[0] == '#') continue;
        const std::vector<std::string> f = Split(row, '\t');
        if (head.empty()) {
            head = f;
            continue;
        }
        std::map<std::string, std::string> c;
        for (size_t i = 0; i < f.size() && i < head.size(); i++) c[head[i]] = f[i];
        if (c["ja"].empty() || c["ko"].empty()) {
            d.errors.push_back(file + ":" + std::to_string(lineNo) + ": a lexicon row needs ja and ko");
            continue;
        }
        const bool senseRow = c.count("sense") > 0;
        Sense s;
        s.ko = c["ko"];
        s.sense = senseRow ? std::atoi(c["sense"].c_str()) : 1;
        s.cue = c["cue"];
        s.frame = c["frame"];
        s.st = c["st"];
        s.prov = c["prov"];
        s.stand = !senseRow;
        for (const std::string& kv : Words(c["feat"])) {
            const size_t eq = kv.find('=');
            if (eq != std::string::npos) s.feat[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
        std::map<std::string, std::string> entryFeat;
        if (!senseRow)
            for (const auto& [k, v] : c)
                if (!plain.count(k) && !v.empty() && v != "-") entryFeat[k] = v;
        auto& list = d.byLemma[c["ja"]];
        Entry* found = nullptr;
        for (size_t i : list)
            if (d.entries[i].yomi == c["yomi"] && d.entries[i].pos == c["pos"]) found = &d.entries[i];
        // fill: a lemma the curriculum already has keeps its reviewed senses
        if (fill && found && found->file != file) continue;
        if (fill && c["st"] == "u") continue;
        if (!found) {
            Entry ne;
            ne.ja = c["ja"];
            ne.yomi = c["yomi"];
            ne.pos = c["pos"];
            ne.file = file;
            list.push_back(d.entries.size());
            d.entries.push_back(std::move(ne));
            found = &d.entries.back();
        }
        for (const auto& [k, v] : entryFeat) found->feat[k] = v;
        if (senseRow) {
            std::erase_if(found->senses, [](const Sense& x) { return x.stand; });
            found->senses.push_back(std::move(s));
        } else if (found->senses.empty()) {
            found->senses.push_back(std::move(s));
        }
    }
}

// Reviewed lexicon phrases without slots (st=r): id pattern ko cue st prov note. Each is a row matched by its
// Japanese text; its Korean words are nodes (the last a predicate when it is one).
void LoadPhrases(const std::string& path, Data& d) {
    std::string text;
    if (!ReadWholeFile(path, text)) return;
    const std::string file = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string();
    Rows(text, [&](const std::vector<std::string>& f, size_t) {
        if (f.size() < 5 || f[1].find('{') != std::string::npos || f[4] != "r" || !f[3].empty()) return;
        Row r;
        r.id = "ph:" + f[0];
        r.spec = "LEX";
        r.file = file;
        r.phrase = f[1];
        r.ko = f[2];
        r.st = f[4];
        r.prov = f.size() > 5 ? f[5] : "";
        const std::vector<std::string> words = Words(f[2]);
        for (size_t i = 0; i < words.size(); i++) {
            KoNode n;
            n.kind = i + 1 == words.size() && !d.predicateEnd.empty() && words[i].ends_with(d.predicateEnd) ? "V" : "N";
            n.lemma = words[i];
            r.nodes.push_back(std::move(n));
        }
        r.order = d.rows.size();
        d.rows.push_back(std::move(r));
    });
}

void LoadClasses(const std::string& path, Data& d) {
    std::string text;
    if (!ReadWholeFile(path, text)) return;
    Rows(text, [&](const std::vector<std::string>& f, size_t) {
        if (f.empty() || f[0].empty()) return;
        d.classParent[f[0]] = f.size() > 1 ? f[1] : "";
    });
}

void SetSpecificity(const Data& d, Row& r) {
    int lemmas = 0, depth = 0, facts = 0;
    for (const Atom& a : r.atoms) {
        if (a.k == Atom::K::Lemma) lemmas++;
        // a fact that names an aux.tsv row identifies the morpheme, as a lemma does
        if (a.k == Atom::K::Fact)
            for (const std::string& f : a.facts) (f.starts_with("aux.") ? lemmas : facts)++;
        if (a.k == Atom::K::Slot && !a.cls.empty()) depth += d.ClassDepth(a.cls);
    }
    for (const Cond& c : r.conds) {
        if (c.negate) {
            facts++;
        } else if (c.key == "lemma" || c.key == "ko" || c.key == "has" || c.key == "frame") {  // a frame belongs to one lemma
            lemmas++;
        } else if (c.key == "cls") {
            int best = 0;
            for (const std::string& v : c.values) best = best == 0 ? d.ClassDepth(v) : std::min(best, d.ClassDepth(v));
            depth += best;
        } else {
            facts++;
        }
    }
    int consumed = 0;
    for (const Atom& a : r.atoms) consumed += a.k != Atom::K::Slot && !a.optional;
    r.specificity = {lemmas, consumed, depth, facts, static_cast<int>(r.atoms.size())};
}

}  // namespace

int Data::ClassDepth(const std::string& cls) const {
    int depth = 0;
    std::string c = cls;
    for (int guard = 0; guard < 32 && !c.empty(); guard++) {
        auto it = classParent.find(c);
        depth++;
        if (it == classParent.end()) break;
        c = it->second;
    }
    return depth;
}

bool Data::IsA(const std::string& cls, const std::string& ancestor) const {
    std::string c = cls;
    for (int guard = 0; guard < 32 && !c.empty(); guard++) {
        if (c == ancestor) return true;
        auto it = classParent.find(c);
        if (it == classParent.end()) return false;
        c = it->second;
    }
    return false;
}

void Load(const std::string& root, Data& d) {
    const std::string xfer = root + "/transfer/data/xfer";
    for (const std::string& f : ListFiles(xfer, ".tsv")) {
        if (f.size() > 10 && f.ends_with(".cases.tsv")) LoadCases(f, d);
        else LoadRows(f, d);
    }
    LoadClasses(root + "/lexicon/lex/classes.tsv", d);
    for (const std::string& f : ListFiles(root + "/lexicon/lex/levels", ".tsv")) LoadLexicon(f, d);
    LoadLexicon(root + "/lexicon/lex/lexicon/curriculum.tsv", d);
    LoadPhrases(root + "/lexicon/lex/phrases-curriculum.tsv", d);
    for (size_t i = 0; i < d.rows.size(); i++)
        if (d.rows[i].then == "supp") d.supp[d.rows[i].when.substr(8)] = i;
    for (const Entry& e : d.entries)
        if (e.pos == "n")
            for (const Sense& sn : e.senses)
                if (sn.ko.find(' ') == std::string::npos) d.koNouns.insert(sn.ko);
    for (Row& r : d.rows) {
        SetSpecificity(d, r);
        // a phrase names its words: it is more specific than any grammar row over them
        if (!r.phrase.empty()) r.specificity = {static_cast<int>(r.phrase.size()), static_cast<int>(r.phrase.size()), 0, 0, 0};
    }
    std::set<std::string> ids;
    for (const Row& r : d.rows)
        if (!ids.insert(r.id).second) d.errors.push_back(r.file + ": duplicate id " + r.id);
}

std::vector<std::string> SpecIds(const std::string& specDir) {
    std::set<std::string> out;
    std::error_code ec;
    const std::filesystem::path p(std::u8string(specDir.begin(), specDir.end()));
    for (std::filesystem::recursive_directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& e = *it;
        if (!e.is_regular_file(ec) || e.path().extension() != ".md") continue;
        const std::u8string u = e.path().u8string();
        std::string text;
        if (!ReadWholeFile(std::string(u.begin(), u.end()), text)) continue;
        // an id is at the start of a table cell or after "## ": AREA-N, AREA-HN, N5-N
        for (size_t i = 0; i + 3 < text.size(); i++) {
            const bool start = text.compare(i, 2, "| ") == 0 || text.compare(i, 3, "## ") == 0;
            if (!start) continue;
            size_t j = i + (text[i] == '|' ? 2 : 3), k = j;
            while (k < text.size() && (std::isalnum(static_cast<unsigned char>(text[k])) || text[k] == '-')) k++;
            const std::string id = text.substr(j, k - j);
            if (id.find('-') != std::string::npos && id.size() > 2 && std::isdigit(static_cast<unsigned char>(id.back())))
                out.insert(id);
        }
    }
    return {out.begin(), out.end()};
}

namespace {

bool IsVerbPos(Pos p) { return p == Pos::v1 || p == Pos::v5 || p == Pos::vs || p == Pos::vk || p == Pos::vz; }
bool IsNounPos(Pos p) { return p == Pos::n || p == Pos::pn || p == Pos::num; }
bool IsContentPos(Pos p) {
    return IsVerbPos(p) || IsNounPos(p) || p == Pos::adj || p == Pos::na || p == Pos::adv || p == Pos::adn ||
           p == Pos::conj || p == Pos::intj || p == Pos::pref || p == Pos::suf;
}

std::string PosName(Pos p) { return std::string(Name(F::pos, static_cast<unsigned>(p))); }

// One analysed token as the matcher reads it.
struct Tk {
    const Token* t = nullptr;    // the first analyser token of the unit
    int first = 0, last = 0;     // analyser tokens of the unit (a lexicon long unit joins several)
    Span src;
    Pos epos = Pos::unk;         // the part of speech the unit is read as (the lexicon's, when it names one)
    std::string lemma, surf, pos, form;
    const TokAux* aux = nullptr;
    bool content = false;  // a content word that no function row names
    const Entry* entry = nullptr;
    const Sense* sense = nullptr;
    std::string senseWhy;        // the cue that chose the sense ("" = sense 1 by default)
    int number = -1;             // a numeral unit: its value (-1 none); digits = written in digits
    bool digits = false;
    const Entry* counter = nullptr;  // the counter word joined to the numeral
    int bun = -1, clause = -1;
    uint16_t utt = 0;
    bool dialogue = false;
};

struct Cand {
    size_t row = 0;
    int begin = 0, end = 0;               // matched token range [begin, end)
    std::vector<int> consumed;            // token indexes
    std::map<std::string, int> slots;     // slot name → token (first of each name), for conditions and targets
    std::vector<int> slotToks;            // every token a slot read
    int anchor = -1;
    int clause = -1;
    std::vector<std::string> because;
};

bool SpecGreater(const std::array<int, 5>& a, const std::array<int, 5>& b) { return a > b; }

struct Ctx {
    const Data& d;
    Doc& doc;
    Line& line;
    std::vector<Tk> tk;
    std::vector<int> tkOf;       // analyser token → unit
    std::vector<TokAux> aux;

    int ClauseOfBun(int b) const {
        int best = -1, width = 1 << 30;
        for (size_t c = 0; c < line.clauses.size(); c++) {
            const Clause& cl = line.clauses[c];
            if (b < cl.firstBun || b > cl.lastBun) continue;
            if (cl.lastBun - cl.firstBun < width) {
                width = cl.lastBun - cl.firstBun;
                best = static_cast<int>(c);
            }
        }
        // a clause owns its predicate bunsetsu
        for (size_t c = 0; c < line.clauses.size(); c++)
            if (line.clauses[c].pred == b && b >= line.clauses[c].firstBun && b <= line.clauses[c].lastBun)
                return static_cast<int>(c);
        return best;
    }

    bool SlotOk(const Atom& a, int i) const {
        const Tk& k = tk[static_cast<size_t>(i)];
        // {NP}: a noun, or a nominaliser (の, こと) standing where a noun does
        if (a.slot == "NP" && std::find(k.aux->facts.begin(), k.aux->facts.end(), "role=nomi") != k.aux->facts.end())
            return true;
        if (!k.content) return false;
        const Pos p = k.epos;
        bool ok = false;
        if (a.slot == "N" || a.slot == "NP") ok = IsNounPos(p);
        else if (a.slot == "V") ok = IsVerbPos(p);
        else if (a.slot == "A") ok = p == Pos::adj;
        else if (a.slot == "Na") ok = p == Pos::na;
        else if (a.slot == "X") ok = IsVerbPos(p) || IsNounPos(p) || p == Pos::adj || p == Pos::na;
        else if (a.slot == "C") ok = true;
        if (!ok) return false;
        if (!a.cls.empty()) {
            bool isA = false;
            std::string cls = TokKey(i, "cls");
            std::replace(cls.begin(), cls.end(), '|', ' ');
            for (const std::string& one : Split(cls, ' '))
                isA = isA || (!one.empty() && d.IsA(one, a.cls));
            if (!isA) return false;
        }
        return true;
    }

    static std::string Get(const std::map<std::string, std::string>& m, const std::string& k) {
        auto it = m.find(k);
        return it == m.end() ? std::string() : it->second;
    }

    bool AtomOk(const Atom& a, int i) const {
        const Tk& k = tk[static_cast<size_t>(i)];
        switch (a.k) {
            case Atom::K::Slot: return SlotOk(a, i);
            case Atom::K::Lemma:
                if (!a.pos.empty() && a.pos != k.pos) return false;
                return std::find(a.lemmas.begin(), a.lemmas.end(), k.lemma) != a.lemmas.end();
            case Atom::K::Fact:
                for (const std::string& f : a.facts) {
                    const auto& fs = k.aux->facts;
                    if (std::find(fs.begin(), fs.end(), f) == fs.end() && k.aux->row != "aux:" + f &&
                        k.aux->form != "aux:" + f)
                        return false;
                }
                return true;
        }
        return false;
    }

    // Every way the row's atoms match from token i (optional atoms tried absent and present).
    void MatchFrom(const Row& r, size_t ai, int i, Cand& c, std::vector<Cand>& out) const {
        if (ai == r.atoms.size()) {
            Cand x = c;
            x.end = i;
            out.push_back(std::move(x));
            return;
        }
        const Atom& a = r.atoms[ai];
        if (a.optional) MatchFrom(r, ai + 1, i, c, out);
        if (i >= static_cast<int>(tk.size()) || !AtomOk(a, i)) return;
        Cand y = c;
        if (a.k == Atom::K::Slot) {
            y.slots.emplace(a.slot, i);
            y.slotToks.push_back(i);
        } else {
            y.consumed.push_back(i);
        }
        MatchFrom(r, ai + 1, i + 1, y, out);
    }

    // The value a condition path reads, for one candidate; found = false when the target does not exist.
    std::string TokKey(int i, const std::string& key) const {
        if (i < 0 || i >= static_cast<int>(tk.size())) return "";
        const Tk& k = tk[static_cast<size_t>(i)];
        if (key == "lemma") return k.lemma;
        if (key == "ispred") return PredTok(k.clause) == i ? "+" : "";
        if (key == "pos") return k.pos;
        if (key == "form") return k.form;
        if (key == "surf") return k.surf;
        if (key == "ko") return k.sense ? k.sense->ko : "";
        if (key == "frame") return k.sense ? k.sense->frame : "";
        if (key == "cls" && k.sense) {
            const std::string c = Get(k.sense->feat, "cls");
            return c.empty() && k.entry && k.sense->sense == 1 ? Get(k.entry->feat, "cls") : c;
        }
        if (key == "kind" || key == "role" || key == "kase" || key == "link") {
            for (const std::string& f : k.aux->facts)
                if (f.starts_with(key + "=")) return f.substr(key.size() + 1);
            return "";
        }
        if (!k.sense) return "";
        if (key == "trans") {
            // a verb that takes an object: its case frame (args) names o=
            std::string a = Get(k.sense->feat, "args");
            if (a.empty() && k.entry && k.sense->sense == 1) a = Get(k.entry->feat, "args");
            return (' ' + a).find(" o=") != std::string::npos ? "+" : "";
        }
        const std::string v = Get(k.sense->feat, key);
        // a level list's columns describe sense 1
        return v.empty() && k.entry && k.sense->sense == 1 ? Get(k.entry->feat, key) : v;
    }

    std::string ClauseKey(int c, const std::string& key) const {
        if (c < 0 || c >= static_cast<int>(line.clauses.size())) return "";
        const Clause& cl = line.clauses[static_cast<size_t>(c)];
        auto nm = [](F f, unsigned v) { return std::string(Name(f, v)); };
        if (key == "tense") return nm(F::tense, cl.tense);
        if (key == "pol") return nm(F::pol, cl.pol);
        if (key == "negFn") return nm(F::negfn, cl.negFn);
        if (key == "force") return nm(F::force, cl.force);
        if (key == "polite") return nm(F::polite, cl.polite);
        if (key == "link") return nm(F::link, cl.link);
        if (key == "linkSub") return nm(F::timesub, cl.linkSub);
        if (key == "aspect") return nm(F::jaspect, cl.aspect);
        if (key == "voice") return nm(F::voice, cl.voice);
        if (key == "bene") return nm(F::bene, cl.bene);
        if (key == "mod") {
            std::string s;
            for (unsigned b = 0; b < Count(F::modality); b++)
                if (cl.modality & (1u << b)) s += (s.empty() ? "" : ",") + nm(F::modality, b);
            return s;
        }
        return "";
    }

    // Sense choice by cue rules (DESIGN-v3 §5): a sense applies when every atom of its cue holds; among those the
    // most specific wins (lemma atoms, class depth, form and neighbour atoms, register, atom count); none → the
    // lowest sense without a cue. Atoms: subj obj dat loc with from to arg (the clause's arguments when the word is
    // its predicate), head (the noun an adnominal clause modifies), pred (the predicate a noun belongs to), next and
    // after (neighbouring lemmas), mod, form (adv: a 連用形 adjective before a predicate), register (polite), main.vol.
    const Sense* Choose(size_t i, std::string& why) const {
        const Tk& k = tk[i];
        const Entry& e = *k.entry;
        auto classesOf = [&](const std::string& lemma) {
            std::vector<std::string> out;
            auto it = d.byLemma.find(lemma);
            if (it == d.byLemma.end()) return out;
            for (size_t ei : it->second) {
                const Entry& en = d.entries[ei];
                std::string c = Get(en.feat, "cls");
                for (const Sense& s : en.senses)
                    if (s.sense == 1 && c.empty()) c = Get(s.feat, "cls");
                std::replace(c.begin(), c.end(), '|', ' ');
                for (const std::string& two : Split(c, ' '))
                    if (!two.empty()) out.push_back(two);
            }
            return out;
        };
        // the context
        std::map<std::string, std::vector<std::string>> args;
        std::vector<std::string> heads, preds, nexts, afters;
        std::set<std::string> forms{k.form};
        const int cl = k.clause;
        if (cl >= 0) {
            const Clause& c = line.clauses[static_cast<size_t>(cl)];
            if (PredTok(cl) == static_cast<int>(i))
                for (const Arg& a : c.args) {
                    std::string role(Name(F::role, static_cast<unsigned>(a.role)));
                    if (role == "topic") role = "subj";
                    if (role == "head") heads.emplace_back(doc.S(a.lemma));
                    else args[role].emplace_back(doc.S(a.lemma));
                }
        }
        if (k.bun >= 0 && line.buns[static_cast<size_t>(k.bun)].head >= 0) {
            const int h = tkOf[line.buns[static_cast<size_t>(line.buns[static_cast<size_t>(k.bun)].head)].content];
            if (h >= 0 && tk[static_cast<size_t>(h)].content) preds.push_back(tk[static_cast<size_t>(h)].lemma);
        }
        if (i + 1 < tk.size()) nexts.push_back(tk[i + 1].lemma);
        if (i > 0) afters.push_back(tk[i - 1].lemma);
        if (k.epos == Pos::adj && k.form == "renyo" && i + 1 < tk.size() && tk[i + 1].content) forms.insert("adv");
        // cop / attr: a noun read as a predicate (便利だ, 便利な, 便利ではない)
        if (i + 1 < tk.size() && (IsNounPos(k.epos) || k.epos == Pos::na)) {
            const Tk& nx = tk[i + 1];
            auto has = [&](const Tk& t, const char* f) {
                return std::find(t.aux->facts.begin(), t.aux->facts.end(), f) != t.aux->facts.end();
            };
            if (has(nx, "role=cop") || has(nx, "role=cop-ja") || has(nx, "role=cop-te") || has(nx, "role=cop-ni") ||
                has(nx, "role=daro") ||
                (has(nx, "kase=de") && i + 2 < tk.size() && has(tk[i + 2], "kase=wa")))
                forms.insert("cop");
            if (has(nx, "role=cop") && nx.form == "rentai") forms.insert("attr");
            // a noun that ends its clause as a bare predicate (便利？) is read with its predicate sense too
            if (has(nx, "role=q") && nx.t->pos == Pos::sym) forms.insert("cop");
        }
        // ta: the clause is in the past (もう + past → 벌써)
        if (cl >= 0 && line.clauses[static_cast<size_t>(cl)].tense == tense::past) forms.insert("ta");
        // the noun an adnominal adjective modifies is its subject (高い本: 本が高い)
        if ((k.epos == Pos::adj || k.epos == Pos::na) && args["subj"].empty()) args["subj"] = heads;
        const Utterance* u = k.utt < line.utts.size() ? &line.utts[k.utt] : nullptr;
        const bool polite = (u && (u->sourceLevel == srclevel::desu_masu || u->sourceLevel == srclevel::keigo)) ||
                            (cl >= 0 && line.clauses[static_cast<size_t>(cl)].polite != polite::plain);
        const std::string mainVol = [&] {
            if (cl < 0 || line.clauses[static_cast<size_t>(cl)].parent < 0) return std::string();
            return TokKey(PredTok(line.clauses[static_cast<size_t>(cl)].parent), "vol");
        }();
        auto nounsMatch = [&](const std::vector<std::string>& nouns, const std::vector<std::string>& vals,
                              int& lemmaHit, int& depth) {
            bool any = false;
            for (const std::string& nn : nouns)
                for (const std::string& v : vals) {
                    if (v.starts_with("@")) {
                        for (const std::string& c : classesOf(nn))
                            if (d.IsA(c, v)) {
                                any = true;
                                depth = std::max(depth, d.ClassDepth(v));
                            }
                    } else if (v == nn) {
                        any = true;
                        lemmaHit = 1;
                    }
                }
            return any;
        };
        static const std::set<std::string> argSlots{"subj", "obj", "dat", "loc", "with", "from", "to"};
        std::array<int, 5> best{};
        const Sense* chosen = nullptr;
        for (const Sense& s : e.senses) {
            if (s.cue.empty()) continue;
            std::array<int, 5> t{};
            bool holds = true;
            for (const std::string& atom : Words(s.cue)) {
                const size_t eq = atom.find('=');
                const std::string slot = atom.substr(0, eq);
                const std::vector<std::string> vals = Split(eq == std::string::npos ? "" : atom.substr(eq + 1), '|');
                const bool none = vals.size() == 1 && vals[0] == "-";
                int lemmaHit = 0, depth = 0;
                bool ok = false;
                int kind = 2;
                if (argSlots.count(slot) || slot == "arg") {
                    std::vector<std::string> nouns;
                    for (const auto& [role, list] : args)
                        if (slot == "arg" ? argSlots.count(role) > 0 : role == slot) nouns.insert(nouns.end(), list.begin(), list.end());
                    ok = none ? nouns.empty() : nounsMatch(nouns, vals, lemmaHit, depth);
                    kind = none ? 2 : lemmaHit ? 0 : 1;
                } else if (slot == "head") {
                    ok = none ? heads.empty() : nounsMatch(heads, vals, lemmaHit, depth);
                    kind = none ? 2 : lemmaHit ? 0 : 1;
                } else if (slot == "pred" || slot == "next" || slot == "after" || slot == "mod") {
                    const std::vector<std::string>& pool = slot == "pred" ? preds : slot == "next" ? nexts
                                                         : slot == "after" ? afters : std::vector<std::string>{};
                    ok = none ? pool.empty() : nounsMatch(pool, vals, lemmaHit, depth);
                } else if (slot == "form") {
                    for (const std::string& v : vals) ok = ok || forms.count(v) > 0;
                } else if (slot == "main.vol") {
                    for (const std::string& v : vals) ok = ok || v == mainVol;
                } else if (slot == "register") {
                    for (const std::string& v : vals) ok = ok || (v == "polite" && polite) || (v == "casual" && !polite);
                    kind = 3;
                }
                if (!ok) {
                    holds = false;
                    break;
                }
                if (kind == 0) t[0]++;
                else if (kind == 1) t[1] += depth;
                else if (kind == 2) t[2]++;
                else t[3]++;
                t[4]++;
            }
            if (holds && (!chosen || t > best)) {
                best = t;
                chosen = &s;
            }
        }
        if (chosen) {
            why = chosen->cue;
            return chosen;
        }
        const Sense* first = nullptr;
        for (const Sense& s : e.senses)
            if (!first || s.sense < first->sense) first = &s;
        return first;
    }

    // The unit is followed by a copula (だ, な, です, で, じゃ, だろう) or by で + は.
    bool Copular(size_t i) const {
        if (i + 1 >= tk.size()) return false;
        const Tk& nx = tk[i + 1];
        auto has = [&](const Tk& t, const char* f) {
            return std::find(t.aux->facts.begin(), t.aux->facts.end(), f) != t.aux->facts.end();
        };
        return has(nx, "role=cop") || has(nx, "role=cop-ja") || has(nx, "role=cop-te") || has(nx, "role=cop-ni") ||
               has(nx, "role=daro") || (has(nx, "kase=de") && i + 2 < tk.size() && has(tk[i + 2], "kase=wa"));
    }

    int PredTok(int c) const {
        if (c < 0 || c >= static_cast<int>(line.clauses.size())) return -1;
        const Clause& cl = line.clauses[static_cast<size_t>(c)];
        if (cl.pred >= line.buns.size() || line.buns[cl.pred].content >= tkOf.size()) return -1;
        return tkOf[line.buns[cl.pred].content];
    }

    bool CondOk(const Cond& cd, const Cand& c, std::string& fact) const {
        std::string v;
        bool multi = false;  // mod holds several values
        if (cd.target == "utt" && cd.key == "level") {
            // the source level S read for the utterance: plain desu-masu keigo archaic casual
            const Tk* a = c.anchor >= 0 ? &tk[static_cast<size_t>(c.anchor)] : nullptr;
            if (a && a->utt < line.utts.size()) v = std::string(Name(F::srclevel, line.utts[a->utt].sourceLevel));
        } else if (cd.target == "utt") {
            v = c.anchor >= 0 && tk[static_cast<size_t>(c.anchor)].dialogue ? "dialogue" : "narration";
        } else if (cd.target == "cl" && cd.key == "has") {
            // the lemmas of the clause's tokens
            multi = true;
            if (c.clause >= 0) {
                const Clause& cl = line.clauses[static_cast<size_t>(c.clause)];
                for (size_t i = 0; i < tk.size(); i++)
                    if (tk[i].bun >= cl.firstBun && tk[i].bun <= cl.lastBun) v += (v.empty() ? "" : ",") + tk[i].lemma;
            }
        } else if (cd.target == "cl" && cd.key == "endform") {
            // the inflected form the clause's predicate bunsetsu ends in (punctuation aside)
            if (c.clause >= 0 && line.clauses[static_cast<size_t>(c.clause)].pred < line.buns.size()) {
                const Bunsetsu& b = line.buns[line.clauses[static_cast<size_t>(c.clause)].pred];
                for (int i = b.last; i >= b.first; i--)
                    if (static_cast<size_t>(i) < tkOf.size() && tk[static_cast<size_t>(tkOf[static_cast<size_t>(i)])].t->pos != Pos::sym) {
                        v = tk[static_cast<size_t>(tkOf[static_cast<size_t>(i)])].form;
                        break;
                    }
            }
        } else if (cd.target == "cl" || cd.target == "main") {
            int cl = c.clause;
            if (cd.target == "main") cl = cl >= 0 ? line.clauses[static_cast<size_t>(cl)].parent : -1;
            if (cd.key == "args") {
                // the roles of the clause's overt arguments
                multi = true;
                if (cl >= 0)
                    for (const Arg& ag : line.clauses[static_cast<size_t>(cl)].args)
                        if (ag.how != how::omitted)
                            v += (v.empty() ? "" : ",") + std::string(Name(F::role, static_cast<unsigned>(ag.role)));
            } else {
                v = ClauseKey(cl, cd.key);
                multi = cd.key == "mod";
            }
        } else if (cd.target == "mainpred") {
            // the predicate word of the main clause, as a lexicon word (main.vol in the sense cues)
            const int p = c.clause >= 0 ? line.clauses[static_cast<size_t>(c.clause)].parent : -1;
            v = TokKey(PredTok(p), cd.key);
        } else if (cd.target == "pred") {
            v = TokKey(PredTok(c.clause), cd.key);
        } else if (cd.target == "head") {
            int h = -1;
            if (c.anchor >= 0) {
                const int b = tk[static_cast<size_t>(c.anchor)].bun;
                if (b >= 0 && line.buns[static_cast<size_t>(b)].head >= 0)
                    h = tkOf[line.buns[static_cast<size_t>(line.buns[static_cast<size_t>(b)].head)].content];
            }
            v = TokKey(h, cd.key);
        } else if (cd.target == "next") {
            v = TokKey(c.end, cd.key);
        } else if (cd.target == "prev") {
            v = TokKey(c.begin - 1, cd.key);
        } else {
            auto it = c.slots.find(cd.target);
            v = it == c.slots.end() ? "" : TokKey(it->second, cd.key);
        }
        if (cd.key == "frame" || cd.key == "cls") {
            std::replace(v.begin(), v.end(), ' ', ',');
            std::replace(v.begin(), v.end(), '|', ',');
            multi = true;
        }
        bool hit = false;
        for (const std::string& want : cd.values) {
            if (want == "absent") hit = hit || v.empty();
            else if (want == "present") hit = hit || !v.empty();
            else if (cd.key == "cls") {
                for (const std::string& one : Split(v, ','))
                    hit = hit || (!one.empty() && d.IsA(one, want));
            }
            else if (multi) hit = hit || (',' + v + ',').find(',' + want + ',') != std::string::npos;
            else hit = hit || v == want;
        }
        if (cd.negate) hit = !hit;
        fact = cd.target + (cd.key.empty() ? "" : "." + cd.key) + "=" + (v.empty() ? "-" : v);
        return hit;
    }
};

std::string Hiragana(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xE3 && i + 2 < s.size()) {
            uint32_t cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                          (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            if (cp >= 0x30A1 && cp <= 0x30F6) cp -= 0x60;
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
            i += 3;
            continue;
        }
        out += s[i++];
    }
    return out;
}

// The value of a numeral written in digits or in kanji (三, 二十, 百五十), -1 when it is neither.
int NumberValue(const Data& d, std::string_view s, bool& digits) {
    digits = false;
    if (s.empty()) return -1;
    long v = 0;
    bool allDigits = true;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
        } else if (c == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBC &&
                   static_cast<unsigned char>(s[i + 2]) >= 0x90 && static_cast<unsigned char>(s[i + 2]) <= 0x99) {
            v = v * 10 + (static_cast<unsigned char>(s[i + 2]) - 0x90);  // full-width digit
            i += 2;
        } else {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        digits = true;
        return v > 1000000 ? -1 : static_cast<int>(v);
    }
    if (auto w = d.digits.find(std::string(s)); w != d.digits.end()) return w->second;  // ひと, ふた
    // kanji numerals by the table's digit and unit rows (三, 二十, 百五十)
    long total = 0, part = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        const std::string ch(s.substr(i, len));
        i += len;
        if (auto dg = d.digits.find(ch); dg != d.digits.end()) {
            part = part * 10 + dg->second;
        } else if (auto un = d.units.find(ch); un != d.units.end()) {
            total += (part == 0 ? 1 : part) * un->second;
            part = 0;
        } else {
            return -1;
        }
    }
    return static_cast<int>(total + part);
}

// The counter entry of a token (a lexicon word with a num feature: つ 人 枚 円), by its lemma without origin tag.
const Entry* FindCounter(const Data& d, std::string lemma) {
    if (const size_t dash = lemma.find('-'); dash != std::string::npos && dash > 0) lemma = lemma.substr(0, dash);
    auto it = d.byLemma.find(lemma);
    if (it == d.byLemma.end()) return nullptr;
    for (size_t i : it->second) {
        const Entry& e = d.entries[i];
        if (e.pos != "suf" && e.pos != "n") continue;
        for (const Sense& s : e.senses)
            if (s.feat.count("num")) return &e;
        if (e.feat.count("num")) return &e;
    }
    return nullptr;
}

// The Korean numeral of a value: native (adnominal before a counter) up to 99, else Sino-Korean.
std::string Numeral(const Data& d, int v, bool native, bool adn) {
    auto get = [&](int k, int col) -> std::string {
        auto it = d.numerals.find(k);
        if (it == d.numerals.end()) return "";
        const std::string& w = col == 0 ? it->second[0] : col == 1 ? it->second[1] : it->second[2];
        return w == "-" ? "" : w;
    };
    if (native && v >= 1 && v <= 99) {
        const int tens = v / 10 * 10, ones = v % 10;
        if (ones == 0) return get(tens, adn ? 1 : 0);
        return (tens ? get(tens, 0) : "") + get(ones, adn ? 1 : 0);
    }
    if (v <= 0) return "";
    std::string out;
    int rest = v;
    for (int unit : {1000, 100, 10}) {
        const int q = rest / unit;
        rest %= unit;
        if (q == 0) continue;
        out += (q > 1 ? get(q, 2) : "") + get(unit, 2);
    }
    if (rest) out += get(rest, 2);
    return out;
}

// The entry of a spelling read as a noun (n, pn or na), for joined units.
const Entry* FindSpelling(const Data& d, const std::string& ja, const char*) {
    auto it = d.byLemma.find(ja);
    if (it == d.byLemma.end()) return nullptr;
    for (size_t i : it->second) {
        const std::string& p = d.entries[i].pos;
        if (p == "n" || p == "pn" || p == "na") return &d.entries[i];
    }
    return nullptr;
}

// The lexicon entry of a token: by its lemma, then by the lemma without UniDic's origin tag (パン-pao, 私-代名詞),
// then by its spelling (proper nouns, whose lemma is a reading). The pos must agree: verb classes match each
// other, and nouns match nouns, proper nouns and な-adjectives (UniDic's 形状詞可能 nouns are な-adjectives too).
const Entry* FindEntry(const Data& d, const Doc& doc, const Line& line, const Token& t) {
    const std::string pos = PosName(t.pos);
    auto pick = [&](const std::string& key) -> const Entry* {
        auto it = d.byLemma.find(key);
        if (it == d.byLemma.end()) return nullptr;
        const Entry* exact = nullptr;
        const Entry* loose = nullptr;
        for (size_t i : it->second) {
            const Entry& e = d.entries[i];
            Pos ep = Pos::unk;
            unsigned v = 0;
            if (Parse(F::pos, e.pos, v)) ep = static_cast<Pos>(v);
            if (e.pos == pos) {
                if (!exact || Hiragana(doc.S(t.reading)) == e.yomi) exact = &e;
            } else if (!loose && ((IsVerbPos(ep) && IsVerbPos(t.pos)) ||
                                  ((IsNounPos(ep) || ep == Pos::na) && (IsNounPos(t.pos) || t.pos == Pos::na)))) {
                loose = &e;
            }
        }
        return exact ? exact : loose;
    };
    const std::string lemma(doc.S(t.lemma));
    if (const Entry* e = pick(lemma)) return e;
    if (const size_t dash = lemma.find('-'); dash != std::string::npos && dash > 0)
        if (const Entry* e = pick(lemma.substr(0, dash))) return e;
    if (t.pos == Pos::pn)
        if (const Entry* e = pick(std::string(line.Src(t.src)))) return e;
    return nullptr;
}

bool SetField(KNode& n, Doc& doc, const std::string& field, const std::string& value) {
    unsigned v = 0;
    auto en = [&](F f, uint8_t& slot) {
        if (!Parse(f, value, v)) return false;
        slot = static_cast<uint8_t>(v);
        return true;
    };
    const bool on = value != "0";
    if (field == "neg") return en(F::kneg, n.neg);
    if (field == "tense") return en(F::tense, n.tense);
    if (field == "aspect") return en(F::kaspect, n.aspect);
    if (field == "mood") return en(F::mood, n.mood);
    if (field == "ctype") return en(F::ctype, n.ctype);
    if (field == "rel") return en(F::rel, n.rel);
    if (field == "kase") return en(F::kase, n.kase);
    if (field == "punct") return en(F::punct, n.punct);
    if (field == "vpos") return en(F::vpos, n.vpos);
    if (field == "modal") return en(F::modal, n.modal);
    if (field == "kcls") return en(F::kcls, n.kcls);
    if (field == "lemma") return n.lemma = doc.I(value), true;
    if (field == "kind") {
        if (!Parse(F::kk, value, v)) return false;
        n.kind = static_cast<KK>(v);
        return true;
    }
    if (field == "eomi") return n.eomi = doc.I(value), true;
    if (field == "aux") return n.aux = on, true;
    if (field == "dep") return n.dep = on, true;
    if (field == "glue") return n.glue = on, true;
    if (field == "passive") return n.passive = on, true;
    if (field == "causative") return n.causative = on, true;
    if (field == "potential") return n.potential = on, true;
    if (field == "spont") return n.spont = on, true;
    return false;
}

uint32_t SemOf(const Data& d, std::string classes) {
    uint32_t m = 0;
    std::replace(classes.begin(), classes.end(), '|', ' ');
    for (const std::string& c : Split(classes, ' ')) {
        if (c.empty()) continue;
        if (d.IsA(c, "@person")) m |= sem::person | sem::animate;
        if (d.IsA(c, "@animal")) m |= sem::animate;
        if (d.IsA(c, "@place")) m |= sem::place;
        if (d.IsA(c, "@time")) m |= sem::time;
    }
    return m;
}

const TokAux& NoAux() {
    static const TokAux none;
    return none;
}

struct Placed {
    KNode n;
    int cand = -1;         // the accepted candidate whose template made the node (-1: a lexicon word)
    int tok = 0, sub = 0;  // order key
    int ptok = 0;          // token whose bunsetsu the node belongs to
};

}  // namespace

void Transfer(const Data& d, Doc& doc, Line& line) {
    line.kir.clear();
    line.unmatched.clear();
    if (!d.errors.empty()) {
        line.unmatched.push_back(doc.I("native-tables/error"));
        line.route = 2;
        return;
    }
    Ctx x{d, doc, line, {}, {}, {}};
    x.aux = d.structure ? AuxOf(*d.structure, doc, line.toks) : std::vector<TokAux>(line.toks.size());
    x.aux.resize(line.toks.size());
    // Units: analyser tokens, with runs of noun parts joined where the joined spelling is a lexicon entry
    // (日本 + 語 → 日本語), longest first.
    const size_t nt = line.toks.size();
    x.tkOf.assign(nt, 0);
    auto nounPart = [&](size_t i) {
        const Pos p = line.toks[i].pos;
        return (IsNounPos(p) || p == Pos::suf || p == Pos::pref) && x.aux[i].row.empty();
    };
    for (size_t i = 0; i < nt;) {
        size_t last = i;
        const Entry* joined = nullptr;
        // a numeral (三, 二十, 100) and the counter after it (つ, 人, 円) are one unit
        if (line.toks[i].pos == Pos::num) {
            std::string spell(line.Src(line.toks[i].src));
            size_t j = i;
            while (j + 1 < nt && line.toks[j + 1].pos == Pos::num) spell += line.Src(line.toks[++j].src);
            bool digits = false;
            const int value = NumberValue(d, spell, digits);
            if (value >= 0) {
                Tk k;
                const Token& t = line.toks[i];
                k.t = &t;
                k.first = static_cast<int>(i);
                const Entry* counter = j + 1 < nt ? FindCounter(d, std::string(doc.S(line.toks[j + 1].lemma))) : nullptr;
                if (counter) j++;
                k.last = static_cast<int>(j);
                k.src = {t.src.begin, line.toks[j].src.end};
                k.surf = std::string(line.Src(k.src));
                k.lemma = spell;
                k.pos = "num";
                k.epos = Pos::n;
                k.content = true;
                k.aux = &NoAux();  // the unit is a word: the counter's function reading (時 → link time) is not
                k.number = value;
                k.digits = digits;
                k.counter = counter;
                for (size_t u = 0; u < line.utts.size(); u++)
                    if (Contains(line.utts[u].span, k.src)) {
                        k.utt = static_cast<uint16_t>(u);
                        k.dialogue = line.utts[u].depth > 0 || !line.utts[u].narration;
                    }
                for (size_t m = i; m <= j; m++) x.tkOf[m] = static_cast<int>(x.tk.size());
                x.tk.push_back(std::move(k));
                i = j + 1;
                continue;
            }
        }
        if (nounPart(i)) {
            std::string spell(line.Src(line.toks[i].src));
            for (size_t j = i + 1; j < nt && j < i + 6 && nounPart(j); j++) {
                spell += line.Src(line.toks[j].src);
                if (const Entry* e = FindSpelling(d, spell, "n")) {
                    joined = e;
                    last = j;
                }
            }
        }
        Tk k;
        const Token& t = line.toks[i];
        k.t = &t;
        k.first = static_cast<int>(i);
        k.last = static_cast<int>(last);
        k.src = {t.src.begin, line.toks[last].src.end};
        k.surf = std::string(line.Src(k.src));
        k.aux = joined ? &NoAux() : &x.aux[last];
        k.form = Strip(Strip(x.aux[last].form, "aux:"), "aux.form.");
        if (joined) {
            k.lemma = joined->ja;
            k.pos = joined->pos;
            k.content = true;
            k.entry = joined;
        } else {
            k.lemma = std::string(doc.S(t.lemma));
            k.pos = PosName(t.pos);
            // a content word: a content part of speech that no function row of aux.tsv names (quote rows name
            // verbs that may take a quote; they stay content)
            k.content = IsContentPos(t.pos) &&
                        (x.aux[i].row.empty() ||
                         std::find(x.aux[i].facts.begin(), x.aux[i].facts.end(), "kind=quote") != x.aux[i].facts.end());
            if (IsContentPos(t.pos)) k.entry = FindEntry(d, doc, line, t);
        }
        k.epos = t.pos;
        if (k.entry) {
            unsigned v = 0;
            if (Parse(F::pos, k.entry->pos, v)) k.epos = static_cast<Pos>(v);
        }
        // utterance: the innermost one around the unit
        int depth = -1;
        for (size_t u = 0; u < line.utts.size(); u++) {
            const Utterance& ut = line.utts[u];
            if (!Contains(ut.span, k.src) || static_cast<int>(ut.depth) < depth) continue;
            depth = ut.depth;
            k.utt = static_cast<uint16_t>(u);
            k.dialogue = ut.depth > 0 || !ut.narration;
        }
        for (size_t j = i; j <= last; j++) x.tkOf[j] = static_cast<int>(x.tk.size());
        x.tk.push_back(std::move(k));
        i = last + 1;
    }
    const size_t n = x.tk.size();
    for (size_t b = 0; b < line.buns.size(); b++) {
        const int c = x.ClauseOfBun(static_cast<int>(b));
        for (int i = line.buns[b].first; i <= line.buns[b].last && i < static_cast<int>(nt); i++) {
            Tk& k = x.tk[static_cast<size_t>(x.tkOf[static_cast<size_t>(i)])];
            if (k.bun < 0) {
                k.bun = static_cast<int>(b);
                k.clause = c;
            }
        }
    }

    // tokens the analyzer read by a derived spoken arc, as unknown or by a fallback: the VN speech stage reads them,
    // so the line stays with 0.7.x for now
    for (size_t i = 0; i < nt; i++)
        if (line.toks[i].pos != Pos::sym &&
            (line.toks[i].flags & (tokflag::Unknown | tokflag::Fallback | tokflag::Spoken | tokflag::Elongated)))
            line.unmatched.push_back(doc.I(std::string(line.Src(line.toks[i].src)) + "/spoken"));
    // A stutter (そ、それ), a prolonged mark inside a word that is not a noun (いただきまーす), or a quote opened or
    // closed on another line: the VN speech stage and S read these, so the line stays with 0.7.x for now.
    {
        int open = 0, close = 0;
        for (size_t i = 0; i < n; i++) {
            const Tk& k = x.tk[i];
            const auto& f = k.aux->facts;
            if (std::find(f.begin(), f.end(), "role=open") != f.end()) open++;
            if (std::find(f.begin(), f.end(), "role=close") != f.end()) close++;
            if (i + 2 < n && std::find(x.tk[i + 1].aux->facts.begin(), x.tk[i + 1].aux->facts.end(), "role=comma") !=
                                 x.tk[i + 1].aux->facts.end() &&
                !k.surf.empty() && k.surf.size() < x.tk[i + 2].surf.size() && x.tk[i + 2].surf.starts_with(k.surf))
                line.unmatched.push_back(doc.I(k.surf + "/stutter"));
            if (k.t->pos != Pos::sym && !IsNounPos(k.t->pos) && k.surf.find("\xE3\x83\xBC") != std::string::npos)  // ー
                line.unmatched.push_back(doc.I(k.surf + "/prolonged"));
        }
        if (open != close) line.unmatched.push_back(doc.I("quote/unbalanced"));
    }
    for (size_t i = 0; i < n; i++) {
        Tk& k = x.tk[i];
        if (!k.entry) continue;
        k.sense = x.Choose(i, k.senseWhy);
        // a noun whose chosen sense is a predicate (便利 → 편리하다 by form=cop) is read as a な-adjective
        if (k.sense && IsNounPos(k.epos) && !d.predicateEnd.empty() && k.sense->ko.ends_with(d.predicateEnd) &&
            (k.senseWhy.find("form=") != std::string::npos || x.Copular(i)))
            k.epos = Pos::na;
    }

    // Candidates: every match of every row whose conditions hold.
    std::vector<Cand> cands;
    for (size_t ri = 0; ri < d.rows.size(); ri++) {
        const Row& r = d.rows[ri];
        if (r.then == "supp") continue;
        std::vector<Cand> found;
        if (!r.phrase.empty()) {
            // the phrase's text: the units' spellings, the last one also in its dictionary form (連絡を取った)
            for (int st = 0; st < static_cast<int>(n); st++) {
                std::string spell;
                for (int e = st; e < static_cast<int>(n) && spell.size() < r.phrase.size(); e++) {
                    const Tk& k = x.tk[static_cast<size_t>(e)];
                    std::string dict = k.lemma;
                    if (k.entry) {
                        auto o = k.entry->feat.find("orth");
                        if (o != k.entry->feat.end()) dict = o->second;
                    }
                    const bool hit = spell + k.surf == r.phrase || ((IsVerbPos(k.epos) || k.epos == Pos::adj) && spell + dict == r.phrase);
                    spell += k.surf;
                    if (!hit) continue;
                    Cand c;
                    c.row = ri;
                    c.begin = st;
                    c.end = e + 1;
                    for (int t = st; t <= e; t++) c.consumed.push_back(t);
                    c.anchor = st;
                    c.clause = x.tk[static_cast<size_t>(e)].clause;
                    found.push_back(std::move(c));
                    break;
                }
            }
        } else if (r.atoms.empty()) {
            // a row without atoms reads one clause: its anchor is the clause's predicate word
            for (size_t c = 0; c < line.clauses.size(); c++) {
                Cand cd;
                cd.row = ri;
                cd.anchor = x.PredTok(static_cast<int>(c));
                if (cd.anchor < 0) continue;
                cd.begin = cd.end = cd.anchor;
                cd.clause = static_cast<int>(c);
                found.push_back(cd);
            }
        } else {
            for (int s = 0; s < static_cast<int>(n); s++) {
                Cand c0;
                c0.row = ri;
                c0.begin = s;
                std::vector<Cand> here;
                x.MatchFrom(r, 0, s, c0, here);
                for (Cand& c : here) {
                    if (c.end == c.begin) continue;
                    // the anchor: the first slot, else the first consumed token
                    c.anchor = c.consumed.empty() ? c.begin : c.consumed.front();
                    if (!c.slots.empty()) {
                        c.anchor = c.end;
                        for (const auto& [name, tok] : c.slots) c.anchor = std::min(c.anchor, tok);
                    }
                    c.clause = x.tk[static_cast<size_t>(c.anchor)].clause;
                    found.push_back(std::move(c));
                }
            }
        }
        for (Cand& c : found) {
            bool ok = true;
            for (const Cond& cd : r.conds) {
                std::string fact;
                if (!x.CondOk(cd, c, fact)) {
                    ok = false;
                    break;
                }
                c.because.push_back(fact);
            }
            if (ok) cands.push_back(std::move(c));
        }
    }
    std::stable_sort(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
        const Row& ra = d.rows[a.row];
        const Row& rb = d.rows[b.row];
        if (ra.specificity != rb.specificity) return SpecGreater(ra.specificity, rb.specificity);
        if (a.end - a.begin != b.end - b.begin) return a.end - a.begin > b.end - b.begin;
        if (a.begin != b.begin) return a.begin < b.begin;
        if (ra.order != rb.order) return ra.order < rb.order;
        if (a.consumed != b.consumed) return a.consumed < b.consumed;
        return a.slotToks < b.slotToks;
    });
    // A token is consumed by one accepted candidate, or read as a slot by any number of them, not both.
    std::vector<int> owner(n, -1);   // accepted candidate consuming the token
    std::vector<int> reader(n, -1);  // first accepted candidate reading the token as a slot
    std::vector<size_t> accepted;
    std::vector<std::vector<size_t>> lost;  // per accepted: candidates it beat on a token
    for (size_t ci = 0; ci < cands.size(); ci++) {
        const Cand& c = cands[ci];
        int clash = -1;
        for (int t : c.consumed) {
            if (owner[static_cast<size_t>(t)] >= 0) clash = owner[static_cast<size_t>(t)];
            else if (reader[static_cast<size_t>(t)] >= 0) clash = reader[static_cast<size_t>(t)];
        }
        // a nominaliser read as {NP} is a function word its own row consumes: no clash
        for (int t : c.slotToks)
            if (x.tk[static_cast<size_t>(t)].content && owner[static_cast<size_t>(t)] >= 0) clash = owner[static_cast<size_t>(t)];
        if (clash >= 0) {
            lost[static_cast<size_t>(clash)].push_back(ci);
            continue;
        }
        for (int t : c.consumed) owner[static_cast<size_t>(t)] = static_cast<int>(accepted.size());
        for (int t : c.slotToks)
            if (x.tk[static_cast<size_t>(t)].content && reader[static_cast<size_t>(t)] < 0)
                reader[static_cast<size_t>(t)] = static_cast<int>(accepted.size());
        accepted.push_back(ci);
        lost.emplace_back();
    }

    // The inflected form of a content word is a morpheme too: an imperative, volitional or fused conditional form,
    // or a stem form not followed by its ending, needs an accepted row that reads it (a condition on the slot's
    // form, or a consumed atom right after the slot).
    std::vector<bool> formRead(n, false);
    for (size_t a = 0; a < accepted.size(); a++) {
        const Cand& c = cands[accepted[a]];
        const Row& r = d.rows[c.row];
        for (const auto& [name, tok] : c.slots)
            for (const Cond& cd : r.conds)
                if (cd.target == name && cd.key == "form") formRead[static_cast<size_t>(tok)] = true;
        for (int tok : c.slotToks)
            if (std::find(c.consumed.begin(), c.consumed.end(), tok + 1) != c.consumed.end())
                formRead[static_cast<size_t>(tok)] = true;
    }
    for (size_t i = 0; i < n; i++) {
        const Tk& k = x.tk[i];
        // a 一段 form of a 五段 lemma is its potential (なれる of 成る): voice is N4, so the line stays with 0.7.x
        if (k.content && owner[i] < 0 && k.entry && k.entry->pos == "v5" && k.t->pos == Pos::v1)
            line.unmatched.push_back(doc.I(k.surf + "/potential"));
        if (!k.content || owner[i] >= 0 || formRead[i]) continue;
        static const std::set<std::string> always{"meirei", "ishi", "katei-fused", "izen"};
        static const std::set<std::string> stem{"renyo", "mizen", "gokan", "katei"};
        bool needs = always.count(k.form) > 0;
        if (!needs && stem.count(k.form)) {
            const Tk* nx = i + 1 < n ? &x.tk[i + 1] : nullptr;
            const bool ending = nx && !nx->content && nx->t->pos != Pos::sym && nx->t->pos != Pos::p_case &&
                                nx->t->pos != Pos::p_topic;
            needs = !ending;
        }
        if (needs) line.unmatched.push_back(doc.I(k.surf + "/form-" + k.form));
    }

    // Nodes: lexicon words in token order, row nodes at their first consumed token.
    std::vector<Placed> placed;
    std::vector<int> wordNode(n, -1);  // token → index in placed of its lexicon node
    for (size_t i = 0; i < n; i++) {
        Tk& k = x.tk[i];
        if (owner[i] >= 0) continue;
        if (k.number >= 0) {
            // N5-55, N5-56: a numeral with its counter; native numerals before native counters (세 개, 한 명),
            // Sino-Korean otherwise (백 엔); digits stay digits and the counter attaches (3명, 100엔); つ with 1 alone
            // is 하나
            // the counter's sense: a head= cue reads the counted noun (the nearest noun before the numeral)
            const Sense* cs = nullptr;
            if (k.counter) {
                std::string counted;
                for (int b = static_cast<int>(i) - 1; b >= 0 && b >= static_cast<int>(i) - 3; b--)
                    if (x.tk[static_cast<size_t>(b)].content && IsNounPos(x.tk[static_cast<size_t>(b)].epos)) {
                        const Tk& cn = x.tk[static_cast<size_t>(b)];
                        counted = cn.entry ? cn.entry->ja : cn.lemma;
                        break;
                    }
                for (const Sense& sn : k.counter->senses) {
                    if (sn.cue.starts_with("head=")) {
                        bool hit = false;
                        for (const std::string& v : Split(sn.cue.substr(5), '|')) {
                            if (v == counted) hit = true;
                            if (v.starts_with("@")) {
                                std::string cl;
                                for (size_t ei : d.byLemma.count(counted) ? d.byLemma.at(counted) : std::vector<size_t>{})
                                    for (const Sense& s2 : d.entries[ei].senses)
                                        if (s2.sense == 1) cl += " " + Ctx::Get(s2.feat, "cls");
                                std::replace(cl.begin(), cl.end(), '|', ' ');
                                for (const std::string& one : Split(cl, ' '))
                                    if (!one.empty() && d.IsA(one, v)) hit = true;
                            }
                        }
                        if (hit) cs = &sn;
                    }
                }
                if (!cs)
                    for (const Sense& sn : k.counter->senses)
                        if (!cs || sn.sense < cs->sense) cs = &sn;
            }
            std::string numFeat = cs ? Ctx::Get(cs->feat, "num") : "";
            if (numFeat.empty() && k.counter) numFeat = Ctx::Get(k.counter->feat, "num");
            const bool native = numFeat == "native";
            const auto alone = k.counter ? d.alone.find(k.counter->ja) : d.alone.end();
            Decision dec;
            dec.stage = 'T';
            dec.kind = Ev::Cued;
            dec.rule = doc.I(k.counter ? (native ? "xfer:num.native" : "xfer:num.sino") : "xfer:num.bare");
            dec.spec = doc.I(k.counter ? "N5-56" : "N5-55");
            dec.src = k.src;
            uint32_t did = 0;
            auto push = [&](KK kind, const std::string& lemma, bool glue, int sub) {
                if (!did) did = line.Decide(dec);
                Placed p;
                p.tok = p.ptok = static_cast<int>(i);
                p.sub = sub;
                p.n.kind = kind;
                p.n.lemma = doc.I(lemma);
                p.n.glue = glue;
                p.n.sem = sem::num;
                p.n.src = k.src;
                p.n.decision = did;
                placed.push_back(std::move(p));
            };
            const size_t first = placed.size();
            const std::string digitText = k.digits ? std::string(line.Src({k.t->src.begin, line.toks[static_cast<size_t>(k.counter ? k.last - 1 : k.last)].src.end})) : "";
            if (alone != d.alone.end() && k.number == alone->second && !k.digits) {
                push(KK::N, Numeral(d, 1, true, false), false, 0);
            } else if (k.digits) {
                push(KK::Raw, digitText, false, 0);
                if (cs) push(KK::N, cs->ko, true, 1);
            } else {
                const std::string word = Numeral(d, k.number, native, cs != nullptr);
                if (word.empty()) {
                    line.unmatched.push_back(doc.I(k.surf + "/number"));
                    continue;
                }
                push(KK::N, word, false, 0);
                if (cs) push(KK::N, cs->ko, false, 1);
            }
            wordNode[i] = static_cast<int>(first);
            continue;
        }
        if (!k.content || !k.sense) {
            // not consumed: the source text stays as a Raw node, so --why and the native-only output show it
            const bool word = k.content;
            line.unmatched.push_back(doc.I(k.surf + (word ? "/word" : "/form")));
            Decision dec;
            dec.stage = 'T';
            dec.kind = word ? Ev::Fallback : Ev::UnmatchedForm;
            dec.rule = doc.I(word ? "code:native-no-lexicon" : "code:native-unmatched");
            dec.src = k.src;
            if (!k.aux->row.empty()) dec.because.push_back(doc.I("aux=" + k.aux->row));
            Placed p;
            p.tok = p.ptok = static_cast<int>(i);
            p.n.kind = KK::Raw;
            p.n.lemma = doc.I(k.surf);
            p.n.src = k.src;
            p.n.decision = line.Decide(std::move(dec));
            placed.push_back(std::move(p));
            continue;
        }
        Decision dec;
        dec.stage = 'T';
        int alts = 0;
        for (const Sense& s : k.entry->senses) alts += s.cue.empty() ? 1 : 0;
        dec.kind = alts > 1 ? Ev::Default : Ev::Lexical;
        dec.rule = doc.I("lex:" + k.entry->ja + "/" + k.entry->yomi + "/" + k.entry->pos + "#" + std::to_string(k.sense->sense));
        if (!k.senseWhy.empty()) {
            dec.kind = Ev::Cued;
            dec.because.push_back(doc.I("cue=" + k.senseWhy));
        }
        dec.src = k.src;
        dec.nAlt = static_cast<uint8_t>(std::min<size_t>(k.entry->senses.size(), 255));
        unsigned stv = 0;
        if (!k.sense->st.empty() && Parse(F::st, k.sense->st, stv)) dec.st = static_cast<uint8_t>(stv);
        Placed p;
        p.tok = p.ptok = static_cast<int>(i);
        KNode& nd = p.n;
        const Pos pos = k.epos;
        nd.kind = IsNounPos(pos) || pos == Pos::suf || pos == Pos::pref ? KK::N
                  : pos == Pos::adv || pos == Pos::conj                   ? KK::Adv
                  : pos == Pos::adn                                       ? KK::Det
                  : pos == Pos::intj                                      ? KK::Intj
                                                                          : KK::V;
        nd.lemma = doc.I(k.sense->ko);
        // a sense chosen by form=adv is an adverb (早く → 빨리)
        if (nd.kind == KK::V && k.senseWhy.find("form=adv") != std::string::npos) nd.kind = KK::Adv;
        if (pos == Pos::adj) nd.vpos = vpos::A;
        if (x.TokKey(static_cast<int>(i), "kcls") == "EXIST") nd.vpos = vpos::X_;  // 재미있다: adnominal -는
        if (const std::string kc = x.TokKey(static_cast<int>(i), "kcls"); !kc.empty()) SetField(nd, doc, "kcls", kc);
        if (const std::string vp = x.TokKey(static_cast<int>(i), "vpos"); !vp.empty()) SetField(nd, doc, "vpos", vp);
        if (pos == Pos::suf) nd.glue = true;
        nd.sem = SemOf(d, x.TokKey(static_cast<int>(i), "cls"));
        nd.src = k.src;
        nd.decision = line.Decide(std::move(dec));
        wordNode[i] = static_cast<int>(placed.size());
        placed.push_back(std::move(p));
    }
    std::vector<uint32_t> candDecision(accepted.size());
    std::vector<std::vector<int>> candNodes(accepted.size());
    for (size_t a = 0; a < accepted.size(); a++) {
        const Cand& c = cands[accepted[a]];
        const Row& r = d.rows[c.row];
        Decision dec;
        dec.stage = 'T';
        dec.kind = Ev::Cued;
        dec.rule = doc.I("xfer:" + r.id);
        dec.spec = doc.I(r.spec);
        Span src{UINT32_MAX, 0};
        for (int t : c.consumed) {
            src.begin = std::min(src.begin, x.tk[static_cast<size_t>(t)].src.begin);
            src.end = std::max(src.end, x.tk[static_cast<size_t>(t)].src.end);
        }
        if (src.begin > src.end) src = x.tk[static_cast<size_t>(c.anchor)].src;
        dec.src = src;
        for (const std::string& b : c.because) dec.because.push_back(doc.I(b));
        const auto& beaten = lost[a];
        dec.nAlt = static_cast<uint8_t>(std::min<size_t>(beaten.size() + 1, 255));
        if (!beaten.empty()) {
            const Row& ru = d.rows[cands[beaten.front()].row];
            dec.runnerUp = doc.I("xfer:" + ru.id);
            if (ru.specificity == r.specificity) dec.kind = Ev::Default;
            for (size_t k = 0; k < r.specificity.size(); k++)
                if (ru.specificity[k] != r.specificity[k]) {
                    dec.margin = static_cast<float>(r.specificity[k] - ru.specificity[k]);
                    break;
                }
            float code = 0;
            for (int v : r.specificity) code = code * 10 + static_cast<float>(v);
            dec.contrib.emplace_back(doc.I("specificity"), code);
        }
        candDecision[a] = line.Decide(std::move(dec));
        const int at = c.consumed.empty() ? c.anchor : c.consumed.front();
        int sub = 1;
        for (const KoNode& kn : r.nodes) {
            Placed p;
            p.tok = at;
            p.sub = static_cast<int>(a) * 64 + sub++;
            // a row's nodes belong to the bunsetsu of its last consumed token (じゃ + あり: the あり clause)
            p.ptok = c.consumed.empty() ? at : c.consumed.back();
            KNode& nd = p.n;
            unsigned kv = 0;
            Parse(F::kk, kn.kind, kv);
            nd.kind = static_cast<KK>(kv);
            if (nd.kind == KK::P) {
                if (kn.lemma.starts_with("=")) {
                    nd.kase = kase::LEX;
                    nd.lemma = doc.I(kn.lemma.substr(1));
                } else {
                    SetField(nd, doc, "kase", kn.lemma);
                }
            } else if (nd.kind == KK::Punct) {
                SetField(nd, doc, "punct", kn.lemma);
                nd.jaMark = doc.I(x.tk[static_cast<size_t>(at)].surf);
            } else {
                nd.lemma = doc.I(kn.lemma);
            }
            for (const auto& [f, v] : kn.fields) SetField(nd, doc, f, v);
            nd.src = dec.src;
            nd.decision = candDecision[a];
            p.cand = static_cast<int>(a);
            candNodes[a].push_back(static_cast<int>(placed.size()));
            placed.push_back(std::move(p));
        }
    }
    // Order: by token, lexicon word first, then row nodes in acceptance order.
    std::vector<size_t> order(placed.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return placed[a].tok != placed[b].tok ? placed[a].tok < placed[b].tok : placed[a].sub < placed[b].sub;
    });
    std::vector<int> rank(placed.size());
    for (size_t i = 0; i < order.size(); i++) rank[order[i]] = static_cast<int>(i);

    // The predicate node of a clause: the last V node placed within its predicate bunsetsu, else the last V
    // node before the bunsetsu ends.
    auto predNode = [&](int clause, int limit) -> int {
        if (clause < 0 || clause >= static_cast<int>(line.clauses.size())) return -1;
        const Clause& cl = line.clauses[static_cast<size_t>(clause)];
        if (cl.pred >= line.buns.size()) return -1;
        const Bunsetsu& bo = line.buns[cl.pred];
        struct { int first, last; } b{x.tkOf[bo.first], x.tkOf[bo.last]};
        const int clauseFirst = cl.firstBun < line.buns.size() ? x.tkOf[line.buns[cl.firstBun].first] : b.first;
        int best = -1, before = -1;
        if (limit != std::numeric_limits<int>::max()) {
            // a row that consumes a morpheme marks the predicate right before it: the last predicate node placed
            // before the morpheme, in its bunsetsu or the one before (読まない|だろう, 食べる|前に)
            const int bun = x.tk[static_cast<size_t>(limit)].bun;
            const int from = bun > 0 ? x.tkOf[line.buns[static_cast<size_t>(bun - 1)].first]
                                     : bun == 0 ? x.tkOf[line.buns[0].first] : limit;
            for (size_t oi = 0; oi < order.size(); oi++) {
                const Placed& p = placed[order[oi]];
                if (p.n.kind == KK::V && p.ptok < limit && p.ptok >= from) best = static_cast<int>(order[oi]);
            }
            return best;
        }
        for (size_t oi = 0; oi < order.size(); oi++) {
            const Placed& p = placed[order[oi]];
            if (p.n.kind != KK::V) continue;
            if (p.ptok >= b.first && p.ptok <= b.last) best = static_cast<int>(order[oi]);
            if (p.ptok >= clauseFirst && p.ptok <= b.last) before = static_cast<int>(order[oi]);
        }
        return best >= 0 ? best : before;
    };

    // Assignments: per node field, the first (most specific) accepted row wins; a field a row's node template sets
    // is that row's.
    std::set<std::pair<int, std::string>> done;
    for (size_t a = 0; a < accepted.size(); a++) {
        const Row& r = d.rows[cands[accepted[a]].row];
        for (size_t k = 0; k < r.nodes.size() && k < candNodes[a].size(); k++)
            for (const auto& [f, v] : r.nodes[k].fields) done.insert({candNodes[a][k], f});
    }
    for (size_t a = 0; a < accepted.size(); a++) {
        const Cand& c = cands[accepted[a]];
        const Row& r = d.rows[c.row];
        for (const Assign& s : r.sets) {
            int node = -1;
            if (s.target == "line") {
                // a hold row: a shape the native rules do not translate yet goes to 0.7.x
                if (s.field == "route" && s.value == "fallback") line.unmatched.push_back(doc.I(r.id + "/held"));
                continue;
            }
            if (s.target == "pred") {
                node = predNode(c.clause, c.consumed.empty() ? std::numeric_limits<int>::max() : c.consumed.front());
            } else if (s.target == "ko") {
                node = candNodes[a].empty() ? -1 : candNodes[a].front();
            } else {
                auto it = c.slots.find(s.target);
                if (it != c.slots.end()) node = wordNode[static_cast<size_t>(it->second)];
            }
            // a clause row whose predicate word another row consumed (そう + いう → 그런): that row speaks for it
            if (node < 0 && r.atoms.empty() && c.anchor >= 0 && owner[static_cast<size_t>(c.anchor)] >= 0) continue;
            if (node < 0) {
                // the feature has no node to land on: it would be lost, so the line is not covered
                line.decisions[candDecision[a] - 1].because.push_back(doc.I("unset=" + s.target + "." + s.field));
                line.unmatched.push_back(doc.I(r.id + "/unset"));
                continue;
            }
            if (!done.insert({node, s.field}).second) continue;
            if (!SetField(placed[static_cast<size_t>(node)].n, doc, s.field, s.value))
                line.decisions[candDecision[a] - 1].because.push_back(doc.I("bad=" + s.field + "=" + s.value));
        }
    }
    // A predicate whose Korean is two words (안 되다) cannot take a negation of its own: the line stays with 0.7.x.
    for (const Placed& p : placed)
        if (p.n.kind == KK::V && p.n.neg != kneg::none && doc.S(p.n.lemma).find(' ') != std::string_view::npos)
            line.unmatched.push_back(doc.I(std::string(doc.S(p.n.lemma)) + "/neg"));
    // Suppletive negatives (supp rows): a negated predicate whose Korean lemma has its own negative word.
    for (Placed& p : placed) {
        if (p.n.kind != KK::V || p.n.neg == kneg::none) continue;
        const auto sp = d.supp.find(std::string(doc.S(p.n.lemma)));
        if (sp == d.supp.end()) continue;
        {
            const Row& r = d.rows[sp->second];
            Decision dec;
            dec.stage = 'T';
            dec.kind = Ev::Cued;
            dec.rule = doc.I("xfer:" + r.id);
            dec.spec = doc.I(r.spec);
            dec.src = p.n.src;
            dec.because.push_back(doc.I("neg=" + std::string(Name(F::kneg, p.n.neg))));
            p.n.lemma = doc.I(r.ko);
            p.n.neg = kneg::none;
            p.n.kcls = 0;  // the class was the positive lemma's (알다 ㄹ, 모르다 르)
            line.Decide(std::move(dec));
        }
    }
    // A word right after an ellipsis or a dash keeps the source's spacing: no space in the source, none in Korean.
    for (size_t oi = 1; oi < order.size(); oi++) {
        const KNode& prev = placed[order[oi - 1]].n;
        KNode& cur = placed[order[oi]].n;
        if (prev.kind != KK::Punct || (prev.punct != punct::ellipsis && prev.punct != punct::dash)) continue;
        if (cur.kind == KK::Punct || cur.kind == KK::P) continue;
        if (prev.src.end == cur.src.begin) cur.glue = true;
    }
    for (size_t oi = 0; oi < order.size(); oi++) {
        Placed& p = placed[order[oi]];
        KNode nd = p.n;
        // N5-H1: a short-negated noun + 하다 verb puts 안 before 하다 (話さない → 이야기 안 해): the noun is a node
        // (a verb: 話す → 이야기하다, or a phrase verb: 顔を洗う → 세수하다; never an adjective: 안 편리해)
        if (nd.kind == KK::V && nd.neg == kneg::an && !d.lightVerb.empty() &&
            IsVerbPos(x.tk[static_cast<size_t>(p.ptok)].epos)) {
            const std::string lemma(doc.S(nd.lemma));
            const std::string stem = lemma.size() > d.lightVerb.size() ? lemma.substr(0, lemma.size() - d.lightVerb.size()) : "";
            const bool phraseVerb = p.cand >= 0 && !d.rows[cands[accepted[static_cast<size_t>(p.cand)]].row].phrase.empty();
            if (!stem.empty() && lemma.ends_with(d.lightVerb) && (d.koNouns.count(stem) || (phraseVerb && stem.size() >= 6))) {
                KNode noun;
                noun.kind = KK::N;
                noun.lemma = doc.I(lemma.substr(0, lemma.size() - d.lightVerb.size()));
                noun.src = nd.src;
                noun.decision = nd.decision;
                noun.glue = nd.glue;
                noun.id = static_cast<uint32_t>(line.kir.size() + 1);
                const Tk& k0 = x.tk[static_cast<size_t>(p.ptok)];
                noun.utt = k0.utt;
                noun.clause = static_cast<uint16_t>(k0.clause < 0 ? 0 : k0.clause);
                line.kir.push_back(noun);
                nd.lemma = doc.I(d.lightVerb);
                nd.kcls = 0;
                nd.glue = true;
            }
        }
        // a repeated interjection right after itself is written as one word (はいはい → 네네)
        if (nd.kind == KK::Intj && !line.kir.empty() && line.kir.back().kind == KK::Intj &&
            line.kir.back().lemma == nd.lemma && line.kir.back().src.end == nd.src.begin)
            nd.glue = true;
        nd.id = static_cast<uint32_t>(line.kir.size() + 1);
        const Tk& k = x.tk[static_cast<size_t>(p.ptok)];
        nd.utt = k.utt;
        nd.clause = static_cast<uint16_t>(k.clause < 0 ? 0 : k.clause);
        line.kir.push_back(nd);
    }
    line.route = line.unmatched.empty() && n > 0 ? 1 : 2;
}

}  // namespace hd08::nat
