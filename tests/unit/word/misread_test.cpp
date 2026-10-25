#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/engine/driver.h"
#include "core/word/misread.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

constexpr std::string_view kHeader = "key\tko\tpos\tsem\tgram\tdict\trule\tid\n";

std::shared_ptr<const dict::Snapshot> Dictionary(std::string_view rows, std::string_view plain = {}) {
    std::vector<dict::File> files{{0, "base.tsv", std::string(kHeader) + std::string(rows)}};
    if (!plain.empty()) files.push_back({0, "terms.tsv", std::string(plain)});
    const dict::LoadResult loaded = dict::Load(std::move(files));
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

const char* const kIwakuRows =
    "曰く\t왈\tN110\tXXX\t\tGN\t0\t1\n"
    "曰く\t가라사대\tN110\t\t\tGN\t0\t2\n";

std::string Translate(const dict::Snapshot& snapshot, std::string_view text, const word::MisreadModel* model) {
    const auto generated = internal::NewGenerated(snapshot);
    internal::TranslationOptions options;
    options.wordImprovements.misreadFilter = model != nullptr;
    options.misreadModel = model;
    const std::string line(text);
    const auto out = internal::TranslateDocument(snapshot, *generated, {&line, 1}, {}, options);
    return out.empty() ? std::string() : out[0];
}

struct Collected {
    std::vector<std::vector<std::string>> chosen, alternatives;
    std::vector<std::string> surfaces;
};

void OnResolved(void* context, int, int, const word::Chain& chain) {
    auto& c = *static_cast<Collected*>(context);
    for (const word::MisreadDecision& d : word::MisreadDecisions(chain, word::BuiltinMisreadModel())) {
        c.surfaces.emplace_back(d.item->group->surface);
        c.chosen.push_back(d.candidates.front().features);
        c.alternatives.push_back(d.candidates.back().features);
    }
}

Collected Decisions(const dict::Snapshot& snapshot, std::string_view text) {
    Collected c;
    internal::StageObserver observer;
    observer.context = &c;
    observer.resolved = OnResolved;
    const auto generated = internal::NewGenerated(snapshot);
    const std::string line(text);
    internal::TranslateDocument(snapshot, *generated, {&line, 1}, observer, {});
    return c;
}

bool HasFeature(const std::vector<std::string>& features, std::string_view f) {
    return std::find(features.begin(), features.end(), f) != features.end();
}

}  // namespace

HD_TEST(word_misread_features_of_a_line_start_word) {
    const auto snapshot = Dictionary(kIwakuRows);
    HD_REQUIRE(snapshot != nullptr);
    const Collected c = Decisions(*snapshot, "曰く、嘘。");
    HD_REQUIRE(c.surfaces.size() == 1);
    HD_CHECK_EQ(c.surfaces[0], std::string("曰く"));
    for (const std::string_view f : {"k=W", "s=曰く", "sc=M", "n=2", "b=_", "a=、", "ls", "pv=FirstXxx", "pos=N110",
                                     "pos2|ls=N1", "r=0", "t=왈", "st=曰く|왈", "sem=XXX", "m=1"}) {
        HD_CHECK(HasFeature(c.chosen[0], f));
    }
    for (const std::string_view f : {"pv=Alt", "r=1", "t=가라사대", "st=曰く|가라사대"}) {
        HD_CHECK(HasFeature(c.alternatives[0], f));
    }
    HD_CHECK(!HasFeature(c.alternatives[0], "sem=XXX"));
}

HD_TEST(word_misread_filter_replaces_confident_choices_only) {
    const auto snapshot = Dictionary(kIwakuRows);
    HD_REQUIRE(snapshot != nullptr);
    const word::MisreadWeight confident[] = {{"st=曰く|왈", 3000}};
    const word::MisreadWeight both[] = {{"st=曰く|가라사대", 3000}, {"st=曰く|왈", 3000}};
    struct Case {
        std::span<const word::MisreadWeight> weights;
        int threshold;
        const char* want;
    };
    const Case cases[] = {
        {confident, 1000, "가라사대, 허."},
        {confident, 3500, "왈, 허."},
        {both, 1000, "왈, 허."},
    };
    HD_CHECK_EQ(Translate(*snapshot, "曰く、嘘。", nullptr), std::string("왈, 허."));
    for (const Case& c : cases) {
        const word::MisreadModel model{c.weights, 0, c.threshold, 1000};
        HD_CHECK_EQ(Translate(*snapshot, "曰く、嘘。", &model), std::string(c.want));
    }
}

HD_TEST(word_misread_sound_reading_beside_a_mask) {
    const auto snapshot = Dictionary("ニー\t무릎\tN110\tVBp\t\tGN\t0\t1\n");
    HD_REQUIRE(snapshot != nullptr);
    const Collected c = Decisions(*snapshot, "○ニー");
    HD_REQUIRE(c.surfaces.size() == 1);
    HD_CHECK(HasFeature(c.chosen[0], "s|b=ニー|○"));
    HD_CHECK(HasFeature(c.alternatives[0], "snd"));
    HD_CHECK(HasFeature(c.alternatives[0], "t=니"));
    const word::MisreadWeight weights[] = {{"snd", -4000}, {"s|b=ニー|○", 3000}};
    const word::MisreadModel model{weights, 0, 1000, 0};
    HD_CHECK_EQ(Translate(*snapshot, "○ニー", nullptr), std::string("○무릎"));
    HD_CHECK_EQ(Translate(*snapshot, "○ニー", &model), std::string("○니"));
}

HD_TEST(word_misread_leaves_fixed_items) {
    const auto snapshot = Dictionary(kIwakuRows, "曰く\t왈\n");
    HD_REQUIRE(snapshot != nullptr);
    HD_CHECK(Decisions(*snapshot, "曰く、嘘。").surfaces.empty());
}

HD_TEST(word_misread_builtin_model_is_sorted) {
    const word::MisreadModel& m = word::BuiltinMisreadModel();
    HD_CHECK(std::adjacent_find(m.weights.begin(), m.weights.end(), [](const word::MisreadWeight& a, const word::MisreadWeight& b) {
                 return !(a.feature < b.feature);
             }) == m.weights.end());
    const std::string features[] = {"x", std::string(m.weights.empty() ? "" : m.weights.front().feature)};
    HD_CHECK_EQ(word::MisreadScore(m, std::span<const std::string>(features, 1)), m.bias);
}
