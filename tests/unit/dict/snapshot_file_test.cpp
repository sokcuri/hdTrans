#include "core/dict/snapshot_file.h"

#include <cstdint>
#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
#include <string>
#include <tuple>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/dict/mapped_file.h"
#include "core/dict/integrity.h"
#include "core/engine.h"
#include "core/engine/driver.h"
#include "core/names/document.h"
#include "hdtest.h"

namespace {

std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

struct ScopedEnvironment {
#ifdef _WIN32
    std::wstring name, previous;
#else
    std::string name, previous;
#endif
    bool present = false;

    ScopedEnvironment(const char* key, const char* value) {
#ifdef _WIN32
        name = Utf8Path(key).native();
        SetLastError(ERROR_SUCCESS);
        const auto size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
        present = size != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        if (size) {
            previous.resize(size);
            const auto length = GetEnvironmentVariableW(name.c_str(), previous.data(), size);
            previous.resize(length);
        }
#else
        name = key;
        const char* old = std::getenv(key);
        present = old != nullptr;
        if (old) previous = old;
#endif
        Set(value);
    }

    void Set(const char* value) const {
#ifdef _WIN32
        const bool ok = SetEnvironmentVariableW(name.c_str(), value ? Utf8Path(value).c_str() : nullptr) != 0;
        HD_CHECK(ok || (!value && GetLastError() == ERROR_ENVVAR_NOT_FOUND));
#else
        HD_CHECK((value ? setenv(name.c_str(), value, 1) : unsetenv(name.c_str())) == 0);
#endif
    }

    bool Matches(const char* value) const {
#ifdef _WIN32
        SetLastError(ERROR_SUCCESS);
        const auto size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
        if (size == 0) {
            const bool exists = GetLastError() != ERROR_ENVVAR_NOT_FOUND;
            return value ? exists && value[0] == '\0' : !exists;
        }
        if (!value) return false;
        std::wstring current(size, L'\0');
        const auto length = GetEnvironmentVariableW(name.c_str(), current.data(), size);
        current.resize(length);
        return current == Utf8Path(value).native();
#else
        const char* current = std::getenv(name.c_str());
        return value ? current && std::string_view(current) == value : current == nullptr;
#endif
    }

    ~ScopedEnvironment() {
#ifdef _WIN32
        SetEnvironmentVariableW(name.c_str(), present ? previous.c_str() : nullptr);
#else
        if (present) setenv(name.c_str(), previous.c_str(), 1);
        else unsetenv(name.c_str());
#endif
    }
};

struct TempFolder {
    std::filesystem::path path;
    TempFolder() {
        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec) /
               ("hdtrans-snapshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path, ec);
    }
    ~TempFolder() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::string File(std::string_view name) const {
        const auto text = (path / std::filesystem::path(name)).u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }
};

struct ScopedCurrentPath {
    std::filesystem::path previous;
    bool changed = false;

    explicit ScopedCurrentPath(const std::filesystem::path& path) {
        std::error_code ec;
        previous = std::filesystem::current_path(ec);
        if (!ec) {
            std::filesystem::current_path(path, ec);
            changed = !ec;
        }
        HD_CHECK(changed);
    }

    ~ScopedCurrentPath() {
        if (!changed) return;
        std::error_code ec;
        std::filesystem::current_path(previous, ec);
        HD_CHECK(!ec);
    }
};

struct CacheTestEnvironment {
    TempFolder storage;
    ScopedEnvironment cache{"HDTRANS_CACHE_DIR", storage.File("cache").c_str()};
    ScopedEnvironment snapshot{"HDTRANS_SNAPSHOT", nullptr};
    ScopedEnvironment strict{"HDTRANS_VERIFY_DICTS", nullptr};
};

std::string FixtureFolder() {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "testdata" / "folder";
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

}  // namespace

HD_TEST(SnapshotFileWriter) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto file = temp.File("dict.hdsnap");
    HD_REQUIRE(hdTrans::dict::CompileSnapshot({FixtureFolder()}, file));
    const auto mapped = hdTrans::dict::internal::MappedFile::Open(file);
    HD_REQUIRE(mapped != nullptr);
    const auto bytes = mapped->Bytes();
    HD_REQUIRE(bytes.size() > 256);
    HD_CHECK(std::memcmp(bytes.data(), "HDSNAP\r\n", 8) == 0);
    HD_CHECK(bytes[8] == std::byte{1});
    HD_CHECK(bytes[12] == std::byte{2});
    HD_CHECK(!hdTrans::dict::CompileSnapshot({temp.File("absent")}, temp.File("bad.hdsnap")));
    HD_CHECK(!std::filesystem::exists(temp.path / "bad.hdsnap"));
    const auto cache = hdTrans::dict::internal::SnapshotPath({FixtureFolder()}, temp.File("cache"));
    HD_REQUIRE(!cache.empty());
    HD_CHECK(cache.find(".latest") != std::string::npos);
    HD_CHECK(cache != hdTrans::dict::internal::SnapshotPath({FixtureFolder(), FixtureFolder()}, temp.File("cache")));
}

HD_TEST(SnapshotFileRoundTrip) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const std::vector<std::string> folders = {FixtureFolder(), FixtureFolder() + "/over"};
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    const auto cache = temp.File("cache");
    const auto first = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(first.snapshot != nullptr);
    const auto loaded = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
    HD_REQUIRE(loaded.snapshot->Healthy());
    HD_CHECK_EQ(loaded.errors.size(), editable.errors.size());
    HD_CHECK(loaded.snapshot->Warnings() == editable.snapshot->Warnings());
    HD_CHECK_EQ(loaded.snapshot->Base().Len(), editable.snapshot->Base().Len());
    for (bool extra : {false, true}) {
        HD_CHECK_EQ(loaded.snapshot->Plain(extra).Entries().size(), editable.snapshot->Plain(extra).Entries().size());
        HD_CHECK_EQ(loaded.snapshot->TextPlain(extra).Entries().size(), editable.snapshot->TextPlain(extra).Entries().size());
        for (const auto& pair : {std::pair{&loaded.snapshot->Plain(extra), &editable.snapshot->Plain(extra)},
                                std::pair{&loaded.snapshot->TextPlain(extra), &editable.snapshot->TextPlain(extra)}}) {
            const auto& a = pair.first->Entries();
            const auto& b = pair.second->Entries();
            HD_REQUIRE(a.size() == b.size());
            for (size_t i = 0; i < a.size(); i++) {
                auto fields = [](const hdTrans::dict::UserEntry& e) {
                    return std::tie(e.key, e.target, e.negative, e.prio, e.file, e.line, e.order, e.name, e.nameLayer,
                                    e.kana, e.force, e.hidden, e.finalClass, e.honorific, e.end, e.registerLevel,
                                    e.polite, e.stative, e.euImperative);
                };
                HD_CHECK(fields(a[i]) == fields(b[i]));
                HD_REQUIRE(a[i].codes != nullptr && b[i].codes != nullptr);
                HD_CHECK_EQ(a[i].codes->partText, b[i].codes->partText);
                HD_CHECK(a[i].codes->sem.Text() == b[i].codes->sem.Text());
                HD_CHECK(a[i].codes->gram.Text() == b[i].codes->gram.Text());
                HD_CHECK(a[i].codes->dict.Text() == b[i].codes->dict.Text());
            }
        }
    }
    editable.snapshot->Base().Each([&](std::string_view key, const std::vector<hdTrans::dict::Record>& records) {
        const auto got = loaded.snapshot->Base().Records(key);
        HD_REQUIRE(got.size() == records.size());
        for (size_t i = 0; i < got.size(); i++) {
            HD_REQUIRE(got[i].codes != nullptr && records[i].codes != nullptr);
            HD_CHECK(got[i].target == records[i].target);
            HD_CHECK_EQ(got[i].codes->partText, records[i].codes->partText);
            HD_CHECK(got[i].codes->sem.Text() == records[i].codes->sem.Text());
            HD_CHECK_EQ(got[i].rule, records[i].rule);
            HD_CHECK_EQ(got[i].id, records[i].id);
        }
    });
}


namespace {

bool WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

std::string CacheResource(const std::string& path) {
    if (!path.ends_with(".latest")) return path;
    const auto resource = hdTrans::dict::internal::SnapshotResourcePath(path);
    return resource.empty() ? path : resource;
}

std::vector<std::byte> ReadBytes(const std::string& path) {
    std::ifstream f(Utf8Path(CacheResource(path)), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    return {bytes.begin(), bytes.end()};
}

bool WriteBytes(const std::string& path, std::span<const std::byte> bytes) {
    return WriteText(Utf8Path(CacheResource(path)), {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

uint64_t Field(std::span<const std::byte> bytes, size_t offset, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; i++) value |= uint64_t{std::to_integer<uint8_t>(bytes[offset + i])} << (8 * i);
    return value;
}

void SetField(std::span<std::byte> bytes, size_t offset, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; i++) bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}

void FixDirectories(std::vector<std::byte>& bytes) {
    HD_REQUIRE(bytes.size() >= 256);
    const auto count = Field(bytes, 40, 4) + Field(bytes, 44, 4);
    HD_REQUIRE(count <= (bytes.size() - 256) / 64);
    SetField(bytes, 196, hdTrans::dict::internal::Crc32c(std::span(bytes).subspan(256, static_cast<size_t>(count * 64))), 4);
    SetField(bytes, 192, 0, 4);
    SetField(bytes, 192, hdTrans::dict::internal::Crc32c(std::span(bytes).first(256)), 4);
}

std::string TargetOf(const hdTrans::dict::Snapshot& snap, std::string_view key) {
    const auto records = snap.Base().Records(key);
    return records.empty() ? std::string() : std::string(records[0].target);
}

}  // namespace

HD_TEST(SnapshotColumnHeaderClassification) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto path = temp.File("header.tsv");
    struct Case {
        std::string text;
        bool column;
    };
    const Case cases[] = {
        {"", false},
        {"\r\n# comment\r\n", false},
        {"key", true},
        {"key\tko\tpos\r\r\n", true},
        {"key\r\r", true},
        {"\rkey\tko\n", false},
        {"\r# comment\nkey\tko\n", false},
        {"# comment\rkey\nkey\tko\n", true},
        {"k\r\nkey\tko\n", false},
        {"key\r\r\tko\n", false},
        {"key\tko\rjunk\n", true},
        {"\xEF\xBB\xBF# comment\r\n\r\nkey\tko\tpos\n", true},
        {"\xEF\xBB\xBF\r\n# comment\nkey\tko\tpos", true},
        {"\n\xEF\xBB\xBFkey\tko\tpos\n", false},
        {"# comment\n\xEF\xBB\xBFkey\tko\tpos\n", false},
        {" key\tko\tpos\n", false},
        {"key \tko\tpos\n", false},
        {"\tkey\tko\n", false},
        {" # comment\nkey\tko\tpos\n", false},
        {"// comment\nkey\tko\tpos\n", false},
        {"key\r\tko\tpos\n", false},
        {"猫\t고양이\tn\nkey\tko\tpos\n", false},
        {"#" + std::string(131072, 'x') + "\r\nkey\tko\tpos\n", true},
        {std::string(4095, '\r') + "\nkey\tko\tpos\n", true},
        {"#" + std::string(4091, 'x') + "\nkey\tko\n", true},
        {"key\t" + std::string(131072, 'x'), true},
        {std::string(131072, 'x') + "\tko\nkey\tko\n", false},
    };
    for (const auto& c : cases) {
        HD_REQUIRE(WriteText(Utf8Path(path), c.text));
        bool column = !c.column;
        HD_REQUIRE(hdTrans::dict::internal::ReadColumnHeader(path, column));
        HD_CHECK_EQ(column, c.column);
    }
    for (const std::string name : {"names.tsv", "userdict.tsv", "readings.tsv", "kana.tsv"}) {
        const auto named = temp.File(name);
        HD_REQUIRE(WriteText(Utf8Path(named), "key\tko\tpos\n"));
        bool column = false;
        HD_REQUIRE(hdTrans::dict::internal::ReadColumnHeader(named, column));
        HD_CHECK(column);
        HD_REQUIRE(WriteText(Utf8Path(named), "猫\t고양이\tn\n"));
        HD_REQUIRE(hdTrans::dict::internal::ReadColumnHeader(named, column));
        HD_CHECK(!column);
    }
    bool column = true;
    HD_CHECK(!hdTrans::dict::internal::ReadColumnHeader(temp.File("absent.tsv"), column));
    HD_CHECK(!hdTrans::dict::internal::ReadColumnHeader(temp.File(""), column));
    HD_CHECK(!hdTrans::dict::internal::ReadColumnHeader(path + std::string("\0suffix", 7), column));
    HD_CHECK(!hdTrans::dict::internal::ReadColumnHeader(path + "\xFF", column));
    const auto unicode = temp.path / Utf8Path("辞書.tsv");
    const auto unicodeText = unicode.u8string();
    HD_REQUIRE(WriteText(unicode, "key\tko\tpos\n"));
    HD_REQUIRE(hdTrans::dict::internal::ReadColumnHeader(
        {reinterpret_cast<const char*>(unicodeText.data()), unicodeText.size()}, column));
    HD_CHECK(column);
}

HD_TEST(SnapshotCachePathNormalization) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    const auto folder = temp.File("dict");
    const auto path = hdTrans::dict::internal::SnapshotPath({folder}, cache);
    HD_REQUIRE(!path.empty());
    HD_CHECK_EQ(path, hdTrans::dict::internal::SnapshotPath({folder + "/./child/../"}, cache));
    std::error_code ec;
    const auto absolute = (std::filesystem::current_path(ec) / "dict").u8string();
    HD_REQUIRE(!ec);
    HD_CHECK_EQ(hdTrans::dict::internal::SnapshotPath({"dict"}, cache),
                hdTrans::dict::internal::SnapshotPath(
                    {{reinterpret_cast<const char*>(absolute.data()), absolute.size()}}, cache));
    const auto other = temp.File("other/dict");
    HD_CHECK(path != hdTrans::dict::internal::SnapshotPath({other}, cache));
    HD_CHECK(hdTrans::dict::internal::SnapshotPath({folder, other}, cache) !=
             hdTrans::dict::internal::SnapshotPath({other, folder}, cache));
}

