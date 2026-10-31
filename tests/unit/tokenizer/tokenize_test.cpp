#include "core/tokenizer/tokenize.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hdtest.h"
#include "stage_records.h"

using namespace hdTrans::tokenizer;
using namespace std::literals;

namespace {

// Tokens as scanned: "type/span:text" separated by spaces, texts escaped as in stage
// records.
std::string Scanned(const Tokens& t) {
    std::string s;
    for (size_t i = 0; i < t.size(); i++) {
        if (i > 0) s += ' ';
        s += std::to_string(t[i].type) + "/" + std::to_string(t[i].span) + ":" + stagetest::EscapeStageText(t.Raw(i));
    }
    return s;
}

// Tokens after the per-token stage: "type:canonical|merged".
std::string Prepared(const Tokens& t) {
    std::string s;
    for (size_t i = 0; i < t.size(); i++) {
        if (i > 0) s += ' ';
        s += std::to_string(t.TypeAfter(i)) + ":" + stagetest::EscapeStageText(t.Canonical(i)) + "|" +
             stagetest::EscapeStageText(t.Merged(i));
    }
    return s;
}

std::string Markers(const Tokens& t) {
    std::string s;
    for (const ControlMarker& m : t.markers) {
        if (!s.empty()) s += ' ';
        s += std::to_string(m.tokenIndex) + "/" + std::to_string(m.textLength) + "/" + std::to_string(m.markerKind);
    }
    return s;
}

std::string ScanText(std::string_view input, bool dashes) {
    std::vector<Piece> pieces;
    if (!input.empty()) pieces.push_back({std::string(input), 0});
    Tokens t;
    TokenizeSpans(pieces, dashes, t);
    return Scanned(t);
}

struct ScanCase {
    std::string_view input;
    const char* plain;   // without dashes
    const char* dashes;  // with dashes; nullptr when equal to plain
};

// Dashes, utterance-closing 」, invalid UTF-8, control characters, ASCII runs, mixed
// scripts, half-width forms, markup, spaces, ・ and sentence ends.
const ScanCase kScanCases[] = {
    {"あんたが――"sv, "24/0:あんたがーー 9/0:",
     "24/0:あんたが 22/0:―― 9/0:"},
    {"あんたが――。"sv, "24/0:あんたがーー 17/0:。 9/0:",
     "24/0:あんたが 22/0:―― 17/0:。 9/0:"},
    {"ロンド－("sv, "24/0:ロンドー( 9/0:",
     "24/0:ロンド 22/0:－ 24/0:( 9/0:"},
    {"シャ－カ－ン"sv, "24/0:シャーカーン 9/0:", nullptr},
    {"亜－ー"sv, "24/0:亜ーー 9/0:", nullptr},
    {"――あ"sv, "22/0:―― 24/0:あ 9/0:", nullptr},
    {"「黙れ」"sv, "24/0:「黙れ」 9/0:",
     "24/0:「黙れ 18/0:」 9/0:"},
    {"「疲れてしまって」"sv, "24/0:「疲れてしまって」 9/0:", nullptr},
    {"「黙れ」「はい」"sv, "24/0:「黙れ」「はい」 9/0:",
     "24/0:「黙れ 18/0:」 24/0:「はい 18/0:」 9/0:"},
    {"「あ」\012い"sv, "24/0:「あ」 6/0:\\n 24/0:い 9/0:",
     "24/0:「あ 18/0:」 6/0:\\n 24/0:い 9/0:"},
    {"「あ」　い"sv, "24/0:「あ」い 9/0:",
     "24/0:「あ 18/0:」 26/0:　 24/0:い 9/0:"},
    {"「あ」い"sv, "24/0:「あ」い 9/0:", nullptr},
    {"」"sv, "24/0:」 9/0:", nullptr},
    {"a\377b"sv, "2/0:a 22/0:� 2/0:b 9/0:", nullptr},
    {"あ\343\201い"sv, "24/0:あ 22/0:�� 24/0:い 9/0:", nullptr},
    {"\355\240\200"sv, "22/0:��� 9/0:", nullptr},
    {"\300\257あ"sv, "22/0:�� 24/0:あ 9/0:", nullptr},
    {"あ\360\237\230"sv, "24/0:あ 22/0:��� 9/0:", nullptr},
    {"\377\376"sv, "22/0:�� 9/0:", nullptr},
    {"あ\007い"sv, "24/0:あい 9/0:", nullptr},
    {"\033ABC"sv, "7/0:  2/0:ABC 9/0:", nullptr},
    {"a\177b"sv, "2/0:a\\x7fb 9/0:", nullptr},
    {"あ\011い"sv, "24/0:あい 9/0:", nullptr},
    {"あ\013い"sv, "24/0:あい 9/0:", nullptr},
    {"\000"sv, "", nullptr},
    {"あ\000い"sv, "24/0:あ 9/0:", nullptr},
    {"\015\012\015\012"sv, "6/0:\\r\\n\\r\\n 9/0:", nullptr},
    {"あ\014い"sv, "24/0:あい 9/0:", nullptr},
    {"あ\001"sv, "24/0:あ 7/0:  9/0:", nullptr},
    {"C:\\Windows\\System32\\drivers\\etc\\hosts"sv, "2/0:C:\\\\Windows\\\\System32\\\\drivers\\\\etc\\\\hosts 9/0:", nullptr},
    {"abc123def456!!!"sv, "2/0:abc123def456 3/0:!!! 9/0:", nullptr},
    {"1,000,000円"sv, "24/0:1,000,000円 9/0:", nullptr},
    {"4,867/4,867"sv, "8/0:4,867/4,867 9/0:", nullptr},
    {"HP：4,867/4,867"sv, "2/0:HP 22/0:： 8/0:4,867/4,867 9/0:", nullptr},
    {"a4,867"sv, "2/0:a4,867 9/0:", nullptr},
    {"4，867"sv, "1/0:4,867 9/0:", nullptr},
    {"１，２３４"sv, "1/0:1,234 9/0:", nullptr},
    {"3.14159"sv, "2/0:3.14159 9/0:", nullptr},
    {"4.5/4.5"sv, "2/0:4.5/4.5 9/0:", nullptr},
    {"４．５"sv, "2/0:4.5 9/0:", nullptr},
    {"Lv.55"sv, "2/0:Lv.55 9/0:", nullptr},
    {"Ｌｖ．２"sv, "2/0:Lv.2 9/0:", nullptr},
    {"ver1.2.3"sv, "2/0:ver1.2.3 9/0:", nullptr},
    {"a.b.c"sv, "2/0:a.b.c 9/0:", nullptr},
    {"100%OFF"sv, "2/0:100%OFF 9/0:", nullptr},
    {"x-1"sv, "2/0:x-1 9/0:", nullptr},
    {"#1"sv, "8/0:#1 9/0:", nullptr},
    {"$100"sv, "8/0:$100 9/0:", nullptr},
    {"@user_name"sv, "2/0:@user_name 9/0:", nullptr},
    {"1a.b"sv, "2/0:1a.b 9/0:", nullptr},
    {"1.a"sv, "1/0:1 3/0:. 2/0:a 9/0:", nullptr},
    {"a,1"sv, "2/0:a 24/0:、 1/0:1 9/0:", nullptr},
    {"1,a"sv, "1/0:1 24/0:、 2/0:a 9/0:", nullptr},
    {"(1)"sv, "24/0:( 1/0:1 24/0:) 9/0:", nullptr},
    {"'a'"sv, "24/0:' 2/0:a 24/0:' 9/0:", nullptr},
    {"a'b"sv, "2/0:a 24/0:' 2/0:b 9/0:", nullptr},
    {"[a]{b}<c"sv, "5/0:[ 2/0:a 4/0:] 5/0:{ 2/0:b 4/0:} 77/0:<c 9/0:", nullptr},
    {"\"q\""sv, "4/0:\" 2/0:q 4/0:\" 9/0:", nullptr},
    {"e.g."sv, "2/0:e.g 3/0:. 9/0:", nullptr},
    {"a...b"sv, "2/0:a 3/0:... 2/0:b 9/0:", nullptr},
    {"東京タワーはTokyo Towerです"sv, "24/0:東京タワーは 2/0:Tokyo 7/0:  24/0:Towerです 9/0:", nullptr},
    {"ｶﾀｶﾅとｶﾞ"sv, "24/0:カタカナとカ 22/0:゛ 9/0:", nullptr},
    {"Ｈｅｌｌｏ　ワールド"sv, "2/0:Hello 26/0:　 24/0:ワールド 9/0:", nullptr},
    {"한국어와日本語"sv, "22/0:한국어와 24/0:日本語 9/0:", nullptr},
    {"ΑΒΓあ"sv, "22/0:ΑΒΓ 24/0:あ 9/0:", nullptr},
    {"😀あ😀"sv, "22/0:😀 24/0:あ 22/0:😀 9/0:", nullptr},
    {"第１２３回"sv, "24/0:第123回 9/0:", nullptr},
    {"１ａ２"sv, "2/0:1a2 9/0:", nullptr},
    {"ａ＊ｂ"sv, "24/0:a＊ 2/0:b 9/0:", nullptr},
    {"ａ１あ"sv, "24/0:a1あ 9/0:", nullptr},
    {"１あ"sv, "24/0:1あ 9/0:", nullptr},
    {"Ａ．Ｂ"sv, "2/0:A 17/0:． 2/0:B 9/0:", nullptr},
    {"あＡ"sv, "24/0:あ 2/0:A 9/0:", nullptr},
    {"abc日本"sv, "24/0:abc日本 9/0:", nullptr},
    {"日本abc"sv, "24/0:日本 2/0:abc 9/0:", nullptr},
    {"123日本"sv, "24/0:123日本 9/0:", nullptr},
    {"日本123"sv, "24/0:日本 1/0:123 9/0:", nullptr},
    {"ｱｲｳ｡ｴ"sv, "24/0:アイウ 22/0:。 24/0:エ 9/0:", nullptr},
    {"ﾊﾟﾝ"sv, "24/0:ハ 22/0:゜ 24/0:ン 9/0:", nullptr},
    {"｢ﾃｽﾄ｣"sv, "22/0:「 24/0:テスト 24/0:」 9/0:", nullptr},
    {"ﾃｽﾄ､ﾃｽﾄ"sv, "24/0:テスト 24/0:、テスト 9/0:", nullptr},
    {"abｱ"sv, "24/0:abｱ 9/0:", nullptr},
    {"ｱ･ｲ"sv, "24/0:ア 22/0:・ 24/0:イ 9/0:", nullptr},
    {"ｰｰ"sv, "24/0:ーー 9/0:", nullptr},
    {"ﾞﾟ"sv, "22/0:゛゜ 9/0:", nullptr},
    {"1ｱ"sv, "24/0:1ｱ 9/0:", nullptr},
    {"｣"sv, "24/0:」 9/0:", nullptr},
    {"､あ"sv, "24/0:、あ 9/0:", nullptr},
    {"あ｣"sv, "24/0:あ 24/0:」 9/0:", nullptr},
    {"あ<br>い"sv, "24/0:あ 77/0:<br> 24/0:い 9/0:", nullptr},
    {"<!-- c -->あ"sv, "77/0:<!-- c --> 24/0:あ 9/0:", nullptr},
    {"あ&#CSい&#CSう"sv, "24/0:あ 77/0:&#CS 24/0:い 77/0:&#CS 24/0:う 9/0:", nullptr},
    {"あ　&#CS　い"sv, "24/0:あ 77/0:&#CS 26/0:　 24/0:い 9/0:", nullptr},
    {"a<b"sv, "2/0:a 77/0:<b 9/0:", nullptr},
    {"x<1"sv, "2/0:x 5/0:< 1/0:1 9/0:", nullptr},
    {"<ruby>漢<rt>かん</rt></ruby>"sv, "77/0:<ruby> 24/0:漢 77/0:<rt> 24/0:かん 77/0:</rt> 77/0:</ruby> 9/0:", nullptr},
    {"&#CS"sv, "77/0:&#CS 9/0:", nullptr},
    {"&#C"sv, "2/0:&#C 9/0:", nullptr},
    {"あ&#Cい"sv, "24/0:あ 24/0:&#Cい 9/0:", nullptr},
    {"あ\u2003い"sv, "24/0:あい 9/0:", nullptr},
    {"あ\u00A0"sv, "24/0:あ 9/0:", nullptr},
    {"a　b"sv, "2/0:a 26/0:　 2/0:b 9/0:", nullptr},
    {"あ　　い"sv, "24/0:あい 9/0:", nullptr},
    {"あ 　い"sv, "24/0:あい 9/0:", nullptr},
    {"　"sv, "26/0:　 9/0:", nullptr},
    {"あ\u2028い"sv, "24/0:あい 9/0:", nullptr},
    {"あ・・い"sv, "24/0:あ 17/0:・・ 24/0:い 9/0:", "24/0:あ 22/0:・・ 24/0:い 9/0:"},
    {"あ・。"sv, "24/0:あ 17/0:・ 17/0:。 9/0:", nullptr},
    {"・"sv, "17/0:・ 9/0:", nullptr},
    {"・あ"sv, "24/0:・あ 9/0:", nullptr},
    {"あ・a"sv, "24/0:あ・ 2/0:a 9/0:", nullptr},
    {"あ・ "sv, "24/0:あ 17/0:・ 9/0:", nullptr},
    {"あー"sv, "24/0:あ 17/0:ー 9/0:", nullptr},
    {"あーい"sv, "24/0:あーい 9/0:", nullptr},
    {"すごいー！"sv, "24/0:すごい 17/0:ー！ 9/0:", nullptr},
    // ！ or ？ after a lengthening ～ starts a token of its own, a sentence end like any other.
    {"すごい～！！"sv, "24/0:すごい 17/0:～ 17/0:！！ 9/0:", nullptr},
    {"すごい～？"sv, "24/0:すごい 17/0:～ 17/0:？ 9/0:", nullptr},
    {"あ～"sv, "24/0:あ 17/0:～ 9/0:", nullptr},
    {"あ～a"sv, "24/0:あ 17/0:～ 2/0:a 9/0:", nullptr},
    {"あ-a"sv, "24/0:あ 17/0:ー 2/0:a 9/0:", nullptr},
    {"あ-"sv, "24/0:あ 17/0:ー 9/0:", nullptr},
    {"ア-"sv, "24/0:アー 9/0:", nullptr},
    {"亜-"sv, "24/0:亜ー 9/0:", nullptr},
    {"あ--"sv, "24/0:あ 17/0:ー 8/0:- 9/0:", nullptr},
    {"。。。"sv, "17/0:。 17/0:。 17/0:。 9/0:", "22/0:。 22/0:。 22/0:。 9/0:"},
    {"あ。。い"sv, "24/0:あ 17/0:。 17/0:。 24/0:い 9/0:", "24/0:あ 22/0:。 22/0:。 24/0:い 9/0:"},
    {"時ーー扉"sv, "24/0:時ーー扉 9/0:", "24/0:時 22/0:ーー 24/0:扉 9/0:"},
    {"「ーー待て」"sv, "24/0:「ーー待て」 9/0:", "24/0:「 22/0:ーー 24/0:待て」 9/0:"},
    {"兄上ーーっ"sv, "24/0:兄上ーーっ 9/0:", nullptr},
    {"あーーい"sv, "24/0:あーーい 9/0:", nullptr},
    {"あ。い。"sv, "24/0:あ 17/0:。 24/0:い 17/0:。 9/0:", nullptr},
    {"？！？"sv, "17/0:？！？ 9/0:", nullptr},
    {"．"sv, "17/0:． 9/0:", nullptr},
    {"！あ"sv, "17/0:！ 24/0:あ 9/0:", nullptr},
    {"…"sv, "22/0:… 9/0:", nullptr},
    {"‥"sv, "22/0:‥ 9/0:", nullptr},
    {"※"sv, "22/0:※ 9/0:", nullptr},
    {"〒"sv, "22/0:〒 9/0:", nullptr},
    {"♪♪"sv, "22/0:♪♪ 9/0:", nullptr},
    {"★あ"sv, "22/0:★ 24/0:あ 9/0:", nullptr},
    {"、"sv, "24/0:、 9/0:", nullptr},
    {"あ，い"sv, "24/0:あ、い 9/0:", nullptr},
    {"，"sv, "24/0:、 9/0:", nullptr},
    {"（あ）"sv, "24/0:（あ） 9/0:", nullptr},
    {"＝"sv, "24/0:＝ 9/0:", nullptr},
    {"○×"sv, "22/0:○× 9/0:", nullptr},
    {"“あ”"sv, "24/0:“あ” 9/0:", nullptr},
    {"‘あ’"sv, "18/0:‘ 24/0:あ 18/0:’ 9/0:", nullptr},
    {"〈あ〉"sv, "18/0:〈 24/0:あ 18/0:〉 9/0:", nullptr},
    {"＜あ＞"sv, "18/0:＜ 24/0:あ 18/0:＞ 9/0:", nullptr},
    {"あ い う"sv, "24/0:あいう 9/0:", nullptr},
    {"あ  い"sv, "24/0:あい 9/0:", nullptr},
    {"あ &#CS い"sv, "24/0:あ 77/0:&#CS 7/0:  24/0:い 9/0:", nullptr},
    {"a い"sv, "2/0:a 7/0:  24/0:い 9/0:", nullptr},
    {"あ a"sv, "24/0:あ 7/0:  2/0:a 9/0:", nullptr},
    {"1 2"sv, "1/0:1 7/0:  1/0:2 9/0:", nullptr},
    {"! ?"sv, "3/0:! 7/0:  3/0:? 9/0:", nullptr},
    {"あ　a"sv, "24/0:あ 26/0:　 2/0:a 9/0:", nullptr},
    {"a　あ"sv, "2/0:a 26/0:　 24/0:あ 9/0:", nullptr},
};

}  // namespace

