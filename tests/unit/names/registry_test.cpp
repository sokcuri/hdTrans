// Speaker labels, honorifics, occurrence guards, name rendering and plain rows.
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/names/historical.h"
#include "core/names/registry.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

const dict::Snapshot* Dictionary() {
    static const dict::LoadResult loaded = dict::Load({
        {0, "base.tsv",
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "魅音\t미온\tN130\tQPN\tWb\tGN\t1\n"
         "梨花\t배꽃\tN110\t\t\tGN\t2\n"
         "光\t빛\tN110\t\t\tGN\t3\n"
         "光\t히카루\tN130\tQPN\tWb\tGN\t4\n"
         "光\t빛나&AA@\tM428\t\t\tGN\t5\n"
         "乾\t마르&AA@\tM428\t\t\tGN\t6\n"
         "乾\t이누이\tN130\tQPN\tWa\tGN\t7\n"
         "前原\t마에바라\tN130\tQPN\tWa\tGN\t8\n"
         "圭一\t케이이치\tN130\tQPN\tWb\tGN\t9\n"
         "ソード\t소드\tN110\t\t\tGN\t10\n"
         "ワールド\t월드\tN110\t\t\tGN\t11\n"
         "ハル\t헐\tN110\t\t\tGN\t15\n"
         "提督\t제독\tN110\t\t\tGN\t17\n"
         "あなた\t당신\tN120\t\t\tGN\t18\n"
         "私\t나\tN120\t\t\tGN\t19\n"
         "わたくし\t저\tN120\t\t\tGN\t20\n"
         "三日\t3일\tN110\t\t\tGN\t21\n"},
        {0, "names.tsv",
         "#! layer=names\n"
         "key\tko\tpos\tsem\tgram\tdict\tid\n"
         "椎名\t시이나\tN130\tQPN\tWa\tGN\t12\n"
         "周\t아마네\tN130\tQPN\tWb\tGN\t13\n"
         "樹\t이츠키\tN130\tQPN\tWb\tGN\t14\n"
         "ハル\t하루\tN130\tQPNVHn\tWb\tGN\t16\n"},
    });
    return loaded.snapshot.get();
}

std::string Repeat(const std::string& line, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += line + "\n";
    return out;
}

// The registry of doc, with every line added.
names::Registry Scan(const dict::Snapshot* snap, const std::string& doc) {
    names::Registry r(snap);
    size_t start = 0;
    for (;;) {
        const size_t nl = doc.find('\n', start);
        r.Add(doc.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return r;
}

// The surfaces of line's occurrences, a "!" after a guarded one, separated by spaces.
std::string Occurrences(names::Registry& r, const std::string& line) {
    std::string out;
    for (const names::Occurrence& o : r.Occurrences(line)) {
        if (!out.empty()) out += " ";
        out += o.surface;
        if (o.noun) out += "!";
    }
    return out;
}

}  // namespace

HD_TEST(names_strip_ruby) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"|梨花《りか》ちゃん", "梨花ちゃん"},
        {"｜幻晶騎士《シルエットナイト》", "幻晶騎士"},
        {"猫猫《マオマオ》", "猫猫"},
        {"<ruby>梨花<rt>りか</rt></ruby>ちゃん", "梨花ちゃん"},
        {"<ruby><rb>梨花</rb><rp>（</rp><rt>りか</rt><rp>）</rp></ruby>", "梨花"},
        {"《》だけ", "《》だけ"},
    };
    for (const auto& [in, want] : cases) HD_CHECK_EQ(names::StripRuby(in), want);
}

HD_TEST(names_speaker_cues) {
    HD_CHECK_EQ(names::SpeakerCues("魅音「おはよう」\n圭一です。").size(), size_t{2});
    HD_CHECK_EQ(names::SpeakerCues("そう言って、おはよう").size(), size_t{0});
}

