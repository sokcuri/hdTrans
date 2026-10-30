#include "core/word/select.h"

#include <algorithm>
#include <limits>

#include "core/text/utf8.h"
#include "core/word/flush.h"
#include "core/word/document_senses.h"

namespace hdTrans::word {
namespace {

bool MaySelect(Provenance layer, bool rules) {
    switch (layer) {
        case Provenance::FirstXxx:
            return true;
        case Provenance::Digit:
        case Provenance::LocalRule:
        case Provenance::GlobalRule:
            return rules;
        default:
            return false;
    }
}

}  // namespace

void Select(Flusher& f) {
    ChooseSenses(f);
    const Settings& settings = f.w.settings;
    const bool documentActive = settings.improvements.documentSenses && settings.sessionSenses != nullptr;
    const bool documentShadow = settings.selectShadow && settings.sessionSenses != nullptr;
    const Scorer documentScorer{"document-senses", DocumentSenseScore};
    const std::size_t documentCount = documentActive || documentShadow ? 1 : 0;
    const std::size_t activeEnd = documentCount + settings.scorers.size();
    const std::size_t scorerCount = activeEnd + (settings.selectShadow ? settings.shadowScorers.size() : 0);
    if (scorerCount == 0) return;
    const SelectContext context{f.w.snapshot, f.w.chain};
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const int count = static_cast<int>(it->group->entries.size());
        if (it->Fixed() || it->selected < 0 || it->selected >= count) continue;
        if (documentActive || documentShadow) settings.sessionSenses->Remember(*it);
        if (count < 2) continue;
        const int current = it->selected;
        const std::string_view code = it->Code(current);
        if (code.empty()) continue;
        // Both scorer sets see the same selection; diagnostics never suppress an active choice.
        int activeWinner = -1, activeId = -1;
        bool shadowDecided = false;
        for (std::size_t index = 0; index < scorerCount; index++) {
            const bool document = index < documentCount;
            const bool active = document ? documentActive : index < activeEnd;
            const bool shadow = settings.selectShadow && (document || index >= activeEnd);
            const Scorer& scorer = document ? documentScorer : index < activeEnd
                ? settings.scorers[index - documentCount] : settings.shadowScorers[index - activeEnd];
            const bool eligible = MaySelect(it->choice.layer, !document && settings.selectRules);
            if (scorer.score == nullptr || (!eligible && !shadow) || (activeWinner >= 0 && !shadow)) continue;
            const int currentScore = scorer.score(*it, current, context);
            int best = current, bestScore = currentScore;
            for (int candidate = 0; candidate < count; candidate++) {
                const std::string_view candidateCode = it->Code(candidate);
                if (candidate == current || candidateCode.empty() || candidateCode[0] != code[0]) continue;
                const int score = scorer.score(*it, candidate, context);
                // Starting at current preserves it on ties; other ties keep the first slot.
                if (score > bestScore) {
                    best = candidate;
                    bestScore = score;
                }
            }
            const bool winner = eligible && best != current;
            if (shadow) {
                const bool shadowWinner = winner && !shadowDecided;
                it->shadowChoices.push_back({scorer.name, current, best, currentScore, bestScore,
                                             eligible, shadowWinner, std::string(it->EntryText(best))});
                shadowDecided = shadowDecided || shadowWinner;
            }
            if (active && activeWinner < 0 && winner) {
                activeWinner = best;
                activeId = document ? 0 : static_cast<int>(index - documentCount + (documentActive ? 1 : 0));
            }
        }
        if (activeWinner >= 0) it->SelectCandidate(activeWinner, {Provenance::Scorer, activeId});
    }
}

int ShortestTargetScore(const Item& it, int candidate, const SelectContext&) {
    const std::size_t length = text::RuneCount(it.EntryText(candidate));
    return -static_cast<int>(std::min(length, static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

}  // namespace hdTrans::word
// probe
