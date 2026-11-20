#include <cstdint>
#include "core/text/utf8.h"
#include "core/text/lines.h"

#include "hdtest.h"

using hdTrans::text::AppendRune;
using hdTrans::text::DecodeRune;
using hdTrans::text::Find;
using hdTrans::text::kReplacement;
using hdTrans::text::RuneCount;

HD_TEST(utf8_decode_round_trip) {
    struct Case {
        std::string_view in;
        char32_t cp;
        size_t size;
    };
    const Case cases[] = {
        {"a", 'a', 1},
        {"\xC3\xA9", 0xE9, 2},          // é
        {"\xEA\xB0\x80", 0xAC00, 3},    // 가
        {"\xE3\x81\x82", 0x3042, 3},    // あ
        {"\xF0\x9F\x98\x80", 0x1F600, 4},
    };
    for (const auto& c : cases) {
        const auto r = DecodeRune(c.in);
        HD_CHECK_EQ(static_cast<uint32_t>(r.cp), static_cast<uint32_t>(c.cp));
        HD_CHECK_EQ(r.size, c.size);
        std::string out;
        AppendRune(out, c.cp);
        HD_CHECK_EQ(out, std::string(c.in));
    }
}

HD_TEST(utf8_decode_invalid_consumes_one_byte) {
    const std::string_view cases[] = {
        "\x80",              // stray continuation
        "\xC3",              // truncated
        "\xE3\x81",          // truncated
        "\xC0\x80",          // overlong
        "\xED\xA0\x80",      // surrogate
        "\xF4\x90\x80\x80",  // > U+10FFFF
        "\xFF",
    };
    for (const auto& c : cases) {
        const auto r = DecodeRune(c);
        HD_CHECK_EQ(static_cast<uint32_t>(r.cp), static_cast<uint32_t>(kReplacement));
        HD_CHECK_EQ(r.size, size_t{1});
    }
    HD_CHECK_EQ(DecodeRune("").size, size_t{0});
}

HD_TEST(utf8_rune_count) {
    HD_CHECK_EQ(RuneCount(""), size_t{0});
    HD_CHECK_EQ(RuneCount("abc"), size_t{3});
    HD_CHECK_EQ(RuneCount("\xE7\x8C\xAB\xE3\x81\x8C\xE5\xA5\xBD\xE3\x81\x8D"), size_t{4});  // 猫が好き
    HD_CHECK_EQ(RuneCount("a\x80z"), size_t{3});
}

HD_TEST(utf8_find_matches_string_find) {
    struct Case {
        std::string_view s;
        std::string_view needle;
    };
    const Case cases[] = {
        {"そうやろ、ちゃうやろ", "やろ"}, {"そうやろ、ちゃうやろ", "ちゃう"}, {"そうやろ", "やろか"}, {"やや", "や"},
        {"ああああ", "ああ"},         {"abcab", "ab"},             {"", "あ"},       {"あ", ""},
        {"", ""},                     {"猫が好き", "好き"},         {"猫が好き", "猫が好きだ"}, {"a\x80z", "\x80"},
    };
    for (const auto& c : cases) {
        for (size_t from = 0; from <= c.s.size() + 1; from++) HD_CHECK_EQ(Find(c.s, c.needle, from), c.s.find(c.needle, from));
    }
}

HD_TEST(text_line_pieces) {
    using namespace hdTrans::text;
    for (std::size_t bytes : {std::size_t{0}, std::size_t{1}, kMaxLineBytes}) {
        const std::string line(bytes, 'a');
        const auto pieces = TranslationPieces(line);
        HD_CHECK_EQ(pieces.size(), std::size_t{1});
        HD_CHECK_EQ(std::string(pieces.front()), line);
    }
    for (const std::string_view boundary : {"。", "！", "？", "\r", "\v", "\f", "\xC2\x85", "\xE2\x80\xA8", "\xE2\x80\xA9", " "}) {
        const std::string line = std::string(100, 'a') + std::string(boundary) + std::string(70000, 'b');
        const auto pieces = TranslationPieces(line);
        HD_CHECK_EQ(pieces.front().size(), std::size_t{100} + boundary.size());
        std::string joined;
        for (const auto piece : pieces) {
            HD_CHECK(piece.size() <= kMaxLineBytes);
            joined += piece;
        }
        HD_CHECK_EQ(joined, line);
    }
    std::string line;
    while (line.size() < 100000) line += "あ😀";
    std::string joined;
    for (const auto piece : TranslationPieces(line)) {
        HD_CHECK(piece.size() <= kMaxLineBytes);
        for (std::size_t i = 0; i < piece.size();) {
            const auto rune = DecodeRune(piece.substr(i));
            HD_CHECK(rune.cp != kReplacement);
            i += rune.size;
        }
        joined += piece;
    }
    HD_CHECK_EQ(joined, line);
    const auto lines = Lines("\nline\r\n");
    HD_CHECK_EQ(lines.size(), std::size_t{3});
    HD_CHECK(lines.front().empty());
    HD_CHECK_EQ(std::string(lines[1]), "line\r");
    HD_CHECK(lines.back().empty());
}

HD_TEST(text_line_pieces_brackets) {
    using namespace hdTrans::text;
    for (const std::string_view unit : {"「今日は（いい！）天気です。明日も晴れる。」", "『【あ！】（い？）《う。》え。』", "\"alpha? (beta!) gamma.\"", "\"alpha \\\"?beta\\\" (gamma!).\""}) {
        std::string line;
        while (line.size() < 100000) line += unit;
        std::string joined;
        for (const auto piece : TranslationPieces(line)) {
            HD_CHECK(piece.size() <= 16384);
            HD_CHECK_EQ(piece.size() % unit.size(), std::size_t{0});
            joined += piece;
        }
        HD_CHECK_EQ(joined, line);
    }
    std::string inside = "外。「";
    while (inside.size() < 70000) inside += "あ。";
    inside += "」";
    HD_CHECK_EQ(std::string(TranslationPieces(inside).front()), "外。");
    const std::string spaced = "外。  " + std::string(70000, 'a');
    HD_CHECK_EQ(std::string(TranslationPieces(spaced).front()), "外。");
    const std::string unclosed = "「" + std::string(70000, 'a');
    std::string joined;
    for (const auto piece : TranslationPieces(unclosed)) {
        HD_CHECK(piece.size() <= 16384);
        joined += piece;
    }
    HD_CHECK_EQ(joined, unclosed);
}
