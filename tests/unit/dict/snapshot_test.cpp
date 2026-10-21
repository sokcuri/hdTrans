#include "core/dict/snapshot.h"

#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "hdtest.h"

using namespace hdTrans::dict;

namespace {

std::string SourceDir() {
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return slash == std::string::npos ? "." : f.substr(0, slash);
}

std::string Folder() { return SourceDir() + "/testdata/folder"; }

std::string Show(const Candidate& c) {
    std::string s = "[" + std::string(c.source) + "] " + std::string(c.target) + " " + c.codes->partText + "|" +
                    std::string(c.codes->sem.Text()) + "|" + std::string(c.codes->gram.Text()) + "|" +
                    std::string(c.codes->dict.Text()) + " r" + std::to_string(c.rule) + " id" + std::to_string(c.id);
    if (c.user) s += c.user->nameLayer ? " name" : " plain";
    return s;
}

// "count N" and a line per candidate.
std::string Show(const Candidates& cs) {
    std::string s = "count " + std::to_string(cs.count);
    for (const Candidate& c : cs.list) s += "\n" + Show(c);
    return s;
}

std::string Show(const std::vector<Candidate>& cs) {
    std::string s;
    for (const Candidate& c : cs) s += (s.empty() ? "" : "\n") + Show(c);
    return s;
}

std::string Targets(const std::vector<Record>& rs) {
    std::string s;
    for (const Record& r : rs) s += (s.empty() ? "" : " ") + std::string(r.target);
    return s;
}

const std::string kHeader = "key\tko\tpos\tsem\tgram\tdict\tid\n";

File MakeFile(std::string name, std::string text) {
    File f;
    f.name = std::move(name);
    f.text = std::move(text);
    return f;
}

// A plain entry with codes of table.
UserEntry Entry(CodeTable& table, std::string key, std::string target, std::string_view part, int line) {
    UserEntry e;
    e.key = std::move(key);
    e.target = std::move(target);
    e.codes = table.At(table.Intern(part, "", "", "GN"));
    e.line = line;
    return e;
}

}  // namespace

// A dictionary from before the rule bodies moved into the rows names rules that no row
// carries. It loads, because the rows are valid, but the load reports it: the engine
// would assemble an empty program and translate as if no local rule existed.
HD_TEST(SnapshotRowsWithoutRuleBodiesAreReported) {
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\trule\tid\n";
    const LoadResult r = Load({MakeFile("base.tsv", header + "の\t의\tP200\t\t\tGN\t12\t1\n")});
    HD_REQUIRE(r.snapshot != nullptr);
    HD_REQUIRE(r.errors.size() == 1);
    HD_CHECK(r.errors[0].kind == LoadErrorKind::MissingRuleBodies);
    HD_CHECK(r.errors[0].message.find("ctx") != std::string::npos);
    // A dictionary whose rows carry the bodies has nothing to report.
    const std::string withCtx = "key\tko\tpos\tsem\tgram\tdict\trule\tctx\tid\n";
    const LoadResult carried =
        Load({MakeFile("base.tsv", withCtx + "の\t의\tP200\t\t\tGN\t12\t||1 S-1== \"x\"\t1\n")});
    HD_REQUIRE(carried.snapshot != nullptr);
    HD_CHECK(carried.errors.empty());
}

