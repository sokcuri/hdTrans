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

// An utterance in hiragana that writes its long vowels with ー reads as the interjection or adverb
// that the vowels spell; a tail of ー after a sentence particle, a vowel kana drawn out and a run after
// other text stay (SND-30).
HD_TEST(pipeline_drawn_utterances) {
    dict::File f;
    f.name = "base.tsv";
    f.text = "key\tko\tpos\tsem\tgram\tdict\tid\n"
             "まあ\t뭐\tE300\t\t\tGN\t1\n"
             "まあまあ\t그저\tE311\t\t\tGN\t2\n"
             "ま\t사이\tN110\t\t\tGN\t3\n"
             "そよ\t살랑거&AA@\tM422\t\t\tGN\t4\n"
             "いいなあ\t좋겠다\tE300\t\t\tGN\t5\n";
    const auto snap = dict::Load({f}).snapshot;
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「まー、ちょっと」", ctx, *snap), std::string("「まあ、ちょっと」"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("まーまー……", ctx, *snap), std::string("まあまあ……"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「そーよ」", ctx, *snap), std::string("「そうよ」"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「あ、そーよそーよ！」", ctx, *snap), std::string("「あ、そうよそうよ！」"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「いいなー」", ctx, *snap), std::string("「いいなー」"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「あーあ」", ctx, *snap), std::string("「あーあ」"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("今まー、", ctx, *snap), std::string("今まー、"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「まーね」", ctx, *snap), std::string("「まーね」"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「どーも」", ctx, *snap), std::string("「どーも」"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「ほほー」", ctx, *snap), std::string("「ほほー」"));
    HD_CHECK_EQ(internal::SpellDrawnUtterances("「くーん」", ctx, *snap), std::string("「くーん」"));
}

// A loanword in hiragana whose long mark at the end became ～, or that a shout's っ closes, is the key in
// katakana (SND-30).
HD_TEST(pipeline_hiragana_loan_drawn_end) {
    dict::File f;
    f.name = "base.tsv";
    f.text = "key\tko\tpos\tsem\tgram\tdict\tid\n"
             "ラジャー\t로저\tN110\t\t\tGN\t1\n";
    const auto snap = dict::Load({f}).snapshot;
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    HD_CHECK_EQ(internal::RewriteSpellingVariants("「らじゃ～」", ctx, *snap), std::string("「ラジャー」"));
    HD_CHECK_EQ(internal::RewriteSpellingVariants("「らじゃーっ！」", ctx, *snap), std::string("「ラジャーっ！」"));
    HD_CHECK_EQ(internal::RewriteSpellingVariants("「らじゃ」", ctx, *snap), std::string("「らじゃ」"));
}
