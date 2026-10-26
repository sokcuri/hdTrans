#include "gui/text_file.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include "hdtest.h"

namespace {

using hdTrans::gui::DecodeText;
using hdTrans::gui::ReadTextFile;
using hdTrans::gui::TextEncoding;
using hdTrans::gui::TranslatedPath;
using hdTrans::gui::WriteTextFile;
using namespace std::string_view_literals;

struct TempFolder {
    std::filesystem::path path;

    TempFolder() {
        std::error_code error;
        const auto base = std::filesystem::temp_directory_path(error);
        if (error) return;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            const auto candidate = base / ("hdtrans-text-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate, error)) {
                path = candidate;
                return;
            }
            if (error) return;
        }
    }

    ~TempFolder() {
        if (path.empty()) return;
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

bool WriteBytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return static_cast<bool>(file);
}

std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

HD_TEST(GuiTextFileEncodingDetection) {
    struct Case {
        std::string_view bytes;
        std::string_view expected;
        TextEncoding encoding;
    };
    const Case cases[] = {
        {"", "", TextEncoding::Utf8},
        {"ASCII", "ASCII", TextEncoding::Utf8},
        {"한국어 日本語 😀", "한국어 日本語 😀", TextEncoding::Utf8},
        {"\xC2\xA5", "¥", TextEncoding::Utf8},
        {"\xEF\xBB\xBF", "", TextEncoding::Utf8Bom},
        {"\xEF\xBB\xBF한국어 日本語", "한국어 日本語", TextEncoding::Utf8Bom},
        {"\xFF\xFE", "", TextEncoding::Utf16Le},
        {"\xFF\xFE\x5C\xD5\x6D\xAD\xB4\xC5"sv, "한국어", TextEncoding::Utf16Le},
        {"\xFF\xFE\x3D\xD8\x00\xDE"sv, "😀", TextEncoding::Utf16Le},
        {"\xFF\xFE\x00\xD8\x00\xDC\xFF\xDB\xFF\xDF"sv,
         "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF", TextEncoding::Utf16Le},
        {"\x93\xFA\x96\x7B\x8C\xEA", "日本語", TextEncoding::ShiftJis},
        {"\x82\xB1\x82\xF1\x82\xC9\x82\xBF\x82\xCD", "こんにちは", TextEncoding::ShiftJis},
        {"\xB6\xC0\xB6\xC5", "ｶﾀｶﾅ", TextEncoding::ShiftJis},
        {"\x87\x40", "①", TextEncoding::ShiftJis},
    };
    for (const auto& test : cases) {
        std::string text = "previous";
        auto encoding = TextEncoding::Utf16Le;
        HD_REQUIRE(DecodeText(test.bytes, text, encoding));
        HD_CHECK(text == test.expected);
        HD_CHECK(encoding == test.encoding);
    }
}

HD_TEST(GuiTextFileStrictUtf8) {
    const std::string_view valid[] = {
        "\x7F", "\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xED\x9F\xBF",
        "\xEE\x80\x80", "\xEF\xBF\xBF", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF",
    };
    for (const auto bytes : valid) {
        std::string text;
        auto encoding = TextEncoding::ShiftJis;
        HD_REQUIRE(DecodeText(bytes, text, encoding));
        HD_CHECK(text == bytes);
        HD_CHECK(encoding == TextEncoding::Utf8);
    }
    const std::string_view invalid[] = {
        "\x00"sv, "\x80", "\xBF", "\xC0\x80", "\xC1\xBF", "\xC2", "\xC2x",
        "\xE0\x80\x80", "\xED\xA0\x80", "\xED\xBF\xBF", "\xE3", "\xE3\x81",
        "\xF0\x80\x80\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80",
        "\xF0", "\xF0\x9F", "\xF0\x9F\x98", "\xF0\x9Fx\x80", "\xFF",
    };
    for (const auto bytes : invalid) {
        const std::string bomBytes = std::string("\xEF\xBB\xBF") + std::string(bytes);
        std::string text = "unchanged";
        auto encoding = TextEncoding::Utf16Le;
        HD_CHECK(!DecodeText(bomBytes, text, encoding));
        HD_CHECK_EQ(text, "unchanged");
        HD_CHECK(encoding == TextEncoding::Utf16Le);
    }
}

HD_TEST(GuiTextFileRejectMalformedInput) {
    const std::string_view cases[] = {
        "\xEF", "\xEF\xBB", "\xEF\xBB\xBF\x93\xFA", "\xFE\xFF\x00\x41"sv,
        "\xFF\xFE\x41", "\xFF\xFE\x00\xD8"sv, "\xFF\xFE\x00\xDC"sv,
        "\xFF\xFE\x00\xD8\x41\x00"sv, "\xFF\xFE\x00\xD8\x00\xD8"sv,
        "\xFF\xFE\x00\xD8\x00"sv, "\xFF\xFE\x00\x00"sv,
        "\xFF\xFE\x00\x00\x41\x00\x00\x00"sv, "\x00\x00\xFE\xFF"sv,
        "a\x00z"sv, "\x81", "\x82\x20", "\x82\x7F", "\x82\xFD", "\xFF", "\x80",
        "\x93\xFA\x00"sv,
    };
    for (const auto bytes : cases) {
        std::string text = "unchanged";
        auto encoding = TextEncoding::Utf8;
        HD_CHECK(!DecodeText(bytes, text, encoding));
        HD_CHECK_EQ(text, "unchanged");
        HD_CHECK(encoding == TextEncoding::Utf8);
    }
}

HD_TEST(GuiTextFileNormalizeNewlines) {
    const std::string_view cases[] = {
        "a\r\nb\rc\n\r\r\n",
        "\xEF\xBB\xBF" "a\r\nb\rc\n\r\r\n",
        "\xFF\xFE" "a\0\r\0\n\0b\0\r\0c\0\n\0\r\0\r\0\n\0"sv,
    };
    for (const auto bytes : cases) {
        std::string text;
        auto encoding = TextEncoding::Utf8;
        HD_REQUIRE(DecodeText(bytes, text, encoding));
        HD_CHECK_EQ(text, "a\nb\nc\n\n\n");
    }
    std::string text;
    auto encoding = TextEncoding::Utf8;
    HD_REQUIRE(DecodeText("\x93\xFA\r\n\x96\x7B\r\x8C\xEA\n", text, encoding));
    HD_CHECK_EQ(text, "日\n本\n語\n");
    HD_CHECK(encoding == TextEncoding::ShiftJis);
}

HD_TEST(GuiTextFileKeepLineBreak) {
    struct Case {
        std::string_view bytes;
        std::string_view lineBreak;
        std::string_view written;
    };
    const Case cases[] = {
        {"a\r\nb\nc\r", "\r\n", "\xEF\xBB\xBF" "a\r\nb\r\nc\r\n"},
        {"a\nb\r\n", "\n", "\xEF\xBB\xBF" "a\nb\n"},
        {"a\rb\r\n", "\r", "\xEF\xBB\xBF" "a\rb\r"},
        {"a", "\n", "\xEF\xBB\xBF" "a"},
    };
    TempFolder temporary;
    HD_REQUIRE(!temporary.path.empty());
    const auto path = temporary.path / "output.txt";
    for (const auto& test : cases) {
        std::string text;
        std::string_view lineBreak;
        auto encoding = TextEncoding::Utf8;
        HD_REQUIRE(DecodeText(test.bytes, text, encoding, &lineBreak));
        HD_CHECK(lineBreak == test.lineBreak);
        HD_REQUIRE(WriteTextFile(path, text, lineBreak));
        HD_CHECK_EQ(ReadBytes(path), std::string(test.written));
    }
}

HD_TEST(GuiTextFileLongShiftJis) {
    std::string bytes = "a";
    std::string expected = "a";
    for (int i = 0; i < 12000; ++i) {
        bytes += "\x93\xFA\x96\x7B\x8C\xEA\r\n";
        expected += "日本語\n";
    }
    std::string text;
    auto encoding = TextEncoding::Utf8;
    HD_REQUIRE(DecodeText(bytes, text, encoding));
    HD_CHECK_EQ(text, expected);
    HD_CHECK(encoding == TextEncoding::ShiftJis);
}

HD_TEST(GuiTextFileReadWriteNonAsciiPath) {
    TempFolder temporary;
    HD_REQUIRE(!temporary.path.empty());
    const auto folder = temporary.path / std::filesystem::path(u8"한글 日本語");
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(folder, error));
    const auto path = folder / std::filesystem::path(u8"번역 😀.txt");
    std::string expected;
    for (int i = 0; i < 2000; ++i) expected += "한국어 日本語 😀\n";
    HD_REQUIRE(WriteTextFile(path, expected));
    HD_CHECK_EQ(ReadBytes(path), std::string("\xEF\xBB\xBF") + expected);
    std::string text;
    auto encoding = TextEncoding::Utf8;
    HD_REQUIRE(ReadTextFile(path, text, encoding));
    HD_CHECK_EQ(text, expected);
    HD_CHECK(encoding == TextEncoding::Utf8Bom);
    HD_REQUIRE(WriteTextFile(path, "a\r\nb\rc\n"));
    HD_CHECK_EQ(ReadBytes(path), "\xEF\xBB\xBF" "a\r\nb\rc\n");
    HD_REQUIRE(ReadTextFile(path, text, encoding));
    HD_CHECK_EQ(text, "a\nb\nc\n");
    HD_REQUIRE(WriteTextFile(path, ""));
    HD_CHECK_EQ(ReadBytes(path), "\xEF\xBB\xBF");
    HD_REQUIRE(ReadTextFile(path, text, encoding));
    HD_CHECK(text.empty());
    HD_CHECK(encoding == TextEncoding::Utf8Bom);
}

