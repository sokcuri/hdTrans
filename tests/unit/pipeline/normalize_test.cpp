// Table tests of the normalising rewrites, one table per rewrite, and of the whole
// sequence of Normalize.
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline/normalize.h"
#include "core/pipeline/stage_text.h"
#include "hdtest.h"

using namespace std::literals;
using namespace hdTrans::pipeline;
using namespace hdTrans::pipeline::internal;

namespace {

struct Case {
    std::string_view in;
    std::string_view want;
};

template <size_t N>
void Check(std::string (*rewrite)(std::string_view), const Case (&cases)[N]) {
    for (const Case& c : cases) HD_CHECK_EQ(rewrite(c.in), std::string(c.want));
}

}  // namespace

HD_TEST(pipeline_normalize_forms) {
    const Case cases[] = {
        {"ｶﾞﾝﾊﾞﾚ"sv, "ガンバレ"sv},
        {"ﾊﾟﾝ ｳﾞｧｲｵﾘﾝ"sv, "パン ヴァイオリン"sv},
        {"ｱﾞ ﾞ ｶﾟ"sv, "ア゛ ゛ カ゜"sv},
        {"｢ｱｲｳ｣､ｦ･ｰ｡"sv, "｢アイウ｣、ヲ・ー。"sv},
        {"〜−‖¢£¬"sv, "～－∥￠￡￢"sv},
        {"ＡＢＣ①"sv, "ＡＢＣ①"sv},
        {"a\x00" "b\x01" "c\x02" "d\x03" "e"sv, "a b c d e"sv},
        {"頰𠮟﨑髙"sv, "頬叱崎高"sv},
        {"ﾍﾟﾎﾞﾍﾞ"sv, "ペボベ"sv},
        {"x\xffy"sv, "x\xffy"sv},
        {"ｶ"sv, "カ"sv},
    };
    Check(NormalizeForms, cases);
}

HD_TEST(pipeline_join_nasai) {
    const Case cases[] = {
        {"ごめん、なさい"sv, "ごめんなさい"sv},
        {"おやすみ…なさい！"sv, "おやすみなさい！"sv},
        {"お帰り、、なさい"sv, "お帰りなさい"sv},
        {"ごめんなさい"sv, "ごめんなさい"sv},
        {"ゴメン・・なさい"sv, "ゴメンなさい"sv},
        {"おかえり、なさ"sv, "おかえり、なさ"sv},
    };
    Check(JoinNasai, cases);
}

HD_TEST(pipeline_join_masu) {
    // \uE005 encloses the pause marks written back after the ending (kDrawlMark).
    const Case cases[] = {
        {"ござい、ます"sv, "ございます"sv},
        {"頑張り…ます！"sv, "頑張ります\uE005…\uE005！"sv},
        {"い、ました。"sv, "いました。"sv},
        {"い、ますか"sv, "い、ますか"sv},
        {"し、ません"sv, "しません"sv},
        {"き、ます"sv, "きます"sv},
        {"い、ますー"sv, "い、ますー"sv},
        {"い、ますッ"sv, "い、ますッ"sv},
        {"み、、ましたね"sv, "み、、ましたね"sv},
        {"いただきま…す。"sv, "いただきます\uE005…\uE005。"sv},
        {"いただきま・・・す"sv, "いただきます\uE005・・・\uE005"sv},
        {"いただきま・す"sv, "いただきます"sv},
        {"行きま―した！"sv, "行きました\uE005―\uE005！"sv},
        {"寝ま‥せん"sv, "寝ません\uE005‥\uE005"sv},
        {"行きまっす！"sv, "行きます！"sv},
        {"頑張りまーっす"sv, "頑張ります"sv},
        {"いただきまス"sv, "いただきます"sv},
        {"いただきまーす"sv, "いただきまーす"sv},
        {"行きまっした"sv, "行きまっした"sv},
        {"まっすぐ"sv, "まっすぐ"sv},
        {"いま…すぐ"sv, "いま…すぐ"sv},
        {"最低……で……す"sv, "最低……です\uE005……\uE005"sv},
        {"好き・・・で・・・す！"sv, "好き・・・です\uE005・・・\uE005！"sv},
        {"元気でっす"sv, "元気です"sv},
        {"そうで…しょう？"sv, "そうでしょう\uE005…\uE005？"sv},
        {"ここで…した"sv, "ここで…した"sv},
        {"で、すぐ"sv, "で、すぐ"sv},
        {"雨でーす"sv, "雨でーす"sv},
        // After a pause of …‥・―─ only, a sentence particle may follow the ending.
        {"行きま…すか？"sv, "行きますか\uE005…\uE005？"sv},
        {"鳴いて…ますね。"sv, "鳴いてますね\uE005…\uE005。"sv},
        {"守れ……ましたか？"sv, "守れましたか\uE005……\uE005？"sv},
        {"運びま…しょうか？"sv, "運びましょうか\uE005…\uE005？"sv},
        {"そうで……すよね"sv, "そうですよね\uE005……\uE005"sv},
        {"行き…ましょう！"sv, "行きましょう\uE005…\uE005！"sv},
        {"行きま…すかな"sv, "行きま…すかな"sv},
        {"そうで…すねた"sv, "そうで…すねた"sv},
    };
    Check(JoinMasu, cases);
}

