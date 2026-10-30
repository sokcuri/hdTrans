#include "core/dict/codes.h"
#include "core/dict/dict.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hdtest.h"

using hdTrans::dict::Codes;
using hdTrans::dict::CodeTable;
using hdTrans::dict::DictCode;
using hdTrans::dict::GramCodes;
using hdTrans::dict::MarkerClassError;
using hdTrans::dict::PartCode;
using hdTrans::dict::PosCodes;
using hdTrans::dict::SemCodes;
using hdTrans::dict::WordClass;

namespace {

void CloseCodes(CodeTable& table) {
    for (std::size_t i = 0; i < table.Size(); i++) {
        table.SetWithXXX(static_cast<int32_t>(i));
        table.SetAsPlain(static_cast<int32_t>(i));
    }
}

void CheckCodesUnchanged(const Codes& got, const Codes& want) {
    HD_CHECK(got.part == want.part);
    HD_CHECK_EQ(got.partText, want.partText);
    HD_CHECK(got.sem.Text() == want.sem.Text());
    HD_CHECK(got.gram.Text() == want.gram.Text());
    HD_CHECK(got.dict.Text() == want.dict.Text());
    HD_CHECK(got.withXXX == want.withXXX);
    HD_CHECK(got.asPlain == want.asPlain);
}

std::string Pos(std::string_view pos) {
    std::string_view part, sem, gram;
    if (!PosCodes(pos, part, sem, gram)) return "unknown";
    return std::string(part) + "|" + std::string(sem) + "|" + std::string(gram);
}

}  // namespace

HD_TEST(PartCodeBytes) {
    const PartCode p("M429");
    HD_CHECK_EQ(p.Tag(), 'M');
    HD_CHECK_EQ(p.ClassDigit(), '4');
    HD_CHECK_EQ(p.Sub(), '2');
    HD_CHECK_EQ(p.Kind(), '9');
    HD_CHECK(p.Class() == WordClass::Verb);
    HD_CHECK_EQ(p.VerbKind(), 9);
    HD_CHECK_EQ(std::string(p.Text()), "M429");
    HD_CHECK_EQ(std::string(p.Code()), "429");
    HD_CHECK(p.TagIn("MHG"));
    HD_CHECK(!p.TagIn("NIK"));
    HD_CHECK(p.LetterTag());

    // Kinds outside the digits keep their arithmetic: ')' is -7, '?' is 15.
    HD_CHECK_EQ(PartCode("H7U)").VerbKind(), -7);
    HD_CHECK_EQ(PartCode("M42?").VerbKind(), 15);
    // An ending's branch reaches 'A' (17).
    HD_CHECK_EQ(PartCode("S5A0").EndingBranch(), 17);
    HD_CHECK(PartCode("1110").Class() == WordClass::Noun);
    HD_CHECK(!PartCode("1110").LetterTag());
    HD_CHECK(PartCode("NA00").Class() == WordClass::Other);
}

HD_TEST(PartCodeOddLengths) {
    const PartCode empty;
    HD_CHECK(empty.Empty());
    HD_CHECK_EQ(empty.At(0), 0);
    HD_CHECK(empty.Class() == WordClass::Other);
    HD_CHECK(!empty.TagIn("NIK"));
    HD_CHECK_EQ(std::string(empty.Code()), "");

    // A missing class byte counts as 0 for the class comparisons.
    const PartCode one("N");
    HD_CHECK_EQ(one.Size(), size_t{1});
    HD_CHECK_EQ(one.At(1), 0);
    HD_CHECK(one.SameClass(PartCode("M")));
    HD_CHECK(!one.SameClass(PartCode("N1")));

    // A longer text keeps its length and its first four bytes.
    const PartCode longer("N1100X");
    HD_CHECK_EQ(longer.Size(), size_t{6});
    HD_CHECK_EQ(std::string(longer.Text()), "N110");
    HD_CHECK(!(longer == PartCode("N110")));

    // Bytes are C chars: a byte above 0x7F sorts before the letters.
    const PartCode high("\xEDN10");
    HD_CHECK(high.At(0) < PartCode("A110").At(0));
    HD_CHECK(!high.LetterTag());
}

