#include <filesystem>
#include <fstream>
#include <string>

#include "core/engine.h"
#include "core/engine/wrapped.h"
#include "hdtest.h"

using namespace hdTrans;
using internal::WrapEdges;
using internal::WrapWord;

namespace {

// 高い as 높다, as in speech_test.cpp.
const std::string kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "高\t$D3@\tH7N0\t\t\tGN\t0\t1\n"
    "い\t1다\tT500\t\t\tGN\t0\t2\n";

WrapEdges Edges(std::initializer_list<WrapWord> words) {
    WrapEdges e;
    for (const WrapWord& w : words) {
        if (!e.first) e.first = w;
        if (e.last && e.last->detail != output::DetailClass::None) e.predicate = true;
        e.last = w;
        e.words++;
    }
    return e;
}

WrapWord Word(std::string surface, std::string code) {
    WrapWord w;
    w.surface = std::move(surface);
    w.code = std::move(code);
    return w;
}

WrapWord Verb(std::string surface, std::string ending, std::string koEnding, int kind = 0) {
    WrapWord w = Word(std::move(surface), "437");
    w.detail = output::DetailClass::Yougo;
    w.ending = std::move(ending);
    w.koEnding = std::move(koEnding);
    w.kind = kind;
    return w;
}

}  // namespace

HD_TEST(engine_wrapped_goes_on) {
    using internal::GoesOn;
    const WrapEdges particle = Edges({Word("駅", "110"), Word("で", "221")});
    const WrapEdges verb = Edges({Verb("会", "った", "5다", 6)});
    HD_CHECK(GoesOn("駅で", particle, "会った。", verb));
    // A case particle that is the whole line (でも) is a conjunction.
    HD_CHECK(!GoesOn("でも", Edges({Word("でも", "200")}), "会った。", verb));
    // Conditional, connective and 連用 forms.
    HD_CHECK(GoesOn("行かなければ", Edges({Verb("行", "かなければ", "6야 한다", 8)}), "ならない。", verb));
    HD_CHECK(GoesOn("読んで", Edges({Verb("読", "んで", "1고", 7)}), "会った。", verb));
    HD_CHECK(GoesOn("食べ", Edges({Verb("食べ", "", "1고", 8)}), "させられた。", verb));
    HD_CHECK(GoesOn("降り", Edges({Verb("降", "り", "6", 10)}), "会った。", verb));
    // A て form read as a request stays, but not before an auxiliary.
    const WrapEdges request = Edges({Verb("待", "って", "6", 7)});
    HD_CHECK(!GoesOn("待って", request, "会った。", verb));
    HD_CHECK(GoesOn("待って", request, "いる。", Edges({Verb("い", "る", "4다")})));
    HD_CHECK(GoesOn("待って", request, "ください", Edges({Word("ください", "300")})));
    // A predicate before a noun that needs one.
    HD_CHECK(GoesOn("会った", verb, "ことにした", Edges({Word("ことに", "469")})));
    HD_CHECK(!GoesOn("会った", verb, "ところで、", Edges({Word("ところで", "330")})));
    // A comma goes on; a sentence end, a bracket or an ellipsis does not.
    HD_CHECK(GoesOn("それで、", Edges({Word("それで", "331")}), "会った。", verb));
    HD_CHECK(!GoesOn("駅で。", particle, "会った。", verb));
    HD_CHECK(!GoesOn("「駅で」", particle, "会った。", verb));
    HD_CHECK(!GoesOn("駅で……", particle, "会った。", verb));
    // A blank line, and a line that starts an item of its own.
    HD_CHECK(!GoesOn("駅で", particle, "", verb));
    HD_CHECK(!GoesOn("", particle, "会った。", verb));
    HD_CHECK(!GoesOn("駅で", particle, "・会った", verb));
    HD_CHECK(!GoesOn("駅で", particle, "「会った」", verb));
    HD_CHECK(!GoesOn("駅で", particle, "２）会った", verb));
    HD_CHECK(!GoesOn("駅で", particle, "結論：会った", verb));
    HD_CHECK(!GoesOn("駅で&#CS", particle, "会った。", verb));
    // A complete line.
    HD_CHECK(!GoesOn("行きます", Edges({Verb("行", "きます", "7니다", 10)}), "会った。", verb));
}

HD_TEST(engine_wrapped_split) {
    using internal::SplitAtBreaks;
    const auto two = SplitAtBreaks("내일까지 가야 &#CS한다.", 2);
    HD_REQUIRE(two.size() == 2);
    HD_CHECK_EQ(two[0], "내일까지 가야");
    HD_CHECK_EQ(two[1], "한다.");
    HD_CHECK(SplitAtBreaks("내일까지 가야 한다.", 2).empty());
    HD_CHECK(SplitAtBreaks("가야 &#CS한다&#CS.", 2).empty());
    HD_CHECK(SplitAtBreaks("먹었다&#CS", 2).empty());
    HD_CHECK(SplitAtBreaks("&#CS 먹었다", 2).empty());
}

// Context lines give their cues and are not returned; nothing is kept from one call to the next.
HD_TEST(engine_wrapped_context) {
    const auto folder = std::filesystem::temp_directory_path() / "hdtrans-wrapped-context";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "base.tsv") << kRows;
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.Open({folder.string()}));
    Session session(engine);
    session.SetSpeech(Speech::Soft);
    HD_CHECK_EQ(session.Translate("高い。」"), "높다.」");
    HD_CHECK_EQ(session.Translate("高い。」", "「高い。"), "높아.」");  // the quote the context opened
    HD_CHECK_EQ(session.Translate("高い。」", "「高い。"), "높아.」");
    HD_CHECK_EQ(session.Translate("高い。」"), "높다.」");
    HD_CHECK_EQ(session.Translate("高い\n高い", "「高い」"), "높다\n높다");  // a document of 「」 narrates
    HD_CHECK_EQ(session.Translate("高い"), "높아");
    // Wrapped, a line's narration comes from its own text, not from the other lines.
    session.SetWrapped(true);
    HD_CHECK_EQ(session.Translate("高い", "「高い」"), "높아");
    HD_CHECK_EQ(session.Translate("「高い」\n高い"), "「높아」\n높아");
    HD_CHECK_EQ(session.Translate("高い。」", "「高い。"), "높아.」");
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
}
