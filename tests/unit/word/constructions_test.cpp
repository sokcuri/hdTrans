#include "core/word/constructions.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/analyze.h"
#include "core/word/flush.h"
#include "core/word/worker.h"
#include "hdtest.h"

using namespace hdTrans::word;

namespace {

std::string Errors(const internal::ConstructionTable& t) {
    std::string out;
    for (const internal::CxError& e : t.errors) out += std::to_string(e.line) + ": " + e.message + "\n";
    return out;
}

constexpr std::string_view kPasses[] = {"First", "Second"};

}  // namespace

HD_TEST(constructions_parse) {
    const std::vector<std::string_view> lines = {
        "// a comment\n",
        "//= de-scene\tafter First 20\tthe place of an action\n",
        "//= later\tafter First 30\truns after de-scene\r\n",
        "cx:de-scene:20\tLEX-9\t&&2 S+0== \"で\" S-1== @noun.scene\tselect-lemma \"에서\"\tnote\n",
        "cx:de-scene:10\t\t&&2 S+0== \"で\" ||2 S-1== \"一瞬\" S-1== \"瞬時\"\tselect-lemma \"에\"\t\n",
        "cx:later:10\t\t&&2 !1 <0 U+0== \"Rば\" ||0 F+1== \"selected\"\tset-ending \"3 정도\" select-lemma@+1 \"(으)로\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.rows.size() == 3 && t.families.size() == 2);
    // Rows run by PRIO, not by table order.
    HD_CHECK(t.families[0].rows == (std::vector<int>{1, 0}));
    HD_CHECK_EQ(t.rows[0].spec, std::string("LEX-9"));
    HD_CHECK(t.afterPass == (std::vector<std::vector<int>>{{0, 1}, {}}));
    HD_REQUIRE(t.rows[2].actions.size() == 2);
    HD_CHECK(t.rows[2].actions[0].verb == internal::CxVerb::SetEnding);
    HD_CHECK_EQ(t.rows[2].actions[0].value, std::string("3 정도"));
    HD_CHECK(t.rows[2].actions[1].verb == internal::CxVerb::SelectLemma);
    HD_CHECK_EQ(static_cast<int>(t.rows[2].actions[1].delta), 1);
    // !1 over a scan of a target and a barrier, then the feature atom.
    const std::vector<internal::CxNode>& n = t.nodes;
    const uint32_t root = t.rows[2].root;
    HD_CHECK(n[root].kind == hdTrans::rules::BodyKind::And && n[root].count == 2);
    HD_CHECK(n[root + 1].kind == hdTrans::rules::BodyKind::Not);
    HD_CHECK(n[root + 2].kind == hdTrans::rules::BodyKind::ScanLeft && n[root + 2].count == 0);
    HD_CHECK(n[root + 3].atom && n[root + 4].kind == hdTrans::rules::BodyKind::Or && n[root + 4].count == 0);
    HD_CHECK(n[root + 5].atom && n[root].end == root + 6);
}

HD_TEST(constructions_parse_maps) {
    const std::vector<std::string_view> lines = {
        "//~ subject\t나=내 저=제\tthe subject form\n",
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\t&&4 C+0== \"가다\" G+0== \"HdCt\" O+1== \"R는\" F-1== \"person\"\tmap-lemma@-1 @subject set-verb \"가다\"\t\n",
        "cx:a:20\t\tO+0== @Rnoun.event\tmap-target @subject\t\n",
        "cx:a:30\t\tF+0== \"selected\"\tselect-map@-1 @subject\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.maps.size() == 1 && t.rows.size() == 3);
    HD_CHECK_EQ(t.maps[0].name, std::string("subject"));
    HD_CHECK(t.maps[0].pairs == (std::vector<std::pair<std::string, std::string>>{{"나", "내"}, {"저", "제"}}));
    HD_REQUIRE(t.rows[0].actions.size() == 2);
    HD_CHECK(t.rows[0].actions[0].verb == internal::CxVerb::MapLemma && t.rows[0].actions[0].map == 0);
    HD_CHECK_EQ(static_cast<int>(t.rows[0].actions[0].delta), -1);
    HD_CHECK(t.rows[0].actions[1].verb == internal::CxVerb::SetVerb);
    HD_CHECK_EQ(t.rows[0].actions[1].value, std::string("가다"));
    HD_CHECK(t.rows[1].actions[0].verb == internal::CxVerb::MapTarget);
    HD_CHECK(t.rows[2].actions[0].verb == internal::CxVerb::SelectMap && t.rows[2].actions[0].map == 0);
}

HD_TEST(constructions_parse_select_and_tail) {
    const std::vector<std::string_view> lines = {
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\t&&2 T+0== \"L、\" F+0== \"final-consonant\"\tselect-lemma \"L(이)가 아니\" select-target@-1 \"L갈 수\"\t\n",
        "cx:a:20\t\tT+0== @Lparticle.contrast\treplace-ending-tail \" 것이다\" \" 것이\"\t\n",
        "cx:a:30\t\tF+0== \"selected\"\treplace-ending-tail \"던\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string("4: actions: record ends early\n"));
    HD_REQUIRE(t.rows.size() == 2);
    HD_REQUIRE(t.rows[0].actions.size() == 2);
    HD_CHECK(t.rows[0].actions[0].verb == internal::CxVerb::SelectLemma);
    HD_CHECK_EQ(t.rows[0].actions[0].value, std::string("L(이)가 아니"));
    HD_CHECK(t.rows[0].actions[1].verb == internal::CxVerb::SelectTarget);
    HD_CHECK_EQ(static_cast<int>(t.rows[0].actions[1].delta), -1);
    HD_CHECK(t.atoms[t.nodes[t.rows[0].root + 1].count].field == internal::CxField::Tail);
    HD_REQUIRE(t.rows[1].actions.size() == 1);
    HD_CHECK(t.rows[1].actions[0].verb == internal::CxVerb::ReplaceEndingTail);
    HD_CHECK_EQ(t.rows[1].actions[0].value, std::string(" 것이다"));
    HD_CHECK_EQ(t.rows[1].actions[0].with, std::string(" 것이"));
}

HD_TEST(constructions_parse_set_sound) {
    const std::vector<std::string_view> lines = {
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\tF+0== \"katakana\"\tset-sound select-lemma@+1 \"(와)과\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.rows.size() == 1);
    HD_REQUIRE(t.rows[0].actions.size() == 2);
    HD_CHECK(t.rows[0].actions[0].verb == internal::CxVerb::SetSound);
    HD_CHECK(t.rows[0].actions[0].value.empty());
    HD_CHECK(t.rows[0].actions[1].verb == internal::CxVerb::SelectLemma);
    HD_CHECK_EQ(static_cast<int>(t.rows[0].actions[1].delta), 1);
}

HD_TEST(constructions_parse_append_target) {
    const std::vector<std::string_view> lines = {
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\tS+0== \"もう\"\tset-target \"\" append-target@+1 \" 더\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.rows.size() == 1);
    HD_REQUIRE(t.rows[0].actions.size() == 2);
    HD_CHECK(t.rows[0].actions[0].verb == internal::CxVerb::SetTarget);
    HD_CHECK(t.rows[0].actions[1].verb == internal::CxVerb::AppendTarget);
    HD_CHECK_EQ(t.rows[0].actions[1].value, std::string(" 더"));
    HD_CHECK_EQ(static_cast<int>(t.rows[0].actions[1].delta), 1);
}

HD_TEST(constructions_parse_prepend_and_verb_from) {
    const std::vector<std::string_view> lines = {
        "//~ tails\t있는=있으면서 버리는=버리면서\tdoc\n",
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\tF+0== \"causative-passive\"\tset-verb-from \"게 &C6@\" \"다\" prepend-target \"억지로 \"\t\n",
        "cx:a:20\t\tS+0== \"くせに\"\tmap-ending-tail@-1 @tails\t\n",
        "cx:a:30\t\tS+0== \"から\"\tmap-target-tail@-1 @tails\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.rows.size() == 3);
    HD_REQUIRE(t.rows[2].actions.size() == 1);
    HD_CHECK(t.rows[2].actions[0].verb == internal::CxVerb::MapTargetTail);
    HD_CHECK_EQ(t.rows[2].actions[0].map, 0);
    HD_REQUIRE(t.rows[0].actions.size() == 2);
    HD_CHECK(t.rows[0].actions[0].verb == internal::CxVerb::SetVerbFrom);
    HD_CHECK_EQ(t.rows[0].actions[0].value, std::string("게 &C6@"));
    HD_CHECK_EQ(t.rows[0].actions[0].with, std::string("다"));
    HD_CHECK(t.rows[0].actions[1].verb == internal::CxVerb::PrependTarget);
    HD_CHECK_EQ(t.rows[0].actions[1].value, std::string("억지로 "));
    HD_REQUIRE(t.rows[1].actions.size() == 1);
    HD_CHECK(t.rows[1].actions[0].verb == internal::CxVerb::MapEndingTail);
    HD_CHECK_EQ(t.rows[1].actions[0].map, 0);
    HD_CHECK_EQ(static_cast<int>(t.rows[1].actions[0].delta), -1);
    const std::vector<std::string_view> bad = {
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\tF+0== \"causative-passive\"\tset-verb-from \"게 &C6@\"\t\n",
    };
    HD_CHECK(!internal::ParseConstructions(bad, kPasses).errors.empty());
}

HD_TEST(constructions_parse_named_conditions) {
    const std::vector<std::string_view> lines = {
        "//: particle\t||2 S+0== \"に\" S+0== \"へ\"\ta goal particle\n",
        "//: goal-before\t&&2 :particle F-1== \"selected\"\tone after a word\n",
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\t&&2 :goal-before S+1== \":particle\"\tselect-lemma \"에\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.rows.size() == 1);
    // The names expand in place: && of (&& of (|| of two atoms) and an atom) and an atom whose
    // quoted value keeps its text.
    const std::vector<internal::CxNode>& n = t.nodes;
    const uint32_t root = t.rows[0].root;
    HD_CHECK(n[root].kind == hdTrans::rules::BodyKind::And && n[root].count == 2);
    HD_CHECK(n[root + 1].kind == hdTrans::rules::BodyKind::And && n[root + 1].count == 2);
    HD_CHECK(n[root + 2].kind == hdTrans::rules::BodyKind::Or && n[root + 2].count == 2);
    HD_REQUIRE(t.atoms.size() == 4);
    HD_CHECK_EQ(t.atoms.back().value, std::string(":particle"));
    const std::vector<std::string_view> bad = {
        "//: two\tS+0== \"に\" S+0== \"へ\"\ttwo expressions\n",
        "//: twice\tS+0== \"に\"\tdoc\n",
        "//: twice\tS+0== \"へ\"\tdoc\n",
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\t:missing\tselect-lemma \"에\"\t\n",
    };
    const std::string errors = Errors(internal::ParseConstructions(bad, kPasses));
    HD_CHECK(errors.find("1: condition: ") != std::string::npos);
    HD_CHECK(errors.find("3: a second line for the condition twice") != std::string::npos);
    HD_CHECK(errors.find("5: condition: no named condition :missing") != std::string::npos);
}

HD_TEST(constructions_surface_index) {
    const std::vector<std::string_view> lines = {
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\t&&2 F+0== \"selected\" ||2 S+0== \"に\" S+0== \"99には\"\tselect-lemma \"에\"\t\n",
        "cx:a:20\t\t&&2 S+0== \"Lから\" F+0== \"selected\"\tselect-lemma \"에서\"\t\n",
        "cx:a:30\t\t&&3 S+0== \"に\" S+0!= \"で\" S+1== \"の\"\tselect-lemma \"의\"\t\n",
        "cx:a:40\t\t||2 S+0== \"で\" F+0== \"selected\"\tselect-lemma \"\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.families.size() == 1);
    const internal::CxFamily& c = t.families[0];
    HD_CHECK(c.indexed);
    // A prefix, a negated atom and a || with an operand that needs no surface give no key.
    HD_CHECK(c.open == (std::vector<int>{1, 3}));
    HD_REQUIRE(c.bySurface.size() == 2);
    HD_CHECK(c.bySurface.find(std::string_view("に"))->second == (std::vector<int>{0, 1, 2, 3}));
    HD_CHECK(c.bySurface.find(std::string_view("には"))->second == (std::vector<int>{0, 1, 3}));
}

HD_TEST(constructions_parse_map_errors) {
    const std::vector<std::string_view> lines = {
        "//~ m\t나=내\tdoc\n",
        "//~ m\t너=네\tagain\n",
        "//~ bad\t나내\tno equals sign\n",
        "//~ bad\t나=내 나=제\ta key twice\n",
        "//~ bad\t=내\tno key\n",
        "//~ bad\t나=\tno value\n",
        "//~ bad\t나=내  저=제\ttwo spaces\n",
        "//~ Bad\t나=내\tname\n",
        "//~ bad\t나=내\n",
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\tS+0== \"で\"\tmap-lemma @none\t\n",
        "cx:a:20\t\tS+0== \"で\"\tmap-lemma \"나\"\t\n",
        "cx:a:30\t\tG+0== \"Hdc\"\tselect-lemma \"에\"\t\n",
        "cx:a:40\t\tG+0== @noun.event\tselect-lemma \"에\"\t\n",
        "cx:a:50\t\tS+0== \"で\"\tset-verb \"가다\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    const std::string bad = ": a map line is //~ MAP <TAB> FROM=TO ... <TAB> what it is for, each FROM once\n";
    HD_CHECK_EQ(Errors(t), "2: a second line for the map m\n3" + bad + "4" + bad + "5" + bad + "6" + bad + "7" + bad +
                               "8" + bad + "9" + bad +
                               "11: actions: no such map\n"
                               "12: actions: a map action takes @MAP\n"
                               "13: condition: G takes two-letter codes\n"
                               "14: condition: this field takes no word class\n");
}

HD_TEST(constructions_parse_errors) {
    const std::vector<std::string_view> lines = {
        "//= a\tafter First 10\tdoc\n",
        "//= a\tafter Second 10\tagain\n",
        "//= b\tafter Nowhere 10\tdoc\n",
        "//= c\tafter First 10\tone stage twice\n",
        "//= d\tafter e 10\tafter a family\n",
        "//= e\tafter First\tno stage\n",
        "//= empty\tafter Second 10\tno rows\n",
        "cx:a:10\t\tS+0== \"で\"\tselect-lemma \"에\"\n",
        "cx:a\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:z:10\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:a:10\t\tQ+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:a:20\t\tS+0== @no.such-class\tselect-lemma \"에\"\t\n",
        "cx:a:30\t\tF+0== \"no-such-feature\"\tselect-lemma \"에\"\t\n",
        "cx:a:40\t\tS+0-= \"で\"\tselect-lemma \"에\"\t\n",
        "cx:a:50\t\tS+0== \"で\"\tpick \"에\"\t\n",
        "cx:a:60\t\tS+0== \"で\" S+1== \"に\"\tselect-lemma \"에\"\t\n",
        "cx:a:70\t\t!2 S+0== \"で\" S+1== \"に\"\tselect-lemma \"에\"\t\n",
        "cx:a:80\t\tS+0== \"で\"\t\t\n",
        "cx:a:90\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:a:090\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:a:90\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:b:10\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:c:10\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:d:10\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
        "cx:e:10\t\tS+0== \"で\"\tselect-lemma \"에\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string("2: a second line for the family a\n"
                                       "6: a family line is //= FAMILY <TAB> after PASS STAGE [<TAB> guard CONDITION] <TAB> what it is for\n"
                                       "8: a row is ID <TAB> SPEC <TAB> CONDITION <TAB> ACTIONS <TAB> NOTE\n"
                                       "9: an id is cx:FAMILY:PRIO\n"
                                       "10: no family line for cx:z:10\n"
                                       "11: condition: unknown field\n"
                                       "12: condition: no such word class\n"
                                       "13: condition: no such feature\n"
                                       "14: condition: unknown operator\n"
                                       "15: actions: unknown action\n"
                                       "16: condition: trailing tokens\n"
                                       "17: condition: ! takes one operand: !1\n"
                                       "18: a row is ID <TAB> SPEC <TAB> CONDITION <TAB> ACTIONS <TAB> NOTE\n"
                                       "21: a second row cx:a:90\n"
                                       "25: no family line for cx:e:10\n"
                                       "20: two rows of a share a PRIO\n"
                                       "3: the family b runs after Nowhere, which is no pass\n"
                                       "5: the family d runs after e, which is no pass\n"
                                       "7: no rows for the family empty\n"
                                       "4: two families run after First at stage 10; give one another stage\n"));
}

HD_TEST(constructions_builtin_table) {
    const internal::ConstructionTable& t = internal::BuiltinConstructions();
    HD_CHECK_EQ(Errors(t), std::string());
    HD_CHECK(!t.rows.empty());
    HD_CHECK(t.afterPass.size() == TextPassNames().size());
    for (std::size_t i = 0; i < t.rows.size(); i++) {
        HD_CHECK_EQ(std::string(ConstructionId(static_cast<int>(i))), t.rows[i].id);
    }
    HD_CHECK(ConstructionId(-1).empty() && ConstructionId(static_cast<int>(t.rows.size())).empty());
}

namespace {

// The target of 人気 in 人気。 after the word stage and then the family of table, which runs after
// pass First.
std::string GuardedTarget(const std::vector<std::string_view>& table) {
    const internal::ConstructionTable t = internal::ParseConstructions(table, kPasses);
    if (!t.errors.empty()) return "(table: " + Errors(t) + ")";
    std::vector<hdTrans::dict::File> files{
        {0, "base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n人気\t인기\tN110\t\t\tGN\t0\t1\t\n"}};
    const hdTrans::dict::LoadResult loaded = hdTrans::dict::Load(std::move(files));
    if (loaded.snapshot == nullptr) return "(no dictionary)";
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    hdTrans::tokenizer::Tokens tokens;
    Arena arena;
    Analysis analysis;
    hdTrans::tokenizer::Tokenize("人気。", tokens);
    Analyze(tokens, *loaded.snapshot, {}, Settings::Reference(), arena, analysis);
    if (analysis.fault || analysis.sentences.empty()) return "(no analysis)";
    Worker& w = *analysis.worker;
    w.chain = analysis.sentences[0].chain;
    w.current = analysis.sentences[0].current;
    w.transformSurface = analysis.sentences[0].transformSurface;
    Flusher f{w, generated->rules, loaded.snapshot->OutputFamilies()};
    Flush(f);
    internal::RunConstructions(f, t, 0);
    for (Item* it = w.chain.head; it != nullptr; it = it->next) {
        if (it->group->surface == "人気") return std::string(it->EntryText(it->selected));
    }
    return "(no item)";
}

// The target of 人気 in text after a row that writes X where the questions of the word's speech are 해체,
// with the quote styles of the line and the level and voice the speech-level stage decides for it.
std::string CasualLevelTarget(std::string_view text, std::vector<std::uint8_t> styles,
                              Settings::LineLevel level = Settings::LineLevel::none, bool authoritative = false,
                              bool prose = false, std::string_view feature = "casual-level",
                              std::vector<Settings::LineLevel> quotes = {}) {
    const std::string row = "cx:a:10\t\tF+0== \"" + std::string(feature) + "\"\tset-target \"X\"\t\n";
    const std::vector<std::string_view> table = {"//= a\tafter First 10\tdoc\n", row};
    const internal::ConstructionTable t = internal::ParseConstructions(table, kPasses);
    if (!t.errors.empty()) return "(table: " + Errors(t) + ")";
    std::vector<hdTrans::dict::File> files{
        {0, "base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n人気\t인기\tN110\t\t\tGN\t0\t1\t\n"}};
    const hdTrans::dict::LoadResult loaded = hdTrans::dict::Load(std::move(files));
    if (loaded.snapshot == nullptr) return "(no dictionary)";
    const auto generated = hdTrans::internal::NewGenerated(*loaded.snapshot);
    hdTrans::tokenizer::Tokens tokens;
    Arena arena;
    Analysis analysis;
    hdTrans::tokenizer::Tokenize(text, tokens);
    Settings settings = Settings::Reference();
    settings.quoteStyles = styles;
    settings.lineLevel = level;
    settings.authoritative = authoritative;
    settings.prose = prose;
    settings.quoteLevels = quotes;
    Analyze(tokens, *loaded.snapshot, {}, settings, arena, analysis);
    if (analysis.fault || analysis.sentences.empty()) return "(no analysis)";
    for (const Sentence& sentence : analysis.sentences) {
        Worker& w = *analysis.worker;
        w.chain = sentence.chain;
        w.current = sentence.current;
        w.transformSurface = sentence.transformSurface;
        Flusher f{w, generated->rules, loaded.snapshot->OutputFamilies()};
        Flush(f);
        internal::RunConstructions(f, t, 0);
        for (Item* it = w.chain.head; it != nullptr; it = it->next) {
            if (it->group->surface == "人気") return std::string(it->EntryText(it->selected));
        }
    }
    return "(no item)";
}

}  // namespace

// casual-level reads the level the speech-level stage decides for the line and its voice; without one,
// the style of the quote the word is in.
HD_TEST(constructions_casual_level) {
    using L = Settings::LineLevel;
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 0}), std::string("X"));
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 32}), std::string("X"));  // marked casual
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 1}), std::string("인기"));  // polite
    HD_CHECK_EQ(CasualLevelTarget("「あ」「人気」", {0, 1, 0}), std::string("X"));
    HD_CHECK_EQ(CasualLevelTarget("「あ」「人気」", {0, 0, 2}), std::string("인기"));  // old speech
    HD_CHECK_EQ(CasualLevelTarget("人気。", {0}), std::string("인기"));  // outside quotes
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {}), std::string("인기"));  // no styles: reference path
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 0}, L::casual), std::string("X"));
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 0}, L::plain), std::string("인기"));
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 1}, L::casual), std::string("X"));  // the stage decides
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 0}, L::casual, true), std::string("인기"));  // REG-14
    HD_CHECK_EQ(CasualLevelTarget("人気。", {0}, L::casual), std::string("X"));  // a line the stage writes
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 0}, L::casual, false, true, "prose"), std::string("X"));
    // REG-15: the level of the word's own quote on a line of several quotes.
    HD_CHECK_EQ(CasualLevelTarget("「あ」「人気」", {0, 0, 0}, L::casual, false, false, "casual-level", {L::casual, L::polite}),
                std::string("인기"));
    HD_CHECK_EQ(CasualLevelTarget("「人気」「あ」", {0, 0, 0}, L::polite, false, false, "casual-level", {L::casual, L::polite}),
                std::string("X"));
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 0}, L::none, false, true, "prose"), std::string("인기"));
    HD_CHECK_EQ(CasualLevelTarget("「人気」", {0, 0}, L::casual, false, false, "prose"), std::string("인기"));
}

