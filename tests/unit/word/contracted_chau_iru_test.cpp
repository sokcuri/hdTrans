// The て form of ちゃう before いる with its い dropped (寝ちゃってた) is done and past, with the
// ending after it or the empty one; where its た starts a longer word (寝ちゃってたまに) it is no
// such form and the kana stay read.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_contracted_chau_iru) {
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n";
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         header +
             "寝\t&AK@\tM427\t\t\tGN\t0\t1\t\n"
             "けど\t5지만\tS560\t\t\tGN\t0\t2\t\n"
             "V\t5다\tS560\t\t\tGN\t0\t3\t\n"
             "った\t6 버렸다\tS5A0\t\t\tGN\t0\t4\t\n"
             "って\t6서\tS5A0\t\t\tGN\t0\t5\t\n"
             "たまに\t이따금\tE300\t\t\tGN\t0\t6\t\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* input;
        const char* want;
    };
    const Case cases[] = {
        {"寝ちゃってたけど", "자 버렸지만"},
        {"寝ちゃってた", "자 버렸다"},
        {"寝ちゃってたまに", "자서 이따금"},
    };
    for (const Case& c : cases) {
        const std::string input = c.input;
        hdTrans::internal::Memory memory;
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, {&input, 1}, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0], std::string(c.want));
    }
}
