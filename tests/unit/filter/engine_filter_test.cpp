// The filters through the engine: prefilter and postfilter files of a dictionary folder,
// and an Ehnd folder with its user dictionary.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/dict/folders.h"
#include "core/dict/snapshot.h"
#include "core/dict/snapshot_file.h"
#include "core/engine.h"
#include "core/engine/user_watch.h"
#include "core/engine/load_info.h"
#include "core/engine/raw.h"
#include "core/filter/ehnd.h"
#include "core/filter/filter.h"
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
    hdTrans::EngineOptions options;
    options.strictDict = false;
    hdTrans::Engine e(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Off, options);
    HD_REQUIRE(e.Open({dicts.path.string()}));
    HD_CHECK_EQ(Translate(e, "猫\n\r\n犬\r"), "멍멍이\n\r\n멍멍이\r");
    HD_CHECK_EQ(Translate(e, "ポチ"), "포치");
    HD_CHECK(e.FilterErrors().empty());

    for (const bool strict : {false, true}) {
        const auto missing = (ehnd.path / (strict ? "missing-strict" : "missing")).string();
        HD_REQUIRE(e.OpenEhnd(missing, hdTrans::DictionaryOptions{strict}));
        HD_CHECK(hdTrans::internal::UserFileErrors(e).empty());
        HD_CHECK(e.UserFileError().empty());
        const auto warnings = e.FilterErrors();
        HD_REQUIRE(warnings.size() == 1);
        HD_CHECK_EQ(warnings[0], missing + ":0: the explicit Ehnd folder is missing");
        HD_CHECK(!hdTrans::internal::FilterFileFailed(e));
        HD_CHECK_EQ(Translate(e, "猫\n\r\n犬\r"), "멍멍이\n\r\n멍멍이\r");
        HD_CHECK_EQ(Translate(e, "ポチ"), "포치");
    }
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

HD_TEST(EngineEhndExamplesAreComments) {
    TempFolder dicts("example-dicts");
    dicts.Write("base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n");
    const auto examples = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "ehnd";
    const auto text = examples.u8string();
    const std::string path(reinterpret_cast<const char*>(text.data()), text.size());
    std::vector<hdTrans::filter::Source> sources;
    std::vector<hdTrans::filter::Error> errors;
    HD_REQUIRE(hdTrans::filter::ReadEhndFolder(path, sources, errors));
    HD_CHECK(errors.empty());
    HD_REQUIRE(sources.size() == 3);
    HD_CHECK_EQ(sources[0].name, "PreFilter_example.txt");
    HD_CHECK_EQ(sources[1].name, "PostFilter_example.txt");
    HD_CHECK_EQ(sources[2].name, "UserDict_example.txt");
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.OpenEhnd(path));
    HD_REQUIRE(engine.Open({dicts.path.string()}));
    if (const auto filters = engine.Filters()) {
        HD_CHECK(filters->Rows(hdTrans::filter::Stage::Pre) == 0);
        HD_CHECK(filters->Rows(hdTrans::filter::Stage::Post) == 0);
        HD_CHECK(filters->User() == nullptr);
    }
    HD_CHECK(engine.FilterErrors().empty());
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK_EQ(Translate(engine, "猫"), "고양이");
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

namespace {

void EhndReadFailureKeepsWholeStack(bool strict) {
    for (const auto mode : {hdTrans::UserReload::Off, hdTrans::UserReload::Auto}) {
        TempFolder root("reload-filter");
        const auto dicts = root.path / "dicts";
        const auto ehnd = root.path / "ehnd";
        const auto away = root.path / "ehnd-away";
        const auto user = root.path / "userdict.tsv";
        std::error_code error;
        HD_REQUIRE(std::filesystem::create_directory(dicts, error));
        HD_REQUIRE(std::filesystem::create_directory(ehnd, error));
        root.Write("dicts/base.tsv",
                   "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n"
                   "猫\t고양이\tN110\t\t\tGN\t\t\t\t\t1\n"
                   "犬\t개\tN110\t\t\tGN\t\t\t\t\t2\n");
        root.Write("userdict.tsv", "猫\t냥이\n");
        root.Write("ehnd/UserDict_a.txt", "ポチ\t바둑이\t1\n");
        hdTrans::EngineOptions options;
        options.strictDict = strict;
        hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, mode, options);
        HD_REQUIRE(engine.OpenEhnd(ehnd.string()));
        HD_REQUIRE(engine.OpenUserDicts({dicts.string()}, false, {user.string()}));
        HD_CHECK_EQ(Translate(engine, "猫"), "냥이");
        HD_CHECK_EQ(Translate(engine, "ポチ"), "바둑이");
        const auto good = engine.Dictionaries();
        const auto filters = engine.Filters();
        const auto ehndVersion = hdTrans::internal::FolderVersionOf(ehnd.string());

        root.Write("userdict.tsv", "猫\t야옹이\n");
        const auto pendingUser = hdTrans::internal::UserVersionOf({user.string()});
        std::filesystem::rename(ehnd, away, error);
        HD_REQUIRE(!error);
        if (mode == hdTrans::UserReload::Auto) {
            HD_CHECK(hdTrans::internal::CheckUserFile(engine));
        } else {
            HD_CHECK(!engine.Reload());
        }
        HD_CHECK(engine.Dictionaries() == good);
        HD_CHECK(engine.Filters() == filters);
        const auto errors = engine.FilterErrors();
        HD_REQUIRE(errors.size() == 1);
        HD_CHECK(errors[0].find("the folder cannot be listed") != std::string::npos);
        HD_CHECK_EQ(Translate(engine, "猫"), "냥이");
        HD_CHECK_EQ(Translate(engine, "犬"), "개");
        HD_CHECK_EQ(Translate(engine, "ポチ"), "바둑이");
        HD_CHECK(!hdTrans::internal::CheckUserFile(engine));

        std::filesystem::rename(away, ehnd, error);
        HD_REQUIRE(!error);
        HD_CHECK(hdTrans::internal::FolderVersionOf(ehnd.string()) == ehndVersion);
        HD_CHECK(hdTrans::internal::UserVersionOf({user.string()}) == pendingUser);
        if (mode == hdTrans::UserReload::Auto) {
            HD_CHECK(hdTrans::internal::CheckUserFile(engine));
        } else {
            HD_CHECK(engine.Reload());
        }
        HD_CHECK(engine.Dictionaries() != good);
        HD_CHECK(engine.FilterErrors().empty());
        HD_CHECK_EQ(Translate(engine, "猫"), "야옹이");
        HD_CHECK_EQ(Translate(engine, "ポチ"), "바둑이");
    }
}

}  // namespace

