#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/engine.h"
#include "core/engine/driver.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

// 高い as 높다: a plain sentence end whose casual form is 높아.
const std::string kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "高\t$D3@\tH7N0\t\t\tGN\t0\t1\n"
    "い\t1다\tT500\t\t\tGN\t0\t2\n";

struct Fixture {
    std::shared_ptr<const dict::Snapshot> snapshot;
    std::shared_ptr<internal::Generated> generated;
};

const Fixture& Dictionary() {
    static const Fixture f = [] {
        const auto loaded = dict::Load({{0, "base.tsv", kRows}});
        HD_CHECK(loaded.errors.empty());
        return Fixture{loaded.snapshot, internal::NewGenerated(*loaded.snapshot)};
    }();
    return f;
}

std::vector<std::string> Translate(const std::vector<std::string>& lines, internal::Memory& memory,
                                   const internal::TranslationOptions& options = {}) {
    const Fixture& f = Dictionary();
    return internal::TranslateDocument(*f.snapshot, *f.generated, lines, memory, {}, options);
}

std::string Joined(const std::vector<std::string>& lines) {
    std::string s;
    for (const std::string& line : lines) s += (s.empty() ? "" : "|") + line;
    return s;
}

std::string Translate(const std::vector<std::string>& lines, const internal::TranslationOptions& options = {}) {
    internal::Memory memory;
    return Joined(Translate(lines, memory, options));
}

}  // namespace

HD_TEST(engine_speech_levels) {
    HD_CHECK_EQ(Translate({"「高い」", "高い。"}), "「높아」|높다.");
    internal::TranslationOptions strict;
    strict.speech = Speech::Strict;
    HD_CHECK_EQ(Translate({"「高い」", "高い。"}, strict), "「높다」|높다.");
    internal::TranslationOptions reference;
    reference.reference = true;
    HD_CHECK_EQ(Translate({"「高い」"}, reference), "「높다」");
}

HD_TEST(engine_speech_quote_across_lines) {
    // A quote closed on a later line is speech on every line it covers.
    HD_CHECK_EQ(Translate({"「高い。", "高い。", "高い」", "高い。"}), "「높아.|높아.|높아」|높다.");
    // An unclosed quote leaves the lines after it alone, and so does a blank line or a line
    // that opens its own quote.
    HD_CHECK_EQ(Translate({"「高い。", "高い。"}), "「높아.|높다.");
    HD_CHECK_EQ(Translate({"「高い。", "", "高い」"}), "「높아.||높다」");
    HD_CHECK_EQ(Translate({"「高い。", "「高い」", "高い。"}), "「높아.|「높아」|높다.");
    // A quote runs for at most five lines.
    HD_CHECK_EQ(Translate({"「高い。", "高い。", "高い。", "高い。", "高い。", "高い。", "高い」"}),
                "「높아.|높다.|높다.|높다.|높다.|높다.|높다」");
    // The quote carries from one call to the next of the same memory.
    internal::Memory memory;
    HD_CHECK_EQ(Joined(Translate({"「高い。"}, memory)), "「높아.");
    HD_CHECK_EQ(Joined(Translate({"高い」"}, memory)), "높아」");
    HD_CHECK_EQ(Joined(Translate({"高い。"}, memory)), "높다.");
}

HD_TEST(engine_speech_session) {
    const auto folder = std::filesystem::temp_directory_path() / "hdtrans-speech-session";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "base.tsv") << kRows;
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.Open({folder.string()}));
    Session session(engine);
    HD_CHECK_EQ(session.Translate("「高い」"), "「높아」");
    session.SetSpeech(Speech::Strict);
    HD_CHECK_EQ(session.Translate("「高い」"), "「높다」");
    session.Reset();
    HD_CHECK_EQ(session.Translate("「高い」"), "「높다」");
    session.SetSpeech(Speech::Soft);
    HD_CHECK_EQ(session.Translate("「高い。"), "「높아.");
    HD_CHECK_EQ(session.Translate("高い」"), "높아」");
    HD_CHECK_EQ(session.Translate("「高い。"), "「높아.");
    session.Reset();
    HD_CHECK_EQ(session.Translate("高い」"), "높다」");
    Session reference(engine, true);
    HD_CHECK_EQ(reference.Translate("「高い」"), "「높다」");
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
}