HD_TEST(SnapshotFolderLayers) {
    const LoadResult r = LoadFoldersTsv({Folder(), Folder() + "/over"});
    HD_REQUIRE(r.snapshot != nullptr);
    const Snapshot& s = *r.snapshot;
    // The line with an unknown tag is skipped and listed; the load takes place.
    HD_REQUIRE(r.errors.size() == 1);
    HD_CHECK(r.errors[0].kind == LoadErrorKind::UnknownPos);
    HD_CHECK_EQ(r.errors[0].line, 17);
    HD_CHECK(r.errors[0].file.ends_with("base.tsv"));
    HD_CHECK(s.HasDictionary());
    HD_REQUIRE(s.PlainFiles().size() == 1);
    HD_CHECK(s.PlainFiles()[0].name.ends_with("plain.tsv"));
    HD_CHECK(s.PlainFiles()[0].text.starts_with("// A plain dictionary"));
    HD_REQUIRE(s.Warnings().size() == 1);
    HD_CHECK(s.Warnings()[0].ends_with("names.tsv: name senses dropped: 1 keys with 、 or 。, 1 inflected keys, 1 long Korean"));
    HD_CHECK_EQ(s.Base().Len(), size_t{17});
    HD_REQUIRE(s.User() != nullptr);
    HD_CHECK_EQ(s.User()->Len(), size_t{2});

    const LookupContext none;
    // more.tsv shadows the base senses of 学生 of class 1, words.tsv adds one with prio
    // 1, sub/ is not read; user-dictionary records come first by tag.
    HD_CHECK_EQ(Show(s.GetWordInfo("学生です", LookupMode::General, none)),
                "count 7\n"
                "[学] 유저학 E300|||GN r0 id0\n"
                "[学] 학교 I110|||PN r0 id28\n"
                "[学] 배울&AG@ M42;|||GN r0 id27\n"
                "[学生] 유저학생 N110|||GN r0 id0\n"
                "[学生] 학우 N110|||GN r0 id1\n"
                "[学生] 생도 N110|||GN r0 id1\n"
                "[学] 학 N110|||BZ r0 id26");
    // The later folder shadows the class-1 senses of 行 of every earlier file; the verb
    // senses stay, ordered by kind (? 9 8 7).
    HD_CHECK_EQ(Show(s.GetWordInfo("行くの", LookupMode::General, none)),
                "count 5\n"
                "[行く] 가&AE@ M42?|||GN r0 id18\n"
                "[行] 갈&AB@ M429|||GN r0 id5\n"
                "[行] 가&AA@ M428|||GN r0 id3\n"
                "[行] 다닐&AC@ M427|||GN r0 id6\n"
                "[行] 최종행 N110|||GN r0 id1");
    // words.tsv replace shadows; the base place sense of 霧島 takes the respelling of
    // names-places.tsv.
    HD_CHECK_EQ(Show(s.GetWordInfo("東京と", LookupMode::General, none)), "count 1\n[東京] 동경 N130|QCTVPl||GN r0 id2");
    HD_CHECK_EQ(Show(s.GetWordInfo("霧島さん", LookupMode::General, none)),
                "count 2\n"
                "[霧島] 키리시마 N130|QCTVPl||GN r0 id8\n"
                "[霧島] 키리시마 N130|QPN||GN r0 id9");
    // Special codes round-trip; SO senses never match.
    HD_CHECK_EQ(Show(s.GetWordInfo("大阪の", LookupMode::General, none)), "count 1\n[大阪] 오사카 N130|QCT VPl|Wb | r0 id16");
    HD_CHECK_EQ(Show(s.GetWordInfo("の", LookupMode::General, none)),
                "count 2\n[の] 한 P200|||GN r5089 id10\n[の] 의 P200|||GN r12 id11");
    HD_CHECK_EQ(Show(s.GetWordInfo("田中", LookupMode::General, none)), "count 1\n[田中] 다나카 N130|QPNXXX||GN r0 id19");
    HD_CHECK_EQ(Show(s.GetWordInfo("中村", LookupMode::General, none)), "count 1\n[中村] 나카무라 N130|XXXQPN||GN r0 id20");
    // The flag kana files the sense under its reading too; words.tsv adds a verb.
    HD_CHECK_EQ(Show(s.GetWordInfo("はしる", LookupMode::General, none)), "count 1\n[はしる] 달리다 N110|||GN r0 id21");
    HD_CHECK_EQ(Show(s.GetWordInfo("走る", LookupMode::General, none)),
                "count 3\n"
                "[走] 달리&AD@ M428|||GN r0 id13\n"
                "[走] 뛰&AH@ M428|||GN r0 id3\n"
                "[走る] 달리다 N110|||GN r0 id21");
    // Hidden and comment lines are not loaded; the key D is the sentinel of mode 1.
    HD_CHECK_EQ(Show(s.GetWordInfo("隠", LookupMode::General, none)), "count 0");
    HD_CHECK_EQ(Show(s.GetWordInfo("#注", LookupMode::General, none)), "count 0");
    HD_CHECK_EQ(Show(s.GetWordInfo("", LookupMode::AdjectivalNounEnding, none)), "count 1\n[] 1고 U5I1|||GN r0 id12");
    HD_CHECK_EQ(Show(s.GetWordInfo("Dragon", LookupMode::General, none)), "count 0");
    HD_CHECK_EQ(Show(s.GetWordInfo("Dragon", LookupMode::AdjectivalNounEnding, none)), "count 1\n[] 1고 U5I1|||GN r0 id12");

    // Exact lookups return the stored records: tags resolved by the part-of-speech table.
    HD_CHECK_EQ(Show(s.RetrieveRecords("山田", LookupMode::General)), "[山田] 야마다 N130|QPN|Wa|GN r0 id14");
    HD_CHECK_EQ(Show(s.RetrieveRecords("副", LookupMode::General)), "[副] 부 E300|||BZ r0 id25");
    HD_CHECK_EQ(Show(s.RetrieveRecords("", LookupMode::AdjectivalNounEnding)), "[D] 1고 U5I1|||GN r0 id12");
    HD_CHECK_EQ(Show(s.RetrieveRecords("霧島", LookupMode::General)),
                "[霧島] 기리시마 N130|QCTVPl||GN r0 id8\n[霧島] 키리시마 N130|QPN||GN r0 id9");

    // The name layer: a later name file shadows an earlier one; a non-person sense of a
    // respelled key is dropped.
    HD_CHECK_EQ(Targets(s.NameRecords("霧島")), "기리시마");
    HD_CHECK_EQ(Targets(s.NameRecords("京都")), "경도");
    HD_CHECK_EQ(Targets(s.NameRecords("食べるの")), "");
    HD_CHECK_EQ(Targets(s.NameRecords("猫")), "");
    // Names that are no base word take part; a base word only when cued.
    HD_CHECK_EQ(Show(s.GetWordInfo("アリスさん", LookupMode::General, none)), "count 1\n[アリス] 앨리스 N130|QPN||GN r0 id0 name");
    HD_CHECK_EQ(Show(s.GetWordInfo("アリスさん", LookupMode::VerbEnding, none)), "count 0");
    HD_CHECK_EQ(Show(s.GetWordInfo("翔太くんが", LookupMode::General, none)), "count 1\n[翔太] 쇼타 N130|QPN||GN r0 id0 name");
    HD_CHECK_EQ(s.PersonName("カイト"), "카이토");
    HD_CHECK_EQ(s.PersonName("アリス"), "");
    HD_CHECK(s.PersonCueKey("翔太"));
    HD_CHECK(!s.PersonCueKey("アリス"));
    const std::vector<std::string> keys = s.PersonPlaceKeys("霧島と霧島");
    HD_REQUIRE(keys.size() == 1);
    HD_CHECK_EQ(keys[0], "霧島");

    // A key the text confirms takes part with its person senses, dropping the base
    // and user senses of its key and class; persons read a respelled place key with the
    // person spelling.
    KeySet confirmed{"学生"}, persons{"霧島"};
    LookupContext ctx;
    ctx.confirmed = &confirmed;
    ctx.persons = &persons;
    HD_CHECK_EQ(Show(s.GetWordInfo("学生", LookupMode::General, ctx)),
                "count 5\n"
                "[学生] 가쿠세이 N130|QPN||GN r0 id0 name\n"
                "[学] 유저학 E300|||GN r0 id0\n"
                "[学] 학교 I110|||PN r0 id28\n"
                "[学] 배울&AG@ M42;|||GN r0 id27\n"
                "[学] 학 N110|||BZ r0 id26");
    HD_CHECK_EQ(Show(s.GetWordInfo("霧島", LookupMode::General, ctx)),
                "count 2\n"
                "[霧島] 기리시마 N130|QCTVPl||GN r0 id8\n"
                "[霧島] 기리시마 N130|QPN||GN r0 id9");
}

HD_TEST(SnapshotFolderWithoutLaterFolder) {
    const LoadResult r = LoadFoldersTsv({Folder()});
    HD_REQUIRE(r.snapshot != nullptr);
    // more.tsv shadows the base sense 행; words-extra.tsv adds 행차 after it.
    HD_CHECK_EQ(Show(r.snapshot->RetrieveRecords("行", LookupMode::General)),
                "[行] 가&AA@ M428|||GN r0 id3\n"
                "[行] 갈&AB@ M429|||GN r0 id5\n"
                "[行] 다닐&AC@ M427|||GN r0 id6\n"
                "[行] 줄 N110|||GN r0 id2\n"
                "[行] 행차 N110|||GN r0 id1");
}

HD_TEST(SnapshotAdditiveFolderPriority) {
    const std::string header = "key\tko\tpos\tsem\tgram\tdict\tprio\tid\n";
    File base = MakeFile("base/words.tsv", "#! layer=words\n" + header + "黙殺\t곡살\tN110\t\t\tGN\t0\t0\n");
    File extra = MakeFile("extra/words-extra.tsv", "#! layer=words\n" + header + "黙殺\t묵살\tN110\t\t\tGN\t1\t0\n");
    extra.folder = 1;
    File names = MakeFile("extra/names-extra.tsv", "#! layer=names\n" + header + "アメリア\t아멜리아\tN130\tQPN\t\tGN\t0\t0\n");
    names.folder = 1;
    const LoadResult r = Load({base, extra, names});
    HD_REQUIRE(r.snapshot != nullptr);
    const auto records = r.snapshot->RetrieveRecords("黙殺", LookupMode::General);
    HD_REQUIRE(records.size() == 2);
    HD_CHECK_EQ(std::string(records[0].target), "묵살");
    HD_CHECK_EQ(std::string(records[1].target), "곡살");
    HD_CHECK_EQ(Targets(r.snapshot->NameRecords("アメリア")), "아멜리아");
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("アメリアさん", LookupMode::General, {})),
                "count 1\n[アメリア] 아멜리아 N130|QPN||GN r0 id0 name");
}

