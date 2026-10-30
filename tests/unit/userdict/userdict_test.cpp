#include "core/userdict/userdict.h"

#include <string>
#include <string_view>
#include <vector>

#include "core/dict/builtin.h"
#include "core/userdict/compile.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::userdict;

namespace {

struct Case {
    const char* in;
    const char* want;
};

constexpr std::string_view kEhndFormatHint = " (Ehnd 형식 사전으로 보입니다: ehnd 폴더에 두세요)";

std::string Collapse(const std::string& s) {
    std::string out;
    for (const std::string_view f : internal::Fields(s)) {
        if (!out.empty()) out += ' ';
        out += f;
    }
    return out;
}

std::string Errors(const std::vector<Error>& errors) {
    std::string s;
    for (const Error& e : errors) s += (s.empty() ? "" : "\n") + std::to_string(e.line) + ": " + e.message;
    return s;
}

struct Parsed {
    dict::CodeTable codes;
    Families families;
    Compiled out;
};

void Run(Parsed& p, std::string_view text, bool soundTable = false) {
    Context ctx{&p.codes, &p.families, nullptr};
    ctx.soundTable = soundTable;
    p.out = Parse(text, 0, ctx);
}

// The entries as "KEY TARGET PART SEM GRAM" with flags and "@line.order", joined by
// " | ".
std::string Show(const std::vector<dict::UserEntry>& entries) {
    std::string out;
    for (const dict::UserEntry& e : entries) {
        std::string s = e.key + " " + e.target + " " + e.codes->partText + " " + std::string(e.codes->sem.Text()) + " " +
                        std::string(e.codes->gram.Text());
        if (e.prio != 0) s += " prio=" + std::to_string(e.prio);
        if (e.finalClass) s += std::string(" final=") + e.finalClass;
        if (e.name) s += " name";
        if (e.force) s += " force";
        if (e.hidden) s += " hidden";
        if (e.honorific) s += " hon";
        s += " @" + std::to_string(e.line) + "." + std::to_string(e.order);
        out += (out.empty() ? "" : " | ") + Collapse(s);
    }
    return out;
}

// The entries as "KEY TARGET PART" with the end-row fields.
std::string ShowEndings(const std::vector<dict::UserEntry>& entries) {
    std::string out;
    for (const dict::UserEntry& e : entries) {
        std::string s = e.key + " " + e.target + " " + e.codes->partText;
        if (e.registerLevel) s += " register=" + std::to_string(e.registerLevel);
        if (e.polite) s += " polite";
        if (e.stative) s += " stative";
        if (!e.negative.empty()) s += " neg=" + e.negative;
        s += " @" + std::to_string(e.line) + "." + std::to_string(e.order);
        out += (out.empty() ? "" : " | ") + Collapse(s);
    }
    return out;
}

std::string_view (Families::*RowOf(char marker))(std::string_view) const {
    switch (marker) {
        case '$': return &Families::DollarRow;
        case '%': return &Families::PercentRow;
    }
    return &Families::SourceRow;
}

}  // namespace