HD_TEST(SnapshotCacheFolderAliases) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedCurrentPath current(temp.path);
    HD_REQUIRE(current.changed);
    const auto dict = temp.path / "DictAlias";
    std::error_code ec;
    HD_REQUIRE(std::filesystem::create_directory(dict, ec) && !ec);
    HD_REQUIRE(WriteText(dict / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    const auto text = dict.u8string();
    const std::string folder(reinterpret_cast<const char*>(text.data()), text.size());
    const auto cache = temp.File("cache");
    const auto path = hdTrans::dict::internal::SnapshotPath({folder}, cache);
    HD_REQUIRE(!path.empty());
    HD_REQUIRE(hdTrans::dict::internal::CompileCache({folder}, cache));
    const auto relative = std::filesystem::relative(dict, std::filesystem::current_path(ec), ec).u8string();
    HD_REQUIRE(!ec);
    std::vector<std::string> aliases{{reinterpret_cast<const char*>(relative.data()), relative.size()}};
#ifdef _WIN32
    aliases.push_back(temp.File("dictalias"));
    const DWORD size = GetShortPathNameW(dict.c_str(), nullptr, 0);
    HD_REQUIRE(size != 0);
    std::wstring shortName(size, L'\0');
    const DWORD length = GetShortPathNameW(dict.c_str(), shortName.data(), size);
    HD_REQUIRE(length != 0 && length < size);
    shortName.resize(length);
    const auto shortText = std::filesystem::path(shortName).u8string();
    aliases.emplace_back(reinterpret_cast<const char*>(shortText.data()), shortText.size());
    const auto extended = std::filesystem::path(L"\\\\?\\" + dict.native()).u8string();
    aliases.emplace_back(reinterpret_cast<const char*>(extended.data()), extended.size());
#else
    const auto link = temp.path / "link";
    std::filesystem::create_directory_symlink(dict, link, ec);
    HD_REQUIRE(!ec);
    aliases.push_back(temp.File("link"));
#endif
    for (const auto& alias : aliases) {
        HD_CHECK_EQ(hdTrans::dict::internal::SnapshotPath({alias}, cache), path);
        const auto loaded = hdTrans::dict::LoadFolders({alias}, cache, true, false);
        HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
        HD_REQUIRE(loaded.snapshot->Base().Records("猫").size() == 1);
        HD_CHECK(loaded.snapshot->Base().Records("猫")[0].target == "고양이");
    }
}

HD_TEST(SnapshotDefaultCacheLocation) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedEnvironment home("HOME", temp.File("home").c_str());
    ScopedEnvironment local("LOCALAPPDATA", temp.File("local").c_str());
    ScopedEnvironment xdg("XDG_CACHE_HOME", nullptr);
    ScopedEnvironment overrideDir("HDTRANS_CACHE_DIR", nullptr);
#ifdef _WIN32
    const auto cache = temp.File("local/hdtrans/cache");
#elif defined(__APPLE__)
    const auto cache = temp.File("home/Library/Caches/hdtrans");
#else
    const auto cache = temp.File("home/.cache/hdtrans");
#endif
    std::error_code ec;
    const auto dict = temp.File("dict");
    std::filesystem::create_directory(Utf8Path(dict), ec);
    HD_REQUIRE(!ec);
    HD_REQUIRE(WriteText(Utf8Path(dict) / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    const std::vector<std::string> folders{dict};
    const auto path = hdTrans::dict::internal::SnapshotPath(folders);
    HD_REQUIRE(!path.empty());
    HD_CHECK(Utf8Path(path) == Utf8Path(hdTrans::dict::internal::SnapshotPath(folders, cache)));
    const auto built = hdTrans::dict::LoadFolders(folders);
    HD_REQUIRE(built.snapshot != nullptr);
    HD_REQUIRE(std::filesystem::exists(Utf8Path(path)));
    const auto loaded = hdTrans::dict::LoadFolders(folders);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
    for (const auto& entry : std::filesystem::directory_iterator(Utf8Path(dict))) {
        HD_CHECK(entry.path().filename() == "base.tsv");
    }

    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto time = std::filesystem::last_write_time(Utf8Path(CacheResource(path)));
    overrideDir.Set("");
    HD_CHECK(hdTrans::dict::internal::SnapshotPath(folders).empty());
    const auto disabled = hdTrans::dict::LoadFolders(folders);
    HD_REQUIRE(disabled.snapshot != nullptr);
    HD_CHECK(!disabled.snapshot->Mapped());
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
    HD_CHECK(!std::filesystem::exists(Utf8Path(dict) / ".hdsnap"));

    overrideDir.Set(nullptr);
#if !defined(_WIN32) && !defined(__APPLE__)
    xdg.Set(temp.File("xdg").c_str());
    HD_CHECK(Utf8Path(hdTrans::dict::internal::SnapshotPath(folders)) ==
             Utf8Path(hdTrans::dict::internal::SnapshotPath(folders, temp.File("xdg/hdtrans"))));
    xdg.Set("");
    HD_CHECK(Utf8Path(path) == Utf8Path(hdTrans::dict::internal::SnapshotPath(folders)));
#endif
    home.Set(nullptr);
    local.Set(nullptr);
    HD_CHECK(hdTrans::dict::internal::SnapshotPath(folders).empty());
}

HD_TEST(SnapshotCacheEnvironmentOverride) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    ScopedEnvironment overrideDir("HDTRANS_CACHE_DIR", cache.c_str());
    const std::vector<std::string> folders{temp.File("")};
    HD_REQUIRE(WriteText(temp.path / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    const auto path = hdTrans::dict::internal::SnapshotPath(folders);
    HD_REQUIRE(!path.empty());
    HD_CHECK(Utf8Path(path) == Utf8Path(hdTrans::dict::internal::SnapshotPath(folders, cache)));
    const auto built = hdTrans::dict::LoadFolders(folders);
    HD_REQUIRE(built.snapshot != nullptr);
    HD_REQUIRE(std::filesystem::exists(Utf8Path(path)));
    const auto loaded = hdTrans::dict::LoadFolders(folders);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
    HD_CHECK(!std::filesystem::exists(temp.path / ".hdsnap"));

    const auto explicitDir = temp.File(".hdsnap");
    const auto explicitPath = hdTrans::dict::internal::SnapshotPath(folders, explicitDir);
    HD_REQUIRE(!explicitPath.empty());
    HD_CHECK(Utf8Path(explicitPath).parent_path() == Utf8Path(explicitDir));
    const auto explicitBuild = hdTrans::dict::LoadFolders(folders, explicitDir);
    HD_REQUIRE(explicitBuild.snapshot != nullptr);
    HD_REQUIRE(std::filesystem::exists(Utf8Path(explicitPath)));
    const auto explicitLoad = hdTrans::dict::LoadFolders(folders, explicitDir);
    HD_REQUIRE(explicitLoad.snapshot != nullptr);
    HD_CHECK(explicitLoad.snapshot->Mapped());
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto time = std::filesystem::last_write_time(Utf8Path(CacheResource(path)));
    for (const char* value : {"", "off", "OFF", "Off", "oFF", "OfF", "oFf", "OFf", "ofF"}) {
        overrideDir.Set(value);
        const auto disabled = hdTrans::dict::LoadFolders(folders);
        HD_REQUIRE(disabled.snapshot != nullptr);
        HD_CHECK(!disabled.snapshot->Mapped());
        HD_CHECK(hdTrans::dict::internal::SnapshotPath(folders, explicitDir).empty());
        const auto explicitDisabled = hdTrans::dict::LoadFolders(folders, explicitDir);
        HD_REQUIRE(explicitDisabled.snapshot != nullptr);
        HD_CHECK(!explicitDisabled.snapshot->Mapped());
        HD_CHECK(ReadBytes(path) == bytes);
        HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
    }
    const auto enabled = temp.File("off-cache");
    overrideDir.Set(enabled.c_str());
    HD_CHECK(!hdTrans::dict::internal::SnapshotPath(folders).empty());
}

HD_TEST(SnapshotRelativeCacheOverrideDisablesCaching) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedCurrentPath current(temp.path);
    HD_REQUIRE(current.changed);
    const auto cache = temp.path / "relative-cache";
    std::error_code ec;
    const auto relative = cache.lexically_relative(std::filesystem::current_path(ec));
    HD_REQUIRE(!ec && !relative.empty() && relative.is_relative());
    const auto text = relative.u8string();
    const std::string value(reinterpret_cast<const char*>(text.data()), text.size());
    ScopedEnvironment overrideDir("HDTRANS_CACHE_DIR", value.c_str());
    HD_CHECK(hdTrans::dict::internal::RelativeCacheOverride());
    const std::vector<std::string> folders{temp.File("")};
    HD_REQUIRE(WriteText(temp.path / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    const auto explicitCache = temp.File("explicit-cache");
    HD_CHECK(hdTrans::dict::internal::SnapshotPath(folders).empty());
    HD_CHECK(hdTrans::dict::internal::SnapshotPath(folders, explicitCache).empty());
    HD_CHECK(!hdTrans::dict::internal::CompileCache(folders));
    HD_CHECK(!hdTrans::dict::internal::CompileCache(folders, explicitCache));
    const auto loaded = hdTrans::dict::LoadFolders(folders, explicitCache);
    HD_REQUIRE(loaded.snapshot != nullptr);
    HD_CHECK(!loaded.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*loaded.snapshot, "猫"), "고양이");
    HD_CHECK(!std::filesystem::exists(cache));
    HD_CHECK(!std::filesystem::exists(Utf8Path(explicitCache)));
    for (const char* disabled : {"", "off", "OfF"}) {
        overrideDir.Set(disabled);
        HD_CHECK(!hdTrans::dict::internal::RelativeCacheOverride());
    }
    const auto absolute = temp.File("absolute-cache");
    overrideDir.Set(absolute.c_str());
    HD_CHECK(!hdTrans::dict::internal::RelativeCacheOverride());
    HD_CHECK(!hdTrans::dict::internal::SnapshotPath(folders).empty());
}

HD_TEST(SnapshotCacheWritePolicy) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    ScopedEnvironment overrideDir("HDTRANS_CACHE_DIR", cache.c_str());
    const auto base = temp.path / "base.tsv";
    HD_REQUIRE(WriteText(base, "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    const std::vector<std::string> folders{temp.File("")};
    const auto path = hdTrans::dict::internal::SnapshotPath(folders);
    HD_REQUIRE(!path.empty());
    const auto readonly = hdTrans::dict::LoadFolders(folders, cache, true, false);
    HD_REQUIRE(readonly.snapshot != nullptr);
    HD_CHECK(!readonly.snapshot->Mapped());
    HD_CHECK(!std::filesystem::exists(Utf8Path(path)));
    hdTrans::Engine reader(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(reader.Open(folders));
    HD_REQUIRE(reader.Dictionaries() != nullptr);
    HD_CHECK(!reader.Dictionaries()->Mapped());
    HD_REQUIRE(reader.Reload());
    HD_CHECK(!std::filesystem::exists(Utf8Path(path)));
    hdTrans::Engine writer;
    HD_REQUIRE(writer.Open(folders));
    HD_REQUIRE(std::filesystem::exists(Utf8Path(path)));
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto cacheTime = std::filesystem::last_write_time(Utf8Path(CacheResource(path)));
    const auto sourceTime = std::filesystem::last_write_time(base);
    std::filesystem::last_write_time(base, sourceTime + std::chrono::seconds(2));
    HD_REQUIRE(reader.Reload());
    HD_CHECK(reader.Dictionaries()->Mapped());
    HD_CHECK(!std::filesystem::exists(Utf8Path(CacheResource(path) + ".meta")));
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == cacheTime);
    HD_REQUIRE(WriteText(base, "key\tko\tpos\tid\n猫\t야옹이\tN110\t1\n"));
    HD_REQUIRE(reader.Reload());
    HD_CHECK(!reader.Dictionaries()->Mapped());
    HD_CHECK_EQ(TargetOf(*reader.Dictionaries(), "猫"), "야옹이");
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == cacheTime);
}

HD_TEST(SnapshotSourceInvalidation) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    const auto base = temp.path / "base.tsv";
    const std::string header = "key\tko\tpos\tid\n";
    HD_REQUIRE(WriteText(base, header + "猫\t고양이\tN110\t1\n"));
    const std::vector<std::string> folders{temp.File("")};
    const auto built = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(built.snapshot != nullptr);
    const auto cached = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(cached.snapshot != nullptr && cached.snapshot->Mapped());
    const auto time = std::filesystem::last_write_time(base);
    HD_REQUIRE(WriteText(base, header + "猫\t야옹이\tN110\t1\n"));
    std::filesystem::last_write_time(base, time);
    // Strict verification detects changed bytes even when all file metadata match.
    const auto changed = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(changed.snapshot != nullptr);
    HD_CHECK(!changed.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*changed.snapshot, "猫"), "야옹이");
    const auto current = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(current.snapshot != nullptr && current.snapshot->Mapped());
    std::filesystem::last_write_time(base, time + std::chrono::seconds(2));
    const auto touched = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(touched.snapshot != nullptr);
    HD_CHECK(touched.snapshot->Mapped());
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    HD_CHECK(std::filesystem::exists(Utf8Path(CacheResource(path) + ".meta")));
    const auto again = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(again.snapshot != nullptr);
    HD_CHECK(again.snapshot->Mapped());
    HD_REQUIRE(WriteText(temp.path / "more.tsv", header + "犬\t개\tN110\t2\n"));
    const auto added = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(added.snapshot != nullptr);
    HD_CHECK(!added.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*added.snapshot, "犬"), "개");
    std::filesystem::remove(temp.path / "more.tsv");
    const auto removed = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(removed.snapshot != nullptr);
    HD_CHECK(!removed.snapshot->Mapped());
    HD_CHECK_EQ(removed.snapshot->Base().Len(), size_t{1});
}

HD_TEST(SnapshotCorruptFileFallback) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const std::vector<std::string> folders{FixtureFolder()};
    const auto cache = temp.File("cache");
    const auto built = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(built.snapshot != nullptr);
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    const auto good = ReadBytes(path);
    HD_REQUIRE(good.size() > 1024);
    const size_t lengths[] = {0, 7, 255, good.size() / 2, good.size() - 1};
    for (const size_t length : lengths) {
        HD_REQUIRE(WriteBytes(path, std::span(good).first(length)));
        const auto loaded = hdTrans::dict::LoadFolders(folders, cache, true);
        HD_REQUIRE(loaded.snapshot != nullptr);
        HD_CHECK(!loaded.snapshot->Mapped());
        HD_CHECK_EQ(loaded.snapshot->Base().Len(), built.snapshot->Base().Len());
    }
    for (int variant = 0; variant < 8; variant++) {
        auto bytes = good;
        switch (variant) {
        case 0: bytes[0] ^= std::byte{1}; break;
        case 1: SetField(bytes, 8, 99, 4); break;
        case 2: SetField(bytes, 12, 99, 4); break;
        case 3: SetField(bytes, 256 + 8, std::numeric_limits<uint64_t>::max() - 7, 8); break;
        case 4: SetField(bytes, 256 + 36, 3, 4); break;
        case 5: SetField(bytes, 256 + 64 + 8, Field(bytes, 256 + 8, 8), 8); break;
        case 6: SetField(bytes, 204, sizeof(void*) == 8 ? 4 : 8, 4); break;
        case 7: bytes[208] ^= std::byte{1}; break;
        }
        FixDirectories(bytes);
        HD_REQUIRE(WriteBytes(path, bytes));
        const auto loaded = hdTrans::dict::LoadFolders(folders, cache, true);
        HD_REQUIRE(loaded.snapshot != nullptr);
        HD_CHECK(!loaded.snapshot->Mapped());
        HD_CHECK_EQ(loaded.snapshot->Base().Len(), built.snapshot->Base().Len());
    }
}

HD_TEST(SnapshotLazyChecksumFallback) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const std::vector<std::string> folders{FixtureFolder()};
    const auto cache = temp.File("cache");
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    const auto built = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(built.snapshot != nullptr);
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    size_t labels = 0;
    const auto sections = Field(bytes, 40, 4);
    HD_REQUIRE(sections <= (bytes.size() - 256) / 64);
    for (size_t section = 0; section < sections; section++) {
        const size_t at = 256 + 64 * section;
        if (Field(bytes, at, 4) == 12 && Field(bytes, at + 4, 4) == 2) labels = static_cast<size_t>(Field(bytes, at + 8, 8));
    }
    HD_REQUIRE(labels != 0 && labels < bytes.size());
    bytes[labels] ^= std::byte{1};
    HD_REQUIRE(WriteBytes(path, bytes));
    const auto loaded = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
    HD_CHECK(loaded.snapshot->Healthy());
    const hdTrans::dict::LookupContext context;
    const auto want = editable.snapshot->GetWordInfo("アリスさん", hdTrans::dict::LookupMode::General, context);
    const auto got = loaded.snapshot->GetWordInfo("アリスさん", hdTrans::dict::LookupMode::General, context);
    HD_CHECK(!loaded.snapshot->Healthy());
    HD_REQUIRE(got.list.size() == want.list.size());
    for (size_t i = 0; i < got.list.size(); i++) HD_CHECK(got.list[i].target == want.list[i].target);
    const auto retry = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(retry.snapshot != nullptr);
    HD_CHECK(!retry.snapshot->Mapped());
    HD_CHECK(retry.snapshot->Healthy());
}

HD_TEST(SnapshotCacheErrorsAreOptional) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto blocked = temp.File("not-a-directory");
    HD_REQUIRE(WriteText(Utf8Path(blocked), "data"));
    const auto loaded = hdTrans::dict::LoadFolders({FixtureFolder()}, blocked, true);
    HD_REQUIRE(loaded.snapshot != nullptr);
    HD_CHECK(!loaded.snapshot->Mapped());
    HD_CHECK(loaded.snapshot->Healthy());
    ScopedEnvironment overrideDir("HDTRANS_CACHE_DIR", blocked.c_str());
    const auto fallback = hdTrans::dict::LoadFolders({FixtureFolder()});
    HD_REQUIRE(fallback.snapshot != nullptr);
    HD_CHECK(!fallback.snapshot->Mapped());
    HD_CHECK(fallback.snapshot->Healthy());
    HD_CHECK_EQ(fallback.errors.size(), loaded.errors.size());
    HD_CHECK_EQ(fallback.snapshot->Base().Len(), loaded.snapshot->Base().Len());