HD_TEST(SnapshotFileErrors) {
    // A malformed line fails the load.
    std::vector<File> files;
    files.push_back(MakeFile("a/base.tsv", kHeader + "犬\t견\tN110\t\t\tGN\t1\n犬\t견\tN110\t\t\tGN\t1\textra\n犬\t견\tN110\t\t\tGN\t\n"));
    LoadResult r = Load(std::move(files));
    HD_CHECK(r.snapshot == nullptr);
    HD_REQUIRE(r.errors.size() == 2);
    HD_CHECK(r.errors[0].kind == LoadErrorKind::Tsv);
    HD_CHECK(r.errors[0].tsv == TsvErrorKind::TooManyColumns);
    HD_CHECK_EQ(r.errors[0].line, 3);
    HD_CHECK(r.errors[1].tsv == TsvErrorKind::EmptyKeyOrId);
    HD_CHECK_EQ(r.errors[1].file, "a/base.tsv");

    // No column file: no dictionary, the plain files kept.
    files.clear();
    files.push_back(MakeFile("a/plain.tsv", "犬\t개\tn\n"));
    r = Load(std::move(files));
    HD_REQUIRE(r.snapshot != nullptr);
    HD_CHECK(!r.snapshot->HasDictionary());
    HD_CHECK_EQ(r.snapshot->PlainFiles().size(), size_t{1});
}

HD_TEST(SnapshotMarkerClassLines) {
    // Lines whose marker family does not fit the part code are listed; the load takes
    // place with them.
    const LoadResult r = LoadFoldersTsv({SourceDir() + "/testdata/marker_class"});
    HD_REQUIRE(r.snapshot != nullptr);
    struct Want {
        int line;
        std::string_view message;
    };
    const Want want[] = {
        {6, "the marker family $FA@ conjugates class 7 (adjective) stems, not the part code K110"},
        {7, "the marker family &AV@ conjugates class 4 (verb) stems, not the part code N110"},
        {8, "the marker family &A5@ conjugates class 4 (verb) stems, not the part code H7U0"},
    };
    HD_REQUIRE(r.errors.size() == std::size(want));
    for (std::size_t i = 0; i < std::size(want); i++) {
        HD_CHECK(r.errors[i].kind == LoadErrorKind::MarkerClass);
        HD_CHECK(!FailsLoad(r.errors[i].kind));
        HD_CHECK(r.errors[i].file.ends_with("base.tsv"));
        HD_CHECK_EQ(r.errors[i].line, want[i].line);
        HD_CHECK_EQ(r.errors[i].message, std::string(want[i].message));
    }
    HD_CHECK_EQ(Targets(r.snapshot->Base().Records("下卑")), "천&AV@");
    HD_CHECK_EQ(Targets(r.snapshot->Base().Records("あけすけ")), "노골적%D4@");
}

HD_TEST(SnapshotKeysAndUserDictionary) {
    std::vector<File> files;
    files.push_back(MakeFile("base.tsv", "\xEF\xBB\xBF# comment\n" + kHeader +
                                             "犬\t견\tN110\t\t\tGN\t1\n犬\t개과\tN110\t\t\t\t2\n\xFFX\t깨짐\tN110\t\t\tGN\t3\n"
                                             "#犬\t주석\tN110\t\t\tGN\t4\n"));
    files.push_back(MakeFile("userdict.tsv", "#! layer=user\n" + kHeader + "犬\t개\tN110\t\t\t\t1\n"));
    const LoadResult r = Load(std::move(files));
    HD_REQUIRE(r.snapshot != nullptr);
    HD_CHECK(r.errors.empty());
    const Snapshot& s = *r.snapshot;
    const LookupContext none;
    // With no prior dictionary, an empty dict code counts as the prior one when a user
    // record meets a base record of the same tag and length: the user record gets XXX.
    HD_CHECK_EQ(Show(s.GetWordInfo("犬だ", LookupMode::General, none)),
                "count 3\n"
                "[犬] 개 N110|XXX|| r0 id0\n"
                "[犬] 견 N110|||GN r0 id1\n"
                "[犬] 개과 N110||| r0 id2");
    // An invalid byte of a key is stored as U+FFFD, as lookups decode it.
    HD_CHECK_EQ(Show(s.GetWordInfo("\xFFX", LookupMode::General, none)), "count 1\n[\xFFX] 깨짐 N110|||GN r0 id3");
    HD_CHECK_EQ(Show(s.GetWordInfo("\xEF\xBF\xBDX", LookupMode::General, none)), "count 1\n[\xEF\xBF\xBDX] 깨짐 N110|||GN r0 id3");
    HD_CHECK_EQ(Show(s.GetWordInfo("#犬", LookupMode::General, none)), "count 0");

    // Plain entries: an entry drops the base and user candidates of its key and class;
    // force entries come first; hidden entries are not indexed.
    CodeTable table;
    std::vector<UserEntry> entries;
    entries.push_back(Entry(table, "犬", "멍멍이", "N110", 1));
    entries.push_back(Entry(table, "犬だ", "개다", "N130", 2));
    entries.push_back(Entry(table, "犬", "강아지", "E300", 3));
    entries.back().force = true;
    entries.push_back(Entry(table, "犬", "가려짐", "N110", 4));
    entries.back().hidden = true;
    const UserText plain(std::move(entries));
    HD_CHECK_EQ(plain.Len(), size_t{3});
    LookupContext ctx;
    ctx.plain = &plain;
    const std::string want =
        "count 3\n"
        "[犬] 강아지 E300|||GN r0 id0 plain\n"
        "[犬だ] 개다 N130|||GN r0 id0 plain\n"
        "[犬] 멍멍이 N110|||GN r0 id0 plain";
    HD_CHECK_EQ(Show(s.GetWordInfo("犬だ", LookupMode::General, ctx)), want);
    ctx.strategy = Strategy::UserFirst;
    HD_CHECK_EQ(Show(s.GetWordInfo("犬だ", LookupMode::General, ctx)), want);
    ctx.strategy = Strategy::LongestMatch;
    ctx.skip = {[](const void*, const UserEntry& e) { return e.force; }, nullptr};
    HD_CHECK_EQ(Show(s.GetWordInfo("犬だ", LookupMode::General, ctx)),
                "count 2\n"
                "[犬だ] 개다 N130|||GN r0 id0 plain\n"
                "[犬] 멍멍이 N110|||GN r0 id0 plain");
}

