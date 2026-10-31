// The stage on lines the word stage translated (regen.cases.tsv: the line, its translation and what the word
// stage resolved): the regenerated text, the lines the stage replaces, and the detectors of dialogue lines,
// caption documents, keigo and commands.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/register/reader.h"
#include "core/register/regen.h"
#include "core/register/retrofit.h"
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

struct Case {
    std::string kind, expected, ja, out;
    reg::Capture capture;  // one line
};

std::vector<Case> Cases() {
    std::vector<Case> out;
    std::string text;
    if (!Read(RepoDir() + "/tests/unit/register/regen.cases.tsv", text)) return out;
    for (const std::string& line : reg::Split(text, '\n')) {
        if (line.empty() || line[0] == '#' || line.starts_with("kind\t")) continue;
        const std::vector<std::string> c = reg::Split(line, '\t');
        if (c.size() < 4) continue;
        Case k{c[0], c[1], c[2], c[3], {}};
        k.capture.Line(0);
        for (std::size_t i = 4; i < c.size(); i++) {
            const std::vector<std::string> f = reg::Split(c[i], '|');
            if (f.size() < 8) continue;
            reg::Capture::Item it;
            it.sentence = std::stoi(f[0]);
            it.token = std::stoi(f[1]);
            it.surface = f[2];
            it.target = f[3];
            it.ending = f[4];
            it.verb = f[5] == "1";
            it.kind = std::stoi(f[6]);
            it.sourceEnding = f[7];
            k.capture.Add(0, it);
        }
        out.push_back(std::move(k));
    }
    return out;
}

}  // namespace

HD_TEST(register_regenerated_lines) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    const std::vector<Case> cases = Cases();
    HD_REQUIRE(cases.size() > 20);
    for (const Case& c : cases) {
        if (c.kind == "gen") {
            const bool dialogue = reg::DialogueLine(d->reader, c.ja);
            const std::vector<reg::PreLine> lines = {reg::ReadLine(*d, 0, c.ja, c.out, c.capture, dialogue, 0)};
            const auto regen = reg::Regenerate(*d, lines, {true}, {});
            if (regen.empty() || regen[0].gen.find(c.expected) == std::string::npos)
                hdtest::Fail(__FILE__, __LINE__, c.ja + ": " + (regen.empty() ? "" : regen[0].gen) + " lacks " + c.expected);
        } else {
            const std::string source[] = {c.ja};
            std::string out[] = {c.out};
            reg::Capture capture = c.capture;
            reg::Retrofit(source, capture, out);
            if (out[0] != c.expected) hdtest::Fail(__FILE__, __LINE__, c.ja + ": " + out[0] + " vs " + c.expected);
        }
    }
}

// Tables loaded again after the first ones are let go (they may take the same address): the readings a thread
// keeps between calls belong to the first tables and are not used for the second.
HD_TEST(register_tables_loaded_again) {
    const std::vector<Case> cases = Cases();
    HD_REQUIRE(!cases.empty());
    const auto regenerate = [&](const reg::Data& d) {
        std::vector<std::string> out;
        for (const Case& c : cases) {
            if (c.kind != "gen") continue;
            const bool dialogue = reg::DialogueLine(d.reader, c.ja);
            const std::vector<reg::PreLine> lines = {reg::ReadLine(d, 0, c.ja, c.out, c.capture, dialogue, 0)};
            const auto regen = reg::Regenerate(d, lines, {true}, {});
            out.push_back(regen.empty() ? "" : regen[0].gen);
        }
        return out;
    };
    std::unique_ptr<reg::Data> first = reg::LoadData();
    HD_REQUIRE(first != nullptr);
    const std::uint64_t firstLoad = first->loadId;
    const std::vector<std::string> before = regenerate(*first);
    first.reset();
    const std::unique_ptr<reg::Data> second = reg::LoadData();
    HD_REQUIRE(second != nullptr);
    HD_CHECK(second->loadId != firstLoad);
    HD_CHECK(second->loadId != 0);
    HD_CHECK(regenerate(*second) == before);
    HD_CHECK(regenerate(*reg::LoadedData()) == before);
}

