// Speaker labels, honorifics, occurrence guards, name rendering and plain rows.
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/names/historical.h"
#include "core/names/registry.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

const dict::Snapshot* Dictionary() {
    static const dict::LoadResult loaded = dict::Load({
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "紗英\t사에\tN130\tQPN\tWb\tGN\t1\n"
         "桃花\t복숭아꽃\tN110\t\t\tGN\t2\n"
         "光\t빛\tN110\t\t\tGN\t3\n"
         "光\t히카루\tN130\tQPN\tWb\tGN\t4\n"
         "光\t빛나&AA@\tM428\t\t\tGN\t5\n"
         "乾\t마르&AA@\tM428\t\t\tGN\t6\n"
         "乾\t이누이\tN130\tQPN\tWa\tGN\t7\n"
         "高森\t타카모리\tN130\tQPN\tWa\tGN\t8\n"
         "大丈夫\t괜%C4@\tG8U0\t\t\tGN\t9101\n"
         "真昼\t한낮\tN110\tUMe\t\tGN\t9001\n"
         "真昼\t마히루\tN130\tQPN\tWb\tGN\t9002\n"
         "恭平\t쿄헤이\tN130\tQPN\tWb\tGN\t9\n"
         "ソード\t소드\tN110\t\t\tGN\t10\n"
         "ワールド\t월드\tN110\t\t\tGN\t11\n"
         "ハル\t헐\tN110\t\t\tGN\t15\n"
         "提督\t제독\tN110\tUAqVHn\t\tGN\t17\n"
         "あなた\t당신\tN120\t\t\tGN\t18\n"
         "私\t나\tN120\t\t\tGN\t19\n"
         "わたくし\t저\tN120\t\t\tGN\t20\n"
         "三日\t3일\tN110\t\t\tGN\t21\n"
         "あお\t$C2@\tH7U0\t\t\tGN\t25\n"
         "ひかり\t빛\tN110\t\t\tGN\t26\n"
         "ひよこ\t병아리\tN110\tVAm\t\tGN\t29\n"
         "話\t이야기&AV@\tM443\t\t\tGN\t30\n"
         "なま\t날것\tN110\t\t\tGN\t32\n"
         "秋刀魚\t꽁치\tN110\t\t\tGN\t33\n"
         "さんま\t꽁치\tN110\t\t\tGN\t34\n"
         "さんま\t산마\tN130\tQPN\tWb\tGN\t35\n"
         "千里\t천리\tN110\t\t\tGN\t36\n"
         "ちさと\t치사토\tN130\tQPN\tWb\tGN\t37\n"
         "みか\t미카\tN130\tQPN\tWb\tGN\t38\n"
         "用途\t용도\tN110\t\t\tGN\t39\n"
         "生\t날것\tN110\t\t\tGN\t40\n"
         "錆び\t녹&LJ@\tM427\t\t\tGN\t63\n"
         "さび\t녹&LJ@\tM417\t\t\tGN\t64\n"
         "全て\t모두\tN110\t\t\tGN\t41\n"
         "すべて\t모두\tE300\t\t\tGN\t42\n"
         "壊\t깨뜨&AA@\tM443\t\t\tGN\t43\n"
         "こわ\t깨뜨&AA@\tM443\t\t\tGN\t44\n"
         "心\t마음\tN110\t\t\tGN\t45\n"
         "こころ\t마음\tN110\t\t\tGN\t46\n"
         "梁\t대들보\tN110\t\t\tGN\t47\n"
         "はり\t바늘\tN110\t\t\tGN\t48\n"
         "姉\t언니\tN110\tVHn\t\tGN\t49\n"
         "はい\t재\tN110\t\t\tGN\t50\n"
         "うん\t응\tN320\t\t\tGN\t51\n"
         "店\t가게\tN110\t\t\tGN\t52\n"
         "結果\t결과\tN110\t\t\tGN\t53\n"
         "まあまあ\t그저\tE311\t\t\tGN\t54\n"
         "理由\t이유\tN110\t\t\tGN\t55\n"
         "なし\t없음\tN110\t\t\tGN\t56\n"
         "鉄道株式会社\t철도 주식회사\tN110\t\t\tGN\t57\n"
         "朝餉\t조반\tN110\tVFd\t\tGN\t65\n"
         "あさがれい\t아침 수라\tN110\tVFd\t\tGN\t66\n"
         "きょうさ\t교사\tN110\t\t\tGN\t67\n"
         "うち\t집\tN110\tVPl\t\tGN\t58\n"
         "ウチ\t우리\tN110\t\t\tGN\t59\n"
         "フトン\t이불\tN110\t\t\tGN\t60\n"
         "ふとん\t이부자리\tN110\t\t\tGN\t61\n"
         "猫\t고양이\tN110\t\t\tGN\t68\n"
         "猫\t네코\tN130\tQPN\tWb\tGN\t69\n"
         "たま\t구슬\tN110\t\t\tGN\t70\n"
         "妹\t여동생\tN110\tVHn\t\tGN\t71\n"
         "ゆき\t눈\tN110\t\t\tGN\t72\n"
         "誓\t맹세&AV@\tM440\t\t\tGN\t73\n"
         "ちか\t맹세&AV@\tM440\t\t\tGN\t74\n"
         "母\t어머니\tN110\tVHn\t\tGN\t75\n"
         "日\t날\tN110\t\t\tGN\t76\n"
         "空\t하늘\tN110\t\t\tGN\t79\n"
         "そら\t자\tN110\t\t\tGN\t80\n"
         "星\t별\tN110\t\t\tGN\t81\n"
         "星\t호시\tN130\tQPN\tWa\tGN\t82\n"
         "犬\t개\tN110\t\t\tGN\t83\n"
         "ベルクロア\t벨크로아\tN130\tQPN\t\tGN\t88\n"
         "ユフィリカ\t유필리카\tN130\tQPN\t\tGN\t92\n"
         "ベルンハルト\t베른할트\tN130\tQPN\t\tGN\t93\n"
         "シャルロッテ\t샬롯테\tN130\tQPN\t\tGN\t94\n"
         "ミリアム\t밀리암\tN130\tQPN\t\tGN\t95\n"
         "アレクシス\t알렉시스\tN130\tQPN\t\tGN\t96\n"
         "恭\t야스시\tN130\tQPN\tWb\tGN\t97\n"
         "恭\t공\tN110\tXXX\tOn\tGN\t98\n"
         "きょう\t오늘\tN110\tUMe\t\tGN\t99\n"
         "拍手\t박수\tN110\t\t\tGN\t100\n"
         "佐藤\t사토우\tN130\tQPN\tWa\tGN\t101\n"
         "大野\t오오노\tN130\tQPN\tWa\tGN\t102\n"
         "結城\t유우키\tN130\tQPN\tWa\tGN\t103\n"
         "大上\t오우에\tN130\tQCTVPl\t\tGN\t104\n"
         "ダンジョン\t던전\tN110\tVPl\t\tGN\t106\n"
         "ゲート\t게이트\tN110\t\t\tGN\t107\n"
         "セット\t세트\tN110\t\t\tGN\t108\n"
         "スキル\t스킬\tN110\t\t\tGN\t109\n"
         "スケルトン\t스켈레톤\tN110\t\t\tGN\t110\n"},
        {0, "names.tsv",
         "#! layer=names\n"
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "椎名\t시이나\tN130\tQPN\tWa\tGN\t12\n"
         "周\t아마네\tN130\tQPN\tWb\tGN\t13\n"
         "樹\t이츠키\tN130\tQPN\tWb\tGN\t14\n"
         "ハル\t하루\tN130\tQPNVHn\tWb\tGN\t16\n"
         "桃花\t모모카\tN130\tQPNVHn\tWb\tGN\t22\n"
         "つぐみ\t츠구미\tN130\tQPNVHn\tWb\tGN\t23\n"
         "がび\t가비\tN130\tQPNVHn\tWb\tGN\t24\n"
         "あおい\t아오이\tN130\tQPNVHn\tWb\tGN\t27\n"
         "ひかり\t히카리\tN130\tQPNVHn\tWb\tGN\t28\n"
         "ひよこ\t히요코\tN130\tQPNVHn\tWb\tGN\t31\n"
         "サーラ\t사라\tN130\tQPNVHn\tWb\tGN\t62\n"
         "たま\t타마\tN130\tQPNVHn\tWb\tGN\t77\n"
         "ゆき\t유키\tN130\tQPNVHn\tWb\tGN\t78\n"
         "空\t소라\tN130\tQPNVHn\tWb\tGN\t84\n"
         "そら\t소라\tN130\tQPNVHn\tWb\tGN\t85\n"
         "ほし\t호시\tN130\tQPNVHn\tWb\tGN\t86\n"
         "ぽち\t포치\tN130\tQPNVHn\tWb\tGN\t87\n"
         "セリーヌ・アルドルフェン\t셀린 알돌펜\tN130\tQPNVHn\t\tGN\t89\n"
         "ミレイユ・ベルクロア\t미레유 베르크로아\tN130\tQPNVHn\t\tGN\t90\n"
         "ニーナ・ルーナ\t니나루나\tN130\tQPNVHn\t\tGN\t91\n"
         "大上\t오오카미\tN130\tQPNVHn\tWb\tGN\t105\n"},
    });
    return loaded.snapshot.get();
}

