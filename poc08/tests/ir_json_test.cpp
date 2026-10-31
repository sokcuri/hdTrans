#include <string>

#include "driver/driver.h"
#include "driver/report.h"
#include "hdtest.h"
#include "ir/invariants.h"
#include "ir/ir_json.h"

namespace {

using namespace hd08;

// The examples of IR.md §7.
const char* const kToks =
    R"({"doc":"fic-03","line":12,"src":"変な噂なんてたったらどうするんですか！","norm":"変な噂なんてたったらどうするんですか！","edits":[],)"
    R"("toks":[{"src":[0,3],"surf":"変","lemma":"変","reading":"ヘン","pos":"na","ud":"形状詞-一般","ctype":"","cform":"語幹","flags":[],"cost":0,"margin":3.1},)"
    R"({"src":[18,24],"surf":"たっ","lemma":"経つ","reading":"タッ","pos":"v5","ud":"動詞-一般","ctype":"五段-タ行","cform":"連用形-促音便","flags":[],"cost":0,"margin":0.4,)"
    R"("alts":[{"lemma":"立つ","cost":0.4}]}]})";

const char* const kGen =
    R"({"case":"gen-0001","kir2":[{"id":1,"kind":"N","lemma":"소문"},{"id":2,"kind":"P","kase":"subj"},)"
    R"({"id":3,"kind":"V","lemma":"나다","kcls":"REG","tense":"nonpast","ctype":"conn","rel":"cond"},)"
    R"({"id":4,"kind":"Adv","lemma":"어떻게"},{"id":5,"kind":"V","lemma":"하다","kcls":"HA","mood":"q","ctype":"final"},)"
    R"({"id":6,"kind":"Punct","punct":"excl","jaMark":"！"}],)"
    R"("reg":[{"node":5,"level":"haeyo","persona":"none","hon":false}],"out":"소문이 나면 어떻게 해요!"})";

HD_TEST(poc08_names_round_trip) {
    for (F f : {F::ev, F::pos, F::link, F::negfn, F::modality, F::kcls, F::ctype, F::rel, F::kase, F::level,
                F::flag, F::jafact}) {
        for (unsigned v = 0; v < Count(f); v++) {
            unsigned back = 999;
            HD_CHECK(Parse(f, Name(f, v), back));
            HD_CHECK_EQ(back, v);
        }
    }
    unsigned v = 0;
    HD_CHECK(Parse(F::negfn, "potential-neg", v));
    HD_CHECK_EQ(v, unsigned{negfn::potential_neg});
    HD_CHECK(!Parse(F::negfn, "potential_neg", v));
    HD_CHECK_EQ(std::string(Name(F::ev, static_cast<unsigned>(Ev::UnmatchedForm))), std::string("unmatched-form"));
}

HD_TEST(poc08_toks_example_reads) {
    std::vector<Doc> docs;
    std::string err;
    HD_REQUIRE(ReadJsonl(kToks, docs, err));
    HD_REQUIRE(docs.size() == 1 && docs[0].lines.size() == 1);
    const Doc& d = docs[0];
    const Line& l = d.lines[0];
    HD_CHECK_EQ(d.id, std::string("fic-03"));
    HD_CHECK_EQ(l.id, 12u);
    HD_REQUIRE(l.toks.size() == 2);
    HD_CHECK(l.toks[1].pos == Pos::v5);
    HD_CHECK_EQ(std::string(d.S(l.toks[1].lemma)), std::string("経つ"));
    HD_CHECK_EQ(std::string(d.cforms.Str(l.toks[1].cform)), std::string("連用形-促音便"));
    HD_CHECK_EQ(l.toks[1].src.begin, 18u);
    HD_REQUIRE(l.toks[1].nAlt == 1);
    const Token& alt = l.alts[l.toks[1].alt];
    HD_CHECK_EQ(std::string(d.S(alt.lemma)), std::string("立つ"));
    HD_CHECK(alt.pos == Pos::v5);  // an alternative inherits what it does not name
    HD_CHECK(l.toks[0].margin > 3.0f);
}

HD_TEST(poc08_line_json_round_trip) {
    std::vector<Doc> docs;
    std::string err;
    HD_REQUIRE(ReadJsonl(kToks, docs, err));
    const std::string once = LineJson(docs[0], docs[0].lines[0]);
    std::vector<Doc> again;
    HD_REQUIRE(ReadJsonl(once, again, err));
    HD_CHECK_EQ(LineJson(again[0], again[0].lines[0]), once);
}

HD_TEST(poc08_gen_case_reads) {
    std::vector<Doc> docs;
    std::string err;
    HD_REQUIRE(ReadJsonl(kGen, docs, err));
    const Line& l = docs[0].lines[0];
    HD_CHECK_EQ(l.name, std::string("gen-0001"));
    HD_REQUIRE(l.kir2.size() == 6);
    HD_CHECK(l.kir2[2].kind == KK::V);
    HD_CHECK_EQ(unsigned{l.kir2[2].ctype}, unsigned{ctype::conn});
    HD_CHECK_EQ(unsigned{l.kir2[2].rel}, unsigned{rel::cond});
    HD_CHECK_EQ(unsigned{l.kir2[4].kcls}, unsigned{kcls::HA});
    HD_CHECK_EQ(unsigned{l.kir2[4].mood}, unsigned{mood::q});
    HD_CHECK_EQ(unsigned{l.kir2[5].punct}, unsigned{punct::excl});
    HD_REQUIRE(l.reg.size() == 1);
    HD_CHECK_EQ(unsigned{l.reg[0].level}, unsigned{level::haeyo});
    HD_CHECK_EQ(l.out, std::string("소문이 나면 어떻게 해요!"));
    // "conn:<rel>" is read as ctype conn plus rel.
    Json j;
    HD_REQUIRE(ParseJson(R"({"id":9,"kind":"V","ctype":"conn:cause"})", j, err));
    KNode n;
    Doc d;
    HD_REQUIRE(ReadNode(j, d, n, err));
    HD_CHECK_EQ(unsigned{n.ctype}, unsigned{ctype::conn});
    HD_CHECK_EQ(unsigned{n.rel}, unsigned{rel::cause});
}

HD_TEST(poc08_unknown_value_is_an_error) {
    std::vector<Doc> docs;
    std::string err;
    HD_CHECK(!ReadJsonl(R"({"case":"x","kir2":[{"id":1,"kind":"Verb"}]})", docs, err));
    HD_CHECK(err.find("Verb") != std::string::npos);
}

Engine StubEngine() {
    Engine e;
    Config cfg;
    cfg.data = HD08_SOURCE_DIR;
    std::string err;
    LoadEngine(cfg, e, err);
    return e;
}

HD_TEST(poc08_driver_runs_end_to_end) {
    Engine e = StubEngine();
    HD_REQUIRE(e.analyzer && e.generator);
    Doc d;
    for (const char* s : {"変な噂なんてたったらどうするんですか！", "", "ABC 123"}) {
        Line l;
        l.id = static_cast<uint32_t>(d.lines.size() + 1);
        l.src = s;
        d.lines.push_back(l);
    }
    RunDoc(e, d, {});
    for (const Line& l : d.lines) {
        const LineReport r = Report(e, d, l);
        HD_CHECK_EQ(r.tracedBytes, r.outBytes);  // G11: every output morpheme traced
        if (!StubComponents().empty() && !l.src.empty()) HD_CHECK(!r.covered);
    }
    FreeEngine(e);
}

HD_TEST(poc08_invariants_carry_and_polarity) {
    Doc d;
    Line l;
    l.src = "ABC 12";
    l.out = "ABC";
    InvariantData data;
    data.lexicalNegatives = {"없다"};
    std::vector<Flag> flags;
    CheckInvariants(d, l, data, flags);
    HD_REQUIRE(flags.size() == 1);
    HD_CHECK_EQ(unsigned{flags[0].kind}, unsigned{flag::carry});
    HD_CHECK_EQ(unsigned{flags[0].inv}, 5u);

    // A negative clause whose Korean has no negation flips (I1); a lexical negative satisfies it.
    Line n;
    n.src = "ない";
    Token t;
    t.src = {0, 6};
    t.pos = Pos::v5;
    n.toks.push_back(t);
    Bunsetsu b;
    n.buns.push_back(b);
    Clause c;
    c.pol = pol::neg;
    n.clauses.push_back(c);
    KNode v;
    v.id = 1;
    v.kind = KK::V;
    v.lemma = d.I("있다");
    v.src = {0, 6};
    n.kir.push_back(v);
    n.kir2.push_back(v);
    n.out = "있다";
    flags.clear();
    CheckInvariants(d, n, data, flags);
    HD_REQUIRE(flags.size() == 1);
    HD_CHECK_EQ(unsigned{flags[0].kind}, unsigned{flag::flip});
    HD_CHECK_EQ(unsigned{flags[0].inv}, 1u);
    n.kir2[0].lemma = d.I("없다");
    flags.clear();
    CheckInvariants(d, n, data, flags);
    HD_CHECK(flags.empty());
}

}  // namespace

