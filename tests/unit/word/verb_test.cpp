#include <deque>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "core/word/worker.h"
#include "core/word/flush.h"
#include "core/tokenizer/tokenize.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

// Records with the given (source, part code) pairs in order, kept alive by pool.
word::Record* RecordList(std::deque<word::Record>& pool,
                         std::initializer_list<std::pair<std::string_view, std::string_view>> list) {
    word::Record* head = nullptr;
    word::Record* last = nullptr;
    for (const auto& [source, partCode] : list) {
        word::Record& r = pool.emplace_back();
        r.source = source;
        r.partCode = partCode;
        if (last != nullptr) last->child = &r;
        else head = &r;
        last = &r;
    }
    return head;
}

}  // namespace

// Branch and length of the conjugation, or no match.
HD_TEST(verb_analyse_tail) {
    struct Case {
        std::string_view key;
        char kind;
        std::string_view tail;
        bool ok;
        int branch, length;
    };
    const Case cases[] = {
        {"書", '1', "いた", true, 6, 6},      {"書", '1', "いて", true, 7, 6},
        {"書", '1', "かない", true, 8, 3},    {"書", '1', "く", true, 0, 3},
        {"書", '1', "いたら", true, 6, 6},    {"書", '1', "いた、", true, 6, 6},
        {"書", '1', "いたっけ", true, 6, 6},  {"行", '?', "った", true, 6, 6},
        {"行", '?', "く", true, 0, 3},        {"行", '1', "った", false, 0, 0},
        {"食べ", '3', "た", false, 0, 0},     {"食べ", '3', "て", false, 0, 0},
        {"食べ", '3', "ない", false, 0, 0},   {"食べ", '3', "", false, 0, 0},
        {"見", '2', "た", false, 0, 0},       {"書", '1', "ぃた", false, 0, 0},
        {"書", '1', "", false, 0, 0},         {"書", '7', "た", true, 6, 3},
        {"書", '7', "いた", true, 8, 0},      {"する", '5', "た", false, 0, 0},
    };
    for (const Case& c : cases) {
        int branch = 0, length = 0;
        const bool ok = word::AnalyseVerbTail(c.key, c.kind, c.tail, branch, length);
        HD_CHECK_EQ(ok, c.ok);
        if (c.ok) {
            HD_CHECK_EQ(branch, c.branch);
            HD_CHECK_EQ(length, c.length);
        }
    }
}

HD_TEST(verb_bare_command_boundaries) {
    struct Row {
        std::string_view key;
        char kind;
        std::string_view e;
    };
    const Row rows[] = {
        {"買", '0', "え"}, {"書", '1', "け"}, {"泳", '2', "げ"},
        {"出", '3', "せ"}, {"待", '4', "て"}, {"読", '5', "め"},
        {"遊", '6', "べ"}, {"帰", '8', "れ"}, {"行", '?', "け"},
    };
    for (const Row& row : rows) {
        for (const std::string_view boundary : {"」と", "』と", "」って", "』って", "だの", "」だの", "』だの"}) {
            int branch = -1, length = -1;
            HD_CHECK(word::AnalyseVerbTail(row.key, row.kind, std::string(row.e) + std::string(boundary), branch, length));
            HD_CHECK_EQ(branch, 11);
            HD_CHECK_EQ(length, 3);
        }
        for (const std::string_view continuation : {"る", "ない", "た", "れば", "ます", "本", "の", "よ", "」の", "』か"}) {
            int branch = -1, length = -1;
            HD_CHECK(word::AnalyseVerbTail(row.key, row.kind, std::string(row.e) + std::string(continuation), branch, length));
            HD_CHECK_EQ(branch, 4);
            HD_CHECK_EQ(length, 3);
        }
    }
    struct Case {
        std::string_view key;
        char kind;
        std::string_view tail;
        int branch, length;
    };
    const Case cases[] = {
        {"書", '1', "け", 11, 3},
        {"書", '1', "けば", 3, 6}, {"書", '1', "かれた", 1, 6},
        {"書", '1', "いて", 7, 6}, {"読", '5', "んで", 7, 6},
        {"待", '4', "って", 7, 6}, {"見", '7', "ろ」と", 11, 3},
        {"見", '7', "て」と", 7, 3}, {"する", '9', "しろ』って", 11, 6},
        {"する", '9', "できる」と", 4, 6},
    };
    for (const Case& c : cases) {
        int branch = -1, length = -1;
        HD_CHECK(word::AnalyseVerbTail(c.key, c.kind, c.tail, branch, length));
        HD_CHECK_EQ(branch, c.branch);
        HD_CHECK_EQ(length, c.length);
    }
}