HD_TEST(tokenizer_scan_cases) {
    for (const ScanCase& c : kScanCases) {
        const std::string plain = ScanText(c.input, false);
        const std::string dashes = ScanText(c.input, true);
        if (plain != c.plain) {
            hdtest::Fail(__FILE__, __LINE__, stagetest::EscapeStageText(c.input) + ": " + plain + " want " + c.plain);
        }
        const char* wantDashes = c.dashes != nullptr ? c.dashes : c.plain;
        if (dashes != wantDashes) {
            hdtest::Fail(__FILE__, __LINE__,
                         stagetest::EscapeStageText(c.input) + " (dashes): " + dashes + " want " + wantDashes);
        }
    }
}

HD_TEST(tokenizer_empty_and_trimmed) {
    Tokens t;
    Tokenize("", t);
    HD_CHECK_EQ(t.size(), size_t{0});
    Tokenize("   ", t);  // trailing spaces are dropped first, leaving only the end token
    HD_CHECK_EQ(Scanned(t), std::string("9/0:"));
    Tokenize("\t", t);
    HD_CHECK_EQ(Scanned(t), std::string("7/0:\\t 9/0:"));
    Tokenize("a\0b"sv, t);
    HD_CHECK_EQ(Scanned(t), std::string("2/0:a 9/0:"));
    Tokenize("a b  ", t);
    HD_CHECK_EQ(Scanned(t), std::string("2/0:a 7/0:  2/0:b 9/0:"));
    TokenizeSpans({}, true, t);
    HD_CHECK_EQ(t.size(), size_t{0});
}

