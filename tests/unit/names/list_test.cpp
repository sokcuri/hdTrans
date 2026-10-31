// Name lists: lines, kinds, options and errors.
#include <string>
#include <utility>
#include <vector>

#include "core/names/list.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

// The parsed names as list lines joined with "|".
std::string Lines(const names::List& list) {
    std::string out;
    for (const names::Listed& n : list.names) out += (out.empty() ? "" : "|") + names::ListLine(n);
    return out;
}

}  // namespace

HD_TEST(names_list_parse) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"若葉\t와카바", "若葉\t와카바\tgiven"},
        {"\xEF\xBB\xBF# 주석\n\n若葉\t와카바\r\n", "若葉\t와카바\tgiven"},
        {"黒沢\t쿠로사와\tfamily\n黒沢暁\t쿠로사와 아카츠키\tfull", "黒沢\t쿠로사와\tfamily|黒沢暁\t쿠로사와 아카츠키\tfull"},
        {"みみ\t미미\tnick\thon", "みみ\t미미\tnick\thon"},
        {"恋\t렌\t\thon", "恋\t렌\tgiven\thon"},
        {"星見町\t호시미초\tplace\n星見学園\t호시미 학원\torg\n星見祭\t호시미제\ttitle",
         "星見町\t호시미초\tplace|星見学園\t호시미 학원\torg|星見祭\t호시미제\ttitle"},
        {"若葉\t새잎\n若葉\t와카바", "若葉\t와카바\tgiven"},  // the later line of a name replaces the earlier
        {"若葉\t와카바\t\t\n黒沢\t쿠로사와\tfamily\t\t\t", "若葉\t와카바\tgiven|黒沢\t쿠로사와\tfamily"},  // trailing tabs
        {"\xE3\x80\x80若葉\xE3\x80\x80\t와카바\xE3\x80\x80", "若葉\t와카바\tgiven"},  // full-width spaces around a field
        {"みみ\t미미\tnick\thon  ", "みみ\t미미\tnick\thon"},
    };
    for (const auto& [text, want] : cases) {
        names::List list;
        HD_CHECK(names::ParseList(text, list));
        HD_CHECK_EQ(Lines(list), want);
    }
}

HD_TEST(names_list_errors) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"若葉", "1: a name needs JA and KO"},
        {"若葉\t", "1: a name needs JA and KO"},
        {"# 주석\n若葉\t와카바\tperson", "2: unknown kind person"},
        {"若葉\t와카바\tgiven\tforce", "1: unknown option force"},
        {"若葉\t와카바\tgiven\thon\tx", "1: more than four fields"},
        {"黒沢\xE3\x80\x80若葉\t쿠로사와 와카바\tfull", "1: JA holds white space, | or /"},
        {"黒沢 若葉\t쿠로사와 와카바\tfull", "1: JA holds white space, | or /"},
        {"若葉|わかば\t와카바", "1: JA holds white space, | or /"},
        {"若葉/わかば\t와카바", "1: JA holds white space, | or /"},
        {"星見町\t호시미초\tplace\thon", "1: hon is an option of person names"},
        {"星見祭\t호시미제\ttitle\thon", "1: hon is an option of person names"},
        {"\x8E\xE1\x97\x74\t\xBF\xCD\xC4\xAB\xB9\xD9", "1: invalid UTF-8"},  // a legacy code page
    };
    for (const auto& [text, want] : cases) {
        names::List list;
        std::vector<std::string> errors;
        HD_CHECK(!names::ParseList(text + "\n睦月\t무츠키", list, &errors));
        HD_CHECK(errors.size() == 1 && errors[0] == want);
        HD_CHECK_EQ(Lines(list), std::string("睦月\t무츠키\tgiven"));  // the other lines are kept
    }
    // A bad later line of a name keeps the earlier line.
    names::List list;
    std::vector<std::string> errors;
    HD_CHECK(!names::ParseList("若葉\t와카바\n若葉\t새잎\tperson", list, &errors));
    HD_CHECK(errors.size() == 1 && errors[0] == "2: unknown kind person");
    HD_CHECK_EQ(Lines(list), std::string("若葉\t와카바\tgiven"));
}