HD_TEST(GuiTextFileReadOtherEncodings) {
    TempFolder temporary;
    HD_REQUIRE(!temporary.path.empty());
    const auto path = temporary.path / "input.txt";
    struct Case {
        std::string_view bytes;
        std::string_view expected;
        TextEncoding encoding;
    };
    const Case cases[] = {
        {"", "", TextEncoding::Utf8},
        {"한국어\r\n", "한국어\n", TextEncoding::Utf8},
        {"\x93\xFA\x96\x7B\x8C\xEA\r\n", "日本語\n", TextEncoding::ShiftJis},
        {"\xFF\xFE\x3D\xD8\x00\xDE\r\0\n\0"sv, "😀\n", TextEncoding::Utf16Le},
    };
    for (const auto& test : cases) {
        HD_REQUIRE(WriteBytes(path, test.bytes));
        std::string text;
        auto encoding = TextEncoding::Utf8Bom;
        HD_REQUIRE(ReadTextFile(path, text, encoding));
        HD_CHECK(text == test.expected);
        HD_CHECK(encoding == test.encoding);
    }
}

HD_TEST(GuiTextFileIoFailures) {
    TempFolder temporary;
    HD_REQUIRE(!temporary.path.empty());
    const auto path = temporary.path / "input.txt";
    std::string text = "unchanged";
    auto encoding = TextEncoding::ShiftJis;
    HD_CHECK(!ReadTextFile(path, text, encoding));
    HD_CHECK(!WriteTextFile(temporary.path / "missing" / "file.txt", "text"));
    HD_CHECK(!WriteTextFile(temporary.path, "text"));
    HD_REQUIRE(WriteBytes(path, "\xEF\xBB\xBF\xC0\xAF"));
    HD_CHECK(!ReadTextFile(path, text, encoding));
    HD_CHECK_EQ(text, "unchanged");
    HD_CHECK(encoding == TextEncoding::ShiftJis);
    HD_REQUIRE(WriteTextFile(path, "keep"));
    HD_CHECK(!WriteTextFile(path, "\xC0\xAF"));
    HD_CHECK(!WriteTextFile(path, "a\0b"sv));
    HD_CHECK_EQ(ReadBytes(path), "\xEF\xBB\xBFkeep");
}

HD_TEST(GuiTextFileTranslatedPath) {
    namespace fs = std::filesystem;
    const fs::path folder = fs::path(u8"문서");
    const fs::path other = fs::path(u8"출력");
    HD_CHECK(TranslatedPath(folder / u8"원문.txt", {}) == folder / u8"원문_번역.txt");
    HD_CHECK(TranslatedPath(folder / u8"원문.txt", other) == other / u8"원문_번역.txt");
    HD_CHECK(TranslatedPath(folder / "a.b.TXT", {}) == folder / u8"a.b_번역.txt");
    HD_CHECK(TranslatedPath(folder / "notes", {}) == folder / u8"notes_번역.txt");
    HD_CHECK(TranslatedPath(folder / u8"원문_번역.txt", {}) == folder / u8"원문_번역_번역.txt");
}
