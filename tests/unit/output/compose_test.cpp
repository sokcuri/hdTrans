#include <chrono>
#include <vector>

#include "core/output/compose.h"
#include "core/output/text.h"
#include "core/tokenizer/token.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::output;

namespace {

struct Word {
    const char* surface;
    const char* code;
    const char* tags;
    const char* target;
    const char* ending = "";
    const char* firstTarget = nullptr;
};

struct CompositionCase {
    std::vector<Word> words;
    std::vector<int> ends;
    const char* expected;
};

void CheckComposition(const CompositionCase& c, bool literal = false) {
    std::vector<TokenText> tokens = {
        {tokenizer::kTypeJapanese, 0, "", ""},
        {tokenizer::kTypeSentenceEnd, 0, ".", ""},
        {tokenizer::kTypeTruncated, 0, "", ""},
    };
    std::vector<ResolvedItem> items;
    for (const Word& word : c.words) {
        ResolvedItem item;
        item.offset = static_cast<int>(tokens[0].text.size());
        item.surface = word.surface;
        item.length = static_cast<int>(item.surface.size());
        item.selected = 0;
        item.first = item.entry = {word.code, word.tags, word.target};
        if (word.firstTarget != nullptr) {
            item.selected = 1;
            item.first.text = word.firstTarget;
        }
        if (*word.ending != '\0') {
            item.directIndex = 0;
            item.ending = {"500", "", word.ending};
        }
        tokens[0].text += item.surface;
        items.push_back(std::move(item));
    }
    Nodes nodes;
    BuildItems(items, tokens, nodes);
    HD_CHECK_EQ(nodes.size(), c.ends.size());
    for (size_t i = 0; i < nodes.size() && i < c.ends.size(); ++i) HD_CHECK_EQ(nodes[i], c.ends[i]);
    Composer composer;
    Families families;
    Settings settings;
    settings.text = true;
    settings.literal = literal;
    Pieces result;
    composer.Compose(items, tokens, 0, 1, {}, settings, families, result);
    HD_CHECK_EQ(String(result), c.expected);
}

}  // namespace

HD_TEST(compose_prefix_run_boundaries) {
    const CompositionCase cases[] = {
        {{{"金", "110", "Z6Z7J5Mo", "금", "", "김"}, {"と", "201", "J1", "(와)과"},
          {"銀", "110", "MoZ6", "은"}, {"がある", "428", "TuPjVaMa", "(이)가 &HN@", "4다"}},
         {2, 3, 4}, "금과 은이 있다."},
        {{{"金", "110", "Z6Z7J5Mo", "금"}, {"も", "200", "", "도"},
          {"銀", "110", "MoZ6", "은"}},
         {2, 3}, "금도 은."},
        {{{"金", "110", "Z6Z7J5Mo", "금"}, {"落ちた", "427", "", "떨어&AL@", "5다"}},
         {1, 2}, "금 떨어졌다."},
        {{{"金", "110", "Z6Z7J5Mo", "금"}, {"貨", "110", "", "화"}},
         {2}, "금화."},
        {{{"非", "110", "Z6", "비"}, {"現実的な", "800", "", "현실적인"}},
         {2}, "비현실적인."},
        {{{"第", "110", "Z6Ha", "제"}, {"○", "600", "Nd", "○"},
          {"話", "110", "Ct", "화"}},
         {3}, "제○화."},
        {{{"新", "110", "Z6", "신"}, {"金", "110", "Z6Z7J5Mo", "금"},
          {"貨", "110", "", "화"}},
         {3}, "신금화."},
        {{{"金", "110", "Z6Z7J5Mo", "금"}, {"銀", "110", "MoZ6", "은"}},
         {2}, "금은."},
        {{{"金", "110", "Z6Z7J5Mo", "금"}, {"色", "110", "Z7", "색"},
          {"の", "200", "", "의"}, {"箱", "110", "", "상자"}},
         {3, 4}, "금색의 상자."},
    };
    for (const auto& c : cases) CheckComposition(c);
}