HD_TEST(pipeline_join_paused_copula) {
    // \uE006 encloses the marks of a pause before です or でしょう, \uE007 before でした.
    const Case cases[] = {
        {"好き……です"sv, "好きです\uE006……\uE006"sv},
        {"静か…ですね。"sv, "静かですね\uE006…\uE006。"sv},
        {"いい…ですか？"sv, "いいですか\uE006…\uE006？"sv},
        {"二度寝…でしょうか？"sv, "二度寝でしょうか\uE006…\uE006？"sv},
        {"静か――でした。"sv, "静かでした\uE007――\uE007。"sv},
        {"カレー・・・です"sv, "カレーです\uE006・・・\uE006"sv},
        {"好き……です\uE005……\uE005"sv, "好きです\uE006……\uE006\uE005……\uE005"sv},
        // Other marks, a following kana, no word before, a case particle or a request form.
        {"元気、ですね"sv, "元気、ですね"sv},
        {"かい・です"sv, "かい・です"sv},
        {"そう…ですって"sv, "そう…ですって"sv},
        {"……ですが"sv, "……ですが"sv},
        {"私が…ですか？"sv, "私が…ですか？"sv},
        {"お任せください……です。"sv, "お任せください……です。"sv},
        {"2300…です"sv, "2300…です"sv},
    };
    Check(JoinPausedCopula, cases);
}

