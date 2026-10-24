#include "core/word/flush.h"

#include <array>
#include <iterator>
#include <string_view>

#include "core/word/cg.h"
#include "core/word/constructions.h"
#include "core/word/misread.h"
#include "core/word/select.h"

#ifdef HDTRANS_PASS_EXPERIMENTS
#include <charconv>
#include <cstdint>
#include <cstdio>
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
    {"ChooseInitialConjunctions", ChooseInitialConjunctions},
    {"HanBeforeNa", HanBeforeNa},
    {"VerseTe", VerseTe},
    {"ResultState", ResultState},
    {"SimpleAspect", SimpleAspect},
    {"PastProgressiveAdnominal", PastProgressiveAdnominal},
    {"PreparedState", PreparedState},
    {"NotYet", NotYet},
    {"TeThanks", TeThanks},
    {"PastSequence", PastSequence},
    {"QuoteTo", QuoteTo},
    {"TerminalBeforeQuote", TerminalBeforeQuote},
    {"WonderingDarou", WonderingDarou},
    {"LongVowelAfterKatakana", LongVowelAfterKatakana},
    {"ParallelNoka", ParallelNoka},
    {"OfferPromise", OfferPromise},
    {"PluralPronoun", PluralPronoun},
    {"HumbleFirstPerson", HumbleFirstPerson},
    {"InitialSoundRule", InitialSoundRule},
    {"InwardAuxiliary", InwardAuxiliary},
    {"EmphaticMo", EmphaticMo},
};

constexpr auto kTextPassNames = [] {
    std::array<std::string_view, std::size(kTextPasses)> names{};
    for (std::size_t i = 0; i < names.size(); i++) names[i] = kTextPasses[i].name;
    return names;
}();

#ifdef HDTRANS_PASS_EXPERIMENTS
// Experiment builds only (CMake HDTRANS_PASS_EXPERIMENTS, docs/testing.md), read once per process:
// HDTRANS_PASS_SKIP lists pass names, construction families and construction row ids to leave out,
// comma-separated; HDTRANS_PASS_SHUFFLE is a decimal seed that reorders kTextPasses, each with the
// families that run after it. An unknown name is reported on stderr; a seed that is not a decimal
// number aborts.

// The passes Flush runs outside kTextPasses.
constexpr std::string_view kOuterPasses[] = {
    "DrainNmTail",    "ReinsertPhrases",    "PassiveRecords", "CleanupSelectors", "CleanupClass3", "ApplyNegatives",
    "PreferTerminal", "ExclaimedAdjective", "VerbNegative",   "FilterMisreads",   "FilterCg"};

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

bool KnownPass(std::string_view name) {
    for (const Pass& pass : kTextPasses) {
        if (pass.name == name) return true;
    }
    const internal::ConstructionTable& table = internal::BuiltinConstructions();
    for (const internal::CxFamily& family : table.families) {
        if (family.name == name) return true;
    }
    for (const internal::CxRow& row : table.rows) {
        if (row.id == name) return true;
    }
    for (std::string_view outer : kOuterPasses) {
        if (outer == name) return true;
    }
    return false;
}

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
                const std::string_view name = Trim(rest.substr(0, comma));
                if (!name.empty()) {
                    if (!KnownPass(name)) {
                        std::fprintf(stderr, "HDTRANS_PASS_SKIP: no pass named %.*s\n", static_cast<int>(name.size()),
                                     name.data());
                    }
                    e.skip.emplace_back(name);
                }
                rest.remove_prefix(comma == std::string_view::npos ? rest.size() : comma + 1);
            }
        }
        for (std::size_t i = 0; i < std::size(kTextPasses); i++) e.order.push_back(i);
        const char* seedText = std::getenv("HDTRANS_PASS_SHUFFLE");
        if (const std::string_view seed = Trim(seedText != nullptr ? seedText : ""); !seed.empty()) {
            std::uint64_t state = 0;
            const auto [end, error] = std::from_chars(seed.data(), seed.data() + seed.size(), state);
            if (error != std::errc() || end != seed.data() + seed.size()) {
                std::fprintf(stderr, "HDTRANS_PASS_SHUFFLE is not a decimal number: %s\n", seedText);
                std::abort();
            }
            for (std::size_t i = e.order.size(); i > 1; i--) {
                std::swap(e.order[i - 1], e.order[static_cast<std::size_t>(SplitMix(state) % i)]);
            }
        }
        return e;
    }();
    return experiment;
}

bool Skipped(std::string_view name) {
    for (const std::string& skip : CurrentExperiment().skip) {
        if (skip == name) return true;
    }
    return false;
}
#endif

void Run(Flusher& f, const Pass& pass) {
#ifdef HDTRANS_PASS_EXPERIMENTS
    if (Skipped(pass.name)) return;
#endif
    pass.run(f);
}

// Text pass i and the construction families that run after it.
void RunText(Flusher& f, std::size_t i) {
    Run(f, kTextPasses[i]);
#ifdef HDTRANS_PASS_EXPERIMENTS
    RunConstructions(f, i, Skipped);
#else
    RunConstructions(f, i);
#endif
}

}  // namespace

std::span<const std::string_view> TextPassNames() { return kTextPassNames; }

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
        for (const std::size_t i : CurrentExperiment().order) RunText(f, i);
#else
        for (std::size_t i = 0; i < std::size(kTextPasses); i++) RunText(f, i);
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
