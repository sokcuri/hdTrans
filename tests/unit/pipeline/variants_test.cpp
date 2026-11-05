#include <memory>
#include <string>

#include "core/dict/snapshot.h"
#include "core/pipeline/sound.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::pipeline;

// A long mark inside a run of hiragana reads as the vowel where a longer key holds it and the kana
// around it read as words; a key read only as a noun must start the run and end before a particle
// or no more kana; a mark that draws out the end of a word stays (LEX-4822).
HD_TEST(pipeline_inner_long_vowels) {
    dict::File f;
    f.name = "base.tsv";
    f.text = "key\tko\tpos\tsem\tgram\tdict\tid\n"
             "しょうもな\t시시$AB@\tH7U0\t\t\tGN\t1\n"
             "かいちょう\t회장\tN110\t\t\tGN\t2\n"
             "さんそう\t산장\tN110\t\t\tGN\t3\n"
             "ぎょらい\t어뢰\tN110\t\t\tGN\t4\n"
             "みるな\t보지 마\tE300\t\t\tGN\t5\n"
             "みるなあ\t보지 마\tE300\t\t\tGN\t6\n";
    const auto snap = dict::Load({f}).snapshot;
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    HD_CHECK_EQ(internal::RewriteSpellingVariants("しょーもない。", ctx, *snap), std::string("しょうもない。"));
    HD_CHECK_EQ(internal::RewriteSpellingVariants("かいちょーだ。", ctx, *snap), std::string("かいちょうだ。"));
    HD_CHECK_EQ(internal::RewriteSpellingVariants("さんそーぎょらい", ctx, *snap), std::string("さんそーぎょらい"));
    HD_CHECK_EQ(internal::RewriteSpellingVariants("みるなー！", ctx, *snap), std::string("みるなー！"));
    HD_CHECK_EQ(internal::RewriteSpellingVariants("かいちょーくん", ctx, *snap), std::string("かいちょーくん"));
}