// A family guard is a condition tried on each word before the family's rows: a false guard skips
// every row, a true one leaves the rows as they are without a guard.
HD_TEST(constructions_family_guard) {
    const std::string_view row = "cx:a:10\t\tS+0== \"人気\"\tset-target \"X\"\t\n";
    HD_CHECK_EQ(GuardedTarget({"//= a\tafter First 10\tdoc\n", row}), std::string("X"));
    HD_CHECK_EQ(GuardedTarget({"//= a\tafter First 10\tguard S+0== \"人気\"\tdoc\n", row}), std::string("X"));
    HD_CHECK_EQ(GuardedTarget({"//= a\tafter First 10\tguard S+0== \"犬\"\tdoc\n", row}), std::string("인기"));
    HD_CHECK_EQ(GuardedTarget({"//: n\tS+0== \"犬\"\tdoc\n", "//= a\tafter First 10\tguard :n\tdoc\n", row}),
                std::string("인기"));
    // A guard that names surfaces skips the other words by surface; one that names none is tried on each.
    const std::vector<std::string_view> keyed = {
        "//= a\tafter First 10\tguard ||2 S+0== \"人気\" S+0== \"犬\"\tdoc\n",
        "//= b\tafter First 20\tguard U+0== \"ます\"\tdoc\n",
        "cx:a:10\t\tF+0== \"selected\"\tset-target \"X\"\t\n",
        "cx:b:10\t\tF+0== \"selected\"\tset-target \"X\"\t\n",
    };
    const internal::ConstructionTable k = internal::ParseConstructions(keyed, kPasses);
    HD_CHECK_EQ(Errors(k), std::string());
    HD_REQUIRE(k.families.size() == 2);
    HD_CHECK(k.families[0].guardKeyed && k.families[0].guardSurfaces.size() == 2);
    HD_CHECK(!k.families[1].guardKeyed && k.families[1].guard != internal::kNoGuard);
    const std::vector<std::string_view> bad = {
        "//= a\tafter First 10\tguard S+0= \"人気\"\tdoc\n",
        "//= b\tafter First 20\tguard :missing\tdoc\n",
        "//= c\tafter First 30\tS+0== \"人気\"\tdoc\n",
        "cx:a:10\t\tS+0== \"人気\"\tset-target \"X\"\t\n",
    };
    const std::string errors = Errors(internal::ParseConstructions(bad, kPasses));
    HD_CHECK(errors.find("1: guard: unknown operator") != std::string::npos);
    HD_CHECK(errors.find("2: guard: no named condition :missing") != std::string::npos);
    HD_CHECK(errors.find("3: a family line is //= FAMILY <TAB> after PASS STAGE [<TAB> guard CONDITION]") != std::string::npos);
}

// @scan±n counts from the word where a scan of the row's condition found its target.
HD_TEST(constructions_scan_positions) {
    const std::vector<std::string_view> lines = {
        "//~ m\t저=제\tdoc\n",
        "//= a\tafter First 10\tdoc\n",
        "cx:a:10\t\t<4 S+0== \"に\" ||0\tset-target@scan \"(이)가\" map-lemma@scan-1 @m set-ending \"1\"\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.rows.size() == 1 && t.rows[0].actions.size() == 3);
    HD_CHECK(t.rows[0].actions[0].scanned && t.rows[0].actions[0].delta == 0);
    HD_CHECK(t.rows[0].actions[1].scanned && t.rows[0].actions[1].delta == -1);
    HD_CHECK(!t.rows[0].actions[2].scanned);
    const std::vector<std::string_view> bad = {"//= a\tafter First 10\tdoc\n", "cx:a:10\t\tS+0== \"に\"\tset-target@ \"X\"\t\n"};
    HD_CHECK(Errors(internal::ParseConstructions(bad, kPasses)).find("bad action position") != std::string::npos);
}
