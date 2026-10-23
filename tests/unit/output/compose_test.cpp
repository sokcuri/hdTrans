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

HD_TEST(compose_native_counts) {
    const CompositionCase cases[] = {
        {{{"三", "110", "NjNd", "3"}, {"匹", "110", "Ct", "마리"}}, {2}, "세 마리."},
        {{{"何回", "110", "", "몇회"}}, {1}, "몇 번."},
        {{{"三", "110", "NjNd", "3"}, {"回", "110", "Ct", "회"}}, {2}, "세 번."},
        // One in an ordinal is 첫, counted with 번.
        {{{"一", "110", "NjNd", "1"}, {"回", "110", "Ct", "회"}, {"目", "110", "Ct", "째"}}, {3}, "첫 번째."},
        {{{"一", "110", "NjNd", "1"}, {"匹", "110", "Ct", "마리"}, {"目", "110", "Ct", "째"}}, {3}, "첫 번째."},
        {{{"1番目", "110", "Ct", "1번째"}}, {1}, "첫 번째."},
        {{{"二", "110", "NjNd", "2"}, {"匹", "110", "Ct", "마리"}, {"目", "110", "Ct", "째"}}, {3}, "두 마리째."},
        // A count the dictionary reads apart from its counter: 첫 번 in an ordinal, people
        // with 명, the first day 첫날; outside an ordinal it stays.
        {{{"一度", "110", "Ct", "한 번"}, {"目", "110", "", "째"}}, {2}, "첫 번째."},
        {{{"一枚", "110", "", "한 장"}, {"目", "110", "Ct", "째"}}, {2}, "첫 번째."},
        {{{"二人", "110", "", "두 사람"}, {"目", "110", "Ct", "째"}}, {2}, "두 명째."},
        {{{"一日", "110", "", "하루"}, {"目", "110", "Ct", "째"}}, {2}, "첫날."},
        {{{"一枚", "110", "", "한 장"}, {"の", "200", "", "의"}}, {2}, "한 장의."},
        {{{"一日", "110", "", "하루"}}, {1}, "하루."},
        // 체 counts with Sino-Korean numerals.
        {{{"八", "110", "NjNd", "8"}, {"体", "110", "Ct", "체"}}, {2}, "8체."},
        {{{"数", "110", "NjNd", "몇"}, {"体", "110", "Ct", "체"}}, {2}, "몇 체."},
        {{{"二十", "110", "NjNd", "20"}, {"匹", "110", "Ct", "마리"}}, {2}, "20마리."},
    };
    for (const auto& c : cases) CheckComposition(c, true);
}