HD_TEST(UserTextSparseOverlay) {
    CodeTable table;
    auto add = [&table](std::vector<UserEntry>& entries, std::string key, std::string target, std::string_view part,
                        int file, int line, int prio = 0, int order = 0) -> UserEntry& {
        entries.push_back(Entry(table, std::move(key), std::move(target), part, line));
        UserEntry& e = entries.back();
        e.file = file;
        e.prio = prio;
        e.order = order;
        return e;
    };
    std::vector<UserEntry> baseEntries;
    add(baseEntries, "学", "학", "N110", 0, 1);
    add(baseEntries, "学生", "학생", "N110", 0, 2, 9);
    add(baseEntries, "学生", "배우다", "M428", 0, 3);
    add(baseEntries, "学生", "생도", "N110", 0, 4);
    add(baseEntries, "学生会", "학생회", "N110", 0, 5);
    add(baseEntries, "行", "줄", "N110", 0, 1);
    add(baseEntries, "行", "행차", "E300", 1, 9);
    add(baseEntries, "行", "걷다", "M428", 1, 3, 0, 1);
    add(baseEntries, "行", "가다", "M428", 1, 3);
    add(baseEntries, "行", "다니다", "M428", 1, 3, 0, 1);
    add(baseEntries, "あい", "사랑", "N110", -2, 1).kana = true;
    add(baseEntries, "あい", "아끼다", "M428", -2, 2).kana = true;
    add(baseEntries, "", "다", "S5A0", 0, 1).end = true;
    add(baseEntries, "", "네", "S5A0", 0, 2).registerLevel = 1;
    add(baseEntries, "", "어요", "S5A0", 0, 3).polite = true;
    add(baseEntries, "", "다네", "S5A0", 0, 4).stative = true;
    add(baseEntries, "", "게", "T5A0", 0, 5);
    add(baseEntries, "", "이고", "U5I1", 0, 6);
    add(baseEntries, "さん", "씨", "N110", 0, 1).honorific = true;
    add(baseEntries, "さま", "님", "N110", 0, 2).honorific = true;
    add(baseEntries, "さん方", "여러분", "N110", 0, 3).honorific = true;

    std::vector<UserEntry> extraEntries;
    add(extraEntries, "学生", "학우", "N110", 1, 1);
    add(extraEntries, "学生", "학도", "N110", 1, 2);
    add(extraEntries, "学生会館", "학생회관", "N110", 1, 3);
    add(extraEntries, "行", "오다", "M428", 1, 3, 0, 1);
    add(extraEntries, "行", "앞서다", "N130", 0, 20, 2);
    add(extraEntries, "あい", "애정", "N110", -1, 1);
    add(extraEntries, "あいさつ", "인사", "N110", -1, 2);
    add(extraEntries, "", "는다", "S5A0", 1, 1).end = true;
    add(extraEntries, "た", "었다", "S5A0", -1, 3);
    add(extraEntries, "", "무시", "N110", 1, 4);
    add(extraEntries, "学生会", "숨김", "N110", 9, 1).hidden = true;
    add(extraEntries, "学生", "제외", "N110", 9, 2).codes = nullptr;
    add(extraEntries, "さん", "선생", "N110", -1, 1, 9).honorific = true;
    add(extraEntries, "さん", "씨께", "N110", -2, 2).honorific = true;
    add(extraEntries, "さん方々", "여러분께", "N110", -1, 3).honorific = true;

    const UserText base(baseEntries);
    const UserText overlay(base, extraEntries);
    const UserText unchanged(base, {});
    std::vector<UserEntry> allEntries = baseEntries;
    allEntries.insert(allEntries.end(), extraEntries.begin(), extraEntries.end());
    const UserText flattened(std::move(allEntries));
    HD_CHECK_EQ(base.Len(), size_t{18});
    HD_CHECK_EQ(overlay.Len(), flattened.Len());
    HD_CHECK_EQ(overlay.Len(), size_t{23});
    HD_CHECK_EQ(unchanged.Len(), base.Len());
    HD_CHECK_EQ(overlay.Entries().size(), extraEntries.size());

    auto targets = [](const std::vector<const UserEntry*>& entries) {
        std::string s;
        for (const UserEntry* e : entries) s += (s.empty() ? "" : " | ") + e->target;
        return s;
    };
    struct SearchCase {
        std::string_view text;
        LookupMode mode;
        const char* targets;
    };
    const SearchCase searches[] = {
        {"学生会館です", LookupMode::General, "학생회관 | 학생회 | 학우 | 학도 | 배우다 | 학"},
        {"学生です", LookupMode::General, "학우 | 학도 | 배우다 | 학"},
        {"学ぶ", LookupMode::General, "학"},
        {"行く", LookupMode::General, "앞서다 | 가다 | 걷다 | 다니다 | 오다 | 행차 | 줄"},
        {"あいさつする", LookupMode::General, "인사 | 애정 | 아끼다"},
        {"あい", LookupMode::General, "애정 | 아끼다"},
        {"", LookupMode::General, ""},
        {"", LookupMode::VerbEnding, "는다 | 네 | 어요 | 다네"},
        {"", LookupMode::AdjectiveEnding, "게"},
        {"", LookupMode::AdjectivalNounEnding, "이고"},
        {"", LookupMode::PrefixVerb, ""},
        {"た後", LookupMode::VerbEnding, "었다"},
        {"た後", LookupMode::General, ""},
        {"学生です", LookupMode::VerbEnding, ""},
        {"さん方々へ", LookupMode::General, ""},
        {"猫", LookupMode::General, ""},
    };
    for (const SearchCase& c : searches) {
        const auto actual = overlay.Search(c.text, c.mode);
        const auto expected = flattened.Search(c.text, c.mode);
        HD_CHECK_EQ(targets(expected), c.targets);
        HD_CHECK_EQ(targets(actual), targets(expected));
        HD_CHECK_EQ(targets(unchanged.Search(c.text, c.mode)), targets(base.Search(c.text, c.mode)));
        HD_REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            HD_CHECK_EQ(actual[i]->key, expected[i]->key);
            HD_CHECK(actual[i]->codes == expected[i]->codes);
            HD_CHECK_EQ(actual[i]->prio, expected[i]->prio);
            HD_CHECK_EQ(actual[i]->file, expected[i]->file);
            HD_CHECK_EQ(actual[i]->line, expected[i]->line);
            HD_CHECK_EQ(actual[i]->order, expected[i]->order);
            HD_CHECK_EQ(actual[i]->kana, expected[i]->kana);
            HD_CHECK_EQ(actual[i]->end, expected[i]->end);
            HD_CHECK_EQ(actual[i]->registerLevel, expected[i]->registerLevel);
            HD_CHECK_EQ(actual[i]->polite, expected[i]->polite);
            HD_CHECK_EQ(actual[i]->stative, expected[i]->stative);
        }
    }
    HD_CHECK_EQ(targets(base.Search("学生です", LookupMode::General)), "학생 | 배우다 | 생도 | 학");

    struct HonorificCase {
        std::string_view text;
        const char* target;
    };
    const HonorificCase honorifics[] = {
        {"さんへ", "씨께"},
        {"さん方へ", "여러분"},
        {"さん方々へ", "여러분께"},
        {"さまへ", "님"},
        {"猫", ""},
        {"", ""},
    };
    for (const HonorificCase& c : honorifics) {
        const UserEntry* actual = overlay.Honorific(c.text);
        const UserEntry* expected = flattened.Honorific(c.text);
        HD_CHECK_EQ(actual != nullptr, expected != nullptr);
        HD_CHECK_EQ(actual ? actual->target : "", c.target);
        if (actual && expected) {
            HD_CHECK_EQ(actual->key, expected->key);
            HD_CHECK_EQ(actual->target, expected->target);
        }
        HD_CHECK(unchanged.Honorific(c.text) == base.Honorific(c.text));
    }
    HD_REQUIRE(base.Honorific("さんへ") != nullptr);
    HD_CHECK_EQ(base.Honorific("さんへ")->target, "씨");
}

