#include "core/pipeline/protect.h"

#include <memory>
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::pipeline;

namespace {

// Pieces written as text, with spans as 〖kind text〗.
std::string Show(const output::Pieces& pieces) {
    std::string s;
    for (const output::Piece& p : pieces) {
        if (p.span == output::SpanText) {
            s += p.text;
        } else {
            s += "〖" + std::to_string(static_cast<int>(p.span)) + p.text + "〗";
        }
    }
    return s;
}

output::Pieces Text(std::string s) { return {{std::move(s), output::SpanText}}; }

// A snapshot of one base file with the rows (key ko pos).
std::shared_ptr<const dict::Snapshot> Snapshot(const std::string& rows) {
    dict::File f;
    f.name = "base.tsv";
    f.text = "key\tko\tpos\tsem\tgram\tdict\tid\n" + rows;
    return dict::Load({f}).snapshot;
}

}  // namespace

HD_TEST(pipeline_cut_spans) {
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"김철수が来た", "〖1김철수〗が来た"},
        {"「안녕 하세요」", "「〖1안녕 하세요〗」"},
        {"ᄒᆞᆫ글", "〖1ᄒᆞᆫ글〗"},
        {"ㅋㅋ です", "〖1ㅋㅋ〗 です"},
        {"🍣を", "〖1🍣〗を"},
        {"ゔぁいゕゔ", "〖1ゔぁ〗い〖1ゕゔ〗"},
        {"<b>김</b>", "〖2<b>〗〖1김〗〖3</b>〗"},
        {"[color=red]x[/color]", "〖2[color=red]〗x〖3[/color]〗"},
        {"[b x]y[color=a\nb]", "[b x]y[color=a\nb]"},
        {"a&amp;b&#169;&#x1F363;&x;", "a〖2&amp;〗b〖2&#169;〗〖2&#x1F363;〗&x;"},
        {"&#12345678;&#X41;&a12345678901234567890123456789012;", "&#12345678;&#X41;&a12345678901234567890123456789012;"},
        // Inline style codes: a font, a colour and the reset.
        {"だよ%f源ノ角ゴシックB;#00ff92cb;♥%fbase;#;」", "だよ〖2%f源ノ角ゴシックB;〗〖2#00ff92cb;〗〖1♥〗〖2%fbase;〗〖2#;〗」"},
        {"C#;と#12354;と%f;x%fa%b;", "C〖2#;〗と#12354;と〖2%f;〗x%fa%b;"},
        // A tag wins over a BBCode tag and Hangul inside it.
        {"<a title=\"[b]김\">", "〖2<a title=\"[b]김\">〗"},
        // Not tags: no letter after <, < inside.
        {"1 < 2 <a <b>", "1 < 2 <a 〖2<b>〗"},
        // <BR> and <LI> stay sentence ends.
        {"a<BR>b<li>c<br/>", "a<BR>b<li>c〖2<br/>〗"},
        {"<!-- x --><?xml?>", "〖2<!-- x -->〗〖2<?xml?>〗"},
        // Hangul runs do not take leading or trailing spaces.
        {" 김 　치 ", " 〖1김 　치〗 "},
        // Stutter marks with their separators.
        {"\xEE\x80\x80、\xEE\x80\x80、あ", "〖2\xEE\x80\x80、\xEE\x80\x80、〗あ"},
        {"\xEE\x80\x80……あ\xEE\x80\x80・あ", "〖2\xEE\x80\x80……〗あ〖2\xEE\x80\x80〗・あ"},
        // Drawl spans from mark to the same mark; an unclosed mark is foreign.
        {"です\uE005……\uE005。", "です〖2\uE005……\uE005〗。"},
        {"です\uE006…\uE006\uE005…\uE005", "です〖2\uE006…\uE006〗〖2\uE005…\uE005〗"},
        {"です\uE007―\uE007", "です〖2\uE007―\uE007〗"},
        {"です\uE005…", "です〖1\uE005〗…"},
        {"a\uE005b\uE005", "a〖1\uE005〗b〖1\uE005〗"},
        {"a\uE005\uE005", "a〖1\uE005\uE005〗"},
        // Emphasis marks open and close a span each.
        {"\uE008さいてい\uE009！", "〖2\uE008〗さいてい〖3\uE009〗！"},
        // The lengthening mark closes a span.
        {"せんぱい\uE00A！", "せんぱい〖3\uE00A〗！"},
        // Foreign runs end at white space; ASCII, half-width forms and CP932 symbols are
        // not foreign; invalid bytes are.
        {"Ωé αβ☆ｱ ไทย", "Ω〖1é〗 αβ☆ｱ 〖1ไทย〗"},
        {"あ\xFFい", "あ〖1\xFF〗い"},
        // Masks and particle marks in or after a Hangul run belong to it; masks without
        // Hangul before them do not.
        {"자○が", "〖1자○〗が"},
        {"섹××\uE001を", "〖1섹××\uE001〗を"},
        {"오○니 자○", "〖1오○니 자○〗"},
        {"○○さん", "○○さん"},
        {"○김", "○〖1김〗"},
        // ◯ has no Korean form of its own and is kept as written; ○ is a dictionary word.
        {"◯◯さん", "〖1◯◯〗さん"},
        {"○○さん", "○○さん"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Show(internal::CutSpans(c.in, {})), std::string(c.want));

    const std::vector<std::string> markers = {"@@", "@@@", "#"};
    HD_CHECK_EQ(Show(internal::CutSpans("a@@@b#c@d", markers)), std::string("a〖2@@@〗b〖2#〗c@d"));
    // A marker never cuts into a tag.
    HD_CHECK_EQ(Show(internal::CutSpans("<a #>#", markers)), std::string("〖2<a #>〗〖2#〗"));
}