std::string Repeat(const std::string& line, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += line + "\n";
    return out;
}

// The registry of doc, with every line added.
names::Registry Scan(const dict::Snapshot* snap, const std::string& doc) {
    names::Registry r(snap);
    size_t start = 0;
    for (;;) {
        const size_t nl = doc.find('\n', start);
        r.Add(doc.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return r;
}

// The surfaces of line's occurrences, a "!" after a guarded one, separated by spaces.
std::string Occurrences(names::Registry& r, const std::string& line) {
    std::string out;
    for (const names::Occurrence& o : r.Occurrences(line)) {
        if (!out.empty()) out += " ";
        out += o.surface;
        if (o.noun) out += "!";
    }
    return out;
}

}  // namespace

HD_TEST(names_strip_ruby) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"|桃花《ももか》ちゃん", "桃花ちゃん"},
        {"｜幻晶騎士《シルエットナイト》", "幻晶騎士"},
        {"猫猫《マオマオ》", "猫猫"},
        {"<ruby>桃花<rt>ももか</rt></ruby>ちゃん", "桃花ちゃん"},
        {"<ruby><rb>桃花</rb><rp>（</rp><rt>ももか</rt><rp>）</rp></ruby>", "桃花"},
        {"《》だけ", "《》だけ"},
        {"｜魔導騎士（マジックナイト）が", "魔導騎士が"},
        {"運行情報 | 京都(北部)へ", "運行情報 | 京都(北部)へ"},
        {"｜騎士（の話）", "｜騎士（の話）"},
        // A half-width bar, or one before kana, separates.
        {"名前|田中(たなか)", "名前|田中(たなか)"},
        {"｜メニュー(めにゅう)", "｜メニュー(めにゅう)"},
    };
    for (const auto& [in, want] : cases) HD_CHECK_EQ(names::StripRuby(in), want);
}