HD_TEST(SnapshotStrategies) {
    std::vector<File> files;
    files.push_back(MakeFile("base.tsv", kHeader + "犬\t견\tN110\t\t\tGN\t1\n犬小屋\t개집\tN110\t\t\tGN\t2\n"));
    const LoadResult r = Load(std::move(files));
    HD_REQUIRE(r.snapshot != nullptr);
    CodeTable table;
    std::vector<UserEntry> entries;
    entries.push_back(Entry(table, "犬", "강아지", "E300", 1));
    const UserText plain(std::move(entries));
    LookupContext ctx;
    ctx.plain = &plain;
    // A plain entry goes behind longer base candidates, ahead of the others.
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("犬小屋", LookupMode::General, ctx)),
                "count 3\n"
                "[犬小屋] 개집 N110|||GN r0 id2\n"
                "[犬] 강아지 E300|||GN r0 id0 plain\n"
                "[犬] 견 N110|||GN r0 id1");
    ctx.strategy = Strategy::UserFirst;
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("犬小屋", LookupMode::General, ctx)),
                "count 3\n"
                "[犬] 강아지 E300|||GN r0 id0 plain\n"
                "[犬小屋] 개집 N110|||GN r0 id2\n"
                "[犬] 견 N110|||GN r0 id1");
}

HD_TEST(SnapshotNamesAndPlainEntries) {
    const LoadResult r = LoadFoldersTsv({Folder()});
    HD_REQUIRE(r.snapshot != nullptr);
    CodeTable table;
    // A name entry goes before the plain entries with a shorter key.
    std::vector<UserEntry> shorter;
    shorter.push_back(Entry(table, "ア", "아", "E300", 1));
    const UserText a(std::move(shorter));
    LookupContext ctx;
    ctx.plain = &a;
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("アリスさん", LookupMode::General, ctx)),
                "count 2\n"
                "[アリス] 앨리스 N130|QPN||GN r0 id0 name\n"
                "[ア] 아 E300|||GN r0 id0 plain");
    // A plain entry with the key removes the name entry.
    std::vector<UserEntry> same;
    same.push_back(Entry(table, "アリス", "아리스", "N130", 1));
    const UserText b(std::move(same));
    ctx.plain = &b;
    HD_CHECK_EQ(Show(r.snapshot->GetWordInfo("アリスさん", LookupMode::General, ctx)),
                "count 1\n[アリス] 아리스 N130|||GN r0 id0 plain");
}

namespace {

// The entries of u from file as "key target part @line.order".
std::string EntriesOf(const UserText& u, int file) {
    std::string s;
    for (const UserEntry& e : u.Entries()) {
        if (e.file != file) continue;
        s += (s.empty() ? "" : " | ") + e.key + " " + e.target + " " + e.codes->partText + " @" + std::to_string(e.line) +
             "." + std::to_string(e.order);
    }
    return s;
}

std::size_t CountFile(const UserText& u, int file) {
    std::size_t n = 0;
    for (const UserEntry& e : u.Entries()) n += e.file == file;
    return n;
}

}  // namespace

HD_TEST(SnapshotCompiledPlainDictionaries) {
    std::vector<File> files;
    files.push_back(MakeFile("a/base.tsv", kHeader + "帰\t돌아가&AA@\tM428\t\t\tGN\t1\n愛\t사랑\tN110\t\t\tGN\t2\n"
                                                    "愛\t사랑하&AV@\tM429\t\t\tGN\t3\n"));
    files.push_back(MakeFile("a/kana.tsv", "#! layer=kana\n// kana\nあい\t愛\t\n"));
    files.push_back(MakeFile("a/plain.tsv", "帰る\t돌아가다\tv\n猫\t냥이\n"));
    files.push_back(MakeFile("a/readings.tsv", "#! layer=readings\nx\ty\n"));
    files.push_back(MakeFile("b/userdict.tsv", "猫\t고양이\tn\tprio=1\n"));
    const LoadResult r = Load(std::move(files));
    HD_REQUIRE(r.snapshot != nullptr);
    HD_CHECK(r.errors.empty());
    const Snapshot& s = *r.snapshot;
    HD_CHECK_EQ(s.PlainFiles().size(), size_t{4});
    // The builtin endings come first, then the plain files numbered in order; the
    // base dictionary resolves the る-verb 帰る as godan.
    HD_CHECK_EQ(CountFile(s.Plain(), -1), size_t{426});
    HD_CHECK_EQ(EntriesOf(s.Plain(), 0), "帰 돌아&A1@ M428 @1.0 | 猫 냥이 N110 @2.0");
    HD_CHECK_EQ(EntriesOf(s.Plain(), 1), "猫 고양이 N110 @1.0");
    HD_CHECK_EQ(s.Plain().Entries().size(), size_t{429});
    HD_CHECK(s.Plain().Entries()[0].end);
    // The text path adds the kana spellings, the phrases but the 343 interjections in
    // kana, and the patterns.
    HD_CHECK_EQ(EntriesOf(s.TextPlain(), -2), "あい 사랑 N110 @3.0");
    HD_CHECK_EQ(CountFile(s.TextPlain(), -1), size_t{2111});
    HD_CHECK_EQ(s.TextPlain().Entries().size(), size_t{2115});
    HD_CHECK(s.Senses().contains("腕"));
    const auto better = s.Senses().find("マシ");
    HD_REQUIRE(better != s.Senses().end());
    HD_REQUIRE(!better->second.empty());
    const std::string& betterTarget = better->second[0].target;
    bool betterFamily = false;
    for (const auto& [id, rows] : s.GeneratedFamilies().GeneratedFamilies()) {
        HD_CHECK(id.front() != '&' || rows[1] != "많");
        if (id != betterTarget) continue;
        betterFamily = true;
        HD_CHECK_EQ(rows[1], std::string("낫"));
        HD_CHECK_EQ(rows[5], std::string("나았"));
        HD_CHECK_EQ(rows[6], std::string("나아"));
    }
    HD_CHECK(betterFamily);
    HD_CHECK_EQ(std::string(s.GeneratedFamilies().PercentRow(betterTarget + "5")), std::string("나았"));

    // The later file wins on an equal key and part code.
    LookupContext ctx;
    ctx.plain = &s.Plain();
    HD_CHECK_EQ(Show(s.GetWordInfo("猫", LookupMode::General, ctx)), "count 1\n[猫] 고양이 N110|||GN r0 id0 plain");

    // A line that does not compile fails the load and is listed with its file.
    files.clear();
    files.push_back(MakeFile("a/base.tsv", kHeader + "犬\t견\tN110\t\t\tGN\t1\n"));
    files.push_back(MakeFile("a/plain.tsv", "猫\t냥이\n帰る\t돌아가다\tv\n鳥\n"));
    const LoadResult bad = Load(std::move(files));
    HD_CHECK(bad.snapshot == nullptr);
    HD_REQUIRE(bad.errors.size() == 2);
    HD_CHECK(bad.errors[0].kind == LoadErrorKind::Plain);
    HD_CHECK_EQ(bad.errors[0].file, "a/plain.tsv");
    HD_CHECK_EQ(bad.errors[0].line, 2);
    HD_CHECK_EQ(bad.errors[0].message, "る-verb \"帰る\" is ambiguous: write v1 (ichidan) or v5 (godan)");
    HD_CHECK_EQ(bad.errors[1].message, "empty KR");
}

