#include <string>
#include <string_view>

#include "core/engine/driver.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/worker.h"
#include "hdtest.h"

namespace {

std::string Translate(std::string_view text) {
    static const auto loaded = hdTrans::dict::Load({{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "効\t강&Mf@\tM411\t\tVa\tGN\t0\t1\n"
        "い\t&HN@\tM428\tXXX\tDaVa\tGN\t0\t2\n"
        "V\t1고\tS571\t\tJ1\tGN\t0\t3\n"
        "V\t5다\tS560\t\t\tGN\t0\t4\n"
        "V\t4다\tS500\t\t\tGN\t0\t5\n"
        "V\t0\tS500\t\t\tGN\t0\t6\n"}});
    HD_CHECK(loaded.errors.empty());
    if (loaded.snapshot == nullptr) return {};
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    const std::string line(text);
    const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&line, 1}, {}, {});
    return out.empty() ? std::string() : out[0];
}

}  // namespace

HD_TEST(word_adjective_target_reads_teiru_as_its_state) {
    // A verb whose Korean is an adjective reads ている as the adjective itself; the verb
    // form 4 of いる (있다) has no adjective form to move onto it.
    HD_CHECK_EQ(Translate("効いている。"), "강하다.");
}

HD_TEST(word_degree_suffix_keeps_negative_na_out_of_adnominal) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "多\t$D4@\tH7U0\t\t\tGN\t0\t1\n"
        "派手\t화려%A1@\tG8U0\t\t\tGN\t0\t2\n"
        "食べ\t&HU@\tM417\t\t\tGN\t0\t3\n"
        "マジ\t진짜\tN110\t\t\tGN\t0\t4\n"
        "すぎ\t%D1@\tG8U0\t\t\tGN\t0\t5\n"
        "すぎ\t지&A5@\tM457\t\t\tGN\t0\t6\n"
        "過ぎ\t지%D3@\tG8U0\t\t\tGN\t0\t7\n"
        "過ぎ\t지&A5@\tM417\t\t\tGN\t0\t8\n"
        "な\t4\tU500\t\t\tGN\t0\t9\n"
        "だ\t1다\tU500\t\t\tGN\t0\t10\n"
        "な\t$DH@\tH7U0\t\tXnJsAv\tGN\t0\t11\n"
        "い\t1다\tT500\t\t\tGN\t0\t12\n"
        "V\t1지\tS580\t\t\tGN\t0\t13\n"
        "ければ\t2면\tT500\t\t\tGN\t0\t14\n"
        "い\t&HN@\tM417\t\tDaVa\tGN\t0\t15\n"
        "V\t1고\tS5:0\t\t\tGN\t0\t16\n"
        "ないか\t1지 않는가\tU500\t\t\tGN\t0\t17\n"
        "と\t(이)라고\tP201\t\t\tGN\t0\t18\n"
        "な\t4 것\tU500\t\t\tGN\t0\t19\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    struct Case {
        std::string_view stem, tail;
        bool verb;
    };
    const Case cases[] = {
        {"多", "すぎない", true}, {"多", "過ぎない", true},
        {"派手", "すぎない", true}, {"多", "過ぎなければ", true},
        {"食べ", "すぎない", true}, {"多", "すぎだ", false},
        {"多", "過ぎな話", false}, {"マジ", "すぎない", false},
        {"多", "すぎないかと", false}, {"派手", "過ぎないかと", false},
        {"食べ", "すぎないかと", false},
    };
    for (const Case& c : cases) {
        const std::string input = std::string(c.stem) + std::string(c.tail);
        tokenizer::Tokens tokens;
        tokenizer::TokenizeSpans({{tokenizer::Piece{input, 0}}}, true, tokens);
        word::Arena arena;
        word::Settings settings = word::Settings::Reference();
        settings.text = true;
        word::Worker worker(tokens, *loaded.snapshot, {}, settings, arena, {});
        int last = 0;
        worker.Dispatch(0, last);
        const auto* item = worker.chain.head;
        while (item != nullptr && item->group->surface != "すぎ" && item->group->surface != "過ぎ") item = item->next;
        HD_REQUIRE(item != nullptr && !item->Code(0).empty());
        HD_CHECK_EQ(input + ":" + item->Code(0).front(), input + ":" + (c.verb ? '4' : '8'));
        if (c.tail.ends_with("かと")) {
            HD_CHECK_EQ(std::string(item->DirectSurface()), "ないか");
            HD_REQUIRE(item->next != nullptr);
            HD_CHECK_EQ(item->next->group->surface, "と");
        }
    }
}