HD_TEST(pipeline_spoken_forms) {
    const Case cases[] = {
        {"マジでウザい！"sv, "マジでうざい！"sv},
        {"ダサすぎます！"sv, "ださすぎます！"sv},
        {"ズルい！"sv, "ズルい！"sv},
        {"取るに足らない安物じゃね？"sv, "取るに足らない安物じゃない？"sv},
        {"いや、意外とお子ちゃまじゃねえ？"sv, "いや、意外とお子ちゃまじゃない？"sv},
        {"それあれじゃねーの、"sv, "それあれじゃないの、"sv},
        {"最悪、男の方だけ挿げ変えりゃ良くね？"sv, "最悪、男の方だけ挿げ変えりゃ良くない？"sv},
        {"じゃね、また明日"sv, "じゃね、また明日"sv},
        {"殿下と取り巻きが引き込んでんじゃねーの？"sv, "殿下と取り巻きが引き込んでるんじゃないの？"sv},
        {"何で上級貴族クラスにいんの？"sv, "何で上級貴族クラスにいるの？"sv},
        {"あれはねえだろ"sv, "あれはないだろ"sv},
        {"考えるまでもねえだろ"sv, "考えるまでもないだろ"sv},
        {"あっ時間はねぇ、ヒトサンマルマルだよぉ。"sv, "あっ時間はねぇ、ヒトサンマルマルだよぉ。"sv},
        {"今から来てもねえ…"sv, "今から来てもねえ…"sv},
        {"仕方ないじゃない！"sv, "仕方ないじゃん！"sv},
        {"嫌いじゃない。"sv, "嫌いじゃない。"sv},
        {"義姉を困らせるんじゃない」"sv, "義姉を困らせるなよ」"sv},
        {"行くんじゃない？"sv, "行くんじゃない？"sv},
        {"いいじゃない？"sv, "いいじゃない？"sv},
        {"何それ怖っ！"sv, "何それ怖い！"sv},
        {"寒っ"sv, "寒い"sv},
        {"ずるっ"sv, "ずるっ"sv},
        {"感心できんぞ！"sv, "感心できないぞ！"sv},
        {"あり得ん！"sv, "あり得ない！"sv},
        {"そんなこと知らん！"sv, "そんなこと知らない！"sv},
        {"許さんぞ！"sv, "許さないぞ！"sv},
        {"一つしか食わんかったし。"sv, "一つしか食わんかったし。"sv},
        {"そういうとこ変わんないなぁ"sv, "そういうとこ変わんないなぁ"sv},
        {"それはできぬ相談でござる"sv, "それはできない相談でござる"sv},
        {"させんなよ"sv, "させんなよ"sv},
        {"田中さん！"sv, "田中さん！"sv},
        {"ハズい"sv, "ハズい"sv},
        {"ハズいしウザい"sv, "はずいしうざい"sv},
        {"それじゃねっって"sv, "それじゃないって"sv},
        {"決まってんだろ"sv, "決まってるんだろ"sv},
        {"そこにいんじゃ"sv, "そこにいるんじゃ"sv},
        {"遅っっ。"sv, "遅い。"sv},
        {"やったじゃない"sv, "やったじゃん"sv},
    };
    Check([](std::string_view text) { return SpokenForms(text, false); }, cases);
}

HD_TEST(pipeline_rewrite_interjections) {
    const Case cases[] = {
        {"あれ、アルくん！"sv, "あれっ、アルくん！"sv},
        {"「え？」"sv, "「えっ？」"sv},
        {"えぇ？"sv, "えっ？"sv},
        {"「えー…？」"sv, "「えっ…？」"sv},
        {"じゃあどっち"sv, "じゃあ、どっち"sv},
        {"じゃあね"sv, "じゃあね"sv},
        {"あれ！あれ、"sv, "あれっ！あれ、"sv},
        {"。　あれ…"sv, "。　あれっ…"sv},
        {"\x0aえ、"sv, "\x0aえっ、"sv},
        {"じゃあ"sv, "じゃあ"sv},
        {"「え――本当？」"sv, "「えっ――本当？」"sv},
        {"あれ─"sv, "あれっ─"sv},
        {"「ええ――」"sv, "「ええ――」"sv},
    };
    Check(RewriteInterjections, cases);
}