HD_TEST(tokenizer_long_ascii_runs) {
    std::string letters, digits;
    for (int i = 0; i < 500; i++) {
        letters += static_cast<char>('a' + i % 26);
        digits += static_cast<char>('0' + i % 10);
    }
    Tokens t;
    Tokenize(letters + "あ" + digits, t);
    HD_REQUIRE(t.size() == 3);
    HD_CHECK_EQ(t[0].type, kTypeJapanese);
    HD_CHECK(t.Raw(0) == letters + "あ");
    HD_CHECK_EQ(t[1].type, kTypeNumber);
    HD_CHECK(t.Raw(1) == digits);
    Tokenize(letters + " " + letters, t);
    HD_CHECK_EQ(Scanned(t), "2/0:" + letters + " 7/0:  2/0:" + letters + " 9/0:");
}

HD_TEST(tokenizer_spans) {
    const Piece word{"김", kSpanWord};
    struct Case {
        std::vector<Piece> input;
        bool dashes;
        const char* want;
    } cases[] = {
        {{{"これは", 0}, word, {"です", 0}}, false, "24/0:これは 79/1:김 24/0:です 9/0:"},
        // Spaces next to a span are kept, even between Japanese text.
        {{{"あ ", 0}, word, {" い", 0}}, false, "24/0:あ 7/0:  79/1:김 7/0:  24/0:い 9/0:"},
        {{{"<b>", kSpanOpen}, word, {"</b>", kSpanClose}, {"が", 0}}, false,
         "77/2:<b> 79/1:김 77/3:</b> 24/0:が 9/0:"},
        {{{"A", 0}, word, {"の", 0}, word}, false, "2/0:A 79/1:김 24/0:の 79/1:김 9/0:"},
        // A trailing span loses the ASCII spaces after it.
        {{{"あ", 0}, word, {"  ", 0}}, false, "24/0:あ 79/1:김 9/0:"},
        // A control character in the text is not a span.
        {{{"A\x01" "B", 0}}, false, "2/0:A 7/0:  2/0:B 9/0:"},
        // A span reads as a control character to the look-arounds: ー after it stays a
        // symbol, 」 before it is not utterance-final.
        {{word, {"－あ", 0}}, true, "79/1:김 22/0:－ 24/0:あ 9/0:"},
        {{{"「あ」", 0}, word}, true, "24/0:「あ」 79/1:김 9/0:"},
        // Text after a NUL is ignored, spans included.
        {{{"あ\0い"s, 0}, word}, false, "24/0:あ 9/0:"},
    };
    Tokens t;
    for (const Case& c : cases) {
        TokenizeSpans(c.input, c.dashes, t);
        HD_CHECK_EQ(Scanned(t), std::string(c.want));
        for (size_t i = 0; i < t.size(); i++) {
            if (t[i].span != 0) HD_CHECK(t.Merged(i) == t.Raw(i) && t.Canonical(i) == t.Raw(i));
        }
    }
}

