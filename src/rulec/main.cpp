// hdrulec: the rule compiler.
//   hdrulec lint RULES [-dicts DIR] [-tsv] [-max-delta N] [-budget FILE]
//       parses the flat rules (or readable rules, *.hdr) and runs the checks; exit 1 on errors,
//       and with -budget also when the warnings of a flag are not the count FILE records
//   hdrulec convert RULES [-o OUT]       the flat rules in the readable syntax
//   hdrulec flat RULES.hdr [-o OUT]      readable rules in the flat format
//   hdrulec verify RULES [-paths N]      checks the front end against rules::Load and the
//                                        conversion round trip on every rule (at most N
//                                        evaluation paths per rule, default 2^20)
//   hdrulec cpp RULES -local FIRST-LAST [-globals] [-o OUT | -check FILE]
//   hdrulec dump RULES [-hash] [-o OUT | -check FILE]
//                                        the program rules::Load builds, in run order, or the
//                                        FNV-1a 64 of that dump; -check fails unless FILE holds it
// RULES is a flat rules file, a directory of them (*.txt, read as the build reads it), or a
// readable file (*.hdr).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/rules/compiled.h"
#include "core/rules/rules.h"
#include "rulec/oracle.h"
#include "rulec/rulec.h"

using namespace hdTrans;
using namespace hdTrans::rulec;

