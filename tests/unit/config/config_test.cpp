#include "core/config/config.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "hdtest.h"

namespace {

std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

struct TempConfig {
    std::filesystem::path folder;
    std::string path;

    TempConfig() {
        std::error_code error;
        folder = std::filesystem::temp_directory_path(error) /
                 ("hdtrans-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(folder, error);
        path = Utf8(folder / "hdtrans.config.yaml");
    }
    ~TempConfig() {
        std::error_code error;
        std::filesystem::remove_all(folder, error);
    }
    hdTrans::internal::ConfigResult Load(const std::string& text) const {
        std::ofstream(folder / "hdtrans.config.yaml", std::ios::binary) << text;
        return hdTrans::internal::LoadConfig(path);
    }
    std::string Resolve(const char* name) const {
        const std::string_view text(name);
        const std::u8string u8(text.begin(), text.end());  // a narrow path is the ANSI code page on Windows
        return Utf8((folder / std::filesystem::path(u8)).lexically_normal());
    }
};

}  // namespace

HD_TEST(ConfigAllKeys) {
    TempConfig file;
    const auto result = file.Load(
        "# settings\ndicts: [dicts, '../extra']\nuserdict: user.tsv\nehnd: Ehnd\n"
        "cache: cache\ncg: false\nmisread: true\nreference: true\nspeech: strict\n");
    HD_CHECK(result.found);
    HD_REQUIRE(result.diagnostics.empty());
    const auto& settings = result.settings;
    HD_REQUIRE(settings.dicts && settings.dicts->size() == 2);
    HD_CHECK_EQ((*settings.dicts)[0], file.Resolve("dicts"));
    HD_CHECK_EQ((*settings.dicts)[1], file.Resolve("../extra"));
    HD_REQUIRE(settings.userdict && settings.ehnd && settings.cache);
    HD_REQUIRE(settings.userdict->size() == 1);
    HD_CHECK_EQ(settings.userdict->front(), file.Resolve("user.tsv"));
    HD_CHECK_EQ(*settings.ehnd, file.Resolve("Ehnd"));
    HD_CHECK_EQ(*settings.cache, file.Resolve("cache"));
    HD_CHECK(!settings.cg);
    HD_CHECK(settings.misread);
    HD_CHECK(settings.reference);
    HD_CHECK(settings.speech == hdTrans::Speech::Strict);
}

HD_TEST(ConfigSpeech) {
    TempConfig file;
    const struct Case { const char* yaml; hdTrans::Speech want; } cases[] = {
        {"speech: soft\n", hdTrans::Speech::Soft},
        {"speech: strict\n", hdTrans::Speech::Strict},
        {"speech: Strict\n", hdTrans::Speech::Strict},
    };
    for (const auto& test : cases) {
        const auto result = file.Load(test.yaml);
        HD_CHECK(result.diagnostics.empty());
        HD_CHECK(result.settings.speech == test.want);
    }
}

HD_TEST(ConfigPaths) {
    TempConfig file;
    const struct Case { const char* yaml; const char* path; } cases[] = {
        {"dicts: dicts\n", "dicts"},
        {"dicts: '사전 폴더'\n", "사전 폴더"},
        {"dicts: !!str 42\n", "42"},
        {"dicts: './a/../dicts'\n", "dicts"},
    };
    for (const auto& test : cases) {
        const auto result = file.Load(test.yaml);
        HD_CHECK(result.diagnostics.empty());
        HD_REQUIRE(result.settings.dicts && result.settings.dicts->size() == 1);
        HD_CHECK_EQ(result.settings.dicts->front(), file.Resolve(test.path));
    }
    const std::string absolute = file.Resolve("absolute");
    const auto result = file.Load("userdict: '" + absolute + "'\nehnd: ''\ncache: OFF\n");
    HD_REQUIRE(result.diagnostics.empty());
    HD_REQUIRE(result.settings.userdict && result.settings.ehnd && result.settings.cache);
    HD_REQUIRE(result.settings.userdict->size() == 1);
    HD_CHECK_EQ(result.settings.userdict->front(), absolute);
    HD_CHECK(result.settings.ehnd->empty());
    HD_CHECK_EQ(*result.settings.cache, "off");
}

HD_TEST(ConfigEmptyAndMissing) {
    TempConfig file;
    const auto missing = hdTrans::internal::LoadConfig(file.path);
    HD_CHECK(!missing.found);
    HD_CHECK(missing.diagnostics.empty());
    for (const char* text : {"", "# empty\n", "---\n", "{}\n"}) {
        const auto result = file.Load(text);
        HD_CHECK(result.found);
        HD_CHECK(result.diagnostics.empty());
        HD_CHECK(!result.settings.dicts);
        HD_CHECK(!result.settings.userdict);
        HD_CHECK(!result.settings.ehnd);
        HD_CHECK(!result.settings.useEhnd);
        HD_CHECK(!result.settings.cache);
        HD_CHECK(result.settings.cg);
        HD_CHECK(!result.settings.misread);
        HD_CHECK(!result.settings.reference);
        HD_CHECK(result.settings.speech == hdTrans::Speech::Soft);
    }
}

HD_TEST(ConfigInvalidEntries) {
    TempConfig file;
    const char* cases[] = {
        "unknown: {nested: [x, y]}\n",
        "dicts: {folder: dicts}\n",
        "dicts: [dicts, 123]\n",
        "userdict: false\n",
        "ehnd: [Ehnd]\n",
        "cache: true\n",
        "cg: 'false'\n",
        "cg: 1\n",
        "misread: yes\n",
        "misread: null\n",
        "reference: []\n",
        "speech: always\n",
        "speech: true\n",
        "speech: 1\n",
        "speech: [soft]\n",
        "userdict: 123\n",
        "userdict: !!int 42\n",
        "userdict: \"a\\0b\"\n",
        "? [key]\n: value\n",
    };
    for (const char* text : cases) {
        const auto result = file.Load(std::string(text) + "reference: true\n");
        HD_REQUIRE(result.diagnostics.size() == 1);
        HD_CHECK_EQ(result.diagnostics.front().line, std::size_t{1});
        HD_CHECK(!result.diagnostics.front().message.empty());
        HD_CHECK(result.settings.reference);
        HD_CHECK(!result.settings.dicts);
    }
    const auto unknown = file.Load("cg: false\nunknown: value\nmisread: true\n");
    HD_REQUIRE(unknown.diagnostics.size() == 1);
    HD_CHECK_EQ(unknown.diagnostics[0].line, std::size_t{2});
    HD_CHECK_EQ(unknown.diagnostics[0].message, "unknown key: unknown");
    HD_CHECK(!unknown.settings.cg);
    HD_CHECK(unknown.settings.misread);
}

HD_TEST(ConfigSyntaxAndDocuments) {
    TempConfig file;
    const auto syntax = file.Load("cg: false\ndicts: [a,\n");
    HD_REQUIRE(syntax.diagnostics.size() == 1);
    HD_CHECK_EQ(syntax.diagnostics[0].line, std::size_t{3});
    HD_CHECK(!syntax.settings.cg);
    for (const char* text : {"[a, b]\n", "text\n", "cg: false\n---\nmisread: true\n"}) {
        const auto result = file.Load(text);
        HD_REQUIRE(result.diagnostics.size() == 1);
        HD_CHECK(result.diagnostics[0].line > 0);
    }
    const auto alias = file.Load("userdict: &path user.tsv\nehnd: *path\ncg: false\n");
    HD_REQUIRE(alias.diagnostics.size() == 1);
    HD_CHECK(!alias.settings.cg);
}

HD_TEST(ConfigEhndSwitch) {
    TempConfig file;
    const struct Case { const char* yaml; bool enabled; const char* path; } cases[] = {
        {"ehnd: true\n", true, nullptr},
        {"ehnd: false\n", false, nullptr},
        {"ehnd: Ehnd\n", true, "Ehnd"},
        {"ehnd: 'true'\n", true, "true"},
        {"ehnd: true\nehnd: false\n", false, nullptr},
        {"ehnd: true\nehnd: custom\n", true, "custom"},
        {"ehnd: custom\nehnd: true\n", true, nullptr},
    };
    for (const auto& test : cases) {
        const auto result = file.Load(test.yaml);
        HD_CHECK(result.diagnostics.empty());
        HD_CHECK_EQ(result.settings.useEhnd, test.enabled);
        if (test.path) {
            HD_REQUIRE(result.settings.ehnd);
            HD_CHECK_EQ(*result.settings.ehnd, file.Resolve(test.path));
        } else HD_CHECK(!result.settings.ehnd);
    }
}

HD_TEST(ConfigUserDictionaryList) {
    TempConfig file;
    const auto result = file.Load("userdict: [first.tsv, '사용자 폴더', second.txt]\n");
    HD_REQUIRE(result.diagnostics.empty());
    HD_REQUIRE(result.settings.userdict && result.settings.userdict->size() == 3);
    HD_CHECK_EQ((*result.settings.userdict)[0], file.Resolve("first.tsv"));
    HD_CHECK_EQ((*result.settings.userdict)[1], file.Resolve("사용자 폴더"));
    HD_CHECK_EQ((*result.settings.userdict)[2], file.Resolve("second.txt"));
    for (const char* text : {"userdict: []\n", "userdict: ''\n"}) {
        const auto empty = file.Load(text);
        HD_REQUIRE(empty.settings.userdict);
        HD_CHECK(empty.diagnostics.empty());
        HD_CHECK(empty.settings.userdict->empty() || empty.settings.userdict->front().empty());
    }
    const auto invalid = file.Load("userdict: first.tsv\nuserdict: [second.tsv, 42]\ncg: false\n");
    HD_REQUIRE(invalid.diagnostics.size() == 1 && invalid.settings.userdict);
    HD_CHECK_EQ(invalid.settings.userdict->front(), file.Resolve("first.tsv"));
    HD_CHECK(!invalid.settings.cg);
}