HD_TEST(names_drop_readings) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r(snap);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"天井の梁（はり）から", "天井の梁から"},
        {"錆（さ）びない", "錆びない"},
        {"和気藹々(わきあいあい)とした", "和気藹々とした"},
        {"生（ナマ）食", "生食"},
        {"山田太郎（やまだ たろう）。", "山田太郎。"},
        // One space may stand before the parenthesis; then only a reading the dictionaries
        // confirm (a word with the same Korean, the sound of a name) is dropped.
        {"秋刀魚 (さんま) を焼く", "秋刀魚 を焼く"},
        {"高森 (たかもり) が来た", "高森 が来た"},
        {"天井の梁 (はり) から", "天井の梁 (はり) から"},
        {"結果 （まあまあ） だった。", "結果 （まあまあ） だった。"},
        {"全「すべ」てを壊「こわ」せる", "全てを壊せる"},
        // A noun of the same kind, any word over a word of no dictionary.
        {"朝餉(あさがれい)を", "朝餉を"},
        {"夾叉(きょうさ)か", "夾叉か"},
        {"心「こころ」に", "心に"},
        // A name reading the kanji is written with in either dictionary.
        {"空（そら）を見上げた", "空を見上げた"},
        {"星（ほし）が綺麗だ", "星が綺麗だ"},
        // In 「」 after one kanji, a reading in a line whose every kanji run has one.
        {"あの日「ひ」の誓「ちか」いを", "あの日の誓いを"},
        // A name quoted after a noun, a quote before と, a quote that ends the line.
        {"戦艦「やまと」の", "戦艦「やまと」の"},
        {"私「こんびに」とやら", "私「こんびに」とやら"},
        {"紗英「おはよう」", "紗英「おはよう」"},
        // No word of the base's: a quote after a speaker, a name after a noun, a remark,
        // and fewer morae than kanji.
        {"姉「うん」って言った。", "姉「うん」って言った。"},
        {"姉「はい」って言った。", "姉「はい」って言った。"},
        {"店「まるや」で買った。", "店「まるや」で買った。"},
        {"結果（まあまあ）だった。", "結果（まあまあ）だった。"},
        {"理由(なし)", "理由(なし)"},
        {"鉄道株式会社（ながてつ）の人", "鉄道株式会社（ながてつ）の人"},
        // A name over a noun, and a word in 「」 beside a reading in a line with other
        // kanji that have none.
        {"猫（タマ）が鳴いた", "猫（タマ）が鳴いた"},
        {"妹（ゆき）が来た", "妹（ゆき）が来た"},
        {"猫（たま）が鳴いた", "猫（たま）が鳴いた"},
        {"誓「ちか」いの言葉と母「はい」の返事", "誓いの言葉と母「はい」の返事"},
        // A katakana word over the kanji, a pronoun over a person noun, an aside.
        {"炎（ブレス）", "炎（ブレス）"},
        {"提督(あなた)、", "提督(あなた)、"},
        {"一部隊（十騎）", "一部隊（十騎）"},
        // More morae than one kanji is read with, mixed scripts, no kanji before.
        {"猫（ねこねこねこ）", "猫（ねこねこねこ）"},
        {"種（ポストカードつき）", "種（ポストカードつき）"},
        {"これ（はり）", "これ（はり）"},
        {"梁（ ）", "梁（ ）"},
    };
    for (const auto& [in, want] : cases) HD_CHECK_EQ(r.DropReadings(in), want);
}

HD_TEST(names_caption_inline_readings) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r(snap);
    const std::string key = r.Key();
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"（恭(きょう)）今日は", "（쿄）今日は"},
        {"(恭（きょう）)はい", "(쿄)はい"},
        {"（恭(キョウ)）はい", "（쿄）はい"},
        {"⚟（恭(きょう)）はい", "⚟（쿄）はい"},
        {"[無線]（恭(きょう)）はい", "[無線]（쿄）はい"},
        {"（光(ひかり)）はい", "（히카리）はい"},
        {"（椎名(しいな)）はい", "（시이나）はい"},
        {"（佐藤(さとう)）はい", "（사토）はい"},
        {"（大野(おおの)）はい", "（오노）はい"},
        {"（結城(ゆうき)）はい", "（유키）はい"},
        {"（大上(おおうえ)）はい", "（오우에）はい"},
        {"大上(おおうえ)に行く", "大上に行く"},
        {"（恭(きょう)／光(ひかり)）はい", "（쿄／히카리）はい"},
        {"（恭(きょう)さん）はい", "（쿄さん）はい"},
        {"（恭(きょう)）はい。恭は光を見る", "（쿄）はい。恭は光を見る"},
        {"（恭(きょう)）", "（恭）"},
        {"（恭(きょう)）\nはい", "（恭）\nはい"},
        {"恭(きょう)と光(ひかり)", "恭と光"},
        {"（恭(きょう)）と思った", "（恭）と思った"},
        {"前の文（恭(きょう)）はい", "前の文（恭）はい"},
        {"（恭 (きょう)）はい", "（恭 (きょう)）はい"},
        {"（恭(きょう))はい", "（恭)はい"},
        {"（恭(きょう）)はい", "（恭)はい"},
        {"（恭(俳優)）はい", "（恭(俳優)）はい"},
        {"（恭(声・きょう)）はい", "（恭(声・きょう)）はい"},
        {"（恭(きょう・な)）はい", "（恭）はい"},
        {"（恭(きょう な)）はい", "（恭）はい"},
        {"（恭(笑)）はい", "（恭(笑)）はい"},
        {"（医師(いし)）はい", "（医師）はい"},
        {"（母(はは)）はい", "（母）はい"},
        {"（注(ちゅう)）はい", "（注）はい"},
        {"（拍手(はくしゅ)）はい", "（拍手）はい"},
        {"天井の梁（はり）から", "天井の梁から"},
        {"結果（まあまあ）だった", "結果（まあまあ）だった"},
    };
    for (const auto& [in, want] : cases) HD_CHECK_EQ(r.DropReadings(in), want);
    std::string cues = "stale";
    HD_CHECK_EQ(r.DropReadings("（光(ひかり)）はい", &cues), "（히카리）はい");
    HD_CHECK_EQ(cues, "（光）はい");
    HD_CHECK_EQ(r.DropReadings("（光）はい", &cues), "（光）はい");
    HD_CHECK(cues.empty());
    HD_CHECK_EQ(r.DropReadings("光", &cues), "光");
    HD_CHECK(cues.empty());
    std::vector<std::string> labels;
    HD_CHECK_EQ(r.DropReadings("（恭(きょう・な)）はい", &cues, &labels), "（恭）はい");
    HD_CHECK(cues.empty() && labels.empty());
    HD_CHECK_EQ(r.Key(), key);
    HD_CHECK_EQ(r.DropReadings("（光(ひかる)）はい"), "（히카루）はい");
    HD_CHECK_EQ(r.DropReadings("（光(ひかり)）はい"), "（히카리）はい");
}