HD_TEST(pipeline_is_foreign) {
    HD_CHECK(internal::IsForeign(U'가'));
    HD_CHECK(internal::IsForeign(U'é'));
    HD_CHECK(!internal::IsForeign(U'μ'));
    HD_CHECK(!internal::IsForeign(U'—'));
    HD_CHECK(internal::IsForeign(U'ゔ'));
    HD_CHECK(internal::IsForeign(U'\U0001F363'));
    HD_CHECK(!internal::IsForeign(U'A'));
    HD_CHECK(!internal::IsForeign(U'ｱ'));
    HD_CHECK(!internal::IsForeign(U'ヺ'));
    HD_CHECK(!internal::IsForeign(U'漢'));
    HD_CHECK(!internal::IsForeign(U'Ω'));
    HD_CHECK(!internal::IsForeign(U'☆'));
}

HD_TEST(pipeline_rewrite_conjunctions) {
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"なら行こう", "それなら行こう"},
        {"ならない", "ならない"},  // before hiragana
        {"「ならば、行く」", "「それならば、行く」"},
        // The 。 after a match is taken by it, so it starts no second one.
        {"ですから。にもかかわらず彼は", "だから。にもかかわらず彼は"},
        {"それ。　…対して彼は", "それ。　…反面彼は"},
        {"彼なら行く", "彼なら行く"},  // not at a sentence start
        {"なら", "なら"},            // nothing after it
        {"ならば\n", "ならば\n"},  // なら before the hiragana ば
        {"であれば\nなら行く", "であれば\nそれなら行く"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Show(internal::RewriteConjunctions(Text(c.in))), std::string(c.want));

    // A span before a piece hides the start of the text; a span after it is a character.
    const output::Pieces spans = {{"<b>", output::SpanOpen}, {"なら行く。なら", output::SpanText}, {"김", output::SpanWord}};
    HD_CHECK_EQ(Show(internal::RewriteConjunctions(spans)), std::string("〖2<b>〗なら行く。それなら〖1김〗"));
}

HD_TEST(pipeline_lines_and_brackets) {
    const output::Pieces p = {{"a\n", output::SpanText}, {"김", output::SpanWord}, {"\nb\n", output::SpanText}};
    const auto lines = output::Lines(p);
    HD_REQUIRE(lines.size() == 4);
    HD_CHECK_EQ(Show(lines[0]), std::string("a"));
    HD_CHECK_EQ(Show(lines[1]), std::string("〖1김〗"));
    HD_CHECK_EQ(Show(lines[2]), std::string("b"));
    HD_CHECK(lines[3].empty());
    HD_CHECK_EQ(Show(output::JoinLines(lines)), Show(p));
    HD_CHECK_EQ(output::JoinLines(lines).size(), p.size());

    HD_CHECK_EQ(Show(MarkCornerBrackets({{"｢a｣", output::SpanText}, {"｢", output::SpanOpen}})),
                std::string("「a」〖2｢〗"));
}

