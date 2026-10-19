#include "core/dict/folders.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "hdtest.h"

using namespace hdTrans::dict;

namespace {

std::filesystem::path Path(const std::string& utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

}  // namespace

HD_TEST(DictFoldersOfProgram) {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) /
                     ("hdtrans-folders-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    HD_REQUIRE(std::filesystem::create_directory(dir, ec));
    const std::string program = Utf8(dir);

    HD_CHECK(Path(ShippedFolder(program)) == dir / "dicts");
    HD_REQUIRE(UserPaths(program).size() == 1);
    HD_CHECK(Path(UserPaths(program).front()) == dir / "user");
    HD_CHECK(UserPaths("").empty());
    // Without dicts next to the program, dicts in the current directory.
    HD_CHECK_EQ(DefaultFolder(program), "dicts");
    HD_CHECK_EQ(DefaultFolder(""), "dicts");
    std::filesystem::create_directory(dir / "dicts", ec);
    HD_REQUIRE(!ec);
    HD_CHECK(Path(DefaultFolder(program)) == dir / "dicts");
    // The Ehnd folder only when it is there.
    HD_CHECK_EQ(EhndFolder(program), "");
    HD_CHECK_EQ(EhndFolder(""), "");
    std::filesystem::create_directory(dir / "Ehnd", ec);
    HD_REQUIRE(!ec);
    HD_CHECK(Path(EhndFolder(program)) == dir / "Ehnd");

    std::filesystem::remove_all(dir, ec);
}

HD_TEST(DictDataLayouts) {
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        ("hdtrans-layouts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const struct Case { const char* data; const char* user; const char* dicts; const char* userdict; const char* config; } cases[] = {
        {"", "", "dicts", "userdict.tsv", "hdtrans.config.yaml"},
        {"", "user", "dicts", "userdict.tsv", "hdtrans.config.yaml"},
        {"hdTrans", "user", "dicts", "userdict.tsv", "hdtrans.config.yaml"},
        {"hdtrans", "USER", "DICTS", "USERDICT.TSV", "HDTRANS.CONFIG.YAML"},
        {"HDTRANS", "User", "Dicts", "UserDict.tsv", "HdTrans.Config.yaml"},
    };
    std::size_t index = 0;
    for (const auto& test : cases) {
        const auto program = root / std::to_string(index++);
        const auto data = test.data[0] ? program / test.data : program;
        const auto user = test.user[0] ? data / test.user : data;
        std::filesystem::create_directories(program, error);
        if (!test.data[0]) std::ofstream(program / "hdtrans") << "executable";
        std::filesystem::create_directories(data / test.dicts, error);
        HD_REQUIRE(!error);
        std::filesystem::create_directories(user, error);
        HD_REQUIRE(!error);
        std::ofstream(user / test.userdict) << "# user dictionary\n";
        std::ofstream(user / test.config) << "cg: true\n";
        std::filesystem::create_directory(program / "EHND", error);
        HD_REQUIRE(!error);
        const auto path = Utf8(program);
        HD_CHECK(Path(DataFolder(path)) == data);
        HD_CHECK(Path(UserFolder(path)) == (test.user[0] ? user : data / "user"));
        HD_CHECK(Path(ShippedFolder(path)) == data / test.dicts);
        HD_CHECK(Path(DefaultFolder(path)) == data / test.dicts);
        HD_REQUIRE(!UserFiles(UserPaths(path)).empty());
        HD_CHECK(Path(UserFiles(UserPaths(path)).front()) == user / test.userdict);
        HD_CHECK(Path(ConfigFile(path)) == user / test.config);
        HD_CHECK(Path(EhndFolder(path)) == program / "EHND");
        HD_CHECK(Path(FindChild(path, "missing")) == program / "missing");
        if (test.data[0]) {
            std::filesystem::create_directory(program / "dicts", error);
            std::ofstream(program / "dicts.hdsnap") << "outer snapshot\n";
            std::ofstream(program / "userdict.tsv") << "# legacy\n";
            std::ofstream(program / "hdtrans.config.yaml") << "cg: false\n";
            HD_CHECK(Path(ShippedFolder(path)) == data / test.dicts);
            HD_REQUIRE(!UserFiles(UserPaths(path)).empty());
            HD_CHECK(Path(UserFiles(UserPaths(path)).front()) == user / test.userdict);
            HD_CHECK(Path(ConfigFile(path)) == user / test.config);
        }
        const auto snapshot = data / "DiCtS.HdSnAp";
        std::ofstream(snapshot) << "damaged snapshot\n";
        HD_CHECK(Path(ShippedFolder(path)) == snapshot);
        HD_CHECK(Path(DefaultFolder(path)) == snapshot);
        HD_CHECK(Path(UserFolder(path)) == (test.user[0] ? user : data / "user"));
        HD_CHECK(Path(UserFiles(UserPaths(path)).front()) == user / test.userdict);
        HD_CHECK(Path(ConfigFile(path)) == user / test.config);
        std::filesystem::remove_all(data / test.dicts, error);
        HD_CHECK(Path(ShippedFolder(path)) == snapshot);
        HD_CHECK(Path(DefaultFolder(path)) == snapshot);
        std::filesystem::remove(snapshot, error);
        std::filesystem::create_directory(snapshot, error);
        HD_CHECK(Path(ShippedFolder(path)) == data / "dicts");
        HD_CHECK_EQ(DefaultFolder(path), "dicts");
    }
    std::filesystem::remove_all(root, error);
}

HD_TEST(UserDictionaryDiscovery) {
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        ("hdtrans-discovery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto folder = root / std::filesystem::path(u8"사용자");
    std::filesystem::create_directories(folder / "userdict-sub.txt", error);
    HD_REQUIRE(!error);
    const std::vector<std::string> expected{
        "userdict-2.tsv", "userdict.txt", "USERDICT_A.TXT", "UserDict_Z.tSv", "userdict한글.tsv"};
    for (const std::string& name : expected) std::ofstream(folder / Path(name)) << "# comment\n";
    for (const char* name : {"other.tsv", "userdict.tsv.bak", "xuserdict.txt", "userdict.yaml"}) {
        std::ofstream(folder / name) << "# ignored\n";
    }
    std::ofstream(folder / "userdict-sub.txt/userdict.tsv") << "# nested\n";
    const auto files = UserFiles({Utf8(folder)});
    HD_REQUIRE(files.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        HD_CHECK_EQ(Utf8(Path(files[i]).filename()), expected[i]);
    }
    const std::string explicitFile = Utf8(folder / "other.tsv");
    const auto explicitPaths = UserFiles({explicitFile, "", Utf8(folder), explicitFile});
    HD_REQUIRE(explicitPaths.size() == expected.size() + 2);
    HD_CHECK_EQ(explicitPaths.front(), explicitFile);
    HD_CHECK_EQ(explicitPaths.back(), explicitFile);
    std::filesystem::remove_all(root, error);
}