HD_TEST(EngineEhndReadFailureKeepsWholeStack) {
    for (const bool strict : {false, true}) EhndReadFailureKeepsWholeStack(strict);
}

HD_TEST(EngineEhndRowsFollowStrictOption) {
    for (const bool strict : {false, true}) {
        for (const auto mode : {hdTrans::UserReload::Off, hdTrans::UserReload::Auto}) {
            TempFolder dicts("dicts"), ehnd("ehnd");
            dicts.Write("base.tsv", "key\tko\tpos\tdict\tid\n猫\t고양이\tN110\tGN\t1\n");
            ehnd.Write("UserDict_a.txt", "ポチ\t바둑이\t1\n");
            ehnd.Write("PostFilter_a.txt", "고양이\t냥이\t1\t0\n");
            hdTrans::EngineOptions options;
            options.strictDict = strict;
            hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, mode, options);
            HD_REQUIRE(engine.OpenEhnd(ehnd.path.string()));
            HD_REQUIRE(engine.Open({dicts.path.string()}));
            const auto good = engine.Dictionaries();
            const auto filters = engine.Filters();
            ehnd.Write("UserDict_a.txt", "ポチ\t흰둥이\t1\n");
            ehnd.Write("PostFilter_a.txt", "고양이\t흰고양이\t1\t0\n(x\ty\t1\t1\n");
            if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload() == !strict);
            HD_CHECK((engine.Dictionaries() == good) == strict);
            HD_CHECK((engine.Filters() == filters) == strict);
            HD_CHECK_EQ(Translate(engine, "猫"), strict ? "냥이" : "흰고양이");
            HD_CHECK_EQ(Translate(engine, "ポチ"), strict ? "바둑이" : "흰둥이");
            const auto warnings = engine.FilterErrors();
            HD_REQUIRE(warnings.size() == 1);
            HD_CHECK(warnings[0].starts_with("PostFilter_a.txt:2: regex rejected"));
            HD_CHECK(!hdTrans::internal::CheckUserFile(engine));
            const auto beforeAllBad = engine.Dictionaries();
            const auto beforeAllBadFilters = engine.Filters();
            ehnd.Write("PostFilter_a.txt", "(x\ty\t1\t1\n");
            if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload() == !strict);
            HD_CHECK((engine.Dictionaries() == beforeAllBad) == strict);
            HD_CHECK((engine.Filters() == beforeAllBadFilters) == strict);
            HD_CHECK(hdTrans::internal::FilterFileFailed(engine) == strict);
            HD_CHECK_EQ(Translate(engine, "猫"), strict ? "냥이" : "고양이");
            HD_REQUIRE(engine.FilterErrors().size() == 1);
            HD_CHECK(engine.FilterErrors()[0].starts_with("PostFilter_a.txt:1: regex rejected"));
            ehnd.Write("PostFilter_a.txt", "고양이\t검은고양이\t1\t0\n");
            if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload());
            HD_CHECK_EQ(Translate(engine, "猫"), "검은고양이");
            HD_CHECK_EQ(Translate(engine, "ポチ"), "흰둥이");
            HD_CHECK(engine.FilterErrors().empty());
        }
    }
}

HD_TEST(EngineEhndInitialStrictSkipsWholeBadFile) {
    TempFolder dicts("dicts"), ehnd("ehnd");
    dicts.Write("base.tsv", "key\tko\tpos\tdict\tid\n猫\t고양이\tN110\tGN\t1\n");
    ehnd.Write("UserDict_a.txt", "ポチ\t바둑이\t1\n");
    ehnd.Write("PostFilter_bad.txt", "고양이\t냥이\t1\t0\n(x\ty\t1\t1\n");
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.OpenEhnd(ehnd.path.string()));
    HD_REQUIRE(engine.Open({dicts.path.string()}));
    HD_CHECK_EQ(Translate(engine, "猫"), "고양이");
    HD_CHECK_EQ(Translate(engine, "ポチ"), "바둑이");
    HD_CHECK(!engine.FilterErrors().empty());
    const auto filters = engine.Filters();
    HD_CHECK(!engine.OpenEhnd(ehnd.path.string()));
    HD_CHECK(engine.Filters() == filters);
    HD_CHECK(engine.OpenEhnd(ehnd.path.string(), hdTrans::DictionaryOptions{false}));
    HD_CHECK_EQ(Translate(engine, "猫"), "냥이");
    HD_CHECK(!engine.FilterErrors().empty());
    HD_CHECK(!engine.OpenEhnd(ehnd.path.string()));
    HD_CHECK_EQ(Translate(engine, "猫"), "냥이");
}