#ifdef _WIN32
    const wchar_t invalidValue[] = {static_cast<wchar_t>(0xd800), L'\0'};
    HD_REQUIRE(SetEnvironmentVariableW(L"HDTRANS_CACHE_DIR", invalidValue) != 0);
    HD_CHECK(hdTrans::dict::internal::SnapshotPath({FixtureFolder()}).empty());
    const auto invalidEnvironment = hdTrans::dict::LoadFolders({FixtureFolder()});
    HD_REQUIRE(invalidEnvironment.snapshot != nullptr);
    HD_CHECK(!invalidEnvironment.snapshot->Mapped());
#endif
    const std::string invalid(1, static_cast<char>(0xff));
    const auto bad = hdTrans::dict::LoadFolders({invalid});
    HD_CHECK(bad.snapshot == nullptr);
    HD_REQUIRE(bad.errors.size() == 1);
    HD_CHECK(bad.errors[0].kind == hdTrans::dict::LoadErrorKind::Folder);
    HD_CHECK(!hdTrans::dict::CompileSnapshot({FixtureFolder()}, invalid));
}

namespace {

void CheckLoadErrors(const std::vector<hdTrans::dict::LoadError>& actual,
                     const std::vector<hdTrans::dict::LoadError>& expected) {
    HD_REQUIRE(actual.size() == expected.size());
    for (size_t i = 0; i < actual.size(); i++) {
        const auto fields = [](const hdTrans::dict::LoadError& e) {
            return std::tie(e.file, e.kind, e.line, e.tsv, e.message);
        };
        HD_CHECK(fields(actual[i]) == fields(expected[i]));
    }
}

void CheckPlainCompilation(const hdTrans::dict::Snapshot& actual, const hdTrans::dict::Snapshot& expected, bool portable = false) {
    HD_CHECK_EQ(actual.HasDictionary(), expected.HasDictionary());
    HD_REQUIRE(actual.Warnings().size() == expected.Warnings().size());
    for (size_t i = 0; i < actual.Warnings().size(); i++) {
        if (portable) {
            HD_CHECK(Utf8Path(actual.Warnings()[i]).is_relative());
            HD_CHECK(Utf8Path(actual.Warnings()[i]).filename() == Utf8Path(expected.Warnings()[i]).filename());
        } else HD_CHECK_EQ(actual.Warnings()[i], expected.Warnings()[i]);
    }
    HD_REQUIRE(actual.PlainFiles().size() == expected.PlainFiles().size());
    for (size_t i = 0; i < actual.PlainFiles().size(); i++) {
        const auto& a = actual.PlainFiles()[i];
        const auto& b = expected.PlainFiles()[i];
        HD_CHECK(std::tie(a.folder, a.text, a.layer, a.user, a.head) == std::tie(b.folder, b.text, b.layer, b.user, b.head));
        if (portable) {
            HD_CHECK(Utf8Path(a.name).is_relative());
            HD_CHECK(Utf8Path(a.name).filename() == Utf8Path(b.name).filename());
        } else HD_CHECK_EQ(a.name, b.name);
    }
    for (bool extra : {false, true}) {
        for (const auto& pair : {std::pair{&actual.Plain(extra), &expected.Plain(extra)},
                                std::pair{&actual.TextPlain(extra), &expected.TextPlain(extra)}}) {
            HD_CHECK_EQ(pair.first->Len(), pair.second->Len());
            const auto& a = pair.first->Entries();
            const auto& b = pair.second->Entries();
            HD_REQUIRE(a.size() == b.size());
            for (size_t i = 0; i < a.size(); i++) {
                const auto fields = [](const hdTrans::dict::UserEntry& e) {
                    return std::tie(e.key, e.target, e.negative, e.prio, e.file, e.line, e.order, e.name, e.nameLayer,
                                    e.kana, e.force, e.hidden, e.finalClass, e.honorific, e.end, e.registerLevel,
                                    e.polite, e.stative, e.euImperative);
                };
                HD_CHECK(fields(a[i]) == fields(b[i]));
                HD_REQUIRE(a[i].codes != nullptr && b[i].codes != nullptr);
                HD_CHECK_EQ(a[i].codes->partText, b[i].codes->partText);
                HD_CHECK(a[i].codes->sem.Text() == b[i].codes->sem.Text());
                HD_CHECK(a[i].codes->gram.Text() == b[i].codes->gram.Text());
                HD_CHECK(a[i].codes->dict.Text() == b[i].codes->dict.Text());
            }
        }
    }
    HD_REQUIRE(actual.Senses().size() == expected.Senses().size());
    for (const auto& [key, senses] : expected.Senses()) {
        const auto found = actual.Senses().find(key);
        HD_REQUIRE(found != actual.Senses().end());
        HD_REQUIRE(found->second.size() == senses.size());
        for (size_t i = 0; i < senses.size(); i++) {
            const auto fields = [](const hdTrans::userdict::Sense& s) {
                return std::tie(s.key, s.classDigit, s.target, s.triggers, s.side, s.adjective, s.row);
            };
            HD_CHECK(fields(found->second[i]) == fields(senses[i]));
        }
    }
    auto a = actual.GeneratedFamilies().GeneratedFamilies();
    auto b = expected.GeneratedFamilies().GeneratedFamilies();
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    HD_CHECK(a == b);
}

}  // namespace

HD_TEST(SnapshotWarmPlainEditsKeepColumnCache) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const std::vector<std::string> folders{temp.File("")};
    const auto cache = temp.File("cache");
    HD_REQUIRE(WriteText(temp.path / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    HD_REQUIRE(WriteText(temp.path / "plain.tsv", "猫\t냥이\tn\n白い\t하얗다\tadj\n"));
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    const auto baseline = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(baseline.snapshot != nullptr && baseline.snapshot->Mapped());
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto time = std::filesystem::last_write_time(Utf8Path(CacheResource(path)));
    const auto oldCat = baseline.snapshot->Plain().Search("猫", hdTrans::dict::LookupMode::General);
    HD_REQUIRE(oldCat.size() == 1);
    HD_CHECK_EQ(oldCat[0]->target, "냥이");

    HD_REQUIRE(WriteText(temp.path / "plain.tsv", "猫\t야옹이\tn\n称える\t일컫다\tv1:ㄷ\n"));
    for (bool verify : {false, true}) {
        const auto changed = hdTrans::dict::LoadFolders(folders, cache, verify);
        const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
        HD_REQUIRE(changed.snapshot != nullptr && changed.snapshot->Mapped());
        HD_REQUIRE(editable.snapshot != nullptr);
        CheckPlainCompilation(*changed.snapshot, *editable.snapshot);
        CheckLoadErrors(changed.errors, editable.errors);
        HD_CHECK(ReadBytes(path) == bytes);
        HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
        const auto cat = changed.snapshot->Plain().Search("猫", hdTrans::dict::LookupMode::General);
        HD_REQUIRE(cat.size() == 1);
        HD_CHECK_EQ(cat[0]->target, "야옹이");
    }
    HD_CHECK_EQ(oldCat[0]->target, "냥이");
    HD_REQUIRE(WriteText(temp.path / "z-extra.tsv", "猫\t고양님\tn\n黄色い\t누르다\tadj:러\n"));
    std::error_code ec;
    HD_REQUIRE(std::filesystem::remove(temp.path / "plain.tsv", ec) && !ec);
    const auto replaced = hdTrans::dict::LoadFolders(folders, cache, true);
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(replaced.snapshot != nullptr && replaced.snapshot->Mapped());
    HD_REQUIRE(editable.snapshot != nullptr);
    CheckPlainCompilation(*replaced.snapshot, *editable.snapshot);
    CheckLoadErrors(replaced.errors, editable.errors);
    HD_REQUIRE(replaced.snapshot->PlainFiles().size() == 1);
    HD_CHECK(replaced.snapshot->PlainFiles()[0].name.ends_with("z-extra.tsv"));
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
}

HD_TEST(SnapshotPlainReadingsByLayer) {
    CacheTestEnvironment environment;
    const std::vector<std::string> lines{"風音「こんにちは」", "風音「ありがとう」"};
    for (const char* name : {"readings.tsv", "first/readings.tsv", "first\\zz-sounds.tsv"}) {
        const auto loaded = hdTrans::dict::Load({
            {0, "base.tsv", "key\tko\tpos\tid\n愛\t사랑\tN110\t1\n"},
            {0, name, "#! layer=readings\n風音\t카자네\n"},
        });
        HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
        const auto doc = hdTrans::pipeline::BuildDocument(*loaded.snapshot, lines);
        HD_REQUIRE(doc != nullptr && doc->registry != nullptr);
        HD_CHECK_EQ(doc->registry->Render("風音"), "카자네");
    }
}

HD_TEST(SnapshotWarmPlainFileOrderAndReadings) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto first = temp.path / "first";
    const auto second = temp.path / "second";
    std::error_code ec;
    HD_REQUIRE(std::filesystem::create_directory(first, ec) && !ec);
    HD_REQUIRE(std::filesystem::create_directory(second, ec) && !ec);
    const std::vector<std::string> folders{temp.File("first"), temp.File("second")};
    const auto cache = temp.File("cache");
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\tid\n";
    HD_REQUIRE(WriteText(first / "base.tsv", header + "愛\t사랑\tN110\t\t\tGN\t1\n"
                                                    "帰\t돌아가&AA@\tM428\t\t\tGN\t2\n"
                                                    "謎\t수수께끼\tunknown-tag\t\t\tGN\t3\n"));
    HD_REQUIRE(WriteText(first / "names.tsv", "#! layer=names\n" + header + "難、名\t이름\tN130\tQPN\t\tGN\t1\n"));
    HD_REQUIRE(WriteText(first / "a.tsv", "猫\t냥이\tn\n帰る\t돌아가다\tv\n白い\t하얗다\tadj\n"));
    HD_REQUIRE(WriteText(first / "kana.tsv", "#! layer=kana\nあい\t愛\t\n"));
    HD_REQUIRE(WriteText(first / "readings.tsv", "#! layer=readings\n風音\t카자네\n"));
    HD_REQUIRE(WriteText(first / "userdict.tsv", "猫\t고양이\tn\n"));
    HD_REQUIRE(WriteText(second / "a.tsv", "猫\t야옹이\tn\n称える\t일컫다\tv1:ㄷ\n"));
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    const auto baseline = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(baseline.snapshot != nullptr && baseline.snapshot->Mapped());
    HD_REQUIRE(baseline.errors.size() == 1);
    HD_REQUIRE(baseline.snapshot->Warnings().size() == 1);
    const std::vector<std::string> lines{"風音「こんにちは」", "風音「ありがとう」"};
    const auto oldDoc = hdTrans::pipeline::BuildDocument(*baseline.snapshot, lines);
    HD_CHECK_EQ(oldDoc->registry->Render("風音"), "카자네");
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto time = std::filesystem::last_write_time(Utf8Path(CacheResource(path)));

    HD_REQUIRE(WriteText(first / "a.tsv", "猫\t냥이\tn\n帰る\t돌아가다\tv\n黄色い\t누르다\tadj:러\n"));
    HD_REQUIRE(WriteText(first / "kana.tsv", "#! layer=kana\nいとし\t愛\t\n"));
    HD_REQUIRE(WriteText(second / "names-plain.tsv", "犬\t멍멍이\tn\n"));
    HD_REQUIRE(WriteText(second / "readings.tsv", "#! layer=readings\n風音\t카논\n"));
    const auto changed = hdTrans::dict::LoadFolders(folders, cache, true);
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(changed.snapshot != nullptr && changed.snapshot->Mapped());
    HD_REQUIRE(editable.snapshot != nullptr);
    CheckPlainCompilation(*changed.snapshot, *editable.snapshot);
    CheckLoadErrors(changed.errors, editable.errors);
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
    const auto& files = changed.snapshot->PlainFiles();
    const std::vector<std::string> names{"first/a.tsv", "first/kana.tsv", "first/readings.tsv", "first/userdict.tsv",
                                         "second/a.tsv", "second/names-plain.tsv", "second/readings.tsv"};
    HD_REQUIRE(files.size() == names.size());
    for (size_t i = 0; i < files.size(); i++) {
        HD_CHECK(Utf8Path(files[i].name) == temp.path / Utf8Path(names[i]));
        HD_CHECK_EQ(files[i].folder, i < 4 ? 0 : 1);
    }
    const auto cat = changed.snapshot->Plain().Search("猫", hdTrans::dict::LookupMode::General);
    HD_REQUIRE(cat.size() == 1);
    HD_CHECK_EQ(cat[0]->target, "야옹이");
    HD_CHECK_EQ(cat[0]->file, 2);
    const auto dog = changed.snapshot->Plain().Search("犬", hdTrans::dict::LookupMode::General);
    HD_REQUIRE(dog.size() == 1);
    HD_CHECK_EQ(dog[0]->file, 3);
    const auto kana = changed.snapshot->TextPlain().Search("いとし", hdTrans::dict::LookupMode::General);
    HD_REQUIRE(kana.size() == 1);
    HD_CHECK(kana[0]->kana);
    HD_CHECK_EQ(kana[0]->target, "사랑");
    HD_CHECK_EQ(kana[0]->file, -2);
    HD_CHECK(changed.snapshot->TextPlain().Search("あい", hdTrans::dict::LookupMode::General).empty());
    HD_CHECK(changed.snapshot->Plain().Search("いとし", hdTrans::dict::LookupMode::General).empty());
    const auto currentDoc = hdTrans::pipeline::BuildDocument(*changed.snapshot, lines);
    HD_CHECK(currentDoc->data != oldDoc->data);
    HD_CHECK_EQ(currentDoc->registry->Render("風音"), "카논");
    HD_CHECK_EQ(oldDoc->registry->Render("風音"), "카자네");
}

HD_TEST(SnapshotWarmPlainColumnMembershipChanges) {
    CacheTestEnvironment environment;
    for (const std::string name : {"names.tsv", "userdict.tsv", "readings.tsv", "kana.tsv"}) {
        TempFolder temp;
        const auto cache = temp.File("cache");
        const std::vector<std::string> folders{temp.File("")};
        HD_REQUIRE(WriteText(temp.path / "base.tsv", "key\tko\tpos\tid\n愛\t사랑\tN110\t1\n"));
        const std::string plain = name == "kana.tsv" ? "#! layer=kana\nあい\t愛\t\n" :
                                  name == "readings.tsv" ? "#! layer=readings\n風音\t카자네\n" : "犬\t개\tn\n";
        const std::string layer = name == "names.tsv" ? "#! layer=names\n" : name == "userdict.tsv" ? "#! layer=user\n" : "";
        const auto source = temp.path / Utf8Path(name);
        HD_REQUIRE(WriteText(source, plain));
        HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
        const auto baseline = hdTrans::dict::LoadFolders(folders, cache, true);
        HD_REQUIRE(baseline.snapshot != nullptr && baseline.snapshot->Mapped());
        HD_REQUIRE(baseline.snapshot->PlainFiles().size() == 1);
        const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
        HD_REQUIRE(!path.empty());
        const auto bytes = ReadBytes(path);
        HD_REQUIRE(bytes.size() >= 256);
        HD_REQUIRE(WriteText(source, layer + "key\tko\tpos\tid\n犬\t개\tN110\t1\n"));
        const auto column = hdTrans::dict::LoadFolders(folders, cache, true);
        const auto columnTsv = hdTrans::dict::LoadFoldersTsv(folders);
        HD_REQUIRE(column.snapshot != nullptr && !column.snapshot->Mapped());
        HD_REQUIRE(columnTsv.snapshot != nullptr);
        HD_CHECK(column.snapshot->PlainFiles().empty());
        CheckPlainCompilation(*column.snapshot, *columnTsv.snapshot);
        CheckLoadErrors(column.errors, columnTsv.errors);
        if (name == "names.tsv") {
            HD_CHECK_EQ(column.snapshot->NameRecords("犬").size(), size_t{1});
        } else if (name == "userdict.tsv") {
            HD_REQUIRE(column.snapshot->User() != nullptr);
            HD_CHECK_EQ(column.snapshot->User()->Records("犬").size(), size_t{1});
        } else {
            HD_CHECK_EQ(column.snapshot->Base().Records("犬").size(), size_t{1});
        }
        HD_CHECK(ReadBytes(path) != bytes);
        const auto mappedColumn = hdTrans::dict::LoadFolders(folders, cache, true);
        HD_REQUIRE(mappedColumn.snapshot != nullptr && mappedColumn.snapshot->Mapped());
        HD_REQUIRE(WriteText(source, plain));
        const auto restored = hdTrans::dict::LoadFolders(folders, cache, true);
        const auto restoredTsv = hdTrans::dict::LoadFoldersTsv(folders);
        HD_REQUIRE(restored.snapshot != nullptr && !restored.snapshot->Mapped());
        HD_REQUIRE(restoredTsv.snapshot != nullptr);
        HD_REQUIRE(restored.snapshot->PlainFiles().size() == 1);
        CheckPlainCompilation(*restored.snapshot, *restoredTsv.snapshot);
        CheckLoadErrors(restored.errors, restoredTsv.errors);
        const auto mappedPlain = hdTrans::dict::LoadFolders(folders, cache, true);
        HD_REQUIRE(mappedPlain.snapshot != nullptr && mappedPlain.snapshot->Mapped());
    }
}

