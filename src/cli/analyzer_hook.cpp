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

bool Ends(void*, std::string_view text, std::vector<uint8_t>* ends) {
    thread_local hd8::ana::WorkspacePtr ws = hd8::ana::NewWorkspace();
    hd8::ana::Options opt;
    opt.margins = false;
    opt.splitLongUnits = true;
    hd8::ana::Analysis r;
    analyzer->Analyze(text, opt, &r, ws.get());
    using P = hd8::ana::Pos;
    auto code = [](P p) -> uint8_t {
        switch (p) {
        case P::n: case P::pn: case P::num: return 'n';
        case P::v1: case P::v5: case P::vs: case P::vk: case P::vz: return 'v';
        case P::adj: return 'a';
        case P::na: return 'k';
        case P::adv: case P::conj: case P::intj: return 'd';
        case P::adn: return 'm';
        case P::aux: return 'x';
        case P::p_case: case P::p_topic: case P::p_conj: case P::p_final: case P::p_adv: return 'p';
        case P::pref: return 'f';
        case P::suf: return 'z';
        case P::sym: return 's';
        default: return 'u';
        }
    };
    ends->assign(text.size() + 1, 0);
    (*ends)[text.size()] = 'e';
    for (auto& t : r.toks) {
        if (t.src.end <= text.size() && (*ends)[t.src.end] == 0) (*ends)[t.src.end] = 'e';
    }
    for (auto& t : r.toks)
        if (t.src.begin <= text.size()) (*ends)[t.src.begin] = code(t.pos);
    if ((*ends)[0] == 0) (*ends)[0] = 'u';
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