HD_TEST(pipeline_rewrite_casual) {
    const Case cases[] = {
        {"ふざけんなよ！"sv, "ふざけるなよ！"sv},
        {"ふざけるなっ！"sv, "ふざけるな！"sv},
        {"何すんだよ"sv, "何するんだよ"sv},
        {"何してくれんのよ"sv, "何してくれるのよ"sv},
        {"貰っとく。"sv, "貰っておく。"sv},
        {"直しとこう"sv, "直しておこう"sv},
        {"読んどくね"sv, "読んでおくね"sv},
        {"置いてくからね"sv, "置いていくからね"sv},
        {"いくっきゃない"sv, "いくしかない"sv},
        {"まあいっか。"sv, "まあいいか。"sv},
        {"いきましょー！"sv, "いきましょう！"sv},
        {"そーゆうの"sv, "そういうの"sv},
        {"ちょっとこっち"sv, "ちょっとこっち"sv},
        {"もっとこうして"sv, "もっとこうして"sv},
        {"置いてください"sv, "置いてください"sv},
        {"みんなよ"sv, "みんなよ"sv},
        {"見てねぇな"sv, "見ていないな"sv},
        {"わかんねぇ"sv, "わからない"sv},
        {"渡しちまった"sv, "渡しちゃった"sv},
        {"飲んじまえ"sv, "飲んじゃえ"sv},
        {"どうしろっつーんだよ"sv, "どうしろというんだよ"sv},
        {"あってねえ……"sv, "あってねえ……"sv},
        {"見てねぇ"sv, "見てねぇ"sv},
        {"来ねぇよ"sv, "来ないよ"sv},
        {"かもしれん"sv, "かもしれない"sv},
        {"かも知れねぇ"sv, "かも知れない"sv},
        {"ふざけるなっっ！"sv, "ふざけるな！"sv},
        {"ふざけるなっッあ"sv, "ふざけるなッあ"sv},
        {"何をすんの"sv, "何をするの"sv},
        {"なにすんだ"sv, "なにするんだ"sv},
        {"結婚すんの"sv, "結婚するの"sv},
        {"信頼すんだよ"sv, "信頼するんだよ"sv},
        {"探すんだ"sv, "探すんだ"sv},
        {"目指すんだろ"sv, "目指すんだろ"sv},
        {"もうすんだ"sv, "もうすんだ"sv},
        {"殺されてぇか"sv, "殺されたいか"sv},
        {"うっさい"sv, "うるさい"sv},
        {"とんでもねー"sv, "とんでもない"sv},
        {"ましょ～～"sv, "ましょう"sv},
        {"満々じゃねえか"sv, "満々じゃないか"sv},
        {"出すんじゃねぇ"sv, "出すんじゃない"sv},
        {"任せとけ"sv, "任せとけ"sv},
        {"私とこ"sv, "私とこ"sv},
        {"しといて"sv, "しておいて"sv},
        {"。しとけよ"sv, "。しておけよ"sv},
        {"見とけ"sv, "見ておけ"sv},
        {"書いときな"sv, "書いておきな"sv},
        {"書いときなさい"sv, "書いておきなさい"sv},
        {"もらっとくれ"sv, "もらっとくれ"sv},
        {"いっか"sv, "いいか"sv},
    };
    Check(RewriteCasual, cases);
}

HD_TEST(pipeline_rewrite_military_time) {
    const Case cases[] = {
        {"マルフタマルマル。夜です。"sv, "0200。夜です。"sv},
        {"ヒトヨン、マルマル！"sv, "1400！"sv},
        {"ひとよんまるまる。"sv, "1400。"sv},
        {"フタフタマルマル、お風呂"sv, "2200、お風呂"sv},
        {"マルマルの"sv, "マルマルの"sv},
        {"マルマルマルマルだよ"sv, "マルマルマルマルだよ"sv},
        {"ゴーゴーマルマル"sv, "5500"sv},
        {"キュウ・マル マルマル"sv, "9000"sv},
        {"マルマルマルマルー"sv, "マルマルマルマルー"sv},
        {"マルマルマルマルマルマルマルマル"sv, "マルマルマルマル0000"sv},
        {"ゴマルマルマル"sv, "5000"sv},
        {"まるまる"sv, "まるまる"sv},
    };
    Check(RewriteMilitaryTime, cases);
}

HD_TEST(pipeline_strip_ruby) {
    const Case cases[] = {
        {"|梨花《りか》ちゃん"sv, "梨花ちゃん"sv},
        {"｜幻晶騎士《シルエットナイト》"sv, "幻晶騎士"sv},
        {"猫猫《マオマオ》"sv, "猫猫"sv},
        {"<ruby>梨花<rt>りか</rt></ruby>ちゃん"sv, "梨花ちゃん"sv},
        {"<ruby><rb>梨花</rb><rp>（</rp><rt>りか</rt><rp>）</rp></ruby>"sv, "梨花"sv},
        {"《》だけ"sv, "《》だけ"sv},
        {"漢字《かな》と|ルビ《るび》"sv, "漢字とルビ"sv},
        {"|あいうえおかきくけこさしすせそたちつてとな《よみ》"sv, "|あいうえおかきくけこさしすせそたちつてとな《よみ》"sv},
        {"a<b"sv, "a<b"sv},
    };
    Check(StripRuby, cases);
}

