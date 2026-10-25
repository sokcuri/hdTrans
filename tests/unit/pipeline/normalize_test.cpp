// Table tests of the normalising rewrites, one table per rewrite, and of the whole
// sequence of Normalize.
#include <cstdlib>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/classes.h"
#include "core/dict/snapshot.h"
#include "core/pipeline/normalize.h"
#include "core/pipeline/spoken_forms_gen.h"
#include "core/pipeline/stage_text.h"
#include "core/text/utf8.h"
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
        {"ｱﾞ ﾞ ｶﾟ"sv, "ア ゛ カ゜"sv},
        {"あ゛っ お゛ほぉ゛ ん゛ん゛ う゛ー ああ゛゛～"sv, "あっ おほぉ んん うー ああ～"sv},
        {"い\u3099や ア゛ア゛ッ ウ゛ァ う゛ぁ"sv, "いや アアッ ヴァ う゛ぁ"sv},
        {"ウ゛ー ウ゛ーッ ウ゛ゥ ウ゛ィーナス ウ゛"sv, "ウー ウーッ ウゥ ヴィーナス ウ"sv},
        {"ウ゛ウ゛ー ウ゛ウ゛ッ ウ゛ン ウ゛ウ゛ァ"sv, "ウウー ウウッ ウン ヴヴァ"sv},
        {"｢ｱｲｳ｣､ｦ･ｰ｡"sv, "｢アイウ｣、ヲ・ー。"sv},
        {"〜−‖¢£¬"sv, "～－∥￠￡￢"sv},
        {"ＡＢＣ①"sv, "ＡＢＣ①"sv},
        {"a\x00" "b\x01" "c\x02" "d\x03" "e"sv, "a b c d e"sv},
        {"頰𠮟﨑髙"sv, "頬叱崎高"sv},
        {"ﾍﾟﾎﾞﾍﾞ"sv, "ペボベ"sv},
        {"x\xffy"sv, "x\xffy"sv},
        {"ｶ"sv, "カ"sv},
        {"待って‼ えっ⁉ 何⁈ うっ‼\uFE0E"sv, "待って！！ えっ！？ 何？！ うっ！！"sv},
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
        // A restarted word after 、 (a stutter or the particle で), and the word いま or 今ま.
        {"ここで…す、すみません！"sv, "ここで…す、すみません！"sv},
        {"学校で……す、すごい"sv, "学校で……す、すごい"sv},
        {"家で…す、好き"sv, "家で…す、好き"sv},
        {"今ま…す、すぐに"sv, "今ま…す、すぐに"sv},
        {"いま、した。"sv, "いま、した。"sv},
        {"そうで…す、はい。"sv, "そうです\uE005…\uE005、はい。"sv},
        {"していま…す。"sv, "しています\uE005…\uE005。"sv},
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
        // A particle, filler or conjunction before the pause.
        {"えっと……ですね"sv, "えっと……ですね"sv},
        {"つまり……ですね"sv, "つまり……ですね"sv},
        {"それは……ですね"sv, "それは……ですね"sv},
        {"今日は……ですね、雨です"sv, "今日は……ですね、雨です"sv},
        {"うーん……ですね"sv, "うーん……ですね"sv},
        {"だから……です"sv, "だから……です"sv},
        {"学校から…です"sv, "学校から…です"sv},
        {"私の…です"sv, "私の…です"sv},
        {"はい……です"sv, "はい……です"sv},
        // Kana after a kanji, an adjective in い or a predicate word in kana.
        {"頑張ったの…です"sv, "頑張ったのです\uE006…\uE006"sv},
        {"沈むん……ですか？"sv, "沈むんですか\uE006……\uE006？"sv},
        {"すごい…ですね"sv, "すごいですね\uE006…\uE006"sv},
        {"そう……ですか"sv, "そうですか\uE006……\uE006"sv},
    };
    Check(JoinPausedCopula, cases);
}