HD_TEST(SemCodesKeepTheText) {
    // A place sense written with a space: the chunks are "QCT" and " VP", the rest "l".
    const SemCodes place("QCT VPl");
    HD_CHECK_EQ(std::string(place.Text()), "QCT VPl");
    HD_CHECK(!place.Aligned());
    HD_CHECK_EQ(place.Count(), size_t{2});
    HD_CHECK_EQ(std::string(place.At(1)), " VP");
    HD_CHECK_EQ(std::string(place.Rest()), "l");
    HD_CHECK(place.Contains("VPl"));
    HD_CHECK(!place.HasCode("VPl"));
    HD_CHECK(place.HasCode("QCT"));

    // The first code is not any code.
    const SemCodes person("VHnQPN");
    HD_CHECK(!person.HasPrefix("QPN"));
    HD_CHECK(person.Contains("QPN"));
    HD_CHECK(SemCodes("QPNVHn").HasPrefix("QPN"));

    // QAA..QAH is a byte range of whole codes.
    HD_CHECK(SemCodes("VSsQAC").HasCodeIn("QAA", "QAH"));
    HD_CHECK(!SemCodes("VSsQAI").HasCodeIn("QAA", "QAH"));
    HD_CHECK(!SemCodes("VQAC").HasCodeIn("QAA", "QAH"));

    SemCodes x("QPN");
    x.Append("XXX");
    HD_CHECK_EQ(std::string(x.Text()), "QPNXXX");
    HD_CHECK_EQ(std::string(x.Last()), "XXX");
}

HD_TEST(GramCodesOddTexts) {
    // A three-byte fallback code: Nj is in it, Nd is not.
    const GramCodes njd("Njd");
    HD_CHECK(njd.Contains("Nj"));
    HD_CHECK(!njd.Contains("Nd"));
    HD_CHECK_EQ(std::string(njd.Rest()), "d");

    // A trailing space stays.
    HD_CHECK_EQ(std::string(GramCodes("Wb ").Text()), "Wb ");
    HD_CHECK(GramCodes("WaWb").HasPrefix("Wa"));
    HD_CHECK(!GramCodes("WbWa").HasPrefix("Wa"));
    // The odd rule chunk "n" is true for any code holding a lower-case n.
    HD_CHECK(GramCodes("PnHd").Contains("n"));

    // The passive rewrite relies on Ka being last.
    GramCodes voice("HdKa");
    HD_CHECK(voice.CutSuffix("Ka"));
    voice.Append("Pi");
    HD_CHECK_EQ(std::string(voice.Text()), "HdPi");
    GramCodes notLast("KaHd");
    HD_CHECK(!notLast.CutSuffix("Ka"));
    HD_CHECK_EQ(std::string(notLast.Text()), "KaHd");

    // The digit after the first J at any offset, when 0..7.
    HD_CHECK_EQ(GramCodes("J1J2").PunctDigit(), '1');
    HD_CHECK_EQ(GramCodes("NJ2").PunctDigit(), '2');
    HD_CHECK_EQ(GramCodes("JsJ2").PunctDigit(), 0);
    HD_CHECK_EQ(GramCodes("J8").PunctDigit(), 0);
    HD_CHECK_EQ(GramCodes("WaJ").PunctDigit(), 0);
    HD_CHECK_EQ(GramCodes("Wa").PunctDigit(), 0);
}

HD_TEST(DictCodes) {
    HD_CHECK(DictCode("SO").SourceOnly());
    HD_CHECK(!DictCode("So").SourceOnly());
    HD_CHECK(DictCode("Gn") == std::string_view("Gn"));
    HD_CHECK(!(DictCode("Gn") == DictCode("gn")));
    HD_CHECK(DictCode() == std::string_view(""));
}

