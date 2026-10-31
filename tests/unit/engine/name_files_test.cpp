// The name lists of the user folders (names*.txt/tsv): loaded and reloaded with the user
// dictionaries, under the list of the session. Needs HDTRANS_DICTS.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "core/dict/folders.h"
#include "core/engine.h"
#include "core/engine/user_watch.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

struct TempFolder {
    std::filesystem::path path;
    TempFolder() {
        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec) /
               ("hdtrans-names-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(path, ec);
    }
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

bool Write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    return f.good();
}

}  // namespace

HD_TEST(engine_name_files_discovery) {
    TempFolder temp;
    HD_REQUIRE(Write(temp.path / "userdict.txt", "猫\t고양이\n"));
    HD_REQUIRE(Write(temp.path / "Names-b.tsv", "b\n"));
    HD_REQUIRE(Write(temp.path / "names.txt", "a\n"));
    HD_REQUIRE(Write(temp.path / "names.md", "c\n"));
    std::vector<dict::UserSource> sources;
    std::vector<std::string> names;
    const auto files = dict::UserFiles({dict::UserSource{temp.path.string()}}, sources, nullptr, &names);
    HD_REQUIRE(files.size() == 1);
    HD_CHECK(files[0].ends_with("userdict.txt"));
    HD_REQUIRE(names.size() == 2);
    HD_CHECK(names[0].ends_with("Names-b.tsv"));  // in lowercase filename byte order
    HD_CHECK(names[1].ends_with("names.txt"));
}

HD_TEST(engine_name_files) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    TempFolder temp;
    const auto list = temp.path / "names.txt";
    HD_REQUIRE(Write(list, "# cast\n衣更\t이사라\n若葉\t와카바\nbad\n"));
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(engine.OpenUserDicts({dicts}, false, {temp.path.string()}));
    // A malformed line is reported and left out; the other names load.
    HD_CHECK_EQ(engine.UserFileError(), list.string() + ":4: a name needs JA and KO");
    Session session(engine);
    // One line per call reads the names as a document does.
    HD_CHECK_EQ(session.Translate("衣更は今日も元気だ。"), std::string("이사라는 오늘도 건강하다."));
    HD_CHECK_EQ(session.Translate("「衣更、こっちに来て」"), std::string("「이사라, 여기에 와」"));
    HD_CHECK_EQ(session.Translate("若葉マークだ。"), std::string("초보운전자 마크다."));  // a longer word

    // The list of the session replaces a name of the files whole and keeps the others.
    HD_CHECK(session.SetNames("衣更\t키사라\tfamily"));
    HD_CHECK_EQ(session.Translate("衣更は今日も元気だ。"), std::string("키사라는 오늘도 건강하다."));
    HD_CHECK_EQ(session.Translate("若葉は笑った。"), std::string("와카바는 웃었다."));
    std::vector<std::string> errors;
    HD_CHECK(!session.SetNames("睦月\n若葉\t와카바", &errors));
    HD_REQUIRE(errors.size() == 1);
    HD_CHECK_EQ(errors[0], std::string("1: a name needs JA and KO"));
    HD_CHECK_EQ(session.Translate("衣更は今日も元気だ。"), std::string("이사라는 오늘도 건강하다."));

    // An edit reloads the names with the user dictionaries, and the list of the session stays.
    HD_CHECK(session.SetNames("若葉\t와카바"));
    HD_REQUIRE(Write(list, "衣更\t키사라\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK_EQ(session.Translate("衣更は今日も元気だ。"), std::string("키사라는 오늘도 건강하다."));
    HD_CHECK_EQ(session.Translate("若葉は笑った。"), std::string("와카바는 웃었다."));

    // A removed file takes its names away.
    std::error_code ec;
    HD_REQUIRE(std::filesystem::remove(list, ec));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("衣更は今日も元気だ。"), std::string("의갱은 오늘도 건강하다."));
    HD_CHECK(session.SetNames(""));
    HD_CHECK_EQ(session.Translate("若葉は笑った。"), std::string("새잎은 웃었다."));

    // A later file replaces a name of an earlier one.
    HD_REQUIRE(Write(temp.path / "names-a.txt", "衣更\t이사라\n"));
    HD_REQUIRE(Write(temp.path / "names-b.txt", "衣更\t키사라\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("衣更は今日も元気だ。"), std::string("키사라는 오늘도 건강하다."));

    // A file that is not UTF-8 is left out whole and reported once.
    const auto legacy = temp.path / "names-c.txt";
    HD_REQUIRE(Write(legacy, "\x88\xDF\x8D\x58\t\xC0\xCC\xBB\xE7\xB6\xF3\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK_EQ(engine.UserFileError(), legacy.string() + ": invalid UTF-8");
    HD_CHECK_EQ(session.Translate("衣更は今日も元気だ。"), std::string("키사라는 오늘도 건강하다."));
}

// Calls keep translating while the names reload under them.
HD_TEST(engine_name_files_reload_concurrent) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    TempFolder temp;
    const auto list = temp.path / "names.txt";
    HD_REQUIRE(Write(list, "衣更\t이사라\n"));
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(engine.OpenUserDicts({dicts}, false, {temp.path.string()}));
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 3; i++) {
        workers.emplace_back([&] {
            Session session(engine);
            while (!stop.load()) {
                const std::string out = session.Translate("衣更は今日も元気だ。");
                if (out != "이사라는 오늘도 건강하다." && out != "키사라는 오늘도 건강하다.") bad++;
            }
        });
    }
    for (int round = 0; round < 4; round++) {
        HD_REQUIRE(Write(list, round % 2 == 0 ? "衣更\t키사라\n" : "衣更\t이사라\n"));
        // A call may have started the reload already.
        static_cast<void>(internal::CheckUserFile(engine));
    }
    stop = true;
    for (std::thread& worker : workers) worker.join();
    HD_CHECK_EQ(bad.load(), 0);
    static_cast<void>(internal::CheckUserFile(engine));
    Session session(engine);
    HD_CHECK_EQ(session.Translate("衣更は今日も元気だ。"), std::string("이사라는 오늘도 건강하다."));
}