HD_TEST(tokenizer_source_offsets) {
    struct Case {
        std::vector<Piece> input;
        std::vector<std::pair<uint32_t, uint32_t>> want;
    } cases[] = {
        {{{"a　あ", 0}}, {{0, 1}, {1, 4}, {4, 7}, {7, 7}}},
        {{{"あ い", 0}}, {{0, 7}, {7, 7}}},  // the dropped space lies inside the token
        {{{"a  ", 0}}, {{0, 1}, {1, 1}}},     // trimmed spaces belong to no token
        {{{"a\xff" "b", 0}}, {{0, 1}, {1, 2}, {2, 3}, {3, 3}}},
        {{{"これは", 0}, {"김", kSpanWord}, {"です", 0}}, {{0, 9}, {9, 12}, {12, 18}, {18, 18}}},
        {{{"<a>x</a>", 0}}, {{0, 3}, {3, 4}, {4, 8}, {8, 8}}},
        {{{"あ-", 0}}, {{0, 3}, {3, 4}, {4, 4}}},
    };
    Tokens t;
    for (const Case& c : cases) {
        TokenizeSpans(c.input, false, t);
        HD_REQUIRE(t.size() == c.want.size());
        for (size_t i = 0; i < t.size(); i++) {
            HD_CHECK_EQ(t[i].sourceBegin, c.want[i].first);
            HD_CHECK_EQ(t[i].sourceEnd, c.want[i].second);
        }
    }
}

