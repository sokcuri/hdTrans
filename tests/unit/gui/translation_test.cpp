#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

#include "core/engine.h"
#include "gui/translation.h"
#include "hdtest.h"

namespace {

using hdTrans::gui::OutputMode;
using hdTrans::gui::TranslateText;
using hdTrans::gui::TranslationOptions;

constexpr OutputMode kModes[] = {OutputMode::Translation, OutputMode::Interleaved, OutputMode::Spaced};

std::string LocalDictionary() {
    return (std::filesystem::path(__FILE__).parent_path().parent_path() /
            "engine" / "testdata" / "session").string();
}

struct FilterFolder {
    std::filesystem::path path;

    explicit FilterFolder(std::string_view rows) {
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error);
        if (error) return;
        const auto candidate = root / ("hdtrans-gui-translation-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(candidate, error)) return;
        path = candidate;
        std::ofstream file(path / "post.tsv", std::ios::binary);
        file << "#! layer=postfilter\n# local filter fixture\n" << rows;
        HD_CHECK(file.good());
    }

    ~FilterFolder() {
        std::error_code error;
        if (!path.empty()) std::filesystem::remove_all(path, error);
    }
};

}  // namespace

HD_TEST(gui_translation_defaults) {
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    const std::atomic<bool> cancelled{false};
    const TranslationOptions options;
    HD_CHECK(options.output == OutputMode::Translation);
    HD_CHECK(options.skipBlankLines);
    HD_CHECK(options.wholeDocument);
    HD_CHECK(options.cg);
    HD_CHECK(!options.misread);
    HD_CHECK_EQ(TranslateText(engine, "学生\nASCII\n", options, cancelled), "학생님\nASCII\n");
    HD_CHECK(!cancelled.load());
}

// The speech level of a dialogue line follows options.speechLevels (speech_levels of the settings).
HD_TEST(gui_translation_speech_levels) {
    FilterFolder fixture("");
    HD_REQUIRE(!fixture.path.empty());
    {
        std::ofstream dictionary(fixture.path / "base.tsv", std::ios::binary);
        dictionary << "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
                      "綺麗\t예%C9@\tG8U0\t\t\tGN\t0\t1\n"
                      "花\t꽃\tN110\t\t\tGN\t0\t2\n"
                      "が\t이\tP200\t\t\tGN\t0\t3\n";
        HD_REQUIRE(dictionary.good());
    }
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({fixture.path.string()}));
    const std::atomic<bool> cancelled{false};
    for (const bool wholeDocument : {false, true}) {
        TranslationOptions options;
        options.wholeDocument = wholeDocument;
        HD_CHECK(options.speechLevels == hdTrans::SpeechLevels::Auto);
        HD_CHECK_EQ(TranslateText(engine, "「花が綺麗」", options, cancelled), "「꽃이 예뻐」");
        options.speechLevels = hdTrans::SpeechLevels::Keep;
        HD_CHECK_EQ(TranslateText(engine, "「花が綺麗」", options, cancelled), "「꽃이 예쁘다」");
    }
}

HD_TEST(gui_translation_session_flags) {
    FilterFolder fixture("");
    HD_REQUIRE(!fixture.path.empty());
    {
        std::ofstream dictionary(fixture.path / "base.tsv", std::ios::binary);
        dictionary << "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
                      "ちょうど\t꼭\tN110\tXXX\t\tGN\t0\t1\n"
                      "ちょうど\t마침\tN110\t\t\tGN\t0\t2\n";
        HD_REQUIRE(dictionary.good());
    }
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({fixture.path.string()}));
    const std::atomic<bool> cancelled{false};
    const std::string source = "ちょうど\nちょうど\n";
    for (const bool wholeDocument : {false, true}) {
        TranslationOptions options{OutputMode::Translation, true, wholeDocument};
        HD_CHECK(options.cg);
        HD_CHECK(!options.misread);
        for (const bool cg : {false, true}) {
            for (const bool misread : {false, true}) {
                options.cg = cg;
                options.misread = misread;
                hdTrans::Session session(engine, false, cg, misread);
                const std::string expected = session.Translate(source);
                HD_CHECK_EQ(expected, cg ? "마침\n마침\n" : "꼭\n꼭\n");
                HD_CHECK_EQ(TranslateText(engine, source, options, cancelled), expected);
            }
        }
    }
}