HD_TEST(verb_bare_command_token_boundary) {
    tokenizer::Tokens tokens;
    tokenizer::Tokenize("書け。", tokens);
    HD_REQUIRE(tokens.size() >= 2);
    const std::string_view verb = tokens.Raw(0);
    HD_REQUIRE(verb == "書け");
    HD_CHECK(tokens.Raw(1) == "。");
    int branch = -1, length = -1;
    HD_CHECK(word::AnalyseVerbTail("書", '1', verb.substr(std::string_view("書").size()), branch, length));
    HD_CHECK_EQ(branch, 11);
    HD_CHECK_EQ(length, 3);
}

// The branch, the pattern kind of the last record tried, and the record the walk stopped at
// (null when it ran out).
HD_TEST(verb_match_conjugation) {
    struct Case {
        std::string_view text;
        int offset;
        std::initializer_list<std::pair<std::string_view, std::string_view>> records;
        bool ok;
        int kind, patternKind;
        std::string_view stop;  // part code of the stop record, "" for none
    };
    const Case cases[] = {
        {"書いた", 0, {{"書", "M421"}}, true, 6, 1, "M421"},
        {"書いた", 1, {{"いた", "M421"}}, false, -1, 1, ""},
        {"書かない", 0, {{"書", "M422"}, {"書", "M425"}}, false, -1, 5, ""},
        {"書いた", 0, {{"書", "M122"}, {"書", "M421"}}, false, -1, -1, "M122"},
        {"食べた", 0, {{"食べ", "M423"}}, false, -1, 3, ""},
        {"食べた", 0, {{"食べ", "M431"}, {"食べ", "M433"}}, false, -1, 3, ""},
        {"書いた", 9, {{"書", "M421"}}, false, -1, -1, "M421"},
    };
    for (const Case& c : cases) {
        std::deque<word::Record> pool;
        word::Record* records = RecordList(pool, c.records);
        word::Detail d;
        word::Record* stop = nullptr;
        const bool ok = word::MatchConjugation(d, c.text, c.offset, records, stop);
        HD_CHECK_EQ(ok, c.ok);
        HD_CHECK_EQ(d.kind, c.kind);
        HD_CHECK_EQ(d.patternKind, c.patternKind);
        HD_CHECK_EQ(std::string(stop == nullptr ? std::string_view() : stop->partCode), std::string(c.stop));
    }
}

// Part code byte 3 '>' is pattern kind 14 (no ending): it matches with the length of the
// record's source and leaves the branch unset.
HD_TEST(verb_match_direct_kind) {
    std::deque<word::Record> pool;
    word::Record* records = RecordList(pool, {{"書", "M42>"}});
    word::Detail d;
    word::Record* stop = nullptr;
    HD_CHECK(word::MatchConjugation(d, "書", 0, records, stop));
    HD_CHECK_EQ(d.kind, -1);
    HD_CHECK_EQ(d.patternKind, 14);
    HD_CHECK_EQ(d.patternMatched, 1);
    HD_CHECK_EQ(d.patternLength, 3);
}

HD_TEST(word_expanded_candidate_groups) {
    const auto snapshot = dict::Load({{0, "base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"}}).snapshot;
    HD_REQUIRE(snapshot != nullptr);
    for (bool expanded : {false, true}) {
        tokenizer::Tokens tokens;
        word::Arena arena;
        word::Settings settings = word::Settings::Reference();
        settings.improvements.expandedGroups = expanded;
        word::Worker worker(tokens, *snapshot, {}, settings, arena, {});
        std::deque<word::Record> records(1001);
        for (std::size_t i = 0; i < records.size(); i++) {
            records[i].partCode = "S500";
            if (i + 1 < records.size()) records[i].child = &records[i + 1];
        }
        const auto* group = worker.NewGroup(&records[0], ' ');
        const std::size_t count = expanded ? 1000 : 100;
        HD_CHECK_EQ(group->entries.size(), count);
        HD_CHECK(group->records == &records[count]);
        word::Detail* detail = worker.NewDetail(word::kDetailVerb, -1);
        detail->ResetDirect(count);
        detail->enabled[count - 1] = false;
        detail->ResetEnabled(true);
        HD_CHECK(detail->enabled[count - 1]);
    }
}

