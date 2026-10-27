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

struct TempFolder {
    std::filesystem::path path;
    explicit TempFolder(const char* name) {
        std::error_code error;
        path = std::filesystem::temp_directory_path(error) /
            (std::string(name) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path, error);
    }
    ~TempFolder() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

}  // namespace

HD_TEST(DictFoldersOfProgram) {
    TempFolder temp("hdtrans-folders-");
    const auto data = temp.path / "hdtrans-data";
    const auto program = Utf8(temp.path);
    HD_CHECK(Path(DataFolder(program)) == data);
    HD_CHECK(Path(ShippedFolder(program)) == data / "dicts.hdsnap");
    HD_CHECK(Path(DefaultFolder(program)) == data / "dicts.hdsnap");
    HD_CHECK(Path(UserFolder(program)) == data / "user");
    HD_REQUIRE(UserPaths(program).size() == 1);
    HD_CHECK(Path(UserPaths(program).front()) == data / "user");
    HD_CHECK(Path(ConfigFile(program)) == data / "user/hdtrans.config.yaml");
    HD_CHECK(Path(EhndFolder(program)) == data / "ehnd");
    HD_CHECK(UserPaths("").empty());
    HD_CHECK_EQ(EhndFolder(""), "");
    HD_CHECK(FolderWarnings(program).empty());
    HD_CHECK(FolderWarnings("").empty());
}

HD_TEST(DictDataLayouts) {
    TempFolder temp("hdtrans-layouts-");
    const struct Case { const char* data; const char* user; const char* snapshot; const char* config;
                        const char* userdict; const char* ehnd; } cases[] = {
        {"hdtrans-data", "user", "dicts.hdsnap", "hdtrans.config.yaml", "userdict.tsv", "ehnd"},
        {"HDTRANS-DATA", "USER", "DICTS.HDSNAP", "HDTRANS.CONFIG.YAML", "USERDICT.TSV", "EHND"},
        {"HdTrans-Data", "User", "Dicts.HdSnap", "HdTrans.Config.yaml", "UserDict.tsv", "Ehnd"},
    };
    std::size_t index = 0;
    for (const auto& test : cases) {
        const auto program = temp.path / std::to_string(index++);
        const auto data = program / test.data;
        const auto user = data / test.user;
        std::error_code error;
        std::filesystem::create_directories(user, error);
        HD_REQUIRE(!error);
        std::filesystem::create_directory(data / test.ehnd, error);
        HD_REQUIRE(!error);
        std::ofstream(data / test.snapshot) << "snapshot\n";
        std::ofstream(user / test.userdict) << "# user dictionary\n";
        std::ofstream(user / test.config) << "cg: true\n";
        const auto path = Utf8(program);
        HD_CHECK(Path(DataFolder(path)) == data);
        HD_CHECK(Path(UserFolder(path)) == user);
        HD_CHECK(Path(ShippedFolder(path)) == data / test.snapshot);
        HD_CHECK(Path(DefaultFolder(path)) == data / test.snapshot);
        HD_CHECK(Path(ConfigFile(path)) == user / test.config);
        HD_REQUIRE(UserFiles(UserPaths(path)).size() == 1);
        HD_CHECK(Path(UserFiles(UserPaths(path)).front()) == user / test.userdict);
        HD_CHECK(Path(EhndFolder(path)) == data / test.ehnd);
        HD_CHECK(FolderWarnings(path).empty());
        std::filesystem::remove(data / test.snapshot, error);
        HD_CHECK(Path(ShippedFolder(path)) == data / "dicts.hdsnap");
        HD_CHECK(Path(DefaultFolder(path)) == data / "dicts.hdsnap");
    }
}

HD_TEST(DictDirectoryCaseCollisions) {
    TempFolder temp("hdtrans-folder-cases-");
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(temp.path / "CaseProbe", error));
    const bool distinct = std::filesystem::create_directory(temp.path / "caseprobe", error);
    HD_REQUIRE(!error);
    if (!distinct) HD_SKIP("filesystem does not distinguish directory case variants");
    for (const char* name : {"HDTRANS-DATA", "Hdtrans-Data", "hdtrans-data"}) {
        HD_REQUIRE(std::filesystem::create_directory(temp.path / name, error));
    }
    const auto data = temp.path / "hdtrans-data";
    for (const char* name : {"USER", "User", "user", "EHND", "Ehnd", "ehnd"}) {
        HD_REQUIRE(std::filesystem::create_directory(data / name, error));
    }
    const auto program = Utf8(temp.path);
    HD_CHECK(Path(DataFolder(program)) == data);
    HD_CHECK(Path(UserFolder(program)) == data / "user");
    HD_CHECK(Path(EhndFolder(program)) == data / "ehnd");
    auto warnings = FolderWarnings(program);
    HD_REQUIRE(warnings.size() == 3);
    HD_CHECK(warnings[0].find("HDTRANS-DATA, Hdtrans-Data, hdtrans-data") != std::string::npos);
    HD_CHECK(warnings[0].find("hdtrans-data를 씁니다") != std::string::npos);
    HD_CHECK(warnings[1].find("USER, User, user") != std::string::npos);
    HD_CHECK(warnings[1].find("user를 씁니다") != std::string::npos);
    HD_CHECK(warnings[2].find("EHND, Ehnd, ehnd") != std::string::npos);
    HD_CHECK(warnings[2].find("ehnd를 씁니다") != std::string::npos);
    std::filesystem::remove(data / "user", error);
    std::filesystem::remove(data / "ehnd", error);
    HD_CHECK(Path(UserFolder(program)) == data / "USER");
    HD_CHECK(Path(EhndFolder(program)) == data / "EHND");
    warnings = FolderWarnings(program);
    HD_REQUIRE(warnings.size() == 3);
    HD_CHECK(warnings[1].find("USER를 씁니다") != std::string::npos);
    HD_CHECK(warnings[2].find("EHND를 씁니다") != std::string::npos);
    std::filesystem::remove_all(data, error);
    HD_CHECK(Path(DataFolder(program)) == temp.path / "HDTRANS-DATA");
    warnings = FolderWarnings(program);
    HD_REQUIRE(warnings.size() == 1);
    HD_CHECK(warnings[0].find("HDTRANS-DATA를 씁니다") != std::string::npos);
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