HD_TEST(pipeline_spoken_forms) {
    const Case cases[] = {
        {"マジでウザい！"sv, "マジでうざい！"sv},
        {"ダサすぎます！"sv, "ダサすぎます！"sv},
        {"ズルい！"sv, "ズルい！"sv},
        {"これはマズイ。"sv, "これはまずい。"sv},
        {"マズイっ！"sv, "まずいっ！"sv},
        {"ツラい仕事"sv, "つらい仕事"sv},
        {"セコイアの木"sv, "セコイアの木"sv},
        {"ウマイボウ"sv, "ウマイボウ"sv},
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
        {"足が痛っ"sv, "足が痛い"sv},
        {"痛っ！"sv, "痛っ！"sv},  // the cry, a word of the dictionary
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
        {"ハズいしウザい"sv, "ハズいしうざい"sv},
        {"ムズイしダサイ"sv, "むずいしダサい"sv},
        {"それじゃねっって"sv, "それじゃないって"sv},
        {"決まってんだろ"sv, "決まってるんだろ"sv},
        {"そこにいんじゃ"sv, "そこにいるんじゃ"sv},
        {"遅っっ。"sv, "遅い。"sv},
        {"やったじゃない"sv, "やったじゃん"sv},
        {"血筋かしらねぇ、あんたも"sv, "血筋かしら、あんたも"sv},
        {"そういえば、どうしてかしらね？"sv, "そういえば、どうしてかしら？"sv},
        {"毎日どっかしらでイベント"sv, "毎日どっかしらでイベント"sv},
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
        {"あれ！あれ、"sv, "あれっ！あれっ、"sv},
        {"「え？…え？」"sv, "「えっ？…えっ？」"sv},
        {"「へ……え？」"sv, "「へ……えっ？」"sv},
        {"「……て、…ええ…？」"sv, "「……て、…えっ…？」"sv},
        {"そうですね…え、"sv, "そうですね…え、"sv},
        {"考え？"sv, "考え？"sv},
        {"。　あれ…"sv, "。　あれっ…"sv},
        {"\x0aえ、"sv, "\x0aえっ、"sv},
        {"じゃあ"sv, "じゃあ"sv},
        {"「え――本当？」"sv, "「えっ――本当？」"sv},
        {"あれ─"sv, "あれっ─"sv},
        {"「ええ――」"sv, "「ええ――」"sv},
        // The pronoun あれ before a particle.
        {"あれ――は何だ？"sv, "あれ――は何だ？"sv},
        {"あれ―は誰？"sv, "あれ―は誰？"sv},
        {"あれ……が欲しい"sv, "あれ……が欲しい"sv},
        {"え――はい"sv, "えっ――はい"sv},
        // え after a comma or a pause right before a pause hesitates; before a question only
        // after a pause.
        {"あっ、え…違う"sv, "あっ、えっ…違う"sv},
        {"……え……本当"sv, "……えっ……本当"sv},
        {"そう、え？"sv, "そう、え？"sv},
        {"考え…"sv, "考え…"sv},
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
        {"書こうとしてる。"sv, "書こうとしている。"sv},
        {"帰ろうとしてたら"sv, "帰ろうとしていたら"sv},
        {"そうとして"sv, "そうとして"sv},
        {"そーゆうの"sv, "そういうの"sv},
        {"ちょっとこっち"sv, "ちょっとこっち"sv},
        {"もっとこうして"sv, "もっとこうして"sv},
        {"置いてください"sv, "置いてください"sv},
        {"みんなよ"sv, "みんなよ"sv},
        {"見てねぇな"sv, "見ていないな"sv},
        {"見てねーわ"sv, "見ていないわ"sv},
        {"見てねー！"sv, "見てねー！"sv},
        {"荒れねーから"sv, "荒れないから"sv},
        {"言えねえのよ"sv, "言えないのよ"sv},
        {"トイレいけねーよ"sv, "トイレいけないよ"sv},
        {"夜明けねー。"sv, "夜明けねー。"sv},
        {"いい感じねぇ～"sv, "いい感じねぇ～"sv},
        {"もうすぐ夜明けねー"sv, "もうすぐ夜明けねー"sv},
        {"わかんねぇ"sv, "わからない"sv},
        {"やらねぇよ"sv, "やらないよ"sv},
        {"やらねー"sv, "やらない"sv},
        {"聞き捨てならねぇな"sv, "聞き捨てならないな"sv},
        {"しなきゃならねえんだ"sv, "しなきゃならないんだ"sv},
        {"気にならねえ相手"sv, "気にならない相手"sv},
        {"やらねぇ。"sv, "やらない。"sv},
        {"君ならねぇ、"sv, "君ならねぇ、"sv},
        {"君ならねぇかな"sv, "君ならねぇかな"sv},
        {"よくならねぇ。"sv, "よくならない。"sv},
        {"行くならねぇ、俺も行く"sv, "行くならねぇ、俺も行く"sv},
        {"お前とならねぇ、どこへでも"sv, "お前とならねぇ、どこへでも"sv},
        {"君にならねぇ、話せる"sv, "君にならねぇ、話せる"sv},
        {"田中にならねぇ、話せる"sv, "田中にならねぇ、話せる"sv},
        {"気にならねぇ、そんなの"sv, "気にならない、そんなの"sv},
        {"何やらねぇ、変な音"sv, "何やらねぇ、変な音"sv},
        {"どうやらねぇ、雨"sv, "どうやらねぇ、雨"sv},
        {"しなきゃならねぇ、"sv, "しなきゃならない、"sv},
        {"やらなきゃならねぇ…"sv, "やらなきゃならない…"sv},
        {"話にならねぇ、帰れ"sv, "話にならない、帰れ"sv},
        {"よくならねぇ、全然"sv, "よくならない、全然"sv},
        {"しなきゃならねぇ　"sv, "しなきゃならない　"sv},
        {"渡しちまった"sv, "渡しちゃった"sv},
        {"蜂の巣にされちまうのよさ"sv, "蜂の巣にされちゃうのよさ"sv},
        {"見ちまったなぁ？"sv, "見ちゃったなぁ？"sv},
        {"いちまい"sv, "いちまい"sv},
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
        {"出ていく事ないじゃんか」"sv, "出ていく事ないじゃん」"sv},
        {"透けてるじゃんかーーーっ！！"sv, "透けてるじゃん！！"sv},
        {"それは聞いてるじゃんっ！"sv, "それは聞いてるじゃん！"sv},
        {"早くじゃんけんするのよさ"sv, "早くじゃんけんするのよさ"sv},
        {"それじゃ行こっか"sv, "それじゃ行こうか"sv},
        {"私はどれにしよっかなー"sv, "私はどれにしようかなー"sv},
        {"「帰ろっか」"sv, "「帰ろうか」"sv},
        {"あそっか、ちょっと離れたよね"sv, "あそっか、ちょっと離れたよね"sv},
        {"漢字そっか"sv, "漢字そっか"sv},
        {"お風呂で汗を流そっか？"sv, "お風呂で汗を流そっか？"sv},  // the sound stage reads it
        {"自分なんて、みそっかすの底辺"sv, "自分なんて、みそっかすの底辺"sv},
        {"自分のも、編んじゃおっかな…"sv, "自分のも、編んじゃおうかな…"sv},
        {"寝ちゃおっか？"sv, "寝ちゃおうか？"sv},
        {"で一番おっかない"sv, "で一番おっかない"sv},
        {"て……おっかねぇ"sv, "て……おっかねぇ"sv},
        {"全部手作りなのかい？"sv, "全部手作りなのかい？"sv},
        {"元気してるかい、提督？"sv, "元気してるか、提督？"sv},
        {"お元気でやってますかい？"sv, "お元気でやってますか？"sv},
        {"どうすんだい？"sv, "どうするんだい？"sv},
        {"聞いているかい？"sv, "聞いているかい？"sv},
        {"このかいは"sv, "このかいは"sv},
        {"こんなんで"sv, "こんなので"sv},
        {"そんなんじゃ"sv, "そんなのじゃ"sv},
        {"こんなんだ"sv, "こんなのだ"sv},
        {"こんなん。"sv, "こんなん。"sv},
        {"こっからは教師の領分じゃない"sv, "ここからは教師の領分じゃない"sv},
        {"もっかい撮るから笑って"sv, "もう一回撮るから笑って"sv},
        {"「んで、どうやって相談を集める？」"sv, "「それで、どうやって相談を集める？」"sv},
        {"読んで、"sv, "読んで、"sv},
        {"「しゃーない。じゃあ今回は"sv, "「しょうがない。じゃあ今回は"sv},
        {"しゃーねえだろ、渡し"sv, "しょうがないだろ、渡し"sv},
        {"まぁしゃあないか"sv, "まぁしょうがないか"sv},
        {"それならしゃーない"sv, "それならしょうがない"sv},
        {"だってしゃーねーだろ"sv, "だってしょうがないだろ"sv},
        {"「わっしゃあね、知らんよ」"sv, "「わっしゃあね、知らんよ」"sv},
        {"わたくしゃあね"sv, "わたくしゃあね"sv},
        {"「あたしゃあね、昔は綺麗だったんだよ」"sv, "「あたしゃあね、昔は綺麗だったんだよ」"sv},
        {"わしゃあね、もう歳じゃ"sv, "わしゃあね、もう歳じゃ"sv},
        {"「さーて、帰ってご飯にしよう！"sv, "「さて、帰ってご飯にしよう！"sv},
        {"「じゃっ、そゆことで」"sv, "「じゃっ、そういうことで」"sv},
        {"うん、そだね。"sv, "うん、そうだね。"sv},
        {"そであった。"sv, "そうであった。"sv},
        {"そでもない？"sv, "そうでもない？"sv},
        {"そでを捲る"sv, "そでを捲る"sv},
        {"そだって"sv, "そだって"sv},
        {"子どもがそだつ"sv, "子どもがそだつ"sv},
        {"この むらで そだった"sv, "この むらで そだった"sv},
        {"そだった木"sv, "そだった木"sv},
        {"大きく、そだった"sv, "大きく、そだった"sv},
        {"ふくの そでも ぬれた"sv, "ふくの そでも ぬれた"sv},
        {"そでもいい"sv, "そでもいい"sv},
        {"そだったね"sv, "そうだったね"sv},
        {"うん。 そだね"sv, "うん。 そうだね"sv},
        {"「いきなりひでえ！？"sv, "「いきなりひどい！？"sv},
        {"「ひでえな」"sv, "「ひどいな」"sv},
        {"「あ、おいっ」"sv, "「あ、おい」"sv},
        {"「おいっす、頑張ってる？」"sv, "「おいっす、頑張ってる？」"sv},
        {"間接キスしていいんか？"sv, "間接キスしていいのか？"sv},
        {"「いや、信じるんかい……」"sv, "「いや、信じるのかい……」"sv},
        {"で、上手くいくんかねえ"sv, "で、上手くいくのかねえ"sv},
        {"お前が書くんか？"sv, "お前が書くのか？"sv},
        {"ああ、とっくにやっといた。"sv, "ああ、とっくにやっておいた。"sv},
        {"「やっといた、探したよ」"sv, "「やっといた、探したよ」"sv},
        {"「やっといたよ」"sv, "「やっておいたよ」"sv},
        {"やっといた！見つかった"sv, "やっといた！見つかった"sv},
        {"宿題？やっといたよ"sv, "宿題？やっておいたよ"sv},
        {"やっといた。"sv, "やっておいた。"sv},
        {"やっといてくれ"sv, "やっておいてくれ"sv},
        {"「田中くんか？」"sv, "「田中くんか？」"sv},
        {"「佐藤くんかい？」"sv, "「佐藤くんかい？」"sv},
        {"ユウくんか？"sv, "ユウくんか？"sv},
        {"ゆうくんかよ"sv, "ゆうくんかよ"sv},
        {"「なーにイチャついてんのよ。"sv, "「なにイチャついてんのよ。"sv},
        {"「え、ダメなんすか？"sv, "「え、ダメなんですか？"sv},
        {"なんすか？"sv, "なんすか？"sv},
        {"抜け駆けすんなッ！"sv, "抜け駆けするな！"sv},
        {"心配すんな・・・"sv, "心配するな・・・"sv},
        {"交渉はすんなり進んだ"sv, "交渉はすんなり進んだ"sv},
        {"ごめんな、遅れて"sv, "ごめんな、遅れて"sv},
        {"やめんな！"sv, "やめるな！"sv},
        {"でなければ、死ぬのはおまえだ"sv, "そうでなければ、死ぬのはおまえだ"sv},
        {"できていた。でないと困る"sv, "できていた。そうでないと困る"sv},
        {"学生でなければ入れない"sv, "学生でなければ入れない"sv},
        {"それ、でなければ"sv, "それ、でなければ"sv},
        {"大丈夫さ。寝りゃ治る"sv, "大丈夫さ。寝れば治る"sv},
        {"俺が悪役になりゃ丸く収まる"sv, "俺が悪役になれば丸く収まる"sv},
        {"それができりゃあ苦労しねえ"sv, "それができれば苦労しない"sv},
        {"どこだかもわかりゃしない"sv, "どこだかもわかりはしない"sv},
        {"金がありゃ買える"sv, "金があれば買える"sv},
        {"ありゃなんだ"sv, "ありゃなんだ"sv},
        {"そりゃそうだ"sv, "そりゃそうだ"sv},
        {"うりゃっ！"sv, "うりゃっ！"sv},
        {"見てりゃ分かる"sv, "見てりゃ分かる"sv},
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
        {"マルロクマルマルっぽい！"sv, "0600っぽい！"sv},
        {"ヒトサンマルマルです。"sv, "1300です。"sv},
        {"フタサンマルマルー！"sv, "2300！"sv},
        {"ヒトフタマルマルッ昼"sv, "ヒトフタマルマルッ昼"sv},
        {"ヒトゴーマルマルスタート"sv, "ヒトゴーマルマルスタート"sv},
        {"ゴーゴーマルマルだ"sv, "ゴーゴーマルマルだ"sv},
        {"ヒトハチ～マルマル！"sv, "1800！"sv},
        {"フタヒト～マルマルです！"sv, "2100です！"sv},
        {"マルサン…マルマル！"sv, "0300！"sv},
        {"フタサン……マルマル……。"sv, "2300……。"sv},
        {"ヒトサンマルマル～フタマルマルマル"sv, "1300～2000"sv},
        {"ヒトヨン、マル…ああ"sv, "ヒトヨン、マル…ああ"sv},
        {"現在時刻ヒトキュマルマル。"sv, "現在時刻1900。"sv},
        {"マルサン・・マルマル！"sv, "0300！"sv},
        {"ヒトフタマルマルゥ！"sv, "1200！"sv},
    };
    Check(RewriteMilitaryTime, cases);
}

