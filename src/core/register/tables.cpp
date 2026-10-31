#include "core/register/tables.h"

#include <algorithm>
#include <memory>
#include <span>

#include "core/dict/builtin.h"
#include "core/register/hangul.h"

namespace hdTrans::dict::internal {

// Generated from src/core/register/data/ (cmake/builtin_tables.cmake).
std::span<const BuiltinTable> RegisterReaderTables();
std::span<const BuiltinTable> RegisterKoTables();
std::span<const BuiltinTable> RegisterLevelTables();
std::span<const BuiltinTable> RegisterRetroTables();

}  // namespace hdTrans::dict::internal

namespace hdTrans::reg {

using hangul::ToU32;
using hangul::ToU8;

std::vector<std::string> Split(std::string_view s, char sep) {
    std::vector<std::string> out;
    for (std::size_t start = 0;;) {
        const std::size_t p = s.find(sep, start);
        if (p == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            return out;
        }
        out.emplace_back(s.substr(start, p - start));
        start = p + 1;
    }
}

const std::string& Tables::G(std::string_view name) const {
    static const std::string empty;
    const auto it = grammar.find(std::string(name));
    return it == grammar.end() ? empty : it->second;
}

namespace {

using dict::internal::BuiltinTable;

// The lines of a compiled-in file without their line ends; empty when there is none.
std::vector<std::string_view> FileLines(std::span<const BuiltinTable> files, std::string_view name, bool& found) {
    std::vector<std::string_view> out;
    found = false;
    for (const BuiltinTable& t : files) {
        if (t.name != name) continue;
        found = true;
        for (std::string_view line : t.lines) {
            if (line.ends_with('\n')) line.remove_suffix(1);
            if (line.ends_with('\r')) line.remove_suffix(1);
            out.push_back(line);
        }
    }
    return out;
}

// A TSV file: `#` lines are comments, the first other line is the header.
struct Tsv {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
    int Col(std::string_view name) const {
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == name) return static_cast<int>(i);
        return -1;
    }
};

bool ReadTsv(std::span<const BuiltinTable> files, std::string_view name, Tsv& out, std::string& err) {
    bool found = false;
    for (const std::string_view line : FileLines(files, name, found)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols = Split(line, '\t');
        if (out.header.empty()) {
            out.header = std::move(cols);
            continue;
        }
        cols.resize(std::max(out.header.size(), cols.size()));
        out.rows.push_back(std::move(cols));
    }
    if (!found) {
        err = std::string(name) + ": no such table";
        return false;
    }
    if (out.header.empty()) {
        err = std::string(name) + ": no header";
        return false;
    }
    return true;
}

// The columns names of t, or false with err set.
bool Need(const Tsv& t, std::initializer_list<std::string_view> names, std::vector<std::size_t>& cols,
          std::string_view table, std::string& err) {
    cols.clear();
    for (const std::string_view n : names) {
        const int c = t.Col(n);
        if (c < 0) {
            err = std::string(table) + ": missing column " + std::string(n);
            return false;
        }
        cols.push_back(static_cast<std::size_t>(c));
    }
    return true;
}

int MedialOf(std::string_view s) {
    const std::u32string u = ToU32(s);
    return u.size() == 1 ? hangul::MedialOfJamo(u[0]) : -1;
}

bool LoadKo(Tables& t, std::string& err) {
    const auto files = dict::internal::RegisterKoTables();
    std::vector<std::size_t> c;
    {
        Tsv tsv;
        if (!ReadTsv(files, "eomi.tsv", tsv, err) ||
            !Need(tsv, {"id", "slot", "family", "mood", "level", "pos", "form", "flags"}, c, "eomi.tsv", err))
            return false;
        for (const auto& r : tsv.rows) {
            EomiRow e{r[c[0]], r[c[1]], r[c[2]], r[c[3]], r[c[4]], r[c[5]], r[c[6]]};
            e.honIncl = r[c[7]].find("hon-incl") != std::string::npos;
            e.words = r[c[7]].find("words") != std::string::npos;
            t.eomiById.emplace(e.id, t.eomi.size());
            for (const auto& fam : Split(e.family, ',')) t.eomiBySlotFamily.emplace(e.slot + "|" + fam, t.eomi.size());
            t.eomi.push_back(std::move(e));
        }
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "irregular.tsv", tsv, err) ||
            !Need(tsv, {"class", "before", "stem", "ending"}, c, "irregular.tsv", err))
            return false;
        for (const auto& r : tsv.rows) {
            IrrRow row{r[c[0]], r[c[1]], {}, {}};
            if (r[c[2]] != "-") row.stem = Split(r[c[2]], ',');
            if (r[c[3]] != "-") row.ending = Split(r[c[3]], ',');
            t.irregular.push_back(std::move(row));
        }
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "contract.tsv", tsv, err) ||
            !Need(tsv, {"stem", "ending", "result", "mode", "cond"}, c, "contract.tsv", err))
            return false;
        for (const auto& r : tsv.rows) {
            ContractRow row;
            const std::u32string s = ToU32(r[c[0]]);
            if (s.size() == 1 && hangul::IsSyllable(s[0])) row.stemSyllable = s[0];
            else row.stemVowel = MedialOf(r[c[0]]);
            row.endVowel = MedialOf(r[c[1]]);
            row.result = r[c[2]] == "-" ? -1 : MedialOf(r[c[2]]);
            row.mode = r[c[3]];
            for (const auto& cond : Split(r[c[4]], ',')) {
                if (cond == "init=ㅇ") row.initIeung = true;
                else if (cond == "next=yo") row.nextYo = true;
                else if (cond == "prefinal") row.prefinal = true;
                else if (cond != "-") {
                    err = "contract.tsv: unknown cond " + cond;
                    return false;
                }
            }
            if ((row.stemSyllable == 0 && row.stemVowel < 0) || row.endVowel < 0) {
                err = "contract.tsv: bad vowel in " + r[c[0]];
                return false;
            }
            t.contract.push_back(row);
        }
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "particles.tsv", tsv, err) ||
            !Need(tsv, {"id", "kase", "when", "form"}, c, "particles.tsv", err))
            return false;
        for (const auto& r : tsv.rows) {
            ParticleRow row{r[c[0]], r[c[1]], r[c[3]], {}};
            if (r[c[2]] != "-") row.when = Split(r[c[2]], ',');
            t.particles.push_back(std::move(row));
        }
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "stems.tsv", tsv, err) || !Need(tsv, {"lemma", "pos", "class", "flags"}, c, "stems.tsv", err))
            return false;
        for (const auto& r : tsv.rows) {
            StemRow row{r[c[0]], r[c[1]], r[c[2]]};
            row.ae = r[c[3]].find("ae") != std::string::npos;
            row.whole = r[c[3]].find("whole") != std::string::npos;
            row.honSi = r[c[3]].find("hon-si") != std::string::npos;
            t.stems[row.lemma] = row;
        }
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "lemmas.tsv", tsv, err) || !Need(tsv, {"lemma", "pos"}, c, "lemmas.tsv", err)) return false;
        for (const auto& r : tsv.rows) t.lemmaPos[r[c[0]]] = r[c[1]];
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "classrules.tsv", tsv, err) ||
            !Need(tsv, {"match", "pos", "class"}, c, "classrules.tsv", err))
            return false;
        for (const auto& r : tsv.rows) {
            ClassRule rule;
            const std::size_t eq = r[c[0]].find('=');
            rule.kind = eq == std::string::npos ? r[c[0]] : r[c[0]].substr(0, eq);
            rule.value = eq == std::string::npos ? "" : r[c[0]].substr(eq + 1);
            rule.pos = r[c[1]];
            rule.cls = r[c[2]];
            t.classRules.push_back(rule);
        }
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "words.tsv", tsv, err) || !Need(tsv, {"lemma", "kind", "attach"}, c, "words.tsv", err))
            return false;
        for (const auto& r : tsv.rows) {
            // a lemma may be both a dependent noun and an auxiliary: kind-qualified keys
            WordRow w{r[c[0]], r[c[1]], r[c[2]]};
            t.words[(w.kind == "aux" ? "V:" : "N:") + w.lemma] = w;
        }
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "spoken.tsv", tsv, err) ||
            !Need(tsv, {"host", "next", "result", "mode"}, c, "spoken.tsv", err))
            return false;
        for (const auto& r : tsv.rows) t.spoken.push_back({r[c[0]], r[c[1]], r[c[2]], r[c[3]]});
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "punct.tsv", tsv, err) || !Need(tsv, {"kind", "jaMark", "ko", "attach"}, c, "punct.tsv", err))
            return false;
        for (const auto& r : tsv.rows) t.punct.push_back({r[c[0]], r[c[1]], r[c[2]], r[c[3]]});
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "persona.tsv", tsv, err) ||
            !Need(tsv, {"persona", "level", "mood", "family"}, c, "persona.tsv", err))
            return false;
        for (const auto& r : tsv.rows) t.persona.push_back({r[c[0]], r[c[1]], r[c[2]], r[c[3]]});
    }
    {
        Tsv tsv;
        if (!ReadTsv(files, "grammar.tsv", tsv, err) || !Need(tsv, {"name", "value"}, c, "grammar.tsv", err))
            return false;
        for (const auto& r : tsv.rows) t.grammar[r[c[0]]] = r[c[1]];
        for (const char* need :
             {"conn.aux", "aux.neg", "aux.neg-mot", "aux.prohib", "conn.neg", "family.jima", "nom.geot", "conn.default",
              "final.fallback", "pre.hon", "pre.past", "pre.gess", "lemma.cop", "lemma.negcop", "voice.aux", "lit.ends",
              "mixed.hapsyo", "mixed.haeyo", "mixed.word"})
            if (!t.grammar.contains(need)) {
                err = std::string("grammar.tsv: missing ") + need;
                return false;
            }
    }
    return true;
}

