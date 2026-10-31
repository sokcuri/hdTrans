#include "core/config/config.h"

#include <chrono>
#include <cstdlib>
#include <optional>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

HD_TEST(ConfigCarriesItsParseEntryStamp) {
    TempConfig file;
    const auto missing = hdTrans::internal::LoadConfig(file.path);
    HD_CHECK(!missing.found);
    HD_CHECK(!missing.sourceStamp);
    HD_CHECK(missing.diagnostics.empty());
    const std::string text = "strict_dict: false\n";
    const auto first = file.Load(text);
    HD_CHECK(first.found);
    HD_REQUIRE(first.sourceStamp.has_value());
    HD_CHECK_EQ(first.sourceStamp->size, static_cast<std::uint64_t>(text.size()));
    hdTrans::dict::internal::FileStamp current;
    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(file.path, current));
    HD_CHECK(current == *first.sourceStamp);
    const auto second = file.Load("strict_dict: true\n# changed version\n");
    HD_REQUIRE(second.sourceStamp.has_value());
    HD_CHECK(*second.sourceStamp != *first.sourceStamp);
    HD_CHECK(second.settings.strictDict == true);
}

HD_TEST(ConfigAllKeys) {
    TempConfig file;
    const auto result = file.Load(
        "# settings\ndicts: [dicts, '../extra']\nuserdict: user.tsv\nehnd: Ehnd\n"
        "cache: cache\ncg: false\nmisread: true\nreference: true\nspeech: strict\nwrapped: true\n");
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
    HD_CHECK(settings.wrapped);
}

HD_TEST(ConfigStrictDictionary) {
    TempConfig file;
    const auto missing = file.Load("cg: true\n");
    HD_CHECK(missing.diagnostics.empty());
    HD_CHECK(!missing.settings.strictDict.has_value());
    for (const bool strict : {false, true}) {
        const auto result = file.Load(strict ? "strict_dict: true\n" : "strict_dict: false\n");
        HD_CHECK(result.diagnostics.empty());
        HD_REQUIRE(result.settings.strictDict.has_value());
        HD_CHECK(*result.settings.strictDict == strict);
    }
    for (const char* value : {"'false'", "1", "yes"}) {
        const auto result = file.Load(std::string("strict_dict: ") + value + "\n");
        HD_REQUIRE(result.diagnostics.size() == 1);
        HD_CHECK_EQ(result.diagnostics[0].line, std::size_t{1});
        HD_CHECK_EQ(result.diagnostics[0].message, "strict_dict: expected a boolean (true or false)");
        HD_CHECK(!result.settings.strictDict.has_value());
    }
    const auto earlier = file.Load("strict_dict: false\nstrict_dict: invalid\n");
    HD_REQUIRE(earlier.settings.strictDict.has_value());
    HD_CHECK(!*earlier.settings.strictDict);
    HD_REQUIRE(earlier.diagnostics.size() == 1);
    HD_CHECK_EQ(earlier.diagnostics[0].line, std::size_t{2});
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

// Sets an environment variable (or clears it with nullopt) for one scope.
struct ScopedEnv {
    std::string name;
    std::optional<std::string> saved;
    ScopedEnv(std::string variable, const std::optional<std::string>& value) : name(std::move(variable)) {
        if (const char* old = std::getenv(name.c_str())) saved = old;
        Set(value);
    }
    ~ScopedEnv() { Set(saved); }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    void Set(const std::optional<std::string>& value) const {
#ifdef _WIN32
        _putenv_s(name.c_str(), value ? value->c_str() : "");
#else
        if (value) setenv(name.c_str(), value->c_str(), 1);
        else unsetenv(name.c_str());
#endif
    }
};

HD_TEST(ConfigSpeechLevels) {
    TempConfig file;
    const ScopedEnv cleared("HDTRANS_SPEECH_LEVELS", std::nullopt);
    HD_CHECK(file.Load("").settings.speechLevels == hdTrans::SpeechLevels::Auto);
    HD_CHECK(file.Load("speech_levels: keep\n").settings.speechLevels == hdTrans::SpeechLevels::Keep);
    HD_CHECK(file.Load("speech_levels: Auto\n").settings.speechLevels == hdTrans::SpeechLevels::Auto);
    const auto bad = file.Load("speech_levels: off\n");
    HD_REQUIRE(bad.diagnostics.size() == 1);
    HD_CHECK_EQ(bad.diagnostics[0].message, "speech_levels: expected auto or keep");
    HD_CHECK(!bad.diagnostics[0].environment);
}

HD_TEST(ConfigSpeechLevelsEnvironment) {
    TempConfig file;
    {
        const ScopedEnv keep("HDTRANS_SPEECH_LEVELS", "Keep");
        const auto result = file.Load("speech_levels: auto\n");
        HD_CHECK(result.diagnostics.empty());
        HD_CHECK(result.settings.speechLevels == hdTrans::SpeechLevels::Keep);
    }
    const ScopedEnv bad("HDTRANS_SPEECH_LEVELS", "off");
    const auto result = file.Load("speech_levels: keep\n");
    HD_REQUIRE(result.diagnostics.size() == 1);
    HD_CHECK(result.diagnostics[0].environment);
    HD_CHECK(result.settings.speechLevels == hdTrans::SpeechLevels::Keep);
    HD_CHECK_EQ(hdTrans::internal::ConfigErrors(result, "config.yaml"), "HDTRANS_SPEECH_LEVELS: expected auto or keep");
}

HD_TEST(ConfigAttachment) {
    TempConfig file;
    const ScopedEnv cleared("HDTRANS_ATTACHMENT", std::nullopt);
    HD_CHECK(file.Load("").settings.attachment == hdTrans::Attachment::Auto);
    HD_CHECK(file.Load("attachment: off\n").settings.attachment == hdTrans::Attachment::Off);
    HD_CHECK(file.Load("attachment: Auto\n").settings.attachment == hdTrans::Attachment::Auto);
    const auto bad = file.Load("attachment: keep\n");
    HD_REQUIRE(bad.diagnostics.size() == 1);
    HD_CHECK_EQ(bad.diagnostics[0].message, "attachment: expected auto or off");
    {
        const ScopedEnv off("HDTRANS_ATTACHMENT", "Off");
        const auto result = file.Load("attachment: auto\n");
        HD_CHECK(result.diagnostics.empty());
        HD_CHECK(result.settings.attachment == hdTrans::Attachment::Off);
    }
    const ScopedEnv wrong("HDTRANS_ATTACHMENT", "keep");
    const auto result = file.Load("attachment: off\n");
    HD_REQUIRE(result.diagnostics.size() == 1);
    HD_CHECK(result.diagnostics[0].environment);
    HD_CHECK(result.settings.attachment == hdTrans::Attachment::Off);
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
        HD_CHECK(result.settings.speech == hdTrans::Speech::Strict);
        HD_CHECK(!result.settings.wrapped);
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
        "wrapped: yes\n",
        "wrapped: 'true'\n",
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