HD_TEST(names_labels) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, bool>> cases = {
        {Repeat("魅音「おはよう」", 3), true},
        {Repeat("魅音「おはよう」", 2), false},  // a plain-text form must recur
        {Repeat("【魅音】「おはよう」", 3), true},
        {Repeat("魅音：おはよう", 3), true},
        {Repeat("魅音 : 「おはよう」", 3), true},
        {Repeat("[魅音]: 「おはよう」", 3), true},
        {"OutputLine(\"<color=#5ec69a>魅音</color>\", NULL, \"<color=#5ec69a>미온</color>\", NULL, Line_ContinueAfterTyping);", true},
        {"define m = Character(\"魅音\")\nm \"おはよう\"", true},
        {"[name text=\"魅音\"]おはよう", true},
        {"@nm t=\"魅音\"", true},
        {"#魅音\nおはよう", true},
        {Repeat("そう言って「おはよう」", 3), false},
        {Repeat("魅音の母「おはよう」", 3), false},
        {Repeat("女子生徒Ａ「おはよう」", 3), false},
        {Repeat("魅音？「おはよう」", 3), false},
        {Repeat("// 魅音「おはよう」", 3), false},
    };
    for (const auto& [doc, want] : cases) {
        names::Registry r = Scan(snap, doc);
        const std::vector<std::string> names = r.Names();
        const bool got = std::find(names.begin(), names.end(), "魅音") != names.end();
        HD_CHECK_EQ(got, want);
    }
    // A joint label names both; Xの母 counts once for X.
    names::Registry r = Scan(snap, Repeat("梨花＆沙都子「おはよう」", 3) + "OutputLine(\"圭一の母\", NULL, \"어머니\", NULL, 0);\n圭一さん");
    HD_CHECK(r.Names() == (std::vector<std::string>{"圭一", "梨花", "沙都子"}));
    // A reading in brackets that the word goes on after is ruby, no label (全「すべ」て).
    HD_CHECK(Scan(snap, Repeat("全「すべ」てを壊せる", 3)).Names().empty());
    // A gloss that is a word of its own reads the word, no name (提督(あなた), 私(わたくし));
    // one that is no word names (三日(みか)).
    {
        std::string got;
        for (const std::string& n : Scan(snap, Repeat("提督(あなた)、何いってんの！", 3) + Repeat("私(わたくし)、行くわ。", 3)).Names()) got += n + " ";
        HD_CHECK_EQ(got, std::string());
    }
    const std::vector<std::string> mika = Scan(snap, Repeat("三日(みか)が来た。", 3)).Names();
    HD_CHECK(std::find(mika.begin(), mika.end(), "三日") != mika.end());
    // A numeral is no surname before a name (二美咲).
    const std::vector<std::string> misaki = Scan(snap, Repeat("美咲さん、おはよう。", 3) + Repeat("二美咲が来た。", 3)).Names();
    HD_CHECK(std::find(misaki.begin(), misaki.end(), "二") == misaki.end());
}

HD_TEST(names_honorifics) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
        {Repeat("梨花ちゃん、おはよう。", 4), {"梨花"}},
        {Repeat("梨花ちゃん、おはよう。", 3), {}},  // a common word scores 0.5 per honorific
        {"詩音さん", {}},
        {"詩音さん\n詩音さん", {"詩音"}},
        {"光さんが来た。\n光さんが言った。", {}},  // a single kanji needs 3
        {"光さんが来た。\n光さんが言った。\n光さん、", {"光"}},
        {"光さんだ。\n光さんだ。\n光さんだ。", {}},
        {"西九条先生が来た。\n西九条先生だ。", {"西九条"}},  // a fused kanji honorific is split off
        {"お父さんが来た。\nお父さんだ。\nお父さん", {}},
        {"神様が来た。\n神様だ。\n神様", {}},
        {"ウェイトレスさん\nウェイトレスさん\nウェイトレスさん", {}},
        {"見たんだ。\n見たんだ。\n見たんだ。", {}},
        {Repeat("リン様「来ないで」", 4), {"リン"}},
    };
    for (const auto& [doc, want] : cases) {
        names::Registry r = Scan(snap, doc);
        HD_CHECK(r.Names() == want);
    }
    names::Registry r = Scan(snap, Repeat("リン様", 4));
    HD_CHECK_EQ(r.Render("リン"), std::string("린"));
}

