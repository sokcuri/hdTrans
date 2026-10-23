#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/analyze.h"
#include "core/word/worker.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::shared_ptr<const dict::Snapshot> Dictionary(std::string rows) {
    dict::File base;
    base.name = "base.tsv";
    base.text = "key\tko\tpos\tsem\tgram\tdict\tid\n" + std::move(rows);
    return dict::Load({base}).snapshot;
}

// The word items of text as "start+length:surface".
std::vector<std::string> Items(const dict::Snapshot& snapshot, std::string_view text, bool textPath) {
    tokenizer::Tokens tokens;
    tokenizer::Tokenize(text, tokens);
    word::Arena arena;
    word::Analysis analysis;
    word::Settings settings = word::Settings::Reference();
    settings.text = textPath;
    word::Analyze(tokens, snapshot, dict::LookupContext{}, settings, arena, analysis);
    std::vector<std::string> out;
    for (const word::Sentence& s : analysis.sentences) {
        for (const word::Item* it = s.chain.head; it != nullptr; it = it->next) {
            out.push_back(std::to_string(it->start) + "+" + std::to_string(it->length) + ":" +
                          std::string(it->group->surface));
        }
    }
    return out;
}

}  // namespace

// A token of more words than the search depth is still read word by word, in order,
// past every point where the search stops and goes on.
HD_TEST(word_long_token_reads_every_word) {
    const auto snapshot = Dictionary("犬\t개\tN110\t\t\tGN\t1\n猫\t고양이\tN110\t\t\tGN\t2\n");
    HD_REQUIRE(snapshot != nullptr);
    const int pairs = word::Worker::kMaxWordDepth + 7;
    std::string text;
    for (int i = 0; i < pairs; i++) text += "犬猫";
    for (const bool textPath : {false, true}) {
        const std::vector<std::string> items = Items(*snapshot, text, textPath);
        HD_REQUIRE(items.size() == static_cast<std::size_t>(2 * pairs));
        bool ordered = true;
        for (int i = 0; i < 2 * pairs; i++) {
            const std::string want = std::to_string(3 * i) + "+3:" + (i % 2 == 0 ? "犬" : "猫");
            ordered = ordered && items[static_cast<std::size_t>(i)] == want;
        }
        HD_CHECK(ordered);
    }
}
