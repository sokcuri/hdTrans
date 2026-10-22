#include "core/pipeline/sound.h"

#include <string>

#include "core/dict/builtin.h"
#include "core/dict/snapshot.h"
#include "core/dict/tsv.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::pipeline;

namespace {

const dict::LoadResult& SoundDictionary() {
    static const dict::LoadResult result = dict::Load({{
        0, "base.tsv",
        "key\tko\tpos\tid\n"
        "くすくす笑\t킥킥 웃\tM100\t1\n"
        "もっと\t더\tE300\t2\n"
        "まかせ\t맡기\tM100\t3\n"
        "やめ\t그만두\tM100\t4\n"
        "うるさ\t시끄럽\tH100\t5\n"
        "ちょっと\t조금\tE300\t6\n"
        "どうだ\t어때\tE300\t7\n"
        "い\t가\tM100\t8\n"
        "すご\t대단하\tH100\t9\n"
        "冷た\t차갑\tH100\t10\n"
        "とは\t(이)란\tP200\t11\n"
        "かは\t1는가는\tS561\t12\n"
        "では\t그럼\tP250\t13\n"
        "キー\t키\tN110\t14\n"
        "でも\t그렇지만\tE300\t15\n"
        "浅\t얕\tH100\t16\n"
        "おも\t무겁\tH100\t17\n"
        "おめぇ\t너\tN120\t18\n"
        "てめー\t너\tN120\t19\n"
        "てめぇ\t너\tN120\t20\n",
    }});
    return result;
}

struct Case {
    const char* in;
    const char* want;
};

}  // namespace

