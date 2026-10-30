// Measurement hook: HDTRANS_ANALYZER=<snapshot> (and HDTRANS_ANALYZER_LONG=<overlay>) makes the word
// search keep the 0.8 analyzer's word ends.
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "core/word/boundaries.h"
#include "hd8/analyzer.h"

namespace {

hd8::ana::Analyzer* analyzer = nullptr;

bool ReadAll(const char* path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

bool Ends(void*, std::string_view text, hdTrans::word::BoundaryHints* out) {
    thread_local hd8::ana::WorkspacePtr ws = hd8::ana::NewWorkspace();
    hd8::ana::SegHints h;
    analyzer->Hints(text, &h, ws.get());
    out->ends = std::move(h.ends);
    out->rejected.clear();
    for (auto& r : h.rejected) out->rejected.push_back({r.span.begin, r.span.end, r.cls});
    return true;
}

}  // namespace

void InstallAnalyzerHook() {
    const char* core = std::getenv("HDTRANS_ANALYZER");
    if (core == nullptr || *core == 0) return;
    std::string bytes, err;
    if (!ReadAll(core, &bytes)) return;
    auto* a = new hd8::ana::Analyzer;
    if (!a->Load(bytes, &err)) {
        delete a;
        return;
    }
    const char* longPath = std::getenv("HDTRANS_ANALYZER_LONG");
    std::string lb;
    if (longPath != nullptr && *longPath && ReadAll(longPath, &lb) && !a->LoadOverlay(lb, &err)) {
        delete a;
        return;
    }
    analyzer = a;
    hdTrans::word::SetBoundaryOracle(&Ends, nullptr);
}
