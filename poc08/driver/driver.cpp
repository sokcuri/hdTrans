#include "driver.h"

#include <chrono>
#include <cstdio>

namespace hd08 {

namespace {

// Prior risk per evidence kind until a calibration table replaces it (DESIGN §6).
constexpr std::array<float, 9> kDefaultRisk = {0.05f, 0.06f, 0.20f, 0.50f, 0.50f, 0.50f, 0.30f, 0.02f, 0.10f};

bool ReadText(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool LoadRisk(const std::string& root, std::array<float, 9>& risk, std::string& err) {
    risk = kDefaultRisk;
    const std::string path = root + "/driver/data/risk.tsv";
    std::string text;
    if (!ReadText(path, text)) return true;
    size_t start = 0;
    int lineNo = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string_view row(text.data() + start, end - start);
        start = end + 1;
        lineNo++;
        if (row.empty() || row[0] == '#') continue;
        const size_t tab = row.find('\t');
        unsigned kind = 0;
        if (tab == std::string_view::npos || !Parse(F::ev, row.substr(0, tab), kind)) {
            err = path + ":" + std::to_string(lineNo) + ": expected <evidence kind><TAB><risk>";
            return false;
        }
        risk[kind] = std::strtof(std::string(row.substr(tab + 1)).c_str(), nullptr);
    }
    return true;
}

struct Timer {
    StageTimes* times;
    int stage;
    std::chrono::steady_clock::time_point start;
    Timer(StageTimes* t, int s) : times(t), stage(s) {
        if (times) start = std::chrono::steady_clock::now();
    }
    ~Timer() {
        if (times)
            (*times)[static_cast<size_t>(stage)] += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
    }
};

}  // namespace

const char* StageName(int stage) {
    static const char* const kNames[kStages] = {"N", "A", "S", "A2", "C", "L", "Z", "T", "R", "W", "G"};
    return stage >= 0 && stage < kStages ? kNames[stage] : "?";
}

std::string StubComponents() {
    std::string s;
    auto add = [&s](const char* name) { s += s.empty() ? name : std::string(",") + name; };
#ifdef HD08_STUB_ANALYZER
    add("analyzer");
#endif
#ifdef HD08_STUB_DISCOURSE
    add("discourse");
#endif
#ifdef HD08_STUB_STRUCTURE
    add("structure");
#endif
#ifdef HD08_STUB_TRANSFER
    add("transfer");
#endif
#ifdef HD08_STUB_GENERATOR
    add("generator");
#endif
    static_cast<void>(add);
    return s;
}

bool LoadEngine(const Config& cfg, Engine& e, std::string& err) {
    e.cfg = cfg;
    if (!LoadRisk(cfg.data, e.risk, err)) return false;
    if (!LoadInvariantData(cfg.data, e.invariants, err)) return false;
    std::string why;
    if (!(e.analyzer = LoadAnalyzer(cfg, why))) {
        err = "analyzer: " + why;
        return false;
    }
    if (!(e.discourse = LoadDiscourse(cfg, why))) {
        err = "discourse: " + why;
        return false;
    }
    if (!(e.structure = LoadStructure(cfg, why))) {
        err = "structure: " + why;
        return false;
    }
    if (!(e.transfer = LoadTransfer(cfg, why))) {
        err = "transfer: " + why;
        return false;
    }
    if (!(e.generator = LoadGenerator(cfg, why))) {
        err = "generator: " + why;
        return false;
    }
    return true;
}

void FreeEngine(Engine& e) {
    if (e.analyzer) FreeAnalyzer(e.analyzer);
    if (e.discourse) FreeDiscourse(e.discourse);
    if (e.structure) FreeStructure(e.structure);
    if (e.transfer) FreeTransfer(e.transfer);
    if (e.generator) FreeGenerator(e.generator);
    e.analyzer = nullptr;
    e.discourse = nullptr;
    e.structure = nullptr;
    e.transfer = nullptr;
    e.generator = nullptr;
}

void RunDoc(const Engine& e, Doc& doc, const RunOptions& o) {
    const Config& cfg = e.cfg;
    const std::vector<ForcedSpan> none;
    if (!o.skipAnalysis) {
        for (Line& line : doc.lines) {
            {
                Timer t(o.times, kN);
                StageN(*e.analyzer, cfg, doc, line);
            }
            Timer t(o.times, kA);
            StageA(*e.analyzer, cfg, doc, line, none);
        }
    }
    std::vector<ForcedSpan> forced;
    {
        Timer t(o.times, kS);
        StageS(*e.discourse, cfg, doc, forced);
    }
    if (!forced.empty() && !o.skipAnalysis) {
        Timer t(o.times, kA2);
        std::vector<ForcedSpan> forLine;
        for (uint32_t li = 0; li < doc.lines.size(); li++) {
            forLine.clear();
            for (const ForcedSpan& f : forced)
                if (f.line == li) forLine.push_back(f);
            if (!forLine.empty()) StageA(*e.analyzer, cfg, doc, doc.lines[li], forLine);
        }
    }
    for (Line& line : doc.lines) {
        const int paths = line.alt.toks.empty() ? 1 : 2;
        for (int p = 0; p < paths; p++) {
            {
                Timer t(o.times, kC);
                StageC(*e.structure, cfg, doc, line, p);
            }
            {
                Timer t(o.times, kL);
                StageL(*e.structure, cfg, doc, line, p);
            }
            Timer t(o.times, kZ);
            StageZ(*e.discourse, cfg, doc, line, p);
        }
        Timer t(o.times, kT);
        StageT(*e.transfer, cfg, doc, line);
    }
    {
        Timer t(o.times, kR);
        StageR(*e.discourse, cfg, doc);
    }
    for (Line& line : doc.lines) {
        {
            Timer t(o.times, kW);
            StageW(*e.generator, cfg, doc, line);
        }
        Timer t(o.times, kG);
        StageG(*e.generator, cfg, doc, line);
    }
}

}  // namespace hd08
