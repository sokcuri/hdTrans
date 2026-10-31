// The Korean generator: conjugation by eomi.tsv notation (conj.gold.tsv: lemma, forms, the written word),
// whole lines from nodes, and the readings of a word found by analysis by synthesis.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "core/register/generate.h"
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

std::string Key(const reg::Bundle& b) {
    const reg::Node& v = b.node;
    return std::to_string(static_cast<int>(v.ctype)) + "|" + v.eomi + "|" + v.rel + "|" +
           std::to_string(static_cast<int>(v.mood)) + "|" + std::to_string(static_cast<int>(v.tense)) + "|" + v.modal + "|" +
           reg::Name(b.level) + "|" + (b.hon ? "hon" : "");
}

}  // namespace

HD_TEST(register_conjugation_gold) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    std::string text;
    HD_REQUIRE(Read(RepoDir() + "/tests/unit/register/conj.gold.tsv", text));
    int rows = 0;
    for (const std::string& line : reg::Split(text, '\n')) {
        if (line.empty() || line[0] == '#' || line.starts_with("id\t")) continue;
        const std::vector<std::string> c = reg::Split(line, '\t');
        HD_REQUIRE(c.size() >= 7);
        const auto opt = [](const std::string& s) { return s == "-" ? std::string() : s; };
        std::vector<std::string> forms;
        for (const std::string& f : reg::Split(c[4], ' '))
            if (!f.empty()) forms.push_back(f);
        const std::string got = reg::ConjugateForms(d->ko, c[1], opt(c[2]), opt(c[3]), forms, opt(c[6]));
        bool ok = false;
        for (const std::string& want : reg::Split(c[5], '|')) ok = ok || got == want;
        if (!ok) hdtest::Fail(__FILE__, __LINE__, c[0] + ": got " + got + ", want " + c[5]);
        rows++;
    }
    HD_CHECK(rows > 1000);
    // a lemma that is a verb with the honorific 시 (가시다 = 가다 + -시-) contracts as the prefinal does
    HD_CHECK_EQ(reg::ConjugateForms(d->ko, "가시다", "", "", {"-어요"}), std::string("가세요"));
}

HD_TEST(register_generate_lines) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    const auto node = [](uint32_t id, reg::Kind kind, const char* lemma) {
        reg::Node n;
        n.id = id;
        n.kind = kind;
        n.lemma = lemma;
        return n;
    };
    reg::Node subj = node(2, reg::Kind::P, "");
    subj.kase = "top";
    const std::vector<reg::Node> nodes = {node(1, reg::Kind::N, "나"), subj, node(3, reg::Kind::N, "학생"),
                                          node(4, reg::Kind::V, "이다")};
    reg::UttLevel speech;
    speech.level = reg::Level::hae;
    HD_CHECK_EQ(reg::Generate(d->ko, nodes, {}, speech), std::string("난 학생이야"));
    reg::UttLevel narration;
    narration.level = reg::Level::haera;
    narration.narration = true;
    HD_CHECK_EQ(reg::Generate(d->ko, nodes, {}, narration), std::string("나는 학생이다"));
    reg::UttLevel polite;
    polite.level = reg::Level::haeyo;
    HD_CHECK_EQ(reg::Generate(d->ko, nodes, {}, polite), std::string("난 학생이에요"));
}

// A reading found by its surface is one of the paradigm's readings with that surface, in paradigm order.
HD_TEST(register_readings_by_surface) {
    const reg::Data* d = reg::LoadedData();
    HD_REQUIRE(d != nullptr);
    struct Lemma {
        const char* lemma;
        const char* host;
    };
    const Lemma lemmas[] = {{"가다", ""},     {"하다", ""},   {"듣다", ""},   {"돕다", ""},   {"모르다", ""},
                            {"있다", ""},     {"예쁘다", ""}, {"짓다", ""},   {"그렇다", ""}, {"살다", ""},
                            {"주다", ""},     {"이다", "학생"}, {"이다", "친구"}, {"아니다", ""}, {"기다리다", ""}};
    for (const Lemma& l : lemmas) {
        reg::ParadigmIndex all(d->ko, l.lemma, "", "", l.host);
        std::map<std::string, std::vector<std::string>> bySurface;
        for (const auto& [surface, b] : all.All()) bySurface[surface].push_back(Key(b));
        reg::ParadigmIndex found(d->ko, l.lemma, "", "", l.host);
        for (const auto& [surface, keys] : bySurface) {
            std::vector<std::string> got;
            for (const uint32_t r : found.Find(surface)) got.push_back(Key(found.At(r)));
            if (got != keys) hdtest::Fail(__FILE__, __LINE__, std::string(l.lemma) + " " + surface + ": other readings");
        }
        HD_CHECK(found.Find("없는말").empty());
    }
}