HD_TEST(pipeline_strip_ruby) {
    const Case cases[] = {
        {"|桃花《ももか》ちゃん"sv, "桃花ちゃん"sv},
        {"｜幻晶騎士《シルエットナイト》"sv, "幻晶騎士"sv},
        {"猫猫《マオマオ》"sv, "猫猫"sv},
        {"<ruby>桃花<rt>ももか</rt></ruby>ちゃん"sv, "桃花ちゃん"sv},
        {"<ruby><rb>桃花</rb><rp>（</rp><rt>ももか</rt><rp>）</rp></ruby>"sv, "桃花"sv},
        {"《》だけ"sv, "《》だけ"sv},
        {"漢字《かな》と|ルビ《るび》"sv, "漢字とルビ"sv},
        {"|あいうえおかきくけこさしすせそたちつてとな《よみ》"sv, "|あいうえおかきくけこさしすせそたちつてとな《よみ》"sv},
        {"a<b"sv, "a<b"sv},
    };
    Check(StripRuby, cases);
}

HD_TEST(pipeline_join_brace_ruby) {
    // A stand-in for the dictionary: 背負 and 見惚 start verbs, other longer bases are
    // nouns; 剥 inflects in が; after 庇 and 掬 a word follows the verb inside the kana.
    const ReadBase read = [](const void*, std::string_view base, std::string_view kana) {
        BaseReading r;
        r.continues = base == "背負" || base == "見惚";
        r.inflected = base == "剥";
        if (base == "庇" && kana.starts_with("ってくれ")) r.wordEnd = std::string_view("って").size();
        if (base == "掬" && kana.starts_with("ったまぐろ")) r.wordEnd = std::string_view("った").size();
        return r;
    };
    const Case cases[] = {
        {"{rb}悔{/rb}{rt}く{/rt}やんでも。"sv, "{rb}悔やんでも{/rb}{rt}くやんでも{/rt}。"sv},
        {"{rb}微{/rb}{rt}かす{/rt}かに声"sv, "{rb}微かに{/rb}{rt}かすかに{/rt}声"sv},
        {"{rb}背負{/rb}{rt}しょ{/rt}って"sv, "{rb}背負って{/rb}{rt}しょって{/rt}"sv},
        {"{rb}見惚{/rb}{rt}みほ{/rt}れる。"sv, "{rb}見惚れる{/rb}{rt}みほれる{/rt}。"sv},
        // Only the base's own word moves when another word follows it in the kana.
        {"{rb}庇{/rb}{rt}かば{/rt}ってくれた"sv, "{rb}庇って{/rb}{rt}かばって{/rt}くれた"sv},
        {"{rb}掬{/rb}{rt}すく{/rt}ったまぐろの"sv, "{rb}掬った{/rb}{rt}すくった{/rt}まぐろの"sv},
        // Okurigana that start like a particle move when the base inflects in them.
        {"{rb}剥{/rb}{rt}は{/rt}がそうと"sv, "{rb}剥がそうと{/rb}{rt}はがそうと{/rt}"sv},
        // A noun of two or more characters keeps the kana after the tags.
        {"{rb}安堵{/rb}{rt}あんど{/rt}して"sv, "{rb}安堵{/rb}{rt}あんど{/rt}して"sv},
        {"{rb}杞憂{/rb}{rt}きゆう{/rt}ってもの"sv, "{rb}杞憂{/rb}{rt}きゆう{/rt}ってもの"sv},
        // A particle or a name suffix stays after the tags.
        {"{rb}現実{/rb}{rt}ここ{/rt}にいる"sv, "{rb}現実{/rb}{rt}ここ{/rt}にいる"sv},
        {"{rb}夢{/rb}{rt}ここ{/rt}で"sv, "{rb}夢{/rb}{rt}ここ{/rt}で"sv},
        {"{rb}話{/rb}{rt}ばなし{/rt}だけど"sv, "{rb}話{/rb}{rt}ばなし{/rt}だけど"sv},
        {"お{rb}前{/rb}{rt}め{/rt}ぇは"sv, "お{rb}前{/rb}{rt}め{/rt}ぇは"sv},
        {"{rb}蒼{/rb}{rt}あおい{/rt}くーん"sv, "{rb}蒼{/rb}{rt}あおい{/rt}くーん"sv},
        {"{rb}蒼{/rb}{rt}あおい{/rt}くん！"sv, "{rb}蒼{/rb}{rt}あおい{/rt}くん！"sv},
        {"{rb}書{/rb}{rt}か{/rt}くんだ"sv, "{rb}書くんだ{/rb}{rt}かくんだ{/rt}"sv},
        {"{rb}{/rb}{rt}く{/rt}や"sv, "{rb}{/rb}{rt}く{/rt}や"sv},
        {"{k=-5.0}―――{/k}"sv, "{k=-5.0}―――{/k}"sv},
    };
    for (const Case& c : cases) HD_CHECK_EQ(JoinBraceRuby(c.in, read, nullptr), std::string(c.want));
}