HD_TEST(pipeline_write_kin_terms) {
    const Case female[] = {
        {"お兄様！"sv, "오빠！"sv},
        {"お兄様、俺は"sv, "お兄様、俺は"sv},
        {"お姉ちゃんとお兄さま"sv, "언니と오빠"sv},
    };
    for (const Case& c : female) HD_CHECK_EQ(WriteKinTerms(c.in, true), std::string(c.want));
    const Case male[] = {
        {"お姉ちゃん、わたくしは"sv, "언니、わたくしは"sv},
        {"お兄様！"sv, "お兄様！"sv},
    };
    for (const Case& c : male) HD_CHECK_EQ(WriteKinTerms(c.in, false), std::string(c.want));
}

HD_TEST(pipeline_female_speech) {
    const std::vector<std::string> woman = {"「あたしが行くわよ」", "お兄様は来ない"};
    HD_CHECK(FemaleSpeech(woman));
    const std::vector<std::string> brother = {"「わたくしですの」", "「お兄様、俺だ」"};
    HD_CHECK(!FemaleSpeech(brother));
    const std::vector<std::string> narration = {"あたしは行く"};  // not inside an utterance
    HD_CHECK(!FemaleSpeech(narration));
    const std::vector<std::string> unclosed = {"「行くわよ"};
    HD_CHECK(FemaleSpeech(unclosed));
}

HD_TEST(pipeline_normalize_sequence) {
    const Case femaleDoc[] = {
        {"|お兄様《おにいさま》、ｺﾞﾒﾝ、なさい"sv, "오빠、ゴメンなさい"sv},
        {"「お兄様！」ﾏﾙﾌﾀﾏﾙﾏﾙ"sv, "「오빠！」0200"sv},
    };
    for (const Case& c : femaleDoc) HD_CHECK_EQ(Normalize(c.in, LineContext{.document = true, .female = true}), std::string(c.want));
    const Case doc[] = {
        {"|お兄様《おにいさま》、ｺﾞﾒﾝ、なさい"sv, "お兄様、ゴメンなさい"sv},
        {"あれ、知らんかもしれん"sv, "あれっ、知らんかもしれない"sv},
    };
    for (const Case& c : doc) HD_CHECK_EQ(Normalize(c.in, LineContext{.document = true, .female = false}), std::string(c.want));
    const Case text[] = {
        {"|お兄様《おにいさま》、ｺﾞﾒﾝ、なさい"sv, "|お兄様《おにいさま》、ゴメンなさい"sv},
        {"ごめん…なさい、ござい、ます"sv, "ごめんなさい、ございます"sv},
        // The time is read before a pause before です is joined.
        {"マルフタマルマル…です"sv, "0200…です"sv},
        {"好き……で……す"sv, "好きです\uE006……\uE006\uE005……\uE005"sv},
    };
    for (const Case& c : text) HD_CHECK_EQ(Normalize(c.in, LineContext{.document = false, .female = false}), std::string(c.want));
}

HD_TEST(pipeline_escape_stage_text) {
    const Case cases[] = {
        {"a\\b\x09" "c\x0a" "d\x0d" "e\x01\x7f\xff€"sv, "a\\\\b\\tc\\nd\\re\\x01\\x7f\\xff€"sv},
        {"plain テキスト"sv, "plain テキスト"sv},
    };
    for (const Case& c : cases) HD_CHECK_EQ(EscapeStageText(c.in), std::string(c.want));
}