namespace {

bool ReadFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool WriteFile(const std::string& path, const std::string& text) {
    if (path.empty()) return std::fwrite(text.data(), 1, text.size(), stdout) == text.size();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

bool Readable(const std::string& path) { return path.size() > 4 && path.substr(path.size() - 4) == ".hdr"; }

int Report(const Source& source, const Diagnostics& diags, bool tsv) {
    if (tsv) std::fputs("kind\tindex\tstatement\tfile\tline\tcolumn\tseverity\tflag\tmessage\n", stdout);
    for (const Diagnostic& d : diags.list) {
        const std::string text = tsv ? FormatRow(source, d) : Format(source, d);
        std::fputs(text.c_str(), tsv ? stdout : stderr);
    }
    if (!tsv && (diags.errors || diags.warnings))
        std::fprintf(stderr, "%d error(s), %d warning(s)\n", diags.errors, diags.warnings);
    return diags.errors ? 1 : 0;
}

// The warnings of each flag against the lines "flag<TAB>count" of path (# starts a comment; a
// flag without a line has 0). A count above its line fails, and so does one below it, so that
// the line is lowered with the change that removes the warnings. 0 equal, 1 different, 2 unread.
int Budget(const Diagnostics& diags, const std::string& path) {
    std::string text;
    if (!ReadFile(path, text)) {
        std::fprintf(stderr, "hdrulec: cannot read %s\n", path.c_str());
        return 2;
    }
    std::map<std::string, long> ceiling, count;
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t tab = line.find('\t');
        char* last = nullptr;
        const long n = tab == std::string::npos ? -1 : std::strtol(line.c_str() + tab + 1, &last, 10);
        if (n < 0 || last == line.c_str() + tab + 1 || *last != '\0') {
            std::fprintf(stderr, "hdrulec: %s: not \"flag<TAB>count\": %s\n", path.c_str(), line.c_str());
            return 2;
        }
        if (tab == 0 || !ceiling.emplace(line.substr(0, tab), n).second) {
            std::fprintf(stderr, "hdrulec: %s: an empty or repeated flag: %s\n", path.c_str(), line.c_str());
            return 2;
        }
    }
    for (const Diagnostic& d : diags.list) {
        if (d.severity == Severity::Warning) count[d.flag]++;
    }
    for (const auto& entry : ceiling) count.try_emplace(entry.first, 0);
    int status = 0;
    for (const auto& [flag, n] : count) {
        const auto it = ceiling.find(flag);
        const long want = it == ceiling.end() ? 0 : it->second;
        if (n == want) continue;
        std::fprintf(stderr, "hdrulec: %ld [-W%s] warning(s), %s records %ld; %s\n", n, flag.c_str(), path.c_str(), want,
                     n > want ? "fix the new ones" : "lower the line to the new count");
        status = 1;
    }
    return status;
}

// The front end against rules::Load, and the readable round trip on every rule: same
// normalized trees, and the same calls for every answer script (Oracle).
int Verify(const Source& source, const File& file, long long cap) {
    int failures = 0;
    rules::Program loaded;
    rules::LoadError error{};
    if (!rules::Load(source.text, loaded, error)) {
        std::fprintf(stderr, "rules::Load rejects the file at line %d: %s\n", error.line, error.message);
        return 1;
    }
    const rules::Program lowered = Lower(file);
    const bool same = Dump(loaded) == Dump(lowered);
    std::printf("front end vs rules::Load: %s (%zu nodes, %zu literal bytes)\n",
                same ? "identical dumps" : "DIFFERENT dumps", loaded.nodes.size(), loaded.literals.size());
    failures += !same;

    const std::string readable = PrintReadable(file);
    Source back("(readable)", readable);
    Diagnostics diags;
    File again = ParseReadable(back, diags);
    if (diags.errors) {
        Report(back, diags, false);
        return 1;
    }
    File a = file;
    Normalize(a);
    CanonicalLiterals(a);
    Normalize(again);
    CanonicalLiterals(again);
    int treeDiffs = 0;
    if (a.local.size() != again.local.size() || a.global.size() != again.global.size()) treeDiffs++;
    for (std::size_t i = 0; i < a.local.size() && i < again.local.size(); i++) {
        const bool equal = SameTree(a.local[i].body, again.local[i].body) && a.local[i].tests.size() == again.local[i].tests.size();
        if (!equal) std::fprintf(stderr, "L %zu differs after the round trip\n", i);
        treeDiffs += !equal;
    }
    for (std::size_t i = 0; i < a.global.size() && i < again.global.size(); i++) {
        const Rule &x = a.global[i], &y = again.global[i];
        const bool equal = SameTree(x.body, y.body) && x.pumsa == y.pumsa && x.number == y.number && x.ugan == y.ugan;
        if (!equal) std::fprintf(stderr, "G %zu differs after the round trip\n", i);
        treeDiffs += !equal;
    }
    std::printf("readable round trip: %zu local and %zu global rules, %d tree differences, %zu bytes of readable text\n",
                again.local.size(), again.global.size(), treeDiffs, readable.size());
    failures += treeDiffs;

    // The flat text printed from the readable tree loads and evaluates like the original.
    rules::Program converted;
    const std::string flat = PrintFlat(again);
    if (!rules::Load(flat, converted, error)) {
        std::fprintf(stderr, "the converted flat text does not load: line %d: %s\n", error.line, error.message);
        return 1;
    }
    // A rule may differ from the original only in the spelling of surface markers ("22x" is
    // printed as ends "x" and read back as "Rx"); it must then match the original with
    // canonical literals.
    File canonicalFile = file;
    CanonicalLiterals(canonicalFile);
    const rules::Program canonical = Lower(canonicalFile);
    Oracle oracle;
    long long paths = 0, capped = 0, differ = 0, spelling = 0;
    auto run = [&](const rules::Program& p, uint32_t root) {
        return oracle.Paths([&](rules::Item* it) { return rules::RunRule(p, root, it); }, cap);
    };
    auto compare = [&](uint32_t rootA, uint32_t rootB, const char* kind, std::size_t index) {
        const auto x = run(loaded, rootA);
        const auto y = run(converted, rootB);
        paths += x.paths;
        capped += x.capped;
        if (x.paths == y.paths && x.hash == y.hash) return;
        const auto z = run(canonical, kind[0] == 'L' ? canonical.localRules[index] : canonical.globalRules[index].root);
        if (z.paths == y.paths && z.hash == y.hash) {
            spelling++;
            return;
        }
        std::fprintf(stderr, "%s %zu evaluates differently after the round trip\n", kind, index);
        differ++;
    };
    for (std::size_t i = 0; i < loaded.localRules.size(); i++) compare(loaded.localRules[i], converted.localRules[i], "L", i);
    for (std::size_t i = 0; i < loaded.globalRules.size(); i++)
        compare(loaded.globalRules[i].root, converted.globalRules[i].root, "G", i);
    std::printf("call-trace equivalence of the round trip: %lld paths, %lld rules stopped at %lld paths, "
                "%lld rules differ only in marker spelling, %lld rules differ\n",
                paths, capped, cap, spelling, differ);
    failures += static_cast<int>(differ);
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: hdrulec lint|convert|flat|verify|cpp RULES [options]\n");
        return 2;
    }
    const std::string command = argv[1], path = argv[2];
    std::string out, dicts, check, budget;
    bool tsv = false, globals = false, hashOnly = false;
    int first = 0, last = -1;
    long long cap = 1 << 20;
    CheckOptions options;
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "-dicts" && i + 1 < argc) dicts = argv[++i];
        else if (a == "-check" && i + 1 < argc) check = argv[++i];
        else if (a == "-budget" && i + 1 < argc) budget = argv[++i];
        else if (a == "-tsv") tsv = true;
        else if (a == "-hash") hashOnly = true;
        else if (a == "-globals") globals = true;
        else if (a == "-paths" && i + 1 < argc) cap = std::atoll(argv[++i]);
        else if (a == "-max-delta" && i + 1 < argc) options.maxDelta = std::atoi(argv[++i]);
        else if (a == "-local" && i + 1 < argc && std::sscanf(argv[++i], "%d-%d", &first, &last) == 2) {}
        else {
            std::fprintf(stderr, "hdrulec: unknown option %s\n", a.c_str());
            return 2;
        }
    }
    std::string text, error;
    std::vector<Source::File> files;
    const bool dir = std::filesystem::is_directory(path);
    if (dir ? !ReadRuleDir(path, text, files, error) : !ReadFile(path, text)) {
        std::fprintf(stderr, "hdrulec: cannot read %s %s\n", path.c_str(), error.c_str());
        return 2;
    }
    Source source(path, std::move(text));
    if (dir) source.files = files;
    Diagnostics diags;
    File file = Readable(path) ? ParseReadable(source, diags) : ParseFlat(source, diags);
    if (diags.errors) return Report(source, diags, tsv);

    if (command == "lint") {
        if (!budget.empty() && dicts.empty()) {
            std::fprintf(stderr, "hdrulec: -budget needs -dicts, without which the dictionary checks do not run\n");
            return 2;
        }
        Inventory inventory;
        error.clear();
        if (!dicts.empty() && !LoadInventory(dicts, inventory, error)) {
            std::fprintf(stderr, "hdrulec: %s\n", error.c_str());
            return 2;
        }
        File normalized = file;
        Normalize(normalized);
        Check(normalized, inventory, options, diags);
        const int status = Report(source, diags, tsv);
        if (budget.empty() || status != 0) return status;
        return Budget(diags, budget);
    }
    if (command == "convert") return WriteFile(out, PrintReadable(file)) ? 0 : 2;
    if (command == "flat") return WriteFile(out, PrintFlat(file)) ? 0 : 2;
    if (command == "verify") return Verify(source, file, cap);
    if (command == "dump") {
        // The program as the engine loads it.
        rules::Program loaded;
        rules::LoadError loadError{};
        if (!rules::Load(source.text, loaded, loadError)) {
            std::fprintf(stderr, "hdrulec: rules::Load rejects the program at line %d: %s\n", loadError.line, loadError.message);
            return 1;
        }
        const std::string dump = Dump(loaded);
        char hash[32];
        std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(rules::compiled::RecordHash(dump)));
        if (check.empty()) return WriteFile(out, hashOnly ? std::string(hash) + "\n" : dump) ? 0 : 2;
        std::string want;
        if (!ReadFile(check, want)) return 2;
        if (want.find(hash) == std::string::npos) {
            std::fprintf(stderr, "hdrulec: the rules program changed (dump hash %s, %s records another); if the change is "
                                 "meant, write the new hash with hdrulec dump %s -hash\n",
                         hash, check.c_str(), path.c_str());
            return 1;
        }
        return 0;
    }
    if (command == "cpp") {
        if (Readable(path)) {
            std::fprintf(stderr, "hdrulec: cpp needs the flat rules, whose record lines it hashes\n");
            return 2;
        }
        const std::string code = EmitCpp(file, source, first, last, globals);
        if (check.empty()) return WriteFile(out, code) ? 0 : 2;
        std::string current;
        if (!ReadFile(check, current) || current != code) {
            std::fprintf(stderr, "hdrulec: %s is not the code of %s; regenerate it with hdrulec cpp\n", check.c_str(),
                         path.c_str());
            return 1;
        }
        return 0;
    }
    std::fprintf(stderr, "hdrulec: unknown command %s\n", command.c_str());
    return 2;
}