HD_TEST(pipeline_respell_variants) {
    // A stand-in for the dictionary: 身の程 and ときめく are words that hold a variant.
    const WordKeepsVariant words = [](const void*, std::string_view line, size_t at, std::string_view variant,
                                      std::string_view) {
        const std::string_view rest = line.substr(at);
        return rest.starts_with("程度") ||
               (rest.starts_with("程") && at >= "身の"sv.size() && line.substr(at - "身の"sv.size()).starts_with("身の")) ||
               (variant == "とき"sv && rest.starts_with("ときめ"));
    };
    const Case cases[] = {
        {"痛い程に赤い。"sv, "痛いほどに赤い。"sv},
        {"子供程の背丈"sv, "子供ほどの背丈"sv},
        {"程度の差"sv, "程度の差"sv},
        {"身の程を知れ"sv, "身の程を知れ"sv},
        {"それぐらいに近い"sv, "それぐらいに近い"sv},
        {"人の為に"sv, "人のために"sv},
        {"何を為そうと"sv, "何を為そうと"sv},
        {"死ぬ迄"sv, "死ぬまで"sv},
        {"見たときに分かった"sv, "見た時に分かった"sv},
        {"もっときれいになる"sv, "もっときれいになる"sv},
        {"胸がときめく"sv, "胸がときめく"sv},
        {"信じられない位淡白"sv, "信じられないくらい淡白"sv},
        {"神の位から落ちた"sv, "神の位から落ちた"sv},
        {"知らない事が多い"sv, "知らない事が多い"sv},
        {"仕事を終えた"sv, "仕事を終えた"sv},
        {"行かせない等、"sv, "行かせないなど、"sv},
        {"時間がない"sv, "時間がない"sv},
    };
    for (const Case& c : cases) HD_CHECK_EQ(RespellVariants(c.in, words, nullptr), std::string(c.want));
    // Every member is VARIANT=SPELLING with both sides written.
    for (const std::string_view pair : hdTrans::dict::ClassMembers(HDTRANS_CLASS("spelling.variant"))) {
        const size_t eq = pair.find('=');
        HD_CHECK(eq != std::string_view::npos && eq > 0 && eq + 1 < pair.size() &&
                 pair.find('=', eq + 1) == std::string_view::npos);
    }
}

