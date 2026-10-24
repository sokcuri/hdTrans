#include "core/userdict/sense.h"

#include <string>

#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::userdict;

namespace {

// Each sense as "key class target side[triggers]".
std::string Show(const SenseTable& table) {
    std::string out;
    for (const auto& [key, list] : table) {
        for (const Sense& s : list) {
            std::string side = "always";
            if (!s.triggers.empty()) {
                side = s.side == Side::Before ? "before" : s.side == Side::After ? "after" : "sentence";
                side += "[";
                for (std::size_t i = 0; i < s.triggers.size(); i++) side += (i ? " " : "") + s.triggers[i];
                side += "]";
            }
            if (s.adjective) side += " adjective";
            out += (out.empty() ? "" : "; ") + s.key + " " + s.classDigit + " " + s.target + " " + side;
        }
    }
    return out;
}

std::string Errors(const std::vector<Error>& errors) {
    std::string s;
    for (const Error& e : errors) s += (s.empty() ? "" : "\n") + std::to_string(e.line) + ": " + e.message;
    return s;
}

struct Case {
    const char* in;
    const char* want;
};

}  // namespace

HD_TEST(userdict_senses) {
    const Case cases[] = {
        {"高い\t높다\t身分,地位\tbefore", "高 7 $D3@ before[身分 地位]"},
        {"まずい\t곤란하다", "まず 7 곤란$AB@ always"},
        {"甘い考え\t안이한 생각", "甘い考え 1 안이한 생각 always"},
        {"気が強い\t기가 세다\t\t", "気が強 7 기가 $AK@ always"},
        {"上げる\t키우다\t腕\tbefore", "上げ 4 키&AI@ before[腕]"},
        {"入る\t들어오다\t情報, 連絡\tbefore\tv5", "入 4 들어&AH@ before[情報 連絡]"},
        {"電気\t불\tつける\tafter", "電気 1 불 after[つける]"},
        {"に\t에게\tsem:VHn@に/gram:Rc\tbefore\tp", "に 2 에게 before[sem:VHn@に/gram:Rc]"},
        {"聞く\t묻다\tgram:Oq@を\tbefore\tv:ㄷ", "聞 4 &J7@ before[gram:Oq@を]"},
        {"薄い\t연하다\tsem:UCr\tafter", "薄 7 연$AB@ after[sem:UCr]"},
        {"一杯\t가득\tclass:verb.filling\tafter", "一杯 1 가득 after[class:verb.filling]"},
        {"いっぱい\t가득\tclass:verb.filling\tafter\tadv", "いっぱい 3 가득 after[class:verb.filling]"},
        {"まずい\t맛이 없다\t食べる\tsentence", "まず 7 맛이 $FA@ sentence[食べる]"},
        {"綺麗\t예쁘다\t花\tafter\tna", "綺麗 8 예%C9@ after[花]"},
        {"// comment\n\n腕\t실력\t上げる\tafter", "腕 1 실력 after[上げる]"},
        {"大事\t중요하다", "大事 1 중요 always adjective"},
        {"出来る\t생기다\t彼女\tbefore", "出来 4 생&A2@ before[彼女]"},
        {"出来ない\t못하다", "出来な 7 못$AB@ always"},
        {"まずい\t큰일이다", "まず 7 큰일$AH@ always"},
        {"腕\t실력\t上げる\tafter\n腕\t팔", "腕 1 실력 after[上げる]; 腕 1 팔 always"},
    };
    for (const Case& c : cases) {
        Families families;
        const CompiledSenses s = ParseSenses(c.in, families);
        HD_CHECK_EQ(Errors(s.errors), std::string());
        HD_CHECK_EQ(Show(s.table), std::string(c.want));
    }
}

HD_TEST(userdict_senses_rows) {
    HD_CHECK_EQ(Sense{}.row, 0);
    const struct {
        const char* text;
        std::vector<int> rows;
    } cases[] = {
        {"腕\t실력\n腕\t팔", {1, 2}},
        {"// comment\n\n腕\t실력\n\n// comment\n腕\t팔", {3, 6}},
        {"\xEF\xBB\xBF腕\t실력\n\n// comment\n腕\t팔", {1, 4}},
        {"\xEF\xBB\xBF// comment\r\n\r\n腕\t실력\r\n\r\n// comment\r\n腕\t팔", {3, 6}},
    };
    for (const auto& c : cases) {
        Families families;
        const CompiledSenses s = ParseSenses(c.text, families);
        HD_CHECK_EQ(Errors(s.errors), std::string());
        HD_REQUIRE(s.table.contains("腕"));
        const auto& senses = s.table.at("腕");
        HD_REQUIRE(senses.size() == c.rows.size());
        for (std::size_t i = 0; i < senses.size(); i++) HD_CHECK_EQ(senses[i].row, c.rows[i]);
    }
}

HD_TEST(userdict_senses_errors) {
    const Case cases[] = {
        {"高い", "1: 1 columns, want 2 to 5"},
        {"高い\t\t身分\tbefore", "1: empty KEY or KR"},
        {"高い\t높다\t身分", "1: SIDE \"\": want before, after or sentence with triggers, empty without"},
        {"高い\t높다\t\tbefore", "1: SIDE \"before\": want before, after or sentence with triggers, empty without"},
        {"高い\t높다\t身分\tleft", "1: SIDE \"left\": want before, after or sentence with triggers, empty without"},
        {"高い\t높다\t身分,\tbefore", "1: empty trigger"},
        {"高い\t높다\t身分\tbefore\tphrase", "1: POS \"phrase\", want n, v, v1, v5, adj, na, adv or p"},
        {"高い\t높다\tsem:VH\tbefore", "1: invalid semantic trigger \"sem:VH\""},
        {"高い\t높다\tclass:no.such-class\tbefore", "1: unknown class trigger \"class:no.such-class\""},
        {"高い\t높다\tgram:Oq@\tbefore", "1: invalid semantic trigger \"gram:Oq@\""},
        {"高い\t높다\tgram:Oq@を\tafter", "1: invalid semantic trigger \"gram:Oq@を\""},
        {"高い\t높다\tsem:VHn@に/gram:R\tbefore", "1: invalid semantic trigger \"sem:VHn@に/gram:R\""},
        {"帰る\t돌아가다\t家\tbefore", "1: る-verb \"帰る\" is ambiguous: write v1 (ichidan) or v5 (godan)"},
        {"高い\t높\t身分\tbefore", "1: KR \"높\" is not a lemma ending in 다"},
        {"a\tb\tc\td\te\tf", "1: 6 columns, want 2 to 5"},
    };
    for (const Case& c : cases) {
        Families families;
        HD_CHECK_EQ(Errors(ParseSenses(c.in, families).errors), std::string(c.want));
    }
}