HD_TEST(PosTags) {
    // Part codes are kept as written, whatever they hold.
    HD_CHECK_EQ(Pos("N110"), "N110||");
    HD_CHECK_EQ(Pos("M42?"), "M42?||");
    HD_CHECK_EQ(Pos("N1100X"), "N1100X||");
    HD_CHECK_EQ(Pos(""), "||");
    // UniDic, English and IPAdic tags take the codes of the first row naming them.
    HD_CHECK_EQ(Pos("名詞-固有名詞-人名-姓"), "N130|QPN|Wa");
    HD_CHECK_EQ(Pos("noun-proper-name-surname"), "N130|QPN|Wa");
    HD_CHECK_EQ(Pos("名詞-固有名詞-地名-一般"), "N130|QCTVPl|");
    HD_CHECK_EQ(Pos("noun-common-general"), "N110||");
    HD_CHECK_EQ(Pos("副詞"), "E300||");
    HD_CHECK_EQ(Pos("adverb"), "E300||");
    HD_CHECK_EQ(Pos("verb-general,godan-ra"), "M428||");
    HD_CHECK_EQ(Pos("動詞-一般,五段-ラ行"), "M428||");
    HD_CHECK_EQ(Pos("動詞-一般,五段-ラ行,終止形-一般"), "M428||");
    HD_CHECK_EQ(Pos("verb-general,godan-ra,terminal-general"), "M428||");
    HD_CHECK_EQ(Pos("verb-general,godan-ra,*"), "M428||");
    HD_CHECK_EQ(Pos("verb-general,godan-ra,連用形-一般"), "unknown");
    // A cType of "*" is no cType.
    HD_CHECK_EQ(Pos("adverb,*"), "E300||");
    // A closed class carries its fine class in the grammar codes.
    HD_CHECK_EQ(Pos("助詞-格助詞"), "P202||");
    HD_CHECK_EQ(Pos("particle-case"), "P202||");
    HD_CHECK_EQ(Pos("particle-binding"), "P203||");
    HD_CHECK_EQ(Pos("particle-final"), "P206||");
    HD_CHECK_EQ(Pos("名詞-数詞"), "N112||");
    HD_CHECK_EQ(Pos("aux_symbol-comma"), "N600||");
    HD_CHECK_EQ(Pos("記号-文字"), "N601||");
    HD_CHECK_EQ(Pos("空白"), "N600||");
    // Tags without a part code, unknown tags and lower-case values are unknown.
    HD_CHECK_EQ(Pos("助動詞"), "unknown");
    HD_CHECK_EQ(Pos("foo-bar"), "unknown");
    HD_CHECK_EQ(Pos("n110"), "unknown");
    HD_CHECK_EQ(Pos("N1,10"), "unknown");
}

HD_TEST(MarkerClassErrors) {
    struct Case {
        std::string_view target, part, error;
    };
    const Case cases[] = {
        {"마음에 걸&AA@", "M428", ""},
        {"노골적%D4@", "G8U0", ""},
        {"높$D3@", "H7U0", ""},
        {"학생", "N110", ""},
        {"@", "N110", ""},
        {"마침 $FB@", "M420", "the marker family $FB@ conjugates class 7 (adjective) stems, not the part code M420"},
        {"끝이 $FA@", "K110", "the marker family $FA@ conjugates class 7 (adjective) stems, not the part code K110"},
        {"천&AV@", "N110", "the marker family &AV@ conjugates class 4 (verb) stems, not the part code N110"},
        {"부족&Mf@", "H7U0", "the marker family &Mf@ conjugates class 4 (verb) stems, not the part code H7U0"},
        {"새%CC@", "H7N0", "the marker family %CC@ conjugates class 8 (adjectival noun) stems, not the part code H7N0"},
        {"아주 기$A5@", "H8U0", "the marker family $A5@ conjugates class 7 (adjective) stems, not the part code H8U0"},
        {"&AA@", "", "the marker family &AA@ conjugates class 4 (verb) stems, not the part code "},
        {"마음에 걸&AA@ ", "M428",
         "the marker family &AA@ does not end the target (followed by \" \")"},
        {"예감이 &L4@0", "M428",
         "the marker family &L4@ does not end the target (followed by \"0\")"},
        {"장마가 개&AJ@\"", "M427",
         "the marker family &AJ@ does not end the target (followed by \"\\\"\")"},
        {"21 & 오버", "N110", ""},
        {"1%의 어떤 것", "N110", ""},
    };
    for (const Case& c : cases) HD_CHECK_EQ(MarkerClassError(c.target, PartCode(c.part)), std::string(c.error));
}

