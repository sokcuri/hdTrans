// snapshot_check: dumps the compiled plain entries and the dictionary lookups over
// corpus lines in a line-oriented tab-separated form, for comparison with a reference
// dump; or measures loading.
//
//   snapshot_check -dicts DIR[,DIR...] -compiled FILE [-raw FILE -each FILE [-eachlines N] [-out FILE]]
//   snapshot_check -dicts DIR[,DIR...] -time
//
// -compiled writes, sorted, every compiled plain entry: C lines per source (the builtin
// plain files and the plain files of the folders, compiled again in load order), V
// lines per composed view of the snapshot (raw: the word pipeline, text: the text
// path) with the entry's index, N lines with the entry and indexed counts of each
// view, G lines per sense and M lines per row of a generated marker family.
// Pass a looks every position of every line up in all modes with the text view;
// pass b in modes 0 and 3 with the raw view, strategy UserFirst, prior dictionary BZ,
// the name keys of the line confirmed, its person places and an entry filter.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

#include "core/dict/builtin.h"
#include "core/dict/snapshot.h"
#include "core/dict/tsv.h"
#include "core/text/utf8.h"
#include "core/userdict/sense.h"
#include "core/userdict/userdict.h"

using namespace hdTrans::dict;

namespace {

std::vector<std::string> Split(std::string_view s, char sep) {
    std::vector<std::string> out;
    for (;;) {
        const std::size_t i = s.find(sep);
        out.emplace_back(s.substr(0, i));
        if (i == std::string_view::npos) return out;
        s.remove_prefix(i + 1);
    }
}

bool ReadAll(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[1 << 16];
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof buf, f);
        out.append(buf, n);
        if (n < sizeof buf) break;
    }
    std::fclose(f);
    return true;
}

