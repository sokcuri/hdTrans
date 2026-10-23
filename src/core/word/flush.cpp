#include "core/word/flush.h"

#include <iterator>
#include <string_view>

#include "core/word/cg.h"
#include "core/word/misread.h"
#include "core/word/select.h"

#ifdef HDTRANS_PASS_EXPERIMENTS
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#endif

namespace hdTrans::word {
namespace {

struct Pass {
    std::string_view name;
    void (*run)(Flusher&);
};

// The text-path passes between the negatives and the terminal forms, in order.
constexpr Pass kTextPasses[] = {
    {"Select", Select},
    {"RespectfulPassive", RespectfulPassive},
    {"OwnPassives", OwnPassives},
    {"DegreeAfterNumber", DegreeAfterNumber},
    {"BenefactiveProvider", BenefactiveProvider},
    {"ChooseInitialConjunctions", ChooseInitialConjunctions},
    {"GoNotSay", GoNotSay},
    {"HanBeforeNa", HanBeforeNa},
    {"AdjectiveNeighbours", AdjectiveNeighbours},
    {"GeotBeforeMitai", GeotBeforeMitai},
    {"PositiveIke", PositiveIke},
    {"CopulaDeAru", CopulaDeAru},
    {"NegativeCopula", NegativeCopula},
    {"SubjectBeforeGa", SubjectBeforeGa},
    {"SubjectNo", SubjectNo},
    {"VerseTe", VerseTe},
    {"TeRequest", TeRequest},
    {"ResultState", ResultState},
    {"SimpleAspect", SimpleAspect},
    {"PastProgressiveAdnominal", PastProgressiveAdnominal},
    {"PreparedState", PreparedState},
    {"NotYet", NotYet},
    {"SeeAnd", SeeAnd},
    {"TeSequence", TeSequence},
    {"TeThanks", TeThanks},
    {"TeObjectComma", TeObjectComma},
    {"PastSequence", PastSequence},
    {"QuoteTo", QuoteTo},
    {"NodaTo", NodaTo},
    {"TerminalBeforeQuote", TerminalBeforeQuote},
    {"HodoDegree", HodoDegree},
    {"QuestionCopula", QuestionCopula},
    {"WonderingDarou", WonderingDarou},
    {"GenitiveBetweenNames", GenitiveBetweenNames},
    {"LongVowelAfterKatakana", LongVowelAfterKatakana},
    {"ReturnTo", ReturnTo},
    {"KaraSource", KaraSource},
    {"NiAfterAdverb", NiAfterAdverb},
    {"NoAfterNa", NoAfterNa},
    {"ShortPossessive", ShortPossessive},
    {"DeScene", DeScene},
    {"YounaBeforeModifier", YounaBeforeModifier},
    {"ParticleInNext", ParticleInNext},
    {"JiBeforeJiman", JiBeforeJiman},
    {"ParallelNoka", ParallelNoka},
    {"OfferPromise", OfferPromise},
    {"EvenBeforePassive", EvenBeforePassive},
    {"ComitativeTo", ComitativeTo},
    {"NounTo", NounTo},
    {"LineInitialTo", LineInitialTo},
    {"PluralPronoun", PluralPronoun},
    {"HumbleFirstPerson", HumbleFirstPerson},
    {"ChanAfterKatakana", ChanAfterKatakana},
    {"InitialSoundRule", InitialSoundRule},
    {"InwardAuxiliary", InwardAuxiliary},
    {"EmphaticMo", EmphaticMo},
};

#ifdef HDTRANS_PASS_EXPERIMENTS
// Experiment builds only (CMake HDTRANS_PASS_EXPERIMENTS, docs/testing.md), read once per process:
// HDTRANS_PASS_SKIP lists pass names to leave out, comma-separated; HDTRANS_PASS_SHUFFLE is a seed
// that reorders kTextPasses.
struct Experiment {
    std::vector<std::string> skip;
    std::vector<std::size_t> order;
};

std::uint64_t SplitMix(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

const Experiment& CurrentExperiment() {
    static const Experiment experiment = [] {
        Experiment e;
        if (const char* skip = std::getenv("HDTRANS_PASS_SKIP")) {
            std::string_view rest = skip;
            while (!rest.empty()) {
                const std::size_t comma = rest.find(',');
                if (comma != 0) e.skip.emplace_back(rest.substr(0, comma));
                rest.remove_prefix(comma == std::string_view::npos ? rest.size() : comma + 1);
            }
        }
        for (std::size_t i = 0; i < std::size(kTextPasses); i++) e.order.push_back(i);
        if (const char* seed = std::getenv("HDTRANS_PASS_SHUFFLE"); seed != nullptr && *seed != '\0') {
            std::uint64_t state = std::strtoull(seed, nullptr, 10);
            for (std::size_t i = e.order.size(); i > 1; i--) {
                std::swap(e.order[i - 1], e.order[static_cast<std::size_t>(SplitMix(state) % i)]);
            }
        }
        return e;
    }();
    return experiment;
}
#endif

void Run(Flusher& f, const Pass& pass) {
#ifdef HDTRANS_PASS_EXPERIMENTS
    for (const std::string& name : CurrentExperiment().skip) {
        if (name == pass.name) return;
    }
#endif
    pass.run(f);
}

}  // namespace

// The sentence passes in order. The text-path passes run only when settings.text is set.
void Flush(Flusher& f) {
    Run(f, {"DrainNmTail", DrainNmTail});
    Run(f, {"ReinsertPhrases", ReinsertPhrases});
    if (f.w.settings.text) Run(f, {"PassiveRecords", PassiveRecords});
    Run(f, {"CleanupSelectors", CleanupSelectors});
    Run(f, {"CleanupClass3", CleanupClass3});
    Run(f, {"ApplyNegatives", ApplyNegatives});
    if (f.w.settings.text) {
#ifdef HDTRANS_PASS_EXPERIMENTS
        for (const std::size_t i : CurrentExperiment().order) Run(f, kTextPasses[i]);
#else
        for (const Pass& pass : kTextPasses) Run(f, pass);
#endif
    }
    if (f.w.settings.terminal) {
        Run(f, {"PreferTerminal", PreferTerminal});
        if (f.w.settings.text) Run(f, {"ExclaimedAdjective", ExclaimedAdjective});
    }
    if (f.w.settings.text) Run(f, {"VerbNegative", VerbNegative});
    if (f.w.settings.text && f.w.settings.improvements.misreadFilter) Run(f, {"FilterMisreads", FilterMisreads});
    if (f.w.settings.text && (f.w.settings.improvements.cgRules || f.w.settings.improvements.cgLearned)) {
        Run(f, {"FilterCg", FilterCg});
    }
}

}  // namespace hdTrans::word
