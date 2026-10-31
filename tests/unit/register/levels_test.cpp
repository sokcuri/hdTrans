// The speech level of each line by the REG rules (levels.cases.tsv): a fix case is decided by the rule it
// names, a keep case is near the rule and is not; every rule of levels.tsv has both, and a heading in
// spec/register.md.
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/register/levels.h"
#include "core/register/tables.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::string RepoDir() {
    std::string f = __FILE__;
    for (int i = 0; i < 4; i++) {
        const std::size_t slash = f.find_last_of("/\\");
        if (slash == std::string::npos) return ".";
        f.resize(slash);
    }
    return f;
}

bool Read(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    std::fclose(f);
    return true;
}

// The rows of a TSV file after its header, by column name.
std::vector<std::map<std::string, std::string>> Rows(const std::string& text) {
    std::vector<std::map<std::string, std::string>> out;
    std::vector<std::string> header;
    for (const std::string& line : reg::Split(text, '\n')) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> cols = reg::Split(line, '\t');
        if (header.empty()) {
            header = cols;
            continue;
        }
        std::map<std::string, std::string> row;
        for (std::size_t i = 0; i < header.size(); i++) row[header[i]] = i < cols.size() ? cols[i] : "";
        out.push_back(row);
    }
    return out;
}

}  // namespace

HD_TEST(register_tables_load) {
    HD_CHECK(reg::LoadedData() != nullptr);
    HD_CHECK_EQ(reg::DataError(), std::string());
}

HD_TEST(register_level_cases) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    std::string text;
    HD_REQUIRE(Read(RepoDir() + "/tests/unit/register/levels.cases.tsv", text));
    const auto rows = Rows(text);
    HD_REQUIRE(!rows.empty());
    std::vector<reg::RegLine> lines;
    std::map<std::string, uint32_t> scenes;
    for (const auto& r : rows) {
        reg::RegLine l;
        l.ja = r.at("ja");
        l.speaker = r.at("speaker");
        l.dialogue = r.at("dialogue") == "yes";
        l.scene = scenes.emplace(r.at("doc"), static_cast<uint32_t>(scenes.size() + 1)).first->second;
        lines.push_back(l);
    }
    // one document per run of rows with the same doc id (REG-20 reads the whole document)
    std::vector<reg::RegDecision> ds;
    for (std::size_t a = 0; a < lines.size();) {
        std::size_t b = a;
        while (b < lines.size() && rows[b].at("doc") == rows[a].at("doc")) ++b;
        const std::vector<reg::RegLine> part(lines.begin() + static_cast<std::ptrdiff_t>(a),
                                             lines.begin() + static_cast<std::ptrdiff_t>(b));
        const std::vector<reg::RegDecision> pd = reg::DecideLevels(d->levels, part);
        ds.insert(ds.end(), pd.begin(), pd.end());
        a = b;
    }
    std::map<std::string, std::pair<int, int>> fixKeep;  // spec → fix, keep
    for (std::size_t k = 0; k < rows.size(); k++) {
        const auto& r = rows[k];
        if (r.at("level") == "-") continue;
        const std::string& spec = r.at("spec");
        const bool fix = r.at("case") == "fix";
        HD_CHECK(fix || r.at("case") == "keep");
        (fix ? fixKeep[spec].first : fixKeep[spec].second)++;
        const std::string got = reg::Name(ds[k].level);
        // REG-16 is no level rule: its cases check the -시- only
        const bool specOk = spec == "REG-16" || (fix ? ds[k].spec == spec : ds[k].spec != spec);
        // hon: whether the line's final predicate takes the addressee -시-, the predicate standing in as one V
        // node: 주다 when the addressee gives, else 가다; its mood from the source
        bool honOk = true;
        std::string honGot;
        if (!r.at("hon").empty() && r.at("hon") != "-") {
            const std::string& ja = r.at("ja");
            reg::Node v;
            v.id = 1;
            v.kind = reg::Kind::V;
            v.ctype = reg::CType::final_;
            const bool give = ja.find("くれ") != std::string::npos || ja.find("くださ") != std::string::npos;
            v.lemma = give ? "주다" : "가다";
            v.vpos = "V";
            v.kcls = give ? "U" : "REG";
            if (ja.find("ください") != std::string::npos || ja.find("なさい") != std::string::npos) v.mood = reg::Mood::imp;
            else if (ja.find("？") != std::string::npos || ja.find("か」") != std::string::npos) v.mood = reg::Mood::q;
            const auto regs = reg::AssignRegister(d->levels, d->ko, {v}, ds[k]);
            honGot = !regs.empty() && regs[0].hon ? "yes" : "no";
            honOk = honGot == r.at("hon");
        }
        if (got != r.at("level") || !specOk || !honOk)
            hdtest::Fail(__FILE__, __LINE__,
                         r.at("id") + " (" + spec + " " + r.at("case") + "): got " + got + " by " + ds[k].spec + " [" +
                             ds[k].key + "] hon " + honGot + ", want " + r.at("level") + " hon " + r.at("hon"));
    }
    std::string specText;
    HD_REQUIRE(Read(RepoDir() + "/spec/register.md", specText));
    std::set<std::string> headings;
    for (const std::string& line : reg::Split(specText, '\n'))
        if (line.starts_with("## ")) headings.insert(line.substr(3, line.find(' ', 3) - 3));
    std::set<std::string> inTable;
    for (const auto& [key, row] : d->levels.levels) inTable.insert(row.spec);
    for (const auto& [spec, fk] : fixKeep)
        if (!headings.contains(spec)) hdtest::Fail(__FILE__, __LINE__, spec + " is no heading of spec/register.md");
    for (const std::string& spec : inTable) {
        if (!headings.contains(spec)) hdtest::Fail(__FILE__, __LINE__, spec + " is no heading of spec/register.md");
        if (fixKeep[spec].first == 0 || fixKeep[spec].second == 0)
            hdtest::Fail(__FILE__, __LINE__, spec + " needs a fix and a keep case");
    }
}

HD_TEST(register_cue_keys) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    struct Case {
        const char* ja;
        const char* key;
    };
    const Case cases[] = {
        {"「わかってるわよ」", "casual"},       {"「半信半疑です……」", "pend"},
        {"「粗茶でございます」", "keigo-pend"}, {"「それでは、失礼します」", "set"},
        {"「入りたまえ」", "stiff"},           {"「見つけた！」", "exclaim"},
        {"「見つけたよ！」", "casual"},         {"「よ、よし、それじゃ行くぞっ！」", "decl-zo"},
        {"「はぁ、今日は寒いなぁ」", "exclaim-ne"}, {"「ああ、ただいま」", "bare"},
        {"「……」", "empty"},                   {"『正解！』と鈴木が手で○を作った。", "narr"},
    };
    for (const Case& c : cases) HD_CHECK_EQ(reg::CueKey(d->levels, c.ja), std::string(c.key));
}