namespace {

struct PrepareCase {
    std::string_view input;
    const char* tokens;   // "type:canonical|merged" after the per-token stage
    const char* markers;  // "token/length/kind"
};

// Control-marker merging and canonical forms.
const PrepareCase kPrepareCases[] = {
    {"あ&#CSい"sv, "24:あい|あい 77:| 77:| 9:|", "0/3/1"},
    {"あ &#CS い"sv, "24:あい|あい 77:| 77:| 77:| 9:|", "0/3/1"},
    {"<a>あ</a>い"sv, "77:<a>|<a> 24:あい|あい 77:| 77:| 9:|", "1/3/2"},
    {"あ&#CS&#CSい"sv, "24:あい|あい 77:| 77:| 77:| 9:|", "0/3/1 0/3/1"},
    {"あ&#CS<b>い"sv, "24:あい|あい 77:| 77:<b>|<b> 77:| 9:|", "0/3/1"},
    {"あ&#CS　い&#CSう"sv, "24:あいう|あいう 77:| 77:| 77:| 77:| 77:| 9:|", "0/3/1 0/6/1"},
    {"あ</a>い</a>う"sv, "24:あい|あい 77:| 77:| 77:</a>|</a> 24:う|う 9:|", "0/3/2"},
    {"あ</A>い"sv, "24:あい|あい 77:| 77:| 9:|", "0/3/2"},
    {"あ</a>"sv, "24:あ|あ 77:</a>|</a> 9:|", ""},
    {"</a>い"sv, "77:</a>|</a> 24:い|い 9:|", ""},
    {"あ&#CSa"sv, "24:あ|あ 77:&#CS|&#CS 2:a|a 9:|", ""},
    {"a&#CSい"sv, "2:a|a 77:&#CS|&#CS 24:い|い 9:|", ""},
    {"あ&#CS。い"sv, "24:あ|あ 77:&#CS|&#CS 17:.|。 24:い|い 9:|", ""},
    {"あ&#CS"sv, "24:あ|あ 77:&#CS|&#CS 9:|", ""},
    {"。、"sv, "17:.|。 24:、|、 9:|", ""},
    {"〒♪"sv, "22:〒♪|〒♪ 9:|", ""},
    {"％＆＄"sv, "22:％＆＄|％＆＄ 9:|", ""},
    {"＠＊＋"sv, "22:＠＊＋|＠＊＋ 9:|", ""},
    {"＊＊あ"sv, "24:＊＊あ|＊＊あ 9:|", ""},
    {"＊＠＋"sv, "22:＊＠＋|＊＠＋ 9:|", ""},
    {"あ＊＠"sv, "24:あ|あ 22:＊＠|＊＠ 9:|", ""},
    {"「＊＊＃％」"sv, "24:「|「 22:＊＊＃％|＊＊＃％ 24:」|」 9:|", ""},
    {"Ａ＊＠"sv, "24:A|A 22:＊＠|＊＠ 9:|", ""},
    {"＊<b>＊＠"sv, "24:＊|＊ 77:<b>|<b> 22:＊＠|＊＠ 9:|", ""},
    {"３４％……"sv, "1:34|34 22:%|％ 22:……|…… 9:|", ""},
    {"あ％……"sv, "24:あ|あ 22:％……|％…… 9:|", ""},
    {"３４％％％"sv, "1:34|34 22:％％％|％％％ 9:|", ""},
    {"１！！"sv, "1:1|1 17:!|！！ 9:|", ""},
    {"【】"sv, "18:【】|【】 9:|", ""},
    {"　あ"sv, "26:　|　 24:あ|あ 9:|", ""},
    {"あ\015\012"sv, "24:あ|あ 6:\\r\\n|\\r\\n 9:|", ""},
    {"？！"sv, "17:?|？！ 9:|", ""},
    {"！？"sv, "17:!|！？ 9:|", ""},
    {"あ。"sv, "24:あ|あ 17:.|。 9:|", ""},
    {"あ、"sv, "24:あ、|あ、 9:|", ""},
    {"あ\011"sv, "24:あ|あ 7:\\t|\\t 9:|", ""},
    {"!?"sv, "3:!?|!? 9:|", ""},
    {"..."sv, "3:...|... 9:|", ""},
    {"「あ」"sv, "24:「あ」|「あ」 9:|", ""},
    {"あ<BR>い。ab"sv, "24:あ|あ 77:<BR>|<BR> 24:い|い 17:.|。 2:ab|ab 9:|", ""},
    {"あー！"sv, "24:あ|あ 17:―！|ー！ 9:|", ""},
    {"♪〒①髙"sv, "22:♪〒①高|♪〒①髙 9:|", ""},
    {"１２"sv, "1:12|12 9:|", ""},
    {"ｱ｡"sv, "24:ア|ア 22:.|。 9:|", ""},
    {"あ･"sv, "24:あ|あ 22:·|・ 9:|", ""},
    {"　"sv, "26:　|　 9:|", ""},
    {"あ　い。"sv, "24:あい|あい 17:.|。 9:|", ""},
};

}  // namespace

