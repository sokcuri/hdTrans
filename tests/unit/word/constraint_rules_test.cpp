// The constraint rules on the candidates of a lookup: the words they fix, and words like
// them that they leave alone.
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/tokenizer/token.h"
#include "core/word/constraint_rules.h"
#include "core/word/worker.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

const dict::Snapshot* Dictionary() {
    static const dict::LoadResult loaded = dict::Load({
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "ハル\t헐\tN110\t\t\tGN\t1\n"
         "アヤメ\t붓꽃\tN110\tVPt\t\tGN\t2\n"
         "ネコ\t고양이\tN110\tQAA\t\tGN\t3\n"
         "バカ\t바보\tN110\t\t\tGN\t5\n"
         "翼\t날개\tN110\tXXXVBp\t\tGN\t6\n"
         "翼\t츠바사\tN110\tQPN\tWb\tGN\t7\n"
         "妹\t여동생\tN110\tVHn\t\tGN\t8\n"
         "妹\t이모토\tN110\tQPN\tWb\tGN\t9\n"
         "続\t계속되&AA@\tM418\t\t\tGN\t10\n"
         "続\t츠즈키\tN130\tQPN\tWa\tGN\t11\n"
         "太郎\t타로\tN130\tQPN\tWb\tGN\t12\n"
         "太郎\t타로\tN130\tQCTVPl\t\tGN\t13\n"
         "空\t하늘\tN110\tVPl\t\tGN\t14\n"
         "空\t소라\tN130\tQPN\tWb\tGN\t15\n"
         "君\t너\tN120\tVHn\t\tGN\t16\n"
         "君\t군\tN110\tVHn\t\tGN\t17\n"
         "殿\t전\tN110\t\tOn\tGN\t18\n"
         "様\t님\tN110\t\t\tGN\t19\n"
         "さん\t씨\tN110\t\t\tGN\t20\n"
         "と\t(와)과\tP200\t\t\tGN\t21\n"
         "と\t(이)라고\tP200\t\t\tGN\t22\n"
         "が\t(이)가\tP200\t\t\tGN\t23\n"
         "ドン\t쾅\tN110\t\t\tGN\t24\n"
         "音\t소리\tN110\t\t\tGN\t25\n"
         "猫\t고양이\tN110\tQAA\t\tGN\t26\n"
         "曰く\t왈\tN110\t\t\tGN\t27\n"
         "曰く\t듣자 하니\tE300\t\t\tGN\t28\n"
         "つい\t무심코\tE311\tXXX\t\tGN\t29\n"
         "つい\t불과\tE311\t\t\tGN\t30\n"
         "先\t앞\tN110\tXXX\t\tGN\t31\n"
         "先\t곳\tN110\tVPl\t\tGN\t32\n"
         "先に\t먼저\tM300\tUMe\t\tGN\t33\n"
         "先に\t곳에\tM300\t\t\tGN\t34\n"
         "先に\t앞에\tM300\t\t\tGN\t35\n"
         "こう\t이렇게\tN311\t\t\tGN\t36\n"
         "こう\t이러%A1@\tG8U0\t\t\tGN\t37\n"
         "一人立ち\t독립\tI110\t\tHd\tGN\t38\n"
         "一人\t한 사람\tN110\t\t\tGN\t41\n"
         "君と\t군과\tN110\tQPN\t\tGN\t42\n"
         "君と\t너와\tN110\t\t\tGN\t43\n"
         "上が\t&E6@\tM428\tXXX\t\tGN\t39\n"
         "あお\t$C2@\tH7U0\t\t\tGN\t60\n"
         "しろ\t$AD@\tH7U0\t\t\tGN\t61\n"
         "そら\t하늘\tN110\tVPl\t\tGN\t62\n"
         "つづ\t계속되&AA@\tM421\t\t\tGN\t64\n"
         "あに\t형\tN110\tVHn\t\tGN\t65\n"
         "ねこ\t고양이\tN110\tQAA\t\tGN\t66\n"
         "うみ\t바다\tN110\tVPl\t\tGN\t67\n"
         "りん\t린\tN110\t\t\tGN\t68\n"
         "あお\t우러러&AY@\tM452\t\t\tGN\t69\n"
         "し\t&HT@\tM41<\t\t\tGN\t77\n"
         "め\t째\tQ110\t\t\tGN\t78\n"
         "め\t\tQ110\t\t\tGN\t79\n"
         "め\t녀석\tQ110\t\tHa\tGN\t44\n"
         "わ\t원\tQ110\t\t\tGN\t80\n"
         "わ\t와\tQ110\t\t\tGN\t81\n"
         "わ\t\tQ110\t\t\tGN\t82\n"
         "ん\t응\tQ110\t\t\tGN\t83\n"
         "ん\t\tQ110\t\t\tGN\t84\n"
         "そう言った\t그런\tA9D0\t\t\tGN\t85\n"
         "そう言\t그렇게 말&AV@\tM420\t\t\tGN\t86\n"
         "いた\t아$AA@\tH7N0\t\t\tGN\t87\n"
         "いた\t손상&C6@\tM425\t\t\tGN\t88\n"
         "い\t&HN@\tM417\t\t\tGN\t89\n"},
        {0, "names.tsv",
         "#! layer=names\n"
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "ハル\t하루\tN130\tQPNVHn\tWb\tGN\t31\n"
         "アヤメ\t아야메\tN130\tQPNVHn\tWb\tGN\t32\n"
         "ネコ\t네코\tN130\tQPNVHn\tWb\tGN\t33\n"
         "猫\t네코\tN130\tQPNVHn\tWb\tGN\t35\n"
         "あお\t아오\tN130\tQPNVHn\tWb\tGN\t70\n"
         "しろ\t시로\tN130\tQPNVHn\tWb\tGN\t71\n"
         "そら\t소라\tN130\tQPNVHn\tWb\tGN\t72\n"
         "つづ\t츠즈\tN130\tQPNVHn\tWb\tGN\t73\n"
         "あに\t아니\tN130\tQPNVHn\tWb\tGN\t74\n"
         "ねこ\t네코\tN130\tQPNVHn\tWb\tGN\t75\n"
         "りん\t린\tN130\tQPNVHn\tWb\tGN\t76\n"},
    });
    return loaded.snapshot.get();
}