HD_TEST(names_caption_existing_part_readings) {
    const auto loaded = dict::Load({
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "安田\t야스다\tN130\tQPN\tWa\tGN\t1\n"
         "美沙子\t미사코\tI130\tQPN\t\tGN\t2\n"
         "丸山\t마루야마\tN130\tQPN\tWa\tGN\t3\n"
         "和也\t카즈야\tN130\tQPN\tWb\tGN\t4\n"
         "木村\t키무라\tN130\tQPN\tWa\tGN\t5\n"
         "祐一\t유이치\tN130\tQPN\tWb\tGN\t6\n"
         "小杉\t코스기\tN130\tQPN\tWa\tGN\t7\n"
         "竜一\t류이치\tN130\tQPN\tWb\tGN\t8\n"
         "岩井\t이와이\tN130\tQPN\tWa\tGN\t9\n"
         "元気\t겐키\tN130\tQPN\tWb\tGN\t10\n"
         "小宮\t코미야\tN130\tQPN\tWa\tGN\t11\n"
         "浩信\t히로노부\tN130\tQPN\tWb\tGN\t12\n"
         "相田\t아이다\tN130\tQPN\tWa\tGN\t13\n"
         "周二\t슈우지\tN130\tQPN\tWa\tGN\t14\n"
         "山崎\t야마자키\tN130\tQPN\tWa\tGN\t17\n"
         "弘也\t히로야\tN130\tQPN\tWb\tGN\t18\n"},
        {0, "names.tsv",
         "#! layer=names\n"
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "美沙子\t미사코\tN130\tQPN\tWb\tGN\t15\n"
         "周二\t슈지\tN130\tQPN\tWb\tGN\t16\n"},
    });
    HD_REQUIRE(loaded.snapshot != nullptr);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"（安田美沙子(みさこ)）毎年 見てます", "（야스다 미사코）毎年 見てます"},
        {"（丸山和也(かずや)）これは…", "（마루야마 카즈야）これは…"},
        {"（木村祐一(ゆういち)）盛り上がっておりますが", "（키무라 유이치）盛り上がっておりますが"},
        {"（小杉竜一(りゅういち)）はい どうも", "（코스기 류이치）はい どうも"},
        {"（岩井元気(げんき)）いや もう", "（이와이 겐키）いや もう"},
        {"（小宮浩信(ひろのぶ)）絶対 受かってね", "（코미야 히로노부）絶対 受かってね"},
        {"（相田周二(しゅうじ):小宮のモノマネで）絶対受かって", "（아이다 슈지:小宮のモノマネで）絶対受かって"},
        {"（安田美沙子(やすだみさこ)）毎年", "（야스다 미사코）毎年"},
        {"（山崎弘也(やまざきひろなり)）しかし？", "（야마자키 히로나리）しかし？"},
        {"（安田美沙子(ひかり)）毎年", "（安田美沙子）毎年"},
    };
    for (const auto& [input, want] : cases) {
        names::Registry r = Scan(loaded.snapshot.get(), input);
        const std::string key = r.Key();
        HD_CHECK_EQ(r.DropReadings(input), want);
        HD_CHECK_EQ(r.Key(), key);
    }
    const auto ambiguous = dict::Load({
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "安田\t미사코\tN130\tQPN\tWa\tGN\t1\n"
         "美沙子\t미사코\tN130\tQPN\tWb\tGN\t2\n"},
    });
    HD_REQUIRE(ambiguous.snapshot != nullptr);
    names::Registry r = Scan(ambiguous.snapshot.get(), "（安田美沙子(みさこ)）毎年");
    std::vector<std::string> names;
    HD_CHECK_EQ(r.DropReadings("（安田美沙子(みさこ)）毎年", nullptr, &names), "（安田美沙子）毎年");
    HD_CHECK(names.empty());
}

// A hiragana run in parentheses that is no reading of the kanji before it and no word
// is a name; a word or a reading there is none.
HD_TEST(names_kana_name_in_parentheses) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r = Scan(snap, "鉄道株式会社（ながてつ）の人");
    const std::vector<std::string> found = r.Names();
    HD_CHECK(std::find(found.begin(), found.end(), "ながてつ") != found.end());
    HD_CHECK_EQ(r.Render("ながてつ"), "나가테츠");
    // A name of the name layer over one kanji, when it is no noun.
    const std::vector<std::string> pet = Scan(snap, "犬（ぽち）が鳴いた").Names();
    HD_CHECK(std::find(pet.begin(), pet.end(), "ぽち") != pet.end());
    HD_CHECK(Scan(snap, "妹（ゆき）が来た").Names().empty());
    HD_CHECK(Scan(snap, "理由(なし)").Names().empty());
    HD_CHECK(Scan(snap, "天井の梁（はり）から").Names().empty());
}

HD_TEST(names_speaker_cues) {
    HD_CHECK_EQ(names::SpeakerCues("紗英「おはよう」\n恭平です。").size(), size_t{2});
    HD_CHECK_EQ(names::SpeakerCues("そう言って、おはよう").size(), size_t{0});
}