HD_TEST(EngineEhndDecodeFailuresAreFatalInBothModes) {
    for (const bool strict : {false, true}) {
        TempFolder dicts("dicts"), ehnd("ehnd");
        dicts.Write("base.tsv", "key\tko\tpos\tdict\tid\n猫\t고양이\tN110\tGN\t1\n");
        ehnd.Write("UserDict_a.txt", "ポチ\t바둑이\t1\n");
        hdTrans::EngineOptions options;
        options.strictDict = strict;
        hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto, options);
        HD_REQUIRE(engine.OpenEhnd(ehnd.path.string()));
        HD_REQUIRE(engine.Open({dicts.path.string()}));
        const auto good = engine.Dictionaries();
        const auto filters = engine.Filters();
        const std::string invalid[] = {std::string("\xFF"), std::string("\xFF\xFE\x00", 3),
                                      std::string("\xFF\xFE\x00\xD8", 4)};
        for (const auto& bytes : invalid) {
            ehnd.Write("UserDict_a.txt", bytes);
            HD_CHECK(hdTrans::internal::CheckUserFile(engine));
            HD_CHECK(engine.Dictionaries() == good);
            HD_CHECK(engine.Filters() == filters);
            HD_CHECK_EQ(Translate(engine, "ポチ"), "바둑이");
            HD_REQUIRE(engine.FilterErrors().size() == 1);
            HD_CHECK(engine.FilterErrors()[0].find("the file cannot be decoded") != std::string::npos);
            HD_CHECK(!hdTrans::internal::CheckUserFile(engine));
        }
        ehnd.Write("UserDict_a.txt", "ポチ\t흰둥이\t1\n");
        HD_CHECK(hdTrans::internal::CheckUserFile(engine));
        HD_CHECK_EQ(Translate(engine, "ポチ"), "흰둥이");
        HD_CHECK(engine.FilterErrors().empty());
    }
}

HD_TEST(EngineEhndEmptyFolderFailureAndRestore) {
    TempFolder root("empty-ehnd");
    const auto folder = root.path / "ehnd";
    const auto away = root.path / "away";
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(folder, error));
    root.Write("base.tsv", "key\tko\tpos\tdict\tid\n猫\t고양이\tN110\tGN\t1\n");
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto);
    HD_REQUIRE(engine.OpenEhnd(folder.string()));
    HD_REQUIRE(engine.Open({root.path.string()}));
    const auto good = engine.Dictionaries();
    std::filesystem::rename(folder, away, error);
    HD_REQUIRE(!error);
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(!engine.FilterErrors().empty());
    HD_CHECK(!hdTrans::internal::CheckUserFile(engine));
    std::filesystem::rename(away, folder, error);
    HD_REQUIRE(!error);
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK(engine.FilterErrors().empty());
    HD_CHECK_EQ(Translate(engine, "猫"), "고양이");
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

// A sign the loaded pre-filter writes as a mark is no word of its own: it stays joined to its
// neighbours as the post-filter expects, on the text path and the raw path, also where the source
// has it. Other signs stay words (LAY-15), and the marks are known only while the filter is loaded.
HD_TEST(EngineEhndMarksStayJoined) {
    TempFolder dicts("dicts"), ehnd("ehnd");
    dicts.Write("base.tsv",
                "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n"
                "犬\t개\tN110\t\t\tGN\t\t\t\t\t1\n"
                "猫\t고양이\tN110\t\t\tGN\t\t\t\t\t2\n"
                "が\t가\tP200\t\t\tGN\t\t\t\t\t3\n");
    ehnd.Write("PreFilter_a.txt", "が猫\tがρ猫\t1\t0\n");
    ehnd.Write("PostFilter_a.txt", "가ρ\t가 \t1\t0\n");
    hdTrans::Engine e(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(e.Open({dicts.path.string()}));
    HD_CHECK_EQ(Translate(e, "犬がρ猫"), "개가 ρ고양이");
    HD_CHECK_EQ(hdTrans::internal::TranslateRaw(e, "犬がρ猫"), "개가 ρ고양이");
    HD_CHECK(e.OpenEhnd(ehnd.path.string()));
    HD_CHECK(e.FilterErrors().empty());
    HD_CHECK_EQ(Translate(e, "犬が猫"), "개가 고양이");
    HD_CHECK_EQ(Translate(e, "犬が猫\n犬が猫"), "개가 고양이\n개가 고양이");
    HD_CHECK_EQ(hdTrans::internal::TranslateRaw(e, "犬が猫"), "개가 고양이");
    HD_CHECK_EQ(Translate(e, "犬がρ猫"), "개가 고양이");
    HD_CHECK_EQ(Translate(e, "犬がα猫"), "개가 α고양이");
    HD_CHECK_EQ(Translate(e, "犬が①"), "개가 ①");
    HD_CHECK_EQ(Translate(e, "犬が+1"), "개가 +1");
    HD_CHECK_EQ(Translate(e, "犬が♪猫"), "개가♪고양이");
}

namespace {

void WriteHeaderFilterBase(const TempFolder& folder) {
    folder.Write("base.tsv", "key\tko\tpos\tdict\tid\n"
                            "猫\t고양이\tN110\tGN\t1\n"
                            "犬\t개\tN110\tGN\t2\n"
                            "鳥\t새\tN110\tGN\t3\n");
}

bool HasHeaderFilterError(const hdTrans::Engine& engine, std::string_view file, int line) {
    const std::string location = std::string(file) + ":" + std::to_string(line) + ":";
    for (const auto& error : engine.FilterErrors()) {
        if (error.find(location) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

HD_TEST(EngineUserFolderHeaderForms) {
    for (const std::string& bom : {std::string(), std::string("\xEF\xBB\xBF")}) {
        for (const std::string& newline : {std::string("\n"), std::string("\r\n")}) {
            for (const bool strict : {false, true}) {
                TempFolder dicts("header-forms-dicts"), users("header-forms-users");
                WriteHeaderFilterBase(dicts);
                users.Write("before.TXT", bom + "#! layer=prefilter\t " + newline + "# comment" + newline +
                                          "猫\t犬\t1\t0" + newline);
                users.Write("after.tSv", bom + "#! layer=postfilter" + newline + "// comment" + newline +
                                         "개\t필터값\t1\t0" + newline);
                const auto base = hdTrans::dict::LoadFoldersTsv({dicts.path.string()});
                HD_REQUIRE(base.snapshot);
                hdTrans::EngineOptions options;
                options.strictDict = strict;
                hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Off, options);
                HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {users.path.string()}));
                HD_CHECK_EQ(Translate(engine, "猫"), "필터값");
                HD_CHECK(engine.UserFileError().empty());
                HD_CHECK(engine.FilterErrors().empty());
                HD_CHECK(engine.GetUserDict().empty());
                HD_CHECK(engine.Dictionaries()->User() == nullptr);
                HD_CHECK(engine.Dictionaries()->Plain().Entries().size() == base.snapshot->Plain().Entries().size());
                HD_CHECK(engine.Dictionaries()->TextPlain().Entries().size() == base.snapshot->TextPlain().Entries().size());
            }
        }
    }
}

HD_TEST(EngineUserFolderHeaderDiscoveryKeepsLegacy) {
    TempFolder dicts("header-discovery-dicts"), users("header-discovery-users");
    WriteHeaderFilterBase(dicts);
    users.Write("UserDict_legacy.TXT", "猫\t냥이\n");
    users.Write("userdict-z.tsv", "狐\t여우\n");
    users.Write("userdict_before.txt", "#! layer=prefilter\n鳥\t犬\t1\t0\n");
    users.Write("after.tsv", "#! layer=postfilter\n개\t멍멍이\t1\t0\n");
    const struct {
        const char* file;
        const char* text;
    } ignored[] = {
        {"arbitrary.txt", "猫\t잘못됨\n"},
        {"bad-header.txt", "#! layer=prefilterx\n猫\t犬\t1\t0\n"},
        {"other-layer.tsv", "#! layer=user\n猫\t잘못됨\n"},
        {"late-header.txt", "\n#! layer=prefilter\n猫\t犬\t1\t0\n"},
        {"uppercase-header.txt", "#! layer=PREFILTER\n猫\t犬\t1\t0\n"},
        {"before.txt.bak", "#! layer=prefilter\n猫\t犬\t1\t0\n"},
        {"before.csv", "#! layer=prefilter\n猫\t犬\t1\t0\n"},
    };
    for (const auto& file : ignored) users.Write(file.file, file.text);
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(users.path / "nested", error));
    users.Write("nested/before.txt", "#! layer=prefilter\n猫\t犬\t1\t0\n");
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {users.path.string()}));
    HD_CHECK_EQ(Translate(engine, "猫"), "냥이");
    HD_CHECK_EQ(Translate(engine, "狐"), "여우");
    HD_CHECK_EQ(Translate(engine, "鳥"), "멍멍이");
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK(engine.FilterErrors().empty());
}

