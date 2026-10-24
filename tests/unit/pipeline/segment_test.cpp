#include "core/pipeline/segment.h"

#include <string>
#include <vector>

#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::pipeline;

namespace {

// The chunks joined by |, spans written as 〖kind text〗.
std::string Show(const std::vector<Chunk>& chunks) {
    std::string s;
    for (size_t i = 0; i < chunks.size(); i++) {
        if (i > 0) s += "|";
        for (const output::Piece& p : chunks[i]) {
            s += p.span == output::SpanText ? p.text : "〖" + std::to_string(static_cast<int>(p.span)) + p.text + "〗";
        }
    }
    return s;
}

std::string Cut(const char* text) { return Show(Segment({{text, output::SpanText}})); }

}  // namespace

HD_TEST(pipeline_segment_spaces) {
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"第一章　完", "第一章|完"},
        {"えっ！？　本当に？", "えっ！？|本当に？"},
        {"第一章１　　『ギザ十』", "第一章１|『ギザ十』"},
        {"　段落の始まり。", "　段落の始まり。"},
        {"こ　の　展　開", "こ　の　展　開"},
        {"猫　犬", "猫　犬"},
        {"第一章 完", "第一章 完"},
        {"終わり　\n次", "終わり　\n次"},
        // An ASCII space after a final particle or between hiragana and a kanji word.
        {"そうだね そうじゃない", "そうだね|そうじゃない"},
        {"ひろい集め 彼は", "ひろい集め|彼は"},
        {"ほら 上も", "ほら|上も"},
        {"おなかが すきました", "おなかが すきました"},
        {"美しい 花", "美しい 花"},
        {"大見 健", "大見 健"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Cut(c.in), std::string(c.want));
}

HD_TEST(pipeline_segment_clauses) {
    // At least 40 Japanese characters before the 、 and 10 characters after it before
    // the sentence ends.
    HD_CHECK_EQ(Cut("覚えかけの必殺技の修得率を知りたい場合は、メニュー画面からステータス表示画面へ飛んで、"
                    "修得技を見れば一発でわかるぞ。"),
                std::string("覚えかけの必殺技の修得率を知りたい場合は、メニュー画面からステータス表示画面へ飛んで、|"
                            "修得技を見れば一発でわかるぞ。"));
    HD_CHECK_EQ(Segment({{"兄ちゃんが兵隊に行ってから、新しくウェイトレスが入ったんだけど……ぼく、兄ちゃんが帰ったら、"
                          "またすぐにレストランで働けるように頑張ってるんだ。",
                          output::SpanText}})
                    .size(),
                size_t{2});
    HD_CHECK_EQ(Segment({{"男に生まれて、世界中を旅してまわって、いろんなことをして生きてみたかったなあ。", output::SpanText}})
                    .size(),
                size_t{1});
    // Too little after the 、 before the sentence ends.
    HD_CHECK_EQ(Cut("あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわて、短い。次の文"),
                std::string("あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわて、短い。次の文"));
    // A sentence end restarts the count; spans do not count before the 、 but count
    // after it.
    const std::string a30 = "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほ";
    const std::string c38 = a30 + "まみむめもやゆよ";
    const std::string rest = "て、それから次へと進むのだ";
    HD_CHECK_EQ(Cut((c38 + "ま" + rest).c_str()), c38 + "まて、|それから次へと進むのだ");
    HD_CHECK_EQ(Cut((c38 + rest).c_str()), c38 + rest);
    HD_CHECK_EQ(Cut((a30 + "。" + a30 + rest).c_str()), a30 + "。" + a30 + rest);
    const output::Pieces before = {{"<b>", output::SpanOpen}, {c38 + rest, output::SpanText}};
    HD_CHECK_EQ(Segment(before).size(), size_t{1});
    const output::Pieces after = {{c38 + "まて、それ", output::SpanText},
                                  {"김", output::SpanWord},
                                  {"김", output::SpanWord},
                                  {"から次へと進む", output::SpanText}};
    HD_CHECK_EQ(Show(Segment(after)), c38 + "まて、|それ〖1김〗〖1김〗から次へと進む");
    HD_CHECK_EQ(Cut((c38 + "まて、それから次へと進む").c_str()), c38 + "まて、それから次へと進む");
    // The white space after a cut goes; a chunk of white space only goes whole.
    HD_CHECK_EQ(Show(Segment({{"第一章　", output::SpanText}, {"김", output::SpanWord}})), std::string("第一章　〖1김〗"));
    HD_CHECK_EQ(Show(Segment({{"第一章　 \t完", output::SpanText}})), std::string("第一章|完"));
    HD_CHECK(Segment({}).size() == 1 && Segment({})[0].empty());
}

