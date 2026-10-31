#include "core/register/retrofit.h"

#include <algorithm>
#include <vector>

#include "core/register/regen.h"

namespace hdTrans::reg {

namespace {

std::string WithoutCr(std::string_view s) {
    if (s.ends_with('\r')) s.remove_suffix(1);
    return std::string(s);
}

// A line of white space only, read byte by byte: it cuts scenes.
bool Blank(const std::string& s) { return s.find_first_not_of(" \t\r\xe3\x80\x80") == std::string::npos; }

}  // namespace

void Retrofit(std::span<const std::string> source, const Capture& capture, std::span<std::string> out,
              const Switches& switches) {
    const Data* d = LoadedData();
    if (d == nullptr || !switches.levels || source.size() != out.size()) return;
    std::vector<std::string> ja;
    ja.reserve(source.size());
    for (const std::string& s : source) ja.push_back(WithoutCr(s));
    std::vector<bool> dialogue(ja.size());
    for (std::size_t i = 0; i < ja.size(); i++) dialogue[i] = DialogueLine(d->reader, ja[i]);
    if (std::none_of(dialogue.begin(), dialogue.end(), [](bool b) { return b; }) || CaptionDocument(d->reader, ja)) return;
    std::vector<PreLine> lines;
    uint32_t scene = 0;
    for (std::size_t i = 0; i < ja.size(); i++) {
        if (Blank(ja[i])) ++scene;
        lines.push_back(ReadLine(*d, i, ja[i], WithoutCr(out[i]), capture, dialogue[i], scene));
    }
    RegenOptions options;
    options.levels = switches.levels;
    const std::vector<RegenLine> regen = Regenerate(*d, lines, dialogue, options);
    for (std::size_t i = 0; i < regen.size(); i++) {
        const RegenLine& r = regen[i];
        // only a change of speech level is worth the rewrite: a line at the same level keeps its text, and so
        // does a line whose sentences would end at different levels
        if (!dialogue[i] || !r.ok || r.mixedLevels || !r.levelChanged) continue;
        const bool cr = out[i].ends_with('\r');
        out[i] = r.out + (cr ? "\r" : "");
    }
}

}  // namespace hdTrans::reg