HD_TEST(userdict_parse) {
    const Case cases[] = {
        {"門前\t문 앞", "門前 문 앞 N110 @1.0"},
        {"\xEF\xBB\xBF学生\t학도\tn", "学生 학도 N110 @1.0"},
        {"ガチ\t진짜\tphrase", "ガチ 진짜 A9D0 @1.0"},
        {"春香\t하루카\tname", "春香 하루카 N130 QPNVHn Wb name @1.0"},
        {"藤井\t후지이\tsurname", "藤井 후지이 N130 QPNVHn Wa name @1.0"},
        {"五村\t고노무라\tplace", "五村 고노무라 N130 QCTVPl @1.0"},
        {"はわわ\t아앗\tintj", "はわわ 아앗 E000 @1.0"},
        {"ちゃん\t짱\thon", "ちゃん 짱 N110 Tu hon @1.0"},
        {"先輩\t선배\thon\tspace", "先輩 선배 N110 Ha hon @1.0"},
        {"たん\t\thon", "たん N110 Tu hon @1.0"},
        {"// comment\n\n  // indented comment\n猫\t냥이\tn\t\t// trailing", "猫 냥이 N110 @4.0"},
        {"# comment\n  # indented comment\n#猫\t냥이\n犬\t개", "犬 개 N110 @4.0"},
        {"猫\t냥이\t// no POS", "猫 냥이 N110 @1.0"},
        {"猫\t냥이\tn\tprio=2 force final=v", "猫 냥이 N110 prio=2 final=v force @1.0"},
        {"猫\t냥이\tn\tprio=-1 hidden", "猫 냥이 N110 prio=-1 hidden @1.0"},
        {"猫\t냥이\tn\tattr=VAn gattr=Xy", "猫 냥이 N110 VAn Xy @1.0"},
        {"猫\t냥이\r\n犬\t개\r\n", "猫 냥이 N110 @1.0 | 犬 개 N110 @2.0"},

        {"勉強する\t공부하다\tv", "勉強 공부&AV@ M429 @1.0 | 勉強 공부 N110 Hd @1.1"},
        {"完成する\t완성되다\tv", "完成 완성&C6@ M429 @1.0 | 完成 완성 N110 Hd @1.1"},
        {"嘉する\t가상히 여기다\tv", "嘉 가상히 여&A2@ M429 @1.0"},
        {"手伝う\t돕다\tv:ㅂ", "手伝 &J5@ M420 @1.0"},
        {"書く\t적다\tv", "書 &HY@ M421 @1.0"},
        {"書く\t쓰다\tv", "書 &AF@ M421 @1.0"},
        {"急ぐ\t서두르다\tv", "急 &MC@ M422 @1.0"},
        {"聞く\t묻다\tv:ㄷ", "聞 &J7@ M421 @1.0"},
        {"聞く\t묻다\tv", "聞 &J7@ M421 @1.0"},
        {"埋める\t묻다\tv1:규칙", "埋め &H9@ M427 @1.0"},
        {"食べる\t먹다\tv", "食べ &HU@ M427 @1.0"},
        {"行く\t가다\tv", "行 &A1@ M42? @1.0"},
        {"出来る\t할 수 있다\tv", "出来 할 수 &HN@ M427 @1.0"},
        {"できる\t할 수 있다\tv", "でき 할 수 &HN@ M427 @1.0"},
        {"出来ない\t못하다\tv", "出来な 못$AB@ H7N0 @1.0"},
        {"持って来る\t가져오다\tv", "持って 가져&AH@ M42= @1.0"},
        {"聞こえる\t듣다\tv1", "聞こえ &J6@ M427 @1.0"},
        {"危ない\t큰일이다\tadj", "危な 큰일$AH@ H7N0 @1.0"},
        {"無理\t무리이다\tna", "無理 무리%D4@ G8U0 @1.0"},
        {"出ていく\t나가다\tv", "出てい 나&A1@ M42? @1.0"},
        {"見る\t보다\tv1", "見 &AY@ M427 @1.0"},
        {"走る\t달리다\tv5", "走 달&AA@ M428 @1.0"},
        {"作る\t만들다\tv5", "作 만&L4@ M428 @1.0"},
        {"寂しい\t쓸쓸하다\tadj", "寂し 쓸쓸$AB@ H7N0 @1.0"},
        {"高い\t높다\tadj", "高 $D3@ H7N0 @1.0"},
        {"忙しい\t바쁘다\tadj", "忙し 바$AG@ H7N0 @1.0"},
        {"綺麗\t깔끔하다\tna", "綺麗 깔끔%A1@ G8U0 @1.0"},
        {"静か\t조용하다/조용히\tna", "静か 조용%A1@ G8U0 @1.0 | 静かに 조용히 E300 @1.1"},
        {"ゆっくり\t천천히\tadv", "ゆっくり 천천히 &AV@ M429 @1.0 | ゆっくり 천천히 E300 @1.1"},
        {"ワクワク\t두근두근/두근거리다\tono", "ワクワク 두근거&AA@ M429 @1.0 | ワクワク 두근두근 E300 @1.1"},
    };
    for (const Case& c : cases) {
        Parsed p;
        Run(p, c.in);
        HD_CHECK_EQ(Errors(p.out.errors), std::string());
        HD_CHECK_EQ(Show(p.out.entries), std::string(c.want));
    }
}