HD_TEST(compose_adverb_omitted_particle_spacing) {
    const CompositionCase cases[] = {
        {{{"彼", "120", "", "그"}, {"は", "200", "", "(은)는"},
          {"げんなり", "300", "", "맥없이"}, {"と", "200", "", "", "", "(와)과"},
          {"座った", "418", "", "&FI@", "5다"}},
         {2, 4, 5}, "그는 맥없이 앉았다."},
        {{{"血", "110", "", "피"}, {"が", "211", "", "(이)가"},
          {"ボタッボタ", "301", "", "뚝뚝"}, {"と", "200", "", "", "", "(와)과"},
          {"落ちた", "427", "", "떨어&AL@", "5다"}},
         {2, 4, 5}, "피가 뚝뚝 떨어졌다."},
        {{{"しっかり", "300", "", "똑바로"}, {"と", "200", "", "", "", "(와)과"},
          {"座った", "418", "", "&FI@", "5다"}},
         {2, 3}, "똑바로 앉았다."},
        {{{"いつ", "300", "", "언제"}, {"まで", "200", "", "까지"},
          {"座った", "418", "", "&FI@", "5다"}},
         {2, 3}, "언제까지 앉았다."},
        {{{"げんなり", "300", "", "맥없이"}, {"と", "200", "", "", "", "(와)과"}},
         {2}, "맥없이."},
        {{{"げんなり", "300", "", "맥없이"}, {"と", "200", "", "", "", "(와)과"},
          {"、", "600", "", ","}, {"座った", "418", "", "&FI@", "5다"}},
         {2, 3, 4}, "맥없이, 앉았다."},
    };
    for (const auto& c : cases) CheckComposition(c);
}

HD_TEST(compose_place_noun_after_dropped_no) {
    const CompositionCase cases[] = {
        {{{"店", "110", "", "가게"}, {"の", "230", "", ""}, {"中", "110", "", "안"}},
         {2, 3}, "가게 안."},
        {{{"箱", "110", "", "상자"}, {"の", "230", "", ""}, {"上", "110", "", "위"}},
         {2, 3}, "상자 위."},
        {{{"三日", "110", "", "3일"}, {"の", "230", "", ""}, {"間", "110", "Ct", "간"}},
         {2, 3}, "3일간."},
        {{{"金", "110", "", "금"}, {"の", "230", "", ""}, {"貨", "110", "", "화"}},
         {2, 3}, "금화."},
    };
    for (const auto& c : cases) CheckComposition(c);
}

HD_TEST(compose_untranslated_honorific_spacing) {
    const CompositionCase cases[] = {
        {{{"魔王", "110", "", "마왕"}, {"ちゃん", "110", "", ""}, {"の", "200", "", "의"},
          {"本", "110", "", "책"}},
         {1, 3, 4}, "마왕의 책."},
    };
    for (const auto& c : cases) CheckComposition(c);
}