HD_TEST(VerbNounErrors) {
    struct Case {
        std::string_view key, target, part, sem, error;
    };
    const Case cases[] = {
        {"抜錨", "출항하기", "N110", "", "the noun target \"출항하기\" is a verb made a noun; write the noun \"출항\""},
        {"不見識", "경솔하기", "I110", "", "the noun target \"경솔하기\" is a verb made a noun; write the noun \"경솔\""},
        {"抜錨", "출항", "N110", "", ""},
        {"職探し", "직장 구하기", "N110", "", ""},
        {"夏期", "하기", "N110", "", ""},
        {"氷河期", "빙하기", "N110", "", ""},
        {"矢作", "야하기", "N130", "", ""},
        {"中萩", "나카하기", "N110", "QCT", ""},
        {"使用", "사용하기", "M439", "", ""},
    };
    for (const Case& c : cases) {
        HD_CHECK_EQ(hdTrans::dict::VerbNounError(c.key, c.target, PartCode(c.part), c.sem), std::string(c.error));
    }
}

HD_TEST(JapaneseTargetErrors) {
    struct Case {
        std::string_view target, error;
    };
    const Case cases[] = {
        {"오보로 개", ""},
        {"마감&AV@", ""},
        {"사이토・아카리", ""},
        {"Ez-8 ○", ""},
        {"오보로改", "the target \"오보로改\" holds the Japanese 改; write it in Korean"},
        {"스ィ르", "the target \"스ィ르\" holds the Japanese ィ; write it in Korean"},
        {"포포포포~ン", "the target \"포포포포~ン\" holds the Japanese ン; write it in Korean"},
        {"키키ｷｷ", "the target \"키키ｷｷ\" holds the Japanese ｷ; write it in Korean"},
        {"케이크ー", "the target \"케이크ー\" holds the Japanese ー; write it in Korean"},
        {"마감〆", "the target \"마감〆\" holds the Japanese 〆; write it in Korean"},
    };
    for (const Case& c : cases) HD_CHECK_EQ(hdTrans::dict::JapaneseTargetError(c.target), std::string(c.error));
}

HD_TEST(CodeTablePrefixTupleIdentity) {
    using namespace std::string_view_literals;
    struct Tuple {
        std::string_view part, sem, gram, dict;
    };
    const Tuple tuples[] = {
        {"N110", "QCT VPl", "Njd", "BZ"},
        {"N1100X", "QPN", "Wa", "BZ"},
        {"N110", "QPN\0VHn"sv, "Wa", "BZ"},
        {"N110\0"sv, "QPN", "Wa", "BZ"},
        {"N110", "QPN", "Wa\0"sv, "BZ"},
        {"N110", "QPN", "Wa", "BZ\0"sv},
        {"N110", "QCTVPl", "Njd", "BZ"},
        {"N1100Y", "QPN", "Wa", "BZ"},
        {"N110", "QPN", "Wa", "BZ"},
        {"N110", "QPN\0VHm"sv, "Wa", "BZ"},
        {"N110", "QPN", "Wa ", "BZ"},
        {"N110", "QPN", "Wa", "Bz"},
    };
    CodeTable root, expected;
    std::vector<int32_t> numbers;
    for (std::size_t i = 0; i < 6; i++) {
        const auto& t = tuples[i];
        numbers.push_back(root.Intern(t.part, t.sem, t.gram, t.dict));
        HD_CHECK_EQ(numbers.back(), expected.Intern(t.part, t.sem, t.gram, t.dict));
    }
    CloseCodes(root);
    CloseCodes(expected);
    const auto base = std::make_shared<const CodeTable>(std::move(root));
    CodeTable table;
    HD_REQUIRE(CodeTable::WithBase(base, table));
    HD_CHECK_EQ(table.Size(), base->Size());
    HD_CHECK_EQ(table.MemoryBytes(), base->MemoryBytes());
    for (std::size_t i = 0; i < std::size(tuples); i++) {
        const auto& t = tuples[i];
        const std::size_t before = table.Size();
        const int32_t number = table.Intern(t.part, t.sem, t.gram, t.dict);
        HD_CHECK_EQ(number, expected.Intern(t.part, t.sem, t.gram, t.dict));
        if (i < 6) {
            HD_CHECK_EQ(number, numbers[i]);
            HD_CHECK(table.At(number) == base->At(number));
            HD_CHECK_EQ(table.Size(), before);
            HD_CHECK_EQ(table.MemoryBytes(), base->MemoryBytes());
        } else {
            numbers.push_back(number);
            HD_CHECK_EQ(static_cast<std::size_t>(number), before);
            HD_CHECK_EQ(table.Size(), before + 1);
        }
        const Codes& c = *table.At(number);
        HD_CHECK(c.partText == t.part);
        HD_CHECK(c.sem.Text() == t.sem);
        HD_CHECK(c.gram.Text() == t.gram);
        HD_CHECK(c.dict.Text() == t.dict);
    }
    CloseCodes(table);
    CloseCodes(expected);
    const std::size_t size = table.Size();
    HD_REQUIRE(size == expected.Size());
    for (std::size_t i = 0; i < std::size(tuples); i++) {
        const auto& t = tuples[i];
        HD_CHECK_EQ(table.Intern(t.part, t.sem, t.gram, t.dict), numbers[i]);
    }
    for (std::size_t i = 0; i < size; i++) {
        const Codes& c = *expected.At(static_cast<int32_t>(i));
        HD_CHECK_EQ(table.Intern(c.partText, c.sem.Text(), c.gram.Text(), c.dict.Text()), static_cast<int32_t>(i));
        const Codes* actual = table.At(static_cast<int32_t>(i));
        HD_REQUIRE(c.withXXX != nullptr && c.asPlain != nullptr);
        const int32_t xxx = table.Intern(c.withXXX->partText, c.withXXX->sem.Text(), c.withXXX->gram.Text(), c.withXXX->dict.Text());
        const int32_t plain = table.Intern(c.asPlain->partText, c.asPlain->sem.Text(), c.asPlain->gram.Text(), c.asPlain->dict.Text());
        HD_CHECK(table.At(xxx) == actual->withXXX);
        HD_CHECK(table.At(plain) == actual->asPlain);
    }
    HD_CHECK_EQ(table.Size(), size);
}