HD_TEST(tokenizer_prepare_cases) {
    Tokens t;
    for (const PrepareCase& c : kPrepareCases) {
        Tokenize(c.input, t);
        if (Prepared(t) != c.tokens) {
            hdtest::Fail(__FILE__, __LINE__,
                         stagetest::EscapeStageText(c.input) + ": " + Prepared(t) + " want " + c.tokens);
        }
        if (Markers(t) != c.markers) {
            hdtest::Fail(__FILE__, __LINE__,
                         stagetest::EscapeStageText(c.input) + ": markers " + Markers(t) + " want " + c.markers);
        }
    }
}

HD_TEST(tokenizer_views) {
    Tokens t;
    Tokenize("あ &#CS い。う", t);  // あ &#CS ' ' い 。 う end
    HD_REQUIRE(t.size() == 7);
    HD_CHECK_EQ(t[1].clearedBy, 0);
    HD_CHECK_EQ(t[2].clearedBy, 0);
    HD_CHECK_EQ(t[3].clearedBy, 0);
    HD_CHECK_EQ(t[4].clearedBy, -1);
    HD_CHECK(t.Raw(3) == "い");
    HD_CHECK(t.Merged(0) == "あい");
    HD_CHECK(t.Raw(0) == "あ");
    // Seen from token 0 after its merge, the merged tokens are empty markup.
    HD_CHECK_EQ(t.View(3, 0).type, kTypeMarkup);
    HD_CHECK(t.View(3, 0).text.empty());
    HD_CHECK(t.View(0, 0).text == "あい");
    // Later tokens are seen raw until they are visited, canonical afterwards.
    HD_CHECK_EQ(t.View(4, 0).type, kTypeSentenceEnd);
    HD_CHECK(t.View(4, 0).text == "。");
    HD_CHECK(t.View(4, 4).text == ".");
    HD_CHECK(t.View(4, 5).text == ".");
    HD_CHECK_EQ(t.TypeAfter(2), kTypeMarkup);
    HD_CHECK_EQ(t[2].type, kTypeAsciiSpace);
}

HD_TEST(tokenizer_buffers_reused) {
    Tokens t;
    Tokenize("あ&#CSい。<a>う</a>え", t);
    const std::string first = Prepared(t);
    Tokenize("ｶﾀｶﾅ　です！", t);
    Tokenize("あ&#CSい。<a>う</a>え", t);
    HD_CHECK_EQ(Prepared(t), first);
    HD_CHECK_EQ(Markers(t), std::string("0/3/1 5/3/2"));
}

HD_TEST(tokenizer_canonical_text) {
    struct Case {
        const char* in;
        const char* want;
    } cases[] = {
        {"。", "."},  {"、", ","},     {"？！", "?"}, {"！？", "!"},       {"〒", "(우)"},
        {"♪〒", "♪〒"}, {"【】", "【】"}, {"　", "　"},   {"a。b", "a. b"},   {"ｱ", "ア"},
        {"％", "%"},  {"％＆＄＃", "％＆＄＃"}, {"／／", "／／"}, {"〒♪", "〒♪"}, {"♪％", "♪％"},
        {"／ヽ", "／ヽ"}, {"ヽ", "ヽ"}, {"纊褜", "광褜"}, {"蕙", "혜"}, {"°", "°"},
    };
    for (const Case& c : cases) {
        std::string out = "x";
        AppendCanonicalText(out, c.in);
        HD_CHECK_EQ(out, std::string("x") + c.want);
    }
}

HD_TEST(tokenizer_stage_text_escape) {
    const std::string_view raw = "a\\b\tc\nd\re\x01\x7f\xff\xe3\x81" "f あ"sv;
    const std::string escaped = stagetest::EscapeStageText(raw);
    HD_CHECK_EQ(escaped, std::string("a\\\\b\\tc\\nd\\re\\x01\\x7f\\xff\\xe3\\x81f あ"));
    HD_CHECK(stagetest::UnescapeStageText(escaped) == raw);
    HD_CHECK_EQ(stagetest::EscapeStageText("あい"), std::string("あい"));
}

HD_TEST(tokenizer_mark_run) {
    HD_CHECK_EQ(MarkRun("！！"), std::string("!!"));
    HD_CHECK_EQ(MarkRun("！？！？」"), std::string("!?!?"));
    HD_CHECK_EQ(MarkRun("？"), std::string("?"));
    HD_CHECK_EQ(MarkRun("‼⁉⁈"), std::string("!!!?") + "?!");
    HD_CHECK_EQ(MarkRun("。"), std::string());
}