// The Korean of the records with source in the text-path lookup of query, the end of
// surface; a name is marked with "!". before is the text before the chunk.
std::string Senses(std::string_view surface, std::string_view query, std::string_view source,
                   std::string_view before = {}) {
    const dict::Snapshot* snap = Dictionary();
    if (snap == nullptr) return "<no dictionary>";
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Settings settings;
    settings.text = true;
    settings.before = before;
    word::Worker w(tokens, *snap, dict::LookupContext{}, settings, arena, word::LookupObserver{});
    w.transformSurface = surface;
    std::string out;
    for (const word::Record* r = w.DoLookup(query, word::kModeGeneral).records; r != nullptr; r = r->child) {
        if (r->source != source) continue;
        if (!out.empty()) out += " ";
        out += r->target;
        if (r->user != nullptr && r->user->name) out += "!";
    }
    return out;
}

struct Case {
    std::string_view surface, query, source, want;
};

void CheckCases(std::initializer_list<Case> cases, std::string_view before = {}) {
    for (const Case& c : cases) HD_CHECK_EQ(Senses(c.surface, c.query, c.source, before), std::string(c.want));
}

}  // namespace

HD_TEST(constraint_rules_table) {
    HD_CHECK_EQ(word::ConstraintRules().size(), size_t{22});
    for (const word::ConstraintRule& r : word::ConstraintRules()) {
        HD_CHECK(!r.id.empty());
        HD_CHECK(!r.note.empty());
        HD_CHECK((r.word == word::ConstraintWord::Source) == !r.source.empty());
        HD_CHECK((r.reject == word::ConstraintReject::OtherTargets || r.reject == word::ConstraintReject::Target) ==
                 !r.target.empty());
    }
}

