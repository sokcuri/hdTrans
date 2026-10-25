// A name-layer phrase reads as a title only standing apart from the sentence.
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

namespace {

std::string Translate(const hdTrans::dict::Snapshot& snap, const std::string& input) {
    const auto generated = hdTrans::internal::NewGenerated(snap);
    const auto out = hdTrans::internal::TranslateDocument(snap, *generated, {&input, 1}, {}, {});
    return out.size() == 1 ? out[0] : std::string();
}

}  // namespace

HD_TEST(word_name_layer_phrase_stands_apart) {
    hdTrans::dict::File base, names;
    base.name = "base.tsv";
    base.text = "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
                "星\t별\tN110\t\t\tGN\t0\t1\t\n"
                "に\t에\tP200\t\t\tGN\t0\t2\t\n"
                "願い\t소원\tN110\t\t\tGN\t0\t3\t\n"
                "を\t(을)를\tP200\t\t\tGN\t0\t4\t\n"
                "込め\t담&A6@\tM427\t\t\tGN\t0\t5\t\n"
                "た\t0았다\tS500\t\t\tGN\t0\t6\t\n"
                "。\t.\tN600\t\tMk\tGN\t0\t7\t\n"
                "ポケット\t주머니\tN110\t\t\tGN\t0\t8\t\n"
                "の\t의\tP200\t\t\tGN\t0\t9\t\n"
                "中\t안\tN110\t\t\tGN\t0\t10\t\n";
    names.name = "names.tsv";
    names.text = "#! layer=names\nkey\tko\tpos\tsem\tgram\tdict\tid\n"
                 "星に願いを\t별은 내 가슴에\tN130\tQWK\t\tGN\t11\n"
                 "ポケットの中に\t주머니 속에\tN130\tQWK\t\tGN\t12\n";
    const auto loaded = hdTrans::dict::Load({base, names});
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    // Running on into a verb, the phrase is words; standing alone it is the title.
    HD_CHECK(Translate(*loaded.snapshot, "星に願いを込めた。").find("가슴") == std::string::npos);
    HD_CHECK(Translate(*loaded.snapshot, "星に願いを").find("가슴") != std::string::npos);
    // A key joined by の only is no phrase.
    HD_CHECK(Translate(*loaded.snapshot, "ポケットの中に").find("속에") != std::string::npos);
}