HD_TEST(register_off_keeps_the_translation) {
    const std::vector<Case> cases = Cases();
    for (const Case& c : cases) {
        const std::string source[] = {c.ja};
        std::string out[] = {c.out};
        reg::Capture capture = c.capture;
        reg::Retrofit(source, capture, out, {false});
        HD_CHECK_EQ(out[0], c.out);
    }
}

HD_TEST(register_dialogue_lines) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    HD_CHECK(reg::DialogueLine(d->reader, "「お、意外と素直だな」"));
    HD_CHECK(reg::DialogueLine(d->reader, "　【筧】「いや、もういいです」"));
    HD_CHECK(reg::DialogueLine(d->reader, "『正解！』と鈴木が手で○を作った。"));
    HD_CHECK(!reg::DialogueLine(d->reader, "彼は「行く」と言った。"));
    HD_CHECK(!reg::DialogueLine(d->reader, "雨の音に目を覚ました。"));
}

HD_TEST(register_caption_documents) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    HD_CHECK(reg::CaptionDocument(d->reader, {"00:01:02,345 --> 00:01:04,000", "行くよ", "00:01:05,000 --> 00:01:06,000", "うん"}));
    HD_CHECK(reg::CaptionDocument(d->reader, {"（太郎）行くよ", "（花子）うん", "（太郎）早く"}));
    HD_CHECK(reg::CaptionDocument(d->reader, {"行くよ", "うん", "早くして", "待って"}));
    HD_CHECK(!reg::CaptionDocument(d->reader, {"「行くよ」", "「うん」", "「早くして」"}));
    HD_CHECK(!reg::CaptionDocument(d->reader, {}));
}

// keigo.tsv: the keigo rows of reader.tsv as the source spells them, and the imperative as the word stage
// conjugated it or as keigo.tsv spells it.
HD_TEST(register_keigo_and_command) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    struct Row {
        const char* ja;
        bool keigo, command;
    };
    const Row rows[] = {
        {"「先生がいらっしゃいました」", true, false}, {"「お待ちになってください」", true, true},
        {"「よろしくお願いします」", true, false},     {"「申し訳ありません」", false, false},
        {"「私が参ります」", true, false},             {"「参った！」", false, false},
        {"「ここにおります」", true, false},           {"「みなさん、こんにちは」", false, false},
        {"「ごめんなさい」", true, true},               {"「見てくれ」", false, true},
        {"「見てくれた」", false, false},               {"「何にせよ、助かった」", false, true},
    };
    for (const Row& r : rows) {
        reg::Capture capture;
        capture.Line(0);
        const reg::PreLine line = reg::ReadLine(*d, 0, r.ja, "", capture, true, 0);
        if (line.keigo != r.keigo || line.command != r.command)
            hdtest::Fail(__FILE__, __LINE__, std::string(r.ja) + ": keigo " + (line.keigo ? "yes" : "no") + ", command " +
                                                 (line.command ? "yes" : "no"));
    }
    // a verb the word stage read in the imperative branch, and an e-stem with only marks after it
    reg::Capture capture;
    reg::Capture::Item verb;
    verb.surface = "追い込";
    verb.verb = true;
    verb.kind = 4;
    verb.sourceEnding = "ぇ";
    capture.Add(0, verb);
    HD_CHECK(reg::ReadLine(*d, 0, "「追い込めぇ！」", "", capture, true, 0).command);
    capture.Drop(0);
    verb.sourceEnding = "";
    capture.Add(0, verb);
    reg::Capture::Item next;
    next.surface = "ない";
    capture.Add(0, next);
    HD_CHECK(!reg::ReadLine(*d, 0, "「追い込めない」", "", capture, true, 0).command);
    capture.Drop(0);
    verb.kind = 11;
    verb.sourceEnding = "ろ";
    capture.Add(0, verb);
    HD_CHECK(reg::ReadLine(*d, 0, "「見ろ」", "", capture, true, 0).command);
}