HD_TEST(EngineUserFolderHeaderOrderAndExplicitPaths) {
    TempFolder dicts("header-order-dicts"), users("header-order-users");
    WriteHeaderFilterBase(dicts);
    users.Write("a_stage.TXT", "#! layer=prefilter\n鳥\t猫\t1\t0\n");
    users.Write("Z_stage.tSv", "#! layer=prefilter\n猫\t犬\t1\t0\n");
    users.Write("named.txt", "猫\t냥이\n");
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {users.path.string()}));
    HD_CHECK_EQ(Translate(engine, "鳥"), "개");
    HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false,
                                   {(users.path / "Z_stage.tSv").string(), (users.path / "a_stage.TXT").string()}));
    HD_CHECK_EQ(Translate(engine, "鳥"), "고양이");
    HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {}));
    HD_CHECK_EQ(Translate(engine, "鳥"), "새");
    HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {(users.path / "named.txt").string()}));
    HD_CHECK_EQ(Translate(engine, "猫"), "냥이");
    HD_CHECK_EQ(Translate(engine, "鳥"), "새");
    HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {(users.path / "a_stage.TXT").string()}));
    HD_CHECK_EQ(Translate(engine, "鳥"), "고양이");
}

HD_TEST(EngineUserFolderHeaderAutoReload) {
    TempFolder dicts("header-auto-dicts"), users("header-auto-users");
    WriteHeaderFilterBase(dicts);
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto);
    HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {users.path.string()}));
    hdTrans::Session session(engine);
    HD_CHECK_EQ(session.Translate("猫"), "고양이");
    HD_CHECK(!hdTrans::internal::CheckUserFile(engine));
    users.Write("before.txt", "#! layer=prefilter\n猫\t犬\t1\t0\n");
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "개");
    users.Write("before.txt", "# disabled\n猫\t잘못됨\n");
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "고양이");
    HD_CHECK(engine.UserFileError().empty());
    users.Write("before.txt", "\xEF\xBB\xBF#! layer=prefilter\r\n猫\t鳥\t1\t0\r\n");
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "새");
    users.Write("after.tsv", "#! layer=postfilter\n새\t조류\t1\t0\n");
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "조류");
    users.Write("before.txt", "#! layer=prefilter\n# changed\n猫\t犬\t1\t0\n");
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "개");
    std::error_code error;
    HD_REQUIRE(std::filesystem::remove(users.path / "after.tsv", error));
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_REQUIRE(std::filesystem::remove(users.path / "before.txt", error));
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "고양이");
    users.Write("userdict_alias.txt", "#! layer=prefilter\n猫\t犬\t1\t0\n");
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "개");
    users.Write("userdict_alias.txt", "猫\t집고양이\n");
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "집고양이");
    HD_REQUIRE(std::filesystem::remove(users.path / "userdict_alias.txt", error));
    HD_CHECK(hdTrans::internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "고양이");
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK(engine.FilterErrors().empty());
    HD_CHECK(!hdTrans::internal::CheckUserFile(engine));
}