HD_TEST(pipeline_exclamation_na) {
    const Case cases[] = {
        {"そろそろ日が暮れるな。"sv, "そろそろ日が暮れるなあ。"sv},
        {"懐かしい気がするなー。"sv, "懐かしい気がするなあー。"sv},
        {"最近よく降るな…"sv, "最近よく降るなあ…"sv},
        {"まあ、そうなるな。"sv, "まあ、そうなるなあ。"sv},
        {"逃がすな！"sv, "逃がすな！"sv},
        {"お前が言うな！"sv, "お前が言うな！"sv},
        {"誰も動くな！"sv, "誰も動くな！"sv},
        {"さすが先生ですな。"sv, "さすが先生ですな。"sv},
        {"面倒なことになるなら"sv, "面倒なことになるなら"sv},
        {"困るなぁあーぁ。"sv, "困るなあーぁ。"sv},
        {"よく　降るな。"sv, "よく　降るなあ。"sv},
        {"よく\t降るな。"sv, "よく\t降るな。"sv},
        {"よく眠っているな。"sv, "よく眠っているなあ。"sv},
        {"よく眠ってはいるな。"sv, "よく眠ってはいるな。"sv},
    };
    for (const Case& c : cases) {
        HD_CHECK_EQ(SpokenForms(c.in, true), std::string(c.want));
        HD_CHECK_EQ(SpokenForms(c.in, false), std::string(c.in));
    }
    LineContext ctx;
    ctx.document = false;
    ctx.exclamationNa = true;
    HD_CHECK_EQ(Normalize("日が暮れるな。", ctx), "日が暮れるなあ。");
}

HD_TEST(pipeline_dialect_forms) {
    const Case cases[] = {
        {"こいつはたまんねぇ！"sv, "こいつはたまらない！"sv},
        {"金がねえ"sv, "金がない"sv},
        {"面白くねえな"sv, "面白くないな"sv},
        {"静かねえ。"sv, "静かねえ。"sv},
        {"来たねえ。"sv, "来たねえ。"sv},
        {"消化できへんかったなぁ"sv, "消化できなかったなぁ"sv},
        {"何にもあらへん"sv, "何にもない"sv},
        {"諦めはせえへん"sv, "諦めはしない"sv},
        {"たいへんに稀な"sv, "たいへんに稀な"sv},
        {"そこらへんで"sv, "そこらへんで"sv},
        {"ロボットが動いとる！"sv, "ロボットが動いている！"sv},
        {"何を読んどるんじゃ"sv, "何を読んでいるんじゃ"sv},
        {"メモをとる"sv, "メモをとる"sv},
        {"もちろんとる！"sv, "もちろんとる！"sv},
        {"出口らへんの様子"sv, "出口らへんの様子"sv},
        {"俺は知らへん"sv, "俺は知らない"sv},
        {"待っとった。"sv, "待っていた。"sv},
        {"読んどらん！"sv, "読んでいない！"sv},
        {"読んどれ！"sv, "読んでいろ！"sv},
        {"わかんねー"sv, "わからない"sv},
        {"分かんねぇ"sv, "分からない"sv},
        {"つまんねえ"sv, "つまらない"sv},
        {"くだんねえ"sv, "くだらない"sv},
        {"動かねぇ"sv, "動かない"sv},
        {"待たねえ"sv, "待たない"sv},
        {"あれへんかった"sv, "なかった"sv},
        {"まだ食べてへん。"sv, "まだ食べてない。"sv},
        {"読んどれよ"sv, "読んでいろよ"sv},
        {"読んどる、待っとる。"sv, "読んでいる、待っている。"sv},
    };
    for (const Case& c : cases) {
        HD_CHECK_EQ(SpokenForms(c.in, false, true), std::string(c.want));
        HD_CHECK_EQ(SpokenForms(c.in, false, false), std::string(c.in));
    }
    LineContext ctx;
    ctx.document = false;
    ctx.dialectForms = true;
    HD_CHECK_EQ(Normalize("金がねえ", ctx), "金がない");
}
