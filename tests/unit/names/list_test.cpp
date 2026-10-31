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
    };
    for (const auto& [text, want] : cases) {
        names::List list;
        std::vector<std::string> errors;
        HD_CHECK(!names::ParseList(text + "\n睦月\t무츠키", list, &errors));
        HD_CHECK(errors.size() == 1 && errors[0] == want);
        HD_CHECK_EQ(Lines(list), std::string("睦月\t무츠키\tgiven"));  // the other lines are kept
    }
}