HD_TEST(EngineUserFolderHeaderReadFailureKeepsWholeStack) {
    for (const bool strict : {false, true}) {
        for (const auto mode : {hdTrans::UserReload::Off, hdTrans::UserReload::Auto}) {
            for (const bool folderSource : {false, true}) {
                TempFolder dicts("header-io-dicts"), root("header-io-users");
                WriteHeaderFilterBase(dicts);
                const auto users = root.path / "users";
                const auto before = users / "before.txt";
                const auto after = users / "after.tsv";
                const auto legacy = users / "userdict.txt";
                const auto away = root.path / "saved";
                std::error_code error;
                HD_REQUIRE(std::filesystem::create_directory(users, error));
                root.Write("users/before.txt", "#! layer=prefilter\n猫\t犬\t1\t0\n");
                root.Write("users/after.tsv", "#! layer=postfilter\n개\t기존필터\t1\t0\n");
                root.Write("users/userdict.txt", "狐\t여우\n");
                hdTrans::EngineOptions options;
                options.strictDict = strict;
                hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, mode, options);
                const std::vector<std::string> paths = folderSource
                    ? std::vector<std::string>{users.string()}
                    : std::vector<std::string>{before.string(), after.string(), legacy.string()};
                HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, paths));
                const auto good = engine.Dictionaries();
                const auto filters = engine.Filters();
                HD_CHECK_EQ(Translate(engine, "猫"), "기존필터");
                HD_CHECK_EQ(Translate(engine, "狐"), "여우");
                root.Write("users/after.tsv", "#! layer=postfilter\n개\t새필터\t1\t0\n");
                root.Write("users/userdict.txt", "狐\t금빛여우\n");
                const auto source = folderSource ? users : before;
                std::filesystem::rename(source, away, error);
                HD_REQUIRE(!error);
                if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
                else HD_CHECK(!engine.Reload());
                HD_CHECK(engine.Dictionaries() == good);
                HD_CHECK(engine.Filters() == filters);
                HD_CHECK(!engine.UserFileError().empty());
                HD_CHECK_EQ(Translate(engine, "猫"), "기존필터");
                HD_CHECK_EQ(Translate(engine, "狐"), "여우");
                if (mode == hdTrans::UserReload::Auto) HD_CHECK(!hdTrans::internal::CheckUserFile(engine));
                if (folderSource) root.Write("users", "# replacement\n");
                else HD_REQUIRE(std::filesystem::create_directory(source, error));
                if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
                else HD_CHECK(!engine.Reload());
                HD_CHECK(engine.Dictionaries() == good);
                HD_CHECK(engine.Filters() == filters);
                HD_CHECK_EQ(Translate(engine, "猫"), "기존필터");
                HD_REQUIRE(std::filesystem::remove(source, error));
                std::filesystem::rename(away, source, error);
                HD_REQUIRE(!error);
                if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
                else HD_CHECK(engine.Reload());
                HD_CHECK(engine.Dictionaries() != good);
                HD_CHECK(engine.Filters() != filters);
                HD_CHECK(engine.UserFileError().empty());
                HD_CHECK(engine.FilterErrors().empty());
                HD_CHECK_EQ(Translate(engine, "猫"), "새필터");
                HD_CHECK_EQ(Translate(engine, "狐"), "금빛여우");
            }
        }
    }
}

HD_TEST(EngineUserFolderHeaderRowsFollowStrictOption) {
    for (const bool strict : {false, true}) {
        for (const auto mode : {hdTrans::UserReload::Off, hdTrans::UserReload::Auto}) {
            TempFolder dicts("header-strict-dicts"), users("header-strict-users"), ehnd("header-strict-ehnd");
            WriteHeaderFilterBase(dicts);
            users.Write("before.txt", "#! layer=prefilter\n猫\t犬\t1\t0\n");
            users.Write("after.tsv", "#! layer=postfilter\n개\t기존필터\t1\t0\n");
            users.Write("userdict.txt", "狐\t여우\n");
            ehnd.Write("UserDict_a.txt", "ポチ\t바둑이\t1\n");
            hdTrans::EngineOptions options;
            options.strictDict = strict;
            hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, mode, options);
            HD_REQUIRE(engine.OpenEhnd(ehnd.path.string()));
            HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {users.path.string()}));
            const auto good = engine.Dictionaries();
            const auto filters = engine.Filters();
            users.Write("userdict.txt", "狐\t금빛여우\n");
            ehnd.Write("UserDict_a.txt", "ポチ\t흰둥이\t1\n");
            users.Write("after.tsv", "\xEF\xBB\xBF#! layer=postfilter\r\n# comment\r\n"
                                     "개\t새필터\t1\t0\r\n(x\tz\t1\t1\r\n(y\tz\t1\t1\r\n");
            if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload() == !strict);
            HD_CHECK((engine.Dictionaries() == good) == strict);
            HD_CHECK((engine.Filters() == filters) == strict);
            HD_CHECK_EQ(Translate(engine, "猫"), strict ? "기존필터" : "새필터");
            HD_CHECK_EQ(Translate(engine, "狐"), strict ? "여우" : "금빛여우");
            HD_CHECK_EQ(Translate(engine, "ポチ"), strict ? "바둑이" : "흰둥이");
            HD_REQUIRE(engine.FilterErrors().size() == 2);
            HD_CHECK(HasHeaderFilterError(engine, "after.tsv", 4));
            HD_CHECK(HasHeaderFilterError(engine, "after.tsv", 5));
            HD_CHECK(!hdTrans::internal::CheckUserFile(engine));
            users.Write("after.tsv", "#! layer=postfilter\n(x\tz\t1\t1\n(y\tz\t1\t1\n");
            if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload() == !strict);
            HD_CHECK_EQ(Translate(engine, "猫"), strict ? "기존필터" : "개");
            HD_CHECK_EQ(Translate(engine, "狐"), strict ? "여우" : "금빛여우");
            HD_CHECK_EQ(Translate(engine, "ポチ"), strict ? "바둑이" : "흰둥이");
            HD_REQUIRE(engine.FilterErrors().size() == 2);
            HD_CHECK(HasHeaderFilterError(engine, "after.tsv", 2));
            HD_CHECK(HasHeaderFilterError(engine, "after.tsv", 3));
            users.Write("after.tsv", "#! layer=postfilter\n개\t복구필터\t1\t0\n");
            if (mode == hdTrans::UserReload::Auto) HD_CHECK(hdTrans::internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload());
            HD_CHECK(engine.Dictionaries() != good);
            HD_CHECK(engine.Filters() != filters);
            HD_CHECK_EQ(Translate(engine, "猫"), "복구필터");
            HD_CHECK_EQ(Translate(engine, "狐"), "금빛여우");
            HD_CHECK_EQ(Translate(engine, "ポチ"), "흰둥이");
            HD_CHECK(engine.UserFileError().empty());
            HD_CHECK(engine.FilterErrors().empty());
        }
    }
}