HD_TEST(pipeline_respell_katakana_endings) {
    const Case cases[] = {
        {"私デース！"sv, "私でーす！"sv},
        {"楽しいネー"sv, "楽しいねー"sv},
        {"受信シマシタ…"sv, "受信しました…"sv},
        {"行きマース！"sv, "行きまーす！"sv},
        {"justデース"sv, "justでーす"sv},
        {"一マス進む"sv, "一マス進む"sv},
        {"あと一マス。"sv, "あと一マス。"sv},
        {"Decemberネー！"sv, "Decemberネー！"sv},
        {"言ったデース！"sv, "言ったデース！"sv},
        {"何してるデース！"sv, "何してるデース！"sv},
        {"来たデース！"sv, "来たデース！"sv},
        {"あなたデス！"sv, "あなたです！"sv},
        {"まだデース！"sv, "まだでーす！"sv},
        {"行くデース！"sv, "行くでーす！"sv},
        {"デスゲーム"sv, "デスゲーム"sv},
        {"サドンデス！"sv, "サドンデス！"sv},
        {"……デース"sv, "……デース"sv},
        {"音声ヲ、解析シマス。"sv, "音声ヲ、解析シマス。"sv},
    };
    for (const Case& c : cases) HD_CHECK_EQ(RespellKatakanaEndings(c.in), std::string(c.want));
}

