// A person name whose last kanji is an ichidan verb stem keeps it before a person suffix
// (里見たち, 里見さん), which no verb form starts.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

HD_TEST(word_name_keeps_verb_stem_before_person_suffix) {
    const std::vector<hdTrans::dict::File> files = {
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
         "里見\t사토미\tN130\tQPN\tWa\tGN\t0\t1\n"
         "里\t마을\tN110\t\t\tGN\t0\t2\n"
         "見\t&AY@\tM437\t\t\tGN\t0\t3\n"
         "たち\t들\tN110\tVHn\tTu\tGN\t0\t7\n"},
    };
    const auto loaded = hdTrans::dict::Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    struct Case {
        const char* in;
        const char* head;  // the start of the output
    };
    const Case cases[] = {
        {"里見たち", "사토미"},
        {"里見さん", "사토미"},
    };
    for (const Case& c : cases) {
        hdTrans::internal::Memory memory;
        const std::vector<std::string> lines = {c.in};
        const auto out = hdTrans::internal::TranslateDocument(*loaded.snapshot, *generated, lines, memory, {}, {});
        HD_REQUIRE(out.size() == 1);
        HD_CHECK_EQ(out[0].substr(0, std::string(c.head).size()), std::string(c.head));
    }
}
