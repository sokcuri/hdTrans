// Work titles of the name layer read with the marks of the text, not their own.
#include <string>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "hdtest.h"

namespace {

std::string Translate(const hdTrans::dict::Snapshot& snap, const std::string& input) {
    const auto generated = hdTrans::internal::NewGenerated(snap);
    const auto out = hdTrans::internal::TranslateDocument(snap, *generated, {&input, 1}, {}, {});
    return out.size() == 1 ? out[0] : std::string();
}

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

HD_TEST(word_title_marks_follow_the_text) {
    hdTrans::dict::File base, names;
    base.name = "base.tsv";
    base.text = "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n"
                "を\t(을)를\tP200\t\t\tGN\t0\t1\t\n"
                "見\t보&A6@\tM427\t\t\tGN\t0\t2\t\n"
                "た\t0았다\tS500\t\t\tGN\t0\t3\t\n"
                "。\t.\tN600\t\tMk\tGN\t0\t4\t\n"
                "ろく\t여섯\tN110\t\tNd\tGN\t0\t5\t\n";
    names.name = "names.tsv";
    names.text = "#! layer=names\nkey\tko\tpos\tsem\tgram\tdict\tid\n"
                 "かえるる!\t개구리별!\tN130\tQWK\t\tGN\t11\n"
                 "ぼっちざろっく\t봇치 더 록!\tN130\tQWK\t\tGN\t12\n"
                 "ぼっち・ざ・ろっく\t봇치 더 록\tN130\tQWK\t\tGN\t13\n";
    const auto loaded = hdTrans::dict::Load({base, names});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto& snap = *loaded.snapshot;
    // A key written with ! reads the text with either width of the mark, which comes from the text.
    HD_CHECK(Has(Translate(snap, "かえるる！"), "개구리별!"));
    HD_CHECK(Has(Translate(snap, "かえるる!"), "개구리별!"));
    HD_CHECK(!Has(Translate(snap, "かえるる！"), "!!"));
    // Without the mark the short kana key is no title.
    HD_CHECK(!Has(Translate(snap, "かえるる"), "개구리별"));
    // A long kana title no words spell reads inside a sentence, without a mark the text lacks.
    const std::string sentence = Translate(snap, "ぼっちざろっくを見た。");
    HD_CHECK(Has(sentence, "봇치 더 록"));
    HD_CHECK(!Has(sentence, "!"));
    // The sound passes leave the っ of a title alone.
    HD_CHECK(Has(Translate(snap, "ぼっち・ざ・ろっく"), "봇치 더 록"));
}