HD_TEST(userdict_parse_errors) {
    const Case cases[] = {
        {"猫", "1: empty KR"},
        {"\t냥이", "1: empty JP"},
        {"猫\t냥이\tnoun", "1: unknown POS \"noun\""},
        {"猫\t냥이\tn:ㅂ", "1: POS n takes no conjugation class"},
        {"猫\t냥이\tn\t\tloop", "1: 5 columns, at most 4"},
        {"猫\t냥이\tn\tsource=loop", "1: option \"source=loop\": unknown option \"source=loop\""},
        {"猫\t냥이\tn\tbogus", "1: option \"bogus\": unknown option \"bogus\""},
        {"ぷるぷる\t탱글탱글\tono\tacts", "1: option \"acts\": acts is only for the built-in sound table"},
        {"猫\t냥이\tn\tprio=x", "1: option \"prio=x\": \"x\" is not an integer"},
        {"猫\t냥이\tn\tprio=+-1", "1: option \"prio=+-1\": \"+-1\" is not an integer"},
        {"猫\t냥이\tn\tspace", "1: option \"space\": unknown option \"space\""},
        {"猫\t냥이\tn\tneg=모르다", "1: option \"neg=모르다\": unknown option \"neg=모르다\""},
        {"書いたじゃん\t썼잖아\tend\tregister=rude", "1: option \"register=rude\": unknown option \"register=rude\""},
        {"話したじゃん\t말했잖아\tend",
         "1: end JP \"話したじゃん\" does not start with a model word (書く verb, 高い i-adjective, 静か adjectival noun, "
         "学生 noun)"},
        {"書いたじゃん\t적었잖아\tend", "1: KR \"적었잖아\" does not start with a form of 쓰다"},
        {"帰る\t돌아가다\tv", "1: る-verb \"帰る\" is ambiguous: write v1 (ichidan) or v5 (godan)"},
        {"書く\t적\tv", "1: KR \"적\" is not a lemma ending in 다"},
        {"聞く\t묻다\tv:ㅂ", "1: class ㅂ does not fit \"묻다\""},
        {"聞く\t묻다\tv:ㅁ", "1: unknown conjugation class \"ㅁ\" (known: 규칙 하다 되다 이다 ㄹ ㅂ ㄷ ㅅ 르 러 ㅎ 우 으)"},
        {"高\t높다\tadj", "1: adj JP must be a dictionary form ending in い"},
        {"分かる\t알다\tv5\tneg=모르", "1: neg: KR \"모르\" is not a lemma ending in 다"},
        {"学生だ\t학도다\tend", "1: KR \"학도다\" does not start with 학생"},
        {"高\t높다\tend", "1: end row has no ending after the model word"},
        {"猫\t냥이\n\n犬\n鳥\t새\tx", "3: empty KR\n4: unknown POS \"x\""},
    };
    for (const Case& c : cases) {
        Parsed p;
        Run(p, c.in);
        HD_CHECK_EQ(Errors(p.out.errors), std::string(c.want));
    }
    // The good lines of a file with errors compile.
    Parsed p;
    Run(p, "猫\t냥이\n\n犬\n鳥\t새\tx");
    HD_CHECK_EQ(Show(p.out.entries), std::string("猫 냥이 N110 @1.0"));
}

