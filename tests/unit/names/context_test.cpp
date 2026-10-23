// Per-line speaker cues, builtin whole-line rows and confirmed-name lookup caching.
#include <algorithm>
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
         "睦月\t무쓰키\tN130\tQCT\t\tGN\t2\n"},
        {0, "names.tsv",
         "#! layer=names\n"
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "紗英\t사에\tN130\tQPN\tWb\tGN\t3\n"
         "柚葉\t유즈하\tN130\tQPN\tWb\tGN\t4\n"
         "水野柚葉\t미즈노 유즈하\tN130\tQPN\t\tGN\t5\n"
         "時雨\t시구레\tN130\tQPN\tWb\tGN\t6\n"
         "睦月\t무츠키\tN130\tQPN\tWb\tGN\t7\n"},
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