// With SpaceJoins, a single space between two kanji parts two words unless a word joins
// them; two lone pieces of a name stay one token.
HD_TEST(tokenize_kept_kanji_space) {
    // The words 未来 and 通知表, starting anywhere in the text before the space.
    const SpaceJoins joins = [](std::string_view before, std::string_view after, SpaceWord kind) {
        if (kind == SpaceWord::Filler) return false;
        for (const std::string_view word : {"未来"sv, "通知表"sv}) {
            for (std::size_t start = 0; start < before.size(); start++) {
                const std::string text = std::string(before.substr(start)) + std::string(after);
                if (text.starts_with(word) && word.size() > before.size() - start) return true;
            }
        }
        return false;
    };
    const auto scan = [&](std::string_view input) {
        const std::vector<Piece> pieces = {{std::string(input), 0}};
        Tokens t;
        TokenizeSpans(pieces, true, t, &joins);
        return Scanned(t);
    };
    HD_CHECK_EQ(scan("光 影 光 影"), std::string("24/0:光 7/0:  24/0:影 7/0:  24/0:光 7/0:  24/0:影 9/0:"));
    HD_CHECK_EQ(scan("光　影　光　影"), std::string("24/0:光 7/0:  24/0:影 7/0:  24/0:光 7/0:  24/0:影 9/0:"));
    HD_CHECK_EQ(scan("副総理 兼 外務大臣"), std::string("24/0:副総理 7/0:  24/0:兼 7/0:  24/0:外務大臣 9/0:"));
    HD_CHECK_EQ(scan("未 来"), std::string("24/0:未来 9/0:"));
    HD_CHECK_EQ(scan("通 知 表"), std::string("24/0:通知表 9/0:"));
    HD_CHECK_EQ(scan("林　涛"), std::string("24/0:林涛 9/0:"));
    HD_CHECK_EQ(scan("君の 夢"), std::string("24/0:君の 24/0:夢 9/0:"));
    HD_CHECK_EQ(scan("ジモ得 週"), std::string("24/0:ジモ得週 9/0:"));  // a word of kana and kanji before it
    // Without joins every such space is dropped.
    HD_CHECK_EQ(ScanText("光 影", true), std::string("24/0:光影 9/0:"));
}

// With SpaceJoins, any other single space between Japanese characters ends the Japanese token, and
// the next one is afterSpace (marked +), unless the text is spaced out character by character, a
// piece is said again, or joins finds a name, a phrase or an ending across it.
HD_TEST(tokenize_space_parts_words) {
    const SpaceJoins joins = [](std::string_view before, std::string_view after, SpaceWord word) {
        const std::string text = std::string(before) + std::string(after);
        switch (word) {
        case SpaceWord::Any:
            for (const std::string_view w : {"あご"sv, "山田太郎"sv, "展開"sv, "先行"sv, "紅白"sv}) {
                if (text.find(w) != std::string::npos) return true;
            }
            return false;
        case SpaceWord::Binding:
            return text.starts_with("山田太郎") || (before == "いや" && after.starts_with("そう")) ||
                   (before == "では" && after.starts_with("あり"));
        case SpaceWord::Phrase: return before == "何だ" && after.starts_with("あれ");
        case SpaceWord::Ending: return after == "ね";
        case SpaceWord::Filler: return before == "あ" || before == "いや";
        case SpaceWord::Spelled: return text.find("展開") != std::string::npos;
        case SpaceWord::Compound: return before == "紅" && after.starts_with("白って");
        }
        return false;
    };
    const auto scan = [&](std::string_view input) {
        const std::vector<Piece> pieces = {{std::string(input), 0}};
        Tokens t;
        TokenizeSpans(pieces, true, t, &joins);
        std::string s;
        for (size_t i = 0; i < t.size(); i++) {
            if (i > 0) s += ' ';
            s += (t[i].afterSpace ? "+" : "") + std::to_string(t[i].type) + ":" + std::string(t.Raw(i));
        }
        return s;
    };
    HD_CHECK_EQ(scan("あ ごめんね"), std::string("24:あ +24:ごめんね 9:"));
    HD_CHECK_EQ(scan("いや　そうじゃない"), std::string("24:いや +24:そうじゃない 9:"));
    HD_CHECK_EQ(scan("あ ごめん いいよ。"), std::string("24:あ +24:ごめん +24:いいよ 17:。 9:"));
    HD_CHECK_EQ(scan("山田 太郎が来た"), std::string("24:山田太郎が来た 9:"));
    HD_CHECK_EQ(scan("何だ あれ"), std::string("24:何だあれ 9:"));
    HD_CHECK_EQ(scan("では ありません"), std::string("24:ではありません 9:"));
    HD_CHECK_EQ(scan("そうだ ね……"), std::string("24:そうだね 22:…… 9:"));
    HD_CHECK_EQ(scan("そうだ ねえ"), std::string("24:そうだ +24:ねえ 9:"));
    HD_CHECK_EQ(scan("やめて やめて"), std::string("24:やめてやめて 9:"));
    HD_CHECK_EQ(scan("消す っていう"), std::string("24:消すっていう 9:"));
    HD_CHECK_EQ(scan("こ の 展 開 は"), std::string("24:この展開は 9:"));
    HD_CHECK_EQ(scan("ね え さ ん"), std::string("24:ねえさん 9:"));
    HD_CHECK_EQ(scan("あ れ が"), std::string("24:あれが 9:"));
    HD_CHECK_EQ(scan("私 先 行くね"), std::string("24:私 +24:先 +24:行くね 9:"));
    HD_CHECK_EQ(scan("紅 白ってある"), std::string("24:紅白ってある 9:"));
    HD_CHECK_EQ(scan("金 貸して"), std::string("24:金 7:  24:貸して 9:"));
    HD_CHECK_EQ(scan("通 知 展 開は"), std::string("24:通知展開は 9:"));
    HD_CHECK_EQ(scan("あ な た"), std::string("24:あなた 9:"));
    HD_CHECK_EQ(scan("あ の 人"), std::string("24:あの人 9:"));
    HD_CHECK_EQ(scan("あ い"), std::string("24:あ +24:い 9:"));
    HD_CHECK_EQ(scan("何 これ"), std::string("24:何 +24:これ 9:"));
    HD_CHECK_EQ(scan("どう する"), std::string("24:どう +24:する 9:"));
    // Not between Japanese characters: digits, letters and marks keep their own rules.
    HD_CHECK_EQ(scan("2 人"), std::string("1:2 7:  24:人 9:"));
    HD_CHECK_EQ(scan("え、 そう"), std::string("24:え、そう 9:"));
    // Without joins every such space is dropped.
    HD_CHECK_EQ(ScanText("あ ごめんね", true), std::string("24/0:あごめんね 9/0:"));
    // The space between copies of two characters or more stays in the token as a CopySpace: its token,
    // the byte offset of the copy after it and the bytes of one copy.
    const auto copies = [&](std::string_view input) {
        const std::vector<Piece> pieces = {{std::string(input), 0}};
        Tokens t;
        TokenizeSpans(pieces, true, t, &joins);
        std::string s;
        for (const CopySpace& c : t.copySpaces) {
            s += std::to_string(c.tokenIndex) + "@" + std::to_string(c.at) + "/" + std::to_string(c.unit) + " ";
        }
        return s;
    };
    HD_CHECK_EQ(copies("えっ えっ"), std::string("0@6/6 "));
    HD_CHECK_EQ(copies("やめて やめて やめて"), std::string("0@9/9 0@18/9 "));
    HD_CHECK_EQ(copies("お お"), std::string(""));  // one kana: the sound stage's
    // The lookups at the edges of a run: the first copy, a later copy, between copies and past the run.
    {
        const std::vector<Piece> pieces = {{"やめて やめて やめて 何", 0}};
        Tokens t;
        TokenizeSpans(pieces, true, t, &joins);
        HD_CHECK(t.CopySpaceAt(0, 9));
        HD_CHECK(t.CopySpaceAt(0, 18));
        HD_CHECK(!t.CopySpaceAt(0, 0));
        HD_CHECK(!t.CopySpaceAt(0, 10));
        HD_CHECK(!t.CopySpaceAt(1, 9));
        HD_CHECK_EQ(t.CopyUnitAt(0, 0), size_t{9});
        HD_CHECK_EQ(t.CopyUnitAt(0, 9), size_t{9});
        HD_CHECK_EQ(t.CopyUnitAt(0, 18), size_t{9});
        HD_CHECK_EQ(t.CopyUnitAt(0, 3), size_t{0});
        HD_CHECK_EQ(t.CopyUnitAt(0, 27), size_t{0});
        HD_CHECK_EQ(t.CopySpaceAfter(0, 0), size_t{9});
        HD_CHECK_EQ(t.CopySpaceAfter(0, 9), size_t{18});
        HD_CHECK_EQ(t.CopySpaceAfter(0, 18), std::string_view::npos);
        HD_CHECK_EQ(t.CopySpaceAfter(1, 0), std::string_view::npos);
    }
    HD_CHECK_EQ(copies("あ ごめんね"), std::string(""));
}

