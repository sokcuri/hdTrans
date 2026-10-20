// UTF-8 CLI. The whole input is one session unless --each or --raw is given.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "core/config/config.h"
#include "core/config/cache.h"
#include "core/engine.h"
#include "core/dict/folders.h"
#include "core/dict/snapshot_file.h"
#include "core/engine/driver.h"
#include "core/engine/rules.h"
#include "core/word/select.h"
#include "stage_records.h"
#include "trace_json.h"
#include "frontend/dictionary_version.h"

namespace {

namespace stagerecords = hdTrans::cli::stagerecords;

std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t comma = s.find(',', start);
        out.push_back(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) return out;
        start = comma + 1;
    }
}

// The folder of the running executable in UTF-8; empty when unknown.
std::string ExecutableDir() {
    std::filesystem::path exe;
#ifdef _WIN32
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    exe = path;
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
    exe = path.c_str();
#else
    std::error_code readError;
    exe = std::filesystem::read_symlink("/proc/self/exe", readError);
    if (readError) return {};
#endif
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::canonical(exe, ec);
    const std::u8string dir = (ec ? exe : canonical).parent_path().u8string();
    return std::string(reinterpret_cast<const char*>(dir.data()), dir.size());
}

void Usage(std::FILE* file) {
    std::fputs("hdTrans\nAuthor: Sokcuri (https://sokcuri.neko.kr/)\n\n"
               "usage: hdtrans [options] [text]\n"
               "  --in FILE, --file FILE  read UTF-8 input (default: stdin)\n"
               "  --out FILE             write UTF-8 output (default: stdout)\n"
               "  --json                 return text, choices and logs as JSON\n"
               "  --why                  write selection and filter logs to stderr\n"
               "  --trace FILE           write every candidate of every word to FILE\n"
               "  --version              load dictionaries and show engine, runtime and dictionary versions\n"
               "  --help                 show this help\n"
               "  --config FILE          settings file\n"
               "  --dicts DIR,...        dictionary paths in load order\n"
               "  --userdict PATH        user dictionary file or folder (repeatable)\n"
               "  --ehnd DIR             opt-in Ehnd folder\n"
               "  --each, --raw          independent lines, or word-pipeline-only lines\n"
               "  --reference, --misread, --no-cg, --cg-learned, --select-shadow\n"
               "  --verify-dicts, --compile-dicts, --compile-snapshot FILE, --no-cache\n"
               "  --stages DIR, --rule-counts FILE, --dump-rules FILE\n"
               "Legacy single-hyphen options are also accepted. Use -- before literal text starting with '-'.\n", file);
}