HD_TEST(EngineUserFolderHeaderAcceptedCallOverridePersists) {
    for (const bool strict : {false, true}) {
        TempFolder dicts("header-call-dicts"), users("header-call-users");
        WriteHeaderFilterBase(dicts);
        users.Write("after.tsv", "#! layer=postfilter\n고양이\t기존필터\t1\t0\n");
        hdTrans::EngineOptions options;
        options.strictDict = strict;
        hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Off, options);
        HD_REQUIRE(engine.OpenUserDicts({dicts.path.string()}, false, {users.path.string()}));
        users.Write("after.tsv", "#! layer=postfilter\n고양이\t새필터\t1\t0\n(x\tz\t1\t1\n");
        HD_CHECK(engine.Reload(hdTrans::DictionaryOptions{!strict}) == strict);
        HD_CHECK_EQ(Translate(engine, "猫"), strict ? "새필터" : "기존필터");
        HD_CHECK(HasHeaderFilterError(engine, "after.tsv", 3));
        const auto accepted = engine.Dictionaries();
        const auto filters = engine.Filters();
        users.Write("after.tsv", "#! layer=postfilter\n고양이\t다른필터\t1\t0\n(x\tz\t1\t1\n");
        HD_CHECK(engine.Reload());
        HD_CHECK(engine.Dictionaries() != accepted);
        HD_CHECK(engine.Filters() != filters);
        HD_CHECK_EQ(Translate(engine, "猫"), "다른필터");
        HD_CHECK(HasHeaderFilterError(engine, "after.tsv", 3));
        const auto beforeStrict = engine.Dictionaries();
        HD_CHECK(!engine.Reload(hdTrans::DictionaryOptions{true}));
        HD_CHECK(engine.Dictionaries() == beforeStrict);
        HD_CHECK(engine.Reload());
    }
}

HD_TEST(EngineIgnoredEhndShortFieldsNeverRejectGoodRows) {
    for (const bool strict : {false, true}) {
        for (const auto cache : {hdTrans::CacheWrite::Off, hdTrans::CacheWrite::Sync}) {
            TempFolder dicts("short-dicts"), ehnd("short-ehnd");
            WriteHeaderFilterBase(dicts);
            const std::string text = "고양이\t첫필터\t1\t0\nTITLE\n개\t둘필터\t2\t\n";
            ehnd.Write("PostFilter.txt", text);
            hdTrans::EngineOptions options;
            options.strictDict = strict;
            const std::vector<std::string> folders{dicts.path.string()};
            hdTrans::Engine cold(false, cache, hdTrans::UserReload::Off, options);
            HD_REQUIRE(cold.OpenEhnd(ehnd.path.string()));
            HD_REQUIRE(cold.Open(folders));
            HD_CHECK(!hdTrans::internal::FilterFileFailed(cold));
            HD_CHECK(HasHeaderFilterError(cold, "PostFilter.txt", 2));
            HD_REQUIRE(cold.FilterErrors().size() == 1);
            HD_CHECK_EQ(Translate(cold, "猫"), "첫필터");
            HD_CHECK_EQ(Translate(cold, "犬"), "둘필터");
            hdTrans::Engine warm(false, cache, hdTrans::UserReload::Auto, options);
            HD_REQUIRE(warm.OpenEhnd(ehnd.path.string()));
            HD_REQUIRE(warm.Open(folders));
            const char* snapshotMode = std::getenv("HDTRANS_SNAPSHOT");
            if (cache == hdTrans::CacheWrite::Sync &&
                !(snapshotMode && std::string_view(snapshotMode) == "off") &&
                !hdTrans::dict::internal::SnapshotPath(folders, {}).empty()) {
                HD_CHECK(warm.Dictionaries()->Mapped());
            }
            HD_CHECK(!hdTrans::internal::FilterFileFailed(warm));
            HD_CHECK_EQ(Translate(warm, "猫"), "첫필터");
            HD_CHECK_EQ(Translate(warm, "犬"), "둘필터");
            for (const auto& file : hdTrans::internal::LoadedSourceInfo(warm).files) {
                if (file.kind != hdTrans::dict::LoadFileKind::EhndPostFilter) continue;
                HD_REQUIRE(file.acceptedCount.has_value());
                HD_CHECK_EQ(*file.acceptedCount, std::size_t{2});
            }
            ehnd.Write("PostFilter.txt", "고양이\t새필터\t1\t0\nTITLE\n개\t둘필터\t2\t\n");
            HD_CHECK(warm.Reload());
            HD_CHECK(!hdTrans::internal::FilterFileFailed(warm));
            HD_CHECK_EQ(Translate(warm, "猫"), "새필터");
            const auto good = warm.Dictionaries();
            const auto filters = warm.Filters();
            ehnd.Write("PostFilter.txt", text + "[\tx\t3\t1\n");
            HD_CHECK(hdTrans::internal::CheckUserFile(warm));
            HD_CHECK((warm.Dictionaries() == good) == strict);
            HD_CHECK((warm.Filters() == filters) == strict);
            HD_CHECK(hdTrans::internal::FilterFileFailed(warm) == strict);
            HD_CHECK_EQ(Translate(warm, "猫"), strict ? "새필터" : "첫필터");
            ehnd.Write("PostFilter.txt", text);
            HD_REQUIRE(warm.SetUserDictRaw("狐\t여우\n鳥\n", hdTrans::DictionaryOptions{false}));
            HD_REQUIRE(warm.Reload());
            HD_REQUIRE(warm.GetUserDict().size() == 1);
            HD_CHECK_EQ(Translate(warm, "狐"), "여우");
            HD_CHECK(warm.SetUserDictRaw("狐\t흰여우\n鳥\n") == !strict);
            HD_CHECK(warm.Reload());
        }
    }
}