HD_TEST(pipeline_kata_honorifics) {
    const auto snap = Snapshot("タン\t혀\tN110\t\t\tGN\t1\nたくさん\t많이\tN311\t\t\tGN\t2\n");
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"ヨウタサマ", "ヨウタさま"},
        {"「ヨウタサン、ドウゾ」", "「ヨウタさん、ドウゾ」"},
        {"タンサン", "タンサン"},
        {"タクサン", "タクサン"},
        {"オジサン", "オジサン"},
        {"サマ", "サマ"},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(Show(internal::KataHonorifics(Text(c.in), ctx, *snap)), std::string(c.want));
    }
}

HD_TEST(pipeline_nicknames_to_hangul) {
    const auto snap = Snapshot("おばちゃん\t아줌마\tN110\t\t\tGN\t1\nどんちゃん騒ぎ\t법석\tN110\t\t\tGN\t2\n");
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"ゆうくん、おはよう。", "〖1유〗くん、おはよう。"},
        {"おやすみ、ふみちゃん", "おやすみ、〖1후미〗ちゃん"},
        {"「みーちゃん！」", "「〖1미〗ちゃん！」"},
        {"私はゆうくんが好き。", "私は〖1유〗くんが好き。"},
        {"あにくんが来た。", "〖1아니〗くんが来た。"},
        {"群がるおばちゃん", "群がるおばちゃん"},
        {"ねえちゃん、待って", "ねえちゃん、待って"},
        {"ここにちゃんと書いた。", "ここにちゃんと書いた。"},
        {"皆がどんちゃん騒ぎだ。", "皆がどんちゃん騒ぎだ。"},
        {"内容にしとくんで。", "内容にしとくんで。"},
        {"なんでゆうくんまで", "なんでゆうくんまで"},  // a nickname inside a longer hiragana run
        {"ぼくんち", "ぼくんち"},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(Show(internal::NicknamesToHangul(Text(c.in), ctx, *snap)), std::string(c.want));
    }
}

HD_TEST(pipeline_kata_words) {
    const auto snap = Snapshot("からだ\t몸\tN110\t\t\tGN\t1\n");
    HD_REQUIRE(snap != nullptr);
    LineContext ctx;
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"アレは何だ？", "あれは何だ？"},
        {"これナニ？", "これなに？"},
        {"食べちゃダメ！", "食べちゃだめ！"},
        {"ダメ", "だめ"},
        {"アレさん、おはよう。", "アレさん、おはよう。"},  // not followed by a particle
        {"アレ・ハインツが来た。", "アレ・ハインツが来た。"},
        {"ハインツ・アレが来た。", "ハインツ・アレが来た。"},
        {"原発のアレだ。", "原発のアレだ。"},
        {"ひょっとしてアレ？", "ひょっとしてアレ？"},
        {"せまいからダメ？", "せまいからダメ？"},  // からだ joins the text around it
        {"アレックスは来た。", "アレックスは来た。"},
        {"スゴイね。", "すごいね。"},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(Show(internal::KataWordsToHiragana(Text(c.in), ctx, *snap)), std::string(c.want));
    }
    // After a span, and an adjective before a span.
    const output::Pieces spans = {{"<b>", output::SpanOpen}, {"アレは。ダメ", output::SpanText}, {"</b>", output::SpanClose}};
    HD_CHECK_EQ(Show(internal::KataWordsToHiragana(spans, ctx, *snap)), std::string("〖2<b>〗アレは。ダメ〖3</b>〗"));

    // A name of the document stays.
    const dict::KeySet named = {"アレ"};
    ctx.named = &named;
    HD_CHECK_EQ(Show(internal::KataWordsToHiragana(Text("アレは何だ？ダメだ"), ctx, *snap)), std::string("アレは何だ？だめだ"));
    ctx.named = nullptr;

    // So does a word with a plain sense.
    dict::CodeTable codes;
    dict::UserEntry e;
    e.key = "アレ";
    e.target = "알레";
    e.codes = codes.At(codes.Intern("N130", "QPN", "", "GN"));
    e.name = true;
    std::vector<dict::UserEntry> entries;
    entries.push_back(e);
    const dict::UserText plain(std::move(entries));
    ctx.lookup.plain = &plain;
    HD_CHECK_EQ(Show(internal::KataWordsToHiragana(Text("アレは何だ？"), ctx, *snap)), std::string("アレは何だ？"));
    HD_CHECK_EQ(internal::KeyTags("アレ", ctx, *snap), std::string("N"));
    HD_CHECK_EQ(internal::KeyTags("からだ", ctx, *snap), std::string("N"));
    HD_CHECK_EQ(internal::KeyTags("から", ctx, *snap), std::string());
}

