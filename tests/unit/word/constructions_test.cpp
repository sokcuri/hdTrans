#include "core/word/constructions.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/word/flush.h"
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
        "//= de-scene\tafter First\tthe place of an action\n",
        "//= later\tafter de-scene\truns after de-scene\r\n",
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
    HD_CHECK(t.afterPass == (std::vector<int>{0, -1}));
    HD_CHECK_EQ(t.families[0].next, 1);
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
        "//= a\tafter First\tdoc\n",
        "cx:a:10\t\t&&4 C+0== \"가다\" G+0== \"HdCt\" O+1== \"R는\" F-1== \"person\"\tmap-lemma@-1 @subject set-verb \"가다\"\t\n",
        "cx:a:20\t\tO+0== @Rnoun.event\tmap-target @subject\t\n",
    };
    const internal::ConstructionTable t = internal::ParseConstructions(lines, kPasses);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_REQUIRE(t.maps.size() == 1 && t.rows.size() == 2);
    HD_CHECK_EQ(t.maps[0].name, std::string("subject"));
    HD_CHECK(t.maps[0].pairs == (std::vector<std::pair<std::string, std::string>>{{"나", "내"}, {"저", "제"}}));
    HD_REQUIRE(t.rows[0].actions.size() == 2);
    HD_CHECK(t.rows[0].actions[0].verb == internal::CxVerb::MapLemma && t.rows[0].actions[0].map == 0);
    HD_CHECK_EQ(static_cast<int>(t.rows[0].actions[0].delta), -1);
    HD_CHECK(t.rows[0].actions[1].verb == internal::CxVerb::SetVerb);
    HD_CHECK_EQ(t.rows[0].actions[1].value, std::string("가다"));
    HD_CHECK(t.rows[1].actions[0].verb == internal::CxVerb::MapTarget);
}

HD_TEST(constructions_surface_index) {
    const std::vector<std::string_view> lines = {
        "//= a\tafter First\tdoc\n",
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
        "//= a\tafter First\tdoc\n",
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
        "//= a\tafter First\tdoc\n",
        "//= a\tafter Second\tagain\n",
        "//= b\tafter Nowhere\tdoc\n",
        "//= c\tafter First\ttwo after one name\n",
        "//= d\tafter e\ta cycle\n",
        "//= e\tafter d\ta cycle\n",
        "//= empty\tafter Second\tno rows\n",
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
                                       "20: two rows of a share a PRIO\n"
                                       "3: the family b runs after Nowhere, which is no pass or family\n"
                                       "4: two families run after First; order them with after\n"
                                       "7: no rows for the family empty\n"
                                       "5: the family d is not reached from a pass\n"
                                       "6: the family e is not reached from a pass\n"));
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