// With SpaceJoins, a letter after kana goes on with the Japanese token when a word spans both.
HD_TEST(tokenize_kept_letter_after_kana) {
    const SpaceJoins joins = [](std::string_view before, std::string_view after, SpaceWord) {
        const std::size_t end = after.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        const std::size_t letters = end == std::string_view::npos ? after.size() : end;
        for (const std::string_view word : {"うp"sv, "おk"sv}) {
            for (std::size_t start = 0; start < before.size(); start++) {
                const std::string text = std::string(before.substr(start)) + std::string(after);
                const std::size_t left = before.size() - start;
                if (text.starts_with(word) && word.size() > left && word.size() - left >= letters) return true;
            }
        }
        return false;
    };
    const auto scan = [&](std::string_view input) {
        const std::vector<Piece> pieces = {{std::string(input), 0}};
        Tokens t;
        TokenizeSpans(pieces, true, t, &joins);
        return Scanned(t);
    };
    HD_CHECK_EQ(scan("写真うｐ"), std::string("24/0:写真うp 9/0:"));
    HD_CHECK_EQ(scan("おｋおｋ"), std::string("24/0:おkおk 9/0:"));
    HD_CHECK_EQ(scan("うpした"), std::string("24/0:うpした 9/0:"));
    HD_CHECK_EQ(scan("うｐ乙"), std::string("24/0:うp乙 9/0:"));
    HD_CHECK_EQ(scan("おk"), std::string("24/0:おk 9/0:"));
    for (const std::string_view input : {"思うp8です"sv, "言うp6です"sv, "思うp12です"sv, "思うpxです"sv,
                                        "思うp8&x:"sv, "見るp8です"sv, "思うq8です"sv, "思うP8です"sv,
                                        "思うｐ８です"sv, "思うp８です"sv, "思うｐxです"sv, "思うpｘです"sv,
                                        "うp8"sv, "おkmです"sv}) {
        HD_CHECK_EQ(scan(input), ScanText(input, true));
    }
    HD_CHECK_EQ(scan("これはOKです"), ScanText("これはOKです", true));
    HD_CHECK_EQ(ScanText("写真うｐ", true), std::string("24/0:写真う 2/0:p 9/0:"));
}