namespace {

using namespace hd08;

HD_TEST(poc08_v2_fields_round_trip) {
    const char* rec =
        R"({"doc":"d","line":1,"src":"ab","toks":[{"src":[0,1],"surf":"a","pos":"n","derive":"derive:1"}],)"
        R"("alt":{"toks":[{"src":[0,2],"surf":"ab","pos":"v5"}],"buns":[{"first":0,"last":0,"type":"P"}],)"
        R"("clauses":[{"pred":0,"subj":{"kind":"addressee","decision":2}}]},)"
        R"("clauses":[{"pred":0,"negFn":"potential-neg","beneTo":{"kind":"speaker","bun":0}}],)"
        R"("kir":[{"id":1,"kind":"N","lemma":"가"}],"chosen":{"path":1,"heads":[-1]},)"
        R"("decisions":[{"stage":"Z","kind":"example","rule":"tm:b1#3","src":[0,2],"contrib":{"force=q":1.8,"prior:cue":0.5},"path":1,"flip":"obj=x","nAlt":2}]})";
    std::vector<Doc> docs;
    std::string err;
    HD_REQUIRE(ReadJsonl(rec, docs, err));
    const Doc& d = docs[0];
    const Line& l = d.lines[0];
    HD_REQUIRE(l.alt.toks.size() == 1 && l.alt.buns.size() == 1 && l.alt.clauses.size() == 1);
    HD_CHECK_EQ(unsigned{l.alt.clauses[0].subj.kind}, unsigned{refkind::addressee});
    HD_CHECK_EQ(l.alt.clauses[0].subj.bun, uint16_t{0xFFFF});
    HD_CHECK_EQ(l.clauses[0].beneTo.bun, uint16_t{0});
    HD_CHECK_EQ(unsigned{l.chosen.path}, 1u);
    HD_REQUIRE(l.decisions.size() == 1);
    HD_CHECK(l.decisions[0].kind == Ev::Example);
    HD_REQUIRE(l.decisions[0].contrib.size() == 2);
    HD_CHECK_EQ(std::string(d.S(l.decisions[0].contrib[0].first)), std::string("force=q"));
    HD_CHECK_EQ(std::string(d.S(l.toks[0].derive)), std::string("derive:1"));
    const std::string once = LineJson(d, l);
    std::vector<Doc> again;
    HD_REQUIRE(ReadJsonl(once, again, err));
    HD_CHECK_EQ(LineJson(again[0], again[0].lines[0]), once);
}

}  // namespace

