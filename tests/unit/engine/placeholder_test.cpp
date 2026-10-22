#include <memory>
#include <string>

#include "core/engine/driver.h"
#include "hdtest.h"
#include "rule_fixture.h"

using namespace hdTrans;

namespace {

// ○ has a numeral sense written as ○, the way the shipped base dictionary has it.
const std::string kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "○\t○\tN600\t\tMk\tGN\t\t1\n"
    "○\t○\tN600\t\tNd\tGN\t3043\t2\n"
    "月\t달\tN110\tXXXUMe\t\tGN\t1062\t3\n"
    "月\t월\tN110\tUMe\tZ7CtMo\tGN\t1063\t4\n"
    "月\t월\tN110\t\tCtOn\tGN\t1901\t11\n"
    "日\t날\tN110\tXXX\tTm\tGN\t1114\t5\n"
    "日\t일\tN110\tUMe\tZ7J5Ct\tGN\t1115\t6\n"
    "第\t제\tN110\tXXX\tZ6Ha\tGN\t\t7\n"
    "話\t이야기\tN110\tXXXUTs\tHd\tGN\t\t8\n"
    "話\t화\tN110\t\tCt\tGN\t\t9\n"
    "さん\t씨\tN110\tVHn\tTu\tGN\t327\t10\n";

std::string Translate(const dict::Snapshot& snap, const std::string& line) {
    const auto generated = internal::NewGenerated(snap);
    internal::Memory memory;
    const std::string lines[] = {line};
    return internal::TranslateDocument(snap, *generated, lines, memory, {})[0];
}

}  // namespace

// A mask that stands for an unknown number or name is written as itself, spaced like a
// number before a counter; a symbol that is no numeral keeps no space.
HD_TEST(engine_placeholder_masks) {
    const auto loaded = dict::Load({{0, "base.tsv", fixture::WithRuleBodies(kRows)}});
    HD_REQUIRE(loaded.errors.empty() && loaded.snapshot != nullptr);
    const dict::Snapshot& snap = *loaded.snapshot;
    HD_CHECK_EQ(Translate(snap, "○月○日"), std::string("○월 ○일"));
    HD_CHECK_EQ(Translate(snap, "第○話"), std::string("제○화"));
    HD_CHECK_EQ(Translate(snap, "◯◯さん"), std::string("◯◯ 씨"));
    HD_CHECK_EQ(Translate(snap, "●月●日"), std::string("●월●일"));
}
