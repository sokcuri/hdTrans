// Per-line speaker cues, builtin whole-line rows and confirmed-name lookup caching.
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/names/document.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

const dict::Snapshot* Dictionary() {
    static const dict::LoadResult loaded = dict::Load({
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "光\t히카루\tN130\tQPN\tWb\tGN\t1\n"
         "睦月\t무쓰키\tN130\tQCT\t\tGN\t2\n"
         "時雨\t늦가을 비\tN110\tUMe\t\tGN\t8\n"
         "ランチ\t런치\tN110\tVFd\t\tGN\t9\n"
         "タイム\t타임\tN110\tUMe\t\tGN\t10\n"
         "武士\t무사\tN110\tUAqVHn\t\tGN\t11\n"},
        {0, "names.tsv",
         "#! layer=names\n"
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "紗英\t사에\tN130\tQPN\tWb\tGN\t3\n"
         "柚葉\t유즈하\tN130\tQPN\tWb\tGN\t4\n"
         "水野柚葉\t미즈노 유즈하\tN130\tQPN\t\tGN\t5\n"
         "時雨\t시구레\tN130\tQPN\tWb\tGN\t6\n"
         "睦月\t무츠키\tN130\tQPN\tWb\tGN\t7\n"
         "武士\t타케시\tN130\tQPN\tWb\tGN\t12\n"
         "鎌倉\t카마쿠라\tN130\tQPNVHn\tWa\tGN\t13\n"
         "学園長\t학원장\tN130\tQPNVHn\tWb\tGN\t14\n"
         "ゆき\t유키\tN130\tQPNVHn\tWb\tGN\t15\n"
         "マチルダ\t마틸다\tN130\tQPNVHn\tWb\tGN\t16\n"},
    });
    return loaded.snapshot.get();
}

}  // namespace

HD_TEST(names_cued_names) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
        {"紗英「おはよう」", {"紗英"}},
        {"柚葉さん", {}},
        {"睦月です。", {}},
        {"時雨、行くよ！", {"時雨"}},
        {"そう言って「おはよう」", {}},
        {"水野柚葉「こんにちは」", {"水野柚葉"}},
        {"photo\n紗英「あ」", {"紗英"}},
    };
    for (const auto& [text, want] : cases) {
        HD_CHECK(pipeline::CuedNames(*snap, text) == want);
    }
}

HD_TEST(names_whole_line_rows) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::string> lines = {"つづきから"};
    auto doc = pipeline::BuildDocument(*snap, lines);
    HD_REQUIRE(doc != nullptr);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"つづきから", "이어하기"},
        {"  つづきから  ", "  이어하기  "},
        {"つづきから!", ""},
        {"こんにちは", ""},
    };
    for (const auto& [line, want] : cases) {
        const pipeline::LineFacts facts = pipeline::ReadLine(*doc, line);
        HD_CHECK(facts.wholeLine.has_value() == !want.empty());
        if (facts.wholeLine) HD_CHECK(*facts.wholeLine == want);
    }
}

HD_TEST(names_confirmed_cache) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::string> lines = {"光さん、", "光さん、", "光さん、"};
    auto doc = pipeline::BuildDocument(*snap, lines);
    HD_REQUIRE(doc != nullptr);
    HD_REQUIRE(doc->accepted.contains("光"));
    const std::vector<std::vector<std::string>> skips = {{}, {"光"}, {"光", "未登録"}};
    std::vector<pipeline::LineLookup> saved;
    for (const auto& skip : skips) {
        pipeline::LineFacts facts = pipeline::ReadLine(*doc, lines[0]);
        facts.skip = skip;
        const pipeline::LineLookup first = pipeline::LookupOf(*doc, facts);
        const pipeline::LineLookup again = pipeline::LookupOf(*doc, facts);
        HD_REQUIRE(first.confirmed != nullptr);
        HD_CHECK(first.confirmed == again.confirmed);
        HD_CHECK(first.plain == again.plain);
        HD_CHECK(pipeline::ContextOf(*doc, first).lookup.confirmed == first.confirmed);
        if (skip.empty()) HD_CHECK(first.confirmed == &doc->accepted);
        for (const std::string& name : doc->accepted) {
            const bool omitted = std::find(skip.begin(), skip.end(), name) != skip.end();
            HD_CHECK_EQ(first.confirmed->contains(name), !omitted);
        }
        saved.push_back(first);
    }
    HD_CHECK(saved[0].confirmed->contains("光"));
    HD_CHECK(!saved[1].confirmed->contains("光"));
    HD_CHECK(!saved[2].confirmed->contains("光"));
    HD_CHECK(doc->accepted.contains("光"));
}