HD_TEST(CodeTablePrefixRewritesDoNotChangeBase) {
    CodeTable root;
    root.Intern("N110", "QPNXXX", "Wa", "BZ");
    root.Intern("M429", "VSs", "Hd", "GN");
    CloseCodes(root);
    const auto base = std::make_shared<const CodeTable>(std::move(root));
    std::vector<Codes> saved;
    for (std::size_t i = 0; i < base->Size(); i++) saved.push_back(*base->At(static_cast<int32_t>(i)));
    const std::size_t memory = base->MemoryBytes();
    CodeTable table;
    HD_REQUIRE(CodeTable::WithBase(base, table));
    for (std::size_t i = 0; i < base->Size(); i++) {
        table.SetWithXXX(static_cast<int32_t>(i));
        table.SetAsPlain(static_cast<int32_t>(i));
        HD_CHECK(table.At(static_cast<int32_t>(i)) == base->At(static_cast<int32_t>(i)));
    }
    HD_CHECK_EQ(table.Size(), base->Size());
    HD_CHECK_EQ(table.MemoryBytes(), memory);
    struct Case {
        std::string_view part, sem, gram, dict;
        bool xxxInBase, plainInBase;
    };
    const Case cases[] = {
        {"N110", "QPN", "Wa", "BZ", true, false},
        {"M429", "VSs", "Hd", "BZ", false, true},
        {"E300", "QCT", "Njd", "BZ", false, false},
        {"N130", "QPNXXX", "Wa", "GN", false, false},
    };
    for (const auto& t : cases) {
        const int32_t number = table.Intern(t.part, t.sem, t.gram, t.dict);
        table.SetWithXXX(number);
        table.SetAsPlain(number);
        const Codes* c = table.At(number);
        const std::string sem = c->sem.Contains("XXX") ? std::string(t.sem) : std::string(t.sem) + "XXX";
        const int32_t xxx = table.Intern(t.part, sem, t.gram, t.dict);
        const int32_t plain = table.Intern(t.part, t.sem, t.gram, "GN");
        HD_CHECK(c->withXXX == table.At(xxx));
        HD_CHECK(c->asPlain == table.At(plain));
        HD_CHECK_EQ(static_cast<std::size_t>(xxx) < base->Size(), t.xxxInBase);
        HD_CHECK_EQ(static_cast<std::size_t>(plain) < base->Size(), t.plainInBase);
        HD_CHECK(c->withXXX->withXXX == c->withXXX);
        HD_CHECK(c->asPlain->asPlain == c->asPlain);
        if (c->sem.Contains("XXX")) HD_CHECK(c->withXXX == c);
        if (c->dict == "GN") HD_CHECK(c->asPlain == c);
    }
    CloseCodes(table);
    HD_CHECK_EQ(base->Size(), saved.size());
    HD_CHECK_EQ(base->MemoryBytes(), memory);
    for (std::size_t i = 0; i < saved.size(); i++) CheckCodesUnchanged(*base->At(static_cast<int32_t>(i)), saved[i]);
}