HD_TEST(names_labels) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, bool>> cases = {
        {Repeat("紗英「おはよう」", 3), true},
        {Repeat("紗英「おはよう」", 2), false},  // a plain-text form must recur
        {Repeat("【紗英】「おはよう」", 3), true},
        {Repeat("紗英：おはよう", 3), true},
        {Repeat("紗英 : 「おはよう」", 3), true},
        {Repeat("[紗英]: 「おはよう」", 3), true},
        {"OutputLine(\"<color=#5ec69a>紗英</color>\", NULL, \"<color=#5ec69a>사에</color>\", NULL, Line_ContinueAfterTyping);", true},
        {"define m = Character(\"紗英\")\nm \"おはよう\"", true},
        {"[name text=\"紗英\"]おはよう", true},
        {"@nm t=\"紗英\"", true},
        {"#紗英\nおはよう", true},
        {Repeat("そう言って「おはよう」", 3), false},
        {Repeat("紗英の母「おはよう」", 3), false},
        {Repeat("女子生徒Ａ「おはよう」", 3), false},
        {Repeat("紗英？「おはよう」", 3), false},
        {Repeat("// 紗英「おはよう」", 3), false},
        // A name box: the name alone on a line, its quote on the next.
        {Repeat("紗英\n「おはよう」", 3), true},
        {Repeat("紗英\n「おはよう」", 2), false},
        {Repeat("紗英\nおはよう。", 3), false},
    };
    for (const auto& [doc, want] : cases) {
        names::Registry r = Scan(snap, doc);
        const std::vector<std::string> names = r.Names();
        const bool got = std::find(names.begin(), names.end(), "紗英") != names.end();
        HD_CHECK_EQ(got, want);
    }
    // A joint label names both; Xの母 counts once for X.
    names::Registry r = Scan(snap, Repeat("桃花＆奈津子「おはよう」", 3) + "OutputLine(\"恭平の母\", NULL, \"어머니\", NULL, 0);\n恭平さん");
    HD_CHECK(r.Names() == (std::vector<std::string>{"奈津子", "恭平", "桃花"}));
    // A name box of hiragana names the speaker and renders the name.
    {
        names::Registry box = Scan(snap, Repeat("のぞみ\n「……え？」", 3));
        HD_CHECK(box.Names() == (std::vector<std::string>{"のぞみ"}));
        HD_CHECK_EQ(box.Render("のぞみ"), std::string("노조미"));
    }
    // A hiragana person of the name layer that no other dictionary reads is a name before an
    // honorific, also after a particle; a particle after a kanji starts no name (体がびくん).
    // It stands for the person alone after one honorific, a common word (ひかり) after four;
    // the form of an adjective (あおい) only where an honorific follows it.
    {
        const std::vector<std::string> tsugumi = {"つぐみ"};
        names::Registry once = Scan(snap, "「つぐみちゃん的にはどう？」\n");
        HD_CHECK(once.Names() == tsugumi);
        HD_CHECK(once.HonorificOnlyNames().empty());
        HD_CHECK_EQ(Occurrences(once, "つぐみちゃん"), std::string("つぐみ"));
        HD_CHECK_EQ(Occurrences(once, "デスク、つぐみが唸る。"), std::string("つぐみ"));
        HD_CHECK_EQ(Occurrences(once, "妹のつぐみちゃん。"), std::string("つぐみ"));
        HD_CHECK_EQ(Occurrences(once, "ねえつぐみちゃん"), std::string("つぐみ"));
        HD_CHECK(Scan(snap, "「ねえつぐみちゃん」\n").HonorificOnlyNames().empty());
        names::Registry twice = Scan(snap, "「つぐみちゃん的にはどう？」\n妹のつぐみちゃん。\n");
        HD_CHECK(twice.Names() == tsugumi);
        HD_CHECK(twice.HonorificOnlyNames().empty());
        names::Registry four = Scan(snap, Repeat("ひかりちゃん、おはよう。", 4));
        HD_CHECK(four.HonorificOnlyNames().empty());
        HD_CHECK_EQ(Occurrences(four, "ひかりは笑った。"), std::string("ひかり"));
        HD_CHECK_EQ(Occurrences(four, "最後のひかりだった。"), std::string("ひかり!"));
        HD_CHECK(Scan(snap, Repeat("体がびくんと跳ねる。", 3)).Names().empty());
        names::Registry aoi = Scan(snap, Repeat("あおいさんが来た。", 3));
        HD_CHECK(aoi.HonorificOnlyNames() == (std::vector<std::string>{"あおい"}));
        HD_CHECK_EQ(Occurrences(aoi, "あおいさん、待って！"), std::string("あおい"));
        HD_CHECK_EQ(Occurrences(aoi, "あおい空が広がる。"), std::string("あおい!"));
        HD_CHECK(Scan(snap, Repeat("ひかりちゃん、おはよう。", 3)).HonorificOnlyNames() == (std::vector<std::string>{"ひかり"}));
        // A creature noun is what speech addresses with an honorific (ひよこさん), and the ちゃん
        // of the adverb ちゃんと before a verb is no honorific.
        HD_CHECK(Scan(snap, Repeat("ねえひよこさん", 3)).Names().empty());
        HD_CHECK(Scan(snap, Repeat("つぐみちゃんと話そう", 3)).Names().empty());
        HD_CHECK(Scan(snap, Repeat("つぐみちゃんと遊んだ", 3)).Names() == tsugumi);
    }
    // A reading in brackets that the word goes on after is ruby, no label (全「すべ」て).
    HD_CHECK(Scan(snap, Repeat("全「すべ」てを壊せる", 3)).Names().empty());
    // A gloss that is a word of its own reads the word, no name (提督(あなた), 私(わたくし));
    // one that is a name and no word names (三日(みか)).
    {
        std::string got;
        for (const std::string& n : Scan(snap, Repeat("提督(あなた)、何いってんの！", 3) + Repeat("私(わたくし)、行くわ。", 3)).Names()) got += n + " ";
        HD_CHECK_EQ(got, std::string());
    }
    const std::vector<std::string> mika = Scan(snap, Repeat("三日(みか)が来た。", 3)).Names();
    HD_CHECK(std::find(mika.begin(), mika.end(), "三日") != mika.end());
    // A reading in no dictionary after a word that is only a word reads the word.
    HD_CHECK(Scan(snap, Repeat("用途(ようと)はわからない。", 3)).Names().empty());
    // A reading that is also a name reads the word when a word of it has the base's Korean.
    HD_CHECK(Scan(snap, Repeat("秋刀魚(さんま)を焼いた。", 3)).Names().empty());
    const std::vector<std::string> chisato = Scan(snap, Repeat("千里(ちさと)が来た。", 3)).Names();
    HD_CHECK(std::find(chisato.begin(), chisato.end(), "千里") != chisato.end());
    // A given name split from a full name that is a common word does not stand for the
    // person elsewhere (高森光: 光 stays the word).
    const std::vector<std::string> hikaru = Scan(snap, Repeat("高森光は来た。", 3)).Names();
    HD_CHECK(std::find(hikaru.begin(), hikaru.end(), "光") == hikaru.end());
    // One of two kanji or more does where the full name is written more than once (高森真昼 twice).
    const std::vector<std::string> mahiru = Scan(snap, Repeat("高森真昼は来た。", 2)).Names();
    HD_CHECK(std::find(mahiru.begin(), mahiru.end(), "真昼") != mahiru.end());
    const std::vector<std::string> noon = Scan(snap, "高森真昼は来た。").Names();
    HD_CHECK(std::find(noon.begin(), noon.end(), "真昼") == noon.end());
    // A surname of a full name that is a common word does not stand for the person
    // elsewhere (桃花, a word in the fixture: 桃花恭平 keeps 桃花 the word).
    const std::vector<std::string> rika = Scan(snap, Repeat("恭平さん、おはよう。", 3) + Repeat("桃花恭平が来た。", 3)).Names();
    HD_CHECK(std::find(rika.begin(), rika.end(), "桃花") == rika.end());
    // A bracket that the line goes on after is a title, not a speaker's quote.
    HD_CHECK(Scan(snap, Repeat("桃花「第二部隊」、出撃せよ！", 3)).Names().empty());
    // A numeral is no surname before a name (二美咲).
    const std::vector<std::string> misaki = Scan(snap, Repeat("美咲さん、おはよう。", 3) + Repeat("二美咲が来た。", 3)).Names();
    HD_CHECK(std::find(misaki.begin(), misaki.end(), "二") == misaki.end());
}

