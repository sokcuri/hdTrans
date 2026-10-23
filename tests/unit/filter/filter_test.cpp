#include "core/filter/filter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/filter/ehnd.h"
#include "core/filter/matcher.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::filter;

namespace {

using Kind = Source::Kind;

std::shared_ptr<const Filters> Make(std::vector<Source> sources, std::vector<Error>* errors = nullptr) {
    std::vector<Error> e;
    auto f = Build(sources, errors ? *errors : e);
    return f;
}

// line through Pre, then (with post) through Post with the toggles Pre left.
std::string Run(const Filters& f, std::string line, const char* post = nullptr) {
    Filters::Toggles t = f.Start();
    f.Apply(Stage::Pre, line, t);
    if (!post) return line;
    std::string out = post;
    f.Apply(Stage::Post, out, t);
    return line + " | " + out;
}

std::string Shown(const std::vector<Error>& errors) {
    std::string s;
    for (const Error& e : errors) s += (s.empty() ? "" : "\n") + e.file + ":" + std::to_string(e.line) + ": " + e.message;
    return s;
}

}  // namespace

HD_TEST(FilterLiteralRows) {
    // A replacement resumes after the text it inserted. "x\ty" with its newline has three
    // fields (layer 0); the same row at the end of the file without one has two and is
    // dropped.
    auto f = Make({{Kind::PreFilter, "p.txt", "//comment\na\taa\t1\t0\nx\ty\nq\tQ", false}});
    HD_CHECK_EQ(Run(*f, "aXa x q"), "aaXaa y q");
    HD_CHECK_EQ(Run(*f, ""), "");
    // A "//" ends the line; in the REGEX field "0//" reads 0.
    f = Make({{Kind::PreFilter, "p.txt", "b\tc\t1\t0//note\nd\te//f\t1\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "bd"), "cd");
    // "#" lines are comments of native files only.
    f = Make({{Kind::PreFilter, "p.tsv", "# about\n#\tX\t1\t0\n", true}});
    HD_CHECK_EQ(Run(*f, "#"), "#");
    f = Make({{Kind::PreFilter, "p.txt", "#\tX\t1\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "#"), "X");
}

HD_TEST(FilterRegexRows) {
    auto f = Make({{Kind::PreFilter, "p.txt",
                    "(\\d+)円\t$1엔\t1\t1\n"
                    "[ぁ-ん]+\t<$0>\t2\t1\n"
                    "た(?=[。])\tだ\t3\t1\t_AUX:X\n",
                    false}});
    HD_CHECK_EQ(Run(*f, "100円と20円"), "100엔<と>20엔");
    HD_CHECK_EQ(Run(*f, "見た。"), "見<た>。");
    // _AUX: the regex runs only when the text holds the AUX text.
    f = Make({{Kind::PreFilter, "p.txt", "た(?=。)\tだ\t3\t1\t_AUX:X\n", false}});
    HD_CHECK_EQ(Run(*f, "見た。"), "見た。");
    HD_CHECK_EQ(Run(*f, "X見た。"), "X見だ。");
    // A literal row with _AUX runs only with it too.
    f = Make({{Kind::PreFilter, "p.txt", "た\tだ\t3\t0\t_AUX:X\n", false}});
    HD_CHECK_EQ(Run(*f, "見た"), "見た");
    HD_CHECK_EQ(Run(*f, "X見た"), "X見だ");
}

HD_TEST(FilterLayerOrder) {
    // Rows run by layer, then in load order across files.
    auto f = Make({{Kind::PreFilter, "a.txt", "x\ty\t20\t0\nq\tx\t10\t0\nm\tA\t5\t0\n", false},
                   {Kind::PreFilter, "b.txt", "m\tB\t5\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "q m"), "y A");
    f = Make({{Kind::PreFilter, "b.txt", "m\tB\t5\t0\n", false}, {Kind::PreFilter, "a.txt", "m\tA\t5\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "m"), "B");
    // Post rows run only after translation.
    f = Make({{Kind::PostFilter, "o.txt", "개\t멍멍이\t1\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "개", "개"), "개 | 멍멍이");
}

HD_TEST(FilterToggles) {
    const std::string conn = "_TGL\t_CONN:T_\tx\tX\t5\t0\n";
    auto f = Make({{Kind::PreFilter, "s.txt", "_TGL\t_DECR:T_\t_OFF\n", false}, {Kind::PreFilter, "p.txt", conn, false}});
    HD_CHECK_EQ(Run(*f, "x"), "x");
    f = Make({{Kind::PreFilter, "s.txt", "_TGL\t_DECR:T_\t_ON\n", false}, {Kind::PreFilter, "p.txt", conn, false}});
    HD_CHECK_EQ(Run(*f, "x"), "X");
    // The first declaration holds.
    f = Make({{Kind::PreFilter, "s.txt", "_TGL\t_DECR:T_\t_ON\n_TGL\t_DECR:T_\t_OFF\n", false},
              {Kind::PreFilter, "p.txt", conn, false}});
    HD_CHECK_EQ(Run(*f, "x"), "X");
    // A firing _ON row (here a check row) switches T_ on for the later rows of the line,
    // Post included; _OFF switches it off. The next line starts from the declaration.
    f = Make({{Kind::PreFilter, "p.txt",
               "_TGL\t_DECR:T_\t_OFF\n_TGL\t_ON:T_\tgo\t_MATCH_CHECK\t1\t0\n" + conn +
                   "_TGL\t_OFF:T_\tstop\t_MATCH_CHECK\t6\t0\n",
               false},
              {Kind::PostFilter, "o.txt", "_TGL\t_CONN:T_\ty\tY\t1\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "go x", "y"), "go X | Y");
    HD_CHECK_EQ(Run(*f, "x", "y"), "x | y");
    HD_CHECK_EQ(Run(*f, "go stop x", "y"), "go stop X | y");
    // A _CONN row of an undeclared toggle is off.
    f = Make({{Kind::PreFilter, "p.txt", conn, false}});
    HD_CHECK_EQ(Run(*f, "x"), "x");
}

HD_TEST(FilterSkipLayer) {
    // The rows of layer 5 of Pre run only when the text matches every condition of it.
    auto f = Make({{Kind::PreFilter, "p.txt", "a\tb\t5\t0\nc\td\t6\t0\n", false},
                   {Kind::PostFilter, "o.txt", "a\tb\t5\t0\n", false},
                   {Kind::SkipLayer, "s.txt", "//PRE\nPRE\t5\tne+d\t1\t//note\nPOST\t5\tz\n", false}});
    HD_CHECK_EQ(Run(*f, "a c", "a"), "a d | a");
    HD_CHECK_EQ(Run(*f, "need a c", "za"), "need b d | zb");
    f = Make({{Kind::PreFilter, "p.txt", "a\tb\t5\t0\n", false},
              {Kind::SkipLayer, "s.txt", "PRE\t5\tn\nPRE\t5\tm\n", false}});
    HD_CHECK_EQ(Run(*f, "n a"), "n a");
    HD_CHECK_EQ(Run(*f, "m n a"), "m n b");
}

HD_TEST(FilterGuards) {
    // A firing _SkipLayer row opens the next run of _skip_sub rows; the run closes it.
    auto f = Make({{Kind::PreFilter, "p.txt",
                    "_SkipLayer\tK\t_MATCH_CHECK\t5\t0\n_skip_sub\ta\tb\t5\t0\n_skip_sub\tc\td\t5\t0\n"
                    "e\tf\t5\t0\n_skip_sub\tg\th\t5\t0\n_SkipLayer\tK\tL\t6\t0\n_skip_sub\tj\tJ\t6\t0\n",
                    false}});
    HD_CHECK_EQ(Run(*f, "K a c e g j"), "L b d f g J");
    HD_CHECK_EQ(Run(*f, "a c e g j"), "a c f g j");
}

HD_TEST(FilterRegexDialect) {
    // Lookbehind, possessive quantifiers, back-references in lookaheads and escapes of
    // punctuation compile; \d takes full-width digits.
    auto f = Make({{Kind::PreFilter, "p.txt",
                    "(?<=[一-龥].)る\tル\t1\t1\n"
                    "(?<!見)え\tエ\t1\t1\n"
                    "(か)(?=\\1)\tカ\t1\t1\n"
                    "\\_+\t_\t1\t1\n"
                    "\\d++\t#\t1\t1\n",
                    false}});
    HD_CHECK_EQ(Run(*f, "見える 消える かか __ 12３"), "見えル 消エル カか _ #");
}

HD_TEST(FilterReplacementSyntax) {
    auto f = Make({{Kind::PreFilter, "p.txt", "(a)(b)?(c)\t[$3$1|${2}|\\1|$&|$$|$x|$9|\\x41\\t]\t1\t1\n", false}});
    HD_CHECK_EQ(Run(*f, "ac"), "[ca||a|ac|$|$x||A\t]");
    // After an empty match the next match may not be empty at the same place.
    f = Make({{Kind::PreFilter, "p.txt", "x*\t-\t1\t1\n", false}});
    HD_CHECK_EQ(Run(*f, "axb"), "-a--b-");
}

HD_TEST(FilterRejectsBadRegex) {
    std::vector<Error> errors;
    auto f = Make({{Kind::PreFilter, "p.txt", "x\ty\t1\t0\n(a\tc\t1\t1\n", false}}, &errors);
    HD_CHECK_EQ(Shown(errors), "p.txt:2: regex rejected (missing closing parenthesis at 2): (a");
    HD_CHECK_EQ(Run(*f, "ax"), "ay");
    errors.clear();
    Make({{Kind::SkipLayer, "s.txt", "PRE\t1\t[a\n", false}}, &errors);
    HD_CHECK_EQ(Shown(errors), "s.txt:1: regex rejected (missing terminating ] for character class at 2): [a");
}

HD_TEST(FilterApplicationLogs) {
    std::vector<Error> errors;
    auto f = Make({{Kind::PreFilter, "p.txt", "// note\nx\tX\t2\t0\ny\tY\t1\t2\nz\tZ\t3\t1\n(\tbad\t4\t1\n", false},
                   {Kind::PostFilter, "o.txt", "q\tQ\t1\t0\n", false}},
                  &errors);
    HD_REQUIRE(errors.size() == 1);
    HD_CHECK_EQ(errors[0].file, "p.txt");
    HD_CHECK_EQ(errors[0].line, 5);
    std::vector<std::string> logs;
    const LogSink sink{&logs, [](void* context, std::string_view line) {
                           static_cast<std::vector<std::string>*>(context)->emplace_back(line);
                       }};
    auto toggles = f->Start();
    std::string line = "xy";
    f->Apply(Stage::Pre, line, toggles, sink);
    HD_CHECK_EQ(line, "XY");
    HD_REQUIRE(logs.size() == 2);
    HD_CHECK_EQ(logs[0], "PreFilter : [p.txt:2] | y | Y | 1 | 2\n");
    HD_CHECK_EQ(logs[1], "PreFilter : [p.txt:1] | x | X | 2 | 0\n");
    line = "q";
    f->Apply(Stage::Post, line, toggles, sink);
    HD_CHECK_EQ(line, "Q");
    HD_REQUIRE(logs.size() == 3);
    HD_CHECK_EQ(logs[2], "PostFIlter : [o.txt:0] | q | Q | 1 | 0\n");
    line = "xy";
    f->Apply(Stage::Pre, line, toggles);
    HD_CHECK_EQ(line, "XY");
    HD_CHECK_EQ(logs.size(), std::size_t{3});
    logs.clear();
    line = "none";
    f->Apply(Stage::Pre, line, toggles, sink);
    HD_CHECK(logs.empty());
    line.clear();
    f->Apply(Stage::Pre, line, toggles, sink);
    HD_CHECK(logs.empty());
}

HD_TEST(FilterToggleLogs) {
    auto f = Make({{Kind::PreFilter, "p.txt",
                    "_TGL\t_DECR:T_\t_OFF\n"
                    "_TGL\t_ON:T_\tgo\t_MATCH_CHECK\t1\t0\n"
                    "_TGL\t_CONN:T_\tx\tX\t2\t0\n"
                    "_TGL\t_OFF:T_\tstop\t_MATCH_CHECK\t3\t1\n",
                    false},
                   {Kind::PostFilter, "o.txt", "_TGL\t_CONN:T_\ty\tY\t1\t0\n", false}});
    std::vector<std::string> logs;
    const LogSink sink{&logs, [](void* context, std::string_view line) {
                           static_cast<std::vector<std::string>*>(context)->emplace_back(line);
                       }};
    auto toggles = f->Start();
    std::string line = "go x stop";
    f->Apply(Stage::Pre, line, toggles, sink);
    HD_CHECK_EQ(line, "go X stop");
    HD_REQUIRE(logs.size() == 5);
    HD_CHECK_EQ(logs[0], "PreFilter : [p.txt:1] | go | _MATCH_CHECK | 1 | 0\n");
    HD_CHECK_EQ(logs[1], "토글 변수 T_의 상태가 ON으로 변경.\n");
    HD_CHECK_EQ(logs[2], "PreFilter : [p.txt:2] | x | X | 2 | 0\n");
    HD_CHECK_EQ(logs[3], "PreFilter : [p.txt:3] | stop | _MATCH_CHECK | 3 | 1\n");
    HD_CHECK_EQ(logs[4], "토글 변수 T_의 상태가 OFF로 변경.\n");
    logs.clear();
    line = "y";
    f->Apply(Stage::Post, line, toggles, sink);
    HD_CHECK_EQ(line, "y");
    HD_CHECK(logs.empty());
    toggles = f->Start();
    line = "go";
    f->Apply(Stage::Pre, line, toggles, sink);
    HD_CHECK_EQ(line, "go");
    logs.clear();
    line = "y";
    f->Apply(Stage::Post, line, toggles, sink);
    HD_CHECK_EQ(line, "Y");
    HD_REQUIRE(logs.size() == 1);
    HD_CHECK_EQ(logs[0], "PostFIlter : [o.txt:0] | y | Y | 1 | 0\n");
}

HD_TEST(FilterSkipLayerLogs) {
    auto f = Make({{Kind::PreFilter, "p.txt", "a\tA\t5\t0\nb\tB\t6\t0\n", false},
                   {Kind::SkipLayer, "s.txt", "PRE\t0005\tneeded\nPRE\t6\tneeded\n", false}});
    std::vector<std::string> logs;
    const LogSink sink{&logs, [](void* context, std::string_view line) {
                           static_cast<std::vector<std::string>*>(context)->emplace_back(line);
                       }};
    auto toggles = f->Start();
    std::string line = "a b";
    f->Apply(Stage::Pre, line, toggles, sink);
    HD_CHECK_EQ(line, "a b");
    HD_REQUIRE(logs.size() == 1);
    HD_CHECK_EQ(logs[0], "PreSkipLayer : 0005, 6\n");
}

HD_TEST(FilterUserDict) {
    std::vector<Error> errors;
    auto f = Make({{Kind::UserDict, "UserDict_a.txt",
                    "//c\nポチ\t포치#N\t1\tVHn\t//dog\n一応\t일단\t0\n門前\t문 앞#M\n"
                    "あいうえおかきくけこさしすせそた\t길다\t1\nx\ty//no second field\n",
                    false}},
                  &errors);
    HD_CHECK_EQ(Shown(errors), "UserDict_a.txt:5: the key is longer than 30 bytes in CP932");
    const dict::Dict* user = f->User();
    HD_REQUIRE(user != nullptr);
    HD_CHECK_EQ(user->Len(), std::size_t{3});
    auto show = [&](const char* key) {
        std::string s;
        for (const dict::Record& r : user->Records(key)) {
            s += std::string(r.target) + " " + r.codes->partText + " " + std::string(r.codes->sem.Text()) + " " +
                 std::string(r.codes->dict.Text());
        }
        return s;
    };
    HD_CHECK_EQ(show("ポチ"), "포치#N I110 VHn GN");
    HD_CHECK_EQ(show("一応"), "일단 A9D0  GN");
    HD_CHECK_EQ(show("門前"), "문 앞#M I110  GN");
}

HD_TEST(EhndText) {
    const std::string utf16("\xFF\xFE" "a\0\t\0\x10\xAC\r\0\n\0", 12);
    HD_CHECK_EQ(EhndText(utf16), "a\t감\n");
    HD_CHECK_EQ(EhndText("\xEF\xBB\xBFx\r\ny\rz"), "x\ny\rz");
}

HD_TEST(EhndFolderOrder) {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) /
                     ("hdtrans-ehnd-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    HD_REQUIRE(std::filesystem::create_directory(dir, ec));
    for (const char* name : {"PreFilter_b.txt", "prefilter_A.txt", "PreFilter_A.txt.bak", "PostFilter.txt",
                             "UserDict#name.txt", "UserDict#1.txt", "SkipLayer.TXT", "other.txt"}) {
        std::ofstream(dir / name) << "x\n";
    }
    std::vector<Source> sources;
    std::vector<Error> errors;
    HD_CHECK(ReadEhndFolder(dir.string(), sources, errors));
    std::string names;
    for (const Source& s : sources) names += s.name + " ";
    HD_CHECK_EQ(names, "prefilter_A.txt PreFilter_b.txt PostFilter.txt SkipLayer.TXT UserDict#1.txt UserDict#name.txt ");
    std::filesystem::remove_all(dir, ec);
    HD_CHECK(!ReadEhndFolder((dir / "missing").string(), sources, errors));
}

HD_TEST(FilterMatcher) {
    Matcher m;
    const int he = m.Add(u"he"), she = m.Add(u"she"), his = m.Add(u"his"), hers = m.Add(u"hers");
    HD_CHECK_EQ(m.Add(u"she"), she);
    m.Build();
    std::vector<uint32_t> seen(m.Size());
    m.Scan(u"ushers", seen, 1);
    HD_CHECK(seen[static_cast<std::size_t>(he)] == 1 && seen[static_cast<std::size_t>(she)] == 1);
    HD_CHECK(seen[static_cast<std::size_t>(hers)] == 1 && seen[static_cast<std::size_t>(his)] == 0);
    m.Scan(u"this", seen, 2);
    HD_CHECK(seen[static_cast<std::size_t>(his)] == 2 && seen[static_cast<std::size_t>(he)] == 1);
}

HD_TEST(FilterKeepSpacesAfterNumbers) {
    auto keep = [](std::string_view source, std::string filtered) {
        KeepSpacesAfterNumbers(source, filtered);
        return filtered;
    };
    // A space the pre-filter removed after a digit or a Latin letter comes back.
    HD_CHECK_EQ(keep("ステージ3 ボス戦", "ステージ3ボス戦"), "ステージ3 ボス戦");
    HD_CHECK_EQ(keep("1　レベル上がった", "⌒。1レベル上がった"), "⌒。1　レベル上がった");
    HD_CHECK_EQ(keep("HP ゲージ", "HPゲージ"), "HP ゲージ");
    // Other removed spaces, and spaces the filter kept, stay as the filter left them.
    HD_CHECK_EQ(keep("補助能力値が １ ずつ", "補助能力値が１ずつ"), "補助能力値が１ずつ");
    HD_CHECK_EQ(keep("ステージ3 ボス戦", "ステージ3 ボス戦"), "ステージ3 ボス戦");
    // A character the filter rewrote leaves the space out.
    HD_CHECK_EQ(keep("3 ボス", "3魔"), "3魔");
    // A full-width digit the filter wrote in ASCII is the same character.
    HD_CHECK_EQ(keep("１２　月", "12月。⌒"), "12　月。⌒");
    // A long line aligns on its common start and end.
    std::string longSource(3000, 'x'), longFiltered(3000, 'x');
    longSource += "ステージ3 ボス戦";
    longFiltered += "ステージ3ボス戦";
    HD_CHECK_EQ(keep(longSource, longFiltered), longSource);
    // Unchanged text stays.
    HD_CHECK_EQ(keep("3 ボス", "3 ボス"), "3 ボス");
}