HD_TEST(CodeTableRejectsChainedBaseWithoutChangingOutput) {
    CodeTable root;
    root.Intern("N110", "QPN", "Wa", "BZ");
    CloseCodes(root);
    const auto base = std::make_shared<const CodeTable>(std::move(root));
    std::vector<std::shared_ptr<const CodeTable>> chained;
    for (bool addOverflow : {false, true}) {
        CodeTable table;
        HD_REQUIRE(CodeTable::WithBase(base, table));
        if (addOverflow) {
            table.Intern("M429", "VSs", "Hd", "BZ");
            CloseCodes(table);
        }
        chained.push_back(std::make_shared<const CodeTable>(std::move(table)));
    }
    const std::shared_ptr<const CodeTable> previousBases[] = {nullptr, base};
    for (const auto& previous : previousBases) {
        CodeTable out;
        HD_REQUIRE(CodeTable::WithBase(previous, out));
        out.Intern("E300", "QCT", "Njd", "BZ");
        CloseCodes(out);
        std::vector<const Codes*> pointers;
        std::vector<Codes> saved;
        for (std::size_t i = 0; i < out.Size(); i++) {
            pointers.push_back(out.At(static_cast<int32_t>(i)));
            saved.push_back(*pointers.back());
        }
        const std::size_t memory = out.MemoryBytes();
        for (const auto& invalid : chained) {
            HD_CHECK(!CodeTable::WithBase(invalid, out));
            HD_CHECK_EQ(out.Size(), saved.size());
            HD_CHECK_EQ(out.MemoryBytes(), memory);
            for (std::size_t i = 0; i < saved.size(); i++) {
                HD_CHECK(out.At(static_cast<int32_t>(i)) == pointers[i]);
                CheckCodesUnchanged(*out.At(static_cast<int32_t>(i)), saved[i]);
            }
        }
        HD_REQUIRE(CodeTable::WithBase(nullptr, out));
        HD_CHECK_EQ(out.Size(), std::size_t{0});
        HD_CHECK_EQ(out.MemoryBytes(), std::size_t{0});
        HD_CHECK_EQ(out.Intern("E300", "QCT", "Njd", "BZ"), 0);
        HD_REQUIRE(CodeTable::WithBase(base, out));
        HD_CHECK_EQ(out.Size(), base->Size());
        HD_CHECK(out.At(0) == base->At(0));
    }
    auto alias = std::make_shared<CodeTable>();
    const int32_t number = alias->Intern("N130", "QPN", "Wa", "GN");
    CloseCodes(*alias);
    const Codes* pointer = alias->At(number);
    const Codes saved = *pointer;
    const std::size_t size = alias->Size(), memory = alias->MemoryBytes();
    HD_CHECK(!CodeTable::WithBase(alias, *alias));
    HD_CHECK_EQ(alias->Size(), size);
    HD_CHECK_EQ(alias->MemoryBytes(), memory);
    HD_CHECK(alias->At(number) == pointer);
    CheckCodesUnchanged(*alias->At(number), saved);
}

