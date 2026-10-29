#include <string>

#include "core/engine/driver.h"
#include "hdtest.h"
#include "rule_fixture.h"

// Copies of a word written apart keep the space between them (tokenizer::CopySpace): the word stage
// reads the pieces between the spaces as the copies (CopyEnd), a katakana run is not merged across
// one (BuildNmTail), and compose keeps the space and splits a word that reads one copy said as often
// (MarkCopySpaces).
HD_TEST(engine_copies_written_apart) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", fixture::WithRuleBodies(
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "毎日\t매일\tN110\t\t\tGN\t0\t1\n"
        "毎日毎日\t매일매일\tE300\t\t\tGN\t0\t2\n"
        "もし\t만약\tN110\t\t\tGN\t0\t3\n"
        "もしもし\t여보세요\tA9H1\t\t\tGN\t0\t4\n"
        "これ\t이것\tN120\t\t\tGN\t0\t5\n"
        "これこれ\t이러이러\tN110\t\t\tGN\t0\t6\n")}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const std::string input[] = {"毎日 毎日", "毎日毎日", "もしもし もしもし", "これ これ", "ハオ ハオ", "ハオハオ", "もしもし", "もしもしもしもし"};
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, input, {}, {});
    HD_REQUIRE(out.size() == 8);
    HD_CHECK_EQ(out[6], std::string("여보세요"));
    HD_CHECK_EQ(out[7], std::string("만약 만약 만약 만약"));  // written together: the shortest unit
    HD_CHECK_EQ(out[0], std::string("매일 매일"));
    HD_CHECK_EQ(out[1], std::string("매일매일"));
    HD_CHECK_EQ(out[2], std::string("여보세요 여보세요"));
    HD_CHECK_EQ(out[3], std::string("이러이러"));  // 이러 is no reading of これ
    HD_CHECK_EQ(out[4], std::string("하오 하오"));
    HD_CHECK_EQ(out[5], std::string("하오하오"));
}