HD_TEST(SnapshotPlainHashComments) {
    // "#" lines are comments in plain files, with or without a header; a "#!" first
    // line stays the header.
    std::vector<File> files;
    files.push_back(MakeFile("a/base.tsv", kHeader + "犬\t견\tN110\t\t\tGN\t1\n"));
    files.push_back(MakeFile("a/plain.tsv", "# comment\n猫\t냥이\n  # indented\n#鳥\t새\n"));
    files.push_back(MakeFile("b/plain.tsv", "#! layer=base\n# description\n犬\t개\n"));
    const LoadResult r = Load(files);
    HD_REQUIRE(r.snapshot != nullptr);
    HD_CHECK(r.errors.empty());
    HD_CHECK_EQ(EntriesOf(r.snapshot->Plain(), 0), "猫 냥이 N110 @2.0");
    HD_CHECK_EQ(EntriesOf(r.snapshot->Plain(), 1), "犬 개 N110 @3.0");

    files[2].text = "#! base\n犬\t개\n";
    const LoadResult bad = Load(std::move(files));
    HD_CHECK(bad.snapshot == nullptr);
    HD_REQUIRE(bad.errors.size() == 1);
    HD_CHECK(bad.errors[0].kind == LoadErrorKind::Header);
    HD_CHECK_EQ(bad.errors[0].line, 1);
}

HD_TEST(SnapshotColumnMembershipUsesContent) {
    for (const std::string name : {"names.tsv", "userdict.tsv", "readings.tsv", "kana.tsv"}) {
        const auto column = Load({MakeFile(name, kHeader + "猫\t고양이\tN110\t\t\tGN\t1\n")});
        HD_REQUIRE(column.snapshot != nullptr);
        HD_CHECK(column.snapshot->HasDictionary());
        HD_CHECK(column.snapshot->PlainFiles().empty());
        const std::string plainText = "猫\t고양이\tn\n";
        const auto plain = Load({MakeFile(name, plainText)});
        HD_REQUIRE(plain.snapshot != nullptr);
        HD_CHECK(!plain.snapshot->HasDictionary());
        HD_REQUIRE(plain.snapshot->PlainFiles().size() == 1);
        HD_CHECK_EQ(plain.snapshot->PlainFiles()[0].name, name);
        HD_CHECK_EQ(plain.snapshot->PlainFiles()[0].text, plainText);
    }
}

HD_TEST(SnapshotColumnAndPlainCodeClosure) {
    const auto loaded = Load({
        MakeFile("base.tsv", kHeader + "犬\t견\tN110\tQNN\tWb\tBZ\t1\n猫\t고양이\tN110\t\t\tGN\t2\n"),
        MakeFile("names.tsv", "#! layer=names\n" + kHeader + "アリス\t앨리스\tN130\tQPN\tWa\tPN\t1\n"),
        MakeFile("userdict.tsv", "#! layer=user\n" + kHeader + "犬\t개\tN110\tVHn\tWb\t\t1\n"),
        MakeFile("plain.tsv", "猫\t냥이\tn\n歌う\t노래하다\tv5\n"),
    });
    HD_REQUIRE(loaded.snapshot != nullptr);
    HD_CHECK(loaded.errors.empty());
    const auto& s = *loaded.snapshot;
    auto check = [](const Codes* c) {
        HD_REQUIRE(c != nullptr);
        HD_REQUIRE(c->withXXX != nullptr);
        HD_REQUIRE(c->asPlain != nullptr);
        HD_CHECK(c->withXXX->sem.Contains("XXX"));
        HD_CHECK(c->asPlain->dict == "GN");
        HD_CHECK_EQ(c->withXXX->partText, c->partText);
        HD_CHECK(c->asPlain->sem.Text() == c->sem.Text());
        HD_CHECK(c->asPlain->gram.Text() == c->gram.Text());
        HD_CHECK(c->withXXX->withXXX == c->withXXX);
        HD_CHECK(c->asPlain->asPlain == c->asPlain);
        HD_REQUIRE(c->withXXX->asPlain != nullptr);
        HD_CHECK(c->withXXX->asPlain == c->asPlain->withXXX);
    };
    HD_REQUIRE(s.User() != nullptr && s.Names() != nullptr);
    for (const auto* layer : {&s.Base(), s.User(), s.Names()}) {
        layer->Each([&](std::string_view, const std::vector<Record>& records) {
            for (const auto& r : records) check(r.codes);
        });
    }
    for (bool extra : {false, true}) {
        for (const auto* view : {&s.Plain(extra), &s.TextPlain(extra)}) {
            for (const auto& e : view->Entries()) check(e.codes);
        }
    }
    const auto cat = s.Base().Records("猫");
    const auto plainCat = s.Plain().Search("猫", LookupMode::General);
    HD_REQUIRE(cat.size() == 1 && plainCat.size() == 1);
    HD_CHECK(cat[0].codes == plainCat[0]->codes);
}

HD_TEST(SnapshotBuiltinViewIsolation) {
    const auto loaded = Load({});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const Snapshot& s = *loaded.snapshot;
    HD_CHECK(s.TextPlain().Search("駄目で元々", LookupMode::General).empty());
    const auto extra = s.TextPlain(true).Search("駄目で元々", LookupMode::General);
    HD_REQUIRE(extra.size() == 1);
    HD_CHECK_EQ(extra[0]->target, "밑져야 본전");
    HD_CHECK(s.Plain(true).Search("駄目で元々", LookupMode::General).empty());
    HD_CHECK(s.TextPlain().Search("駄目で元々", LookupMode::General).empty());
}

