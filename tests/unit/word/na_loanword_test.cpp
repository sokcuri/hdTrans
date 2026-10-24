// な after a katakana loanword whose row takes な (Dd) is 한, after another noun 인.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

namespace {

std::string Translate(const hdTrans::dict::Snapshot& snap, const std::string& input) {
    const auto generated = hdTrans::internal::NewGenerated(snap);
    hdTrans::internal::Memory memory;
    const auto out = hdTrans::internal::TranslateDocument(snap, *generated, {&input, 1}, memory, {}, {});
    return out.size() == 1 ? out[0] : std::string();
}

}  // namespace

HD_TEST(word_dd_loanword_reads_na_as_han) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
         "ディープ\t딥\tN110\t\tDd\tGN\t0\t1\t\n"
         "ゲーム\t게임\tN110\t\t\tGN\t0\t2\t\n"
         "な\t인\tP200\t\t\tGN\t0\t3\t\n"
         "話\t이야기\tN110\t\t\tGN\t0\t4\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    HD_CHECK_EQ(Translate(*loaded.snapshot, "ディープな話"), std::string("딥한 이야기"));
    HD_CHECK_EQ(Translate(*loaded.snapshot, "ゲームな話"), std::string("게임인 이야기"));
}