HD_TEST(userdict_ehnd_format_hint) {
    const struct {
        const char* in;
        const char* errors;
        std::size_t errorCount;
        const char* entries;
        bool hint;
    } cases[] = {
        {"\n# comment\n // comment\n", "", 0, "", false},
        {"猫\t고양이\tn", "", 0, "猫 고양이 N110 @1.0", false},
        {"猫\t고양이\tn\tattr=VHn", "", 0, "猫 고양이 N110 VHn @1.0", false},
        {"猫\t고양이\tVHn", "1: unknown POS \"VHn\"", 1, "", true},
        {"\xEF\xBB\xBF猫\t고양이\t VPl \r\n", "1: unknown POS \"VPl\"", 1, "", true},
        {"猫\t고양이\tVBp", "1: unknown POS \"VBp\"", 1, "", true},
        {"猫\t고양이\tQPNVHn", "1: unknown POS \"QPNVHn\"", 1, "", false},
        {"猫\t고양이\tVHnXYZ", "1: unknown POS \"VHnXYZ\"", 1, "", false},
        {"猫\t고양이\tvhn\n猫\t고양이\tVHN\n猫\t고양이\tVH\n猫\t고양이\tVHnX",
         "1: unknown POS \"vhn\"\n2: unknown POS \"VHN\"\n3: unknown POS \"VH\"\n4: unknown POS \"VHnX\"",
         4, "", false},
        {"猫\t고양이\tnoun\n猫\t고양이\tN110\n猫\t고양이\tXYZ",
         "1: unknown POS \"noun\"\n2: unknown POS \"N110\"\n3: unknown POS \"XYZ\"", 3, "", false},
        {"猫\t\tVHn", "1: empty KR", 1, "", false},
        {"猫\t고양이\tVHn\tbogus", "1: option \"bogus\": unknown option \"bogus\"", 1, "", false},
        {"猫\t고양이\tVHn:AA", "1: POS VHn takes no conjugation class", 1, "", false},
        {"猫\t고양이\t1\tVHn", "1: option \"VHn\": unknown option \"VHn\"", 1, "", false},
        {"猫\t고양이\tVHn\n犬\t\tn", "1: unknown POS \"VHn\"\n2: empty KR", 2, "", false},
        {"猫\t고양이\tVHn\n猫\t고양이\tVPl\n犬\t\tn",
         "1: unknown POS \"VHn\"\n2: unknown POS \"VPl\"\n3: empty KR", 3, "", true},
        {"猫\t고양이\tVHn\n猫\t고양이\tXYZ\n犬\t\tn",
         "1: unknown POS \"VHn\"\n2: unknown POS \"XYZ\"\n3: empty KR", 3, "", false},
        {"猫\t고양이\tVHn\n猫\t고양이\tVBp\n猫\t고양이\tXYZ\n犬\t\tn",
         "1: unknown POS \"VHn\"\n2: unknown POS \"VBp\"\n3: unknown POS \"XYZ\"\n4: empty KR", 4, "", false},
        {"鳥\t새\tn\n鳥\t새\tn\n鳥\t새\tn\n猫\t고양이\tVHn", "4: unknown POS \"VHn\"", 1,
         "鳥 새 N110 @1.0 | 鳥 새 N110 @2.0 | 鳥 새 N110 @3.0", true},
    };
    for (const auto& c : cases) {
        Parsed p;
        Run(p, c.in);
        std::string want = c.errors;
        if (c.hint) {
            const std::size_t end = want.find('\n');
            want.insert(end == std::string::npos ? want.size() : end, kEhndFormatHint);
        }
        HD_CHECK_EQ(Errors(p.out.errors), want);
        HD_CHECK_EQ(p.out.errors.size(), c.errorCount);
        HD_CHECK_EQ(Show(p.out.entries), std::string(c.entries));
    }
}