HD_TEST(pipeline_segment_rules) {
    const struct {
        const char* before;
        bool want;
    } conjunctive[] = {
        {"飛んで", true}, {"学校で", false}, {"言って", true}, {"帰ったら", true}, {"知らないけど", true},
        {"思ったが", true}, {"ですが", true}, {"彼女が", false}, {"いいし", true}, {"わたし", false},
        {"はい", false}, {"行くので", true}, {"しながら", true}, {"で", false}, {"", false},
    };
    for (const auto& c : conjunctive) HD_CHECK_EQ(internal::Conjunctive(c.before), c.want);

    HD_CHECK(internal::KanaSpace("すきだよ すきだよ", std::string("すきだよ").size()));
    HD_CHECK(!internal::KanaSpace("かがやく おでこ", std::string("かがやく").size()));
    HD_CHECK(!internal::KanaSpace("基本操作 について", std::string("基本操作").size()));
    HD_CHECK(!internal::KanaSpace("だよ  ", std::string("だよ").size()));

    HD_CHECK(internal::BetweenWords("第一章　完", std::string("第一章").size()));
    HD_CHECK(!internal::BetweenWords("猫　犬", std::string("猫").size()));
    HD_CHECK(!internal::BetweenWords("こ　の　展", std::string("こ").size()));
    HD_CHECK(!internal::BetweenWords("　段落", 0));
    HD_CHECK(!internal::BetweenWords("終わり　\n次", std::string("終わり").size()));
}

HD_TEST(pipeline_conjunctive_conditional_endings) {
    const struct { const char* text; bool endsClause; } cases[] = {
        {"泳いで", true}, {"読んで", true}, {"学校で", false},
        {"いいし", true}, {"走るし", true}, {"静かだし", true},
        {"話すし", true}, {"思うし", true}, {"行くし", true}, {"わたし", false},
        {"思ったが", true}, {"よいが", true}, {"あるが", true}, {"思うが", true},
        {"話すが", true}, {"静かだが", true}, {"読まんが", true}, {"彼女が", false},
        {"で", false}, {"し", false}, {"が", false}, {"", false},
        {"行って", true}, {"言えば", true}, {"帰ったら", true}, {"呼んだら", true},
        {"行っても", true}, {"読んでも", true}, {"けど", true}, {"けれど", true},
        {"けれども", true}, {"から", true}, {"ので", true}, {"のに", true}, {"ながら", true},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::Conjunctive(c.text), c.endsClause);
}