HD_TEST(DictBorrowedBuffersAndMoves) {
    const LoadResult loaded = LoadFoldersTsv({Folder(), Folder() + "/over"});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const Dict& base = loaded.snapshot->Base();
    const Dict::ViewData data = base.View();
    Dict borrowed = Dict::FromView(data);
    HD_CHECK(borrowed.View().blob.data() == data.blob.data());
    HD_CHECK(borrowed.View().starts.data() == data.starts.data());
    HD_CHECK(borrowed.View().codes.data() == data.codes.data());
    Dict moved = std::move(borrowed);
    borrowed = std::move(moved);
    HD_CHECK_EQ(borrowed.Len(), base.Len());
    base.Each([&](std::string_view key, const std::vector<Record>& records) {
        HD_CHECK_EQ(borrowed.Find(key), base.Find(key));
        const std::vector<Record> got = borrowed.Records(key);
        HD_REQUIRE(got.size() == records.size());
        for (size_t i = 0; i < got.size(); i++) {
            HD_CHECK(got[i].target == records[i].target);
            HD_CHECK(got[i].codes == records[i].codes);
            HD_CHECK_EQ(got[i].rule, records[i].rule);
            HD_CHECK_EQ(got[i].id, records[i].id);
        }
    });
    HD_CHECK_EQ(borrowed.Find("未登録の単語"), -1);
    const Dict empty;
    const Dict borrowedEmpty = Dict::FromView(empty.View());
    HD_CHECK_EQ(borrowedEmpty.Len(), size_t{0});
    HD_CHECK(borrowedEmpty.Records("学").empty());
}

HD_TEST(DictBoundedRecords) {
    const std::string_view keys[] = {"学"};
    const Trie trie = Trie::Build(keys, nullptr);
    CodeTable table;
    const Codes* codes[] = {table.At(table.Intern("N110", "", "", "GN"))};
    const uint32_t starts[] = {0};
    const auto bytes = [](std::initializer_list<unsigned> values) {
        std::string s;
        for (const unsigned value : values) s.push_back(static_cast<char>(value));
        return s;
    };
    const std::vector<std::string> malformed = {
        {}, bytes({0}), bytes({0, 0}), bytes({4}), bytes({8, 'x', 0, 0}), bytes({0, 1, 0}),
        bytes({2, 0, 0}), bytes({0, 0, 0x80}), bytes({1, 0, 0}), bytes({0, 0, 0, 0}),
        bytes({0, 0, 0x80, 0x80, 0x80, 0x80, 0x10}),
        bytes({2, 0, 0x80, 0x80, 0x80, 0x80, 0x10, 0}),
        bytes({0}) + std::string(10, static_cast<char>(0x80)),
        bytes({2, 0}) + std::string(10, static_cast<char>(0x80)),
        bytes({0, 0}) + std::string(10, static_cast<char>(0x80)),
        std::string(10, static_cast<char>(0x80)),
        std::string(9, static_cast<char>(0x80)) + bytes({2, 0, 0}),
        std::string(10, static_cast<char>(0x80)) + bytes({0, 0, 0}),
    };
    struct GuardState {
        const char* blob;
        size_t size;
        const uint32_t* starts;
        bool readable = true;
        mutable int failures = 0;
        mutable int reads = 0;
    } state{};
    const ReadGuard guard = {
        &state,
        [](const void* context, const void* pointer, size_t length) {
            const auto& s = *static_cast<const GuardState*>(context);
            s.reads++;
            if (!s.readable) return false;
            if (pointer == s.starts) return length == sizeof(uint32_t);
            const auto at = reinterpret_cast<std::uintptr_t>(pointer);
            const auto begin = reinterpret_cast<std::uintptr_t>(s.blob);
            return at >= begin && at - begin <= s.size && length <= s.size - (at - begin);
        },
        [](const void* context) { static_cast<const GuardState*>(context)->failures++; }};
    for (const std::string& blob : malformed) {
        state = {blob.data(), blob.size(), starts};
        const Dict d = Dict::FromView({trie.View(), blob, starts, codes, guard});
        HD_CHECK(!d.Validate());
        size_t calls = 0;
        d.EachRecord(0, [&](const Record&) { calls++; return true; });
        HD_CHECK_EQ(calls, size_t{0});
        HD_CHECK(state.failures > 0);
    }
    const std::string valid = bytes({6, 'x', 0, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F});
    state = {valid.data(), valid.size(), starts};
    auto view = Dict::ViewData{trie.View(), valid, starts, codes, guard};
    const Dict d = Dict::FromView(view);
    HD_CHECK(d.Validate());
    HD_CHECK(d.View().guard.context == &state);
    const auto records = d.Records("学");
    HD_REQUIRE(records.size() == 1);
    HD_CHECK(records[0].target == "x");
    HD_CHECK_EQ(records[0].id, -1);
    HD_CHECK_EQ(records[0].rule, -1);
    HD_CHECK(records[0].codes == codes[0]);
    for (const int k : {-1, 1, 999}) {
        size_t calls = 0;
        d.EachRecord(k, [&](const Record&) { calls++; return true; });
        HD_CHECK_EQ(calls, size_t{0});
    }
    state.readable = false;
    HD_CHECK(d.Records("学").empty());
    HD_CHECK(!d.Validate());
    state.readable = true;
    view.starts = {};
    HD_CHECK(!Dict::FromView(view).Validate());
    const uint32_t outside[] = {999};
    view.guard = {};
    view.starts = outside;
    HD_CHECK(Dict::FromView(view).Records("学").empty());
    HD_CHECK(!Dict::FromView(view).Validate());
    HD_CHECK(Dict{}.Validate());
}

HD_TEST(DictSparseOffsetsAndTruncation) {
    const auto loaded = LoadFoldersTsv({Folder(), Folder() + "/over"});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto valid = loaded.snapshot->Base().View();
    HD_REQUIRE(loaded.snapshot->Base().Validate());
    HD_REQUIRE(valid.starts.size() > 1);
    std::vector<uint32_t> starts(valid.starts.begin(), valid.starts.end());
    for (const uint32_t offset : {uint32_t{0}, starts[1] + 1, UINT32_MAX}) {
        auto data = valid;
        starts[1] = offset;
        data.starts = starts;
        const Dict d = Dict::FromView(data);
        HD_CHECK(!d.Validate());
        for (size_t k = 0; k < d.Len(); k++) d.EachRecord(static_cast<int>(k), [](const Record&) { return true; });
    }
    for (size_t n = 0; n < valid.blob.size(); n++) {
        auto data = valid;
        data.blob = valid.blob.substr(0, n);
        const Dict d = Dict::FromView(data);
        HD_CHECK(!d.Validate());
        for (size_t k = 0; k < d.Len(); k++) d.EachRecord(static_cast<int>(k), [](const Record&) { return true; });
    }
}