// A katakana word that introduces itself with と名乗る is a name in the whole document, also when
// the dictionary has it as a common word; a kanji word, a role and other quotes are not.
HD_TEST(names_self_introduction) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
        {"ハルと名乗った少女が来た。\nハルは笑った。", {"ハル"}},
        {"ソードと名乗る男", {"ソード"}},
        {"ハルと言った。\nハルは笑った。", {}},
        {"ウェイトレスと名乗った女", {}},
        {"魔王と名乗った男", {}},
    };
    for (const auto& [doc, want] : cases) {
        names::Registry r = Scan(snap, doc);
        HD_CHECK(r.Names() == want);
    }
}

HD_TEST(names_occurrences_and_guards) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::array<std::string, 3>> cases = {
        {"", "光が来た。", ""},
        {Repeat("光さん、", 3), "光が来た。", "光"},
        {Repeat("光さん、", 3), "光線を出す。", ""},
        {Repeat("光さん、", 3), "光る石を見た。", "光!"},
        {Repeat("光さん、", 3), "光が差す。", "光!"},
        {Repeat("光さん、", 3), "光さん、光が差すよ。", "光 光"},
        {Repeat("乾さん、", 3), "服が乾いてきた。", "乾!"},
        {Repeat("乾さん、", 3), "乾が来た。", "乾"},
        {Repeat("桜さん、", 3), "桜の花が咲いた。", "桜!"},
        {Repeat("桜さん、", 3), "桜が咲いた。", "桜!"},
        {Repeat("桜さん、", 3), "桜は学生です。", "桜"},
        {Repeat("詩音さん", 2), "詩音達が来た。", "詩音"},
        {Repeat("詩音さん", 2), "詩音祭だ。", ""},
        {Repeat("詩音さん", 2) + "園崎詩音が来た。", "園崎詩音だ。", "園崎詩音"},
    };
    for (const auto& c : cases) {
        names::Registry r = Scan(snap, c[0]);
        HD_CHECK_EQ(Occurrences(r, c[1]), c[2]);
    }
}

HD_TEST(names_dict_rows) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r = Scan(snap, "竜宮レナ（りゅうぐうれな）\n" + Repeat("前原圭一「おはよう」", 3) + Repeat("圭一くん", 2));
    HD_CHECK_EQ(r.Dict(),
                std::string("前原\t마에바라\tsurname\n前原圭一\t마에바라 케이이치\tname\n"
                            "圭一\t케이이치\tname\n竜宮レナ\t류우구우 레나\tname\nちゃん\t짱\thon\n"));
    r.chan = "쨩";
    const std::string dict = r.Dict();
    const std::string tail = "ちゃん\t쨩\thon\n";
    HD_CHECK(dict.size() >= tail.size() && dict.compare(dict.size() - tail.size(), tail.size(), tail) == 0);
}

HD_TEST(names_readings_without_table) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::pair<std::string, text::Preset>>> cases = {
        {"竜宮レナ（りゅうぐうれな）だ。", {"竜宮レナ", text::Preset::Fan}},
        {"竜宮レナ（りゅうぐうれな）だ。", {"竜宮レナ", text::Preset::FanShort}},
        {"|梨花《りか》が来た。", {"梨花", text::Preset::Fan}},
        {"<ruby>梨花<rt>リファ</rt></ruby>が来た。", {"梨花", text::Preset::Fan}},
    };
    const std::vector<std::string> want = {"류우구우 레나", "류구 레나", "리카", "리파"};
    for (size_t i = 0; i < cases.size(); i++) {
        names::Registry r = Scan(snap, cases[i].first);
        r.preset = cases[i].second.second;
        HD_CHECK_EQ(r.Render(cases[i].second.first), want[i]);
    }
    // A dictionary person reading is used when no reading table entry exists.
    names::Registry blank = Scan(snap, Repeat("魅音「おはよう」", 3));
    HD_CHECK_EQ(blank.Render("魅音"), std::string("미온"));
    names::Registry plain = Scan(snap, Repeat("光さん、", 3));
    HD_CHECK_EQ(plain.Render("光"), std::string("히카루"));
    names::Registry shota = Scan(snap, Repeat("翔太「おはよう」", 3));
    HD_CHECK_EQ(shota.Render("翔太"), std::string("쇼타"));
}

