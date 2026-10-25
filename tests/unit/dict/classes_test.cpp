#include "core/dict/classes.h"

#include <string>
#include <string_view>
#include <vector>

#include "core/text/utf8.h"
#include "hdtest.h"

using namespace hdTrans::dict;

namespace {

std::string Errors(const internal::ClassTable& t) {
    std::string out;
    for (const internal::ClassError& e : t.errors) out += std::to_string(e.line) + ": " + e.message + "\n";
    return out;
}

const internal::WordClass* Find(const internal::ClassTable& t, std::string_view set) {
    return internal::FindClass(t, set, internal::ClassHash(set));
}

std::string Members(const internal::ClassTable& t, std::string_view set) {
    const internal::WordClass* c = Find(t, set);
    if (c == nullptr) return "-";
    std::string out;
    for (const std::string_view m : c->members) out += std::string(m) + ",";
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
    HD_REQUIRE(t.classes.size() == 2);
    HD_CHECK_EQ(std::string(t.classes[0].name), std::string("particle.case"));
    HD_CHECK_EQ(std::string(t.classes[0].doc), std::string("case particles"));
    HD_CHECK(t.classes[0].index.contains("に"));
    HD_CHECK(!t.classes[0].index.contains("だ"));
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
    HD_CHECK_EQ(Members(t, "empty"), std::string());
}

// A prefix or suffix is the first member in table order, not the longest; a class of single
// characters is read by its runes.
HD_TEST(classes_members_at_ends) {
    const std::vector<std::string_view> lines = {
        "//= follower\tfollowers\n", "follower\tで\n", "follower\tです\n", "follower\tだ\n",
        "//= title\ttitles\n",       "title\t殿下\n",  "title\t殿\n",    "title\t下\n",
        "//= mark\tmarks\n",         "mark\t！\n",     "mark\t?\n",
    };
    const internal::ClassTable t = internal::ParseClasses(lines);
    HD_REQUIRE(Errors(t).empty() && t.classes.size() == 3);
    const internal::WordClass& follower = t.classes[0];
    const internal::WordClass& title = t.classes[1];
    const internal::WordClass& mark = t.classes[2];
    HD_CHECK_EQ(internal::MemberAtStart(follower, "ですね"), std::string_view("で").size());
    HD_CHECK_EQ(internal::MemberAtStart(follower, "だよ"), std::string_view("だ").size());
    HD_CHECK_EQ(internal::MemberAtStart(follower, "よ"), std::size_t{0});
    HD_CHECK_EQ(internal::MemberAtStart(follower, ""), std::size_t{0});
    HD_CHECK_EQ(internal::MemberAtStart(title, "殿下に"), std::string_view("殿下").size());
    HD_CHECK_EQ(internal::MemberAtEnd(title, "王子殿下"), std::string_view("殿下").size());
    HD_CHECK_EQ(internal::MemberAtEnd(title, "閣下"), std::string_view("下").size());
    HD_CHECK_EQ(internal::MemberAtEnd(title, "殿"), std::string_view("殿").size());
    HD_CHECK_EQ(internal::MemberAtEnd(title, "殿様"), std::size_t{0});
    HD_CHECK(mark.runes == std::u32string(U"?！"));
    HD_CHECK(title.runes.empty());
    HD_CHECK(internal::RuneMember(mark, U'！'));
    HD_CHECK(!internal::RuneMember(mark, U'!'));
    HD_CHECK(internal::RuneMember(title, U'殿'));
    HD_CHECK(!internal::RuneMember(title, U'様'));
}

// Every set of the builtin table has a doc line and rows and is found by its name;
// tests/word_classes.cmake checks that the sets the sources name are the sets of the table.
HD_TEST(classes_builtin) {
    const internal::ClassTable& t = internal::BuiltinClasses();
    HD_CHECK_EQ(Errors(t), std::string());
    for (const internal::WordClass& c : t.classes) {
        HD_CHECK(Find(t, c.name) == &c);
        HD_CHECK(!c.members.empty());
        HD_CHECK(!c.doc.empty());
    }
    HD_CHECK(Find(t, "none") == nullptr);
    HD_CHECK_EQ(t.classes.size(), std::size(internal::kClassNames));
    for (const std::string_view n : internal::kClassNames) HD_CHECK(Find(t, n) != nullptr);
    HD_CHECK(InClass("particle.case-before-stem", "を"));
    HD_CHECK(!InClass("particle.case-before-stem", "が"));
    HD_CHECK(InClass(HDTRANS_CLASS("particle.case-before-stem"), "を"));
    const ClassId id = ClassId::Of("particle.case-before-stem");
    HD_CHECK(id.data != nullptr && ClassId::Of("particle.case-before-stem").id == id.id);
    const ClassId none = ClassId::Of("none");
    HD_CHECK(none.data == nullptr && none.id != id.id && !InClass(none, "を") && ClassMembers(none).empty());
    // InClassRune of the character an invalid byte decodes to is no member, as the byte is not.
    for (const internal::WordClass& c : t.classes) HD_CHECK(!c.index.contains("\uFFFD"));
}

// The prefix and suffix of every builtin class against its members in table order, on its
// members and those of the next class, alone and with text before or after them.
HD_TEST(classes_builtin_members_at_ends) {
    const internal::ClassTable& t = internal::BuiltinClasses();
    const auto first = [](const internal::WordClass& c, std::string_view s, bool start) -> std::size_t {
        for (const std::string_view m : c.members) {
            if (start ? s.starts_with(m) : s.ends_with(m)) return m.size();
        }
        return 0;
    };
    int wrong = 0;
    for (std::size_t k = 0; k < t.classes.size(); k++) {
        const internal::WordClass& c = t.classes[k];
        for (const internal::WordClass* from : {&c, &t.classes[(k + 1) % t.classes.size()]}) {
            for (const std::string_view m : from->members) {
                for (const std::string& s : {std::string(m), std::string(m) + "の", "の" + std::string(m), std::string(m.substr(1))}) {
                    wrong += internal::MemberAtStart(c, s) != first(c, s, true);
                    wrong += internal::MemberAtEnd(c, s) != first(c, s, false);
                    const hdTrans::text::Rune r = hdTrans::text::DecodeRune(s);
                    if (r.size == s.size()) wrong += internal::RuneMember(c, r.cp) != c.index.contains(s);
                }
            }
        }
    }
    HD_CHECK_EQ(wrong, 0);
}

// The sets that the passes read as a part of another set.
HD_TEST(classes_builtin_subsets) {
    for (const std::string_view m : ClassMembers(HDTRANS_CLASS("honorific.constraint-familiar"))) {
        HD_CHECK(InClass("honorific.constraint", m));
    }
}