bool LoadLevels(RegisterTables& t, std::string& err) {
    const auto files = dict::internal::RegisterLevelTables();
    Tsv c;
    if (!ReadTsv(files, "cues.tsv", c, err)) return false;
    const int cc = c.Col("cue"), cm = c.Col("mode"), ct = c.Col("text"), cn = c.Col("notafter"), cb = c.Col("notbefore");
    if (cc < 0 || cm < 0 || ct < 0) {
        err = "cues.tsv: missing column";
        return false;
    }
    const auto at = [](const std::vector<std::string>& r, int col) -> const std::string& {
        return r[static_cast<std::size_t>(col)];
    };
    for (const auto& r : c.rows) {
        const std::u32string notAfter = cn >= 0 ? ToU32(at(r, cn)) : std::u32string();
        if (at(r, cc) == "tail") t.tails.push_back({ToU32(at(r, ct)), notAfter, at(r, cm) == "strong"});
        else if (at(r, cc) == "quotative") t.quotatives.push_back(ToU32(at(r, ct)));
        else if (at(r, cc) == "exclaim") t.exclaim.push_back(ToU32(at(r, ct)));
        else {
            CueRow row{at(r, cc), at(r, cm), ToU32(at(r, ct)), notAfter, {}};
            if (cb >= 0 && !at(r, cb).empty())
                for (const auto& w : Split(at(r, cb), '|')) row.notBefore.push_back(ToU32(w));
            t.cues.push_back(std::move(row));
        }
    }
    Tsv l;
    if (!ReadTsv(files, "levels.tsv", l, err)) return false;
    std::vector<std::size_t> lc;
    if (!Need(l, {"key", "spec", "level", "profile", "keep", "unquoted"}, lc, "levels.tsv", err)) return false;
    Tsv vx;
    if (!ReadTsv(files, "voices.tsv", vx, err)) return false;
    const int vn = vx.Col("notafter");
    for (const auto& r : vx.rows) {
        if (r[0] == "auth") {
            AuthRow a;
            for (const auto& part : Split(r[1], '+')) {
                std::vector<std::u32string> alts;
                for (const auto& alt : Split(part, '|')) alts.push_back(ToU32(alt));
                a.parts.push_back(alts);
            }
            if (r[2] != "speaker" && r[2] != "line") {
                err = "voices.tsv: auth value must be speaker or line: " + r[2];
                return false;
            }
            a.speaker = r[2] == "speaker";
            if (vn >= 0) a.notAfter = ToU32(at(r, vn));
            t.auth.push_back(a);
            continue;
        }
        VoiceRow v;
        v.type = r[0];
        v.key = ToU32(r[1]);
        if (v.type != "suffix" && !ParsePersona(r[2], v.persona)) {
            err = "voices.tsv: bad persona " + r[2];
            return false;
        }
        t.voices.push_back(v);
    }
    Tsv hx;
    if (!ReadTsv(files, "hon.tsv", hx, err)) return false;
    for (const auto& r : hx.rows) {
        if (r[0] == "exclude") {
            t.honExclude.push_back(r[1]);
        } else if (r[0] == "back") {
            if (r.size() < 3 || (r[1] != "sonkei" && r[1] != "give")) {
                err = "hon.tsv: back rows need kind sonkei or give and a text";
                return false;
            }
            t.honBack.push_back({r[1], ToU32(r[2])});
        }
    }
    for (const auto& r : l.rows) {
        LevelRow row;
        row.key = r[lc[0]];
        row.spec = r[lc[1]];
        if (row.spec.empty()) {
            err = "levels.tsv: no spec id for " + row.key;
            return false;
        }
        row.keep = r[lc[4]] == "yes";
        if (r[lc[5]] == "yes") row.unquoted = LevelRow::Unquoted::yes;
        else if (r[lc[5]] == "strong") row.unquoted = LevelRow::Unquoted::strong;
        else if (r[lc[5]] != "no") {
            err = "levels.tsv: bad unquoted " + r[lc[5]];
            return false;
        }
        if (!ParseLevel(r[lc[2]], row.level)) {
            err = "levels.tsv: bad level " + r[lc[2]];
            return false;
        }
        row.profile = r[lc[3]] == "yes";
        t.levels[row.key] = row;
    }
    return true;
}

