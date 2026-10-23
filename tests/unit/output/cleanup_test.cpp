#include "core/output/cleanup.h"

#include <string>

#include "core/output/particle.h"
#include "hdtest.h"

using namespace hdTrans::output;

namespace {

Piece Word(const char* s) { return {s, SpanWord}; }
Piece Tag(const char* s) { return {s, SpanOpen}; }
Piece Text(const char* s) { return {s, SpanText}; }

std::string Show(char c) { return c == 0 ? std::string("0") : std::string(1, c); }

}  // namespace

HD_TEST(output_remove_glosses) {
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {
        {"하면(자)", "하면"},
        {"(분)편", "편"},
        {"(곳)중", "중"},
        {"(듣)묻는다", "묻는다"},
        {"인연(테)", "인연"},
        {"(웃음)", "(웃음)"},            // not attached to a reading
        {"좋아 (웃음)", "좋아 (웃음)"},  // likewise
        {"(은)는", "(은)는"},            // alternating particle
        {"(이)가", "(이)가"},
        {"Q(OP)", "Q(OP)"},
    };
    for (const auto& c : cases) {
        std::string s = c.in;
        RemoveGlosses(s);
        HD_CHECK_EQ(s, std::string(c.want));
    }
}

HD_TEST(output_cleanup_text) {
    struct Case {
        Pieces in;
        const char* want;
    };
    const Case cases[] = {
        {{Text("(은)는, 은")}, "는, 은"},                   // nothing decides: the vowel form
        {{Text("정답! 7…! (와)과 환성이")}, "정답! 7…! 와 환성이"},  // ! ends the search
        {{Text("정답 7…(와)과")}, "정답 7…과"},               // 칠
        {{Text("「그」 (와)과")}, "「그」 와"},
        {{Text("3 (와)과")}, "3 과"},
        {{Text("x10 (을)를")}, "x10 을"},
        {{Text("얼음 (와) 같은")}, "얼음 과 같은"},
        {{Text("고양이 (와) 같은")}, "고양이 와 같은"},
        {{Text("엄(어) 주자")}, "엄어 주자"},
        {{Text("묵물어 (어) 주어")}, "묵물어 주어"},
        {{Text("급행(이)")}, "급행(이)"},  // a gloss, not an alternation
        {{Text("길 (으)로")}, "길 로"},
        {{Text("Hi! (은)는")}, "Hi! 는"},
        {{Text("Bob (은)는")}, "Bob 은"},
        {{Word("이메일"), Text(" (이)가")}, "이메일 이"},
        {{Word("🍣"), Tag("<b>"), Text(" (이)가")}, "🍣<b> 이"},
        {{Word("철수"), Tag("<b>"), Text(" (이)가")}, "철수<b> 가"},
        {{Word("철수"), Text(" (어) 주어")}, "철수 주어"},
        {{Text("「, 조금!」")}, "「조금!」"},
        {{Text(", 바보")}, "바보"},
        {{Tag("<b>"), Text(" 「 ,, 아")}, "<b> 「 아"},
        {{Text("아, 그")}, "아, 그"},
        {{Text("아\n, 그")}, "아\n그"},
        {{Text("정답(이)고 뭐고")}, "정답이고 뭐고"},
        {{Text("이유(이)고 뭐고")}, "이유고 뭐고"},
        {{Text("길(이)고 뭐고")}, "길이고 뭐고"},
        {{Word("정답"), Text("(이)고 뭐고")}, "정답이고 뭐고"},
        {{Word("이유"), Text("(이)고 뭐고")}, "이유고 뭐고"},
        {{Word("길"), Text("(이)고 뭐고")}, "길이고 뭐고"},
        {{Text("(이)고")}, "고"},
        {{Text("정답! (이)고")}, "정답! 고"},
        {{Text("정답이고 이유고")}, "정답이고 이유고"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(String(CleanupText(c.in)), std::string(c.want));
}

HD_TEST(output_stand_in) {
    struct Case {
        const char* in;
        char final;
        const char* standIn;
    };
    const Case cases[] = {
        {"김철수", 'v', "김철수"}, {"이메일", 'l', "이메일"}, {"ᄒᆞᆫ글", 'l', "ᄒᆞᆫ글"},
        {"똠방각하", 'v', "똠방각하"}, {"ㅋㅋ", 'c', "각"}, {"ㅏ", 'v', "가"},
        {"ㄹ", 'l', "갈"}, {"🍣", 'c', "각"},
        // A particle mark decides for the masked word before it.
        {"섹○\uE001", 'v', "가"}, {"가○\uE002", 'c', "각"}, {"애○\uE003", 'l', "갈"},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(Show(SpanFinal(c.in)), Show(c.final));
        HD_CHECK_EQ(std::string(StandIn(c.in)), std::string(c.standIn));
    }
}

HD_TEST(output_squeeze_spaces) {
    struct Case {
        const char* in;
        const char* want;
    };
    const Case cases[] = {{"a  b", "a b"}, {"  a   b ", " a b "}, {"ab", "ab"}};
    for (const auto& c : cases) {
        std::string s = c.in;
        SqueezeSpaces(s);
        HD_CHECK_EQ(s, std::string(c.want));
    }
}

HD_TEST(output_particle_class) {
    struct Case {
        const char* in;
        char want;
    };
    const Case cases[] = {
        {"그", 'v'}, {"책", 'c'}, {"길", 'l'}, {"「그」", 'v'}, {"『책』…", 'c'}, {"그 ", 'v'},
        {"3", 'c'}, {"1", 'l'}, {"2", 'v'}, {"10", 'c'}, {"100", 'c'}, {"1,000", 'c'}, {"20000", 'c'},
        {"1000000000000", 'v'}, {"0", 'c'}, {"3.5", 'v'}, {"3.10", 'c'}, {"７", 'l'}, {"x3", 'c'},
        {"L", 'l'}, {"M", 'c'}, {"N", 'c'}, {"R", 'l'}, {"A", 'v'}, {"ABC", 'v'}, {"BGM", 'c'}, {"ｍ", 'c'},
        {"Bob", 'c'}, {"Hi", 'v'}, {"Morning", 'c'}, {"Google", 'l'}, {"Nelson", 'c'}, {"girl", 'l'},
        {"50%", 'v'}, {"2 m", 'v'}, {"5kg", 'c'}, {"3L", 'v'}, {"ネコ", 'v'}, {"ㅋ", 'c'}, {"ㄹ", 'l'}, {"ㅏ", 'v'},
        {"", 0}, {"!", 0}, {"그, ", 0}, {"그. ", 0}, {"「", 0}, {"♪", 0}, {"돈!", 'c'}, {"돈! ", 0}, {"「빵!」", 'c'}, {"정말?", 'l'},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Show(ParticleClass(c.in)), Show(c.want));
}

HD_TEST(output_alternate_particles) {
    struct Case {
        const char* in;
        const char* context;
        const char* want;
    };
    const Case cases[] = {
        {"(와)과", "수 3", "과"},
        {"(으)로", "길", "로"},
        {"(으)로", "x L", "로"},
        {"(으)로", "M", "(으)로"},  // the reading of M depends on the text before it
        {"(으)로", "7", "(으)로"},
        {"」(이)라고", "「그", "」라고"},
        {"(은)는", "", "(은)는"},  // left for CleanupText
        {"사람(이)가", "", "사람이"},
        {"(이)고 뭐고", "정답", "이고 뭐고"},
        {"(이)고 뭐고", "이유", "고 뭐고"},
        {"(이)고 뭐고", "길", "이고 뭐고"},
        {"정답(이)고 이유(이)고", "", "정답이고 이유고"},
        {"(이)고", "", "(이)고"},
        {"(이)고", "정답! ", "(이)고"},
        {"정답이고 이유고", "", "정답이고 이유고"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::AlternateParticles(c.in, c.context), std::string(c.want));
}

HD_TEST(output_fix_spacing) {
    struct Case {
        Pieces in;
        const char* want;
    };
    const Case cases[] = {
        {{Text("아∼아 ,  그 ,")}, "아~아,  그,"},
        {{Text("『무엇?  』 뭐!)")}, "『무엇?』 뭐!)"},
        {{Text("가라앉고 ――, 가 ― ")}, "가라앉고――, 가 ― "},
        {{Text("제１장 ９")}, "제1장 9"},
        {{Text("「시끄럽다 」라고 『가자  』")}, "「시끄럽다」라고 『가자』"},
        {{Text("「(웃음) )")}, "「(웃음) )"},
        {{Text("? "), Tag("<b>"), Text(" 」")}, "? <b> 」"},
        {{Text("했다 "), Tag("<k>"), Text("――")}, "했다<k>――"},
        {{Text("했다 "), Tag("<k>"), Text("가")}, "했다 <k>가"},
    };
    for (const auto& c : cases) {
        Pieces p = c.in;
        FixSpacing(p);
        HD_CHECK_EQ(String(p), std::string(c.want));
    }
}

HD_TEST(output_cleanup_joins) {
    // Sentences of a chunk: a space that starts a sentence replaces the spaces that
    // end the one before. Chunks: joined by one space.
    const std::vector<Sentence> sentences = {
        {0, {Text("그래. ")}},
        {0, {Text(" 가자.  ")}},
        {1, {Text("   좋아")}},
        {1, {Word("철수"), Text(" (은)는")}},
        {3, {Text("  ")}},
        {4, {Tag("<b>")}},
    };
    const Pieces out = Cleanup(sentences);
    HD_CHECK_EQ(String(out), std::string("그래. 가자. 좋아철수 는 <b>"));
    HD_REQUIRE(out.size() == 4);
    HD_CHECK_EQ(static_cast<int>(out[1].span), static_cast<int>(SpanWord));
    HD_CHECK_EQ(out[2].text, std::string(" 는 "));
}
