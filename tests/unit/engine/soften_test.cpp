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

// 高い as 높다: a plain sentence end that softens to 높아.
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

HD_TEST(engine_soften_modes) {
    HD_CHECK_EQ(Translate({"「高い」", "高い。"}), "「높아」|높다.");
    internal::TranslationOptions off;
    off.soften = Soften::Off;
    HD_CHECK_EQ(Translate({"「高い」", "高い。"}, off), "「높다」|높다.");
    internal::TranslationOptions reference;
    reference.reference = true;
    HD_CHECK_EQ(Translate({"「高い」"}, reference), "「높다」");
    internal::TranslationOptions dialogue;
    dialogue.soften = Soften::Dialogue;
    HD_CHECK_EQ(Translate({"高い。"}, dialogue), "높아.");
    dialogue.reference = true;
    HD_CHECK_EQ(Translate({"高い。"}, dialogue), "높다.");
}

HD_TEST(engine_soften_session) {
    const auto folder = std::filesystem::temp_directory_path() / "hdtrans-soften-session";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "base.tsv") << kRows;
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.Open({folder.string()}));
    Session session(engine);
    HD_CHECK_EQ(session.Translate("「高い」"), "「높아」");
    session.SetSoften(Soften::Off);
    HD_CHECK_EQ(session.Translate("「高い」"), "「높다」");
    session.Reset();
    HD_CHECK_EQ(session.Translate("「高い」"), "「높다」");
    session.SetSoften(Soften::Quotes);
    HD_CHECK_EQ(session.Translate("「高い」"), "「높아」");
    Session reference(engine, true);
    HD_CHECK_EQ(reference.Translate("「高い」"), "「높다」");
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
}