// A katakana word before an honorific becomes the name layer's person reading when no sense
// is a person and none sounds like its kana. An animal, a word the name layer lacks, a word
// without an honorific and the inside of a katakana run stay. (A sense written as the kana
// is left to the name layer's own addressed names.)
HD_TEST(constraint_honorific_katakana) {
    CheckCases({
        {"ハル君、", "ハル君、", "ハル", "하루!"},
        {"ハルさんが", "ハルさんが", "ハル", "하루!"},
        {"アヤメ様", "アヤメ様", "アヤメ", "아야메!"},
        {"ハルが", "ハルが", "ハル", "헐"},
        {"ネコさんが", "ネコさんが", "ネコ", "고양이"},
        {"バカさんが", "バカさんが", "バカ", "바보"},
        {"ミハルさん", "ハルさん", "ハル", "헐"},
    });
}

HD_TEST(constraint_honorific_hiragana) {
    CheckCases({
        {"あおくんが", "あおくんが", "あお", "아오!"},
        {"しろちゃん、", "しろちゃん、", "しろ", "시로!"},
        {"あお君は", "あお君は", "あお", "아오!"},
        {"そら君は", "そら君は", "そら", "하늘"},
        {"あおちゃんと", "あおちゃんと", "あお", "$C2@ 우러러&AY@"},
        {"あおくんない", "あおくんない", "あお", "$C2@ 우러러&AY@"},
        {"あおくんくん", "あおくんくん", "あお", "$C2@ 우러러&AY@"},
        {"しろたん", "しろたん", "しろ", "$AD@"},
        {"しろちゃんと", "しろちゃんと", "しろ", "$AD@"},
        {"あおく染まる", "あおく染まる", "あお", "$C2@ 우러러&AY@"},
        {"あおさんが", "あおさんが", "あお", "$C2@ 우러러&AY@"},
        {"あおたん", "あおたん", "あお", "$C2@ 우러러&AY@"},
        {"みあおくん", "あおくん", "あお", "$C2@ 우러러&AY@"},
        {"真あおくん", "あおくん", "あお", "$C2@ 우러러&AY@"},
        {"つづくんだ", "つづくんだ", "つづ", "계속되&AA@"},
        {"あにくん", "あにくん", "あに", "형"},
        {"ねこちゃん", "ねこちゃん", "ねこ", "고양이"},
        {"うみくん", "うみくん", "うみ", "바다"},
        {"りんくん", "りんくん", "りん", "린"},
    });
}

// A kanji word before くん, 君, ちゃん or たん is its name sense when it has one and no
// person sense; before another honorific, when its other senses are places. A verb as
// long, a person noun, a common noun before さん, a word without a name sense, a word
// after お and a word inside a kanji run stay.
HD_TEST(constraint_honorific_kanji) {
    CheckCases({
        {"翼くんが", "翼くんが", "翼", "츠바사!"},
        {"太郎殿、", "太郎殿、", "太郎", "타로!"},
        {"妹くんが", "妹くんが", "妹", "여동생 이모토"},
        {"続くんだ", "続くんだ", "続", "계속되&AA@ 츠즈키"},
        {"空さんが", "空さんが", "空", "하늘 소라"},
        {"猫ちゃん", "猫ちゃん", "猫", "고양이"},
        {"お空さん", "空さん", "空", "하늘 소라"},
        {"大空くん", "空くん", "空", "하늘 소라"},
        {"翼が", "翼が", "翼", "날개 츠바사"},
    });
}

