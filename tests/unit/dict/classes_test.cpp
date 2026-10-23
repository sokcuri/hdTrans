#include "core/dict/classes.h"

#include <string>
#include <string_view>
#include <vector>

#include "hdtest.h"

using namespace hdTrans::dict;

namespace {

std::string Errors(const internal::ClassTable& t) {
    std::string out;
    for (const internal::ClassError& e : t.errors) out += std::to_string(e.line) + ": " + e.message + "\n";
    return out;
}

std::string Members(const internal::ClassTable& t, std::string_view set) {
    std::string out;
    const auto found = t.sets.find(set);
    if (found == t.sets.end()) return "-";
    for (const std::string_view m : found->second.members) out += std::string(m) + ",";
    return out;
}

}  // namespace

HD_TEST(classes_parse) {
    const std::vector<std::string_view> lines = {
        "// a comment\n", "//= particle.case\tcase particles\n", "//= copula.start\tcopula starts\r\n", "\n",
        "particle.case\tを\n", "particle.case\tに\tthe goal\n", "copula.start\tだ\r\n", "particle.case\tへ",
    };
    const internal::ClassTable t = internal::ParseClasses(lines);
    HD_CHECK_EQ(Errors(t), std::string());
    HD_CHECK_EQ(Members(t, "particle.case"), std::string("を,に,へ,"));
    HD_CHECK_EQ(Members(t, "copula.start"), std::string("だ,"));
    HD_CHECK_EQ(Members(t, "none"), std::string("-"));
    HD_CHECK_EQ(std::string(t.sets.at("particle.case").doc), std::string("case particles"));
    HD_CHECK_EQ(t.order.size(), std::size_t{2});
    HD_CHECK(t.sets.at("particle.case").index.contains("に"));
    HD_CHECK(!t.sets.at("particle.case").index.contains("だ"));
}

HD_TEST(classes_parse_errors) {
    const std::vector<std::string_view> lines = {
        "//= a\tfirst\n",  "//= a\tagain\n", "//= empty\tno rows\n", "//= \tno name\n", "a\tx\n", "a\tx\n",
        "b\ty\n",          "a\t\n",          "a\tz \n",              "a\t 든다\n",
    };
    const internal::ClassTable t = internal::ParseClasses(lines);
    HD_CHECK_EQ(Errors(t), std::string("2: a second doc line for a\n"
                                       "4: a doc line needs a set name and a text\n"
                                       "6: a second row of x in a\n"
                                       "7: no doc line for the set b\n"
                                       "8: a row is SET <TAB> MEMBER, the member neither empty nor ending in a space\n"
                                       "9: a row is SET <TAB> MEMBER, the member neither empty nor ending in a space\n"
                                       "0: no rows for the set empty\n"));
    HD_CHECK_EQ(Members(t, "a"), std::string("x, 든다,"));
}

// Every set of the builtin table has a doc line and rows; tests/word_classes.cmake checks that
// the sets the sources name are the sets of the table.
HD_TEST(classes_builtin) {
    const internal::ClassTable& t = internal::BuiltinClasses();
    HD_CHECK_EQ(Errors(t), std::string());
    for (const std::string_view set : t.order) {
        const std::span<const std::string_view> members = ClassMembers(set);
        HD_CHECK(!members.empty());
        for (const std::string_view m : members) HD_CHECK(InClass(set, m));
    }
    HD_CHECK(!InClass("none", ""));
    HD_CHECK(ClassMembers("none").empty());
}