HD_TEST(pipeline_kata_speech) {
    const auto snap = Snapshot("わたし\t나\tN110\t\t\tGN\t1\nねこ\t고양이\tN110\t\t\tGN\t2\nロボット\t로봇\tN110\t\t\tGN\t3\n");
    HD_REQUIRE(snap != nullptr);
    const LineContext ctx;
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"ワタシハ ネコデス", "わたしは ねこです"},
        {"ワタシハロボットデス", "わたしはロボットです"},  // a katakana word inside a run stays
        {"ワタシハ ネコデス\nエビフライ", "わたしは ねこです\nエビフライ"},  // line by line
        {"ネコ", "ネコ"},
        {"エビフライ", "エビフライ"},
        {"アハハハハ！", "アハハハハ！"},
        {"ネバネバ", "ネバネバ"},
        {"猫はネコデス、と言った。", "猫はネコデス、と言った。"},  // mostly not katakana
        // Particles and okurigana after a kanji word, and お before one.
        {"猫ハ、ワタシノ、ネコデス", "猫は、わたしの、ねこです"},
        {"音声ヲ、解析シマス。発信源ハ、内部", "音声を、解析します。発信源は、内部"},
        {"ワタシノ、猫ヲ、オ貸シ、シマス", "わたしの、猫を、お貸し、します"},
        {"音声ロボット、内部ロボット", "音声ロボット、内部ロボット"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Show(internal::KataSpeech(Text(c.in), ctx, *snap)), std::string(c.want));
    // Spans are passed over.
    const output::Pieces spans = {{"ワタシハ ", output::SpanText}, {"김", output::SpanWord}, {"ネコデス", output::SpanText}};
    HD_CHECK_EQ(Show(internal::KataSpeech(spans, ctx, *snap)), std::string("わたしは 〖1김〗ねこです"));
}