HD_TEST(SnapshotWarmPlainErrorsKeepOrder) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto first = temp.path / "first";
    const auto second = temp.path / "second";
    std::error_code ec;
    HD_REQUIRE(std::filesystem::create_directory(first, ec) && !ec);
    HD_REQUIRE(std::filesystem::create_directory(second, ec) && !ec);
    const auto cache = temp.File("cache");
    const std::vector<std::string> folders{temp.File("first"), temp.File("second")};
    HD_REQUIRE(WriteText(first / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n謎\t수수께끼\tunknown-tag\t2\n"));
    HD_REQUIRE(WriteText(first / "a.tsv", "猫\t냥이\tn\n"));
    HD_REQUIRE(WriteText(first / "readings.tsv", "#! layer=readings\nmalformed reading\n"));
    HD_REQUIRE(WriteText(first / "userdict.tsv", "犬\t개\tn\n"));
    HD_REQUIRE(WriteText(second / "a.tsv", "魚\t물고기\tn\n"));
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    const auto baseline = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(baseline.snapshot != nullptr && baseline.snapshot->Mapped());
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto time = std::filesystem::last_write_time(Utf8Path(CacheResource(path)));
    HD_REQUIRE(WriteText(first / "a.tsv", "猫\t냥이\n帰る\t돌아가다\tv\n鳥\n"));
    HD_REQUIRE(WriteText(first / "userdict.tsv", "犬\n"));
    HD_REQUIRE(WriteText(second / "a.tsv", "魚\n"));
    const auto bad = hdTrans::dict::LoadFolders(folders, cache, true);
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_CHECK(bad.snapshot == nullptr);
    HD_CHECK(editable.snapshot == nullptr);
    CheckLoadErrors(bad.errors, editable.errors);
    HD_REQUIRE(bad.errors.size() == 5);
    HD_CHECK(bad.errors[0].kind == hdTrans::dict::LoadErrorKind::UnknownPos);
    HD_CHECK_EQ(bad.errors[0].line, 3);
    const std::vector<std::string> names{"first/a.tsv", "first/a.tsv", "first/userdict.tsv", "second/a.tsv"};
    const int lines[] = {2, 3, 1, 1};
    for (size_t i = 0; i < names.size(); i++) {
        HD_CHECK(Utf8Path(bad.errors[i + 1].file) == temp.path / Utf8Path(names[i]));
        HD_CHECK(bad.errors[i + 1].kind == hdTrans::dict::LoadErrorKind::Plain);
        HD_CHECK_EQ(bad.errors[i + 1].line, lines[i]);
    }
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
    HD_REQUIRE(WriteText(first / "a.tsv", "猫\t냥이\tn\n"));
    HD_REQUIRE(WriteText(first / "userdict.tsv", "犬\t개\tn\n"));
    HD_REQUIRE(WriteText(second / "a.tsv", "魚\t물고기\tn\n"));
    const auto fixed = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(fixed.snapshot != nullptr && fixed.snapshot->Mapped());
    CheckPlainCompilation(*fixed.snapshot, *baseline.snapshot);
    CheckLoadErrors(fixed.errors, baseline.errors);
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
}

namespace {

void CheckDictEncoding(const hdTrans::dict::Dict& actual, const hdTrans::dict::Dict& expected) {
    HD_CHECK_EQ(actual.Len(), expected.Len());
    const auto a = actual.View();
    const auto b = expected.View();
    HD_CHECK(a.blob == b.blob);
    HD_REQUIRE(a.starts.size() == b.starts.size());
    HD_CHECK(std::equal(a.starts.begin(), a.starts.end(), b.starts.begin()));
    HD_REQUIRE(a.codes.size() == b.codes.size());
    for (size_t i = 0; i < a.codes.size(); i++) {
        HD_REQUIRE(a.codes[i] != nullptr && b.codes[i] != nullptr);
        HD_CHECK_EQ(a.codes[i]->partText, b.codes[i]->partText);
        HD_CHECK(a.codes[i]->sem.Text() == b.codes[i]->sem.Text());
        HD_CHECK(a.codes[i]->gram.Text() == b.codes[i]->gram.Text());
        HD_CHECK(a.codes[i]->dict.Text() == b.codes[i]->dict.Text());
    }
    expected.Each([&](std::string_view key, const std::vector<hdTrans::dict::Record>& records) {
        HD_CHECK_EQ(actual.Find(key), expected.Find(key));
        const auto got = actual.Records(key);
        HD_REQUIRE(got.size() == records.size());
        for (size_t i = 0; i < got.size(); i++) {
            HD_CHECK(got[i].target == records[i].target);
            HD_CHECK_EQ(got[i].rule, records[i].rule);
            HD_CHECK_EQ(got[i].id, records[i].id);
            const auto actualCode = std::find(a.codes.begin(), a.codes.end(), got[i].codes);
            const auto expectedCode = std::find(b.codes.begin(), b.codes.end(), records[i].codes);
            HD_REQUIRE(actualCode != a.codes.end() && expectedCode != b.codes.end());
            HD_CHECK_EQ(actualCode - a.codes.begin(), expectedCode - b.codes.begin());
        }
    });
}

}  // namespace

HD_TEST(SnapshotWarmColumnUserCodesAndPlainViews) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    const std::vector<std::string> folders{temp.File("")};
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\tprio\tid\n";
    HD_REQUIRE(WriteText(temp.path / "base.tsv", header + "犬\t견\tN110\tQNN\tWa\tGN\t1\t1\n"
                                                        "犬\t강아지\tN110\tVAn\tWb\tBZ\t0\t2\n"
                                                        "愛\t사랑\tN110\t\t\tGN\t0\t3\n"
                                                        "帰\t돌아가&AA@\tM428\t\t\tGN\t0\t4\n"));
    HD_REQUIRE(WriteText(temp.path / "names.tsv", "#! layer=names\n" + header + "アリス\t앨리스\tN130\tQPN\tWa\tGN\t0\t1\n"
                                                         "犬\t이누\tN130\tQPN\tWc\tPN\t0\t2\n"));
    HD_REQUIRE(WriteText(temp.path / "userdict.tsv", "#! layer=user\n" + header + "犬\t유저개\tN110\tQPNVHnZ9!\tWzQx\tUDX\t5\t1\n"
                                                            "犬\t유저강아지\tN130\tZZZQCT\tAaBb\tNovel\t4\t2\n"
                                                            "愛\t유저사랑\tE300\tXXXVAn\tCc\tXP\t3\t3\n"));
    HD_REQUIRE(WriteText(temp.path / "plain.tsv", "帰る\t돌아가다\tv\n称える\t일컫다\tv1:ㄷ\n白い\t하얗다\tadj\n"));
    HD_REQUIRE(WriteText(temp.path / "kana.tsv", "#! layer=kana\nあい\t愛\t\n"));
    HD_REQUIRE(WriteText(temp.path / "readings.tsv", "#! layer=readings\n風音\t카자네\n"));
    HD_REQUIRE(WriteText(temp.path / "names2.tsv", "翔太\t쇼타\tname\n"));
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    HD_REQUIRE(editable.snapshot->User() != nullptr && editable.snapshot->Names() != nullptr);
    HD_REQUIRE(editable.snapshot->PlainFiles().size() == 4);
    const auto user = editable.snapshot->User()->Records("犬");
    HD_REQUIRE(user.size() == 2);
    HD_CHECK_EQ(std::string(user[0].target), "유저개");
    HD_CHECK_EQ(std::string(user[1].target), "유저강아지");
    HD_CHECK(user[0].codes->sem.Text() == "QPNVHnZ9!");
    HD_CHECK(user[0].codes->gram.Text() == "WzQx");
    HD_CHECK(user[0].codes->dict.Text() == "UDX");
    const auto built = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(built.snapshot != nullptr);
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto time = std::filesystem::last_write_time(Utf8Path(CacheResource(path)));
    for (bool edit : {false, true}) {
        if (edit) {
            HD_REQUIRE(WriteText(temp.path / "plain.tsv", "帰る\t돌아가다\tv\n黄色い\t누르다\tadj:러\n"));
            HD_REQUIRE(WriteText(temp.path / "names2.tsv", "翔太\t쇼타\tname\tattr=QPNVHnNew gattr=WbXx\n"));
        }
        const auto loaded = hdTrans::dict::LoadFolders(folders, cache, true);
        const auto current = hdTrans::dict::LoadFoldersTsv(folders);
        HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
        HD_REQUIRE(current.snapshot != nullptr);
        HD_REQUIRE(loaded.snapshot->User() != nullptr && loaded.snapshot->Names() != nullptr);
        HD_REQUIRE(current.snapshot->User() != nullptr && current.snapshot->Names() != nullptr);
        CheckDictEncoding(loaded.snapshot->Base(), current.snapshot->Base());
        CheckDictEncoding(*loaded.snapshot->User(), *current.snapshot->User());
        CheckDictEncoding(*loaded.snapshot->Names(), *current.snapshot->Names());
        CheckPlainCompilation(*loaded.snapshot, *current.snapshot);
        CheckLoadErrors(loaded.errors, current.errors);
        HD_CHECK(ReadBytes(path) == bytes);
        HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
    }
}

HD_TEST(SnapshotWarmPlainLongPrefixAndBody) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    const std::vector<std::string> folders{temp.File("")};
    HD_REQUIRE(WriteText(temp.path / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    HD_REQUIRE(WriteText(temp.path / "plain.tsv", "猫\t냥이\tn\n"));
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto time = std::filesystem::last_write_time(Utf8Path(CacheResource(path)));
    const std::string target(131072, 'x');
    const std::string plain = "\xEF\xBB\xBF" + std::string(131072, '\r') + "\n//" + std::string(70000, 'x') +
                              "\r\n猫\t냥이\tn\r\n犬\t" + target + "\tn";
    HD_REQUIRE(WriteText(temp.path / "plain.tsv", plain));
    const auto loaded = hdTrans::dict::LoadFolders(folders, cache, true);
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
    HD_REQUIRE(editable.snapshot != nullptr);
    HD_REQUIRE(loaded.snapshot->PlainFiles().size() == 1);
    HD_CHECK_EQ(loaded.snapshot->PlainFiles()[0].text, plain);
    const auto dog = loaded.snapshot->Plain().Search("犬", hdTrans::dict::LookupMode::General);
    HD_REQUIRE(dog.size() == 1);
    HD_CHECK_EQ(dog[0]->target, target);
    CheckPlainCompilation(*loaded.snapshot, *editable.snapshot);
    CheckLoadErrors(loaded.errors, editable.errors);
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(CacheResource(path))) == time);
}

namespace {

size_t FindSection(std::span<const std::byte> bytes, uint32_t kind, uint32_t layer) {
    if (bytes.size() < 256) return bytes.size();
    const auto count = Field(bytes, 40, 4);
    if (count > (bytes.size() - 256) / 64) return bytes.size();
    for (size_t i = 0; i < count; i++) {
        const size_t at = 256 + 64 * i;
        if (Field(bytes, at, 4) == kind && Field(bytes, at + 4, 4) == layer) return at;
    }
    return bytes.size();
}

std::string LookupDigest(const hdTrans::dict::Snapshot& snapshot) {
    using hdTrans::dict::LookupMode;
    std::string out;
    const auto append = [&](std::string_view value) {
        out += std::to_string(value.size()) + ":";
        out += value;
    };
    const hdTrans::dict::LookupContext context;
    for (const std::string_view key : {"学生です", "アリスさん", "霧島さん", "行くの", "の", ""}) {
        for (const auto mode : {LookupMode::General, LookupMode::AdjectivalNounEnding, LookupMode::AdjectiveEnding,
                                LookupMode::VerbEnding, LookupMode::PrefixVerb}) {
            const auto got = snapshot.GetWordInfo(key, mode, context);
            append(key);
            out += "/" + std::to_string(static_cast<unsigned>(mode)) + "/" + std::to_string(got.count);
            for (const auto& c : got.list) {
                append(c.source);
                append(c.target);
                if (c.codes) {
                    append(c.codes->partText);
                    append(c.codes->sem.Text());
                    append(c.codes->gram.Text());
                    append(c.codes->dict.Text());
                } else {
                    append("null codes");
                }
                out += "/" + std::to_string(c.rule) + "/" + std::to_string(c.id);
                out += c.user ? (c.user->nameLayer ? "/name" : "/plain") : "/column";
            }
        }
    }
    return out;
}

}  // namespace

HD_TEST(SnapshotEveryLayerSectionCorruption) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedEnvironment strictEnv("HDTRANS_VERIFY_DICTS", nullptr);
    ScopedEnvironment snapshotEnv("HDTRANS_SNAPSHOT", nullptr);
    const std::vector<std::string> folders{FixtureFolder()};
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    HD_REQUIRE(editable.snapshot->User() != nullptr && editable.snapshot->Names() != nullptr);
    const hdTrans::dict::Dict* layers[] = {&editable.snapshot->Base(), editable.snapshot->User(), editable.snapshot->Names()};
    const auto expected = LookupDigest(*editable.snapshot);
    size_t tested = 0, skipped = 0;
    for (uint32_t layer = 0; layer < 3; layer++) {
        for (uint32_t kind = 10; kind <= 17; kind++) {
            const auto cache = temp.File("cache-" + std::to_string(layer) + "-" + std::to_string(kind));
            HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
            const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
            HD_REQUIRE(!path.empty());
            auto bytes = ReadBytes(path);
            HD_REQUIRE(bytes.size() >= 256);
            const size_t section = FindSection(bytes, kind, layer);
            HD_REQUIRE(section != bytes.size());
            HD_CHECK_EQ(Field(bytes, 160 + 8 * layer, 8), static_cast<uint64_t>(layers[layer]->Len()));
            const auto length = Field(bytes, section + 16, 8);
            if (length == 0) {
                HD_CHECK_EQ(layers[layer]->Len(), size_t{0});
                HD_CHECK_EQ(Field(bytes, section + 24, 8), uint64_t{0});
                HD_CHECK_EQ(Field(bytes, section + 44, 4), uint64_t{0});
                skipped++;
                continue;
            }
            HD_REQUIRE(Field(bytes, section + 24, 8) > 0);
            HD_REQUIRE(Field(bytes, section + 44, 4) > 0);
            const auto offset = Field(bytes, section + 8, 8);
            HD_REQUIRE(offset < bytes.size() && length <= bytes.size() - offset);
            bytes[static_cast<size_t>(offset)] ^= std::byte{1};
            HD_REQUIRE(WriteBytes(path, bytes));
            const auto loaded = hdTrans::dict::LoadFolders(folders, cache);
            HD_REQUIRE(loaded.snapshot != nullptr);
            CheckLoadErrors(loaded.errors, editable.errors);
            if (kind == 16) {
                HD_CHECK(!loaded.snapshot->Mapped());
                HD_CHECK(loaded.snapshot->Healthy());
            } else {
                HD_REQUIRE(loaded.snapshot->Mapped());
                HD_CHECK(loaded.snapshot->Healthy());
            }
            HD_CHECK_EQ(LookupDigest(*loaded.snapshot), expected);
            if (kind != 16) {
                HD_CHECK(!loaded.snapshot->Healthy());
                const auto fallback = loaded.snapshot->Fallback();
                HD_REQUIRE(fallback != nullptr);
                HD_CHECK(!fallback->Mapped());
                HD_CHECK(fallback->Healthy());
                HD_CHECK_EQ(LookupDigest(*fallback), expected);
                const auto repair = hdTrans::dict::LoadFolders(folders, cache);
                HD_REQUIRE(repair.snapshot != nullptr);
                HD_CHECK(!repair.snapshot->Mapped());
                HD_CHECK(repair.snapshot->Healthy());
                HD_CHECK_EQ(LookupDigest(*repair.snapshot), expected);
            }
            const auto reopened = hdTrans::dict::LoadFolders(folders, cache);
            HD_REQUIRE(reopened.snapshot != nullptr && reopened.snapshot->Mapped());
            HD_CHECK(reopened.snapshot->Healthy());
            HD_CHECK_EQ(LookupDigest(*reopened.snapshot), expected);
            tested++;
        }
    }
    HD_CHECK_EQ(tested + skipped, size_t{24});
    HD_CHECK(tested >= 3);
}