HD_TEST(pipeline_lengthen) {
    const struct {
        const char* korean;
        const char* units;
        const char* want;
    } cases[] = {
        {"하아", "vv", "하아아아"}, {"앙", "vvv", "아아아앙"}, {"꺅", "vv", "꺄아악"},
        {"앙", "~", "아~앙"},       {"하아", "!", "하앗"},     {"하아", "~", "하아~"},
        {"정말", "vv", "정말~"},    {"쾅", "v", "콰앙"},       {"하아", "vvvvvvv", "하아아아아아"},
        {"정말", "!", "정말"},      {"ok", "vv", "ok"},        {"", "v", ""},
        // Repeats write the last syllable again and let ! close it.
        {"아하하", "r", "아하하하"}, {"아하하", "rr!", "아하하하핫"}, {"후후", "r", "후후후"},
        {"아하하", "!", "아하하"},
        // A ～ that stood for the key's ー adds one ~ at the end.
        {"으음", "t", "으음~"}, {"으음", "~t", "으으~음"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::Lengthen(c.korean, c.units), std::string(c.want));
}

HD_TEST(pipeline_casual_danger_boundaries) {
    const struct {
        const char* tag;
        const char* in;
        const char* want;
    } cases[] = {
        {"H7N0", "坂は危ねぇ。", "坂は危ない。"},
        {"H7N0", "危ねえ坂！", "危ない坂！"},
        {"H7N0", "危ねぇ岩。", "危ない岩。"},
        {"H7N0", "危ねえぞ。", "危ないぞ。"},
        {"H7N0", "坂は危ない。", "坂は危ない。"},
        {"H7N0", "坂は危なくない。", "坂は危なくない。"},
        {"H7N0", "危なければ帰る。", "危なければ帰る。"},
        {"", "危ねぇ岩。", "危ねぇ岩。"},
        {"N110", "危ねぇ岩。", "危ねぇ岩。"},
        {"M100", "危ねぇ岩。", "危ねぇ岩。"},
        {"H7N0", "しねぇ。", "しねぇ。"},
        {"H7N0", "危ねええ岩。", "危ない岩。"},
        {"H7N0", "危ねぇに。", "危ねぇに。"},
        {"H7N0", "かてぇ岩。", "かてぇ岩。"},
        {"H7N0", "試合にかてえ！", "試合にかてえ！"},
        {"H7N0", "危ねぇ島。", "危ない島。"},
    };
    for (const auto& c : cases) {
        std::string tsv = "key\tko\tpos\tid\n"
                          "かた\t딱딱$AB@\tH7N0\t1\n"
                          "しな\t하지 $DH@\tH7N0\t2\n";
        if (c.tag[0] != '\0') tsv += "危な\t위험$AB@\t" + std::string(c.tag) + "\t3\n";
        const auto loaded = dict::Load({{0, "base.tsv", tsv}});
        // The N110 and M100 lines are listed for their marker family and still load.
        HD_REQUIRE(loaded.snapshot != nullptr);
        for (const dict::LoadError& e : loaded.errors) HD_REQUIRE(e.kind == dict::LoadErrorKind::MarkerClass);
        LineContext ctx;
        ctx.lookup.plain = &loaded.snapshot->TextPlain();
        HD_CHECK_EQ(internal::RewriteCasualAdjectives(c.in, ctx, *loaded.snapshot), std::string(c.want));
    }
}

HD_TEST(pipeline_sound_lexicon) {
    HD_CHECK(internal::IsSoundKey(U"ドキドキ"));
    HD_CHECK(!internal::IsSoundKey(U"ドキ"));
    HD_CHECK(!internal::IsSoundKey(U""));
    for (const internal::SoundRow& r : internal::SoundRows(U'ド')) HD_CHECK(!r.key.empty() && r.key[0] == U'ド');
    size_t prev = static_cast<size_t>(-1);
    for (const internal::SoundRow& r : internal::SoundRows(U'ア')) {
        HD_CHECK(r.key.size() <= prev);
        prev = r.key.size();
    }
    // Invalid bytes survive the character view.
    HD_CHECK_EQ(internal::EncodeText(internal::DecodeText("の\xff\xe3\x81")), std::string("の\xff\xe3\x81"));
}

// The sound stage of whole lines: sounds, elongation, stutter (U+E000 before each
// separator), grammar after a sound, words that are not sounds, the emphasis and casual
// adjective rewrites and the lead interjections.
HD_TEST(pipeline_sound) {
    const auto& loaded = SoundDictionary();
    HD_REQUIRE(loaded.errors.empty());
    const auto& snap = loaded.snapshot;
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    const Case cases[] = {
        {"あんっ、だめぇ", "앙、안 돼"},
        {"うわ…柔らかい", "우와…柔らかい"},
        {"フキフキ", "쓱쓱"},
        {"肉がジュージューと焼ける。", "肉が지글지글 焼ける。"},
        {"ししょー、剣を見て", "스승님、剣を見て"},
        {"司書が本を運ぶ", "司書が本を運ぶ"},
        {"ししょかん", "ししょかん"},
        {"ああああん", "아아아앙"},
        {"はぁぁぁ…", "하아아아…"},
        {"いやぁぁぁ！", "싫어어어！"},
        {"うわぁー！", "우와아아！"},
        {"きゃああっ！", "꺄아악！"},
        {"やだ～～！", "싫어~！"},
        {"いくぅぅ", "가 버려~"},
        {"な、なにを", "\xEE\x80\x80" "、なにを"},
        {"だっ、だめ", "\xEE\x80\x80" "、だめ"},
        {"お…お兄ちゃん", "\xEE\x80\x80" "…お兄ちゃん"},
        {"だっ……大丈夫", "\xEE\x80\x80" "……大丈夫"},
        {"えっ……本当", "えっ……本当"},
        {"「わ、わ、私は……」", "「\xEE\x80\x80" "、\xEE\x80\x80" "、私は……」"},
        // A syllable said twice is a stutter even where it alone is a word (さ、 well).
        {"「さ、さ、最低」", "「\xEE\x80\x80" "、\xEE\x80\x80" "、最低」"},
        {"「さ、最低」", "「자、最低」"},
        // A dash run separates; a small vowel stutters its plain vowel; before a dash a lone
        // vowel stands alone.
        {"「ぃ―――いやぁッ」", "「\xEE\x80\x80" "―――싫엇」"},
        {"「あ――本当に」", "「あ――本当に」"},
        {"「う――痛い」", "「う――痛い」"},
        // Longer pieces that each begin the next: a kana mark for each kana of the piece and
        // a rest mark for each kana of the word it does not reach.
        {"「ご、ごめ、ごめんなさい」",
         "「\xEE\x80\x80" "、\xEE\x80\x80\xEE\x80\x80\xEE\x80\x84\xEE\x80\x84\xEE\x80\x84\xEE\x80\x84" "、ごめんなさい」"},
        // で、 opening an utterance stutters; a dictionary word, a demonstrative, an answer or
        // a sound is a word of its own; a
        // pause after a word keeps the word; no stutter before a numeral.
        {"「で、でも、本当」", "「\xEE\x80\x80" "、でも、本当」"},
        {"「それで、でも、本当」", "「それで、でも、本当」"},
        {"「さ、さ、三分」", "「자、さ、三分」"},
        {"「あ、あれ、何」", "「\xEE\x80\x80" "、あれ、何」"},
        {"「は、はい、行く」", "「\xEE\x80\x80" "、はい、行く」"},
        {"「そ、そんな……嘘」", "「\xEE\x80\x80" "、そんな……嘘」"},
        // A word said again is no piece of it.
        {"「お、おのれ、おのれぇ」", "「\xEE\x80\x80" "、おのれ、おのれぇ」"},
        {"ドキドキしてきた", "두근두근してきた"},
        {"ぼーっとしてない？", "멍してない？"},
        {"くすくす笑う", "くすくす笑う"},
        {"シーンを撮る", "シーンを撮る"},
        {"パンを食べた", "パンを食べた"},
        {"あんな人", "あんな人"},
        {"まっかせてよ！", "まかせてよ！"},
        {"やーめーてよ！", "やめてよ！"},
        {"見ぃつけたぁ！", "見つけたぁ！"},
        {"うるさ～い！", "うるさい\uE00A！"},
        {"ちょーっと待って", "ちょっと待って"},
        {"どーだ！", "どうだ！"},
        {"いこー！", "いこう！"},
        {"なんだそりゃああぁあッ？", "なんだそりゃッ？"},
        // A lengthening that ends a phrase after a word is written ～; a drawn small vowel
        // after a て form is dropped; the lengthening of the last syllable of a phrase inside
        // a word is marked after it.
        {"まかせてー！", "まかせて～！"},
        {"まかせてぇ！", "まかせて！"},
        {"もっとー！", "もっと～！"},
        {"冷たいねー", "冷たいね～"},
        {"やーめーて！", "やめて\uE00A！"},
        // Unchanged: the casual negative ね, a lone kana, a sound.
        {"知らねー", "知らねー"},
        {"よー！", "よー！"},
        {"もっと早く", "もっと早く"},
        {"鍵が……おーいだれか", "鍵が……おーいだれか"},
        {"うーちゃん！", "うーちゃん！"},
        {"休もうっと。", "休もうっと。"},
        {"うるせぇな", "うるさいな"},
        {"すげーな", "すごいな"},
        {"冷て～！", "冷たい！"},
        {"歌おうぜぇ～。", "歌おうぜぇ～。"},
        {"届いてぇ！", "届いてぇ！"},
        // A laugh repeats its last syllable past its row (はははは, うふふふ), the closing ッ
        // giving it ㅅ; past a row whose syllable holds ッ (あっはっは, くっくっ) it is written
        // as heard, each syllable with its ㅅ. A unit of two syllables does not repeat.
        {"あっはっはっはっ", "앗핫핫핫"},
        {"アッハッハッハッ！", "앗핫핫핫！"},
        {"あっはっはっは", "앗핫핫하"},
        {"はははははっ", "하하하하핫"},
        {"くっくっくっくっ", "쿳쿳쿳쿳"},
        {"くっくっくっ", "큭큭큭"},
        {"うふふふふっ", "우후후후훗"},
        {"うふふふふっ～", "우후후후훗"},
        {"えへへへへっ", "에헤헤헤헷"},
        {"あっはっはっ", "아하하"},
        {"うふふっ", "우후훗"},
        {"ぐおおおおっ", "크오오오옷"},
        // An ABAB mimetic repeated over more than its two units is read as one run and
        // written half its Korean for each unit, with no space; a single AB unit is not.
        // A ッ that closes the run goes with it.
        {"ドキドキ", "두근두근"},
        {"ドキドキドキ", "두근두근두근"},
        {"ドキドキドキドキ", "두근두근두근두근"},
        {"ドキドキドキドキッ", "두근두근두근두근"},
        {"うずうずうず", "근질근질근질"},
        {"うずうずうずうず", "근질근질근질근질"},
        {"うずうずうずうずうず", "근질근질근질근질근질"},
        {"わくわくわく", "두근두근두근"},
        {"あんあんあん", "앙앙앙"},
        {"あんあんあんあん", "앙앙앙앙"},
        {"はあはあはあ", "하아하아하아"},
        {"はあはあはあはあ", "하아하아하아하아"},
        {"ぶーぶーぶー", "꿀꿀꿀"},
        {"がくがくがくがくっ", "후들후들후들후들"},
        {"うずうずうずらを見る。", "근질근질근질らを見る。"},
        // The first sound of a run reads on through the ッ that the next sound starts
        // from; a sound alone still ends before the ッ of the word that follows.
        {"はぎいっくしゅん", "하기잇 에취"},
        {"はぎいっ", "하기잇"},
        {"はっきりして", "はっきりして"},
        {"ぶっ飛ぶ", "ぶっ飛ぶ"},
        // A burst of breath said as one syllable is not a run of sounds.
        {"ぎゃっはっ", "걋핫"},
        {"げっほっ", "겟홋"},
        {"くっはっ", "쿳핫"},
        // Bursts of breath stand alone; in a word they stay.
        {"かはっ……", "카핫……"},
        {"「かッハ……！」", "「캇하……！」"},
        {"がはっ！", "가핫！"},
        {"「だっは！」", "「닷하！」"},
        {"げはッ……", "게핫……"},
        {"どうかはっきりして", "どうかはっきりして"},
        {"「とはっ！」", "「とはっ！」"},
        {"「ではっ……」", "「ではっ……」"},
        {"「かはっと」", "「かはっと」"},
        {"「カハッ！」", "「カハッ！」"},
        // A lone ん or small vowel between punctuation is a voice of its own.
        {"「あっ、ん、くぁっ」", "「앗、응、크앗」"},
        {"……ぅ……", "……우……"},
        {"あっ、く、うぅ", "앗、크、우우"},
        {"な ん", "な ん"},
        {"君ん家", "君ん家"},
        {"「おい、誰か！！」", "「어이、誰か！！」"},
        {"「お、おい、魅…、」", "「\xEE\x80\x80" "、어이、魅…、」"},
        {"おいおいおい", "이봐 이봐 이봐"},
        {"「おい田中」", "「어이 田中」"},
        {"「おいこら！」", "「야 이놈아！」"},
        {"ここにおいて", "ここにおいて"},
        {"ほら、見て！", "자、見て！"},
        {"ちょっと高い", "ちょっと高い"},
        {"これでよし。", "これでよし。"},
        {"<b>きゃあ</b>ぎゅっと", "<b>꺄아</b>꽉"},
        // Invalid UTF-8 bytes stay in place.
        {"の\xff<b>", "の\xff<b>"},
        {"\xffドキドキ\xe3", "\xff두근두근\xe3"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Sound(c.in, ctx, *snap), std::string(c.want));
}

// A verb cut off before a dash or an ellipsis, completed when the dictionary has it.
// Masked words: rows of masked.tsv with the mask as written and a particle mark when
// the Korean ends in the mask, katakana transliterated around a mask, and masks that
// stand for no word.
HD_TEST(pipeline_write_masked) {
    const auto& loaded = SoundDictionary();
    HD_REQUIRE(loaded.errors.empty());
    const auto& snap = loaded.snapshot;
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    const Case cases[] = {
        {"オ○ニーしてた。", "자○\uE001してた。"},
        {"ち○ちん", "자○\uE001"},
        {"ち●こが", "자●\uE001が"},
        {"お○んこ", "보○\uE001"},
        {"セッ○スしたい", "섹○\uE001したい"},
        {"セッ××スしたい", "섹××\uE001したい"},
        {"お○ぱいを", "가○\uE002を"},
        {"ア＊ルに", "애＊\uE003に"},
        {"ワ○ピース", "와○피스"},
        {"オ○ニ", "오○니"},
        // A row with nothing before its mask needs no kana there; with kana there the word
        // is transliterated.
        {"○ナニー", "자○\uE001"},
        {"ア○ナニー", "아○나니"},
        // Not masked words: no kana on both sides, × between names, a syllable that a
        // mask cuts (ポケ○ン), hiragana that no row holds.
        {"○×クイズ", "○×クイズ"},
        {"○月○日", "○月○日"},
        {"○○さん", "○○さん"},
        {"◯◯さんが来た", "◯◯さんが来た"},
        {"あの○○が", "あの○○が"},
        {"これに○をつけて", "これに○をつけて"},
        {"・りんご", "・りんご"},
        {"ナルト×サスケ", "ナルト×サスケ"},
        {"ポケ○ントレーナー", "ポケ○ントレーナー"},
        {"ヨド○シカメラ", "ヨド○シカメラ"},
        {"キー○ボタン", "キー○ボタン"},
        {"ガン○ム", "간○무"},
        {"ふ○ふ", "ふ○ふ"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::WriteMasked(c.in, ctx, *snap), std::string(c.want));

    // Every row of masked.tsv has a mask in its Japanese and Korean and a word.
    const std::string table = dict::BuiltinText("masked.tsv");
    dict::TsvReader reader(table, "//");
    int rows = 0;
    while (reader.Next()) {
        const auto& f = reader.Fields();
        if (f.empty() || f[0].empty()) continue;
        rows++;
        HD_CHECK(f.size() == 3 && f[0].find("○") != std::string_view::npos && f[1].find("○") != std::string_view::npos &&
                 !f[2].empty());
    }
    HD_CHECK(rows > 0);

    // A dictionary key that covers the masked word wins.
    const auto user = dict::Load({{0, "base.tsv", "key\tko\tpos\tid\n"}, {1, "user.tsv", "オ○ニー\t자위\n"}});
    HD_REQUIRE(user.errors.empty() && user.snapshot != nullptr);
    LineContext userCtx;
    userCtx.lookup.plain = &user.snapshot->TextPlain();
    HD_CHECK_EQ(internal::WriteMasked("オ○ニー", userCtx, *user.snapshot), std::string("オ○ニー"));
    HD_CHECK_EQ(internal::WriteMasked("オ●ニー", userCtx, *user.snapshot), std::string("자●\uE001"));
}

HD_TEST(pipeline_complete_cut_offs) {
    const auto loaded = dict::Load({{
        0, "base.tsv",
        "key\tko\tpos\tid\n"
        "待\t기다&AA@\tM434\t1\n"
        "書\t쓰&AA@\tM421\t2\n"
        "やめ\t그만두&AA@\tM437\t3\n"
        "嘘\t거짓말\tN110\t4\n",
    }});
    HD_REQUIRE(loaded.errors.empty() && loaded.snapshot != nullptr);
    LineContext ctx;
    const Case cases[] = {
        {"待っ――", "待って――"},
        {"ちょっと待っ……", "ちょっと待って……"},
        {"「待っ——」", "「待って——」"},
        {"書い・・・", "書いて・・・"},
        {"書かな……", "書かない……"},
        {"書かなきゃ……", "書かなきゃ……"},
        {"待っ。", "待っ。"},
        {"待って――", "待って――"},
        {"待つ……", "待つ……"},
        {"嘘っ……", "嘘っ……"},
        {"やめ――", "やめ――"},
        {"やめな……", "やめな……"},
        {"書い…待っ――", "書いて…待って――"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::CompleteCutOffs(c.in, ctx, *loaded.snapshot), std::string(c.want));
}

HD_TEST(pipeline_sound_markers) {
    const auto& loaded = SoundDictionary();
    HD_REQUIRE(loaded.errors.empty());
    const auto& snap = loaded.snapshot;
    HD_REQUIRE(snap != nullptr);
    const std::string markers[] = {"ドキ"};
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    ctx.markers = markers;
    HD_CHECK_EQ(internal::WriteSounds("ドキドキ", ctx, *snap), std::string("ドキドキ"));
}

HD_TEST(pipeline_rewrite_emphasis) {
    const auto& loaded = SoundDictionary();
    HD_REQUIRE(loaded.errors.empty());
    const auto& snap = loaded.snapshot;
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    const Case cases[] = {
        {"まっかせてよ！", "まかせてよ！"},
        {"やーめーてよ！", "やめてよ！"},
        {"見ぃつけたぁ！", "見つけたぁ！"},
        {"うるさ～い！", "うるさい\uE00A！"},
        {"ちょーっと待って", "ちょっと待って"},
        {"どーだ！", "どうだ！"},
        {"いこー！", "いこう！"},
        {"なんだそりゃああぁあッ？", "なんだそりゃッ？"},
        // A lengthening that ends a phrase after a word is written ～; a drawn small vowel
        // after a て form is dropped; the lengthening of the last syllable of a phrase inside
        // a word is marked after it.
        {"まかせてー！", "まかせて～！"},
        {"まかせてぇ！", "まかせて！"},
        {"もっとー！", "もっと～！"},
        {"冷たいねー", "冷たいね～"},
        {"やーめーて！", "やめて\uE00A！"},
        // Unchanged: the casual negative ね, a lone kana, a sound.
        {"知らねー", "知らねー"},
        {"よー！", "よー！"},
        // Unchanged: a word holds the mark, a sound, a nickname, an ending.
        {"もっと早く", "もっと早く"},
        {"鍵が……おーいだれか", "鍵が……おーいだれか"},
        {"うーちゃん！", "うーちゃん！"},
        {"休もうっと。", "休もうっと。"},
        {"なんとかなるっしょ。", "なんとかなるっしょ。"},
        {"日曜日じゃなかったっけ", "日曜日じゃなかったっけ"},
        // A word spelled with ー keeps it before kana; before a mark, where the tokenizer would
        // split the ー off, it takes the small vowel of a spelling the dictionary has.
        {"てめーが黙ってろ", "てめーが黙ってろ"},
        {"てめー！", "てめぇ！"},
        {"だぞてめー、", "だぞてめぇ、"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::RewriteEmphasis(c.in, ctx, *snap), std::string(c.want));
}

HD_TEST(pipeline_join_spelled_out) {
    const auto loaded = dict::Load({{
        0, "base.tsv",
        "key\tko\tpos\tid\n"
        "さいてい\t최저\tN110\t1\n"
        "ダメ\t안 돼\tN110\t2\n"
        "ば\t1면\tS590\t3\n"
        "大好き\t너무 좋아\tN110\t4\n"
        "あげ\t주\tM100\t5\n"
        "ちょっと\t조금\tE300\t6\n"
        "うそ\t거짓말\tN110\t7\n"
        "はる\t봄\tN110\t8\n"
        "あい\t사랑\tN110\t9\n",
    }});
    HD_REQUIRE(loaded.errors.empty() && loaded.snapshot != nullptr);
    LineContext ctx;
    ctx.lookup.plain = &loaded.snapshot->TextPlain();
    const Case cases[] = {
        {"あなたなんて、さ・い・て・い！", "あなたなんて、\uE008さいてい\uE009！"},
        {"大・好・き", "\uE008大好き\uE009"},
        {"ダ・メ・よ？", "\uE008ダメよ\uE009？"},
        {"あ・げ・る・ねー♪", "\uE008あげるねー\uE009♪"},
        {"ちょ・っ・と", "\uE008ちょっと\uE009"},
        {"さ・い・て・いです", "\uE008さいてい\uE009です"},
        {"う・そ。", "\uE008うそ\uE009。"},
        // Kana that are no word, a list in the order of the kana table or the iroha and the
        // choices after a label are read kana by kana.
        {"そ・ふ・ゆ", "\uE008소후유\uE009"},
        {"は・ひ・ふ・へ・ほ", "\uE008하히후헤호\uE009"},
        {"い・ろ・は・に", "\uE008이로하니\uE009"},
        {"①あ・い", "①\uE008아이\uE009"},
        {"問１　あ・い", "問１　\uE008아이\uE009"},
        {"(1) あ・い", "(1) \uE008아이\uE009"},
        {"あ・イ・う", "\uE008아이우\uE009"},
        {"き・ゃ・あ", "\uE008きゃあ\uE009"},
        {"ド・きゅ・う！", "\uE008도큐우\uE009！"},
        // A word before punctuation, after a bracket that is no label or in katakana and
        // hiragana.
        {"は・る！", "\uE008はる\uE009！"},
        {"(笑) さ・い・て・い！", "(笑) \uE008さいてい\uE009！"},
        {"答え：さ・い・て・い", "答え：\uE008さいてい\uE009"},
        {"さ・イ・テ・い", "\uE008さいてい\uE009"},
        {"さ・ー・い・て・い", "사・ー・이・테・이"},
        // Two pieces at the end of a line or two particles: kana by kana; a lone ー or small kana
        // piece: piece by piece, every piece kept.
        {"う・そ", "\uE008우소\uE009"},
        {"て・を！", "\uE008테오\uE009！"},
        {"あ・ー", "아・ー"},
        {"あ・ゃ", "아・야"},
        {"）あ・い", "）\uE008아이\uE009"},
        // Unchanged: names, lists, kanji read one by one, katakana that is no word, a piece
        // of more than one kana, a run next to other characters, a lone piece.
        {"ジョン・スミス", "ジョン・スミス"},
        {"赤・青・黄の三色", "赤・青・黄の三色"},
        {"謹・賀・新・年", "謹・賀・新・年"},
        {"ケ・ロ", "ケ・ロ"},
        {"スー・チー", "スー・チー"},
        {"愛・たそがれ", "愛・たそがれ"},
        {"元・帝国軍", "元・帝国軍"},
        {"君が・ば・か", "君が・ば・か"},
        {"さ・いてい", "さ・いてい"},
        {"ば・", "ば・"},
        {"っ・ば", "っ・ば"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::JoinSpelledOut(c.in, ctx, *loaded.snapshot), std::string(c.want));
}

HD_TEST(pipeline_rewrite_casual_adjectives) {
    const auto& loaded = SoundDictionary();
    HD_REQUIRE(loaded.errors.empty());
    const auto& snap = loaded.snapshot;
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    const Case cases[] = {
        {"うるせぇな", "うるさいな"},
        {"すげーな", "すごいな"},
        {"冷て～！", "冷たい！"},
        {"歌おうぜぇ～。", "歌おうぜぇ～。"},  // the particle ぜ
        {"届いてぇ！", "届いてぇ！"},          // a て form
        {"「キャリア浅っ」", "「キャリア浅い」"},
        {"最浅っ！", "最浅っ！"},  // inside a word
        {"もっと浅っす", "もっと浅っす"},
        {"おめぇ、何してんだ", "おめぇ、何してんだ"},  // a word spelled with the small vowel
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::RewriteCasualAdjectives(c.in, ctx, *snap), std::string(c.want));
}

// そっか after the kanji stem of a verb in す is its volitional; after a noun the answer.
HD_TEST(pipeline_volitional_so) {
    const auto loaded = dict::Load({{0, "base.tsv",
                                     "key\tko\tpos\tid\n"
                                     "見逃\t놓&AO@\tM433\t1\n"
                                     "流\t흘&AA@\tM443\t2\n"
                                     "漢字\t한자\tN110\t3\n"
                                     "了解\t이해\tN110\t4\n"
                                     "解\t풀&AO@\tM443\t5\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
    const Case cases[] = {
        {"「見逃そっか」", "「見逃そうか」"},
        {"汗を流そっか？", "汗を流そうか？"},
        {"今日は見逃そっかな", "今日は見逃そうかな"},
        {"漢字そっか", "漢字そっか"},
        {"了解そっか", "了解そっか"},  // the noun 了解, though 解 alone is a verb stem
        {"あ、そっか", "あ、そっか"},
        {"見逃そっかす", "見逃そっかす"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::RewriteVolitionalSo(c.in, *loaded.snapshot), std::string(c.want));
}

HD_TEST(pipeline_sound_extra_isolation) {
    HD_CHECK(!internal::IsSoundKey(U"ガボッ"));
    HD_CHECK(internal::IsSoundKey(U"ガボッ", true));
    HD_CHECK(!internal::IsSoundKey(U"ガボッ"));
}

// An interjection before a pause or an ellipsis is read as a sound although an end row
// has the same key; before other text the end row's key still rules it out.
HD_TEST(pipeline_sound_before_pause_over_end_row) {
    const auto& loaded = SoundDictionary();
    HD_REQUIRE(loaded.errors.empty());
    const auto& snap = loaded.snapshot;
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    const Case cases[] = {
        {"見て！ぽい～！", "見て！포이~！"},
        {"…ぽい。", "…포이。"},
        {"ぽい…", "포이…"},
        {"ぽいぽい。", "포이포이。"},
        {"ぽい捨て", "ぽい捨て"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Sound(c.in, ctx, *snap), std::string(c.want));
}