HD_TEST(pipeline_segment_column_gaps) {
    // The chunks joined by | with the separator before each in [ ].
    const auto cut = [](const output::Pieces& pieces) {
        std::vector<std::string> gaps;
        const std::vector<Chunk> chunks = Segment(pieces, &gaps);
        HD_CHECK_EQ(gaps.size(), chunks.size());
        if (gaps.size() != chunks.size()) return std::string();
        std::string s;
        for (size_t i = 0; i < chunks.size(); i++) {
            if (i > 0) s += "|";
            if (!gaps[i].empty()) s += "[" + gaps[i] + "]";
            s += Show({chunks[i]});
        }
        return s;
    };
    const auto text = [&](const char* t) { return cut({{t, output::SpanText}}); };
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"17      医者      浮遊中", "17|[      ]医者|[      ]浮遊中"},
        {"１７　　　医者　　　浮遊中", "１７|[　　　]医者|[　　　]浮遊中"},
        {"名前\t職業", "名前|[\t]職業"},
        {"山田綾　　 　太郎の母", "山田綾|[　　 　]太郎の母"},
        {"wiki      の記事", "wiki|[      ]の記事"},
        {"第一章１　　『始まり』", "第一章１|[　　]『始まり』"},
        // The leading and trailing white space of a line, a single space and the padding of
        // a two-character name are no separators.
        {"　　製薬　　　：Lv.8", "　　製薬|[　　　]：Lv.8"},
        {"終わり　　\n次", "終わり　　\n次"},
        {"高橋　聖", "高橋|聖"},
        {"山田 太郎", "山田 太郎"},
        {"林  涛", "林  涛"},
        {"林  涛  歌手", "林  涛|[  ]歌手"},
        {"林業  涛", "林業|[  ]涛"},
        // Two ASCII spaces are a separator inside a clause too, and so are two ideographic
        // spaces.
        {"君と  僕と", "君と|[  ]僕と"},
        {"私は  学生です  。", "私は|[  ]学生です|[  ]。"},
        {"学生  です", "学生|[  ]です"},
        {"彼  は来た", "彼|[  ]は来た"},
        {"1話  長めのプロローグ", "1話|[  ]長めのプロローグ"},
        {"友人  : 「いつき！」", "友人|[  ]: 「いつき！」"},
        {"名前  はじまり", "名前|[  ]はじまり"},
        {"猫が   好き", "猫が|[   ]好き"},
        // An escape typed as two characters, with the blanks around it, and a run of them.
        {"「むー\\n食べてる」", "「むー|[\\n]食べてる」"},
        {"好き \\n だよ", "好き|[ \\n ]だよ"},
        {"あ\\r\\nい", "あ|[\\r\\n]い"},
        {"名前\\t職業", "名前|[\\t]職業"},
        // Not at the edges of a line, in Latin text or before a Latin word.
        {"\\nこんにちは", "\\nこんにちは"},
        {"こんにちは\\n", "こんにちは\\n"},
        {"C:\\temp\\nに保存", "C:\\temp\\nに保存"},
        {"本当\\\\nだよ", "本当\\\\nだよ"},
        {"式\\notagを", "式\\notagを"},
        {"猫が　　好き", "猫が|[　　]好き"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(text(c.in), std::string(c.want));
    // A separator after a span, before one, and after a 、 that ends a chunk.
    HD_CHECK_EQ(cut({{"<b>", output::SpanOpen}, {"  名前", output::SpanText}}), std::string("〖2<b>〗|[  ]名前"));
    HD_CHECK_EQ(cut({{"名前  ", output::SpanText}, {"<b>", output::SpanOpen}}), std::string("名前|[  ]〖2<b>〗"));
    HD_CHECK_EQ(cut({{"名前  ", output::SpanText}}), std::string("名前  "));

    HD_CHECK_EQ(internal::ColumnGap("a  b", 1, false, false), size_t{3});
    HD_CHECK_EQ(internal::ColumnGap("a b", 1, false, false), size_t{1});
    HD_CHECK_EQ(internal::ColumnGap("a\tb", 1, false, false), size_t{2});
    HD_CHECK_EQ(internal::ColumnGap("  b", 0, false, false), size_t{0});
    HD_CHECK_EQ(internal::ColumnGap("  b", 0, true, false), size_t{2});
    HD_CHECK_EQ(internal::ColumnGap("a  ", 1, false, true), size_t{3});
    // Inside a run, and in leading or trailing white space, the run is not a separator.
    HD_CHECK_EQ(internal::ColumnGap("a   b", 2, false, false), size_t{2});
    HD_CHECK_EQ(internal::ColumnGap("a   ", 1, false, false), size_t{1});
    // A long run of leading and trailing white space is cut in linear time.
    const std::string blanks(1 << 20, ' ');
    HD_CHECK_EQ(cut({{blanks + "猫" + blanks, output::SpanText}}), blanks + "猫" + blanks);
}
