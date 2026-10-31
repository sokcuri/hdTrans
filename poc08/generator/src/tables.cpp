#include "tables.h"

#include <fstream>
#include <sstream>

#include "hangul.h"

namespace hd8gen {

int TsvTable::Col(const std::string& name) const {
    for (size_t i = 0; i < header.size(); ++i)
        if (header[i] == name) return static_cast<int>(i);
    return -1;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t p = s.find(sep, start);
        if (p == std::string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
}

bool ReadTsv(const std::string& path, TsvTable& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols = Split(line, '\t');
        if (out.header.empty()) {
            out.header = cols;
            continue;
        }
        cols.resize(out.header.size() > cols.size() ? out.header.size() : cols.size());
        out.rows.push_back(std::move(cols));
        out.lines.push_back(n);
    }
    if (out.header.empty()) {
        err = path + ": no header";
        return false;
    }
    return true;
}

namespace {

bool Need(const TsvTable& t, const std::vector<std::string>& names, std::vector<int>& cols, const std::string& path,
          std::string& err) {
    cols.clear();
    for (const auto& n : names) {
        const int c = t.Col(n);
        if (c < 0) {
            err = path + ": missing column " + n;
            return false;
        }
        cols.push_back(c);
    }
    return true;
}

int MedialOf(const std::string& s) {
    const std::u32string u = hangul::ToU32(s);
    return u.size() == 1 ? hangul::MedialOfJamo(u[0]) : -1;
}

}  // namespace

std::shared_ptr<const Tables> LoadTables(const std::string& dir, std::string& err) {
    auto t = std::make_shared<Tables>();
    std::vector<int> c;
    {
        const std::string p = dir + "/eomi.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"id", "slot", "family", "mood", "level", "pos", "form", "flags"}, c, p, err))
            return nullptr;
        for (const auto& r : tsv.rows) {
            EomiRow e{r[c[0]], r[c[1]], r[c[2]], r[c[3]], r[c[4]], r[c[5]], r[c[6]]};
            e.honIncl = r[c[7]].find("hon-incl") != std::string::npos;
            e.words = r[c[7]].find("words") != std::string::npos;
            t->eomiById.emplace(e.id, t->eomi.size());
            for (const auto& fam : Split(e.family, ','))
                t->eomiBySlotFamily.emplace(e.slot + "|" + fam, t->eomi.size());
            t->eomi.push_back(std::move(e));
        }
    }
    {
        const std::string p = dir + "/irregular.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"class", "before", "stem", "ending"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) {
            IrrRow row{r[c[0]], r[c[1]], {}, {}};
            if (r[c[2]] != "-") row.stem = Split(r[c[2]], ',');
            if (r[c[3]] != "-") row.ending = Split(r[c[3]], ',');
            t->irregular.push_back(std::move(row));
        }
    }
    {
        const std::string p = dir + "/contract.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"stem", "ending", "result", "mode", "cond"}, c, p, err)) return nullptr;
        for (size_t i = 0; i < tsv.rows.size(); ++i) {
            const auto& r = tsv.rows[i];
            ContractRow row;
            const std::u32string s = hangul::ToU32(r[c[0]]);
            if (s.size() == 1 && hangul::IsSyllable(s[0]))
                row.stemSyllable = s[0];
            else
                row.stemVowel = MedialOf(r[c[0]]);
            row.endVowel = MedialOf(r[c[1]]);
            row.result = r[c[2]] == "-" ? -1 : MedialOf(r[c[2]]);
            row.mode = r[c[3]];
            for (const auto& cond : Split(r[c[4]], ',')) {
                if (cond == "init=ㅇ") row.initIeung = true;
                else if (cond == "next=yo") row.nextYo = true;
                else if (cond == "prefinal") row.prefinal = true;
                else if (cond != "-") {
                    err = p + ":" + std::to_string(tsv.lines[i]) + ": unknown cond " + cond;
                    return nullptr;
                }
            }
            if ((row.stemSyllable == 0 && row.stemVowel < 0) || row.endVowel < 0) {
                err = p + ":" + std::to_string(tsv.lines[i]) + ": bad vowel";
                return nullptr;
            }
            t->contract.push_back(row);
        }
    }
    {
        const std::string p = dir + "/particles.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"id", "kase", "when", "form"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) {
            ParticleRow row{r[c[0]], r[c[1]], r[c[3]], {}};
            if (r[c[2]] != "-") row.when = Split(r[c[2]], ',');
            t->particles.push_back(std::move(row));
        }
    }
    {
        const std::string p = dir + "/stems.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"lemma", "pos", "class", "flags"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) {
            StemRow row{r[c[0]], r[c[1]], r[c[2]]};
            row.ae = r[c[3]].find("ae") != std::string::npos;
            row.whole = r[c[3]].find("whole") != std::string::npos;
            row.honSi = r[c[3]].find("hon-si") != std::string::npos;
            if (row.lemma.size() > t->maxStemLen) t->maxStemLen = row.lemma.size();
            t->stems[row.lemma] = row;
        }
    }
    {
        const std::string p = dir + "/lemmas.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"lemma", "pos"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) t->lemmaPos[r[c[0]]] = r[c[1]];
    }
    {
        const std::string p = dir + "/classrules.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"match", "pos", "class"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) {
            ClassRule rule;
            const size_t eq = r[c[0]].find('=');
            rule.kind = eq == std::string::npos ? r[c[0]] : r[c[0]].substr(0, eq);
            rule.value = eq == std::string::npos ? "" : r[c[0]].substr(eq + 1);
            rule.pos = r[c[1]];
            rule.cls = r[c[2]];
            t->classRules.push_back(rule);
        }
    }
    {
        const std::string p = dir + "/words.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"lemma", "kind", "attach"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) {
            // a lemma may be both a dependent noun and an auxiliary (V); keep both under kind-qualified keys
            WordRow w{r[c[0]], r[c[1]], r[c[2]]};
            t->words[(w.kind == "aux" ? "V:" : "N:") + w.lemma] = w;
        }
    }
    {
        // hand-curated classes of Korean lemmas, read by rule conditions (x.class=state)
        const std::string p = dir + "/classes.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"class", "lemma"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) {
            t->classNames.insert(r[c[0]]);
            if (!r[c[1]].empty() && r[c[1]][0] == '~') {
                t->suffixClasses.push_back({r[c[1]].substr(1), r[c[0]]});
                continue;
            }
            std::string& v = t->lemmaClasses[r[c[1]]];
            v += (v.empty() ? "" : "|") + r[c[0]];
        }
    }
    {
        const std::string p = dir + "/spoken.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"host", "next", "result", "mode"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) t->spoken.push_back({r[c[0]], r[c[1]], r[c[2]], r[c[3]]});
    }
    {
        const std::string p = dir + "/punct.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"kind", "jaMark", "ko", "attach"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) t->punct.push_back({r[c[0]], r[c[1]], r[c[2]], r[c[3]]});
    }
    {
        const std::string p = dir + "/persona.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"persona", "level", "mood", "family"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) t->persona.push_back({r[c[0]], r[c[1]], r[c[2]], r[c[3]]});
    }
    {
        const std::string p = dir + "/grammar.tsv";
        TsvTable tsv;
        if (!ReadTsv(p, tsv, err) || !Need(tsv, {"name", "value"}, c, p, err)) return nullptr;
        for (const auto& r : tsv.rows) t->grammar[r[c[0]]] = r[c[1]];
        for (const char* need : {"aux.prog", "conn.prog", "aux.result", "aux.compl", "aux.dir-come", "aux.dir-go", "aux.try",
                                 "aux.give", "conn.aux", "aux.prog.hon", "neg.an", "neg.mot", "aux.neg", "aux.neg-mot", "conn.neg",
                                 "family.jima", "nom.geot", "conn.default", "final.fallback", "pre.hon", "pre.past", "pre.gess",
                                 "lemma.cop", "lemma.negcop", "pot.noun", "pot.exist", "pot.neg", "voice.aux", "voice.ha",
                                 "voice.doe", "caus.ha", "caus.aux", "conn.caus"})
            if (!t->grammar.count(need)) {
                err = p + ": missing " + need;
                return nullptr;
            }
    }
    return t;
}

std::string Tables::ClassesOf(const std::string& lemma) const {
    std::string v;
    if (auto k = lemmaClasses.find(lemma); k != lemmaClasses.end()) v = k->second;
    for (const auto& [end, cls] : suffixClasses)
        if (lemma.size() > end.size() && lemma.compare(lemma.size() - end.size(), end.size(), end) == 0)
            v += (v.empty() ? "" : "|") + cls;
    return v;
}

const std::string& Tables::G(const std::string& name) const {
    static const std::string empty;
    auto it = grammar.find(name);
    return it == grammar.end() ? empty : it->second;
}

}  // namespace hd8gen