HD_TEST(names_katakana_full_names) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    names::Registry r = Scan(snap, Repeat("アル君、おはよう。", 2) + Repeat("アルガルド様が来た。", 2) +
                                       "王太子、アルガルド・ボナ・パレッティア。\nアルガルドは王子だ。");
    HD_CHECK_EQ(r.Render("アル"), std::string("아르"));
    HD_CHECK_EQ(r.Render("アルガルド"), std::string("아르가르드"));
    HD_CHECK_EQ(r.Render("アルガルド・ボナ・パレッティア"), std::string("아르가르드 보나 파렛티아"));

    names::Registry alone = Scan(snap, Repeat("アル君、おはよう。", 2));
    HD_CHECK_EQ(alone.Render("アル"), std::string("알"));
    // A whole katakana name reads as the name layer's person reading of its kana, whatever
    // the base dictionary makes of the word.
    names::Registry hal = Scan(snap, Repeat("ハル君、おはよう。", 8));
    HD_CHECK_EQ(hal.Render("ハル"), std::string("하루"));

    names::Registry one = Scan(snap, "アルくんが来た。\n" + Repeat("アルガルドは王子だ。", 2));
    HD_CHECK_EQ(one.Render("アル"), std::string("아르"));
    HD_CHECK_EQ(one.Render("アルガルド"), std::string("아르가르드"));
    HD_CHECK(Scan(snap, "アルくんが来た。\nアルガルドは王子だ。").Names().empty());

    names::Registry u(snap);
    u.SetName("アル", "알");
    for (const std::string& line : {std::string("アル君、おはよう。"), std::string("アル君、おはよう。"), std::string("アルガルド様が来た。"), std::string("アルガルド様が来た。")}) u.Add(line);
    HD_CHECK_EQ(u.Render("アル"), std::string("알"));
    HD_CHECK_EQ(u.Render("アルガルド"), std::string("아르가르드"));

    names::Registry chain = Scan(snap, "ソード・ワールドの話。");
    HD_CHECK(chain.Names().empty());

    names::Registry m = Scan(snap, "ユフィリア・マゼンタは目を見開いた。\nグランツ・マゼンタ公爵");
    HD_CHECK_EQ(m.Render("グランツ・マゼンタ"), std::string("그란츠 마젠타"));
    HD_CHECK_EQ(m.Render("マゼンタ"), std::string("마젠타"));
    HD_CHECK_EQ(m.Render("ユフィリア"), std::string("유피리아"));
    m.SetName("ユフィリア", "유필리아");
    HD_CHECK_EQ(m.Render("ユフィリア・マゼンタ"), std::string("유필리아 마젠타"));
}

HD_TEST(names_given_split) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"椎名周が来た。\n周は笑った。", "周"},
        {"椎名樹、それが彼の名だ。", "樹"},
    };
    for (const auto& [doc, given] : cases) {
        names::Registry r = Scan(snap, doc);
        const std::vector<std::string> names = r.Names();
        HD_CHECK(std::find(names.begin(), names.end(), given) != names.end());
    }
    const std::vector<std::pair<std::string, std::string>> absent = {
        {"椎名周りの人", "周"},
        {"田中家に行った。", "家"},
        {"田中達が来た。", "達"},
        {"周は笑った。", "周"},
    };
    for (const auto& [doc, given] : absent) {
        names::Registry r = Scan(snap, doc);
        const std::vector<std::string> names = r.Names();
        HD_CHECK(std::find(names.begin(), names.end(), given) == names.end());
    }
}