HD_TEST(word_expanded_ending_selection) {
    const auto snapshot = dict::Load({{0, "base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"}}).snapshot;
    HD_REQUIRE(snapshot != nullptr);
    for (bool expanded : {false, true}) {
        tokenizer::Tokens tokens;
        word::Arena arena;
        word::Settings settings = word::Settings::Reference();
        settings.improvements.expandedGroups = expanded;
        word::Worker worker(tokens, *snapshot, {}, settings, arena, {});
        word::Record record;
        record.source = "寝";
        record.partCode = "M417";
        auto* group = worker.NewGroup(&record, '0');
        auto* detail = worker.NewDetail(word::kDetailVerb, -1);
        detail->kind = 10;
        detail->group = worker.NewGroup(nullptr, ' ');
        for (int i = 0; i < 130; i++) {
            auto* entry = worker.NewEntry();
            entry->code = i == 129 ? "5:0" : "500";
            detail->group->entries.push_back(entry);
        }
        detail->ResetDirect(detail->group->entries.size());
        auto* item = worker.NewItem(detail, group, "寝", 0, 0);
        worker.chain.head = worker.chain.tail = item;
        rules::Program program;
        word::Flusher flusher{worker, program, snapshot->OutputFamilies()};
        word::CleanupSelectors(flusher);
        HD_CHECK_EQ(item->DirectIndex(), expanded ? 129 : 0);
    }
}

HD_TEST(word_nonterminal_boundary_punctuation) {
    for (bool nonterminal : {false, true}) {
        word::Detail detail;
        detail.nonterminalBoundary = nonterminal;
        for (int type : {tokenizer::kTypeAlpha, tokenizer::kTypeWideSpace, tokenizer::kTypeMarkup,
                         tokenizer::kTypeEnd}) {
            detail.nextType = type;
            HD_CHECK_EQ(detail.ClassifyTrailingPunctuation(""), nonterminal ? 0 : 2);
        }
        detail.nextType = tokenizer::kTypeSentenceEnd;
        detail.nextText = "。";
        HD_CHECK_EQ(detail.ClassifyTrailingPunctuation(""), 2);
        detail.nextText = "？";
        HD_CHECK_EQ(detail.ClassifyTrailingPunctuation(""), 3);
        HD_CHECK_EQ(detail.ClassifyTrailingPunctuation(","), 1);
    }
}

HD_TEST(word_imperative_tail_length) {
    for (bool tailLength : {false, true}) {
        word::Record record;
        record.source = "点";
        record.partCode = "M429";
        word::Detail detail;
        detail.patternKind = 9;
        detail.tailLengthCheck = tailLength;
        HD_REQUIRE(word::MatchKind(detail, "しろ", record.source, record));
        HD_CHECK_EQ(detail.kind, tailLength ? 11 : 10);
        detail.patternMatched = 0;
        HD_REQUIRE(word::MatchKind(detail, "し", record.source, record));
        HD_CHECK_EQ(detail.kind, 10);
    }
}

HD_TEST(word_eu_imperative_lookup) {
    const auto snapshot = dict::Load({{0, "plain.tsv", "書けって\t쓰라고\tpat\n"}}).snapshot;
    HD_REQUIRE(snapshot != nullptr);
    for (bool enabled : {false, true}) {
        tokenizer::Tokens tokens;
        word::Arena arena;
        word::Settings settings = word::Settings::Reference();
        settings.improvements.imperativeEu = enabled;
        dict::LookupContext lookup;
        lookup.plain = &snapshot->Plain();
        word::Worker worker(tokens, *snapshot, lookup, settings, arena, {});
        const auto found = worker.DoLookup("って", word::kModeVerbEnding);
        HD_REQUIRE(found.records != nullptr);
        HD_CHECK_EQ(found.records->target, enabled ? "2라고" : "1라고");
    }
}

HD_TEST(word_short_causative_passive_branch) {
    for (bool enabled : {false, true}) {
        word::Record record;
        record.source = "書";
        record.partCode = "M421";
        word::Detail detail;
        detail.patternKind = 1;
        detail.causativePassive = enabled;
        HD_CHECK_EQ(word::MatchKind(detail, "かされた", record.source, record), enabled);
        if (enabled) HD_CHECK_EQ(detail.kind, 8);
    }
}

HD_TEST(word_su_verb_passive_guard) {
    const auto snapshot = dict::Load({{0, "plain.tsv", "剥がす\t벗기다\tv\n驚かす\t놀라게 하다\tv\n"}}).snapshot;
    HD_REQUIRE(snapshot != nullptr);
    tokenizer::Tokens tokens;
    word::Arena arena;
    dict::LookupContext lookup;
    lookup.plain = &snapshot->Plain();
    word::Worker worker(tokens, *snapshot, lookup, word::Settings::Reference(), arena, {});
    HD_CHECK(worker.SuVerb("剥が"));
    HD_CHECK(worker.SuVerb("引き剥が"));
    HD_CHECK(!worker.SuVerb("驚か"));
    HD_CHECK(!worker.SuVerb("が"));
}
