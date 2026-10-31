#include "core/dict/pack.h"

#include <string>
#include <vector>

#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::dict;
using hdTrans::userdict::Families;

namespace {

std::string Errors(const std::vector<PackError>& errors) {
    std::string s;
    for (const PackError& e : errors) s += (s.empty() ? "" : "\n") + std::to_string(e.line) + ": " + e.message;
    return s;
}

// Each row as "id slot key class target atoms pri".
std::string Show(const PackTable& t) {
    std::string out;
    for (const PackRow& r : t.rows) {
        std::string atoms;
        for (const PackAtom& a : r.atoms) {
            atoms += (atoms.empty() ? "" : ",") + a.slot + "=";
            for (std::size_t i = 0; i < a.values.size(); i++) atoms += (i ? "|" : "") + a.values[i];
        }
        out += (out.empty() ? "" : "; ") + r.id + " " + PackSlotName(r.slot) + " " + r.key + " " +
               (r.classDigit ? std::string(1, r.classDigit) : "-") + " " + r.target + " [" + atoms + "] " +
               std::to_string(r.pri) + (r.colloc ? " colloc" : "");
    }
    return out;
}

}  // namespace

HD_TEST(dict_pack_rows) {
    const std::string text =
        "# comment\n"
        "id\tslot\tlemma\tpos\tkind\tcond\ttarget\tpri\tprov\n"
        "a\tsense\t高い/たかい/adj\tadj\tframe\tsubj=@thing\t비싸다\t2\treview\n"
        "b\t\t本\tn\t\t*\t책\t\t\n"
        "c\tvoice\t読む\tv5\tframe\tobj=手紙|本 form=ta\tactive\t-1\t\n"
        "d\tpolarity\t行く\tv5\tframe\tform=neg\t1지 않을래\t\t\n"
        "e\tsense\t気をつける\tv1\tcolloc\t気をつける\t조심하다\t\t\n";
    Families families;
    PackTable t;
    std::vector<PackError> errors;
    ParsePackRows("p.tsv", text, nullptr, families, t, errors);
    HD_CHECK_EQ(Errors(errors), std::string());
    HD_CHECK_EQ(Show(t), std::string("a sense 高 7 비$AC@ [subj=@thing] 2; b sense 本 1 책 [] 0; "
                                     "c voice 読 4 active [obj=手紙|本,form=ta] -1; d polarity 行 4 1지 않을래 [form=neg] 0; "
                                     "e sense  - 조심하다 [] 0 colloc"));
    HD_CHECK(t.byKey.contains("高") && t.byKey.contains("読") && !t.byKey.contains("気をつけ"));
    HD_CHECK_EQ(PackRowById(t.rows[0].global).id, std::string("a"));
    {
        Families own;
        PackTable v;
        ParsePackRows("v.tsv", "id\tslot\tlemma\tpos\tcond\ttarget\nv\tvoice\t叱る\tv5\tvoice=passive with=*\tpassive:혼나다\n",
                      nullptr, own, v, errors);
        HD_CHECK_EQ(Errors(errors), std::string());
        HD_REQUIRE(v.rows.size() == 1);
        HD_CHECK_EQ(v.rows[0].target, std::string("passive"));
        HD_CHECK(v.rows[0].lexeme.starts_with("혼"));
        HD_CHECK_EQ(PackRowById(v.rows[0].global).target, std::string("passive:혼나다"));
    }
    HD_CHECK_EQ(PackRowById(t.rows[0].global).target, std::string("비싸다"));
}

HD_TEST(dict_pack_errors) {
    const struct {
        const char* row;
        const char* want;
    } cases[] = {
        {"x\twhat\t本\tn\t\t책", "2: slot \"what\", want sense, name, voice, polarity or tense"},
        {"x\tsense\t本\tq\t\t책", "2: pos \"q\", want n, pn, suf, pref, num, v, vs, vk, v1, v5, adj, na, adv, intj or p"},
        {"x\tsense\t本\tn\tobj\t책", "2: cue atom \"obj\", want slot=value"},
        {"x\tsense\t本\tn\tcolour=red\t책", "2: cue slot \"colour\" is not one of the cue DSL"},
        {"x\tvoice\t読む\tv5\t\tpassiv", "2: voice \"passiv\", want passive, potential, respectful, spontaneous or active"},
        {"x\ttense\t読む\tv5\t\t었다", "2: ending \"었다\" does not start with a form code (0-9, A, B)"},
        {"\tsense\t本\tn\t\t책", "2: empty id, lemma, pos or target"},
        {"x\tvoice\t本\tn\t\tpassive:혼나다", "2: a voice verb (\"passive:혼나다\") needs a verb row and a Korean verb"},
    };
    for (const auto& c : cases) {
        Families families;
        PackTable t;
        std::vector<PackError> errors;
        ParsePackRows("p.tsv", std::string("id\tslot\tlemma\tpos\tcond\ttarget\n") + c.row, nullptr, families, t, errors);
        HD_CHECK_EQ(Errors(errors), std::string(c.want));
    }
    Families families;
    PackTable t;
    std::vector<PackError> errors;
    ParsePackRows("p.tsv", "id\tlemma\tpos\ttarget\n", nullptr, families, t, errors);
    HD_CHECK_EQ(Errors(errors), std::string("1: the header has no column cond"));
}

HD_TEST(dict_pack_classes) {
    PackTable t;
    std::vector<PackError> errors;
    ParsePackClasses("c.tsv", "class\tparent\tnote\n@entity\t\troot\n@person\t@entity\n@person.kin\t@person\n", t, errors);
    ParsePackClasses("m.tsv", "lemma\tclasses\n母\t@person.kin\n本\t@thing|@entity\n", t, errors);
    HD_CHECK_EQ(Errors(errors), std::string());
    HD_CHECK_EQ(t.Depth("@entity"), 1);
    HD_CHECK_EQ(t.Depth("@person.kin"), 3);
    HD_CHECK(t.Member("母", "@person.kin") && t.Member("母", "@person") && t.Member("母", "@entity"));
    HD_CHECK(!t.Member("母", "@thing") && t.Member("本", "@entity") && !t.Member("犬", "@entity"));
    ParsePackClasses("x.tsv", "key\tvalue\n", t, errors);
    HD_CHECK_EQ(Errors(errors), std::string("1: the header is neither class, parent nor lemma, classes"));
}