HD_TEST(document_skip_cache_length_prefix) {
    const auto loaded = dict::Load({{0, "base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    auto doc = pipeline::BuildDocument(*loaded.snapshot, {});
    HD_REQUIRE(doc != nullptr);
    pipeline::LineFacts one, two;
    one.skip = {"a\tb"};
    two.skip = {"a", "b"};
    const auto first = pipeline::LookupOf(*doc, one);
    const auto second = pipeline::LookupOf(*doc, two);
    HD_CHECK(first.plain != second.plain);
    HD_CHECK_EQ(doc->plainByKey.size(), std::size_t{2});
    HD_CHECK(pipeline::LookupOf(*doc, one).plain == first.plain);
}

HD_TEST(document_verse_lines) {
    const std::vector<std::string> lines = {"窓を開けて", "風を感じて", "駅まで行こう", "", "もう遅いよ。私に任せて",
                                            "明日また会おう", "星をめざして", "山田太郎"};
    const std::set<std::string> verse = pipeline::internal::VerseLines(lines);
    HD_CHECK_EQ(verse.size(), std::size_t{2});
    HD_CHECK(verse.count("窓を開けて") == 1);
    HD_CHECK(verse.count("風を感じて") == 1);
    // An empty next line and the end of the text end the verse.
    const std::vector<std::string> ends = {"私を信じて", "", "明日も会おう", "空を見上げて"};
    HD_CHECK(pipeline::internal::VerseLines(ends).empty());
    const std::vector<std::string> spaces = {"私を信じて", "　 "};
    HD_CHECK(pipeline::internal::VerseLines(spaces).empty());
}

// The proposal: the persons the cues of the document confirm, a common word with hon (spec LEX-8152), nothing for
// a text without names, and none of the shapes that are no names (spec LEX-8154).
HD_TEST(document_proposed_names) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const auto propose = [&](std::vector<std::string> lines) { return pipeline::ProposeNames(*snap, lines); };
    HD_CHECK_EQ(propose({"時雨「おはよう」", "時雨「うん」", "時雨は笑った。"}), std::string("時雨\t시구레\tgiven\thon\n"));
    HD_CHECK_EQ(propose({"政府は二十日、新たな経済対策を閣議決定した。", "春の宵、桜の下で人々が集まった。",
                         "彼は栞を挟んで本を閉じた。", "ランチタイムには屋上で弁当を食べた。"}),
                std::string());
    const std::string kana = propose({"みおちゃん、おはよう。", "やはりみおちゃんだ。", "今度はみおちゃんの番だ。", "みおちゃん「うん」"});
    HD_CHECK(kana.find("りみお") == std::string::npos && kana.find("はみお") == std::string::npos);
    HD_CHECK_EQ(propose({"ランチタイムさんが来た。", "ランチタイムさんの話。", "ランチタイムは終わった。"}), std::string());
    HD_CHECK_EQ(propose({"鎌倉武士は強い。", "武士は食わねど高楊枝。", "鎌倉武士さんが来た。"}), std::string());
    HD_CHECK_EQ(propose({"学園長「おはよう」", "学園長「うん」", "学園長様が来た。"}), std::string());
    HD_CHECK_EQ(propose({"みゆきちゃん「おはよう」", "ゆきちゃんが来た。", "みゆきちゃんとゆきちゃん。"}),
                std::string("みゆき\t미유키\tgiven\nゆき\t유키\tgiven\n"));
    HD_CHECK_EQ(propose({"マチルダ「おはよう」", "マチルダ「うん」", "マチルダさんが来た。", "マチルダーっ！"}),
                std::string("マチルダ\t마틸다\tgiven\n"));
}