bool LoadRetro(const Tables& ko, RetroWords& t, std::string& err) {
    Tsv lt;
    if (!ReadTsv(dict::internal::RegisterRetroTables(), "words.tsv", lt, err)) return false;
    for (const auto& row : lt.rows) {
        if (row[0] == "dep") t.deps.push_back(row[1]);
        else if (row[0] == "pred") t.auxLemmas.push_back(row[1]);
        else if (row[0] == "particle") t.particleKase[row[1]] = row[2];
        else if (row[0] == "jima") t.jima[row[1]] = row[2];
        else if (row[0] == "fused") t.fused[row[1]] = row[2];
    }
    std::stable_sort(t.deps.begin(), t.deps.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    // lemmaPos is unordered: the index is sorted so that it does not depend on the hash order
    std::vector<std::string> lemmas;
    for (const auto& kv : ko.lemmaPos) lemmas.push_back(kv.first);
    std::sort(lemmas.begin(), lemmas.end());
    for (const std::string& lemma : lemmas) {
        const std::u32string lu = ToU32(lemma);
        if (lu.size() >= 2) t.lemmasByFirst[lu[0]].push_back(lemma);
    }
    for (auto& kv : t.lemmasByFirst)
        std::stable_sort(kv.second.begin(), kv.second.end(),
                         [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    return true;
}

bool LoadReader(Reader& d, std::string& err) {
    bool found = false;
    for (const std::string_view row : FileLines(dict::internal::RegisterReaderTables(), "reader.tsv", found)) {
        if (row.empty() || row[0] == '#') continue;
        std::vector<std::u32string> c;
        for (const std::string& col : Split(row, '\t')) c.push_back(ToU32(col));
        const std::string kind = ToU8(c[0]);
        if (c.size() < 2 || c[1].empty()) continue;
        if (kind == "punct-ja" && c.size() >= 3) d.punctJa[c[1]] = ToU8(c[2]);
        else if (kind == "punct-ko" && c.size() >= 4) d.punctKo[c[1]] = {ToU8(c[2]), c[3] == U"-" ? std::u32string() : c[3]};
        else if (kind == "sym") d.sym.insert(c[1][0]);
        else if (kind == "ja-particle") d.jaParticles.insert(c[1]);
        else if (kind == "copula-not") d.copulaNot.insert(c[1]);
        else if (kind == "dep") d.deps.insert(c[1]);
        else if (kind == "glue-lemma") d.glueLemmas.insert(c[1]);
        else if (kind == "imp-end") d.impEnds.insert(c[1]);
        else if (kind == "imp-tail") d.impTail.insert(c[1][0]);
        else if (kind == "imp-close") d.impClose.insert(c[1][0]);
        else if (kind == "part-alt") d.partAlts.insert(c[1]);
        else if (kind == "stutter-mark") d.stutterMark = c[1][0];
        else if (kind == "copula-alt") d.copulaAlt = c[1];
        else if (kind == "copula-lemma") d.copulaLemma = c[1];
        else if (kind == "lemma-end") d.lemmaEnd = c[1];
        else if (kind == "quote" && c.size() >= 3) d.quotes.push_back({c[1][0], c[2][0]});
        else if (kind == "tag" && c.size() >= 3) d.tags.push_back({c[1][0], c[2][0]});
        else if (kind == "caption-mark") d.captionMarks.insert(c[1][0]);
        else if (kind == "keigo") d.keigo.push_back(ToU8(c[1]));
        else if (kind == "keigo-prefix" && c.size() >= 3) d.keigoPrefix.push_back(ToU8(c[1]) + "+" + ToU8(c[2]));
        else if (kind == "command-form") d.commandForms.push_back(ToU8(c[1]));
        else {
            err = "reader.tsv: unknown row kind " + kind;
            return false;
        }
    }
    if (!found) {
        err = "reader.tsv: no such table";
        return false;
    }
    return true;
}

// Every keigo, keigo-prefix and command-form row of reader.tsv has its row in keigo.tsv.
bool LoadKeigo(const Reader& reader, std::vector<KeigoWord>& out, std::string& err) {
    Tsv t;
    std::vector<std::size_t> c;
    if (!ReadTsv(dict::internal::RegisterReaderTables(), "keigo.tsv", t, err) ||
        !Need(t, {"row", "start", "not"}, c, "keigo.tsv", err))
        return false;
    std::set<std::string> rows;
    for (const auto& r : t.rows) {
        KeigoWord w;
        const std::string& row = r[c[0]];
        const std::size_t colon = row.find(':');
        const std::string kind = row.substr(0, colon), key = colon == std::string::npos ? "" : row.substr(colon + 1);
        const auto known = [&](const std::vector<std::string>& keys) {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        };
        if (kind == "keigo" && known(reader.keigo)) w.kind = KeigoWord::Kind::keigo;
        else if (kind == "keigo-prefix" && known(reader.keigoPrefix)) w.kind = KeigoWord::Kind::prefix;
        else if (kind == "command-form" && known(reader.commandForms)) w.kind = KeigoWord::Kind::command;
        else {
            err = "keigo.tsv: " + row + " is no keigo, keigo-prefix or command-form row of reader.tsv";
            return false;
        }
        rows.insert(row);
        for (const auto& s : Split(r[c[1]], '|'))
            if (!s.empty()) w.start.push_back(ToU32(s));
        for (const auto& s : Split(r[c[2]], '|'))
            if (!s.empty()) w.notStart.push_back(ToU32(s));
        out.push_back(std::move(w));
    }
    const auto need = [&](const std::string& kind, const std::vector<std::string>& keys) {
        for (const std::string& k : keys)
            if (!rows.contains(kind + ":" + k)) {
                err = "keigo.tsv: no row for the reader.tsv row " + kind + " " + k;
                return false;
            }
        return true;
    };
    return need("keigo", reader.keigo) && need("keigo-prefix", reader.keigoPrefix) &&
           need("command-form", reader.commandForms);
}

struct Loaded {
    std::unique_ptr<Data> data;
    std::string error;
};

Loaded Load() {
    Loaded l;
    auto d = std::make_unique<Data>();
    if (LoadKo(d->ko, l.error) && LoadLevels(d->levels, l.error) && LoadRetro(d->ko, d->retro, l.error) &&
        LoadReader(d->reader, l.error) && LoadKeigo(d->reader, d->keigo, l.error))
        l.data = std::move(d);
    return l;
}

const Loaded& Once() {
    static const Loaded loaded = Load();
    return loaded;
}

}  // namespace

const Data* LoadedData() { return Once().data.get(); }
const std::string& DataError() { return Once().error; }

}  // namespace hdTrans::reg
