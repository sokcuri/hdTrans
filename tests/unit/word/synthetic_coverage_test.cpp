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

// き is rejected as a word before ま by the validity rules, and きま is not in the
// dictionary, so neither a word nor its fallback passes the lookup gates. The fallback
// still has to cover き: the compose stage writes the source text of a span no item
// covers.
HD_TEST(word_synthetic_gap_is_covered) {
    const auto snapshot = Dictionary("");
    HD_REQUIRE(snapshot != nullptr);
    for (const bool textPath : {false, true}) {
        const std::vector<std::string> items = Items(*snapshot, "きま", textPath);
        HD_REQUIRE(items.size() == 2);
        HD_CHECK_EQ(items[0], std::string("0+3:き"));
        HD_CHECK_EQ(items[1], std::string("3+3:ま"));
    }
}

// A word of the dictionary still takes the whole span over the per-character fallbacks.
HD_TEST(word_synthetic_gap_keeps_dictionary_words) {
    const auto snapshot = Dictionary("きま\t결정\tN110\t\t\tGN\t1\n");
    HD_REQUIRE(snapshot != nullptr);
    const std::vector<std::string> items = Items(*snapshot, "きま", true);
    HD_REQUIRE(items.size() == 1);
    HD_CHECK_EQ(items[0], std::string("0+6:きま"));
}