HD_TEST(userdict_ehnd_format_hint_per_file) {
    Parsed p;
    Context ctx{&p.codes, &p.families, nullptr};
    const std::string_view first = "猫\t고양이\tVHn\n犬\t\tn";
    const std::string firstErrors = "1: unknown POS \"VHn\"\n2: empty KR";
    const auto a = Parse(first, 0, ctx);
    HD_CHECK_EQ(Errors(a.errors), firstErrors);
    HD_CHECK_EQ(a.errors.size(), std::size_t{2});

    const auto b = Parse("犬\t\tn\n猫\t고양이\tVHn\n猫\t고양이\tVPl", 1, ctx);
    HD_CHECK_EQ(Errors(b.errors), "1: empty KR" + std::string(kEhndFormatHint) +
                                    "\n2: unknown POS \"VHn\"\n3: unknown POS \"VPl\"");
    HD_CHECK_EQ(b.errors.size(), std::size_t{3});
    HD_CHECK_EQ(Errors(a.errors), firstErrors);

    const auto fixed = Parse("鳥\t새\tn", 1, ctx);
    HD_CHECK(fixed.errors.empty());
    HD_REQUIRE(fixed.entries.size() == 1);
    HD_CHECK_EQ(Show(fixed.entries), std::string("鳥 새 N110 @1.0"));
    HD_CHECK_EQ(fixed.entries[0].file, 1);
    HD_CHECK_EQ(Errors(Parse(first, 0, ctx).errors), firstErrors);

    std::vector<internal::Rec> recs;
    HD_CHECK_EQ(internal::Compile("猫", "고양이", "VHn", "", false, nullptr, p.families, recs),
                std::string("unknown POS \"VHn\""));
    HD_CHECK(recs.empty());
}

// The past and infinitive rows of lemmas written without a class, and of the regular
// homographs written with one.
HD_TEST(userdict_auto_class) {
    const struct {
        const char* lemma;
        char marker;
        const char* cls;
        const char* want;
    } cases[] = {
        {"듣다", '&', "", "들었 들어"},
        {"걷다", '&', "", "걸었 걸어"},
        {"걷다", '&', "규칙", "걷었 걷어"},
        {"묻다", '&', "", "물었 물어"},
        {"묻다", '&', "규칙", "묻었 묻어"},
        {"깨닫다", '&', "", "깨달았 깨달아"},
        {"받다", '&', "", "받았 받아"},
        {"돕다", '&', "", "도왔 도와"},
        {"굽다", '&', "", "구웠 구워"},
        {"굽다", '&', "규칙", "굽었 굽어"},
        {"잡다", '&', "", "잡았 잡아"},
        {"춥다", '$', "", "추웠 추워"},
        {"덥다", '$', "", "더웠 더워"},
        {"쉽다", '$', "", "쉬웠 쉬워"},
        {"어렵다", '$', "", "어려웠 어려워"},
        {"좁다", '$', "", "좁았 좁아"},
        {"짓다", '&', "", "지었 지어"},
        {"낫다", '$', "", "나았 나아"},
        {"붓다", '&', "", "부었 부어"},
        {"씻다", '&', "", "씻었 씻어"},
        {"모르다", '&', "", "몰랐 몰라"},
        {"부르다", '&', "", "불렀 불러"},
        {"흐르다", '&', "", "흘렀 흘러"},
        {"따르다", '&', "", "따랐 따라"},
        {"그렇다", '$', "", "그랬 그래"},
        {"하얗다", '$', "", "하얬 하얘"},
        {"놓다", '&', "", "놓았 놓아"},
        {"푸다", '&', "", "펐 퍼"},
        {"크다", '$', "", "컸 커"},
        {"큰일이다", '$', "", "큰일(이었)였 큰일(이어)여"},
        {"문제이다", '%', "", "문제(이었)였 문제(이어)여"},
        {"큰일이다", '&', "이다", "큰일(이었)였 큰일(이어)여"},
        {"죽이다", '&', "규칙", "죽였 죽여"},
    };
    for (const auto& c : cases) {
        Families families;
        std::string target;
        const std::string err = Conjugating(c.lemma, c.marker, c.cls, families, target);
        HD_CHECK_EQ(err, std::string());
        HD_REQUIRE(target.size() >= 4);
        const std::string prefix = target.substr(0, target.size() - 4), id = target.substr(target.size() - 4);
        const auto row = RowOf(c.marker);
        const std::string got = prefix + std::string((families.*row)(id + "5")) + " " + prefix +
                                std::string((families.*row)(id + "6"));
        HD_CHECK_EQ(got, std::string(c.want));
    }
}