HD_TEST(compose_sentence_token_ranges) {
    std::vector<TokenText> tokens(12000);
    for (TokenText& token : tokens) {
        token.type = tokenizer::kTypeAlpha;
        token.text = "text";
    }
    Composer composer;
    Families families;
    Settings settings;
    settings.text = true;
    std::vector<ResolvedItem> items;
    Pieces result;
    const auto start = std::chrono::steady_clock::now();
    for (int first = 0; first < static_cast<int>(tokens.size()); first += 3) {
        Pieces sentence;
        composer.Compose(items, tokens, first, first + 2, {}, settings, families, sentence);
        HD_CHECK_EQ(String(sentence), "texttexttext");
        Append(result, sentence);
    }
    HD_CHECK_EQ(String(result).size(), std::size_t{48000});
    for (const TokenText& token : tokens) HD_CHECK_EQ(token.text, "text");
    std::printf("compose 4000 sentences: %.3f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

// A number or a Latin letter joins the noun written straight after it in the source; a label
// (a reply number, a time, a four-digit number before kanji, a Latin word) and a verbal noun
// that is the predicate stand apart; an abbreviation stands apart from a verbal noun too.
HD_TEST(compose_number_before_noun) {
    const CompositionCase cases[] = {
        {{{"1", "110", "Nd", "1"}, {"レベル", "110", "", "레벨"}}, {1, 2}, "1레벨."},
        {{{"22", "110", "Nd", "22"}, {"コンボ", "110", "", "콤보"}}, {1, 2}, "22콤보."},
        {{{"ステージ", "110", "", "스테이지"}, {"3", "110", "Nd", "3"}, {"ボス戦", "110", "", "보스전"}}, {1, 2, 3}, "스테이지 3 보스전."},
        {{{"Lv.10", "110", "", "Lv.10"}, {"スキル", "110", "", "스킬"}}, {1, 2}, "Lv.10 스킬."},
        {{{"ロウリア", "110", "", "로우리아"}, {"34", "110", "Nd", "34"}, {"世", "110", "", "세"}}, {1, 3}, "로우리아 34세."},
        {{{"ATB", "110", "", "ATB"}, {"ゲージ", "110", "", "게이지"}}, {1, 2}, "ATB 게이지."},
        {{{"A", "110", "", "A"}, {"ランク", "110", "", "랭크"}}, {1, 2}, "A랭크."},
        {{{"SS", "110", "", "SS"}, {"ランク", "110", "", "랭크"}}, {1, 2}, "SS랭크."},
        {{{"3", "110", "Nd", "3"}, {"学期", "110", "", "학기"}}, {1, 2}, "3학기."},
        {{{"第五", "110", "", "제5"}, {"戦隊", "110", "", "전대"}}, {1, 2}, "제5전대."},
        {{{"2026", "110", "Nd", "2026"}, {"特別", "110", "", "특별"}}, {1, 2}, "2026 특별."},
        {{{"2300", "110", "Nd", "2300"}, {"提督", "110", "", "제독"}}, {1, 2}, "2300 제독."},
        {{{"≫372", "110", "Nd", "≫372"}, {"過疎", "110", "", "과소"}}, {1, 2}, "≫372 과소."},
        {{{"23：40", "110", "Nd", "23：40"}, {"追記", "110", "", "추기"}}, {1, 2}, "23：40 추기."},
        {{{"Bismarck", "110", "", "Bismarck"}, {"追撃戦", "110", "", "추격전"}}, {1, 2}, "Bismarck 추격전."},
        {{{"Iowa", "110", "", "Iowa"}, {"級", "110", "CtZ7", "급"}}, {2}, "Iowa급."},
        {{{"Java", "110", "", "Java"}, {"風", "110", "", "풍"}}, {2}, "Java풍."},
        {{{"10cm", "110", "", "10cm"}, {"砲", "110", "", "포"}}, {2}, "10cm포."},
        {{{"1000", "110", "Nd", "1000"}, {"G", "110", "", "G"}}, {1, 2}, "1000G."},
        {{{"キハ", "110", "", "키하"}, {"208", "110", "Nd", "208"}, {"車両", "110", "", "차량"}}, {1, 2, 3}, "키하 208 차량."},
        {{{"9", "110", "Nd", "9"}, {"米国", "130", "", "미국"}}, {1, 2}, "9미국."},
        {{{"1", "110", "Nd", "1"}, {"回復", "110", "Hd", "회복"}}, {1, 2}, "1 회복."},
        {{{"SP", "110", "", "SP"}, {"消費", "110", "Hd", "소비"}}, {1, 2}, "SP 소비."},
        {{{"Lv2", "110", "", "Lv2"}, {"スキル", "110", "", "스킬"}}, {1, 2}, "Lv2 스킬."},
        {{{"改二", "110", "", "개2"}, {"改装", "110", "", "개장"}}, {1, 2}, "개2개장."},
    };
    for (const auto& c : cases) CheckComposition(c);
}

HD_TEST(compose_native_counts) {
    const CompositionCase cases[] = {
        {{{"三", "110", "NjNd", "3"}, {"匹", "110", "Ct", "마리"}}, {2}, "세 마리."},
        {{{"何回", "110", "", "몇회"}}, {1}, "몇 번."},
        {{{"三", "110", "NjNd", "3"}, {"回", "110", "Ct", "회"}}, {2}, "세 번."},
        // One in an ordinal is 첫, counted with 번.
        {{{"一", "110", "NjNd", "1"}, {"回", "110", "Ct", "회"}, {"目", "110", "Ct", "째"}}, {3}, "첫 번째."},
        {{{"一", "110", "NjNd", "1"}, {"匹", "110", "Ct", "마리"}, {"目", "110", "Ct", "째"}}, {3}, "첫 번째."},
        {{{"1番目", "110", "Ct", "1번째"}}, {1}, "1번째."},
        {{{"3", "110", "Nd", "3"}, {"回", "110", "Ct", "회"}}, {2}, "3회."},
        {{{"7", "110", "Nd", "7"}, {"枚", "110", "Ct", "장"}}, {2}, "7장."},
        {{{"二", "110", "NjNd", "2"}, {"匹", "110", "Ct", "마리"}, {"目", "110", "Ct", "째"}}, {3}, "두 마리째."},
        // A count the dictionary reads apart from its counter: 첫 번 in an ordinal, people
        // with 번, the first day 첫날; outside an ordinal it stays.
        {{{"一度", "110", "Ct", "한 번"}, {"目", "110", "", "째"}}, {2}, "첫 번째."},
        {{{"一枚", "110", "", "한 장"}, {"目", "110", "Ct", "째"}}, {2}, "첫 번째."},
        {{{"二人", "110", "", "두 사람"}, {"目", "110", "Ct", "째"}}, {2}, "두 번째."},
        {{{"一日", "110", "", "하루"}, {"目", "110", "Ct", "째"}}, {2}, "첫날."},
        // One period is the first of that period; one of things is 첫 번째.
        {{{"一ヶ月", "110", "", "한 달"}, {"目", "110", "Ct", "째"}}, {2}, "첫 달."},
        {{{"二ヶ月", "110", "", "두 달"}, {"目", "110", "Ct", "째"}}, {2}, "두 달째."},
        {{{"一発", "110", "", "한 방"}, {"目", "110", "Ct", "째"}}, {2}, "첫 번째."},
        {{{"一枚", "110", "", "한 장"}, {"の", "200", "", "의"}}, {2}, "한 장의."},
        {{{"一日", "110", "", "하루"}}, {1}, "하루."},
        // 체 counts with Sino-Korean numerals.
        {{{"八", "110", "NjNd", "8"}, {"体", "110", "Ct", "체"}}, {2}, "8체."},
        {{{"数", "110", "NjNd", "몇"}, {"体", "110", "Ct", "체"}}, {2}, "몇 체."},
        {{{"二十", "110", "NjNd", "20"}, {"匹", "110", "Ct", "마리"}}, {2}, "20마리."},
    };
    for (const auto& c : cases) CheckComposition(c, true);
}

// The ordinal prefix 제 at the end of a token is written with the number that starts the next one.
HD_TEST(compose_ordinal_prefix_before_number_token) {
    std::vector<TokenText> tokens = {
        {tokenizer::kTypeJapanese, 0, "序章第", ""},
        {tokenizer::kTypeJapanese, 0, "2話", ""},
        {tokenizer::kTypeSentenceEnd, 0, ".", ""},
        {tokenizer::kTypeTruncated, 0, "", ""},
    };
    struct Placed {
        int token, offset;
        const char *surface, *code, *tags, *target;
    };
    const Placed placed[] = {
        {0, 0, "序章", "110", "", "서장"},
        {0, 6, "第", "110", "Z6Ha", "제"},
        {1, 0, "2", "600", "Nd", "2"},
        {1, 1, "話", "110", "Ct", "화"},
    };
    std::vector<ResolvedItem> items;
    for (const Placed& p : placed) {
        ResolvedItem item;
        item.tokenIndex = p.token;
        item.offset = p.offset;
        item.surface = p.surface;
        item.length = static_cast<int>(item.surface.size());
        item.selected = 0;
        item.first = item.entry = {p.code, p.tags, p.target};
        items.push_back(std::move(item));
    }
    Nodes nodes;
    BuildItems(items, tokens, nodes);
    Composer composer;
    Families families;
    Settings settings;
    settings.text = true;
    Pieces result;
    composer.Compose(items, tokens, 0, 2, {}, settings, families, result);
    HD_CHECK_EQ(String(result), "서장 제2화.");
}

// A particle that starts a Japanese token joins a Latin word or a number before it across a
// space of the source; a noun after the space stays apart.
HD_TEST(compose_particle_joins_latin_word_across_space) {
    struct Case {
        const char* word;
        int wordType;
        const char *surface, *code, *target;
        const char* expected;
    };
    const Case cases[] = {
        {"wiki", tokenizer::kTypeAlpha, "の", "211", "의", "wiki의."},
        {"API", tokenizer::kTypeAlpha, "です", "200", "입니다", "API입니다."},
        {"3", tokenizer::kTypeNumber, "も", "200", "도", "3도."},
        {"wiki", tokenizer::kTypeAlpha, "記事", "110", "기사", "wiki 기사."},
    };
    for (const Case& c : cases) {
        std::vector<TokenText> tokens = {
            {c.wordType, 0, c.word, ""},
            {tokenizer::kTypeAsciiSpace, 0, " ", ""},
            {tokenizer::kTypeJapanese, 0, c.surface, ""},
            {tokenizer::kTypeSentenceEnd, 0, ".", ""},
            {tokenizer::kTypeTruncated, 0, "", ""},
        };
        std::vector<ResolvedItem> items(2);
        items[0].tokenIndex = 0;
        items[0].surface = c.word;
        items[0].first = items[0].entry = {"110", "Nm", c.word};
        items[1].tokenIndex = 2;
        items[1].surface = c.surface;
        items[1].first = items[1].entry = {c.code, "", c.target};
        for (ResolvedItem& item : items) {
            item.length = static_cast<int>(item.surface.size());
            item.selected = 0;
        }
        Nodes nodes;
        BuildItems(items, tokens, nodes);
        Composer composer;
        Families families;
        Settings settings;
        settings.text = true;
        Pieces result;
        composer.Compose(items, tokens, 0, 3, {}, settings, families, result);
        HD_CHECK_EQ(String(result), c.expected);
    }
}

// A katakana verbal noun joins the される after it when 되다 follows it in the Korean.
HD_TEST(compose_verbal_noun_passive) {
    const CompositionCase cases[] = {
        {{{"マージ", "110", "", "머지"}, {"され", "427", "", "&C6@", "5다"}}, {1, 2}, "머지되었다."},
        {{{"マージ", "110", "", "머지"}, {"し", "427", "", "&AV@", "5다"}}, {1, 2}, "머지했다."},
        {{{"幽閉", "110", "", "유폐"}, {"され", "427", "", "&C6@", "5다"}}, {1, 2}, "유폐되었다."},
        // A kanji doubled with 々 is not a する noun.
        {{{"色々", "110", "", "여러 가지"}, {"され", "427", "", "&C6@", "5다"}}, {1, 2}, "여러 가지 되었다."},
        {{{"色々", "110", "", "여러 가지"}, {"し", "427", "", "&AV@", "5다"}}, {1, 2}, "여러 가지 했다."},
    };
    for (const auto& c : cases) CheckComposition(c);
}

// An ellipsis or a mark of tone closes the word before it: the space the word was written
// with goes, but not one of the input or one after a sentence mark.
HD_TEST(compose_emit_closing_symbols) {
    struct Case {
        std::vector<TokenText> tokens;
        const char* expected;
    };
    const Case cases[] = {
        {{{tokenizer::kTypeJapanese, 0, "그건 ", ""}, {tokenizer::kTypeSymbol, 0, "……", ""}}, "그건……"},
        {{{tokenizer::kTypeJapanese, 0, "선배 ", ""}, {tokenizer::kTypeSymbol, 0, "♡", ""}}, "선배♡"},
        {{{tokenizer::kTypeJapanese, 0, "좀 더 ", ""}, {tokenizer::kTypeSymbol, 0, "♪", ""}}, "좀 더♪"},
        {{{tokenizer::kTypeJapanese, 0, "그건", ""}, {tokenizer::kTypeAsciiSpace, 0, " ", ""}, {tokenizer::kTypeSymbol, 0, "……", ""}},
         "그건 ……"},
        {{{tokenizer::kTypeJapanese, 0, "했다. ", ""}, {tokenizer::kTypeSymbol, 0, "……", ""}}, "했다. ……"},
        {{{tokenizer::kTypeJapanese, 0, "그건 ", ""}, {tokenizer::kTypeSymbol, 0, "※", ""}}, "그건 ※"},
    };
    Settings settings;
    for (const Case& c : cases) {
        Pieces out;
        EmitSynchronousOutput(c.tokens, 0, static_cast<int>(c.tokens.size()) - 1, settings, out);
        HD_CHECK_EQ(String(out), c.expected);
    }
}