HD_TEST(SnapshotEightThreadFirstTouchAndFallback) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedEnvironment strictEnv("HDTRANS_VERIFY_DICTS", nullptr);
    ScopedEnvironment snapshotEnv("HDTRANS_SNAPSHOT", nullptr);
    const std::vector<std::string> folders{FixtureFolder()};
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    const auto expected = LookupDigest(*editable.snapshot);
    for (bool corrupt : {false, true}) {
        const auto cache = temp.File(corrupt ? "bad-cache" : "good-cache");
        HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
        const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
        HD_REQUIRE(!path.empty());
        if (corrupt) {
            auto bytes = ReadBytes(path);
            HD_REQUIRE(bytes.size() >= 256);
            const size_t section = FindSection(bytes, 12, 2);
            HD_REQUIRE(section != bytes.size());
            HD_REQUIRE(Field(bytes, section + 16, 8) > 0);
            const auto offset = Field(bytes, section + 8, 8);
            HD_REQUIRE(offset < bytes.size());
            bytes[static_cast<size_t>(offset)] ^= std::byte{1};
            HD_REQUIRE(WriteBytes(path, bytes));
        }
        const auto loaded = hdTrans::dict::LoadFolders(folders, cache);
        HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
        HD_REQUIRE(loaded.snapshot->Healthy());
        std::barrier start(8);
        std::array<bool, 8> equal;
        equal.fill(true);
        std::array<std::shared_ptr<const hdTrans::dict::Snapshot>, 8> fallbacks;
        std::array<std::thread, 8> threads;
        for (size_t i = 0; i < threads.size(); i++) {
            threads[i] = std::thread([&, i] {
                start.arrive_and_wait();
                for (int n = 0; n < 16; n++) {
                    if (LookupDigest(*loaded.snapshot) != expected) equal[i] = false;
                    if (corrupt) {
                        const auto fallback = loaded.snapshot->Fallback();
                        if (!fallback || LookupDigest(*fallback) != expected) equal[i] = false;
                        fallbacks[i] = fallback;
                    }
                }
            });
        }
        for (auto& thread : threads) thread.join();
        for (const bool value : equal) HD_CHECK(value);
        HD_CHECK_EQ(loaded.snapshot->Healthy(), !corrupt);
        if (corrupt) {
            HD_REQUIRE(fallbacks[0] != nullptr);
            for (const auto& fallback : fallbacks) {
                HD_REQUIRE(fallback != nullptr);
                HD_CHECK(fallback.get() == fallbacks[0].get());
                HD_CHECK(!fallback->Mapped());
                HD_CHECK(fallback->Healthy());
            }
            const auto repair = hdTrans::dict::LoadFolders(folders, cache);
            HD_REQUIRE(repair.snapshot != nullptr && !repair.snapshot->Mapped());
        } else {
            HD_CHECK(loaded.snapshot->Fallback() == nullptr);
        }
        const auto reopened = hdTrans::dict::LoadFolders(folders, cache);
        HD_REQUIRE(reopened.snapshot != nullptr && reopened.snapshot->Mapped());
        HD_CHECK(reopened.snapshot->Healthy());
        HD_CHECK_EQ(LookupDigest(*reopened.snapshot), expected);
    }
}

HD_TEST(SnapshotNormalAndStrictSourceVerification) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedEnvironment strictEnv("HDTRANS_VERIFY_DICTS", nullptr);
    ScopedEnvironment snapshotEnv("HDTRANS_SNAPSHOT", nullptr);
    const auto cache = temp.File("cache");
    const auto source = temp.path / "base.tsv";
    const std::vector<std::string> folders{temp.File("")};
    const std::string header = "key\tko\tpos\tid\n";
    HD_REQUIRE(WriteText(source, header + "猫\t고양이\tN110\t1\n"));
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    const auto baseline = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(baseline.snapshot != nullptr && baseline.snapshot->Mapped());
    hdTrans::dict::internal::FileStamp before, after;
    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(temp.File("base.tsv"), before));
    const auto time = std::filesystem::last_write_time(source);
    HD_REQUIRE(WriteText(source, header + "猫\t야옹이\tN110\t1\n"));
    std::filesystem::last_write_time(source, time);
    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(temp.File("base.tsv"), after));
    HD_REQUIRE(before == after);
    const auto normal = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(normal.snapshot != nullptr && normal.snapshot->Mapped());
    HD_CHECK(normal.snapshot->Healthy());
    HD_CHECK_EQ(TargetOf(*normal.snapshot, "猫"), "고양이");
    const auto strict = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(strict.snapshot != nullptr && !strict.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*strict.snapshot, "猫"), "야옹이");
    const auto repaired = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(repaired.snapshot != nullptr && repaired.snapshot->Mapped());
    HD_CHECK(repaired.snapshot->Healthy());
    HD_CHECK_EQ(TargetOf(*repaired.snapshot, "猫"), "야옹이");

    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(temp.File("base.tsv"), before));
    HD_REQUIRE(WriteText(source, header + "猫\t고양이\tN110\t1\n"));
    std::filesystem::last_write_time(source, time);
    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(temp.File("base.tsv"), after));
    HD_REQUIRE(before == after);
    strictEnv.Set("1");
    const auto environmentStrict = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(environmentStrict.snapshot != nullptr && !environmentStrict.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*environmentStrict.snapshot, "猫"), "고양이");
    const auto environmentReopen = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(environmentReopen.snapshot != nullptr && environmentReopen.snapshot->Mapped());
    HD_CHECK(environmentReopen.snapshot->Healthy());
    HD_CHECK_EQ(TargetOf(*environmentReopen.snapshot, "猫"), "고양이");
}

HD_TEST(SnapshotEnvironmentOffLeavesCacheUntouched) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedEnvironment strictEnv("HDTRANS_VERIFY_DICTS", nullptr);
    ScopedEnvironment snapshotEnv("HDTRANS_SNAPSHOT", nullptr);
    const auto cache = temp.File("cache");
    const auto unusedCache = temp.File("unused-cache");
    const std::vector<std::string> folders{FixtureFolder()};
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    const auto loaded = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    const auto unusedPath = hdTrans::dict::internal::SnapshotPath(folders, unusedCache);
    const auto resource = hdTrans::dict::internal::SnapshotResourcePath(path);
    HD_REQUIRE(!resource.empty());
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto time = std::filesystem::last_write_time(Utf8Path(resource));
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    snapshotEnv.Set("off");
    HD_CHECK(hdTrans::dict::internal::SnapshotPath(folders, cache).empty());
    for (const auto& dir : {cache, unusedCache}) {
        const auto disabled = hdTrans::dict::LoadFolders(folders, dir, true);
        HD_REQUIRE(disabled.snapshot != nullptr);
        HD_CHECK(!disabled.snapshot->Mapped());
        HD_CHECK(disabled.snapshot->Healthy());
        HD_CHECK_EQ(LookupDigest(*disabled.snapshot), LookupDigest(*editable.snapshot));
        CheckPlainCompilation(*disabled.snapshot, *editable.snapshot);
        CheckLoadErrors(disabled.errors, editable.errors);
    }
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(resource)) == time);
    HD_CHECK(!std::filesystem::exists(Utf8Path(unusedPath)));
    snapshotEnv.Set(nullptr);
    const auto reopened = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(reopened.snapshot != nullptr && reopened.snapshot->Mapped());
    HD_CHECK_EQ(LookupDigest(*reopened.snapshot), LookupDigest(*editable.snapshot));
}

HD_TEST(SnapshotBuiltinAndDataHeaderIdentityMismatch) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedEnvironment strictEnv("HDTRANS_VERIFY_DICTS", nullptr);
    ScopedEnvironment snapshotEnv("HDTRANS_SNAPSHOT", nullptr);
    const std::vector<std::string> folders{FixtureFolder()};
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    const auto expected = LookupDigest(*editable.snapshot);
    for (const size_t identity : {size_t{96}, size_t{128}}) {
        const auto cache = temp.File("cache-" + std::to_string(identity));
        HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
        const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
        HD_REQUIRE(!path.empty());
        auto bytes = ReadBytes(path);
        HD_REQUIRE(bytes.size() >= 256);
        bytes[identity] ^= std::byte{1};
        FixDirectories(bytes);
        std::vector<std::byte> header(bytes.begin(), bytes.begin() + 256);
        SetField(header, 192, 0, 4);
        HD_CHECK_EQ(hdTrans::dict::internal::Crc32c(header), static_cast<uint32_t>(Field(bytes, 192, 4)));
        HD_REQUIRE(WriteBytes(path, bytes));
        const auto rejected = hdTrans::dict::LoadFolders(folders, cache);
        HD_REQUIRE(rejected.snapshot != nullptr && !rejected.snapshot->Mapped());
        HD_CHECK(rejected.snapshot->Healthy());
        HD_CHECK_EQ(LookupDigest(*rejected.snapshot), expected);
        CheckLoadErrors(rejected.errors, editable.errors);
        const auto reopened = hdTrans::dict::LoadFolders(folders, cache);
        HD_REQUIRE(reopened.snapshot != nullptr && reopened.snapshot->Mapped());
        HD_CHECK(reopened.snapshot->Healthy());
        HD_CHECK_EQ(LookupDigest(*reopened.snapshot), expected);
    }
}

namespace {

void CheckMetadataStamp(std::span<const std::byte> meta, std::span<const std::byte> resource,
                        const hdTrans::dict::internal::FileStamp& stamp) {
    HD_REQUIRE(meta.size() >= 44 && resource.size() >= 96);
    HD_CHECK(std::memcmp(meta.data(), "HDMETA\r\n", 8) == 0);
    HD_CHECK_EQ(hdTrans::dict::internal::Crc32c(meta.first(meta.size() - 4)),
                static_cast<uint32_t>(Field(meta, meta.size() - 4, 4)));
    HD_CHECK(std::equal(meta.begin() + 8, meta.begin() + 40, resource.begin() + 64));
    HD_REQUIRE(Field(meta, 40, 4) == 1);
    HD_REQUIRE(meta.size() >= 52);
    HD_CHECK_EQ(Field(meta, 44, 4), uint64_t{0});
    const auto nameSize = Field(meta, 48, 4);
    HD_REQUIRE(nameSize <= meta.size() - 52);
    const size_t at = 52 + static_cast<size_t>(nameSize);
    HD_REQUIRE(meta.size() - at == 36 + 32 + 4);
    HD_CHECK(std::string_view(reinterpret_cast<const char*>(meta.data() + 52), static_cast<size_t>(nameSize)) == "base.tsv");
    HD_CHECK_EQ(Field(meta, at, 8), stamp.size);
    HD_CHECK_EQ(Field(meta, at + 8, 8), static_cast<uint64_t>(stamp.mtimeNs));
    HD_CHECK_EQ(Field(meta, at + 16, 8), stamp.identityHigh);
    HD_CHECK_EQ(Field(meta, at + 24, 8), stamp.identityLow);
    HD_CHECK_EQ(Field(meta, at + 32, 4), static_cast<uint64_t>(stamp.platform));
}

}  // namespace

HD_TEST(SnapshotMetadataRefreshAndInvalidSidecars) {
    CacheTestEnvironment environment;
    TempFolder temp;
    ScopedEnvironment strictEnv("HDTRANS_VERIFY_DICTS", nullptr);
    ScopedEnvironment snapshotEnv("HDTRANS_SNAPSHOT", nullptr);
    const auto first = temp.path / "first";
    const auto second = temp.path / "second";
    std::error_code ec;
    HD_REQUIRE(std::filesystem::create_directory(first, ec) && !ec);
    HD_REQUIRE(std::filesystem::create_directory(second, ec) && !ec);
    const auto cache = temp.File("cache");
    const std::vector<std::string> folders{temp.File("first")};
    const std::vector<std::string> otherFolders{temp.File("second")};
    const std::string header = "key\tko\tpos\tid\n";
    HD_REQUIRE(WriteText(first / "base.tsv", header + "猫\t고양이\tN110\t1\n"));
    HD_REQUIRE(WriteText(second / "base.tsv", header + "猫\t야옹이\tN110\t1\n"));
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    const auto baseline = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(baseline.snapshot != nullptr && baseline.snapshot->Mapped());
    const auto path = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!path.empty());
    const auto resource = hdTrans::dict::internal::SnapshotResourcePath(path);
    HD_REQUIRE(!resource.empty());
    const auto bytes = ReadBytes(path);
    HD_REQUIRE(bytes.size() >= 256);
    const auto resourceTime = std::filesystem::last_write_time(Utf8Path(resource));
    const auto sourceTime = std::filesystem::last_write_time(first / "base.tsv") + std::chrono::seconds(2);
    std::filesystem::last_write_time(first / "base.tsv", sourceTime);
    hdTrans::dict::internal::FileStamp refreshedStamp;
    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(temp.File("first/base.tsv"), refreshedStamp));
    const auto refreshed = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(refreshed.snapshot != nullptr && refreshed.snapshot->Mapped());
    HD_CHECK_EQ(hdTrans::dict::internal::SnapshotResourcePath(path), resource);
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(resource)) == resourceTime);
    const auto metaPath = resource + ".meta";
    const auto validMeta = ReadBytes(metaPath);
    CheckMetadataStamp(validMeta, bytes, refreshedStamp);
    for (int variant = 0; variant < 2; variant++) {
        auto corrupt = validMeta;
        HD_REQUIRE(corrupt.size() > 44);
        if (variant == 0) corrupt.resize(16);
        else corrupt.back() ^= std::byte{1};
        HD_REQUIRE(WriteBytes(metaPath, corrupt));
        const auto loaded = hdTrans::dict::LoadFolders(folders, cache);
        HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
        HD_CHECK(loaded.snapshot->Healthy());
        HD_CHECK_EQ(TargetOf(*loaded.snapshot, "猫"), "고양이");
        CheckMetadataStamp(ReadBytes(metaPath), bytes, refreshedStamp);
        HD_CHECK(ReadBytes(path) == bytes);
        HD_CHECK(std::filesystem::last_write_time(Utf8Path(resource)) == resourceTime);
    }

    HD_REQUIRE(hdTrans::dict::LoadFolders(otherFolders, cache).snapshot != nullptr);
    const auto otherPath = hdTrans::dict::internal::SnapshotPath(otherFolders, cache);
    HD_REQUIRE(!otherPath.empty());
    const auto otherResource = hdTrans::dict::internal::SnapshotResourcePath(otherPath);
    HD_REQUIRE(!otherResource.empty());
    const auto otherTime = std::filesystem::last_write_time(second / "base.tsv") + std::chrono::seconds(2);
    std::filesystem::last_write_time(second / "base.tsv", otherTime);
    const auto other = hdTrans::dict::LoadFolders(otherFolders, cache);
    HD_REQUIRE(other.snapshot != nullptr && other.snapshot->Mapped());
    const auto foreign = ReadBytes(otherResource + ".meta");
    HD_REQUIRE(foreign.size() >= 44 && validMeta.size() >= 44);
    HD_CHECK(!std::equal(foreign.begin() + 8, foreign.begin() + 40, validMeta.begin() + 8));
    HD_REQUIRE(WriteBytes(metaPath, foreign));
    const auto foreignIgnored = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(foreignIgnored.snapshot != nullptr && foreignIgnored.snapshot->Mapped());
    HD_CHECK(foreignIgnored.snapshot->Healthy());
    HD_CHECK_EQ(TargetOf(*foreignIgnored.snapshot, "猫"), "고양이");
    const auto currentMeta = ReadBytes(metaPath);
    CheckMetadataStamp(currentMeta, bytes, refreshedStamp);
    HD_CHECK(ReadBytes(path) == bytes);
    HD_CHECK(std::filesystem::last_write_time(Utf8Path(resource)) == resourceTime);

    HD_REQUIRE(WriteText(first / "base.tsv", header + "猫\t야옹이\tN110\t1\n"));
    std::filesystem::last_write_time(first / "base.tsv", sourceTime);
    hdTrans::dict::internal::FileStamp sameStamp;
    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(temp.File("first/base.tsv"), sameStamp));
    HD_REQUIRE(sameStamp == refreshedStamp);
    const auto stampHit = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(stampHit.snapshot != nullptr && stampHit.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*stampHit.snapshot, "猫"), "고양이");
    HD_CHECK(ReadBytes(metaPath) == currentMeta);
    const auto strict = hdTrans::dict::LoadFolders(folders, cache, true);
    HD_REQUIRE(strict.snapshot != nullptr && !strict.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*strict.snapshot, "猫"), "야옹이");
    HD_CHECK_EQ(TargetOf(*baseline.snapshot, "猫"), "고양이");
    const auto reopened = hdTrans::dict::LoadFolders(folders, cache);
    HD_REQUIRE(reopened.snapshot != nullptr && reopened.snapshot->Mapped());
    HD_CHECK(reopened.snapshot->Healthy());
    HD_CHECK_EQ(TargetOf(*reopened.snapshot, "猫"), "야옹이");
}