// The rows of the families made for stems that no static family conjugates; their
// first letter is one no static family of the marker uses.
HD_TEST(userdict_generated_families) {
    const Case cases[] = {
        {"白い\t하얗다\tadj", "하 얗 야 얄 얀 얬 얘 얗습 얌"},
        {"苦手\t서툴다\tna", "서 툴 툴 툴 툰 툴었 툴어 툽 툶"},
        {"黄色い\t누르다\tadj:러", "누 르 르 를 른 르렀 르러 릅 름"},
        {"称える\t일컫다\tv1:ㄷ", "일 컫는 컫 컬으 컬을 컫는 컬었 컬어 컫습 컬은 컬읍 컬으 컬음"},
        {"汲む\t푸다\tv:우", "푸는 푸 푸 풀 푼 펐 퍼 풉 푼 풉 푸 품"},
    };
    for (const Case& c : cases) {
        Parsed p;
        Run(p, c.in);
        HD_CHECK_EQ(Errors(p.out.errors), std::string());
        HD_REQUIRE(!p.out.entries.empty());
        const std::string& target = p.out.entries[0].target;
        const std::string id = target.substr(target.size() - 4);
        std::string_view codes = kFormCodes;
        if (id[0] != '&') codes = "12345678";
        const auto row = RowOf(id[0]);
        std::string got = target.substr(0, target.size() - 4);
        for (const char code : codes) got += " " + std::string((p.families.*row)(id + code));
        HD_CHECK_EQ(Collapse(got), std::string(c.want));
        const std::string_view used = id[0] == '&' ? "ACEFHJKLMN" : id[0] == '$' ? "ACDFGIJKL" : "ABCDE";
        HD_CHECK(used.find(id[1]) == std::string_view::npos);
        HD_CHECK_EQ(p.families.Size(), std::size_t{1});
    }
    // The same rows get the same id.
    Parsed p;
    Run(p, "白い\t하얗다\tadj\n真っ白い\t새하얗다\tadj\n青白い\t하얗다\tadj");
    HD_REQUIRE(p.out.entries.size() == 3);
    HD_CHECK_EQ(p.out.entries[0].target, std::string("하$B0@"));
    HD_CHECK_EQ(p.out.entries[2].target, std::string("하$B0@"));
    HD_CHECK_EQ(p.out.entries[1].target, std::string("새하$B0@"));
    HD_CHECK_EQ(p.families.Size(), std::size_t{1});
}

HD_TEST(userdict_endings) {
    const Case cases[] = {
        {"書いたじゃん\t썼잖아(요)\tend", "じゃん 5잖아(요) S560 @1.0"},
        {"書いてたじゃん\t썼잖아\tend", "たじゃん 5잖아 S570 @1.0"},
        {"書かないで\t쓰지 마\tend\tregister=casual", "ないで 1지 마 S580 register=2 @1.0"},
        {"書く\t쓴다\tend\tregister=plain", "4다 S500 register=1 @1.0"},
        {"書くのだ\t쓰는 거야\tend", "のだ 0 거야 S500 @1.0"},
        {"書くわよ\t써\tend\tstative", "わよ 6 S500 stative @1.0"},
        {"書いちゃった\t써버렸어\tend", "った 6버렸어 S5A0 @1.0"},
        {"書きました\t썼습니다\tend\tregister=formal", "ました 5습니다 S5:0 register=4 polite @1.0"},
        {"書け\t쓰세요\tend", "A세요 S5;0 @1.0"},
        {"書こう\t씁시다\tend", "9시다 S550 @1.0"},
        {"書きまする\t쓰옵니다\tend", "まする A옵니다 S5:0 polite @1.0"},
        {"書きますのよ\t쓴답니다\tend", "ますのよ 4답니다 S5:0 polite @1.0"},
        {"書けば\t쓰면\tpat", "2면 S530 @1.0"},
        {"書いた\t씀\tend\tregister=하오", "B S560 register=5 @1.0"},
        {"高かった\t높았어\tend", "かった 5어 T500 @1.0"},
        {"高いです\t높습니다\tend", "いです 7니다 T500 polite @1.0"},
        {"静かだにゃ\t조용하다냥\tend", "だにゃ 1다냥 U500 @1.0"},
        {"静かなら\t조용하면\tpat", "なら 2면 U500 @1.0"},
        {"静かだから\t조용하니까\tend", "だから 2니까 U500 @1.0"},
        // 하오 takes 으 after a consonant stem (괜찮으오), as the verb model does (읽으오).
        {"静かでござる\t조용하오\tend", "でござる 2오 U500 @1.0"},
        {"学生だにゃ\t학생이다냥\tend", "だにゃ (이)다냥 P200 @1.0"},
        {"学生でござる\t학생이오\tend", "でござる 이오 P200 @1.0"},
        {"学生でしてよ\t학생이랍니다\tend", "でしてよ (이)랍니다 P200 polite @1.0"},
        {"学生は\t학생은 좋아\tpat", "は (은)는 좋아 P200 @1.0"},
        {"分かる\t알다\tv5\tneg=모르다", "分か &K6@ M428 neg=&LS@ @1.0"},
    };
    for (const Case& c : cases) {
        Parsed p;
        Run(p, c.in);
        HD_CHECK_EQ(Errors(p.out.errors), std::string());
        HD_CHECK_EQ(ShowEndings(p.out.entries), std::string(c.want));
    }
}