HD_TEST(names_honorifics) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
        {Repeat("桃花ちゃん、おはよう。", 4), {"桃花"}},
        {Repeat("桃花ちゃん、おはよう。", 3), {}},  // a common word scores 0.5 per honorific
        {"柚葉さん", {}},
        {"柚葉さん\n柚葉さん", {"柚葉"}},
        {"光さんが来た。\n光さんが言った。", {}},  // a single kanji needs 3
        {"光さんが来た。\n光さんが言った。\n光さん、", {"光"}},
        {"光さんだ。\n光さんだ。\n光さんだ。", {}},
        {"西九条先生が来た。\n西九条先生だ。", {"西九条"}},  // a fused kanji honorific is split off
        {"お父さんが来た。\nお父さんだ。\nお父さん", {}},
        {"神様が来た。\n神様だ。\n神様", {}},
        {"ウェイトレスさん\nウェイトレスさん\nウェイトレスさん", {}},
        {"見たんだ。\n見たんだ。\n見たんだ。", {}},
        {Repeat("リン様「来ないで」", 4), {"リン"}},
        // The ちゃんと after a word that is no noun is the adverb (大丈夫ちゃんと), after a name
        // the honorific and a particle.
        {Repeat("大丈夫ちゃんと手当てしてもらった。", 8), {}},
        {Repeat("桃花ちゃんと遊んだ。", 4), {"桃花"}},
    };
    for (const auto& [doc, want] : cases) {
        names::Registry r = Scan(snap, doc);
        HD_CHECK(r.Names() == want);
    }
    // A common word that honorifics show to be a person reads as the person of the name layer.
    HD_CHECK_EQ(Scan(snap, Repeat("桃花ちゃん、おはよう。", 4)).Render("桃花"), std::string("모모카"));
    names::Registry r = Scan(snap, Repeat("リン様", 4));
    HD_CHECK_EQ(r.Render("リン"), std::string("린"));
}

// A stray continuation byte before an honorific or in a speaker line is read as one
// replacement character; scanning never steps back past the start of the line.
HD_TEST(names_malformed_utf8) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    for (const std::string doc : {"\xE3\x81\x82\x80さん", "\x80さん", "\x80\x80つぐみちゃん", "つぐみ\x80\n「おはよう」",
                                  "\xE3\x80\x80\x80"}) {
        names::Registry r = Scan(snap, doc);
        HD_CHECK(r.Names().size() <= 1);
    }
}

// A katakana word that introduces itself with と名乗る is a name in the whole document, also when
// the dictionary has it as a common word; a kanji word, a role and other quotes are not.
HD_TEST(names_self_introduction) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
        {"ハルと名乗った少女が来た。\nハルは笑った。", {"ハル"}},
        {"ソードと名乗る男", {"ソード"}},
        {"ハルと言った。\nハルは笑った。", {}},
        {"ウェイトレスと名乗った女", {}},
        {"魔王と名乗った男", {}},
    };
    for (const auto& [doc, want] : cases) {
        names::Registry r = Scan(snap, doc);
        HD_CHECK(r.Names() == want);
    }
}

HD_TEST(names_occurrences_and_guards) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::array<std::string, 3>> cases = {
        {"", "光が来た。", ""},
        {Repeat("光さん、", 3), "光が来た。", "光"},
        {Repeat("光さん、", 3), "光線を出す。", ""},
        {Repeat("光さん、", 3), "光る石を見た。", "光!"},
        {Repeat("光さん、", 3), "光が差す。", "光!"},
        {Repeat("光さん、", 3), "光さん、光が差すよ。", "光 光"},
        {Repeat("乾さん、", 3), "服が乾いてきた。", "乾!"},
        {Repeat("乾さん、", 3), "乾が来た。", "乾"},
        {Repeat("桜さん、", 3), "桜の花が咲いた。", "桜!"},
        {Repeat("桜さん、", 3), "桜が咲いた。", "桜!"},
        {Repeat("桜さん、", 3), "桜は学生です。", "桜"},
        {Repeat("柚葉さん", 2), "柚葉達が来た。", "柚葉"},
        {Repeat("柚葉さん", 2), "柚葉祭だ。", ""},
        {Repeat("柚葉さん", 2) + "水野柚葉が来た。", "水野柚葉だ。", "水野柚葉"},
    };
    for (const auto& c : cases) {
        names::Registry r = Scan(snap, c[0]);
        HD_CHECK_EQ(Occurrences(r, c[1]), c[2]);
    }
}

HD_TEST(names_dict_rows) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r = Scan(snap, "宝生ミナ（ほうしょうみな）\n" + Repeat("高森恭平「おはよう」", 3) + Repeat("恭平くん", 2));
    HD_CHECK_EQ(r.Dict(),
                std::string("宝生ミナ\t호우쇼우 미나\tname\n恭平\t쿄헤이\tname\n"
                            "高森\t타카모리\tsurname\n高森恭平\t타카모리 쿄헤이\tname\nちゃん\t짱\thon\n"));
    r.chan = "쨩";
    const std::string dict = r.Dict();
    const std::string tail = "ちゃん\t쨩\thon\n";
    HD_CHECK(dict.size() >= tail.size() && dict.compare(dict.size() - tail.size(), tail.size(), tail) == 0);
}

HD_TEST(names_readings_without_table) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::pair<std::string, text::Preset>>> cases = {
        {"宝生ミナ（ほうしょうみな）だ。", {"宝生ミナ", text::Preset::Fan}},
        {"宝生ミナ（ほうしょうみな）だ。", {"宝生ミナ", text::Preset::FanShort}},
        {"|桃花《ももか》が来た。", {"桃花", text::Preset::Fan}},
        {"<ruby>桃花<rt>トウカ</rt></ruby>が来た。", {"桃花", text::Preset::Fan}},
    };
    const std::vector<std::string> want = {"호우쇼우 미나", "호쇼 미나", "모모카", "토우카"};
    for (size_t i = 0; i < cases.size(); i++) {
        names::Registry r = Scan(snap, cases[i].first);
        r.preset = cases[i].second.second;
        HD_CHECK_EQ(r.Render(cases[i].second.first), want[i]);
    }
    // A dictionary person reading is used when no reading table entry exists.
    names::Registry blank = Scan(snap, Repeat("紗英「おはよう」", 3));
    HD_CHECK_EQ(blank.Render("紗英"), std::string("사에"));
    names::Registry plain = Scan(snap, Repeat("光さん、", 3));
    HD_CHECK_EQ(plain.Render("光"), std::string("히카루"));
    names::Registry shota = Scan(snap, Repeat("翔太「おはよう」", 3));
    HD_CHECK_EQ(shota.Render("翔太"), std::string("쇼타"));
}