HD_TEST(pipeline_respell_kanji_cries) {
    const Case cases[] = {
        {"「嫌ぁぁぁ」"sv, "「いやぁぁぁ」"sv},
        {"嫌っ！"sv, "いやっ！"sv},
        {"「嫌ぁ！」"sv, "「いやぁ！」"sv},
        {"「嫌ぁ……」"sv, "「嫌ぁ……」"sv},
        {"すごく嫌ー"sv, "すごく嫌ー"sv},
        {"嫌っている"sv, "嫌っている"sv},
        {"嫌！"sv, "嫌！"sv},
    };
    for (const Case& c : cases) HD_CHECK_EQ(RespellKanjiCries(c.in), std::string(c.want));
}

HD_TEST(pipeline_respell_old_forms) {
    // A stand-in for the dictionary: 赤澤 and 龍 are words that hold an old form.
    const WordHoldsRun words = [](const void*, std::string_view line, size_t at, size_t) {
        return line.substr(at).starts_with("龍") ||
               (at >= "赤"sv.size() && line.substr(at - "赤"sv.size()).starts_with("赤澤"));
    };
    const Case cases[] = {
        {"格好惡いだろ"sv, "格好悪いだろ"sv},
        {"隱れた樣子"sv, "隠れた様子"sv},
        {"赤澤さんと澤田さん"sv, "赤澤さんと沢田さん"sv},
        {"悪い"sv, "悪い"sv},
        {"神龍"sv, "神龍"sv},
    };
    for (const Case& c : cases) HD_CHECK_EQ(RespellOldForms(c.in, words, nullptr), std::string(c.want));
    // Every member is OLD=NEW, one kanji on each side, each old form once.
    std::set<std::string_view> olds;
    for (const std::string_view pair : hdTrans::dict::ClassMembers(HDTRANS_CLASS("spelling.old-form"))) {
        const size_t eq = pair.find('=');
        HD_CHECK(eq != std::string_view::npos && hdTrans::text::RuneCount(pair.substr(0, eq)) == 1 &&
                 hdTrans::text::RuneCount(pair.substr(eq + 1)) == 1);
        HD_CHECK(olds.insert(pair.substr(0, eq)).second);
    }
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
        {"お兄ちゃん、僕も行く"sv, "兄ちゃん、僕も行く"sv},  // a man's お兄ちゃん, read 형
        {"お兄ちゃん、早く"sv, "お兄ちゃん、早く"sv},   // no cue: the dictionary's 오빠
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
        // Private use characters of the input are escaped and so never read as marks.
        {"ボタン\uE005を押す"sv, "ボタン\U000F0005を押す"sv},
        {"a\uE005…\uE005"sv, "a\U000F0005…\U000F0005"sv},
    };
    for (const Case& c : text) HD_CHECK_EQ(Normalize(c.in, LineContext{.document = false, .female = false}), std::string(c.want));
}