HD_TEST(gui_translation_modes_and_line_endings) {
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    const std::atomic<bool> cancelled{false};
    const struct {
        const char* source;
        bool skipBlankLines;
        const char* expected[3];
    } cases[] = {
        {"", false, {"", "", ""}},
        {"", true, {"", "", ""}},
        {"\n", false, {"\n", "\n\n", "\n\n"}},
        {"\n\n", false, {"\n\n", "\n\n\n\n", "\n\n\n\n\n"}},
        {"\n\n", true, {"\n\n", "\n\n", "\n\n"}},
        {"学生", false, {"학생님", "学生\n학생님", "学生\n학생님"}},
        {"学生\n", true, {"학생님\n", "学生\n학생님\n", "学生\n학생님\n"}},
        {"学生\n\n", false, {"학생님\n\n", "学生\n학생님\n\n\n", "学生\n학생님\n\n\n\n"}},
        {"学生\n\n", true, {"학생님\n\n", "学生\n학생님\n\n", "学生\n학생님\n\n"}},
        {"学生\nASCII", true,
         {"학생님\nASCII", "学生\n학생님\nASCII\nASCII", "学生\n학생님\n\nASCII\nASCII"}},
        {"学生\nASCII\n", false,
         {"학생님\nASCII\n", "学生\n학생님\nASCII\nASCII\n", "学生\n학생님\n\nASCII\nASCII\n"}},
        {"学生\n\nASCII", true,
         {"학생님\n\nASCII", "学生\n학생님\n\nASCII\nASCII", "学生\n학생님\n\n\nASCII\nASCII"}},
        {"学生\n\n学生\n", true,
         {"학생님\n\n학생님\n", "学生\n학생님\n\n学生\n학생님\n", "学生\n학생님\n\n\n学生\n학생님\n"}},
        {"\n学生\n\nASCII\n\n", false,
         {"\n학생님\n\nASCII\n\n", "\n\n学生\n학생님\n\n\nASCII\nASCII\n\n\n",
          "\n\n\n学生\n학생님\n\n\n\n\nASCII\nASCII\n\n\n\n"}},
        {"\n学生\n\nASCII\n\n", true,
         {"\n학생님\n\nASCII\n\n", "\n学生\n학생님\n\nASCII\nASCII\n\n",
          "\n学生\n학생님\n\n\nASCII\nASCII\n\n"}},
        {"한글\nASCII", false,
         {"한글\nASCII", "한글\n한글\nASCII\nASCII", "한글\n한글\n\nASCII\nASCII"}},
    };
    for (const bool wholeDocument : {false, true}) {
        for (const auto& test : cases) {
            for (std::size_t mode = 0; mode < std::size(kModes); ++mode) {
                const TranslationOptions options{kModes[mode], test.skipBlankLines, wholeDocument};
                HD_CHECK_EQ(TranslateText(engine, test.source, options, cancelled), test.expected[mode]);
            }
        }
    }
}

HD_TEST(gui_translation_whole_document_context) {
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    const std::atomic<bool> cancelled{false};
    const std::string cue = hdTrans::Session(engine).Translate("「あたし」");
    const std::string source = "お兄様！\n「あたし」";
    hdTrans::Session document(engine);
    const std::string expectedDocument = document.Translate(source);
    HD_CHECK_EQ(expectedDocument, "오빠!\n" + cue);
    for (const bool wholeDocument : {false, true}) {
        const std::string first = wholeDocument ? "오빠!" : "오라버니!";
        const std::string expected[] = {
            first + "\n" + cue,
            "お兄様！\n" + first + "\n「あたし」\n" + cue,
            "お兄様！\n" + first + "\n\n「あたし」\n" + cue,
        };
        for (std::size_t mode = 0; mode < std::size(kModes); ++mode) {
            const TranslationOptions options{kModes[mode], true, wholeDocument};
            HD_CHECK_EQ(TranslateText(engine, source, options, cancelled), expected[mode]);
        }
    }
}

HD_TEST(gui_translation_fresh_line_sessions) {
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    const std::atomic<bool> cancelled{false};
    const std::string cue = hdTrans::Session(engine).Translate("「あたし」");
    TranslationOptions options;
    options.wholeDocument = false;
    HD_CHECK_EQ(TranslateText(engine, "「あたし」\nお兄様！", options, cancelled), cue + "\n오라버니!");
    options.wholeDocument = true;
    HD_CHECK_EQ(TranslateText(engine, "「あたし」\nお兄様！", options, cancelled), cue + "\n오빠!");
    HD_CHECK_EQ(TranslateText(engine, "「あたし」", options, cancelled), cue);
    HD_CHECK_EQ(TranslateText(engine, "お兄様！", options, cancelled), "오라버니!");
}