HD_TEST(names_katakana_full_names) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r = Scan(snap, Repeat("アル君、おはよう。", 2) + Repeat("アルガルド様が来た。", 2) +
                                       "王太子、アルガルド・ボナ・パレッティア。\nアルガルドは王子だ。");
    HD_CHECK_EQ(r.Render("アル"), std::string("아르"));
    HD_CHECK_EQ(r.Render("アルガルド"), std::string("아르가르드"));
    HD_CHECK_EQ(r.Render("アルガルド・ボナ・パレッティア"), std::string("아르가르드 보나 파렛티아"));

    names::Registry alone = Scan(snap, Repeat("アル君、おはよう。", 2));
    HD_CHECK_EQ(alone.Render("アル"), std::string("알"));
    // A whole katakana name reads as the name layer's person reading of its kana, whatever
    // the base dictionary makes of the word.
    names::Registry hal = Scan(snap, Repeat("ハル君、おはよう。", 8));
    HD_CHECK_EQ(hal.Render("ハル"), std::string("하루"));

    names::Registry one = Scan(snap, "アルくんが来た。\n" + Repeat("アルガルドは王子だ。", 2));
    HD_CHECK_EQ(one.Render("アル"), std::string("아르"));
    HD_CHECK_EQ(one.Render("アルガルド"), std::string("아르가르드"));
    HD_CHECK(Scan(snap, "アルくんが来た。\nアルガルドは王子だ。").Names().empty());

    names::Registry u(snap);
    u.SetName("アル", "알");
    for (const std::string& line : {std::string("アル君、おはよう。"), std::string("アル君、おはよう。"), std::string("アルガルド様が来た。"), std::string("アルガルド様が来た。")}) u.Add(line);
    HD_CHECK_EQ(u.Render("アル"), std::string("알"));
    HD_CHECK_EQ(u.Render("アルガルド"), std::string("아르가르드"));

    names::Registry chain = Scan(snap, "ソード・ワールドの話。");
    HD_CHECK(chain.Names().empty());

    names::Registry m = Scan(snap, "ユフィリア・マゼンタは目を見開いた。\nグランツ・マゼンタ公爵");
    HD_CHECK_EQ(m.Render("グランツ・マゼンタ"), std::string("그란츠 마젠타"));
    HD_CHECK_EQ(m.Render("マゼンタ"), std::string("마젠타"));
    HD_CHECK_EQ(m.Render("ユフィリア"), std::string("유피리아"));
    m.SetName("ユフィリア", "유필리아");
    HD_CHECK_EQ(m.Render("ユフィリア・マゼンタ"), std::string("유필리아 마젠타"));
}

// A katakana name before an honorific that starts a longer name of the document reads as the
// leading syllables of that name's reading that spell it, without a final it lacks; with no
// longer name it reads as itself.
HD_TEST(names_prefix_of_learned_name) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    struct Case {
        std::string full, kr, nick, want;
    };
    const Case cases[] = {
        {"ユフィリカ", "유필리카", "ユフィ", "유피"}, {"レオンハルト", "레온하르트", "レオ", "레오"},
        {"ベルンハルト", "베른할트", "ベル", "베르"}, {"アルガルド", "아르가르드", "アル", "아르"},
        {"シャルロッテ", "샬롯테", "シャル", "샬"}, {"レオンハルト", "레온하르트", "レオン", "레온"},
        {"ミリアム", "밀리암", "ミリ", "밀리"}, {"アレクシス", "알렉시스", "アレク", "알렉"},
    };
    for (const Case& c : cases) {
        names::Registry r = Scan(snap, c.full + "様は剣を抜いた。\n" + c.full + "様は笑った。\n" + c.nick + "くん、おはよう。");
        HD_CHECK_EQ(r.Render(c.full), c.kr);
        HD_CHECK_EQ(r.Render(c.nick), c.want);
    }
    names::Registry alone = Scan(snap, Repeat("レオくん、おはよう。", 2));
    HD_CHECK_EQ(alone.Render("レオ"), std::string("레오"));
}

// A common katakana word is no short form of a longer run, a run that goes on in words after a
// name or a common word is a compound, not a longer name, and a label of common words is a
// field.
HD_TEST(names_common_katakana_words) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::string> none;
    HD_CHECK(Scan(snap, Repeat("ダンジョンゲート", 2) + "ダンジョンさんの意思だ。\n試練はダンジョンさんだ。").Names() == none);
    HD_CHECK(Scan(snap, Repeat("スケルトンくんが来た。", 2) + Repeat("スケルトンナイトが来た。", 2)).Names() == none);
    HD_CHECK(Scan(snap, Repeat("セットスキルさんが来た。", 3)).Names() == none);
    HD_CHECK(Scan(snap, Repeat("セットスキル：剣術", 3)).Names() == none);
    HD_CHECK(Scan(snap, Repeat("アルくんが来た。", 2) + Repeat("アルゲートが来た。", 2)).Names() ==
             (std::vector<std::string>{"アル"}));

    const std::vector<std::string> leon = {"レオン"};
    HD_CHECK(Scan(snap, Repeat("レオン様が来た。", 2) + Repeat("レオンアーチャーが来た。", 2) + "アーチャーが来た。").Names() ==
             leon);
    HD_CHECK(Scan(snap, Repeat("レオン様が来た。", 2) + Repeat("レオンスキルが来た。", 2)).Names() == leon);

    // A name of no dictionary before an honorific, as a speaker label, or one the name layer
    // knows as a person.
    HD_CHECK(Scan(snap, Repeat("ミナさんが来た。", 2)).Names() == (std::vector<std::string>{"ミナ"}));
    HD_CHECK(Scan(snap, Repeat("リリカ「おはよう」", 3)).Names() == (std::vector<std::string>{"リリカ"}));
    HD_CHECK(Scan(snap, Repeat("セリス：おはよう", 3)).Names() == (std::vector<std::string>{"セリス"}));
    HD_CHECK(Scan(snap, Repeat("ハル君、おはよう。", 4)).Names() == (std::vector<std::string>{"ハル"}));
    HD_CHECK(Scan(snap, Repeat("メイさんが来た。", 2) + Repeat("メイメイが笑った。", 2)).Names() ==
             (std::vector<std::string>{"メイ", "メイメイ"}));
    names::Registry nick = Scan(snap, "アルくんが来た。\n" + Repeat("アルガルドは王子だ。", 2));
    HD_CHECK(nick.Names() == (std::vector<std::string>{"アル", "アルガルド"}));
}