namespace {

std::string ReadFixture(const std::string& name) {
    std::ifstream in(SourceDir() + "/testdata/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Every key of d with its records.
std::string Dump(const Dict* d) {
    std::string s;
    if (!d) return s;
    d->Each([&](std::string_view key, const std::vector<Record>& records) {
        s += std::string(key) + ":";
        for (const Record& r : records) {
            s += " " + std::string(r.target) + "/" + r.codes->partText + "/" + std::string(r.codes->sem.Text()) + "/" +
                 std::string(r.codes->gram.Text()) + "/" + std::string(r.codes->dict.Text()) + "/" +
                 std::to_string(r.rule) + "/" + std::to_string(r.id);
        }
        s += "\n";
    });
    return s;
}

}  // namespace

HD_TEST(SnapshotLayerHeaders) {
    struct Case {
        const char* file;
        bool ok;
        FileLayer layer;
    };
    const Case cases[] = {
        {"missing.tsv", true, FileLayer::Base},   {"base.tsv", true, FileLayer::Base},
        {"words.tsv", true, FileLayer::Words},    {"names.tsv", true, FileLayer::Names},
        {"user.tsv", true, FileLayer::User},      {"kana.tsv", true, FileLayer::Kana},
        {"readings.tsv", true, FileLayer::Readings}, {"bom.tsv", true, FileLayer::Names},
        {"unknown.tsv", false, FileLayer::Base},  {"nokey.tsv", false, FileLayer::Base},
    };
    for (const Case& c : cases) {
        const std::string text = ReadFixture(std::string("layers/") + c.file);
        HD_REQUIRE(!text.empty());
        FileLayer layer = FileLayer::Words;
        HD_CHECK_EQ(internal::ReadLayer(text, layer), c.ok);
        HD_CHECK(layer == c.layer);
    }
    HD_CHECK_EQ(std::string(internal::WithoutHeader("#! layer=kana\nあい\t愛\n")), "\nあい\t愛\n");
    HD_CHECK_EQ(std::string(internal::WithoutHeader("\xEF\xBB\xBF#! layer=kana")), "");
    HD_CHECK_EQ(std::string(internal::WithoutHeader("あい\t愛\n")), "あい\t愛\n");

    // The layer comes from the header, whatever the file name.
    std::vector<File> files;
    for (const char* name : {"base.tsv", "bom.tsv", "kana.tsv", "readings.tsv", "user.tsv", "words.tsv"}) {
        files.push_back(MakeFile(std::string("x/") + name, ReadFixture(std::string("layers/") + name)));
    }
    const LoadResult loaded = Load(files);
    HD_REQUIRE(loaded.snapshot != nullptr);
    HD_CHECK(loaded.errors.empty());
    HD_CHECK_EQ(Targets(loaded.snapshot->NameRecords("猫")), "고양이");
    HD_REQUIRE(loaded.snapshot->User() != nullptr);
    HD_CHECK_EQ(Targets(loaded.snapshot->User()->Records("猫")), "고양이");
    HD_CHECK_EQ(Targets(loaded.snapshot->Base().Records("猫")), "고양이 고양이");
    HD_REQUIRE(loaded.snapshot->PlainFiles().size() == 2);
    HD_CHECK(loaded.snapshot->PlainFiles()[0].layer == FileLayer::Kana);
    HD_CHECK(loaded.snapshot->PlainFiles()[1].layer == FileLayer::Readings);
    HD_CHECK_EQ(EntriesOf(loaded.snapshot->TextPlain(), -2), "ねこ 고양이 N110 @2.0");

    // An unknown header, or a layer the kind of file cannot have, fails the load at line 1.
    const std::pair<std::string, std::string> bad[] = {
        {"x/unknown.tsv", ReadFixture("layers/unknown.tsv")},
        {"x/nokey.tsv", ReadFixture("layers/nokey.tsv")},
        {"x/column-kana.tsv", "#! layer=kana\n" + kHeader + "猫\t고양이\tN110\t\t\tGN\t1\n"},
        {"x/plain-names.tsv", "#! layer=names\n猫\t고양이\tn\n"},
    };
    for (const auto& [name, text] : bad) {
        const LoadResult failed = Load({MakeFile("x/base.tsv", ReadFixture("layers/base.tsv")), MakeFile(name, text)});
        HD_CHECK(failed.snapshot == nullptr);
        HD_REQUIRE(failed.errors.size() == 1);
        HD_CHECK(failed.errors[0].kind == LoadErrorKind::Header);
        HD_CHECK_EQ(failed.errors[0].file, name);
        HD_CHECK_EQ(failed.errors[0].line, 1);
        HD_CHECK(!failed.errors[0].message.empty());
    }
}

// testdata/renamed is testdata/folder with every file of a layer other than base
// renamed (names.tsv is zz-people.tsv): it loads the same.
HD_TEST(SnapshotRenamedLayerFiles) {
    const LoadResult a = LoadFoldersTsv({Folder()});
    const LoadResult b = LoadFoldersTsv({SourceDir() + "/testdata/renamed"});
    HD_REQUIRE(a.snapshot != nullptr && b.snapshot != nullptr);
    const Snapshot& x = *a.snapshot;
    const Snapshot& y = *b.snapshot;
    HD_REQUIRE(a.errors.size() == b.errors.size());
    for (std::size_t i = 0; i < a.errors.size(); i++) {
        HD_CHECK(a.errors[i].kind == b.errors[i].kind);
        HD_CHECK_EQ(a.errors[i].line, b.errors[i].line);
    }
    HD_REQUIRE(x.Warnings().size() == 1 && y.Warnings().size() == 1);
    HD_CHECK(y.Warnings()[0].find("zz-people.tsv: ") != std::string::npos);
    HD_CHECK_EQ(x.Warnings()[0].substr(x.Warnings()[0].find(".tsv: ")), y.Warnings()[0].substr(y.Warnings()[0].find(".tsv: ")));
    HD_CHECK(!Dump(x.Names()).empty() && !Dump(x.User()).empty());
    HD_CHECK_EQ(Dump(&x.Base()), Dump(&y.Base()));
    HD_CHECK_EQ(Dump(x.User()), Dump(y.User()));
    HD_CHECK_EQ(Dump(x.Names()), Dump(y.Names()));
    HD_CHECK_EQ(x.Plain().Entries().size(), y.Plain().Entries().size());
    HD_CHECK_EQ(x.TextPlain().Entries().size(), y.TextPlain().Entries().size());
    const LookupContext none;
    for (const char* text : {"学生です", "アリスさん", "霧島さん", "京都の", "行くの", "翔太くん", "カイト", "猫"}) {
        for (const auto mode : {LookupMode::General, LookupMode::VerbEnding}) {
            HD_CHECK_EQ(Show(x.GetWordInfo(text, mode, none)), Show(y.GetWordInfo(text, mode, none)));
        }
    }
}