std::vector<std::string> ReadLines(const std::string& path, long limit) {
    std::string text;
    if (!ReadAll(path, text)) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<std::string> out;
    for (std::string& l : Split(text, '\n')) {
        if (limit >= 0 && static_cast<long>(out.size()) == limit) break;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(std::move(l));
    }
    if (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

class Writer {
public:
    explicit Writer(std::FILE* f) : f_(f) {}
    ~Writer() { Flush(); }
    Writer& operator<<(std::string_view s) {
        buf_.append(s);
        if (buf_.size() > (1 << 20)) Flush();
        return *this;
    }
    Writer& operator<<(long v) { return *this << std::string_view(std::to_string(v)); }
    void Flush() {
        std::fwrite(buf_.data(), 1, buf_.size(), f_);
        buf_.clear();
    }

private:
    std::FILE* f_;
    std::string buf_;
};

std::string EntryFlags(const UserEntry& u) {
    std::string s;
    const std::pair<bool, char> flags[] = {{u.name, 'N'},   {u.nameLayer, 'L'}, {u.kana, 'K'},
                                           {u.force, 'F'},  {u.hidden, 'H'},    {u.honorific, 'O'},
                                           {u.end, 'E'},    {u.polite, 'P'},    {u.stative, 'S'}};
    for (const auto& [on, c] : flags) {
        if (on) s += c;
    }
    if (u.finalClass) s += u.finalClass;
    if (u.registerLevel) s += std::to_string(u.registerLevel);
    return s;
}

std::string EntryKind(const UserEntry& e) {
    if (e.kana) return "kana";
    if (e.end) return "end";
    if (e.Part().TagIn("STUP")) return "pat";
    return "plain";
}

std::string EntryFields(const UserEntry& e) {
    return EntryKind(e) + "\t" + std::to_string(e.file) + "\t" + std::to_string(e.line) + "\t" +
           std::to_string(e.order) + "\t" + e.key + "\t" + e.target + "\t" + e.codes->partText + "\t" +
           std::string(e.codes->sem.Text()) + "\t" + std::string(e.codes->gram.Text()) + "\t" + std::to_string(e.prio) +
           "\t" + EntryFlags(e) + "\t" + e.negative;
}

std::string Index(std::size_t i) {
    std::string s = std::to_string(i);
    return std::string(s.size() < 6 ? 6 - s.size() : 0, '0') + s;
}

bool Fail(const std::string& what) {
    std::fprintf(stderr, "%s\n", what.c_str());
    return false;
}

// Compiles the builtin folder and the plain files of s again, in load order, and writes
// the compiled dump of them and of the snapshot's views to path.
bool WriteCompiled(const Snapshot& s, const std::string& path) {
    namespace ud = hdTrans::userdict;
    std::vector<std::pair<std::string, std::vector<UserEntry>>> sources;
    CodeTable codes;
    ud::Families families;
    ud::Context builtin{&codes, &families, nullptr};
    const auto compile = [&](const std::string& label, std::string_view text, int file, ud::Context& ctx) {
        ud::Compiled c = ud::Parse(text, file, ctx);
        if (!c.errors.empty()) return Fail(label + ": " + c.errors[0].message);
        sources.emplace_back(label, std::move(c.entries));
        return true;
    };
    for (const std::string_view name : BuiltinNames()) {
        if (name == "sound.tsv" || name == "phrases.tsv" || name == "patterns.tsv" || name == "senses.tsv" ||
            name == "lines.tsv" || name == "classes.tsv") {
            continue;
        }
        if (!compile("builtin/" + std::string(name), BuiltinText(name), -1, builtin)) return false;
    }
    std::string phrases;
    for (const std::string& line : Split(BuiltinText("phrases.tsv"), '\n')) {
        std::string bare = line;
        while (!bare.empty() && bare.back() == '\r') bare.pop_back();
        const std::vector<std::string> f = Split(bare, '\t');
        const char32_t r = f[0].empty() ? 0 : hdTrans::text::DecodeRune(f[0]).cp;
        const bool kana = (r >= U'ぁ' && r <= U'ゖ') || (r >= U'ァ' && r <= U'ヶ') || r == U'ー' || r == U'～';
        phrases += (f.size() >= 3 && f[2] == "intj" && kana) ? std::string() : line;
        phrases += '\n';
    }
    phrases.pop_back();
    if (!compile("builtin/phrases.tsv", phrases, -1, builtin)) return false;
    if (!compile("builtin/patterns.tsv", BuiltinText("patterns.tsv"), -1, builtin)) return false;
    ud::Context ctx{&codes, &families, &s.Base()};
    int file = 0;
    for (const File& f : s.PlainFiles()) {
        const std::size_t slash = f.name.find_last_of("/\\");
        const std::string base = slash == std::string::npos ? f.name : f.name.substr(slash + 1);
        const std::string label = std::to_string(f.folder) + "/" + base;
        const std::string_view text = hdTrans::dict::internal::WithoutHeader(f.text);
        if (f.layer == hdTrans::dict::FileLayer::Readings) continue;
        if (f.layer == hdTrans::dict::FileLayer::Kana) {
            sources.emplace_back(label, ud::KanaSpellings(text, s.Base(), codes));
            continue;
        }
        if (!compile(label, text, file++, ctx)) return false;
    }

    std::vector<std::string> lines;
    std::vector<std::string> ids;
    const auto note = [&ids](std::string_view t) {
        const std::size_t n = t.size();
        if (n >= 4 && t[n - 1] == '@' && (t[n - 4] == '&' || t[n - 4] == '$' || t[n - 4] == '%')) {
            ids.emplace_back(t.substr(n - 4));
        }
    };
    for (const auto& [label, entries] : sources) {
        for (std::size_t i = 0; i < entries.size(); i++) {
            lines.push_back("C\t" + label + "\t" + Index(i) + "\t" + EntryFields(entries[i]));
            note(entries[i].target);
            note(entries[i].negative);
        }
    }
    for (const auto& [name, view] : {std::pair<const char*, const UserText*>{"raw", &s.Plain()}, {"text", &s.TextPlain()}}) {
        for (std::size_t i = 0; i < view->Entries().size(); i++) {
            lines.push_back(std::string("V\t") + name + "\t" + Index(i) + "\t" + EntryFields(view->Entries()[i]));
        }
        lines.push_back(std::string("N\t") + name + "\t" + std::to_string(view->Entries().size()) + "\t" +
                        std::to_string(view->Len()));
    }
    for (const auto& [key, list] : s.Senses()) {
        for (std::size_t i = 0; i < list.size(); i++) {
            const ud::Sense& x = list[i];
            std::string triggers;
            for (std::size_t t = 0; t < x.triggers.size(); t++) triggers += (t ? "|" : "") + x.triggers[t];
            lines.push_back("G\t" + key + "\t" + std::to_string(i) + "\t" + x.key + "\t" + std::string(1, x.classDigit) +
                            "\t" + x.target + "\t" + std::to_string(static_cast<int>(x.side)) + "\t" + triggers + "\t" +
                            (x.adjective ? "1" : "0"));
            note(x.target);
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    const ud::Families& gen = s.GeneratedFamilies();
    for (const std::string& id : ids) {
        const std::string_view used = id[0] == '&' ? "ACEFHJKLMN" : id[0] == '$' ? "ACDFGIJKL" : "ABCDE";
        if (used.find(id[1]) != std::string_view::npos) continue;
        for (const char code : ud::kFormCodes) {
            const std::string key = id + code;
            const std::string_view row = id[0] == '&' ? gen.SourceRow(key) : id[0] == '$' ? gen.DollarRow(key) : gen.PercentRow(key);
            if (!row.empty()) lines.push_back("M\t" + key + "\t" + std::string(row));
        }
    }
    std::sort(lines.begin(), lines.end());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return Fail("cannot write " + path);
    {
        Writer w(f);
        for (const std::string& l : lines) w << l << "\n";
    }
    std::fclose(f);
    return true;
}

void Lookup(Writer& w, const std::string& pos, const char* pass, int mode, const Candidates& c) {
    if (c.list.empty() && c.count == 0) return;
    w << "W\t" << pos << "\t" << pass << "\t" << long{mode} << "\t" << long{c.count} << "\n";
    for (const Candidate& x : c.list) {
        w << "\t" << x.source << "\t" << x.target << "\t" << x.codes->partText << "\t" << x.codes->sem.Text() << "\t"
          << x.codes->gram.Text() << "\t" << x.codes->dict.Text() << "\t" << long{x.rule} << "\t" << long{x.id} << "\t";
        if (const UserEntry* u = x.user) {
            w << "u:" << long{u->prio} << ":" << long{u->file} << ":" << long{u->line} << ":" << long{u->order} << ":"
              << EntryFlags(*u);
        } else {
            w << "-";
        }
        w << "\n";
    }
}

template <class R>
void Records(Writer& w, const char* kind, const std::string& pos, std::string_view key, const std::vector<R>& records) {
    w << kind << "\t" << pos << "\t" << key << "\n";
    for (const R& r : records) {
        w << "\t" << r.target << "\t" << r.codes->partText << "\t" << r.codes->sem.Text() << "\t" << r.codes->gram.Text()
          << "\t" << r.codes->dict.Text() << "\t" << long{r.rule} << "\t" << long{r.id} << "\n";
    }
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (i) s += "|";
        s += v[i];
    }
    return s;
}

// The substrings of line of 2 to 6 characters that have name-layer records, in order
// of first occurrence.
std::vector<std::string> NameKeys(const Snapshot& s, const std::string& line) {
    std::vector<std::string> keys;
    KeySet seen;
    for (std::size_t i = 0; i < line.size(); i += hdTrans::text::DecodeRune(std::string_view(line).substr(i)).size) {
        std::size_t end = i;
        for (int n = 1; n <= 6 && end < line.size(); n++) {
            end += hdTrans::text::DecodeRune(std::string_view(line).substr(end)).size;
            const std::string k = line.substr(i, end - i);
            if (n >= 2 && !seen.count(k) && !s.NameRecords(k).empty()) {
                seen.insert(k);
                keys.push_back(k);
            }
        }
    }
    return keys;
}

void Exact(Writer& w, const Snapshot& s, const std::string& pos, std::string_view q) {
    std::size_t end = 0;
    for (int n = 1; n <= 8 && end < q.size(); n++) {
        end += hdTrans::text::DecodeRune(q.substr(end)).size;
        const std::string_view k = q.substr(0, end);
        if (const auto c = s.RetrieveRecords(k, LookupMode::General); !c.empty()) Records(w, "R", pos, k, c);
        if (const auto r = s.NameRecords(k); !r.empty()) Records(w, "N", pos, k, r);
        if (const std::string p = s.PersonName(k); !p.empty()) w << "P\t" << pos << "\t" << k << "\t" << p << "\n";
        if (s.PersonCueKey(k)) w << "Q\t" << pos << "\t" << k << "\n";
    }
}

bool SkipB(const void*, const UserEntry& e) { return e.kana || (e.nameLayer && e.line % 3 == 0); }

std::size_t HeapInUse() {
#ifdef __APPLE__
    malloc_statistics_t st;
    malloc_zone_statistics(nullptr, &st);
    return st.size_in_use;
#else
    return 0;
#endif
}

int Time(const std::vector<std::string>& dirs) {
    std::vector<double> times;
    std::size_t before = HeapInUse();
    std::size_t heap = 0, bytes = 0;
    for (int i = 0; i < 5; i++) {
        const auto start = std::chrono::steady_clock::now();
        LoadResult r = LoadFolders(dirs);
        times.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        if (!r.snapshot) {
            std::fprintf(stderr, "load failed\n");
            return 1;
        }
        heap = HeapInUse() - before;
        bytes = r.snapshot->MemoryBytes();
    }
    std::sort(times.begin(), times.end());
    std::printf("load min %.3fs median %.3fs\nheap in use after load %.1f MB, snapshot %.1f MB\n", times[0], times[2],
                static_cast<double>(heap) / 1e6, static_cast<double>(bytes) / 1e6);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string dicts, compiledPath, raw, each, outPath;
    long eachLines = 10000;
    bool timing = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "-dicts" && hasValue) dicts = argv[++i];
        else if (a == "-compiled" && hasValue) compiledPath = argv[++i];
        else if (a == "-raw" && hasValue) raw = argv[++i];
        else if (a == "-each" && hasValue) each = argv[++i];
        else if (a == "-eachlines" && hasValue) eachLines = std::atol(argv[++i]);
        else if (a == "-out" && hasValue) outPath = argv[++i];
        else if (a == "-time") timing = true;
        else {
            std::fprintf(stderr, "usage: snapshot_check -dicts DIR[,DIR] (-time | [-compiled FILE] [-raw FILE -each FILE [-eachlines N] [-out FILE]])\n");
            return 2;
        }
    }
    const std::vector<std::string> dirs = Split(dicts, ',');
    if (timing) return Time(dirs);

    const LoadResult loaded = LoadFolders(dirs);
    if (!loaded.snapshot) {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }
    const Snapshot& s = *loaded.snapshot;
    if (!compiledPath.empty() && !WriteCompiled(s, compiledPath)) return 1;
    if (raw.empty()) return 0;
    std::FILE* outFile = outPath.empty() ? stdout : std::fopen(outPath.c_str(), "wb");
    if (!outFile) return 1;
    {
        Writer w(outFile);
        for (const LoadError& e : loaded.errors) {
            w << "E\t" << e.file << "\t" << long{static_cast<int>(e.kind)} << "\t" << long{e.line} << "\n";
        }
        for (const File& f : s.PlainFiles()) {
            const std::size_t slash = f.name.find_last_of("/\\");
            w << "F\t" << (slash == std::string::npos ? f.name : f.name.substr(slash + 1)) << "\n";
        }
        for (const std::string& x : s.Warnings()) w << "X\t" << x << "\n";
        w << "S\tbase " << static_cast<long>(s.Base().Len()) << "\tuser "
          << static_cast<long>(s.User() ? s.User()->Len() : 0) << "\n";

        LookupContext a;
        a.plain = &s.TextPlain();
        a.strategy = Strategy::LongestMatch;
        for (int m = 0; m <= 4; m++) Lookup(w, "sentinel", "a", m, s.GetWordInfo("", static_cast<LookupMode>(m), a));
        for (int m = 1; m <= 3; m++) {
            Records(w, "R", "sentinel", std::to_string(m), s.RetrieveRecords("", static_cast<LookupMode>(m)));
        }

        const struct {
            const char* name;
            std::vector<std::string> lines;
        } sets[] = {{"raw", ReadLines(raw, -1)}, {"each", ReadLines(each, eachLines)}};
        for (const auto& set : sets) {
            for (std::size_t n = 0; n < set.lines.size(); n++) {
                const std::string& line = set.lines[n];
                const std::string at = std::string(set.name) + ":" + std::to_string(n + 1);
                const std::vector<std::string> persons = s.PersonPlaceKeys(line);
                if (!persons.empty()) w << "K\t" << at << "\t" << Join(persons) << "\n";
                const std::vector<std::string> confirmed = NameKeys(s, line);
                if (!confirmed.empty()) w << "C\t" << at << "\t" << Join(confirmed) << "\n";
                const KeySet confirmedSet(confirmed.begin(), confirmed.end());
                const KeySet personSet(persons.begin(), persons.end());
                LookupContext b;
                b.plain = &s.Plain();
                b.strategy = Strategy::UserFirst;
                b.priorDict = "BZ";
                b.confirmed = &confirmedSet;
                b.persons = &personSet;
                b.skip = {SkipB, nullptr};
                for (std::size_t i = 0; i < line.size(); i += hdTrans::text::DecodeRune(std::string_view(line).substr(i)).size) {
                    const std::string_view q = std::string_view(line).substr(i);
                    const std::string pos = at + ":" + std::to_string(i);
                    for (int m = 0; m <= 4; m++) Lookup(w, pos, "a", m, s.GetWordInfo(q, static_cast<LookupMode>(m), a));
                    for (const int m : {0, 3}) Lookup(w, pos, "b", m, s.GetWordInfo(q, static_cast<LookupMode>(m), b));
                    Exact(w, s, pos, q);
                }
            }
        }
    }
    if (outFile != stdout) std::fclose(outFile);
    return 0;
}