HD_TEST(UnclassifiedUnreadableHeaderIsOptionalButAcceptedHeaderIOIsFatal) {
    for (const bool strict : {false, true}) {
        TempFolder dicts("probe-dicts"), users("probe-users");
        WriteHeaderFilterBase(dicts);
        users.Write("notes.txt", "plain notes\n");
        users.Write("after.tsv", "#! layer=postfilter\n고양이\t기존필터\t1\t0\n");
        users.Write("userdict.txt", "狐\t여우\n");
        const auto stray = users.path / "notes.txt";
        const auto header = users.path / "after.tsv";
        std::error_code error;
        const auto original = std::filesystem::status(stray, error).permissions();
        HD_REQUIRE(!error);
        std::filesystem::permissions(stray, std::filesystem::perms::none, error);
        HD_REQUIRE(!error);
        const std::vector<std::string> paths{users.path.string()};
        const auto observed = hdTrans::internal::UserVersionOf(paths);
        bool denied = false;
        for (const auto& file : observed.files) denied |= file.first == stray.string() && file.second.headerProbeFailed;
        if (!denied) {
            std::filesystem::permissions(stray, original, error);
            HD_SKIP("file read permissions are not enforced");
        }
        hdTrans::EngineOptions options;
        options.strictDict = strict;
        hdTrans::Engine engine(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto, options);
        const bool opened = engine.OpenUserDicts({dicts.path.string()}, false, paths);
        std::filesystem::permissions(stray, original, error);
        HD_REQUIRE(!error);
        HD_REQUIRE(opened);
        const auto warnings = hdTrans::internal::UserFileErrors(engine);
        HD_REQUIRE(warnings.size() == 1);
        HD_CHECK_EQ(warnings[0].file, stray.string());
        HD_CHECK(warnings[0].kind == hdTrans::dict::LoadErrorKind::OptionalSource);
        HD_CHECK(!hdTrans::dict::FailsLoad(warnings[0].kind));
        HD_CHECK_EQ(warnings[0].line, 0);
        HD_CHECK_EQ(Translate(engine, "猫"), "기존필터");
        HD_CHECK(engine.Reload());
        HD_CHECK(engine.UserFileError().empty());
        const auto good = engine.Dictionaries();
        const auto filters = engine.Filters();
        users.Write("userdict.txt", "狐\t금빛여우\n");
        const auto headerPermissions = std::filesystem::status(header, error).permissions();
        HD_REQUIRE(!error);
        std::filesystem::permissions(header, std::filesystem::perms::none, error);
        HD_REQUIRE(!error);
        const bool reloaded = engine.Reload();
        std::filesystem::permissions(header, headerPermissions, error);
        HD_REQUIRE(!error);
        HD_CHECK(!reloaded);
        HD_CHECK(engine.Dictionaries() == good);
        HD_CHECK(engine.Filters() == filters);
        const auto failed = hdTrans::internal::UserFileErrors(engine);
        HD_REQUIRE(!failed.empty());
        HD_CHECK(failed[0].kind == hdTrans::dict::LoadErrorKind::Read);
        HD_CHECK_EQ(failed[0].file, header.string());
        HD_CHECK_EQ(Translate(engine, "猫"), "기존필터");
        HD_CHECK(engine.Reload());
        HD_CHECK(engine.UserFileError().empty());
        HD_CHECK_EQ(Translate(engine, "狐"), "금빛여우");
        const auto history = hdTrans::internal::AcceptedUserSources(engine);
        std::filesystem::permissions(header, std::filesystem::perms::none, error);
        HD_REQUIRE(!error);
        hdTrans::Engine candidate(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Off, options);
        hdTrans::internal::SeedUserSources(candidate, history);
        const bool candidateLoaded = candidate.OpenUserDicts({dicts.path.string()}, false, paths);
        std::filesystem::permissions(header, headerPermissions, error);
        HD_REQUIRE(!error);
        HD_CHECK(!candidateLoaded);
        HD_CHECK(!candidate.loaded());
        const auto candidateErrors = hdTrans::internal::UserFileErrors(candidate);
        HD_REQUIRE(!candidateErrors.empty());
        HD_CHECK(candidateErrors[0].kind == hdTrans::dict::LoadErrorKind::Read);
        HD_CHECK_EQ(candidateErrors[0].file, header.string());
    }
}

