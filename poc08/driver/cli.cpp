// hdtrans08: the 0.8 PoC CLI. Flags mean what they mean in 0.7.x; a 0.7.x flag the PoC does not
// implement exits with 2.
#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "driver.h"
#include "ir/file.h"
#include "ir/ir_json.h"
#include "report.h"

#ifndef HD08_DEFAULT_DATA
#define HD08_DEFAULT_DATA "poc08"
#endif
#ifndef HD08_VERSION
#define HD08_VERSION "0.8.0-poc"
#endif

namespace {

using namespace hd08;

void Usage(std::FILE* f) {
    std::fputs("hdtrans08 " HD08_VERSION " (0.8 proof of concept)\n\n"
               "usage: hdtrans08 [options] [text]\n"
               "  --in FILE, --file FILE  read UTF-8 input (default: stdin)\n"
               "  --out FILE             write UTF-8 output (default: stdout)\n"
               "  --json                 write one JSON object: text and per-line risk, evidence, flags, coverage\n"
               "  --why                  write --why JSON Lines (one record per Korean morpheme) to stderr\n"
               "  --trace FILE           write the --why records of every line to FILE\n"
               "  --stages DIR           write DIR/ir.jsonl with every stage's fields\n"
               "  --toks FILE            read lines and their tokens from a toks.jsonl file (skips N and A)\n"
               "  --dicts DIR            data root (default: the poc08 source folder)\n"
               "  --userdict PATH        user dictionary file or folder (repeatable)\n"
               "  --each                 each line on its own\n"
               "  --wrapped              input lines break mid-sentence; the line count is kept\n"
               "  --context FILE         UTF-8 lines read before the input as context, not output\n"
               "  --speech LEVEL         strict (default), soft, auto, or a level: haera hae haeyo hapsyo\n"
               "  --threads N            with --each, translate on N threads (output order is kept)\n"
               "  --stats                write timing per stage, lines/s and startup time to stderr\n"
               "  --version, --help\n",
               f);
}

bool ReadFile(const std::string& path, std::string& out) {
    if (path != "-") return ReadWholeFile(path, out);
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0) out.append(buf, n);
    return std::ferror(stdin) == 0;
}

// Lines split at '\n' ('\r' before it dropped). A final newline ends the last line; it does not
// start an empty one.
std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == text.size()) break;
        start = end + 1;
    }
    if (!text.empty() && text.back() == '\n') lines.pop_back();
    return lines;
}

bool SpeechOf(std::string_view s, Speech& out) {
    static const std::pair<const char*, Speech> kSpeech[] = {
        {"strict", Speech::Strict}, {"soft", Speech::Soft},   {"auto", Speech::Auto},   {"haera", Speech::Haera},
        {"hae", Speech::Hae},       {"haeyo", Speech::Haeyo}, {"hapsyo", Speech::Hapsyo}};
    for (const auto& [name, value] : kSpeech) {
        if (s == name) {
            out = value;
            return true;
        }
    }
    return false;
}

struct Outputs {
    std::string text;   // the translation
    std::string json;   // --json line objects, comma-separated
    std::string why;    // --why / --trace records
    std::string stages; // --stages records
    uint64_t lines = 0, covered = 0, tracedBytes = 0, outBytes = 0, flagged = 0;
    bool started = false;  // a line was written (document mode puts '\n' between lines)
};

struct Wants {
    bool json = false, why = false, stages = false;
};

void Collect(const Engine& engine, const Doc& doc, const Wants& w, Outputs& o, bool each) {
    if (w.stages) o.stages += DocJson(doc) + '\n';
    for (const Line& line : doc.lines) {
        if (w.stages) o.stages += LineJson(doc, line) + '\n';
        if (line.context) continue;
        if (each) {
            o.text += line.out;
            o.text += '\n';
        } else {
            if (o.started) o.text += '\n';
            o.started = true;
            o.text += line.out;
        }
        const LineReport r = Report(engine, doc, line);
        o.lines++;
        o.covered += r.covered;
        o.tracedBytes += r.tracedBytes;
        o.outBytes += r.outBytes;
        o.flagged += !r.flags.empty();
        if (w.json) {
            if (!o.json.empty()) o.json += ",\n";
            o.json += LineSummaryJson(doc, line, r);
        }
        if (w.why) o.why += WhyRecords(engine, doc, line, r);
    }
}

// Drops what the stages after A write, so that a toks or ir.jsonl file can be run again.
void KeepAnalysis(Line& l) {
    std::vector<Flag> fallback;
    for (const Flag& f : l.flags)
        if (f.kind == flag::fallback) fallback.push_back(f);
    l.flags = std::move(fallback);
    l.utts.clear();
    l.mentions.clear();
    l.buns.clear();
    l.clauses.clear();
    l.kir.clear();
    l.reg.clear();
    l.lev.clear();
    l.kir2.clear();
    l.out.clear();
    l.spans.clear();
    l.decisions.clear();
}