// 君 right after katakana is the suffix, not the pronoun; 君 elsewhere keeps both.
HD_TEST(constraint_kimi_after_katakana) {
    CheckCases({
        {"バカ君が", "君が", "君", "군"},
        {"君が", "君が", "君", "너 군"},
        {"空君が", "君が", "君", "너 군"},
    });
}

// 君 and 君と right after a mask character (a masked name) are the suffix 군, not the pronoun;
// after other text they keep both.
HD_TEST(constraint_kimi_after_mask) {
    CheckCases({{"君が", "君が", "君", "군"}, {"君と", "君と", "君と", "군과"}}, "△");
    CheckCases({{"君が", "君が", "君", "군"}, {"君と", "君と", "君と", "군과"}}, "○");
    CheckCases({{"君が", "君が", "君", "너 군"}, {"君と", "君と", "君と", "군과 너와"}}, "、");
}

// と after ！ or ？ quotes, also past a space or at the start of a chunk after one; と
// elsewhere keeps its senses.
HD_TEST(constraint_quote_after_exclamation) {
    CheckCases({
        {"ドン！と音が", "と音が", "と", "(이)라고"},
        {"ドン！　と音が", "と音が", "と", "(이)라고"},
        {"ドンと音が", "と音が", "と", "(와)과 (이)라고"},
        {"大事！とって", "とって", "と", "(와)과 (이)라고"},
        {"うわ！ととと", "ととと", "と", "(와)과 (이)라고"},
    });
    CheckCases({{"と音が", "と音が", "と", "(이)라고"}}, "！");
    CheckCases({{"と音が", "と音が", "と", "(와)과 (이)라고"}}, "、");
}

// 曰く that opens its line before 、 (past spaces and an opening bracket) is the adverb that
// introduces hearsay; after a word, without 、 or after other text it keeps its noun sense.
HD_TEST(constraint_iwaku_line_opening) {
    CheckCases({
        {"曰く、彼は", "曰く、彼は", "曰く", "듣자 하니"},
        {"　曰く、彼は", "曰く、彼は", "曰く", "듣자 하니"},
        {"「曰く、彼は", "曰く、彼は", "曰く", "듣자 하니"},
        {"先生曰く、彼は", "曰く、彼は", "曰く", "왈"},
        {"曰くによれば", "曰くによれば", "曰く", "왈"},
        {"国、曰く“騎士”", "曰く“騎士”", "曰く", "왈"},
    });
    CheckCases({{"曰く、彼は", "曰く、彼は", "曰く", "왈"}}, "。");
}

// つい before a numeral and 前 (past a quantity word, in its token or the next) is 불과; つい
// before a numeral without 前, or before other words, keeps its senses.
HD_TEST(constraint_tsui_numeral_ago) {
    CheckCases({
        {"つい三時間ほど前", "つい三時間ほど前", "つい", "불과"},
        {"つい数分前に", "つい数分前に", "つい", "불과"},
        {"つい二度寝した", "つい二度寝した", "つい", "무심코 불과"},
        {"つい3回も言った", "つい3回も言った", "つい", "무심코 불과"},
        {"つい笑った", "つい笑った", "つい", "무심코 불과"},
        {"つい三、四日前", "つい三、四日前", "つい", "무심코 불과"},
    });
}

// 先 and 先に right after a verb in た, だ or る are the place (곳, 곳에); 先に after この, その
// or あの is 앞에. まだ先, 先に行く and 先 after a noun keep their senses.
HD_TEST(constraint_saki_after_verb) {
    CheckCases({
        {"嫁いだ先は", "先は", "先", "곳"},
        {"飛ばされる先は", "先は", "先", "곳"},
        {"まだ先の話", "先の話", "先", "앞 곳"},
        {"道の先", "先", "先", "앞 곳"},
        {"戻した先に", "先に", "先に", "곳에"},
        {"この先に", "先に", "先に", "앞에"},
        {"先に行く", "先に行く", "先に", "먼저 곳에 앞에"},
    });
}