HD_TEST(SnapshotCompilerRequiresColumnDictionary) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    ScopedEnvironment cacheEnv("HDTRANS_CACHE_DIR", cache.c_str());
    HD_REQUIRE(WriteText(temp.path / "plain.tsv", "猫\t고양이\tn\n"));
    HD_CHECK(!hdTrans::dict::CompileSnapshot({temp.File("")}, temp.File("plain.hdsnap")));
    HD_CHECK(!std::filesystem::exists(temp.path / "plain.hdsnap"));
    HD_CHECK(!hdTrans::dict::internal::CompileCache({temp.File("")}));
    const auto loaded = hdTrans::dict::LoadFolders({temp.File("")});
    HD_REQUIRE(loaded.snapshot != nullptr);
    HD_CHECK(!loaded.snapshot->HasDictionary());
    HD_CHECK(!std::filesystem::exists(Utf8Path(cache)));
}

HD_TEST(SnapshotStrictEngineReloadRetainsVerification) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    ScopedEnvironment cacheEnv("HDTRANS_CACHE_DIR", cache.c_str());
    ScopedEnvironment strictEnv("HDTRANS_VERIFY_DICTS", nullptr);
    const auto source = temp.path / "base.tsv";
    const std::vector<std::string> folders{temp.File("")};
    const std::string header = "key\tko\tpos\tid\n";
    HD_REQUIRE(WriteText(source, header + "猫\t고양이\tN110\t1\n"));
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders).snapshot != nullptr);
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open(folders, true));
    HD_REQUIRE(engine.Dictionaries() != nullptr);
    HD_REQUIRE(engine.Dictionaries()->Mapped());
    const auto time = std::filesystem::last_write_time(source);
    HD_REQUIRE(WriteText(source, header + "猫\t야옹이\tN110\t1\n"));
    std::filesystem::last_write_time(source, time);
    HD_REQUIRE(engine.Reload());
    HD_CHECK(!engine.Dictionaries()->Mapped());
    HD_CHECK_EQ(TargetOf(*engine.Dictionaries(), "猫"), "야옹이");
}

#ifndef _WIN32
HD_TEST(SnapshotUnreadableSourcesReportOnlyReadErrors) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    const auto plain = temp.path / "plain.tsv";
    const std::vector<std::string> folders{temp.File("")};
    HD_REQUIRE(WriteText(temp.path / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n謎\t수수께끼\tunknown-tag\t2\n"));
    HD_REQUIRE(WriteText(plain, "犬\t개\tn\n"));
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    std::error_code ec;
    std::filesystem::permissions(plain, std::filesystem::perms::none, ec);
    HD_REQUIRE(!ec);
    const auto warm = hdTrans::dict::LoadFolders(folders, cache);
    const auto cold = hdTrans::dict::LoadFoldersTsv(folders);
    std::filesystem::permissions(plain, std::filesystem::perms::owner_all, ec);
    HD_REQUIRE(!ec);
    HD_REQUIRE(!warm.snapshot && !cold.snapshot);
    CheckLoadErrors(warm.errors, cold.errors);
    HD_REQUIRE(warm.errors.size() == 1);
    HD_CHECK(warm.errors[0].kind == hdTrans::dict::LoadErrorKind::Read);
}
#endif

HD_TEST(SnapshotTestsRestoreInheritedCacheEnvironment) {
    TempFolder temp;
    ScopedEnvironment home("HOME", temp.File("home").c_str());
    ScopedEnvironment local("LOCALAPPDATA", temp.File("local").c_str());
    ScopedEnvironment xdg("XDG_CACHE_HOME", temp.File("xdg").c_str());
    ScopedEnvironment cache("HDTRANS_CACHE_DIR", nullptr);
    ScopedEnvironment snapshot("HDTRANS_SNAPSHOT", nullptr);
    ScopedEnvironment strict("HDTRANS_VERIFY_DICTS", "1");
    const auto custom = temp.File("outer-cache");
    const char* cacheValues[] = {nullptr, "", "off", custom.c_str()};
    const char* snapshotValues[] = {nullptr, "off"};
    const auto tests = {
        hdtest_SnapshotFileWriter,
        hdtest_SnapshotFileRoundTrip,
        hdtest_SnapshotSourceInvalidation,
        hdtest_SnapshotCorruptFileFallback,
        hdtest_SnapshotCacheEnvironmentOverride,
    };
    for (const char* cacheValue : cacheValues) {
        cache.Set(cacheValue);
        for (const char* snapshotValue : snapshotValues) {
            snapshot.Set(snapshotValue);
            for (const auto test : tests) {
                const int failures = hdtest::Cur().failures;
                test();
                HD_REQUIRE(hdtest::Cur().failures == failures);
                HD_CHECK(cache.Matches(cacheValue));
                HD_CHECK(snapshot.Matches(snapshotValue));
                HD_CHECK(strict.Matches("1"));
            }
        }
    }
    HD_CHECK(!std::filesystem::exists(Utf8Path(custom)));
}

namespace {

using CacheTreeEntry = std::tuple<std::filesystem::path, std::filesystem::file_type,
                                  std::filesystem::file_time_type, uintmax_t>;

bool ReadCacheTreeMetadata(const std::filesystem::path& root, std::vector<CacheTreeEntry>& entries) {
    if (root.empty()) return true;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return !ec;
    const auto add = [&](const std::filesystem::path& path) {
        const auto status = std::filesystem::symlink_status(path, ec);
        if (ec) return false;
        auto time = std::filesystem::file_time_type{};
        if (!std::filesystem::is_symlink(status)) time = std::filesystem::last_write_time(path, ec);
        if (ec) return false;
        uintmax_t size = 0;
        if (std::filesystem::is_regular_file(status)) size = std::filesystem::file_size(path, ec);
        if (ec) return false;
        entries.emplace_back(path, status.type(), time, size);
        return true;
    };
    if (!add(root)) return false;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!add(it->path())) return false;
    }
    if (ec) return false;
    std::sort(entries.begin(), entries.end());
    return true;
}

}  // namespace

HD_TEST(SnapshotSuiteDoesNotWriteDefaultCache) {
    TempFolder temp;
    ScopedEnvironment cache("HDTRANS_CACHE_DIR", nullptr);
    ScopedEnvironment snapshot("HDTRANS_SNAPSHOT", nullptr);
    ScopedEnvironment strict("HDTRANS_VERIFY_DICTS", "1");
    const auto realIndex = hdTrans::dict::internal::SnapshotPath({FixtureFolder()});
    const auto realRoot = realIndex.empty() ? std::filesystem::path{} : Utf8Path(realIndex).parent_path();
    std::vector<CacheTreeEntry> before, after;
    HD_REQUIRE(ReadCacheTreeMetadata(realRoot, before));
    ScopedEnvironment home("HOME", temp.File("home").c_str());
    ScopedEnvironment local("LOCALAPPDATA", temp.File("local").c_str());
    ScopedEnvironment xdg("XDG_CACHE_HOME", temp.File("xdg").c_str());
    const auto defaultIndex = hdTrans::dict::internal::SnapshotPath({FixtureFolder()});
    HD_REQUIRE(!defaultIndex.empty());
    const auto defaultRoot = Utf8Path(defaultIndex).parent_path();
    const auto relative = defaultRoot.lexically_relative(temp.path);
    HD_REQUIRE(!relative.empty() && !relative.is_absolute() && *relative.begin() != "..");
    HD_REQUIRE(!std::filesystem::exists(defaultRoot));
    const auto custom = temp.File("outer-cache");
    const char* cacheValues[] = {nullptr, "", "off", custom.c_str()};
    const char* snapshotValues[] = {nullptr, "off"};
    size_t selected = 0, runs = 0;
    for (const auto& test : hdtest::Registry()) {
        if (std::string_view(test.name).find("Snapshot") == std::string_view::npos ||
            test.fn == hdtest_SnapshotSuiteDoesNotWriteDefaultCache ||
            test.fn == hdtest_SnapshotTestsRestoreInheritedCacheEnvironment) continue;
        selected++;
        for (const char* cacheValue : cacheValues) {
            cache.Set(cacheValue);
            for (const char* snapshotValue : snapshotValues) {
                snapshot.Set(snapshotValue);
                const int failures = hdtest::Cur().failures;
                test.fn();
                HD_REQUIRE(hdtest::Cur().failures == failures);
                HD_CHECK(cache.Matches(cacheValue));
                HD_CHECK(snapshot.Matches(snapshotValue));
                HD_CHECK(strict.Matches("1"));
                runs++;
            }
        }
    }
    HD_REQUIRE(selected > 0);
    HD_CHECK_EQ(runs, selected * 8);
    HD_REQUIRE(ReadCacheTreeMetadata(realRoot, after));
    HD_CHECK(before == after);
    HD_CHECK(!std::filesystem::exists(defaultRoot));
    HD_CHECK(!std::filesystem::exists(Utf8Path(custom)));
}

HD_TEST(SnapshotCompileCacheReusesFullyVerifiedGeneration) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto cache = temp.File("cache");
    ScopedEnvironment overrideDir("HDTRANS_CACHE_DIR", cache.c_str());
    const auto source = temp.path / "base.tsv";
    const std::vector<std::string> folders{temp.File("")};
    HD_REQUIRE(WriteText(source, "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    HD_REQUIRE(hdTrans::dict::internal::CompileCache(folders));
    const auto index = hdTrans::dict::internal::SnapshotPath(folders);
    HD_REQUIRE(!index.empty());
    const auto resource = hdTrans::dict::internal::SnapshotResourcePath(index);
    HD_REQUIRE(!resource.empty());
    const auto bytes = ReadBytes(resource);
    HD_REQUIRE(bytes.size() >= 256);
    const auto indexBytes = hdTrans::dict::internal::MappedFile::Open(index);
    HD_REQUIRE(indexBytes != nullptr);
    const std::vector<std::byte> savedIndex(indexBytes->Bytes().begin(), indexBytes->Bytes().end());
    std::vector<CacheTreeEntry> before, after;
    HD_REQUIRE(ReadCacheTreeMetadata(Utf8Path(cache), before));
    HD_REQUIRE(hdTrans::dict::internal::CompileCache(folders));
    HD_CHECK(hdTrans::dict::internal::SnapshotResourcePath(index) == resource);
    HD_CHECK(ReadBytes(resource) == bytes);
    HD_REQUIRE(ReadCacheTreeMetadata(Utf8Path(cache), after));
    HD_CHECK(before == after);
    const auto currentIndex = hdTrans::dict::internal::MappedFile::Open(index);
    HD_REQUIRE(currentIndex != nullptr && currentIndex->Bytes().size() == savedIndex.size());
    HD_CHECK(std::equal(savedIndex.begin(), savedIndex.end(), currentIndex->Bytes().begin()));

    const auto time = std::filesystem::last_write_time(source);
    HD_REQUIRE(WriteText(source, "key\tko\tpos\tid\n猫\t야옹이\tN110\t1\n"));
    std::filesystem::last_write_time(source, time);
    HD_REQUIRE(hdTrans::dict::internal::CompileCache(folders));
    const auto changed = hdTrans::dict::internal::SnapshotResourcePath(index);
    HD_REQUIRE(!changed.empty() && changed != resource);
    const auto loaded = hdTrans::dict::LoadFolders(folders, {}, true, false);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped());
    HD_CHECK_EQ(TargetOf(*loaded.snapshot, "猫"), "야옹이");

    auto corrupt = ReadBytes(changed);
    HD_REQUIRE(corrupt.size() >= 256);
    const auto section = FindSection(corrupt, 12, 2);
    HD_REQUIRE(section != corrupt.size());
    const auto offset = Field(corrupt, section + 8, 8);
    HD_REQUIRE(offset < corrupt.size());
    corrupt[static_cast<size_t>(offset)] ^= std::byte{1};
    {
        std::fstream file(Utf8Path(changed), std::ios::binary | std::ios::in | std::ios::out);
        HD_REQUIRE(file.is_open());
        file.seekp(static_cast<std::streamoff>(offset));
        file.put(static_cast<char>(std::to_integer<unsigned char>(corrupt[static_cast<size_t>(offset)])));
        file.flush();
        HD_REQUIRE(file.good());
    }
    HD_REQUIRE(hdTrans::dict::internal::CompileCache(folders));
    HD_CHECK(hdTrans::dict::internal::SnapshotResourcePath(index) != changed);
    const auto repaired = hdTrans::dict::LoadFolders(folders, {}, true, false);
    HD_REQUIRE(repaired.snapshot != nullptr && repaired.snapshot->Mapped());
    HD_CHECK(repaired.snapshot->Healthy());
    HD_CHECK_EQ(TargetOf(*repaired.snapshot, "猫"), "야옹이");
}

HD_TEST(SnapshotOffNoPublicationAllowsCorruptGenerationQuarantine) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const auto source = temp.path / "dict";
    std::error_code ec;
    std::filesystem::copy(Utf8Path(FixtureFolder()), source, std::filesystem::copy_options::recursive, ec);
    HD_REQUIRE(!ec);
    const auto cache = temp.File("cache");
    const std::vector<std::string> folders{temp.File("dict")};
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    const auto expected = LookupDigest(*editable.snapshot);
    HD_REQUIRE(hdTrans::dict::LoadFolders(folders, cache).snapshot != nullptr);
    const auto index = hdTrans::dict::internal::SnapshotPath(folders, cache);
    HD_REQUIRE(!index.empty());
    const auto resource = hdTrans::dict::internal::SnapshotResourcePath(index);
    HD_REQUIRE(!resource.empty());
    const auto rawIndex = hdTrans::dict::internal::MappedFile::Open(index);
    HD_REQUIRE(rawIndex != nullptr);
    const std::vector<std::byte> indexBytes(rawIndex->Bytes().begin(), rawIndex->Bytes().end());
    hdTrans::dict::internal::FileStamp indexStamp;
    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(index, indexStamp));
    std::vector<std::filesystem::path> before;
    for (std::filesystem::directory_iterator it(Utf8Path(cache), ec), end; !ec && it != end; it.increment(ec)) {
        before.push_back(it->path());
    }
    HD_REQUIRE(!ec);
    auto bytes = ReadBytes(index);
    HD_REQUIRE(bytes.size() >= 256);
    const auto section = FindSection(bytes, 12, 2);
    HD_REQUIRE(section != bytes.size());
    HD_REQUIRE(Field(bytes, section + 16, 8) > 0);
    const auto offset = Field(bytes, section + 8, 8);
    HD_REQUIRE(offset < bytes.size());
    bytes[static_cast<size_t>(offset)] ^= std::byte{1};
    HD_REQUIRE(WriteBytes(index, bytes));
    const auto base = source / "base.tsv";
    const auto sourceTime = std::filesystem::last_write_time(base);
    std::filesystem::last_write_time(base, sourceTime + std::chrono::seconds(2));
    const auto readonly = hdTrans::dict::LoadFolders(folders, cache, false, false);
    HD_REQUIRE(readonly.snapshot != nullptr && readonly.snapshot->Mapped());
    HD_REQUIRE(readonly.snapshot->Healthy());
    HD_CHECK(!std::filesystem::exists(Utf8Path(resource + ".meta")));
    HD_CHECK_EQ(LookupDigest(*readonly.snapshot), expected);
    HD_CHECK(!readonly.snapshot->Healthy());
    HD_REQUIRE(std::filesystem::exists(Utf8Path(resource + ".bad")));
    const auto retry = hdTrans::dict::LoadFolders(folders, cache, false, false);
    HD_REQUIRE(retry.snapshot != nullptr && !retry.snapshot->Mapped());
    HD_CHECK_EQ(LookupDigest(*retry.snapshot), expected);
    HD_CHECK(!std::filesystem::exists(Utf8Path(resource + ".meta")));
    const auto currentIndex = hdTrans::dict::internal::MappedFile::Open(index);
    HD_REQUIRE(currentIndex != nullptr);
    HD_REQUIRE(currentIndex->Bytes().size() == indexBytes.size());
    HD_CHECK(std::equal(indexBytes.begin(), indexBytes.end(), currentIndex->Bytes().begin()));
    hdTrans::dict::internal::FileStamp after;
    HD_REQUIRE(hdTrans::dict::internal::ReadStamp(index, after));
    HD_CHECK(after == indexStamp);
    for (std::filesystem::directory_iterator it(Utf8Path(cache), ec), end; !ec && it != end; it.increment(ec)) {
        const auto& path = it->path();
        HD_CHECK(std::find(before.begin(), before.end(), path) != before.end() ||
                 path == Utf8Path(resource + ".bad") || path == Utf8Path(resource + ".quarantine"));
    }
    HD_CHECK(!ec);
}