HD_TEST(gui_translation_filter_expands_lines_without_losing_content) {
    FilterFolder filters("학생님\t첫째\\n둘째\t1\t1\n");
    HD_REQUIRE(!filters.path.empty());
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary(), filters.path.string()}));
    HD_REQUIRE(engine.FilterErrors().empty());
    const std::atomic<bool> cancelled{false};
    const struct {
        const char* source;
        bool skipBlankLines;
        const char* translation;
        const char* interleaved;
    } cases[] = {
        {"学生", true, "첫째\n둘째", "学生\n첫째\n둘째"},
        {"学生\nASCII\n", true, "첫째\n둘째\nASCII\n", "学生\nASCII\n첫째\n둘째\nASCII\n"},
        {"\n学生\n\nASCII\n", true, "\n첫째\n둘째\n\nASCII\n",
         "\n学生\n\nASCII\n\n첫째\n둘째\n\nASCII\n"},
        {"\n学生\n\nASCII\n", false, "\n첫째\n둘째\n\nASCII\n",
         "\n学生\n\nASCII\n\n첫째\n둘째\n\nASCII\n"},
    };
    for (const bool wholeDocument : {false, true}) {
        for (const auto& test : cases) {
            for (const auto mode : kModes) {
                const TranslationOptions options{mode, test.skipBlankLines, wholeDocument};
                HD_CHECK_EQ(TranslateText(engine, test.source, options, cancelled),
                            mode == OutputMode::Translation ? test.translation : test.interleaved);
            }
        }
    }
}

HD_TEST(gui_translation_filter_removes_lines_without_losing_source) {
    FilterFolder filters("학생님\t\t1\t1\n");
    HD_REQUIRE(!filters.path.empty());
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary(), filters.path.string()}));
    HD_REQUIRE(engine.FilterErrors().empty());
    const std::atomic<bool> cancelled{false};
    for (const bool wholeDocument : {false, true}) {
        for (const bool skipBlankLines : {false, true}) {
            for (const auto mode : kModes) {
                const TranslationOptions options{mode, skipBlankLines, wholeDocument};
                HD_CHECK_EQ(TranslateText(engine, "学生", options, cancelled),
                            mode == OutputMode::Translation ? "" : "学生\n");
            }
        }
    }
}

HD_TEST(gui_translation_filter_generated_blank_lines_and_final_newline) {
    FilterFolder filters("학생님\t\\n생성\\n\t1\t1\n");
    HD_REQUIRE(!filters.path.empty());
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary(), filters.path.string()}));
    HD_REQUIRE(engine.FilterErrors().empty());
    const std::atomic<bool> cancelled{false};
    for (const bool wholeDocument : {false, true}) {
        for (const bool skipBlankLines : {false, true}) {
            for (const auto mode : kModes) {
                const TranslationOptions options{mode, skipBlankLines, wholeDocument};
                HD_CHECK_EQ(TranslateText(engine, "学生", options, cancelled),
                            mode == OutputMode::Translation ? "\n생성\n" : "学生\n\n생성\n");
            }
        }
    }
}

HD_TEST(gui_translation_cancellation_before_translation) {
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    std::atomic<bool> cancelled{true};
    for (const bool wholeDocument : {false, true}) {
        for (const bool skipBlankLines : {false, true}) {
            for (const auto mode : kModes) {
                const TranslationOptions options{mode, skipBlankLines, wholeDocument};
                HD_CHECK_EQ(TranslateText(engine, "学生\n\nASCII\n", options, cancelled), "");
                HD_CHECK_EQ(TranslateText(engine, "", options, cancelled), "");
            }
        }
    }
    HD_CHECK(cancelled.load());
    cancelled.store(false);
    HD_CHECK_EQ(TranslateText(engine, "学生", {}, cancelled), "학생님");
}

HD_TEST(gui_translation_cancellation_discards_results) {
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    std::string source;
    for (int i = 0; i < 4096; ++i) source += "学生\n";
    for (const bool wholeDocument : {false, true}) {
        std::atomic<bool> cancelled{false};
        std::thread stop([&cancelled] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            cancelled.store(true, std::memory_order_relaxed);
        });
        const TranslationOptions options{OutputMode::Translation, true, wholeDocument};
        const std::string output = TranslateText(engine, source, options, cancelled);
        stop.join();
        HD_CHECK(cancelled.load());
        HD_CHECK_EQ(output, "");
    }
}