// こう before なる is the adverb 이렇게 (こうなるんだ 이렇게 되는 거야); こうなのだ and こうなるほど keep both.
HD_TEST(constraint_kou_naru) {
    CheckCases({
        {"こうなるんだ", "こうなるんだ", "こう", "이렇게"},
        {"こうなのだ", "こうなのだ", "こう", "이러%A1@ 이렇게"},
        {"こうなるほど", "こうなるほど", "こう", "이러%A1@ 이렇게"},
    });
}

// 一人立ち before 上が is 一人 and 立ち上がり, so its own sense goes; without 上が it stays.
HD_TEST(constraint_tachidachi_before_agaru) {
    CheckCases({
        {"一人立ち上がり、", "一人立ち上がり、", "一人立ち", ""},
        {"一人立ち上がる", "一人立ち上がる", "一人立ち", ""},
        {"一人立ちする", "一人立ちする", "一人立ち", "독립"},
        {"一人立ち", "一人立ち", "一人立ち", "독립"},
        {"一人立ち、", "一人立ち、", "一人立ち", "독립"},
    });
}

// いた right after て or で is the past of いる unless an adjective ending, 致す or 頂く
// follows it (見ていたい, でいたします, ていただく).
HD_TEST(constraint_ita_after_te) {
    CheckCases({
        {"捨てていたんです", "いたんです", "いた", ""},
        {"見ていたな", "いたな", "いた", ""},
        {"見ていたい", "いたい", "いた", "아$AA@ 손상&C6@"},
        {"部屋でいたします", "いたします", "いた", "아$AA@ 손상&C6@"},
        {"肩がいたんだ", "いたんだ", "いた", "아$AA@ 손상&C6@"},
    });
}

// そう言った before a kanji or katakana word is the adnominal 그런 (そう言った意味); elsewhere
// its sense goes so that the verb そう言 and its ending read it (そう言った。, そう言ったが).
HD_TEST(constraint_sou_itta_predicate) {
    CheckCases({
        {"そう言った意味", "そう言った意味", "そう言った", "그런"},
        {"そう言ったミス", "そう言ったミス", "そう言った", "그런"},
        {"そう言った。", "そう言った。", "そう言った", ""},
        {"そう言ったが", "そう言ったが", "そう言った", ""},
        {"そう言った、", "そう言った、", "そう言った", ""},
    });
}

// め and わ with no numeral among the three characters before them are not the ordinal 째 and
// the 원 of a sum (化け物め, おわっ); after a quantity (三回め, 5人め) they keep every sense.
// わ after a predicate is the sentence-final particle, not the interjection 와 (だったわ).
HD_TEST(constraint_counter_without_number) {
    CheckCases({
        {"化け物め", "め", "め", "녀석"},
        {"ごめ", "め", "め", "녀석"},
        {"三回め", "め", "め", "째  녀석"},
        {"5人め", "め", "め", "째  녀석"},
        {"何度め", "め", "め", "째  녀석"},
        {"おわ", "わ", "わ", "와 "},
        {"五わ", "わ", "わ", "원 와 "},
        {"誤算だったわ", "わ", "わ", ""},
        {"よろしいですわ", "わ", "わ", ""},
    });
}

// め standing apart (after a space, punctuation or nothing) is not 녀석.
HD_TEST(constraint_me_yatsu_detached) {
    CheckCases({
        {"ケンタめ", "め", "め", "녀석"},
        {"名前: め", "め", "め", ""},
        {"、め", "め", "め", ""},
        {"め", "め", "め", ""},
    });
}

// ん right after a verb in た, だ or る is not the interjection 응 (終わったん？); ん opening a
// line keeps it.
HD_TEST(constraint_n_after_verb) {
    CheckCases({
        {"終わったん", "ん", "ん", ""},
        {"しとるん", "ん", "ん", ""},
        {"ん", "ん", "ん", "응 "},
    });
}