// A builtin row that does not compile makes every dictionary load fail.
HD_TEST(userdict_builtin_tables_compile) {
    for (const char* name : {"endings.tsv", "patterns.tsv", "phrases.tsv", "sound.tsv"}) {
        for (const std::string& text : {dict::BuiltinText(name), dict::ExtraBuiltinText(name)}) {
            Parsed p;
            Run(p, text, std::string_view(name) == "sound.tsv");
            HD_CHECK_EQ(Errors(p.out.errors), std::string());
        }
    }
}

HD_TEST(userdict_kana_spellings) {
    // Without base senses a kana line gives nothing; comments and short lines are
    // skipped.
    dict::CodeTable codes;
    const dict::Dict empty;
    HD_CHECK(KanaSpellings("// c\nあい\t愛\t\nx\n", empty, codes).empty());
}

HD_TEST(userdict_eu_imperative_model) {
    Parsed p;
    Run(p, "書けって\t쓰라고\tpat\n書きながら\t쓰면서\tpat\n高いなら\t높다면\tpat");
    HD_REQUIRE(p.out.errors.empty());
    HD_REQUIRE(p.out.entries.size() == 3);
    HD_CHECK(p.out.entries[0].euImperative);
    HD_CHECK_EQ(p.out.entries[0].target, "1라고");
    HD_CHECK(!p.out.entries[1].euImperative);
    HD_CHECK(!p.out.entries[2].euImperative);
}

HD_TEST(userdict_accepted_source_count) {
    const struct {
        const char* in;
        std::size_t accepted, entries, errors;
    } cases[] = {
        {"\n# comment\n // comment\n", 0, 0, 0},
        {"猫\t고양이\tn", 1, 1, 0},
        {"ゆっくり\t천천히\tadv", 1, 2, 0},
        {"猫\t고양이\tn\thidden", 1, 1, 0},
        {"たん\t\thon", 1, 1, 0},
        {"猫\t고양이\tn\n犬\t\tn", 1, 1, 1},
        {"猫\t고양이\tVHn\nゆっくり\t천천히\tadv", 1, 2, 1},
        {"猫\t고양이\tQPNVHn", 0, 0, 1},
    };
    for (const auto& c : cases) {
        Parsed normal, counted;
        Context normalCtx{&normal.codes, &normal.families, nullptr};
        Context countedCtx{&counted.codes, &counted.families, nullptr};
        std::vector<SourceEntry> normalRows, countedRows;
        normal.out = Parse(c.in, 0, normalCtx, &normalRows);
        std::size_t accepted = 99;
        counted.out = Parse(c.in, 0, countedCtx, &countedRows, &accepted);
        HD_CHECK_EQ(accepted, c.accepted);
        HD_CHECK_EQ(accepted, countedRows.size());
        HD_CHECK_EQ(counted.out.entries.size(), c.entries);
        HD_CHECK_EQ(counted.out.errors.size(), c.errors);
        HD_CHECK_EQ(Show(counted.out.entries), Show(normal.out.entries));
        HD_CHECK_EQ(Errors(counted.out.errors), Errors(normal.out.errors));
        HD_REQUIRE(countedRows.size() == normalRows.size());
        for (std::size_t i = 0; i < countedRows.size(); i++) {
            HD_CHECK_EQ(countedRows[i].ja, normalRows[i].ja);
            HD_CHECK_EQ(countedRows[i].ko, normalRows[i].ko);
            HD_CHECK_EQ(countedRows[i].pos, normalRows[i].pos);
            HD_CHECK_EQ(countedRows[i].options, normalRows[i].options);
        }
    }
}

