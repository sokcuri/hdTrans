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
#include "core/filter/filter.h"
#include "core/engine/user_watch.h"
#include "core/engine/load_info.h"
#include "frontend/load_info.h"
#include "core/output/families.h"
#include "core/word/constructions.h"
#include "core/word/select.h"
#include "stage_records.h"
#include "trace_json.h"
#include "frontend/dictionary_version.h"
#ifdef HDTRANS_POC08_HOOKS
#include "bridge/bridge.h"
#include "bridge/rule.h"
#endif

namespace {

namespace stagerecords = hdTrans::cli::stagerecords;

#ifdef HDTRANS_POC08_HOOKS
// The observer of --poc08 rule: it records what 0.7.x resolved and cleaned per line for the retrofit, and
// passes every stage on to the observer the other options set up.
struct RuleWatch {
    hdTrans::internal::StageObserver inner;
    hd08::bridge::RuleCapture capture;
};
RuleWatch& Watch(void* c) { return *static_cast<RuleWatch*>(c); }
hdTrans::internal::StageObserver RuleObserver(RuleWatch& w, const hdTrans::internal::StageObserver& inner) {
    using hdTrans::internal::StageObserver;
    w.inner = inner;
    StageObserver o;
    o.context = &w;
    o.line = [](void* c, int line) {
        Watch(c).capture.Line(line);
        if (Watch(c).inner.line) Watch(c).inner.line(Watch(c).inner.context, line);
    };
    o.resolved = [](void* c, int chunk, int sentence, const hdTrans::word::Chain& chain) {
        Watch(c).capture.Resolved(chunk, sentence, chain);
        if (Watch(c).inner.resolved) Watch(c).inner.resolved(Watch(c).inner.context, chunk, sentence, chain);
    };
    o.cleaned = [](void* c, const hdTrans::output::Pieces& pieces) {
        Watch(c).capture.Cleaned(pieces);
        if (Watch(c).inner.cleaned) Watch(c).inner.cleaned(Watch(c).inner.context, pieces);
    };
    if (inner.text)
        o.text = [](void* c, std::string_view tag, std::string_view text) { Watch(c).inner.text(Watch(c).inner.context, tag, text); };
    if (inner.pieces)
        o.pieces = [](void* c, std::string_view tag, int chunk, const hdTrans::output::Pieces& pieces) {
            Watch(c).inner.pieces(Watch(c).inner.context, tag, chunk, pieces);
        };
    if (inner.tokens)
        o.tokens = [](void* c, int chunk, const hdTrans::tokenizer::Tokens& tokens) {
            Watch(c).inner.tokens(Watch(c).inner.context, chunk, tokens);
        };
    if (inner.analysis)
        o.analysis = [](void* c, int chunk, const hdTrans::tokenizer::Tokens& tokens, const hdTrans::word::Analysis& a) {
            Watch(c).inner.analysis(Watch(c).inner.context, chunk, tokens, a);
        };
    if (inner.composed)
        o.composed = [](void* c, int chunk, int sentence, const hdTrans::output::Pieces& pieces) {
            Watch(c).inner.composed(Watch(c).inner.context, chunk, sentence, pieces);
        };
    if (inner.restored)
        o.restored = [](void* c, const hdTrans::pipeline::RestoreSteps& steps) {
            Watch(c).inner.restored(Watch(c).inner.context, steps);
        };
    if (inner.filterLog)
        o.filterLog = [](void* c, std::string_view message) { Watch(c).inner.filterLog(Watch(c).inner.context, message); };
    return o;
}
#endif

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
    std::fprintf(file, "hdTrans v%s\n", HDTRANS_VERSION_TEXT);
    std::fputs("Author: Sokcuri (https://sokcuri.neko.kr/)\n\n"
               "usage: hdtrans [options] [text]\n"
               "  --in FILE, --file FILE  read UTF-8 input (default: stdin)\n"
               "  --out FILE             write UTF-8 output (default: stdout)\n"
               "  --json                 return text, choices and logs as JSON\n"
               "  --why                  write selection and filter logs to stderr\n"
               "  --trace FILE           write every candidate of every word to FILE\n"
               "  --version              load dictionaries and show engine, runtime and dictionary versions\n"
               "  --verbose              write loaded dictionary information to stderr (except with --version)\n"
               "  --help                 show this help\n"
               "  --config FILE          settings file\n"
               "  --dicts DIR,...        dictionary paths in load order\n"
               "  --userdict PATH        user dictionary file or folder (repeatable)\n"
               "  --ehnd DIR             opt-in Ehnd folder\n"
               "  --each, --raw          each line on its own (as text hooks send it), or word-pipeline-only lines\n"
               "  --speech LEVEL         strict (default): keep -다 but where the Japanese marks casual speech;\n"
               "                         soft: 해체 in 「」 and on bare lines that read as speech\n"
               "  --wrapped              input lines break mid-sentence (captions, hooked text); the line count is kept\n"
               "  --context FILE         UTF-8 lines read before the input as context, not output\n"
               "  --reference, --misread, --no-cg, --cg-learned, --select-shadow\n"
               "  --verify-dicts, --compile-dicts, --compile-snapshot FILE, --no-cache\n"
               "  --stages DIR, --rule-counts FILE, --dump-rules FILE\n"
               "Legacy single-hyphen options are also accepted, except --verbose. Use -- before literal text starting with '-'.\n", file);
#ifdef HDTRANS_POC08_HOOKS
    std::fputs("\n0.8 proof of concept (hdtrans08):\n"
               "  --poc08 PARTS          stages the 0.8 components replace: normalize, segment, generate, decide, native,\n"
               "                         reselect (deciders choosing among 0.7.x's endings), all or none (default);\n"
               "                         a part whose component is not built stays 0.7.x\n"
               "  --speech auto|haera|hae|haeyo|hapsyo  speech level of the 0.8 generator\n"
               "  --poc08-data DIR       data root of the 0.8 components (default: the poc08 source folder)\n"
               "  --poc08-why FILE       --why records of the side IR (one per Korean morpheme)\n"
               "  --poc08-stages FILE    the side IR as JSON Lines (IR.md)\n"
               "  --poc08-misses FILE    the lift's missing table keys, most frequent first\n"
               "  --poc08-reselections FILE  the overrides of --poc08 reselect, realised or not\n"
               "  --poc08-rule-pre FILE  the retrofit's input lines of --poc08 rule (JSON Lines)\n"
               "  --poc08-stats          counts of the replaced stages to stderr\n", file);
#endif
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
    case Provenance::Initial: return "rewrites\t";
    case Provenance::Rewrite: return "rewrites\t" + std::string(hdTrans::word::ConstructionId(s.id));
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
    for (std::size_t i = 0; i < tally.constructionRuns.size(); i++) {
        const std::string_view id = hdTrans::word::ConstructionId(static_cast<int>(i));
        std::fprintf(f, "cx\t%.*s\t\t\t\t%llu\t%llu\n", static_cast<int>(id.size()), id.data(),
                     static_cast<unsigned long long>(tally.constructionRuns[i]),
                     static_cast<unsigned long long>(tally.constructionFires[i]));
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
    bool verbose = false;
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
    std::optional<std::string> speechArg;
    bool wrapped = false;
    std::string contextPath;
    bool noCg = false;
    bool cgLearned = false;
    bool reference = false;
    bool verifyDicts = false;
    bool compileDicts = false;
    bool noCache = false;
#ifdef HDTRANS_POC08_HOOKS
    std::string poc08Parts = "none", poc08Data, poc08Why, poc08Stages, poc08Misses, poc08Reselections, poc08RulePre;
    bool poc08Stats = false;
#endif
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
            || option == "-dump-rules" || option == "-speech" || option == "-context"
#ifdef HDTRANS_POC08_HOOKS
            || option == "-poc08" || option == "-poc08-data" || option == "-poc08-why" || option == "-poc08-stages"
            || option == "-poc08-misses" || option == "-poc08-reselections" || option == "-poc08-rule-pre"
#endif
            ;
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
        else if (option == "-verbose" && std::string_view(argv[i]) == "--verbose") verbose = true;
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
        else if (option == "-speech") speechArg = *value;
        else if (option == "-wrapped") wrapped = true;
        else if (option == "-context") contextPath = *value;
        else if (option == "-no-cg") noCg = true;
        else if (option == "-cg-learned") cgLearned = true;
#ifdef HDTRANS_POC08_HOOKS
        else if (option == "-poc08") poc08Parts = *value;
        else if (option == "-poc08-data") poc08Data = *value;
        else if (option == "-poc08-why") poc08Why = *value;
        else if (option == "-poc08-stages") poc08Stages = *value;
        else if (option == "-poc08-misses") poc08Misses = *value;
        else if (option == "-poc08-reselections") poc08Reselections = *value;
        else if (option == "-poc08-rule-pre") poc08RulePre = *value;
        else if (option == "-poc08-stats") poc08Stats = true;
#endif
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
    if ((wrapped || !contextPath.empty()) && (each || raw)) {
        std::fputs("hdtrans: --wrapped and --context cannot be combined with --each or --raw\n", stderr);
        return 2;
    }

    const std::string programDir = ExecutableDir();
    for (const auto& warning : hdTrans::dict::FolderWarnings(programDir))
        std::fprintf(stderr, "hdtrans: warning: %s\n", warning.c_str());
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
    wrapped = wrapped || config.settings.wrapped;
    hdTrans::Speech speech = config.settings.speech;
#ifdef HDTRANS_POC08_HOOKS
    // The levels only the 0.8 generator writes; 0.7.x composes with its default.
    hd08::Speech speech08 = hd08::Speech::Strict;
    bool speech08Only = false;  // a level only the 0.8 generator writes
    if (speechArg) {
        static const std::pair<const char*, hd08::Speech> kLevels[] = {
            {"strict", hd08::Speech::Strict}, {"soft", hd08::Speech::Soft}, {"auto", hd08::Speech::Auto},
            {"haera", hd08::Speech::Haera}, {"hae", hd08::Speech::Hae}, {"haeyo", hd08::Speech::Haeyo},
            {"hapsyo", hd08::Speech::Hapsyo}};
        for (const auto& [name, level] : kLevels) {
            if (*speechArg == name) speech08 = level;
        }
        if (*speechArg != "strict" && *speechArg != "soft") {
            bool known = false;
            for (const auto& [name, level] : kLevels) known = known || *speechArg == name;
            if (!known) {
                std::fprintf(stderr, "hdtrans: --speech takes soft, strict, auto, haera, hae, haeyo or hapsyo\n");
                return 2;
            }
            speechArg.reset();
            speech08Only = true;
        }
    }
#endif
    if (speechArg && !hdTrans::internal::SpeechOf(*speechArg, speech)) {
        std::fprintf(stderr, "hdtrans: --speech takes soft or strict\n");
        return 2;
    }
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
            for (const hdTrans::dict::LoadError& e : hdTrans::dict::LoadFoldersTsv(folders).errors) {
                if (e.kind != hdTrans::dict::LoadErrorKind::MarkerClass) continue;
                std::fprintf(stderr, "hdtrans: %s:%d: %s\n", e.file.c_str(), e.line, e.message.c_str());
            }
            std::fprintf(stderr, "hdtrans: cannot compile the dictionary snapshot; a column dictionary must load successfully "
                                 "and every marker family must expand\n");
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

    if (!dictsArg && !config.settings.dicts) {
        std::error_code error;
        const auto path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(dicts.data()), dicts.size()));
        if (!std::filesystem::exists(path, error) && !error) {
            std::fprintf(stderr, "hdtrans: %s을 찾을 수 없습니다\n", dicts.c_str());
            return 1;
        }
    }
    const char* verifyEnv = std::getenv("HDTRANS_VERIFY_DICTS");
    verifyDicts = verifyDicts || (verifyEnv && std::string_view(verifyEnv) == "1");
    const hdTrans::EngineOptions dictionaryOptions{
        verifyDicts ? std::optional<bool>(true) : std::nullopt, false, configPath};
    hdTrans::Engine engine(reference, hdTrans::CacheWrite::Sync, hdTrans::UserReload::Off, dictionaryOptions);
    const std::string ehnd = ehndArg ? *ehndArg : config.settings.useEhnd ? hdTrans::dict::EhndFolder(programDir) : "";
    std::vector<hdTrans::dict::UserSource> userSources;
    for (const auto& path : userPaths)
        userSources.push_back({path, false, false, userPathsArg.has_value()});
    const hdTrans::dict::UserSource ehndSource{ehnd, true, false, ehndArg.has_value()};
    static_cast<void>(hdTrans::internal::OpenEhndSource(engine, ehndSource, config));
    bool loaded = hdTrans::internal::OpenUserSources(engine, folders, verifyDicts, userSources, config);
    if (verifyDicts) {
        for (const auto& error : hdTrans::internal::UserFileErrors(engine))
            if (hdTrans::dict::FailsLoad(error.kind)) loaded = false;
        if (hdTrans::internal::FilterFileFailed(engine)) loaded = false;
    }
    if (!loaded) {
        std::fprintf(stderr, "hdtrans: no dictionary loaded from %s\n", dicts.c_str());
        // The reason a folder or a snapshot file did not load, one line per file.
        const std::string reason = engine.UserFileError();
        if (!reason.empty()) std::fprintf(stderr, "%s\n", reason.c_str());
        for (const std::string& error : engine.FilterErrors()) std::fprintf(stderr, "hdtrans: %s\n", error.c_str());
        return 1;
    }
    if (const std::string error = engine.UserFileError(); !error.empty())
        std::fprintf(stderr, "hdtrans: warning: user dictionary lines skipped:\n%s\n", error.c_str());
    for (const std::string& error : engine.FilterErrors()) std::fprintf(stderr, "hdtrans: warning: %s\n", error.c_str());
    if (verifyDicts) {
        // Verification also fails on loaded lines whose marker family does not fit their part code
        // and on negations in 지 않 that the class of their key does not account for, and on noun lines
        // whose target is a verb made a noun, and on lines whose target holds kana or kanji.
        const auto checked = hdTrans::dict::LoadFolders(folders, {}, true, false, hdTrans::dict::UserFiles(userPaths));
        std::size_t failed = 0;
        for (const hdTrans::dict::LoadError& e : checked.errors) {
            if (e.kind != hdTrans::dict::LoadErrorKind::MarkerClass &&
                e.kind != hdTrans::dict::LoadErrorKind::MissingRuleBodies &&
                e.kind != hdTrans::dict::LoadErrorKind::NegationEnding &&
                e.kind != hdTrans::dict::LoadErrorKind::VerbNoun &&
                e.kind != hdTrans::dict::LoadErrorKind::UserRow &&
                e.kind != hdTrans::dict::LoadErrorKind::JapaneseTarget)
                continue;
            std::fprintf(stderr, "hdtrans: %s:%d: %s\n", e.file.c_str(), e.line, e.message.c_str());
            failed++;
        }
        if (failed > 0) {
            std::fprintf(stderr, "hdtrans: %zu dictionary lines fail verification\n", failed);
            return 1;
        }
    }
    if (!version && (verbose || verifyDicts)) {
        for (const auto& info : hdTrans::internal::LoadedSourceInfo(engine).files)
            std::fprintf(stderr, "%s\n", hdTrans::frontend::FormatLoadFileInfo(info, programDir).c_str());
    }
    if (version) {
        const std::string dictionaryVersion = hdTrans::frontend::DictionaryVersion(folders);
        std::printf("hdTrans v%s (native) / dict %s\n", HDTRANS_VERSION_TEXT, dictionaryVersion.c_str());
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

    std::string context;
    if (!contextPath.empty()) {
        std::FILE* f = OpenFile(contextPath, false);
        const bool read = f && ReadAll(f, context);
        if (f) std::fclose(f);
        if (!read) {
            std::fprintf(stderr, "hdtrans: cannot read %s\n", contextPath.c_str());
            return 1;
        }
        if (context.ends_with('\n')) context.pop_back();
    }

    const auto snapshot = engine.Dictionaries();
    const auto generated = hdTrans::internal::NewGenerated(*snapshot);
    const hdTrans::word::Scorer diagnosticScorers[] = {{"shortest-target", hdTrans::word::ShortestTargetScore}};
    const auto filters = engine.Filters();
    hdTrans::internal::TranslationOptions options;
    options.reference = reference;
    options.filters = filters.get();
    options.wordImprovements.misreadFilter = misread;
    options.speech = speech;
    options.wrapped = wrapped;
#ifdef HDTRANS_POC08_HOOKS
    hd08::bridge::Options poc08Options;
    poc08Options.cfg.data = poc08Data.empty() ? std::string(HD08_DEFAULT_DATA) : poc08Data;
    poc08Options.cfg.speech = speech08;
    poc08Options.cfg.wrapped = wrapped;
    poc08Options.cfg.document = !each;
    if (userPathsArg) poc08Options.cfg.userdicts = *userPathsArg;
    // the native route reads the source line alone and writes its own text: with mid-sentence line breaks, filters
    // or user dictionaries 0.7.x keeps every line
    poc08Options.nativeHeld = wrapped || userPathsArg.has_value() ||
                              (filters && (filters->Rows(hdTrans::filter::Stage::Pre) > 0 ||
                                           filters->Rows(hdTrans::filter::Stage::Post) > 0 || filters->User() != nullptr));
    poc08Options.sideIr = !poc08Why.empty() || !poc08Stages.empty();
    poc08Options.lift = !poc08Misses.empty() || poc08Stats;
    {
        std::string error;
        if (!hd08::bridge::ParseParts(poc08Parts, poc08Options, error)) {
            std::fprintf(stderr, "hdtrans: --poc08: %s\n", error.c_str());
            return 2;
        }
    }
    struct BridgeCloser {
        hd08::bridge::Bridge* bridge = nullptr;
        ~BridgeCloser() { hd08::bridge::Destroy(bridge); }
    } poc08;
    const bool poc08On = poc08Parts != "none" || poc08Options.sideIr || !poc08Misses.empty() || poc08Stats;
    if (poc08On) {
        std::string error;
        poc08.bridge = hd08::bridge::Create(poc08Options, error);
        if (poc08.bridge == nullptr) {
            std::fprintf(stderr, "hdtrans: cannot load the 0.8 components: %s\n", error.c_str());
            return 1;
        }
        options.hooks = hd08::bridge::HooksOf(poc08.bridge);
    }
    if (speech08Only && (poc08.bridge == nullptr ||
                         hd08::bridge::ActiveParts(poc08.bridge).find("generate") == std::string::npos)) {
        std::fputs("hdtrans: warning: this --speech level is written only by the 0.8 generator, which is off\n",
                   stderr);
    }
    RuleWatch ruleWatch;
    std::shared_ptr<const hd08::bridge::RuleData> ruleData;
    hd08::bridge::RuleCounts ruleCounts;
    std::string rulePre;
    std::string* const rulePreOut = poc08RulePre.empty() ? nullptr : &rulePre;
    const auto finish08 = [&]() -> bool {
        if (poc08.bridge == nullptr) return true;
        auto write = [](const std::string& path, const std::string& text) {
            if (path.empty()) return true;
            std::FILE* f = OpenFile(path, true);
            const bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
            const bool closed = f && std::fclose(f) == 0;
            if (!ok || !closed) std::fprintf(stderr, "hdtrans: cannot write %s\n", path.c_str());
            return ok && closed;
        };
        bool ok = write(poc08Why, hd08::bridge::TakeWhy(poc08.bridge));
        ok = write(poc08Stages, hd08::bridge::TakeStages(poc08.bridge)) && ok;
        ok = write(poc08Misses, hd08::bridge::LiftMisses(poc08.bridge, 100000)) && ok;
        ok = write(poc08Reselections, hd08::bridge::TakeReselections(poc08.bridge)) && ok;
        ok = write(poc08RulePre, rulePre) && ok;
        if (poc08Stats && ruleData)
            std::fprintf(stderr,
                         "hdtrans08 rule: lines=%llu caption_docs=%llu dialogue=%llu keigo=%llu read=%llu replaced=%llu "
                         "same_level=%llu mixed=%llu\n",
                         static_cast<unsigned long long>(ruleCounts.lines), static_cast<unsigned long long>(ruleCounts.captionDocs),
                         static_cast<unsigned long long>(ruleCounts.dialogue), static_cast<unsigned long long>(ruleCounts.keigo),
                         static_cast<unsigned long long>(ruleCounts.read), static_cast<unsigned long long>(ruleCounts.replaced),
                         static_cast<unsigned long long>(ruleCounts.sameLevel), static_cast<unsigned long long>(ruleCounts.mixed));
        if (poc08Stats) {
            const hd08::bridge::Counts c = hd08::bridge::CountsOf(poc08.bridge);
            const std::string parts = hd08::bridge::ActiveParts(poc08.bridge);
            std::fprintf(stderr,
                         "hdtrans08 poc08: parts=%s sentences=%llu generated=%llu kept=%llu lifted_items=%llu "
                         "failed_items=%llu normalized_lines=%llu segmented_chunks=%llu refits=%llu points=%llu "
                         "overrides=%llu reselected=%llu native=%llu\n",
                         parts.empty() ? "none" : parts.c_str(), static_cast<unsigned long long>(c.sentences),
                         static_cast<unsigned long long>(c.generated), static_cast<unsigned long long>(c.kept),
                         static_cast<unsigned long long>(c.liftedItems), static_cast<unsigned long long>(c.failedItems),
                         static_cast<unsigned long long>(c.normalized), static_cast<unsigned long long>(c.segmentedChunks),
                         static_cast<unsigned long long>(c.refits), static_cast<unsigned long long>(c.points),
                         static_cast<unsigned long long>(c.overrides), static_cast<unsigned long long>(c.reselected),
                         static_cast<unsigned long long>(c.native));
        }
        return ok;
    };
#endif
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
#ifdef HDTRANS_POC08_HOOKS
    if (poc08.bridge != nullptr && (ruleData = hd08::bridge::RuleDataOf(poc08.bridge))) observer = RuleObserver(ruleWatch, observer);
#endif
    // The local rules are the dictionary rows' bodies, so the tally counts them only
    // against the program the dictionary assembled.
    const hdTrans::rules::Program& program =
        generated->hasRules ? generated->rules : hdTrans::internal::RulesProgram();
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
        const auto addLines = [&lines](const std::string& text) {
            size_t start = 0;
            for (;;) {
                const size_t end = text.find('\n', start);
                lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        };
        if (!context.empty()) addLines(context);
        const size_t contextLines = lines.size();
        addLines(input);
#ifdef HDTRANS_POC08_HOOKS
        std::vector<std::string> translated =
            hdTrans::internal::TranslateDocument(*snapshot, *generated, lines, observer, options, contextLines);
        if (poc08.bridge != nullptr) hd08::bridge::ApplyNative(poc08.bridge, lines, contextLines, translated);
        if (ruleData) {
            const std::vector<std::string> source(lines.begin() + static_cast<std::ptrdiff_t>(contextLines), lines.end());
            hd08::bridge::RuleRewrite(*ruleData, source, ruleWatch.capture, translated, ruleCounts, rulePreOut,
                                     poc08.bridge);
        }
#else
        const std::vector<std::string> translated =
            hdTrans::internal::TranslateDocument(*snapshot, *generated, lines, observer, options, contextLines);
#endif
        for (size_t i = 0; i < translated.size(); i++) {
            if (stages) stagerecords::AddOutput(collector, static_cast<int>(i) + 1, translated[i]);
            if (i > 0) emit("\n");
            emit(translated[i]);
        }
#ifdef HDTRANS_POC08_HOOKS
        if (!finish08()) return 1;
#endif
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
#ifdef HDTRANS_POC08_HOOKS
            if (ruleData) ruleWatch.capture.Clear();
#endif
            out = hdTrans::internal::TranslateDocument(*snapshot, *generated, lines, observer, options).front();
#ifdef HDTRANS_POC08_HOOKS
            if (poc08.bridge != nullptr) {
                std::vector<std::string> one{out};
                hd08::bridge::ApplyNative(poc08.bridge, lines, 0, one);
                out = one.front();
            }
            if (ruleData) {
                std::vector<std::string> one{out};
                hd08::bridge::RuleRewrite(*ruleData, {std::string(line)}, ruleWatch.capture, one, ruleCounts, rulePreOut,
                                         poc08.bridge);
                out = one.front();
            }
#endif
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
#ifdef HDTRANS_POC08_HOOKS
    if (!finish08()) return 1;
#endif
    return finish();
}

// status, after reporting the marker families that reached the output unexpanded.
int ReportUnexpanded(int status) {
    if (const std::uint64_t n = hdTrans::output::UnexpandedMarkers(); n > 0) {
        std::fprintf(stderr, "hdtrans: internal error: %llu marker families reached the output unexpanded\n",
                     static_cast<unsigned long long>(n));
    }
    return status;
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
    return ReportUnexpanded(RunMain(argc, arguments.data()));
}
#else
int main(int argc, char** argv) {
    return ReportUnexpanded(RunMain(argc, argv));
}
#endif
