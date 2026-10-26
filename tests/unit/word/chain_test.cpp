#include <deque>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/tokenizer/token.h"
#include "core/word/analyze.h"
#include "core/word/worker.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

struct Step {
    int token, start, length;
    bool overlapping;
};

// Inserts the steps into a chain over tokens with the given spans. The result is the
// removed length of each insert ("r<n>"), then the chain as "token:start+length".
std::string RunChain(const std::vector<int>& spans, std::initializer_list<Step> steps, bool text = false) {
    dict::File base;
    base.name = "base.tsv";
    base.text = "key\tko\tpos\tsem\tgram\tdict\tid\n";
    const auto snapshot = dict::Load({base}).snapshot;
    tokenizer::Tokens tokens;
    tokens.tokens.resize(spans.size());
    for (std::size_t i = 0; i < spans.size(); i++) tokens.tokens[i].span = spans[i];
    word::Arena arena;
    word::Settings settings = word::Settings::Reference();
    settings.text = text;
    word::Worker worker(tokens, *snapshot, dict::LookupContext{}, settings, arena, word::LookupObserver{});
    std::deque<word::Item> items;
    word::Chain chain;
    std::string out;
    for (const Step& s : steps) {
        word::Item& item = items.emplace_back();
        item.w = &worker;
        item.tokenIndex = s.token;
        item.start = s.start;
        item.length = s.length;
        const int removed = chain.Insert(&item, s.overlapping);
        if (!out.empty()) out += ' ';
        out += "r" + std::to_string(removed);
    }
    out += " |";
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) {
        out += ' ' + std::to_string(it->tokenIndex) + ':' + std::to_string(it->start) + '+' + std::to_string(it->length);
    }
    return out;
}

}  // namespace

// Each insert reports the length it adds minus the lengths it replaces; the chain follows.
HD_TEST(chain_insert_same_token) {
    HD_CHECK_EQ(RunChain({0}, {{0, 0, 3, false}}), std::string("r3 | 0:0+3"));
    HD_CHECK_EQ(RunChain({0}, {{0, 0, 3, false}, {0, 0, 2, false}}), std::string("r3 r-1 | 0:0+2"));
    HD_CHECK_EQ(RunChain({0}, {{0, 0, 3, false}, {0, 1, 2, false}}), std::string("r3 r2 | 0:1+2 0:0+3"));
    HD_CHECK_EQ(RunChain({0}, {{0, 0, 2, false}, {0, 1, 3, false}}), std::string("r2 r3 | 0:0+2 0:1+3"));
    HD_CHECK_EQ(RunChain({0}, {{0, 0, 3, false}, {0, 1, 4, true}}), std::string("r3 r1 | 0:1+4"));
    HD_CHECK_EQ(RunChain({0}, {{0, 0, 2, false}, {0, 2, 2, true}}), std::string("r2 r2 | 0:0+2 0:2+2"));
    HD_CHECK_EQ(RunChain({0}, {{0, 0, 5, false}, {0, 2, 2, true}}), std::string("r5 r2 | 0:2+2 0:0+5"));
    HD_CHECK_EQ(RunChain({0}, {{0, 0, 5, false}, {0, 2, 2, false}}), std::string("r5 r2 | 0:2+2 0:0+5"));
}

HD_TEST(chain_insert_across_tokens) {
    HD_CHECK_EQ(RunChain({0, 0}, {{0, 0, 2, false}, {1, 0, 2, false}}), std::string("r2 r2 | 0:0+2 1:0+2"));
    HD_CHECK_EQ(RunChain({0, 0}, {{1, 0, 2, false}, {0, 0, 2, false}}), std::string("r2 r2 | 0:0+2 1:0+2"));
    // The predecessor of a later token ends after the new item: the item goes after it.
    HD_CHECK_EQ(RunChain({0, 0, 0}, {{0, 0, 5, false}, {2, 0, 1, false}, {1, 0, 2, false}}),
                std::string("r5 r1 r2 | 0:0+5 2:0+1 1:0+2"));
    // A span predecessor ends at 1, so the item goes before the later token.
    HD_CHECK_EQ(RunChain({1, 0, 0}, {{0, 0, 5, false}, {2, 0, 1, false}, {1, 0, 2, false}}),
                std::string("r5 r1 r2 | 0:0+5 1:0+2 2:0+1"));
    HD_CHECK_EQ(RunChain({0, 0, 0}, {{0, 0, 1, false}, {2, 0, 1, false}, {1, 0, 3, false}}),
                std::string("r1 r1 r3 | 0:0+1 1:0+3 2:0+1"));
    // On the text path the item goes before the later token after all.
    HD_CHECK_EQ(RunChain({0, 0, 0}, {{0, 0, 5, false}, {2, 0, 1, false}, {1, 0, 2, false}}, true),
                std::string("r5 r1 r2 | 0:0+5 1:0+2 2:0+1"));
}
