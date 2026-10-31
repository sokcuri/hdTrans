// A host program's user dictionary (Anemone's AneDic.txt) among the engine's user dictionaries.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/load_info.h"
#include "core/engine.h"
#include "core/engine/load_info.h"
#include "core/engine/user_watch.h"
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
    std::string Utf8() const {
        const auto s = path.u8string();
        return {reinterpret_cast<const char*>(s.data()), s.size()};
    }
};

std::string Translate(const hdTrans::Engine& e, std::string_view text) {
    hdTrans::Session s(e);
    return s.Translate(text);
}

hdTrans::filter::Source AneDic(std::string_view bytes) {
    return {hdTrans::filter::Source::Kind::UserDict, "C:/Anemone/AneDic.txt", hdTrans::filter::EhndText(bytes), false, true};
}

}  // namespace

HD_TEST(AneDicLines) {
    // The file as Anemone writes it: a UTF-8 mark, CR LF, its comment header.
    const std::string text = hdTrans::filter::EhndText(
        "\xEF\xBB\xBF// 아네모네 사전파일 V1\r\n//\r\n雪々\t유키\t1\tVHn\r\nお疲れ様\t수고했어\t0\r\n"
        "強制\t강제\t2\r\n門前\t문 앞\r\n一語\r\n\t빈 키\t1\r\n");
    std::vector<hdTrans::filter::Error> errors;
    const auto rows = hdTrans::filter::ParseUserDict(text, "AneDic.txt", errors);
    HD_REQUIRE(rows.size() == 4);
    HD_CHECK_EQ(rows[0].key, "雪々");
    HD_CHECK_EQ(rows[0].attr, "VHn");
    HD_CHECK(!rows[0].phrase);
    HD_CHECK(rows[1].phrase);
    HD_CHECK(rows[2].phrase);
    HD_CHECK(!rows[3].phrase);
    HD_REQUIRE(errors.size() == 2);
    HD_CHECK_EQ(errors[0].line, 7);
    HD_CHECK_EQ(errors[1].line, 8);
}

HD_TEST(EngineHostUserDicts) {
    TempFolder dicts("host-dicts"), user("host-user"), ehnd("host-ehnd");
    dicts.Write("base.tsv",
                "key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n"
                "犬\t개\tN110\t\t\tGN\t\t\t\t\t1\n");
    for (const bool strict : {false, true}) {
        hdTrans::EngineOptions options;
        options.strictDict = strict;
        hdTrans::Engine e(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Off, options);
        // A bad line is skipped and named, even in strict mode.
        hdTrans::internal::SetHostUserDicts(e, {AneDic("ポチ\t바둑이\t1\n日本語\t니혼고\t1\n" + std::string(31, 'k') + "\tx\n")});
        HD_REQUIRE(e.Open({dicts.Utf8()}));
        HD_CHECK_EQ(Translate(e, "ポチ"), "바둑이");
        HD_CHECK_EQ(Translate(e, "日本語"), "니혼고");
        const auto warnings = e.FilterErrors();
        HD_REQUIRE(warnings.size() == 1);
        HD_CHECK_EQ(warnings[0], "C:/Anemone/AneDic.txt:3: the key is longer than 30 bytes in CP932");
        std::size_t hostFiles = 0;
        for (const auto& file : hdTrans::internal::LoadedSourceInfo(e).files)
            hostFiles += file.kind == hdTrans::dict::LoadFileKind::HostUserDict && file.path == "C:/Anemone/AneDic.txt";
        HD_CHECK_EQ(hostFiles, std::size_t{1});

        // Its rows come before the Ehnd folder's and after the user files.
        ehnd.Write("UserDict_a.txt", "ポチ\t얼룩이\t1\n犬\t멍멍이\t1\n");
        HD_REQUIRE(e.OpenEhnd(ehnd.Utf8()));
        HD_CHECK_EQ(Translate(e, "ポチ"), "바둑이");
        HD_CHECK_EQ(Translate(e, "犬"), "멍멍이");
        user.Write("userdict.txt", "ポチ\t흰둥이\n");
        HD_REQUIRE(e.OpenUserDicts({dicts.Utf8()}, false, {user.Utf8()}));
        HD_CHECK_EQ(Translate(e, "ポチ"), "흰둥이");
        HD_CHECK_EQ(Translate(e, "日本語"), "니혼고");

        // The next load reads the dictionaries set then; none drops them.
        hdTrans::internal::SetHostUserDicts(e, {});
        HD_REQUIRE(e.Reload());
        HD_CHECK(Translate(e, "日本語") != "니혼고");
        std::filesystem::remove(user.path / "userdict.txt");
        std::filesystem::remove(ehnd.path / "UserDict_a.txt");
    }
}