HD_TEST(SnapshotCorruptCacheWithoutSourcesReturnsCallUnchanged) {
    CacheTestEnvironment environment;
    for (bool rawFirst : {false, true}) {
        TempFolder temp;
        const auto source = temp.path / "dict";
        std::error_code ec;
        HD_REQUIRE(std::filesystem::create_directory(source, ec));
        HD_REQUIRE(!ec);
        const std::string contents = "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n";
        HD_REQUIRE(WriteText(source / "base.tsv", contents));
        const auto cache = temp.File("cache");
        ScopedEnvironment cacheEnv("HDTRANS_CACHE_DIR", cache.c_str());
        const std::vector<std::string> folders{temp.File("dict")};
        HD_REQUIRE(hdTrans::dict::internal::CompileCache(folders));
        const auto index = hdTrans::dict::internal::SnapshotPath(folders);
        HD_REQUIRE(!index.empty());
        const auto resource = hdTrans::dict::internal::SnapshotResourcePath(index);
        HD_REQUIRE(!resource.empty());
        const auto bytes = ReadBytes(resource);
        HD_REQUIRE(bytes.size() >= 256);
        const auto section = FindSection(bytes, 17, 0);
        HD_REQUIRE(section != bytes.size());
        HD_REQUIRE(Field(bytes, section + 16, 8) > 0);
        const auto offset = Field(bytes, section + 8, 8);
        HD_REQUIRE(offset < bytes.size());
        hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
        HD_REQUIRE(engine.Open(folders));
        const auto snapshot = engine.Dictionaries();
        HD_REQUIRE(snapshot != nullptr && snapshot->Mapped() && snapshot->Healthy());
        hdTrans::Session session(engine);
        auto generated = hdTrans::internal::NewGenerated(*snapshot);
        HD_REQUIRE(generated != nullptr);
        const auto time = std::filesystem::last_write_time(Utf8Path(resource));
        hdTrans::dict::internal::FileStamp before, after;
        HD_REQUIRE(hdTrans::dict::internal::ReadStamp(resource, before));
        HD_REQUIRE(std::filesystem::remove_all(source, ec) > 0);
        HD_REQUIRE(!ec);
        {
            std::fstream file(Utf8Path(resource), std::ios::binary | std::ios::in | std::ios::out);
            HD_REQUIRE(file.is_open());
            const char changed = static_cast<char>(std::to_integer<unsigned char>(bytes[static_cast<size_t>(offset)]) ^ 1u);
            file.seekp(static_cast<std::streamoff>(offset));
            file.put(changed);
            file.flush();
            HD_REQUIRE(file.good());
        }
        std::filesystem::last_write_time(Utf8Path(resource), time, ec);
        HD_REQUIRE(!ec);
        HD_REQUIRE(hdTrans::dict::internal::ReadStamp(resource, after));
        HD_REQUIRE(before == after);
        HD_REQUIRE(snapshot->Healthy());
        const std::string text = "猫\r\n猫\n\n猫\r\n";
        const std::string raw = "猫";
        if (rawFirst) {
            HD_CHECK_EQ(hdTrans::internal::TranslateRawLine(*snapshot, *generated, raw, {}), raw);
        } else {
            HD_CHECK_EQ(session.Translate(text), text);
        }
        HD_REQUIRE(!snapshot->Healthy());
        HD_CHECK_EQ(session.Translate(text), text);
        HD_CHECK_EQ(hdTrans::internal::TranslateRawLine(*snapshot, *generated, raw, {}), raw);
        HD_CHECK(!engine.Reload());
        HD_CHECK(engine.loaded());
        HD_CHECK(engine.Dictionaries() == snapshot);
        HD_CHECK(!engine.Open(folders));
        HD_CHECK(engine.Dictionaries() == snapshot);
        HD_REQUIRE(std::filesystem::create_directory(source, ec));
        HD_REQUIRE(!ec);
        HD_REQUIRE(WriteText(source / "base.tsv", contents));
        hdTrans::Engine repaired;
        HD_REQUIRE(repaired.Open(folders));
        HD_REQUIRE(repaired.Dictionaries() != nullptr);
        HD_CHECK(repaired.Dictionaries()->Healthy());
        HD_CHECK_EQ(TargetOf(*repaired.Dictionaries(), "猫"), "고양이");
        hdTrans::Engine reopened(false, hdTrans::CacheWrite::Off);
        HD_REQUIRE(reopened.Open(folders));
        HD_REQUIRE(reopened.Dictionaries() != nullptr);
        HD_CHECK(reopened.Dictionaries()->Mapped());
        HD_CHECK(reopened.Dictionaries()->Healthy());
        HD_CHECK_EQ(TargetOf(*reopened.Dictionaries(), "猫"), "고양이");
    }
}

