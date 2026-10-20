#pragma once
// A fixture dictionary whose rows name local rules carries those bodies, as a dictionary
// file does: a rule body lives in the row that names it, in the ctx column.
#include <string>
#include <string_view>

namespace fixture {

// The body of a local rule a fixture names.
inline std::string_view RuleBody(int rule) {
    switch (rule) {
    case 1: return R"(||1 &&2 S+1== "あら" P+1== "3**")";
    case 2: return R"(||1 G+0== "Ow")";
    case 9: return R"(||1 S+1== "、")";
    case 22: return R"(||1 &&5 P-1== "4**" M-1== "50*" P+1== "2**" D+0= "기회" Y-1= "3")";
    case 327: return R"(||2 A-1-= "VHn" &&2 P-1== "13*" G-1== "WaWb")";
    case 1062: return R"(||4 &&2 ||2 S+1== "日付" S+1== "日付で" D+0= "월" &&6 G-1!= "NjNd" S+1== "に" P+1== "2**" P+2== "1**" G+2== "NjNdCt" D+0= "한달" S-1== "翌々" &&2 G+0== "Hz" S+1== "も")";
    case 1063: return R"(||7 &&3 G-1== "NjNd" S+1== "日付" P+1== "1**" G+0== "Td" &&3 P-1== "2**" G+1== "NjNd" G+2== "Ct" &&4 S+1== "～" P+2== "1**" A+2== "UMe" G+2== "On" &&2 A-1== "UMe" ||2 G-1== "Td" G-1== "On" &&2 A+1== "UMe" ||2 G+1== "Td" G+1== "On" &&2 P-1== "1**" G-1== "NjNd")";
    case 1114: return R"(||1 &&3 D-1== "," D-2== "한국" D+0= "일본")";
    case 1115: return R"(||2 G-1== "NjNdTd" G+0== "Td")";
    case 1901: return R"(||1 &&2 P+1== "1**" G+1== "NjNdCt")";
    case 3043: return R"(||3 G+1== "Ct" S-1== "○" S+1== "○")";
    }
    return {};
}

// rows is a column file without the ctx column; the header gains one and each row that
// names a rule carries that rule's body.
inline std::string WithRuleBodies(std::string_view rows) {
    std::string out;
    std::size_t start = 0;
    while (start < rows.size()) {
        std::size_t end = rows.find('\n', start);
        if (end == std::string_view::npos) end = rows.size();
        const std::string_view line = rows.substr(start, end - start);
        start = end + 1;
        if (line.empty()) continue;
        if (line.starts_with("key")) {
            out += "key\tko\tpos\tsem\tgram\tdict\trule\tid\tctx\n";
            continue;
        }
        std::string_view rule;
        std::size_t at = 0;
        for (int field = 0; field < 7 && at <= line.size(); field++) {
            const std::size_t tab = line.find('\t', at);
            rule = line.substr(at, tab == std::string_view::npos ? line.size() - at : tab - at);
            if (tab == std::string_view::npos) break;
            at = tab + 1;
        }
        std::string kind(rule);
        const std::string_view body = kind.empty() || kind == "0" ? std::string_view{} : RuleBody(std::stoi(kind));
        out += std::string(line) + "\t" + std::string(body) + "\n";
    }
    return out;
}

}  // namespace fixture