HD_TEST(CodeTableAddressesSurviveGrowthAndMoves) {
    CodeTable root;
    root.Intern("N110", "QPN", "Wa", "BZ");
    CloseCodes(root);
    const std::shared_ptr<const CodeTable> bases[] = {
        nullptr, std::make_shared<const CodeTable>(), std::make_shared<const CodeTable>(std::move(root)),
    };
    for (const auto& base : bases) {
        CodeTable table;
        HD_REQUIRE(CodeTable::WithBase(base, table));
        table.Intern("E300", "QCT", "Njd", "BZ");
        CloseCodes(table);
        std::vector<const Codes*> pointers;
        std::vector<Codes> saved;
        for (std::size_t i = 0; i < table.Size(); i++) {
            pointers.push_back(table.At(static_cast<int32_t>(i)));
            saved.push_back(*pointers.back());
        }
        for (int phase = 0; phase < 2; phase++) {
            const std::size_t first = table.Size();
            for (int i = 0; i < 2400; i++) {
                const std::string sem = "QPN" + std::to_string(phase * 2400 + i);
                const int32_t number = table.Intern("N130", sem, "Wa", "BZ");
                HD_CHECK_EQ(static_cast<std::size_t>(number), first + static_cast<std::size_t>(i));
            }
            CloseCodes(table);
            CodeTable moved(std::move(table));
            table = CodeTable();
            table.Intern("S5A0", "", "", "GN");
            table = std::move(moved);
            for (std::size_t i = 0; i < saved.size(); i++) {
                const Codes* c = table.At(static_cast<int32_t>(i));
                HD_CHECK(c == pointers[i]);
                CheckCodesUnchanged(*c, saved[i]);
                HD_CHECK_EQ(table.Intern(c->partText, c->sem.Text(), c->gram.Text(), c->dict.Text()), static_cast<int32_t>(i));
            }
            for (std::size_t i = saved.size(); i < table.Size(); i++) {
                pointers.push_back(table.At(static_cast<int32_t>(i)));
                saved.push_back(*pointers.back());
            }
        }
    }
}

HD_TEST(CodeTableOwnsBaseLifetimeAndCountsItsMemoryOnce) {
    CodeTable root;
    const int32_t number = root.Intern("N1100-long-part-text-for-allocation", "QPNVHnQCTVP1VP2VP3VP4VP5", "WaWbWcWdWeWfWgWhWiWjWkWl", "BZ");
    CloseCodes(root);
    auto base = std::make_shared<const CodeTable>(std::move(root));
    const std::weak_ptr<const CodeTable> held = base;
    const Codes* pointer = base->At(number);
    const Codes expected = *pointer;
    const std::size_t baseMemory = base->MemoryBytes();
    {
        CodeTable table;
        HD_REQUIRE(CodeTable::WithBase(base, table));
        HD_CHECK_EQ(table.MemoryBytes(), baseMemory);
        const int32_t duplicate = table.Intern(pointer->partText, pointer->sem.Text(), pointer->gram.Text(), pointer->dict.Text());
        HD_CHECK_EQ(duplicate, number);
        HD_CHECK(table.At(duplicate) == pointer);
        HD_CHECK_EQ(table.MemoryBytes(), baseMemory);
        CodeTable owned;
        owned.Intern("M429", "VSs", "Hd", "BZ");
        table.Intern("M429", "VSs", "Hd", "BZ");
        CloseCodes(owned);
        CloseCodes(table);
        HD_CHECK_EQ(table.MemoryBytes(), baseMemory + owned.MemoryBytes());
        base.reset();
        HD_CHECK(!held.expired());
        CodeTable moved(std::move(table));
        HD_CHECK(moved.At(number) == pointer);
        CheckCodesUnchanged(*moved.At(number), expected);
        HD_CHECK_EQ(moved.MemoryBytes(), baseMemory + owned.MemoryBytes());
    }
    HD_CHECK(held.expired());
}

HD_TEST(GramCodesPunctuationDigits) {
    const struct { const char* attrs; int digit; } cases[] = {
        {"J0", '0'}, {"J1", '1'}, {"J2", '2'}, {"J3", '3'}, {"J4", '4'},
        {"J5", '5'}, {"J6", '6'}, {"J7", '7'}, {"J8", 0},
        {"NJ7Wa", '7'}, {"J7J1", '7'}, {"J8J7", 0}, {"JsJ7", 0},
        {"J", 0}, {"", 0}, {"J/", 0}, {"J9", 0}, {"J00", '0'},
    };
    for (const auto& c : cases) HD_CHECK_EQ(static_cast<int>(GramCodes(c.attrs).PunctDigit()), c.digit);
}

HD_TEST(PartCodeProperNoun) {
    using hdTrans::dict::PartCode;
    HD_CHECK(PartCode("N130").ProperNoun());
    HD_CHECK(PartCode("I130").ProperNoun());
    HD_CHECK(PartCode("K130").ProperNoun());
    HD_CHECK(!PartCode("N110").ProperNoun());
    HD_CHECK(!PartCode("M430").ProperNoun());
    HD_CHECK(!PartCode("N1").ProperNoun());
    HD_CHECK(!PartCode("").ProperNoun());
}
