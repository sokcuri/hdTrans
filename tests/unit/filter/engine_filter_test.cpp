// The filters through the engine: prefilter and postfilter files of a dictionary folder,
// and an Ehnd folder with its user dictionary.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine.h"
#include "core/engine/user_watch.h"
#include "hdtest.h"

namespace {

struct TempFolder {
    std::filesystem::path path;

    explicit TempFolder(const char* name) {
        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec) /
               (std::string("hdtrans-") + name + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path, ec);
    }
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    void Write(const char* name, const std::string& text) const {
        std::ofstream(path / name, std::ios::binary) << text;
    }
};

std::string Translate(const hdTrans::Engine& e, std::string_view text) {
    hdTrans::Session s(e);
    return s.Translate(text);
}

}  // namespace

HD_TEST(EngineFilters) {
    TempFolder dicts("dicts"), ehnd("ehnd");
    dicts.Write("base.tsv",
                "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n"
                "犬\t개\tN110\t\t\tGN\t\t\t\t\t1\n"
                "が\t가\tP200\t\t\tGN\t\t\t\t\t2\n");
    dicts.Write("pre.tsv", "#! layer=prefilter\n# test\n猫\t犬\t1\t0\n");
    dicts.Write("post.tsv", "#! layer=postfilter\n# test\n개\t멍멍이\t1\t0\n");
    ehnd.Write("UserDict_a.txt", "ポチ\t바둑이\t1\tVHn\n");
    ehnd.Write("PreFilter_a.txt", "(?<=犬が)タマ\tポチ\t1\t1\n(x\tz\t1\t1\n");

    // Without CacheWrite, so nothing is published from the temporary folder.
    hdTrans::Engine e(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(e.Open({dicts.path.string()}));
    HD_CHECK_EQ(Translate(e, "猫\n\r\n犬\r"), "멍멍이\n\r\n멍멍이\r");
    HD_CHECK_EQ(Translate(e, "ポチ"), "포치");
    HD_CHECK(e.FilterErrors().empty());

    HD_CHECK(!e.OpenEhnd((ehnd.path / "missing").string()));
    HD_CHECK(e.OpenEhnd(ehnd.path.string()));
    HD_CHECK_EQ(Translate(e, "猫がタマ"), "멍멍이가 바둑이");
    const std::vector<std::string> errors = e.FilterErrors();
    HD_REQUIRE(errors.size() == 1);
    HD_CHECK_EQ(errors[0], "PreFilter_a.txt:2: regex rejected (missing closing parenthesis at 2): (x");

    // Reload reads the Ehnd folder again; an empty folder name drops it.
    ehnd.Write("UserDict_a.txt", "ポチ\t흰둥이\t1\n");
    HD_CHECK(e.Reload());
    HD_CHECK_EQ(Translate(e, "ポチ"), "흰둥이");
    HD_CHECK(e.OpenEhnd(""));
    HD_CHECK_EQ(Translate(e, "ポチ"), "포치");
}

HD_TEST(EngineEhndAutoReload) {
    TempFolder dicts("dicts"), ehnd("ehnd");
    dicts.Write("base.tsv",
                "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n"
                "犬\t개\tN110\t\t\tGN\t\t\t\t\t1\n");
    ehnd.Write("UserDict_a.txt", "ポチ\t바둑이\t1\n");
    hdTrans::Engine e(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto);
    // Before Open the folder is only named; Open reads it.
    HD_CHECK(e.OpenEhnd(ehnd.path.string()));
    HD_REQUIRE(e.Open({dicts.path.string()}));
    hdTrans::Session s(e);
    HD_CHECK_EQ(s.Translate("ポチ"), "바둑이");
    // Nothing changed: no reload. A changed or new file of the folder reloads it, and the
    // session takes the new filters.
    HD_CHECK(!hdTrans::internal::CheckUserFile(e));
    ehnd.Write("PostFilter_a.txt", "바둑이\t흰둥이\t1\t0\n");
    HD_CHECK(hdTrans::internal::CheckUserFile(e));
    HD_CHECK_EQ(s.Translate("ポチ"), "흰둥이");
}

HD_TEST(FilterLayersArePlain) {
    std::vector<hdTrans::dict::File> files;
    files.push_back({0, "a/pre.tsv", "#! layer=prefilter\nkey\tko\n", {}});
    const hdTrans::dict::LoadResult r = hdTrans::dict::Load(std::move(files));
    HD_CHECK(r.snapshot == nullptr);
    HD_REQUIRE(r.errors.size() == 1);
    HD_CHECK(r.errors[0].kind == hdTrans::dict::LoadErrorKind::Header);
}

// A space a prefilter removes after a digit is the source's spacing still (3 고양이, not 3고양이).
HD_TEST(EngineEhndKeepsSpaceAfterNumber) {
    TempFolder dicts("dicts"), ehnd("ehnd");
    dicts.Write("base.tsv",
                "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n"
                "猫\t고양이\tN110\t\t\tGN\t\t\t\t\t1\n");
    ehnd.Write("PreFilter_a.txt", "(?<=[0-9]) +\t\t1\t1\n");
    hdTrans::Engine e(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(e.Open({dicts.path.string()}));
    HD_CHECK_EQ(Translate(e, "3猫"), "3고양이");
    HD_CHECK_EQ(Translate(e, "3 猫"), "3 고양이");
    HD_CHECK(e.OpenEhnd(ehnd.path.string()));
    HD_CHECK(e.FilterErrors().empty());
    HD_CHECK_EQ(Translate(e, "3猫"), "3고양이");
    HD_CHECK_EQ(Translate(e, "3 猫"), "3 고양이");
}