HD_TEST(pipeline_emphatic_spellings) {
    const Case cases[] = {
        {"さっすが先輩！"sv, "さすが先輩！"sv},
        {"しっかたねえ、やるぞ"sv, "しかたねえ、やるぞ"sv},
        {"すんごく痛い"sv, "すごく痛い"sv},
        {"すんなり進んだ"sv, "すんなり進んだ"sv},
        {"あったりまえでしょ"sv, "あたりまえでしょ"sv},
        {"どこかにあったりして"sv, "どこかにあったりして"sv},
        {"くっさいのよ"sv, "くさいのよ"sv},
        {"うわ、くっさ。"sv, "うわ、臭っ。"sv},
        {"うっせーな"sv, "うっせーな"sv},
        {"うっせ"sv, "うるせえ"sv},
        {"バッカじゃないの"sv, "バカじゃないの"sv},
        {"バッカスの神"sv, "バッカスの神"sv},
        {"でしょ、ばっかじゃないの"sv, "でしょ、バカじゃないの"sv},
        {"学食ばっかじゃ飽きる"sv, "学食ばっかじゃ飽きる"sv},
    };
    Check(RewriteEmphaticSpellings, cases);
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
        {"最近よく降るな、これは"sv, "最近よく降るなあ、これは"sv},
        {"逃がすな、追え！"sv, "逃がすな、追え！"sv},
        {"お前が言うな！"sv, "お前が言うな！"sv},
        {"お前ごときが私に勝てると思うな"sv, "お前ごときが私に勝てると思うな"sv},
        {"みんなが来ると思うな。"sv, "みんなが来ると思うな。"sv},
        {"お前が勝てると思うなぁ"sv, "お前が勝てると思うなぁ"sv},
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
        {"このラーメンうめえわ。"sv, "このラーメン美味い。"sv},
        {"マジでうるせえよ"sv, "マジでうるさいよ"sv},
        {"すげえ！"sv, "すげえ！"sv},
        {"すげえすげえ"sv, "すごいすごい"sv},
        {"肉を焼いてー！"sv, "肉を焼いてー！"sv},
        {"会うぜぇ！"sv, "会うぜぇ！"sv},
        {"ちげぇねえ"sv, "ちげぇねえ"sv},
        {"こっちわ大丈夫"sv, "こっちは大丈夫"sv},
        {"お前わかってる"sv, "お前わかってる"sv},
        {"試験に落ちたん？"sv, "試験に落ちたの？"sv},
        {"もう行ったんか？"sv, "もう行ったのか？"sv},
        {"そうなん？"sv, "そうなの？"sv},
        {"うん？"sv, "うん？"sv},
        {"ううん？"sv, "ううん？"sv},
        {"コウジくん？"sv, "コウジくん？"sv},
        {"たぶん？"sv, "たぶん？"sv},
        {"なんてことだ！"sv, "なんてこった！"sv},
        {"「なんということ」"sv, "「なんてこった」"sv},
        {"なんてことを言う"sv, "なんてことを言う"sv},
        {"嘘だ、なんてことはない"sv, "嘘だ、なんてことはない"sv},
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

HD_TEST(pipeline_normalize_doubled_wo) {
    const Case cases[] = {
        {"避ける素振りをを見せない"sv, "避ける素振りを見せない"sv},
        {"好きな食べ物をを互いに"sv, "好きな食べ物を互いに"sv},
        {"本ををを読む"sv, "本を読む"sv},
        {"「をを」"sv, "「をを」"sv},
        {"本を読む"sv, "本を読む"sv},
    };
    Check(CollapseDoubledWo, cases);
}

HD_TEST(pipeline_normalize_tsu_runs) {
    const Case cases[] = {
        {"褒められちゃっっっった♪"sv, "褒められちゃった♪"sv},
        {"とっってもいい"sv, "とってもいい"sv},
        {"バッッカじゃないの"sv, "バッカじゃないの"sv},
        {"行っっッくぞ"sv, "行っくぞ"sv},
        {"あっっ！"sv, "あっっ！"sv},
        {"ああッッッ！！"sv, "ああッッッ！！"sv},
        {"「っっ……」"sv, "「っっ……」"sv},
        {"あっっぁ"sv, "あっっぁ"sv},
        {"ウオッッッー"sv, "ウオッッッー"sv},
        {"行った"sv, "行った"sv},
    };
    Check([](std::string_view text) { return CollapseTsuRuns(text); }, cases);
    const auto holdsAll = [](const void*, std::string_view, size_t, size_t) { return true; };
    HD_CHECK_EQ(CollapseTsuRuns("じっっくり", {.holds = holdsAll}), "じっっくり");
    // The stress mark goes before the word the lookup finds, once for each word.
    const auto lineStart = [](const void*, std::string_view line, size_t at) {
        return line.substr(0, at).ends_with("と") ? std::string::npos : size_t{0};
    };
    HD_CHECK_EQ(CollapseTsuRuns("すっっっごい", {.start = lineStart}), "\uE00Dすっごい");
    HD_CHECK_EQ(CollapseTsuRuns("すっっごっっい", {.start = lineStart}), "\uE00Dすっごっい");
    HD_CHECK_EQ(CollapseTsuRuns("とっっても", {.start = lineStart}), "とっても");
    // A later run whose word starts before the word of an earlier one.
    const auto back = [](const void*, std::string_view, size_t at) { return at > 6 ? size_t{0} : size_t{3}; };
    HD_CHECK_EQ(CollapseTsuRuns("あいっっうえっっお", {.start = back}), "\uE00Dあ\uE00Dいっうえっお");
}

// Every hiragana stem the katakana stems are rewritten to is a stem of an adjective or a
// verb of the shipped dictionaries, so the rewrite never makes a word they lack (ムズイ
// むずい). The dictionaries are needed, so the test skips without HDTRANS_DICTS.
HD_TEST(pipeline_kata_stems_are_dictionary_stems) {
    const char* dicts = std::getenv("HDTRANS_DICTS");
    if (dicts == nullptr || *dicts == '\0') return hdtest::Skip("HDTRANS_DICTS is not set");
    const auto loaded = hdTrans::dict::LoadFoldersTsv({dicts});
    HD_REQUIRE(loaded.snapshot != nullptr);
    for (const KataStem& k : kKataStems) {
        bool stem = false;
        for (const hdTrans::dict::Candidate& c : loaded.snapshot->RetrieveRecords(k.hira, hdTrans::dict::LookupMode::General)) {
            const char tag = c.codes != nullptr ? c.codes->part.Tag() : 0;
            stem = stem || tag == 'H' || tag == 'M';
        }
        HD_CHECK_EQ(std::string(k.hira) + (stem ? " stem" : " missing"), std::string(k.hira) + " stem");
    }
}