HD_TEST(names_shared_tables) {
    names::Registry seed(nullptr);
    HD_REQUIRE(seed.LoadReadings("風音\tかざね\n"));
    seed.SetName("風音", "카자네");
    names::Registry first = seed;
    names::Registry second = seed;
    first.Add("|風音《かざね》");
    second.Add("|風音《かざね》");
    HD_CHECK_EQ(first.Render("風音"), "카자네");
    HD_CHECK_EQ(first.Key(), second.Key());
    const std::string key = second.Key();
    first.SetName("風音", "카논");
    HD_CHECK_EQ(first.Render("風音"), "카논");
    HD_CHECK_EQ(second.Key(), key);
    HD_REQUIRE(first.LoadReadings("風音\tかのん\n"));
    seed.Add("|風音《かざね》");
    HD_CHECK_EQ(seed.Key(), key);
}

HD_TEST(names_registry_forget_evidence) {
    const std::vector<std::string> lines = {
        "光「こんにちは」", "|竜宮《りゅうぐう》", "光さん、",
        "茜「こんばんは」", "|竜宮《たつみや》", "光さん、"};
    names::Registry retained(nullptr);
    std::vector<names::Registry::Evidence> evidence(lines.size());
    for (std::size_t i = 0; i < lines.size(); i++) retained.Add(lines[i], &evidence[i]);
    for (std::size_t first = 0; first < lines.size(); first++) {
        names::Registry fresh(nullptr);
        for (std::size_t i = first; i < lines.size(); i++) fresh.Add(lines[i]);
        // Rendering may derive part readings, so compare a document copy.
        names::Registry document(retained);
        HD_CHECK_EQ(document.Key(), fresh.Key());
        retained.Forget(evidence[first]);
    }
}

HD_TEST(names_registry_forget_empty_reading) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    const std::vector<std::string> lines = {
        "竜宮（りゅう）", "竜宮（・・）", "竜宮（たつみや）", "光さん、",
        "define a = Character(\"光\")", "define a = Character(\"茜\")",
        "|風音《かざね》", "|風音《かのん》", "竜宮（・・）", "光さん、"};
    names::Registry retained(snap);
    std::vector<names::Registry::Evidence> evidence(lines.size());
    for (std::size_t i = 0; i < lines.size(); i++) retained.Add(lines[i], &evidence[i]);
    for (std::size_t first = 0; first < lines.size(); first++) {
        names::Registry fresh(snap), document(retained);
        for (std::size_t i = first; i < lines.size(); i++) fresh.Add(lines[i]);
        HD_CHECK_EQ(document.Key(), fresh.Key());
        retained.Forget(evidence[first]);
    }
}

HD_TEST(names_registry_forget_actors) {
    names::Registry retained(nullptr);
    names::Registry::Evidence first, second;
    retained.Add("define a = Character(\"光\")", &first);
    retained.Add("define a = Character(\"茜\")", &second);
    retained.Forget(first);
    names::Registry document(retained), fresh(nullptr);
    document.Add("a \"こんにちは\"");
    fresh.Add("define a = Character(\"茜\")");
    fresh.Add("a \"こんにちは\"");
    HD_CHECK_EQ(document.Key(), fresh.Key());
    retained.Forget(second);
    document = retained;
    document.Add("a \"こんにちは\"");
    fresh = names::Registry(nullptr);
    fresh.Add("a \"こんにちは\"");
    HD_CHECK_EQ(document.Key(), fresh.Key());
}

// An honorific after a common name is not read as the given name (太郎君 is not 타로 키미).
HD_TEST(names_historical_honorific_not_given) {
    const dict::Snapshot* snap = Dictionary();
    HD_REQUIRE(snap != nullptr);
    for (const char* jp : {"太郎君は走った。", "次郎殿が来た。", "一郎様、どうぞ。"}) {
        const std::string line = jp;
        const std::string rows = names::HistoricalNames(*snap, {&line, 1});
        HD_CHECK(rows.find("君") == std::string::npos && rows.find("殿") == std::string::npos &&
                 rows.find("様") == std::string::npos);
    }
}
