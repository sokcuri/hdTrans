#include "core/word/document_senses.h"

#include <algorithm>
#include <vector>

#include "core/text/utf8.h"
#include "core/word/worker.h"

namespace hdTrans::word {
namespace {

bool EligibleKey(const Item& it, int candidate) {
    const auto code = it.Code(candidate);
    if (code.empty() || (code[0] != '1' && code[0] != '4' && code[0] != '7' && code[0] != '8')) return false;
    std::string_view key = it.group->surface;
    if (text::RuneCount(key) < 2) return false;
    while (!key.empty()) {
        const auto rune = text::DecodeRune(key);
        if ((rune.cp < 0x3040 || rune.cp > 0x309f) && rune.cp != U'ー') return true;
        key.remove_prefix(rune.size);
    }
    return false;
}

std::string PartCode(const Item& it, int candidate) {
    return std::string(1, it.group->At(candidate)->cls) + std::string(it.Code(candidate));
}

std::string_view TrimSpaces(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}

bool SameForm(std::string_view current, std::string_view currentStem,
              std::string_view proposed, std::string_view proposedStem) {
    const auto currentStart = static_cast<std::size_t>(currentStem.data() - current.data());
    const auto proposedStart = static_cast<std::size_t>(proposedStem.data() - proposed.data());
    // Keep every byte outside the stem, including the complete marker family,
    // form code, particles, auxiliary prefixes and any text after the marker.
    return current.substr(0, currentStart) == proposed.substr(0, proposedStart) &&
           current.substr(currentStart + currentStem.size()) == proposed.substr(proposedStart + proposedStem.size());
}

}  // namespace

std::string_view DocumentSenseStem(std::string_view target) {
    target = TrimSpaces(target);
    if (!target.empty() && std::string_view("0123456789ABC").find(target.front()) != std::string_view::npos) {
        target.remove_prefix(1);
    }
    for (std::size_t i = 0; i + 3 < target.size(); i++) {
        const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        const char marker = target[i];
        if ((marker == '&' || marker == '$' || marker == '%') && letter(target[i + 1]) &&
            (letter(target[i + 2]) || (target[i + 2] >= '0' && target[i + 2] <= '9')) && target[i + 3] == '@') {
            target = target.substr(0, i);
            break;
        }
    }
    target = TrimSpaces(target);
    // Strip whole auxiliary words and leading particle templates, never a
    // syllable inside a word: "할 수 구&AV@" and "구&AV@" share the stem "구".
    constexpr std::string_view prefixes[] = {"할 수", "(이)라고", "(을)를", "(으)로", "해"};
    for (const auto prefix : prefixes) {
        if (target == prefix || (target.starts_with(prefix) && target.substr(prefix.size()).starts_with(' '))) {
            target = TrimSpaces(target.substr(prefix.size()));
        }
    }
    // Longest first; remove at most one particle so a shortened word is not
    // repeatedly mistaken for another particle-bearing phrase.
    constexpr std::string_view particles[] = {
        "에서는", "에게서", "으로는", "에는", "에게", "에서", "으로", "은", "는", "을", "를", "의", "에",
    };
    for (const auto particle : particles) {
        if (target.size() > particle.size() && target.ends_with(particle)) {
            target = TrimSpaces(target.substr(0, target.size() - particle.size()));
            break;
        }
    }
    return target;
}

void SessionSenses::FinishLine() {
    choices.merge(pending);
    pending.clear();
}

void SessionSenses::Remember(const Item& it) {
    if (!EligibleKey(it, it.selected)) return;
    switch (it.choice.layer) {
        case Provenance::LocalRule:
        case Provenance::Sense:
            if (!choices.contains(it.group->surface)) {
                pending.try_emplace(it.group->surface,
                                    SessionSense{std::string(it.EntryText(it.selected)), PartCode(it, it.selected)});
            }
            break;
        default:
            break;
    }
}

int DocumentSenseScore(const Item& it, int candidate, const SelectContext&) {
    const SessionSenses* senses = it.w->settings.sessionSenses;
    if (senses == nullptr || !EligibleKey(it, candidate) || !EligibleKey(it, it.selected)) return 0;
    const auto found = senses->choices.find(it.group->surface);
    if (found == senses->choices.end()) return 0;
    if (found->second.partCode != PartCode(it, it.selected) ||
        found->second.partCode != PartCode(it, candidate)) return 0;
    const auto current = it.EntryText(it.selected);
    const auto proposed = it.EntryText(candidate);
    const auto currentStem = DocumentSenseStem(current);
    const auto proposedStem = DocumentSenseStem(proposed);
    if (currentStem.empty() || proposedStem.empty() || currentStem == proposedStem ||
        DocumentSenseStem(found->second.target) != proposedStem) return 0;
    return SameForm(current, currentStem, proposed, proposedStem) ? 1 : 0;
}

}  // namespace hdTrans::word