bool WriteFile(const std::string& path, const std::string& text) {
    std::FILE* f = OpenFile(path, true);
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

int Run(int argc, char** argv) {
#ifdef _WIN32
    // Bytes in and out as they are: no CRLF translation, no end at 0x1A.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    const auto t0 = std::chrono::steady_clock::now();
    Config cfg;
    cfg.data = HD08_DEFAULT_DATA;
    std::string inPath, outPath, contextPath, stagesDir, tracePath, toksPath, text;
    bool hasText = false, json = false, why = false, each = false, stats = false, version = false;
    unsigned threads = 1;
    bool literal = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (literal || arg.empty() || arg[0] != '-' || arg == "-") {
            text += (hasText ? " " : "") + arg;
            hasText = true;
            continue;
        }
        if (arg == "--") {
            literal = true;
            continue;
        }
        std::string option = arg.substr(arg.starts_with("--") ? 2 : 1);
        std::optional<std::string> value;
        if (const size_t eq = option.find('='); eq != std::string::npos) {
            value = option.substr(eq + 1);
            option.resize(eq);
        }
        if (option == "file" || option == "i") option = "in";
        if (option == "o") option = "out";
        if (option == "h") option = "help";
        if (option == "v") option = "version";
        static const char* const kValued[] = {"in", "out", "dicts", "userdict", "speech", "context",
                                              "stages", "trace", "toks", "threads"};
        bool takesValue = false;
        for (const char* v : kValued) takesValue = takesValue || option == v;
        if (takesValue && !value) {
            if (i + 1 == argc) {
                std::fprintf(stderr, "hdtrans08: %s requires a value\n", arg.c_str());
                return 2;
            }
            value = argv[++i];
        } else if (!takesValue && value) {
            std::fprintf(stderr, "hdtrans08: %s does not take a value\n", arg.c_str());
            return 2;
        }
        if (option == "help") {
            Usage(stdout);
            return 0;
        } else if (option == "version") version = true;
        else if (option == "in") inPath = *value;
        else if (option == "out") outPath = *value;
        else if (option == "dicts") cfg.data = *value;
        else if (option == "userdict") cfg.userdicts.push_back(*value);
        else if (option == "context") contextPath = *value;
        else if (option == "stages") stagesDir = *value;
        else if (option == "trace") tracePath = *value;
        else if (option == "toks") toksPath = *value;
        else if (option == "json") json = true;
        else if (option == "why") why = true;
        else if (option == "each") each = true;
        else if (option == "wrapped") cfg.wrapped = true;
        else if (option == "stats") stats = true;
        else if (option == "no-cache") {}  // nothing is cached
        else if (option == "speech") {
            if (!SpeechOf(*value, cfg.speech)) {
                std::fputs("hdtrans08: --speech takes strict, soft, auto, haera, hae, haeyo or hapsyo\n", stderr);
                return 2;
            }
        } else if (option == "threads") {
            threads = static_cast<unsigned>(std::strtoul(value->c_str(), nullptr, 10));
            if (threads == 0 || threads > 256) {
                std::fputs("hdtrans08: --threads takes 1 to 256\n", stderr);
                return 2;
            }
        } else {
            static const char* const kNotYet[] = {"raw", "ehnd", "config", "reference", "misread", "no-cg",
                                                  "cg-learned", "select-shadow", "verify-dicts", "compile-dicts",
                                                  "compile-snapshot", "rule-counts", "dump-rules", "verbose"};
            for (const char* n : kNotYet) {
                if (option == n) {
                    std::fprintf(stderr, "hdtrans08: %s is not supported by the 0.8 proof of concept\n", arg.c_str());
                    return 2;
                }
            }
            std::fprintf(stderr, "hdtrans08: unknown option %s\n", arg.c_str());
            Usage(stderr);
            return 2;
        }
    }
    if (hasText && (!inPath.empty() || !toksPath.empty())) {
        std::fputs("hdtrans08: text arguments cannot be combined with --in or --toks\n", stderr);
        return 2;
    }
    if (!inPath.empty() && !toksPath.empty()) {
        std::fputs("hdtrans08: --in and --toks cannot be combined\n", stderr);
        return 2;
    }
    if ((cfg.wrapped || !contextPath.empty()) && each) {
        std::fputs("hdtrans08: --wrapped and --context cannot be combined with --each\n", stderr);
        return 2;
    }
    if (threads > 1 && !each) {
        std::fputs("hdtrans08: --threads needs --each\n", stderr);
        return 2;
    }
    cfg.document = !each;

    Engine engine;
    std::string err;
    if (!LoadEngine(cfg, engine, err)) {
        std::fprintf(stderr, "hdtrans08: cannot load %s: %s\n", cfg.data.c_str(), err.c_str());
        FreeEngine(engine);
        return 1;
    }
    const auto tLoaded = std::chrono::steady_clock::now();
    const std::string stubs = StubComponents();
    if (version) {
        std::printf("hdtrans08 %s / data %s / stubs: %s\n", HD08_VERSION, cfg.data.c_str(),
                    stubs.empty() ? "none" : stubs.c_str());
        FreeEngine(engine);
        return 0;
    }
#ifdef HD08_STUB_ANALYZER
    if (!cfg.userdicts.empty())
        std::fputs("hdtrans08: warning: --userdict is read by the analyzer, which is a stub in this build\n", stderr);
#endif

    // Build the documents.
    std::vector<Doc> docs;
    bool finalNewline = false;
    std::vector<std::string> eachLines;  // --each text input: one Doc per line, built when translated
    const bool fromToks = !toksPath.empty();
    if (fromToks) {
        std::string jsonl;
        if (!ReadFile(toksPath, jsonl)) {
            std::fprintf(stderr, "hdtrans08: cannot read %s\n", toksPath.c_str());
            FreeEngine(engine);
            return 1;
        }
        std::vector<Doc> read;
        if (!ReadJsonl(jsonl, read, err)) {
            std::fprintf(stderr, "hdtrans08: %s: %s\n", toksPath.c_str(), err.c_str());
            FreeEngine(engine);
            return 1;
        }
        for (Doc& d : read) {
            d.scenes.clear();
            d.speakers.clear();
            for (Line& l : d.lines) {
                KeepAnalysis(l);
                if (l.map.empty()) {
                    l.map.resize(l.norm.size() + 1);
                    for (uint32_t k = 0; k <= l.norm.size(); k++) l.map[k] = k;
                }
            }
            if (!each) {
                d.wrapped = cfg.wrapped;
                docs.push_back(std::move(d));
                continue;
            }
            for (Line& l : d.lines) {
                Doc one;
                one.id = d.id;
                one.ud = d.ud;
                one.ctypes = d.ctypes;
                one.cforms = d.cforms;
                // Re-intern the line's strings into the new document.
                const std::string rec = LineJson(d, l);
                Json j;
                std::string e;
                Line copy;
                if (!ParseJson(rec, j, e) || !ReadLine(j, one, copy, e)) {
                    std::fprintf(stderr, "hdtrans08: %s: %s\n", toksPath.c_str(), e.c_str());
                    FreeEngine(engine);
                    return 1;
                }
                if (copy.map.empty()) {
                    copy.map.resize(copy.norm.size() + 1);
                    for (uint32_t k = 0; k <= copy.norm.size(); k++) copy.map[k] = k;
                }
                one.lines.push_back(std::move(copy));
                docs.push_back(std::move(one));
            }
        }
    } else {
        std::string input = text;
        if (!hasText && !ReadFile(inPath.empty() ? "-" : inPath, input)) {
            std::fprintf(stderr, "hdtrans08: cannot read %s\n", inPath.empty() ? "stdin" : inPath.c_str());
            FreeEngine(engine);
            return 1;
        }
        std::vector<std::string> context;
        if (!contextPath.empty()) {
            std::string c;
            if (!ReadFile(contextPath, c)) {
                std::fprintf(stderr, "hdtrans08: cannot read %s\n", contextPath.c_str());
                FreeEngine(engine);
                return 1;
            }
            context = SplitLines(c);
        }
        // Like 0.7.x, document output ends with a newline when the input did.
        finalNewline = !each && !input.empty() && input.back() == '\n';
        std::vector<std::string> lines = SplitLines(input);
        if (each) {
            eachLines = std::move(lines);
        } else {
            Doc d;
            d.wrapped = cfg.wrapped;
            for (const std::string& c : context) {
                Line l;
                l.context = true;
                l.src = c;
                d.lines.push_back(std::move(l));
            }
            uint32_t id = 0;
            for (const std::string& s : lines) {
                Line l;
                l.id = ++id;
                l.src = s;
                d.lines.push_back(std::move(l));
            }
            docs.push_back(std::move(d));
        }
    }

    // Translate.
    const Wants wants{json, why || !tracePath.empty(), !stagesDir.empty()};
    StageTimes times{};
    RunOptions run;
    run.skipAnalysis = fromToks;
    run.times = stats && threads == 1 ? &times : nullptr;
    Outputs out;
    const auto tStart = std::chrono::steady_clock::now();
    const size_t count = docs.empty() ? eachLines.size() : docs.size();
    // Translates document i and frees it.
    auto translate = [&](size_t i, const RunOptions& opts, Outputs& sink) {
        Doc d;
        if (docs.empty()) {
            Line l;
            l.id = static_cast<uint32_t>(i + 1);
            l.src = eachLines[i];
            d.lines.push_back(std::move(l));
        } else {
            d = std::move(docs[i]);
        }
        RunDoc(engine, d, opts);
        Collect(engine, d, wants, sink, each);
    };
    if (threads <= 1 || count < 2) {
        for (size_t i = 0; i < count; i++) translate(i, run, out);
    } else {
        std::vector<Outputs> parts(threads);
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; t++) {
            pool.emplace_back([&, t] {
                RunOptions mine = run;
                mine.times = nullptr;
                for (size_t i = count * t / threads; i < count * (t + 1) / threads; i++) translate(i, mine, parts[t]);
            });
        }
        for (std::thread& th : pool) th.join();
        for (Outputs& p : parts) {
            out.text += p.text;
            if (!p.json.empty()) out.json += (out.json.empty() ? "" : ",\n") + p.json;
            out.why += p.why;
            out.stages += p.stages;
            out.lines += p.lines;
            out.covered += p.covered;
            out.tracedBytes += p.tracedBytes;
            out.outBytes += p.outBytes;
            out.flagged += p.flagged;
        }
    }
    const auto tEnd = std::chrono::steady_clock::now();
    if (finalNewline) out.text += '\n';

    int status = 0;
    if (why) std::fwrite(out.why.data(), 1, out.why.size(), stderr);
    if (!tracePath.empty() && !WriteFile(tracePath, out.why)) {
        std::fprintf(stderr, "hdtrans08: cannot write %s\n", tracePath.c_str());
        status = 1;
    }
    if (!stagesDir.empty() && !WriteFile(stagesDir + "/ir.jsonl", out.stages)) {
        std::fprintf(stderr, "hdtrans08: cannot write %s/ir.jsonl\n", stagesDir.c_str());
        status = 1;
    }
    std::string result = out.text;
    if (json) {
        std::string j = "{\"text\":";
        AppendJsonString(j, out.text);
        j += ",\"stubs\":";
        AppendJsonString(j, stubs);
        j += ",\"lines\":[\n" + out.json + "\n]}\n";
        result = std::move(j);
    }
    if (!outPath.empty()) {
        if (!WriteFile(outPath, result)) {
            std::fprintf(stderr, "hdtrans08: cannot write %s\n", outPath.c_str());
            status = 1;
        }
    } else {
        std::fwrite(result.data(), 1, result.size(), stdout);
    }
    if (stats) {
        using std::chrono::duration;
        const double load = duration<double>(tLoaded - t0).count();
        const double secs = duration<double>(tEnd - tStart).count();
        std::fprintf(stderr,
                     "hdtrans08 stats: lines=%llu startup_s=%.3f translate_s=%.3f lines_per_s=%.0f covered=%llu "
                     "covered_pct=%.1f traced_pct=%.2f flagged_lines=%llu threads=%u stubs=%s\n",
                     static_cast<unsigned long long>(out.lines), load, secs, secs > 0 ? static_cast<double>(out.lines) / secs : 0.0,
                     static_cast<unsigned long long>(out.covered),
                     out.lines ? 100.0 * static_cast<double>(out.covered) / static_cast<double>(out.lines) : 0.0,
                     out.outBytes ? 100.0 * static_cast<double>(out.tracedBytes) / static_cast<double>(out.outBytes)
                                  : 100.0,
                     static_cast<unsigned long long>(out.flagged), threads, stubs.empty() ? "none" : stubs.c_str());
        if (run.times) {
            std::fputs("hdtrans08 stages: ms", stderr);
            for (int s = 0; s < kStages; s++)
                std::fprintf(stderr, " %s=%.1f", StageName(s), static_cast<double>(times[static_cast<size_t>(s)]) / 1e6);
            std::fputc('\n', stderr);
        }
    }
    FreeEngine(engine);
    return std::fflush(stdout) == 0 ? status : 1;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    for (int i = 0; i < argc; i++) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(static_cast<size_t>(size > 0 ? size : 1), '\0');
        if (size > 0) WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), size, nullptr, nullptr);
        arg.resize(std::strlen(arg.c_str()));
        storage.push_back(std::move(arg));
    }
    std::vector<char*> args;
    for (std::string& s : storage) args.push_back(s.data());
    args.push_back(nullptr);
    return Run(argc, args.data());
}
#else
int main(int argc, char** argv) { return Run(argc, argv); }
#endif
