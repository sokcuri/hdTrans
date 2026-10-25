#include "core/word/analyze.h"

#include <algorithm>

#include "core/text/utf8.h"
#include "core/word/util.h"
#include "core/word/worker.h"

namespace hdTrans::word {

Analysis::Analysis() = default;
Analysis::~Analysis() = default;

void Analyze(const tokenizer::Tokens& tokens, const dict::Snapshot& snapshot, const dict::LookupContext& lookup,
             const Settings& settings, Arena& arena, Analysis& out, LookupObserver observer) {
    arena.Reset();
    out.sentences.clear();
    out.fault = false;
    out.worker = std::make_unique<Worker>(tokens, snapshot, lookup, settings, arena, observer);
    Worker& w = *out.worker;
    const auto& markers = tokens.markers;
    int first = 0;
    for (int i = 0; i < w.TokenCount(); i++) {
        int last = i;
        const bool end = w.Dispatch(i, last);
        if (w.fault) {
            out.fault = true;
            break;
        }
        if (end) {
            Sentence& s = out.sentences.emplace_back();
            s.first = first;
            s.last = last;
            s.chain = w.chain;
            auto byToken = [](const tokenizer::ControlMarker& m, int t) { return m.tokenIndex < t; };
            s.markerBegin = static_cast<int>(std::lower_bound(markers.begin(), markers.end(), first, byToken) - markers.begin());
            s.markerEnd = static_cast<int>(std::lower_bound(markers.begin(), markers.end(), last + 1, byToken) - markers.begin());
            s.current = w.current;
            s.transformSurface = w.transformSurface;
            w.chain = {};
            first = last + 1;
        }
        i = last;
    }
    out.spaced = w.spaced;
}

bool PoiTicLine(std::string_view line) {
    const auto closes = [](std::string_view s) {
        while (s.starts_with("ー") || s.starts_with("～") || s.starts_with("〜")) s.remove_prefix(std::string_view("ー").size());
        return s.empty() || RuneIn(text::DecodeRune(s).cp, "。！？!?…‥」』）　 ♪");
    };
    int finals = 0;
    for (std::size_t at = line.find("ぽ"); at != std::string::npos; at = line.find("ぽ", at + 1)) {
        std::string_view rest = line.substr(at + std::string_view("ぽ").size());
        while (rest.starts_with("ー") || rest.starts_with("～") || rest.starts_with("〜")) rest.remove_prefix(std::string_view("ー").size());
        if (!rest.starts_with("い")) continue;
        rest.remove_prefix(std::string_view("い").size());
        if (rest.starts_with("ぽい")) rest.remove_prefix(std::string_view("ぽい").size());
        if (!closes(rest)) continue;
        std::string_view before = line.substr(0, at);
        if (before.ends_with("っ")) before.remove_suffix(std::string_view("っ").size());
        if (before.empty() || RuneIn(DecodeLastRune(before).cp, "。！？!?…‥「『（、　 ♪～ー")) return true;
        finals++;
    }
    return finals >= 2;
}

}  // namespace hdTrans::word