HD_TEST(userdict_accepted_source_count_per_call) {
    Parsed p;
    Context ctx{&p.codes, &p.families, nullptr};
    std::size_t accepted = 99;
    p.out = Parse("ゆっくり\t천천히\tadv\n猫\t고양이\tn\thidden", 0, ctx, nullptr, &accepted);
    HD_CHECK_EQ(accepted, std::size_t{2});
    HD_CHECK_EQ(p.out.entries.size(), std::size_t{3});
    HD_CHECK(p.out.errors.empty());

    p.out = Parse("\n# comment\n猫\t고양이\tn\tbogus", 1, ctx, nullptr, &accepted);
    HD_CHECK_EQ(accepted, std::size_t{0});
    HD_CHECK(p.out.entries.empty());
    HD_CHECK_EQ(p.out.errors.size(), std::size_t{1});

    std::vector<SourceEntry> rows{{"既存", "기존", "n", ""}};
    p.out = Parse("猫\t고양이\tn", 2, ctx, &rows, &accepted);
    HD_CHECK_EQ(accepted, std::size_t{1});
    HD_CHECK_EQ(rows.size(), std::size_t{2});
    HD_CHECK(p.out.errors.empty());
}

HD_TEST(userdict_validated_source_rows) {
    const std::string text = "\xEF\xBB\xBF# comment\n  // comment\n"
        "学生\t동료\tn\tprio=3 force\t// trailing\n"
        "学生\t학우\tn\tprio=1\n蒼月\t아오츠키\tsurname\tprio=2\r\n"
        "勉強する\t공부하다\tv\n手伝う\t돕다\tv:ㅂ\nたん\t\thon\n"
        "猫\t냥이\tn\tbogus\n";
    Parsed normal, captured;
    Run(normal, text);
    Context context{&captured.codes, &captured.families, nullptr};
    std::vector<SourceEntry> rows;
    captured.out = Parse(text, 0, context, &rows);
    HD_CHECK_EQ(Show(captured.out.entries), Show(normal.out.entries));
    HD_CHECK_EQ(Errors(captured.out.errors), Errors(normal.out.errors));
    HD_REQUIRE(rows.size() == 6);
    HD_CHECK_EQ(rows[0].ja, "学生");
    HD_CHECK_EQ(rows[0].ko, "동료");
    HD_CHECK_EQ(rows[0].options, "prio=3 force");
    HD_CHECK_EQ(rows[1].ja, "学生");
    HD_CHECK_EQ(rows[1].options, "prio=1");
    HD_CHECK_EQ(rows[2].pos, "surname");
    HD_CHECK_EQ(rows[3].ja, "勉強する");
    HD_CHECK_EQ(rows[3].ko, "공부하다");
    HD_CHECK_EQ(rows[3].pos, "v");
    HD_CHECK_EQ(rows[4].pos, "v:ㅂ");
    HD_CHECK_EQ(rows[5].pos, "hon");
    HD_CHECK(rows[5].ko.empty());
    HD_CHECK(captured.out.entries.size() > rows.size());
    HD_REQUIRE(captured.out.errors.size() == 1);
    HD_CHECK_EQ(captured.out.errors[0].line, 9);
}