namespace {

using namespace hd08;

HD_TEST(poc08_review_fixes) {
    std::string err;
    // An alternative whose fields are zero where the main token's are not keeps its zeros.
    Doc d;
    Line l;
    l.src = l.norm = "ab";
    Token main;
    main.src = {0, 2};
    main.lex = d.I("lex:ab/ab/n#1");
    main.flags = tokflag::LongUnit;
    main.cost = 1.25f;
    main.pos = Pos::n;
    Token alt;
    alt.src = {0, 2};
    alt.pos = Pos::unk;
    l.alts.push_back(alt);
    main.alt = 0;
    main.nAlt = 1;
    l.toks.push_back(main);
    std::vector<Doc> docs;
    HD_REQUIRE(ReadJsonl(LineJson(d, l), docs, err));
    const Line& back = docs[0].lines[0];
    HD_REQUIRE(back.alts.size() == 1);
    HD_CHECK_EQ(back.alts[0].lex, 0u);
    HD_CHECK_EQ(unsigned{back.alts[0].flags}, 0u);
    HD_CHECK(back.alts[0].cost == 0.0f);
    HD_CHECK(back.toks[0].cost == 1.25f);
    // norm defaults to src, map to identity.
    HD_CHECK_EQ(back.norm, std::string("ab"));
    HD_CHECK_EQ(back.map.size(), size_t{3});

    // Numbers out of range are errors, not casts.
    docs.clear();
    HD_CHECK(!ReadJsonl(R"({"line":1,"decisions":[{"kind":200}]})", docs, err));
    docs.clear();
    HD_CHECK(!ReadJsonl(R"({"line":1,"toks":[{"src":[-1,2]}]})", docs, err));
    docs.clear();
    HD_CHECK(!ReadJsonl(R"({"line":1,"clauses":[{"modality":1e30}]})", docs, err));

    // Floats round-trip.
    std::string num;
    AppendJsonNumber(num, 1234.5678f);
    Json j;
    HD_REQUIRE(ParseJson(num, j, err));
    HD_CHECK(static_cast<float>(j.n) == 1234.5678f);

    // A lone surrogate becomes U+FFFD; a pair becomes one code point.
    HD_REQUIRE(ParseJson(R"("\ud800A😀")", j, err));
    HD_CHECK_EQ(j.s, std::string("\xEF\xBF\xBD" "A" "\xF0\x9F\x98\x80"));

    // Context lines keep id 0; a document record after lines starts a new document.
    docs.clear();
    HD_REQUIRE(ReadJsonl("{\"doc\":\"\"}\n{\"src\":\"x\",\"context\":true}\n{\"src\":\"y\"}\n{\"doc\":\"\"}\n{\"src\":\"z\"}\n",
                         docs, err));
    HD_REQUIRE(docs.size() == 2);
    HD_CHECK_EQ(docs[0].lines[0].id, 0u);
    HD_CHECK_EQ(docs[0].lines[1].id, 1u);
    HD_CHECK_EQ(docs[1].lines[0].id, 1u);
}

HD_TEST(poc08_invariants_split_runs_and_path) {
    Doc d;
    InvariantData data;
    Line l;
    l.src = "HP100";
    l.out = "HP 100";
    std::vector<Flag> flags;
    CheckInvariants(d, l, data, flags);
    HD_CHECK(flags.empty());

    // With path 1 chosen, the clause and tokens of line.alt are checked.
    Line n;
    n.src = "ない";
    Token t;
    t.src = {0, 6};
    t.pos = Pos::v5;
    n.toks.push_back(t);  // path 0: a content token with no node would be 'dropped'
    Token a = t;
    a.pos = Pos::aux;
    n.alt.toks.push_back(a);
    n.alt.buns.push_back(Bunsetsu{});
    n.alt.clauses.push_back(Clause{});
    n.chosen.path = 1;
    n.out = "x";
    KNode v;
    v.id = 1;
    v.kind = KK::V;
    n.kir2.push_back(v);
    flags.clear();
    CheckInvariants(d, n, data, flags);
    for (const Flag& f : flags) HD_CHECK(f.kind != flag::dropped);
}

}  // namespace