HD_TEST(EngineOpenReloadRejectGarbageSourcesKeepsPriorSnapshot) {
    CacheTestEnvironment environment;
    for (std::string_view invalid : {std::string_view{}, std::string_view{"not a dictionary\n猫\t고양이\n"}}) {
        TempFolder temp;
        const auto source = temp.path / "dict";
        const auto other = temp.path / "other";
        std::error_code ec;
        HD_REQUIRE(std::filesystem::create_directory(source, ec));
        HD_REQUIRE(!ec);
        HD_REQUIRE(std::filesystem::create_directory(other, ec));
        HD_REQUIRE(!ec);
        const auto cache = temp.File("cache");
        ScopedEnvironment cacheEnv("HDTRANS_CACHE_DIR", cache.c_str());
        const std::vector<std::string> folders{temp.File("dict")};
        const std::vector<std::string> badFolders{temp.File("other")};
        HD_REQUIRE(WriteText(other / "base.tsv", invalid));
        hdTrans::Engine empty(false, hdTrans::CacheWrite::Off);
        HD_CHECK(!empty.Open(badFolders));
        HD_CHECK(!empty.loaded());
        HD_CHECK(empty.Dictionaries() == nullptr);
        HD_CHECK(!empty.Reload());
        HD_REQUIRE(WriteText(source / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
        hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
        HD_REQUIRE(engine.Open(folders));
        const auto snapshot = engine.Dictionaries();
        HD_REQUIRE(snapshot != nullptr && snapshot->HasDictionary());
        HD_REQUIRE(WriteText(source / "base.tsv", invalid));
        HD_CHECK(!engine.Reload());
        HD_CHECK(engine.loaded());
        HD_CHECK(engine.Dictionaries() == snapshot);
        HD_CHECK(!engine.Open(badFolders));
        HD_CHECK(engine.loaded());
        HD_CHECK(engine.Dictionaries() == snapshot);
        HD_CHECK_EQ(TargetOf(*snapshot, "猫"), "고양이");
    }
}

namespace {

// The target of the last plain entry of key, empty for none.
std::string PlainTarget(const hdTrans::dict::Snapshot& snap, std::string_view key) {
    std::string target;
    for (const auto& e : snap.Plain().Entries()) {
        if (e.key == key) target = e.target;
    }
    return target;
}

#ifndef _WIN32
// The number of open file descriptors of the process.
std::size_t OpenDescriptors() {
    std::size_t n = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it("/dev/fd", ec), end; !ec && it != end; it.increment(ec)) n++;
    return n;
}
#endif

}  // namespace

HD_TEST(SnapshotUserFile) {
    CacheTestEnvironment environment;
    TempFolder temp;
    std::error_code ec;
    std::filesystem::create_directory(temp.path / "dict", ec);
    HD_REQUIRE(!ec);
    HD_REQUIRE(WriteText(temp.path / "dict" / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    HD_REQUIRE(WriteText(temp.path / "dict" / "plain.tsv", "犬\t개\n"));
    const std::vector<std::string> folders{temp.File("dict")};
    const std::string user = temp.File("userdict.tsv");

    // A missing user dictionary file is none.
    const auto missing = hdTrans::dict::LoadFoldersTsv(folders, user);
    HD_REQUIRE(missing.snapshot != nullptr);
    HD_CHECK(missing.errors.empty());
    HD_CHECK_EQ(missing.snapshot->PlainFiles().size(), size_t{1});

    // A plain file, with or without the header, is the last plain dictionary, through
    // the cache too.
    for (const char* header : {"", "#! layer=user\n"}) {
        HD_REQUIRE(WriteText(Utf8Path(user), std::string(header) + "# comment\n犬\t멍멍이\n"));
        for (int pass = 0; pass < 3; pass++) {
            const auto loaded = pass == 0 ? hdTrans::dict::LoadFoldersTsv(folders, user)
                                          : hdTrans::dict::LoadFolders(folders, {}, false, true, user);
            HD_REQUIRE(loaded.snapshot != nullptr);
            HD_CHECK(loaded.errors.empty());
            if (pass == 2) HD_CHECK(loaded.snapshot->Mapped());
            HD_REQUIRE(loaded.snapshot->PlainFiles().size() == 2);
            HD_CHECK_EQ(loaded.snapshot->PlainFiles()[1].name, user);
            HD_CHECK_EQ(PlainTarget(*loaded.snapshot, "犬"), "멍멍이");
        }
    }

    // A column file joins the user dictionary and is read without the cache.
    for (const char* header : {"", "#! layer=user\n"}) {
        HD_REQUIRE(WriteText(Utf8Path(user), std::string(header) + "key\tko\tpos\tid\n猫\t냥이\tN110\t1\n"));
        const auto loaded = hdTrans::dict::LoadFolders(folders, {}, false, true, user);
        HD_REQUIRE(loaded.snapshot != nullptr);
        HD_CHECK(!loaded.snapshot->Mapped());
        HD_REQUIRE(loaded.snapshot->User() != nullptr);
        HD_CHECK_EQ(std::string(loaded.snapshot->User()->Records("猫")[0].target), "냥이");
        HD_CHECK_EQ(TargetOf(*loaded.snapshot, "猫"), "고양이");
    }

    // Another header fails the load, naming the file.
    for (const char* text : {"#! layer=base\n犬\t멍멍이\n", "#! layer=words\nkey\tko\tpos\tid\n猫\t냥이\tN110\t1\n"}) {
        HD_REQUIRE(WriteText(Utf8Path(user), text));
        const auto failed = hdTrans::dict::LoadFolders(folders, {}, false, true, user);
        HD_CHECK(failed.snapshot == nullptr);
        HD_REQUIRE(failed.errors.size() == 1);
        HD_CHECK(failed.errors[0].kind == hdTrans::dict::LoadErrorKind::Header);
        HD_CHECK_EQ(failed.errors[0].file, user);
    }
}

HD_TEST(SnapshotMultipleUserFiles) {
    CacheTestEnvironment environment;
    TempFolder temp;
    std::error_code error;
    std::filesystem::create_directory(temp.path / "dict", error);
    HD_REQUIRE(!error);
    HD_REQUIRE(WriteText(temp.path / "dict/base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    const std::vector<std::string> folders{temp.File("dict")};
    const std::vector<std::string> users{temp.File("first.txt"), temp.File("second.tsv")};
    HD_REQUIRE(WriteText(Utf8Path(users[0]), "犬\t개\tn\tprio=99\n"));
    HD_REQUIRE(WriteText(Utf8Path(users[1]), "犬\t멍멍이\tn\tprio=1\n"));
    for (int pass = 0; pass < 3; ++pass) {
        const auto loaded = pass == 0 ? hdTrans::dict::LoadFoldersTsv(folders, users)
                                      : hdTrans::dict::LoadFolders(folders, {}, false, true, users);
        HD_REQUIRE(loaded.snapshot);
        HD_CHECK(loaded.errors.empty());
        if (pass == 2) HD_CHECK(loaded.snapshot->Mapped());
        const auto entries = loaded.snapshot->Plain().Search("犬", hdTrans::dict::LookupMode::General);
        HD_REQUIRE(entries.size() == 1);
        HD_CHECK_EQ(entries.front()->target, "멍멍이");
    }
    const std::string header = "key\tko\tpos\tid\n";
    HD_REQUIRE(WriteText(Utf8Path(users[0]), header + "犬\t개\tN110\t1\n"));
    HD_REQUIRE(WriteText(Utf8Path(users[1]), header + "犬\t멍멍이\tN110\t1\n"));
    const auto columns = hdTrans::dict::LoadFolders(folders, {}, false, true, users);
    HD_REQUIRE(columns.snapshot && columns.snapshot->User());
    const auto records = columns.snapshot->User()->Records("犬");
    HD_REQUIRE(records.size() == 1);
    HD_CHECK_EQ(std::string(records.front().target), "멍멍이");
}

// A load leaves no source file open: the user dictionary can be rewritten and renamed.
HD_TEST(SnapshotUserFileStaysEditable) {
    CacheTestEnvironment environment;
    TempFolder temp;
    std::error_code ec;
    std::filesystem::create_directory(temp.path / "dict", ec);
    HD_REQUIRE(!ec);
    HD_REQUIRE(WriteText(temp.path / "dict" / "base.tsv", "key\tko\tpos\tid\n猫\t고양이\tN110\t1\n"));
    const std::vector<std::string> folders{temp.File("dict")};
    const std::string user = temp.File("userdict.tsv");
    HD_REQUIRE(WriteText(Utf8Path(user), "犬\t개\n"));
#ifndef _WIN32
    const std::size_t before = OpenDescriptors();
#endif
    const auto loaded = hdTrans::dict::LoadFolders(folders, {}, false, true, user);
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto cached = hdTrans::dict::LoadFolders(folders, {}, false, true, user);
    HD_REQUIRE(cached.snapshot != nullptr && cached.snapshot->Mapped());
#ifndef _WIN32
    HD_CHECK_EQ(OpenDescriptors(), before);
#endif
    HD_CHECK(WriteText(Utf8Path(user), "犬\t멍멍이\n"));
    std::filesystem::rename(Utf8Path(user), temp.path / "renamed.tsv", ec);
    HD_CHECK(!ec);
    std::filesystem::rename(temp.path / "renamed.tsv", Utf8Path(user), ec);
    HD_CHECK(!ec);
    std::filesystem::remove(Utf8Path(user), ec);
    HD_CHECK(!ec);
    HD_CHECK_EQ(PlainTarget(*cached.snapshot, "犬"), "개");
}

HD_TEST(SnapshotCompiledFile) {
    CacheTestEnvironment environment;
    TempFolder temp;
    const std::vector<std::string> folders = {FixtureFolder(), FixtureFolder() + "/over"};
    const auto file = temp.File("dicts.hdsnap");
    HD_REQUIRE(hdTrans::dict::CompileSnapshot(folders, file));
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    const std::vector<std::string> entries{file};

    // The snapshot contains both the columns and the plain files.
    const auto compiled = hdTrans::dict::LoadFolders(entries, temp.File("cache"));
    HD_REQUIRE(compiled.snapshot != nullptr && compiled.snapshot->Mapped() && compiled.snapshot->Healthy());
    HD_REQUIRE(compiled.errors.size() == editable.errors.size());
    for (size_t i = 0; i < compiled.errors.size(); i++) {
        const auto& a = compiled.errors[i];
        const auto& b = editable.errors[i];
        HD_CHECK(std::tie(a.kind, a.line, a.tsv, a.message) == std::tie(b.kind, b.line, b.tsv, b.message));
        HD_CHECK(Utf8Path(a.file).is_relative());
        HD_CHECK(Utf8Path(a.file).filename() == Utf8Path(b.file).filename());
    }
    CheckPlainCompilation(*compiled.snapshot, *editable.snapshot, true);
    CheckDictEncoding(compiled.snapshot->Base(), editable.snapshot->Base());
    HD_REQUIRE(compiled.snapshot->User() && compiled.snapshot->Names());
    CheckDictEncoding(*compiled.snapshot->User(), *editable.snapshot->User());
    CheckDictEncoding(*compiled.snapshot->Names(), *editable.snapshot->Names());
    HD_CHECK(!std::filesystem::exists(temp.path / "cache"));

    // A plain user dictionary file loads on top; a column one does not.
    HD_REQUIRE(WriteText(temp.path / "plain-user.tsv", "犬\t개\n"));
    const auto plainUser = hdTrans::dict::LoadFolders(entries, {}, false, false, temp.File("plain-user.tsv"));
    HD_REQUIRE(plainUser.snapshot != nullptr);
    HD_CHECK_EQ(PlainTarget(*plainUser.snapshot, "犬"), "개");
    HD_REQUIRE(WriteText(temp.path / "column-user.tsv", "key\tko\tpos\tid\n犬\t개\tN110\t1\n"));
    const auto columnUser = hdTrans::dict::LoadFolders(entries, {}, false, false, temp.File("column-user.tsv"));
    HD_CHECK(columnUser.snapshot == nullptr);
    HD_REQUIRE(columnUser.errors.size() == 1);
    HD_CHECK(columnUser.errors[0].kind == hdTrans::dict::LoadErrorKind::Header);

    // Explicit replacement folders cannot omit a source folder; the snapshot must exist.
    HD_CHECK(hdTrans::dict::LoadFolders({file, folders[0]}).snapshot == nullptr);
    const auto missing = hdTrans::dict::LoadFolders({temp.File("absent.hdsnap"), folders[0], folders[1]});
    HD_CHECK(missing.snapshot == nullptr);
    HD_REQUIRE(missing.errors.size() == 1);
    HD_CHECK(missing.errors[0].kind == hdTrans::dict::LoadErrorKind::Read && missing.errors[0].message.empty());

    // Another pointer size or compiler reads it; other engine data does not. The changed bytes go
    // to a copy: Windows does not let a file be rewritten while the snapshots above map it.
    const auto good = ReadBytes(file);
    const auto copy = temp.File("copy.hdsnap");
    const std::vector<std::string> copyEntries{copy};
    for (int variant = 0; variant < 4; variant++) {
        auto bytes = good;
        switch (variant) {
        case 0: SetField(bytes, 204, sizeof(void*) == 8 ? 4 : 8, 4); break;
        case 1: bytes[208] ^= std::byte{1}; break;
        case 2: bytes[128] ^= std::byte{1}; break;
        case 3: bytes[96] ^= std::byte{1}; break;
        }
        FixDirectories(bytes);
        HD_REQUIRE(WriteBytes(copy, bytes));
        const auto loaded = hdTrans::dict::LoadFolders(copyEntries);
        HD_CHECK_EQ(loaded.snapshot != nullptr, variant < 2);
        if (variant >= 2) HD_CHECK(!loaded.errors.empty() && loaded.errors[0].file == copy);
    }

    // A damaged compiled file fails the load and is left where it is.
    auto bytes = good;
    const size_t blob = FindSection(bytes, 17, 0);
    bytes[static_cast<size_t>(Field(bytes, blob + 8, 8))] ^= std::byte{1};
    HD_REQUIRE(WriteBytes(copy, bytes));
    const auto damaged = hdTrans::dict::LoadFolders(copyEntries);
    HD_CHECK(damaged.snapshot == nullptr);
    HD_CHECK(std::filesystem::exists(Utf8Path(copy)));
    HD_CHECK(!std::filesystem::exists(Utf8Path(copy + ".bad")));
}

HD_TEST(SnapshotCompiledStandaloneAfterSourceRemoval) {
    CacheTestEnvironment environment;
    TempFolder temp;
    std::error_code ec;
    for (const char* name : {"first", "second", "empty"}) {
        HD_REQUIRE(std::filesystem::create_directory(temp.path / name, ec) && !ec);
    }
    const std::vector<std::string> folders{temp.File("first"), temp.File("second"), temp.File("empty")};
    HD_REQUIRE(WriteText(temp.path / "first" / "base.tsv", "key\tko\tpos\tid\n愛\t사랑\tN110\t1\n犬\t개\tN110\t2\n謎\t수수께끼\tunknown-tag\t3\n"));
    HD_REQUIRE(WriteText(temp.path / "first" / "names.tsv", "#! layer=names\nkey\tko\tpos\tsem\tid\n難、名\t이름\tN130\tQPN\t1\n"));
    HD_REQUIRE(WriteText(temp.path / "first" / "a.tsv", "猫\t냥이\tn\n帰る\t돌아가다\tv5\n"));
    HD_REQUIRE(WriteText(temp.path / "first" / "kana.tsv", "#! layer=kana\nあい\t愛\t\n"));
    HD_REQUIRE(WriteText(temp.path / "first" / "readings.tsv", "#! layer=readings\n風音\t카자네\n"));
    HD_REQUIRE(WriteText(temp.path / "first" / "pre.tsv", "#! layer=prefilter\n猫\t犬\t1\t0\n"));
    HD_REQUIRE(WriteText(temp.path / "first" / "post.tsv", "#! layer=postfilter\n개\t멍멍이\t1\t0\n"));
    HD_REQUIRE(WriteText(temp.path / "second" / "a.tsv", "猫\t야옹이\tn\n称える\t일컫다\tv1:ㄷ\n"));
    HD_REQUIRE(WriteText(temp.path / "second" / "readings.tsv", "#! layer=readings\n風音\t카논\n"));
    const auto editable = hdTrans::dict::LoadFoldersTsv(folders);
    HD_REQUIRE(editable.snapshot != nullptr);
    HD_REQUIRE(editable.errors.size() == 1 && editable.snapshot->Warnings().size() == 1);
    const auto path = temp.File("DiCtS.HdSnAp");
    HD_REQUIRE(hdTrans::dict::CompileSnapshot(folders, path));
    for (const auto& folder : folders) {
        HD_REQUIRE(std::filesystem::remove_all(Utf8Path(folder), ec) > 0 && !ec);
    }
    const auto loaded = hdTrans::dict::LoadFolders({path});
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->Mapped() && loaded.snapshot->Healthy());
    CheckPlainCompilation(*loaded.snapshot, *editable.snapshot, true);
    HD_REQUIRE(loaded.errors.size() == 1);
    HD_CHECK_EQ(loaded.errors[0].file, (std::filesystem::path("dicts") / "0" / "base.tsv").string());
    HD_CHECK(std::tie(loaded.errors[0].kind, loaded.errors[0].line, loaded.errors[0].tsv, loaded.errors[0].message) ==
             std::tie(editable.errors[0].kind, editable.errors[0].line, editable.errors[0].tsv, editable.errors[0].message));
    CheckDictEncoding(loaded.snapshot->Base(), editable.snapshot->Base());
    const auto cat = loaded.snapshot->Plain().Search("猫", hdTrans::dict::LookupMode::General);
    HD_REQUIRE(cat.size() == 1);
    HD_CHECK_EQ(cat[0]->target, "야옹이");
    const auto kana = loaded.snapshot->TextPlain().Search("あい", hdTrans::dict::LookupMode::General);
    HD_REQUIRE(kana.size() == 1);
    HD_CHECK(kana[0]->kana);
    HD_CHECK_EQ(kana[0]->target, "사랑");
    const std::vector<std::string> lines{"風音「こんにちは」", "風音「ありがとう」"};
    const auto document = hdTrans::pipeline::BuildDocument(*loaded.snapshot, lines);
    HD_REQUIRE(document != nullptr && document->registry != nullptr);
    HD_CHECK_EQ(document->registry->Render("風音"), "카논");
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({path}));
    hdTrans::Session session(engine);
    HD_CHECK_EQ(session.Translate("猫"), "멍멍이");

    HD_REQUIRE(WriteText(temp.path / "user.tsv", "鳥\t새\n"));
    const auto user = hdTrans::dict::LoadFolders({path}, {}, false, false, temp.File("user.tsv"));
    HD_REQUIRE(user.snapshot != nullptr && !user.snapshot->PlainFiles().empty());
    HD_CHECK(user.snapshot->PlainFiles().back().user);
    HD_CHECK_EQ(user.snapshot->PlainFiles().back().folder, 3);
    HD_CHECK_EQ(PlainTarget(*user.snapshot, "鳥"), "새");
    HD_CHECK_EQ(user.snapshot->PlainFiles().size(), editable.snapshot->PlainFiles().size() + 1);
}

HD_TEST(SnapshotCompiledCurrentPlainFoldersReplaceEmbedded) {
    CacheTestEnvironment environment;
    TempFolder temp;
    std::error_code ec;
    HD_REQUIRE(std::filesystem::create_directory(temp.path / "source", ec) && !ec);
    const auto source = temp.File("source");
    HD_REQUIRE(WriteText(temp.path / "source" / "base.tsv", "key\tko\tpos\tid\n愛\t사랑\tN110\t1\n"));
    HD_REQUIRE(WriteText(temp.path / "source" / "plain.tsv", "猫\t고양이\n犬\t개\n"));
    const auto path = temp.File("dicts.hdsnap");
    HD_REQUIRE(hdTrans::dict::CompileSnapshot({source}, path));
    HD_REQUIRE(WriteText(temp.path / "source" / "plain.tsv", "猫\t냥이\n"));
    const auto current = hdTrans::dict::LoadFoldersTsv({source});
    const auto legacy = hdTrans::dict::LoadFolders({path, source});
    HD_REQUIRE(current.snapshot != nullptr && legacy.snapshot != nullptr);
    CheckPlainCompilation(*legacy.snapshot, *current.snapshot);
    HD_CHECK_EQ(PlainTarget(*legacy.snapshot, "猫"), "냥이");
    HD_CHECK(PlainTarget(*legacy.snapshot, "犬").empty());
    const auto standalone = hdTrans::dict::LoadFolders({path});
    HD_REQUIRE(standalone.snapshot != nullptr);
    HD_CHECK_EQ(PlainTarget(*standalone.snapshot, "猫"), "고양이");
    HD_CHECK_EQ(PlainTarget(*standalone.snapshot, "犬"), "개");
}

HD_TEST(SnapshotCompiledMalformedPlainSection) {
    CacheTestEnvironment environment;
    TempFolder temp;
    std::error_code ec;
    HD_REQUIRE(std::filesystem::create_directory(temp.path / "source", ec) && !ec);
    const auto source = temp.File("source");
    HD_REQUIRE(WriteText(temp.path / "source" / "base.tsv", "key\tko\tpos\tid\n愛\t사랑\tN110\t1\n"));
    HD_REQUIRE(WriteText(temp.path / "source" / "plain.tsv", "猫\t냥이\n"));
    const auto path = temp.File("dicts.hdsnap");
    HD_REQUIRE(hdTrans::dict::CompileSnapshot({source}, path));
    const auto good = ReadBytes(path);
    const auto plain = FindSection(good, 4, 3);
    HD_REQUIRE(plain < good.size());
    const auto start = static_cast<size_t>(Field(good, plain + 8, 8));
    const auto name = start + 16;
    const auto nameLength = static_cast<size_t>(Field(good, start + 12, 4));
    const auto layer = name + nameLength;
    const auto copy = temp.File("bad.HdSnAp");
    for (int variant = 0; variant < 16; variant++) {
        auto bytes = good;
        switch (variant) {
        case 0: SetField(bytes, start, 0, 4); break;
        case 1: SetField(bytes, start, UINT32_MAX, 4); break;
        case 2: SetField(bytes, start + 4, 0, 4); break;
        case 3: SetField(bytes, start + 4, UINT32_MAX, 4); break;
        case 4: SetField(bytes, start + 8, 1, 4); break;
        case 5: SetField(bytes, start + 8, UINT32_MAX, 4); break;
        case 6: SetField(bytes, start + 12, UINT32_MAX, 4); break;
        case 7: bytes[name] = std::byte{'/'}; break;
        case 8: bytes[name] = std::byte{'\\'}; break;
        case 9: bytes[name] = std::byte{'.'}; bytes[name + 1] = std::byte{'.'}; bytes[name + 2] = std::byte{'/'}; break;
        case 10: bytes[name + 1] = std::byte{':'}; break;
        case 11: bytes[name] = std::byte{0}; break;
        case 12: SetField(bytes, layer, static_cast<unsigned>(hdTrans::dict::FileLayer::Words), 4); break;
        case 13: SetField(bytes, layer + 4, UINT32_MAX, 4); break;
        case 14: bytes[layer + 8] ^= std::byte{1}; break;
        case 15: SetField(bytes, layer, static_cast<unsigned>(hdTrans::dict::FileLayer::Kana), 4); break;
        }
        const auto first = static_cast<size_t>(Field(bytes, plain + 40, 4));
        const auto segments = static_cast<size_t>(Field(bytes, plain + 44, 4));
        const auto segmentDirectory = static_cast<size_t>(Field(bytes, 48, 8));
        for (size_t i = first; i < first + segments; i++) {
            const auto segment = segmentDirectory + 64 * i;
            const auto offset = start + static_cast<size_t>(Field(bytes, segment + 8, 8));
            const auto length = static_cast<size_t>(Field(bytes, segment + 16, 8));
            SetField(bytes, segment + 24, hdTrans::dict::internal::Crc32c(std::span(bytes).subspan(offset, length)), 4);
        }
        FixDirectories(bytes);
        HD_REQUIRE(WriteBytes(copy, bytes));
        const auto damaged = hdTrans::dict::LoadFolders({copy, source});
        HD_CHECK(damaged.snapshot == nullptr);
        HD_REQUIRE(damaged.errors.size() == 1);
        HD_CHECK(damaged.errors[0].kind == hdTrans::dict::LoadErrorKind::Read);
        HD_CHECK(std::filesystem::exists(Utf8Path(copy)));
        HD_CHECK(!std::filesystem::exists(Utf8Path(copy + ".bad")));
    }
}


HD_TEST(SnapshotCompiledPlainContentIdentity) {
    CacheTestEnvironment environment;
    TempFolder temp;
    std::error_code ec;
    HD_REQUIRE(std::filesystem::create_directory(temp.path / "source", ec) && !ec);
    const auto source = temp.File("source");
    HD_REQUIRE(WriteText(temp.path / "source" / "base.tsv", "key\tko\tpos\tid\n愛\t사랑\tN110\t1\n"));
    HD_REQUIRE(WriteText(temp.path / "source" / "plain.tsv", "猫\t냥이\n"));
    HD_REQUIRE(hdTrans::dict::CompileSnapshot({source}, temp.File("before.hdsnap")));
    const auto before = ReadBytes(temp.File("before.hdsnap"));
    const auto stamp = std::filesystem::last_write_time(temp.path / "source" / "plain.tsv", ec);
    HD_REQUIRE(!ec);
    HD_REQUIRE(WriteText(temp.path / "source" / "plain.tsv", "猫\t야옹\n"));
    std::filesystem::last_write_time(temp.path / "source" / "plain.tsv", stamp, ec);
    HD_REQUIRE(!ec);
    HD_REQUIRE(hdTrans::dict::CompileSnapshot({source}, temp.File("after.hdsnap")));
    const auto after = ReadBytes(temp.File("after.hdsnap"));
    HD_REQUIRE(before.size() >= 256 && after.size() >= 256);
    HD_CHECK(!std::equal(before.begin() + 64, before.begin() + 96, after.begin() + 64));
    const auto loaded = hdTrans::dict::LoadFolders({temp.File("after.hdsnap")});
    HD_REQUIRE(loaded.snapshot != nullptr);
    HD_CHECK_EQ(PlainTarget(*loaded.snapshot, "猫"), "야옹");
}