std::FILE* OpenFile(const std::string& path, bool write) {
#ifdef _WIN32
    const std::u8string utf8(path.begin(), path.end());
    return _wfopen(std::filesystem::path(utf8).c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

bool ReadAll(std::FILE* f, std::string& out) {
    char buf[1 << 16];
    for (;;) {
        const size_t n = std::fread(buf, 1, sizeof buf, f);
        out.append(buf, n);
        if (n < sizeof buf) return std::ferror(f) == 0;
    }
}

void WriteEscaped(std::FILE* out, std::string_view text) {
    for (char c : text) {
        switch (c) {
        case '\t': std::fputs("\\t", out); break;
        case '\n': std::fputs("\\n", out); break;
        case '\r': std::fputs("\\r", out); break;
        case '\\': std::fputs("\\\\", out); break;
        default: std::fputc(static_cast<unsigned char>(c), out); break;
        }
    }
}

void PrintEscaped(std::string_view text) { WriteEscaped(stderr, text); }

void WhyFilterLog(void*, std::string_view message) {
    std::fwrite(message.data(), 1, message.size(), stderr);
}

struct WhyContext {
    std::size_t lineBase = 0;
    std::size_t line = 1;
    // The candidate dump: every candidate of every word, with the one chosen. Each
    // line is line, chunk, sentence, token, start, surface, index, chosen, class
    // digit, part code, semantic codes, grammar codes, rule id, target.
    std::FILE* trace = nullptr;
};

void WhyLine(void* context, int line) {
    auto& why = *static_cast<WhyContext*>(context);
    why.line = why.lineBase + static_cast<std::size_t>(line) + 1;
    std::fprintf(stderr, "WHY-LINE\t%zu\n", why.line);
}

// The execution layer (docs/pipeline-layers.md) and the rule that made a choice.
std::string Decided(const hdTrans::word::Selection& s, bool ending) {
    using hdTrans::word::Provenance;
    switch (s.layer) {
    case Provenance::None: return "\t";
    case Provenance::Fixed: return "fixed\t";
    case Provenance::LocalRule: return std::string(ending ? "local-ending\t" : "local\t") + hdTrans::internal::RuleSource('L', s.id);
    case Provenance::GlobalRule: {
        std::string layer = "global-ending";
        if (!ending) {
            layer = "global-stem";
            for (const auto& g : hdTrans::internal::RulesProgram().globalRules) {
                if (g.prio == s.id && g.number > 0 && g.number < 1000) layer = "global-last";
            }
        }
        return layer + "\t" + hdTrans::internal::RuleSource('G', s.id);
    }
    case Provenance::Single:
    case Provenance::FirstXxx:
    case Provenance::Digit: return ending ? "ending-defaults\t" : "defaults\t";
    case Provenance::Sense:
    case Provenance::SenseDefault: return "senses\tsenses.tsv:" + std::to_string(s.id);
    case Provenance::Initial:
    case Provenance::Rewrite: return "rewrites\t";
    case Provenance::Scorer: return "scorers\t";
    case Provenance::Misread: return "misread\t";
    case Provenance::Cg: return "cg-lite\t";
    }
    return "\t";
}

void WhyResolved(void* context, int chunk, int sentence, const hdTrans::word::Chain& chain) {
    const auto& why = *static_cast<WhyContext*>(context);
    for (const hdTrans::word::Item* it = chain.head; it; it = it->next) {
        std::fprintf(stderr, "WHY\t%zu\t%d\t%d\t%d:%d\t", why.line, chunk, sentence, it->tokenIndex, it->start);
        PrintEscaped(it->group ? std::string_view(it->group->surface) : std::string_view{});
        std::fprintf(stderr, "\t%d\t%s\t%d\t", it->selected, hdTrans::word::ProvenanceName(it->choice.layer),
                     it->choice.id);
        PrintEscaped(it->EntryText(it->selected));
        std::fprintf(stderr, "\t%d\t%s\t%d\t%s\t%s", it->DirectIndex(),
                     hdTrans::word::ProvenanceName(it->directChoice.layer), it->directChoice.id,
                     Decided(it->choice, false).c_str(), Decided(it->directChoice, true).c_str());
        // The text of the chosen ending, which the rule checks need; empty without one.
        std::fputc('\t', stderr);
        if (const hdTrans::word::Group* ending = it->DirectGroup()) {
            if (const hdTrans::word::Entry* e = ending->At(it->DirectIndex())) PrintEscaped(e->dst);
        }
        std::fputc('\n', stderr);
        if (why.trace && it->group) {
            for (std::size_t c = 0; c < it->group->entries.size(); c++) {
                const hdTrans::word::Entry& e = *it->group->entries[c];
                std::fprintf(why.trace, "CAND\t%zu\t%d\t%d\t%d\t%d\t",
                             why.line, chunk, sentence, it->tokenIndex, it->start);
                WriteEscaped(why.trace, it->group->surface);
                std::fprintf(why.trace, "\t%zu\t%d\t%c\t", c,
                             static_cast<int>(c) == it->selected, e.cls ? e.cls : '-');
                WriteEscaped(why.trace, e.code);
                std::fputc('\t', why.trace);
                WriteEscaped(why.trace, e.attr);
                std::fputc('\t', why.trace);
                WriteEscaped(why.trace, e.gattr);
                std::fprintf(why.trace, "\t%d\t%s\t%s\n", e.ruleId,
                             e.fixed ? "fixed" : "", e.field.empty() ? "" : std::string(e.field).c_str());
            }
        }
        for (const hdTrans::word::ShadowChoice& shadow : it->shadowChoices) {
            std::fprintf(stderr, "SHADOW\t%zu\t%d\t%d\t%d:%d\t", why.line, chunk, sentence, it->tokenIndex,
                         it->start);
            PrintEscaped(shadow.scorer ? shadow.scorer : "");
            std::fprintf(stderr, "\t%d\t%d\t%d\t%d\t%d\t%d\t", shadow.from, shadow.candidate,
                         shadow.currentScore, shadow.score, shadow.eligible ? 1 : 0, shadow.winner ? 1 : 0);
            PrintEscaped(shadow.target);
            std::fputc('\n', stderr);
        }
    }
}

bool WriteText(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

bool WriteRuleCounts(const std::string& path, const hdTrans::rules::Program& program,
                     const hdTrans::rules::Tally& tally) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fputs("kind\tindex\tpumsa\tnumber\tugan\truns\tfires\n", f);
    for (std::size_t i = 0; i < tally.localRuns.size(); i++) {
        std::fprintf(f, "L\t%zu\t\t\t\t%llu\t%llu\n", i, static_cast<unsigned long long>(tally.localRuns[i]),
                     static_cast<unsigned long long>(tally.localFires[i]));
    }
    for (std::size_t i = 0; i < tally.globalRuns.size(); i++) {
        const hdTrans::rules::GlobalRule& r = program.globalRules[i];
        std::fprintf(f, "G\t%d\t%c\t%d\t%d\t%llu\t%llu\n", r.prio, static_cast<char>(r.pumsa), r.number,
                     r.uganRule ? 1 : 0, static_cast<unsigned long long>(tally.globalRuns[i]),
                     static_cast<unsigned long long>(tally.globalFires[i]));
    }
    return std::fclose(f) == 0;
}

}  // namespace

namespace {

int RunMain(int argc, char** argv) {
#ifdef _WIN32
    // The output bytes are the same on every platform: no text-mode CRLF translation.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    std::optional<std::string> dictsArg, snapshotOut;
    std::optional<std::vector<std::string>> userPathsArg;
    std::optional<std::string> ehndArg, configArg;
    std::string inPath, outPath, argumentText;
    bool hasText = false;
    bool json = false;
    bool version = false;
    bool literal = false;
    std::string ruleCountsPath;
    std::string dumpRulesPath;
    const char* dump = std::getenv("HDTRANS_DRIVER_DUMP");
    std::string stagesDir = dump ? dump : "";
    bool raw = false;
    bool each = false;
    bool why = false;
    std::string tracePath;
    bool selectShadow = false;
    bool misread = false;
    bool noCg = false;
    bool cgLearned = false;
    bool reference = false;
    bool verifyDicts = false;
    bool compileDicts = false;
    bool noCache = false;
    for (int i = 1; i < argc; i++) {
        std::string option = argv[i];
        if (!literal && option == "--") {
            literal = true;
            continue;
        }
        if (literal || option.empty() || option.front() != '-') {
            if (hasText) argumentText += ' ';
            argumentText += option;
            hasText = true;
            continue;
        }
        std::optional<std::string> value;
        if (option.starts_with("--")) {
            const auto equal = option.find('=');
            if (equal != std::string::npos) {
                value = option.substr(equal + 1);
                option.resize(equal);
            }
            option.erase(0, 1);
        }
        if (option == "-file" || option == "-i") option = "-in";
        if (option == "-o") option = "-out";
        if (option == "-v") option = "-version";
        if (option == "-h") option = "-help";
        const bool takesValue = option == "-dicts" || option == "-config" || option == "-userdict"
            || option == "-in" || option == "-out" || option == "-ehnd" || option == "-stages"
            || option == "-rule-counts" || option == "-compile-snapshot" || option == "-trace"
            || option == "-dump-rules";
        if (takesValue && !value) {
            if (i + 1 == argc) {
                std::fprintf(stderr, "hdtrans: %s requires a value\n", argv[i]);
                return 2;
            }
            value = argv[++i];
        } else if (!takesValue && value) {
            std::fprintf(stderr, "hdtrans: %s does not take a value\n", option.c_str());
            return 2;
        }
        if (option == "-help") { Usage(stdout); return 0; }
        else if (option == "-version") version = true;
        else if (option == "-json") json = true;
        else if (option == "-dicts") dictsArg = *value;
        else if (option == "-config") configArg = *value;
        else if (option == "-userdict") {
            if (!userPathsArg) userPathsArg.emplace();
            userPathsArg->push_back(*value);
        }
        else if (option == "-in") inPath = *value;
        else if (option == "-out") outPath = *value;
        else if (option == "-ehnd") ehndArg = *value;
        else if (option == "-stages") stagesDir = *value;
        else if (option == "-rule-counts") ruleCountsPath = *value;
        else if (option == "-dump-rules") dumpRulesPath = *value;
        else if (option == "-raw") raw = true;
        else if (option == "-each") each = true;
        else if (option == "-reference") reference = true;
        else if (option == "-verify-dicts") verifyDicts = true;
        else if (option == "-compile-dicts") compileDicts = true;
        else if (option == "-compile-snapshot") snapshotOut = *value;
        else if (option == "-no-cache") noCache = true;
        else if (option == "-why") why = true;
        else if (option == "-trace") tracePath = *value;
        else if (option == "-select-shadow") selectShadow = true;
        else if (option == "-misread") misread = true;
        else if (option == "-no-cg") noCg = true;
        else if (option == "-cg-learned") cgLearned = true;
        else {
            std::fprintf(stderr, "hdtrans: unknown option %s\n", argv[i]);
            Usage(stderr);
            return 2;
        }
    }
    if (hasText && !inPath.empty()) {
        std::fputs("hdtrans: text arguments and --in cannot be combined\n", stderr);
        return 2;
    }

    const std::string programDir = ExecutableDir();
    const std::string configPath = configArg ? *configArg : hdTrans::dict::ConfigFile(programDir);
    const auto config = hdTrans::internal::LoadConfig(configPath);
    if (configArg && !config.found && config.diagnostics.empty()) {
        std::fprintf(stderr, "hdtrans: cannot open config file %s\n", configPath.c_str());
        return 1;
    }
    for (const auto& diagnostic : config.diagnostics) {
        std::fprintf(stderr, "hdtrans: %s:%zu: %s\n", configPath.c_str(), diagnostic.line, diagnostic.message.c_str());
    }
    if (!userPathsArg && config.settings.userdict) userPathsArg = config.settings.userdict;
    if (!ehndArg && config.settings.ehnd) ehndArg = config.settings.ehnd;
    reference = reference || config.settings.reference;
    misread = misread || config.settings.misread;
    noCg = noCg || !config.settings.cg;
    hdTrans::internal::ConfigCache configCache;
    if (!configCache.Apply(config.settings.cache)) {
        std::fprintf(stderr, "hdtrans: cannot set cache directory\n");
        return 1;
    }

    if (static_cast<int>(compileDicts) + static_cast<int>(snapshotOut.has_value()) + static_cast<int>(noCache) > 1) {
        std::fprintf(stderr, "hdtrans: -compile-dicts, -compile-snapshot and -no-cache cannot be combined\n");
        return 2;
    }
    if (noCache) {
#ifdef _WIN32
        const int error = _putenv_s("HDTRANS_CACHE_DIR", "off");
#else
        const int error = setenv("HDTRANS_CACHE_DIR", "off", 1);
#endif
        if (error != 0) {
            std::fprintf(stderr, "hdtrans: cannot disable caching\n");
            return 1;
        }
    }
    if (hdTrans::dict::internal::RelativeCacheOverride()) {
        std::fprintf(stderr, "hdtrans: warning: relative HDTRANS_CACHE_DIR disables dictionary caching\n");
        if (compileDicts) return 1;
    }
    std::string dicts = dictsArg ? *dictsArg : hdTrans::dict::DefaultFolder(programDir);
    const auto userPaths = userPathsArg.value_or(hdTrans::dict::UserPaths(programDir));
    const auto folders = !dictsArg && config.settings.dicts ? *config.settings.dicts : SplitComma(dicts);
    if (!dictsArg && config.settings.dicts) {
        dicts.clear();
        for (const auto& folder : folders) dicts += (dicts.empty() ? "" : ",") + folder;
    }
    if (snapshotOut) {
        if (!hdTrans::dict::CompileSnapshot(folders, *snapshotOut)) {
            std::fprintf(stderr, "hdtrans: cannot compile the dictionary snapshot; a column dictionary must load successfully\n");
            return 1;
        }
        std::printf("%s\n", snapshotOut->c_str());
        return 0;
    }
    if (compileDicts) {
        const auto path = hdTrans::dict::internal::SnapshotPath(folders);
        if (path.empty()) {
            std::fprintf(stderr, "hdtrans: dictionary caching is disabled or no cache directory is available\n");
            return 1;
        }
        const auto previous = hdTrans::dict::internal::SnapshotResourcePath(path);
        if (!hdTrans::dict::internal::CompileCache(folders)) {
            std::fprintf(stderr, "hdtrans: cannot compile dictionary cache; a column dictionary must load successfully\n");
            return 1;
        }
        const auto resource = hdTrans::dict::internal::SnapshotResourcePath(path);
        if (!previous.empty() && previous == resource) {
            std::printf("up to date\n");
            return 0;
        }
        const auto checked = hdTrans::dict::LoadFolders(folders, {}, true, false);
        if (!checked.snapshot || !checked.snapshot->Mapped() || !checked.snapshot->Healthy()) {
            std::fprintf(stderr, "hdtrans: cannot reload the compiled dictionary cache\n");
            return 1;
        }
        std::printf("%s\n", resource.c_str());
        return 0;
    }
    if (why) std::setvbuf(stderr, nullptr, _IOFBF, 1 << 16);

    hdTrans::Engine engine(reference);
    if (!engine.OpenUserDicts(folders, verifyDicts, userPaths)) {
        std::fprintf(stderr, "hdtrans: no dictionary loaded from %s\n", dicts.c_str());
        // The reason a folder or a snapshot file did not load, one line per file.
        const std::string reason = engine.UserFileError();
        if (!reason.empty()) std::fprintf(stderr, "%s\n", reason.c_str());
        return 1;
    }
    if (const std::string error = engine.UserFileError(); !error.empty()) {
        const bool single = userPathsArg && hdTrans::dict::UserFiles(userPaths).size() == 1;
        std::fprintf(stderr, "hdtrans: %suser dictionary not loaded:\n%s\n", single ? "" : "warning: ",
                     error.c_str());
        if (single) return 1;
    }
    const std::string ehnd = ehndArg ? *ehndArg : config.settings.useEhnd ? hdTrans::dict::EhndFolder(programDir) : "";
    if (!ehnd.empty() && !engine.OpenEhnd(ehnd)) {
        std::fprintf(stderr, "hdtrans: cannot read the Ehnd folder %s\n", ehnd.c_str());
        return 1;
    }
    for (const std::string& e : engine.FilterErrors()) std::fprintf(stderr, "hdtrans: %s\n", e.c_str());

    if (version) {
        const std::string dictionaryVersion = hdTrans::frontend::DictionaryVersion(folders);
        std::printf("hdTrans v%s (native) / dict %s\n", HDTRANS_VERSION, dictionaryVersion.c_str());
        return 0;
    }

    std::string input = argumentText;
    if (!hasText) {
        if (inPath.empty() || inPath == "-") {
            if (!ReadAll(stdin, input)) return 1;
        } else {
            std::FILE* f = OpenFile(inPath, false);
            const bool read = f && ReadAll(f, input);
            if (f) std::fclose(f);
            if (!read) {
                std::fprintf(stderr, "hdtrans: cannot read %s\n", inPath.c_str());
                return 1;
            }
        }
    }

    const auto snapshot = engine.Dictionaries();
    const auto generated = hdTrans::internal::NewGenerated(*snapshot);
    const hdTrans::word::Scorer diagnosticScorers[] = {{"shortest-target", hdTrans::word::ShortestTargetScore}};
    const auto filters = engine.Filters();
    hdTrans::internal::TranslationOptions options;
    options.reference = reference;
    options.filters = filters.get();
    options.wordImprovements.misreadFilter = misread;
    if (noCg) options.wordImprovements.cgRules = false;
    options.wordImprovements.cgLearned = cgLearned;
    if (selectShadow) {
        options.shadowScorers = diagnosticScorers;
        options.selectShadow = true;
    }
    WhyContext whyContext;
    hdTrans::internal::StageObserver observer;
    std::FILE* traceFile = nullptr;
    struct TraceCloser {
        std::FILE* file = nullptr;
        ~TraceCloser() { if (file) std::fclose(file); }
    } traceCloser;
    if (!tracePath.empty()) {
        traceFile = std::fopen(tracePath.c_str(), "wb");
        if (!traceFile) {
            std::fprintf(stderr, "cannot write %s\n", tracePath.c_str());
            return 2;
        }
        traceCloser.file = traceFile;
        std::setvbuf(traceFile, nullptr, _IOFBF, 1 << 16);
        whyContext.trace = traceFile;
        observer.context = &whyContext;
        observer.line = WhyLine;
        observer.resolved = WhyResolved;
    }
    if (why) {
        observer.context = &whyContext;
        observer.line = WhyLine;
        observer.resolved = WhyResolved;
        observer.filterLog = WhyFilterLog;
    }
    hdTrans::internal::TraceCollector trace;
    if (json) observer = hdTrans::internal::TraceObserver(trace, observer);
    stagerecords::Collector collector;
    const bool stages = !stagesDir.empty();
    collector.families = &generated->families;
    if (stages) observer = stagerecords::ObserverFor(collector, observer);
    const hdTrans::rules::Program& program = hdTrans::internal::RulesProgram();
    hdTrans::rules::Tally tally(program);
    if (!ruleCountsPath.empty()) options.ruleTally = &tally;
    std::string output;
    auto emit = [&](std::string_view text) {
        if (json || !outPath.empty()) output.append(text);
        else std::fwrite(text.data(), 1, text.size(), stdout);
    };
    auto finish = [&] {
        if (stages && !stagerecords::Dump(stagesDir, collector)) return 1;
        if (!dumpRulesPath.empty()) {
            const std::string rules = hdTrans::internal::RuleProgramText(*snapshot);
            if (rules.empty() || !WriteText(dumpRulesPath, rules)) {
                std::fprintf(stderr, "hdtrans: cannot write %s\n", dumpRulesPath.c_str());
                return 1;
            }
        }
        if (!ruleCountsPath.empty() && !WriteRuleCounts(ruleCountsPath, program, tally)) {
            std::fprintf(stderr, "hdtrans: cannot write %s\n", ruleCountsPath.c_str());
            return 1;
        }
        if (json) {
            trace.result.text = std::move(output);
            output = hdTrans::cli::TraceJson(trace.result);
            output += '\n';
        }
        if (!outPath.empty()) {
            std::FILE* file = OpenFile(outPath, true);
            if (!file) {
                std::fprintf(stderr, "hdtrans: cannot write %s\n", outPath.c_str());
                return 1;
            }
            const bool written = std::fwrite(output.data(), 1, output.size(), file) == output.size();
            const bool closed = std::fclose(file) == 0;
            if (!written || !closed) {
                std::fprintf(stderr, "hdtrans: cannot write %s\n", outPath.c_str());
                return 1;
            }
        } else if (json) {
            std::fwrite(output.data(), 1, output.size(), stdout);
        }
        return std::fflush(stdout) == 0 ? 0 : 1;
    };

    if (!raw && !each) {
        std::vector<std::string> lines;
        size_t start = 0;
        for (;;) {
            const size_t end = input.find('\n', start);
            lines.push_back(input.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        hdTrans::internal::Memory memory;
        const std::vector<std::string> translated =
            hdTrans::internal::TranslateDocument(*snapshot, *generated, lines, memory, observer, options);
        for (size_t i = 0; i < translated.size(); i++) {
            if (stages) stagerecords::AddOutput(collector, static_cast<int>(i) + 1, translated[i]);
            if (i > 0) emit("\n");
            emit(translated[i]);
        }
        return finish();
    }
    size_t start = 0;
    while (start < input.size()) {
        size_t end = input.find('\n', start);
        if (end == std::string::npos) end = input.size();
        const std::string_view line(input.data() + start, end - start);
        std::string out;
        if (raw) {
            out = hdTrans::internal::TranslateRawLine(*snapshot, *generated, line, observer, options);
        } else {
            const std::string lines[] = {std::string(line)};
            hdTrans::internal::Memory memory;
            out = hdTrans::internal::TranslateDocument(*snapshot, *generated, lines, memory, observer, options).front();
        }
        if (stages) {
            stagerecords::AddOutput(collector, collector.caseBase + 1, out);
            collector.caseBase++;
        }
        emit(out);
        emit("\n");
        whyContext.lineBase++;
        trace.lineBase++;
        trace.line = trace.lineBase;
        start = end + 1;
    }
    return finish();
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; i++) {
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (size == 0) return 2;
        std::string text(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, text.data(), size, nullptr, nullptr) == 0) return 2;
        text.pop_back();
        storage.push_back(std::move(text));
    }
    std::vector<char*> arguments;
    for (auto& text : storage) arguments.push_back(text.data());
    arguments.push_back(nullptr);
    return RunMain(argc, arguments.data());
}
#else
int main(int argc, char** argv) {
    return RunMain(argc, argv);
}
#endif