// The parts of a chain name read as the words of its name-layer person reading, in the
// chain and alone, over a transliteration or a base reading of the part.
HD_TEST(names_chain_parts_from_layer) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r = Scan(snap, "セリーヌ・アルドルフェン。\nミレイユ・ベルクロア、八歳。\nアルドルフェン辺境伯\nベルクロア公爵");
    HD_CHECK_EQ(r.Render("セリーヌ・アルドルフェン"), std::string("셀린 알돌펜"));
    HD_CHECK_EQ(r.Render("アルドルフェン"), std::string("알돌펜"));
    HD_CHECK_EQ(r.Render("ミレイユ・ベルクロア"), std::string("미레유 베르크로아"));
    HD_CHECK_EQ(r.Render("ベルクロア"), std::string("베르크로아"));
    // A reading without a word per part leaves the parts to their own readings.
    names::Registry m = Scan(snap, "ニーナ・ルーナは笑った。");
    HD_CHECK_EQ(m.Render("ニーナ・ルーナ"), std::string("니나 루나"));
}

HD_TEST(names_given_split) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"椎名周が来た。\n周は笑った。", "周"},
        {"椎名樹、それが彼の名だ。", "樹"},
    };
    for (const auto& [doc, given] : cases) {
        names::Registry r = Scan(snap, doc);
        const std::vector<std::string> names = r.Names();
        HD_CHECK(std::find(names.begin(), names.end(), given) != names.end());
    }
    const std::vector<std::pair<std::string, std::string>> absent = {
        {"椎名周りの人", "周"},
        {"田中家に行った。", "家"},
        {"田中達が来た。", "達"},
        {"周は笑った。", "周"},
    };
    for (const auto& [doc, given] : absent) {
        names::Registry r = Scan(snap, doc);
        const std::vector<std::string> names = r.Names();
        HD_CHECK(std::find(names.begin(), names.end(), given) == names.end());
    }
}

HD_TEST(names_shared_tables) {
    names::Registry seed(nullptr);
    HD_REQUIRE(seed.LoadReadings("風音\tかざね\n"));
    seed.SetName("風音", "카자네");
    names::Registry first = seed;
    names::Registry second = seed;
    first.Add("|風音《かざね》");
    second.Add("|風音《かざね》");
    HD_CHECK_EQ(first.Render("風音"), "카자네");
    HD_CHECK_EQ(first.Key(), second.Key());
    const std::string key = second.Key();
    first.SetName("風音", "카논");
    HD_CHECK_EQ(first.Render("風音"), "카논");
    HD_CHECK_EQ(second.Key(), key);
    HD_REQUIRE(first.LoadReadings("風音\tかのん\n"));
    seed.Add("|風音《かざね》");
    HD_CHECK_EQ(seed.Key(), key);
}

// An honorific after a common name is not read as the given name (太郎君 is not 타로 키미).
HD_TEST(names_historical_honorific_not_given) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    for (const char* jp : {"太郎君は走った。", "次郎殿が来た。", "一郎様、どうぞ。"}) {
        const std::string line = jp;
        const std::string rows = names::HistoricalNames(*snap, {&line, 1});
        HD_CHECK(rows.find("君") == std::string::npos && rows.find("殿") == std::string::npos &&
                 rows.find("様") == std::string::npos);
    }
}

HD_TEST(names_prose_katakana) {
    HD_CHECK(names::ProseRuns("サーライチは新しい物が好きだ。") == std::vector<std::string>{"サーライチ"});
    HD_CHECK(names::ProseRuns("サーラ、ミナ、サーライチ、サーラニ。").empty());  // no hiragana
    HD_CHECK(names::ProseRuns("サーライチ、サーラニ、サーラサンだ。").empty());  // mostly katakana
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r = Scan(snap, "ミナはよく走る。\nミナが笑った。\nミナ、ハル。");
    const auto& prose = r.ProseKatakana();
    HD_CHECK(prose.count("ミナ") == 1 && prose.at("ミナ") == 2);
    HD_CHECK(prose.count("ハル") == 0);
}

HD_TEST(names_katakana_siblings) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    // A head written with two other endings in the prose makes head + サン one name.
    names::Registry r = Scan(snap, "ミナイチは新しい物が好きだ。\nミナニは用心深い。\nミナサンは背が高い。");
    HD_CHECK(r.Named("ミナサン"));
    const std::vector<std::string> named = r.NamedSurfaces();
    HD_CHECK(std::find(named.begin(), named.end(), "ミナサン") != named.end());
    // One other ending, or endings that are honorifics, are not enough.
    names::Registry one = Scan(snap, "ヨウタノ本だ。\nヨウタサンは来た。");
    HD_CHECK(!one.Named("ヨウタサン"));
    names::Registry honorifics = Scan(snap, "ヨウタクンは来た。\nヨウタチャンが来た。\nヨウタサンは来た。");
    HD_CHECK(!honorifics.Named("ヨウタサン"));
}

HD_TEST(names_numbered_names) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    // Two numbered runs of a name read as the name's reading and the number's.
    names::Registry r = Scan(snap, "サーライチは新しい物が好きだ。\nサーラニは眠っている。\nサーラサンは背が高い。");
    HD_CHECK_EQ(r.Render("サーライチ"), std::string("사라이치"));
    HD_CHECK_EQ(r.Render("サーラサン"), std::string("사라산"));
    // One numbered run is not enough (サン may be the honorific).
    names::Registry one = Scan(snap, "サーラサンは来た。");
    HD_CHECK_EQ(one.Render("サーラサン"), std::string());
}

HD_TEST(names_favour_doer) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    // A katakana word that does favours in two sentences is a name.
    names::Registry r = Scan(snap, "フトンに鞄を直してもらった。\nフトンが帽子を編んでくれた。");
    const std::vector<std::string> names = r.Names();
    HD_CHECK(std::find(names.begin(), names.end(), "フトン") != names.end());
    // Not a place the favour is done at.
    names::Registry place = Scan(snap, "ウチに来てもらった。\n明日もウチに来てもらうよ。");
    const std::vector<std::string> none = place.Names();
    HD_CHECK(std::find(none.begin(), none.end(), "ウチ") == none.end());
    // Not one before another に-phrase of the clause.
    names::Registry other = Scan(snap, "フトンに行って母に作ってもらった。\nフトンに寄って父に直してもらった。");
    const std::vector<std::string> others = other.Names();
    HD_CHECK(std::find(others.begin(), others.end(), "フトン") == others.end());
}
