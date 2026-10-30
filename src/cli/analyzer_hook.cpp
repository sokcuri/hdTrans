// The 0.8 analyzer as the word search's segmenter (built with HDTRANS_ANALYZER_HOOK).
// --analyzer DIR loads DIR/core.bin and, when present, DIR/long.bin.
#include "cli/analyzer_hook.h"

#include <fstream>
#include <sstream>

#include "hd8/analyzer.h"

namespace hdTrans::cli {

namespace {

struct AnalyzerHook {
    hd8::ana::Analyzer analyzer;
    AnalyzerOptions options;
    word::Segmenter segmenter;
};

bool ReadAll(const std::string& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

void Hints(const AnalyzerHook& h, std::string_view text, hd8::ana::SegHints* out) {
    thread_local hd8::ana::WorkspacePtr ws = hd8::ana::NewWorkspace();
    h.analyzer.Hints(text, out, ws.get(), h.options.slack);
}

bool Ends(void* ctx, std::string_view text, word::BoundaryHints* out) {
    const auto& h = *static_cast<const AnalyzerHook*>(ctx);
    hd8::ana::SegHints s;
    Hints(h, text, &s);
    out->ends = std::move(s.ends);
    out->rejected.clear();
    out->lemmas.clear();
    if (h.options.reject)
        for (auto& r : s.rejected) out->rejected.push_back({r.span.begin, r.span.end, r.cls});
    out->blockParticleReading = h.options.particle;
    if (h.options.lemma)
        for (auto& r : s.respell) out->lemmas.push_back({r.span.begin, r.span.end, r.kanji});
    return true;
}

bool Respell(void* ctx, std::string_view text, std::string* out) {
    const auto& h = *static_cast<const AnalyzerHook*>(ctx);
    hd8::ana::SegHints s;
    Hints(h, text, &s);
    if (s.respell.empty()) return false;
    out->clear();
    uint32_t at = 0;
    for (auto& r : s.respell) {
        if (r.span.begin < at) continue;
        out->append(text.substr(at, r.span.begin - at));
        out->append(r.kanji);
        at = r.span.end;
    }
    out->append(text.substr(at));
    return true;
}

}  // namespace

const word::Segmenter* LoadAnalyzer(const std::string& dir, const AnalyzerOptions& options, std::string* err) {
    static AnalyzerHook hook;  // one per process, read-only after this call
    std::string bytes;
    if (!ReadAll(dir + "/core.bin", &bytes)) {
        *err = "cannot read " + dir + "/core.bin";
        return nullptr;
    }
    if (!hook.analyzer.Load(bytes, err)) return nullptr;
    std::string overlay;
    if (ReadAll(dir + "/long.bin", &overlay) && !hook.analyzer.LoadOverlay(overlay, err)) return nullptr;
    hook.options = options;
    hook.segmenter.ctx = &hook;
    hook.segmenter.hints = &Ends;
    hook.segmenter.respell = options.respell ? &Respell : nullptr;
    return &hook.segmenter;
}

}  // namespace hdTrans::cli