HD_TEST(pipeline_katakana_dictionary) {
    const auto loaded = Snapshot(
        "わたし\t나\tN110\t\t\tGN\t1\n"
        "にほんご\t일본어\tN110\t\t\tGN\t2\n"
        "わか\t알&AA@\tM428\t\t\tGN\t3\n"
        "ロボット\t로봇\tN110\t\t\tGN\t4\n"
        "おまえ\t너\tN110\t\t\tGN\t5\n"
        "ころ\t죽이&AA@\tM428\t\t\tGN\t6\n"
        "コロス\t코로스\tN130\tQPN\t\tGN\t7\n"
        "こんにちは\t안녕\tN110\t\t\tGN\t8\n"
        "アリス\t앨리스\tN130\tQPN\t\tGN\t9\n"
        "もう\t이미\tN110\t\t\tGN\t10\n"
        "とべ\t날&AA@\tM427\t\t\tGN\t11\n"
        "ここ\t여기\tN110\t\t\tGN\t12\n"
        "とお\t지나&AA@\tM428\t\t\tGN\t13\n"
        "うそ\t거짓말\tN110\t\t\tGN\t14\n"
        "まだ\t아직\tN110\t\t\tGN\t15\n"
        "なにも\t아무것도\tN110\t\t\tGN\t16\n"
        "てき\t적\tN110\t\t\tGN\t17\n"
        "エビフライ\t새우튀김\tN110\t\t\tGN\t18\n"
        "あれだけ\t그만큼\tN110\t\t\tGN\t19\n"
        "からだ\t몸\tN110\t\t\tGN\t20\n");
    HD_REQUIRE(loaded != nullptr);
    const dict::Snapshot& snap = *loaded;
    const LineContext ctx;
    const struct {
        const char* in;
        const char* want;
    } speech[] = {
        {"ワタシハ ニホンゴガ ワカリマセン", "わたしは にほんごが わかりません"},
        {"ワタシハロボットデス", "わたしはロボットです"},
        {"オマエヲ コロス", "おまえを ころす"},  // コロス is a katakana word, but reads as a verb
        {"コンニチハ、ワタシハ アリスデス", "こんにちは、わたしは アリスです"},
        {"モウ…トベナイノ……トベナイノヨ……ワカル？…ネェ", "もう…とべないの……とべないのよ……わかる？…ねぇ"},
        {"ココ…ハ…トオレナイシ……。……トオサナイ……ヨ……ッ！", "ここ…は…とおれないし……。……とおさない……よ……ッ！"},
        {"Adieu… ウソデショ……。ワタシ…マダ…ナニモ…… テキハ…いない、のね？",
         "Adieu… うそでしょ……。わたし…まだ…なにも…… てきは…いない、のね？"},
        {"ワタシハ ニホンゴガ ワカリマセン\nエビフライ", "わたしは にほんごが わかりません\nエビフライ"},
        {"ヒトナナマルマル。提督、夕方です。", "ヒトナナマルマル。提督、夕方です。"},
        {"ブレイダブリクへようこそ！最近お客さんが少なくってねえ。", "ブレイダブリクへようこそ！最近お客さんが少なくってねえ。"},
        {"俺もおせっかいなヤツはキライなんだ。", "俺もおせっかいなヤツはキライなんだ。"},
        {"エビフライ", "エビフライ"},
        {"カケル", "カケル"},
        {"デンジャラス！", "デンジャラス！"},
        {"スルー", "スルー"},
        {"チリンチリン", "チリンチリン"},
        {"ネバネバ", "ネバネバ"},
        {"アハハハハ！", "アハハハハ！"},
    };
    for (const auto& c : speech) HD_CHECK_EQ(Show(internal::KataSpeech(Text(c.in), ctx, snap)), std::string(c.want));
    const struct {
        const char* in;
        const char* want;
    } words[] = {
        {"アレは何だ？", "あれは何だ？"},
        {"ソレが問題なんだ。", "それが問題なんだ。"},
        {"まぁ、ココだけの話だが", "まぁ、ここだけの話だが"},
        {"アタシも行く。", "あたしも行く。"},
        {"ヤバイか！？", "やばいか！？"},
        {"カワイイだろ～？", "かわいいだろ～？"},
        {"アレちゃんが来た。", "アレちゃんが来た。"},
        {"アレに気をつけて。", "アレに気をつけて。"},
        {"アレだけは苦手。", "アレだけは苦手。"},  // あれだけ is another word
        {"せまいからダメ？", "せまいからダメ？"},
        {"ダメですよ。", "ダメですよ。"},  // the base word conjugates already
        {"ウルサイぞ。", "うるさいぞ。"},
        {"ヒドイよ！", "ひどいよ！"},
        {"ダメよ。", "ダメよ。"},  // only an i-adjective before a sentence-final particle
        {"オレは行く。", "オレは行く。"},
        {"ドレスを着た。", "ドレスを着た。"},
        {"出さないとコロスぞ。", "出さないと殺すぞ。"},  // kanji keeps the word apart from と
        {"アタシがコロスんだ。", "あたしが殺すんだ。"},
        {"ヤメテよ！", "やめてよ！"},
        {"コロスケが来た。", "コロスケが来た。"},
    };
    for (const auto& c : words) {
        HD_CHECK_EQ(Show(internal::KataWordsToHiragana(Text(c.in), ctx, snap)), std::string(c.want));
    }
    HD_CHECK_EQ(Show(Protect("｢アレは何だ？｣なら김치", ctx, snap)), std::string("「あれは何だ？」なら〖1김치〗"));
    HD_CHECK_EQ(Show(ProtectTyped("｢アレは何だ？｣", ctx, snap)), std::string("｢あれは何だ？｣"));
}
