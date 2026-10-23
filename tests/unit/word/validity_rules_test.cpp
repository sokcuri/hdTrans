#include "core/dict/snapshot.h"
#include "core/tokenizer/token.h"
#include "core/word/analyze.h"
#include "core/word/validity_rules.h"
#include "core/word/worker.h"

#include <cstdint>
#include <string>

#include "fnv.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::word;

// The expected value is the hash of the rows the data was generated from, in order.
HD_TEST(validity_rules_fingerprint) {
    const auto rules = ValidityRules();
    hdtest::Fnv h;
    h.U32(static_cast<uint32_t>(rules.size()));
    for (const auto& r : rules) {
        h.Str(r.source);
        h.Byte(static_cast<uint8_t>(r.sourceClass));
        h.Byte(static_cast<uint8_t>(r.followClass));
        h.Str(r.first);
    }
    HD_CHECK_EQ(h.h, uint64_t{0x813edc77a65cfabaull});
    HD_CHECK_EQ(rules.size(), size_t{745});
    HD_REQUIRE(!rules.empty());
    HD_CHECK_EQ(std::string(rules[0].source), std::string("でも"));
    HD_CHECK_EQ(rules[0].sourceClass, '2');
    HD_CHECK_EQ(rules[0].followClass, 'X');
}

// A rule rejects the following text when it starts with one of the rejection tokens. The XX rules check
// only the source class and the tokens, not the following class.
HD_TEST(validity_match_dictionary) {
    dict::File base;
    base.name = "base.tsv";
    base.text = "key\tko\tpos\tsem\tgram\tdict\tid\n";
    const auto snapshot = dict::Load({base}).snapshot;
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Worker worker(tokens, *snapshot, dict::LookupContext{}, word::Settings::Reference(), arena, word::LookupObserver{});
    struct Case {
        std::string_view following, source;
        char first, second;
        bool ok;
    };
    const Case cases[] = {
        {"った", "でも", '2', 'X', false}, {"て", "でも", '2', 'X', false},   {"ら", "でも", '2', 'X', true},
        {"", "でも", '2', 'X', true},      {"め", "でも", '2', 'X', true},    {"めて", "でも", '2', 'X', false},
        {"の", "でも", '2', 'X', false},   {"った", "でも", '3', 'X', true},  {"った", "でも", '2', 'Y', true},
        {"った", "です", '2', 'X', true},  {"ご", "です", '2', 'X', false},   {"っきり", "です", '2', 'X', false},
        {"た", "いったん", '3', 'X', true}, {"で", "いったん", '3', 'X', false},
        {"ですか", "zzz", '3', 'X', false}, {"ですか", "zzz", '3', 'Y', false}, {"ね", "zzz", '3', 'X', true},
        // ところで does not end before the copula である (回ったところである: ところ + である).
        // The verb だ(く) in its potential past does not run into ため (だけため息: だけ + ため息).
        {"けため息", "だ", '4', 'X', false}, {"けた。", "だ", '4', 'X', true},
        // An adjective ending かったと does not run into ころ (しなかったところ: ところ).
        {"ころは", "かったと", '5', '7', false},
        // The adjectival noun 急 does not take the か of the verb stem 急か (急かした: 急かす, not 急 + か + した).
        {"かした。", "急", '8', 'X', false}, {"かされて", "急", '8', 'X', false}, {"な話", "急", '8', 'X', true},
        {"ある。", "ところで", '5', '4', false}, {"あろう", "ところで", '3', 'X', false}, {"、", "ところで", '5', '4', true},
    };
    for (const Case& c : cases) {
        HD_CHECK_EQ(worker.MatchDictionary(c.following, c.source, c.first, c.second), c.ok);
    }
}