HD_TEST(EngineUserFolderHeaderCache) {
    TempFolder dicts("header-cache-dicts"), users("header-cache-users"), cache("header-cache");
    const std::vector<std::string> folders{dicts.path.string()};
    const char* snapshotMode = std::getenv("HDTRANS_SNAPSHOT");
    if ((snapshotMode && std::string_view(snapshotMode) == "off") ||
        hdTrans::dict::internal::SnapshotPath(folders, cache.path.string()).empty()) {
        HD_SKIP("snapshot cache is disabled");
    }
    WriteHeaderFilterBase(dicts);
    users.Write("before.txt", "\xEF\xBB\xBF#! layer=prefilter\r\n# comment\r\n猫\t犬\t1\t0\r\n");
    users.Write("after.tsv", "#! layer=postfilter\n개\t필터값\t1\t0\n");
    const auto files = hdTrans::dict::UserFiles({users.path.string()});
    HD_REQUIRE(files.size() == 2);
    const auto base = hdTrans::dict::LoadFoldersTsv(folders);
    const auto cold = hdTrans::dict::LoadFoldersTsv(folders, files);
    const auto written = hdTrans::dict::LoadFolders(folders, cache.path.string(), false, true, files);
    const auto warm = hdTrans::dict::LoadFolders(folders, cache.path.string(), false, false, files);
    HD_REQUIRE(base.snapshot && cold.snapshot && written.snapshot && warm.snapshot);
    HD_CHECK(warm.snapshot->Mapped());
    HD_CHECK(warm.errors.empty());
    HD_CHECK(warm.snapshot->Plain().Entries().size() == base.snapshot->Plain().Entries().size());
    HD_CHECK(warm.snapshot->TextPlain().Entries().size() == base.snapshot->TextPlain().Entries().size());
    const auto& coldFiles = cold.snapshot->PlainFiles();
    const auto& warmFiles = warm.snapshot->PlainFiles();
    HD_REQUIRE(warmFiles.size() == coldFiles.size());
    std::size_t filterFiles = 0;
    for (std::size_t i = 0; i < warmFiles.size(); ++i) {
        HD_CHECK(warmFiles[i].name == coldFiles[i].name);
        HD_CHECK(warmFiles[i].text == coldFiles[i].text);
        HD_CHECK(warmFiles[i].layer == coldFiles[i].layer);
        HD_CHECK(warmFiles[i].user == coldFiles[i].user);
        if (warmFiles[i].layer == hdTrans::dict::FileLayer::PreFilter ||
            warmFiles[i].layer == hdTrans::dict::FileLayer::PostFilter) {
            ++filterFiles;
            HD_CHECK(warmFiles[i].user);
        }
    }
    HD_CHECK(filterFiles == 2);
}

HD_TEST(EngineUserFolderHeaderCompiled) {
    TempFolder dicts("header-image-dicts"), root("header-image-users");
    WriteHeaderFilterBase(dicts);
    const auto users = root.path / "users";
    const auto compiled = (root.path / "dicts.hdsnap").string();
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(users, error));
    root.Write("users/before.txt", "\xEF\xBB\xBF#! layer=prefilter\r\n猫\t犬\t1\t0\r\n");
    root.Write("users/after.tsv", "#! layer=postfilter\n개\t필터값\t1\t0\n");
    root.Write("users/userdict.txt", "狐\t여우\n");
    const std::vector<std::string> folders{dicts.path.string()};
    const auto files = hdTrans::dict::UserFiles({users.string()});
    HD_REQUIRE(files.size() == 3);
    const auto base = hdTrans::dict::LoadFoldersTsv(folders);
    const auto cold = hdTrans::dict::LoadFoldersTsv(folders, files);
    HD_REQUIRE(base.snapshot && cold.snapshot);
    HD_CHECK(cold.errors.empty());
    HD_CHECK(cold.snapshot->Plain().Entries().size() == base.snapshot->Plain().Entries().size() + 1);
    std::size_t filterFiles = 0;
    for (const auto& file : cold.snapshot->PlainFiles()) {
        if (file.layer == hdTrans::dict::FileLayer::PreFilter || file.layer == hdTrans::dict::FileLayer::PostFilter) {
            ++filterFiles;
            HD_CHECK(file.user);
        }
    }
    HD_CHECK(filterFiles == 2);
    HD_REQUIRE(hdTrans::dict::CompileSnapshot(folders, compiled));
    hdTrans::Engine bare(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(bare.Open({compiled}));
    HD_CHECK_EQ(Translate(bare, "猫"), "고양이");
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.OpenUserDicts({compiled}, false, {users.string()}));
    HD_CHECK_EQ(Translate(engine, "猫"), "필터값");
    HD_CHECK_EQ(Translate(engine, "狐"), "여우");
    HD_CHECK(engine.Dictionaries()->Plain().Entries().size() == cold.snapshot->Plain().Entries().size());
    HD_CHECK(engine.Dictionaries()->TextPlain().Entries().size() == cold.snapshot->TextPlain().Entries().size());
    HD_CHECK(engine.GetUserDict().empty());
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK(engine.FilterErrors().empty());
    const auto good = engine.Dictionaries();
    const auto filters = engine.Filters();
    root.Write("users/after.tsv", "#! layer=postfilter\r\n개\t이미지필터\t1\t0\r\n(x\tz\t1\t1\r\n");
    HD_CHECK(!engine.Reload());
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(engine.Filters() == filters);
    HD_CHECK_EQ(Translate(engine, "猫"), "필터값");
    HD_CHECK(HasHeaderFilterError(engine, "after.tsv", 3));
    HD_CHECK(engine.Reload(hdTrans::DictionaryOptions{false}));
    HD_CHECK_EQ(Translate(engine, "猫"), "이미지필터");
    HD_CHECK(HasHeaderFilterError(engine, "after.tsv", 3));
}
