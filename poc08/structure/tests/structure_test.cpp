// Table tests: tests/cases/*.cases.tsv over the teacher tokens in tests/fixtures/*.toks.jsonl.
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../tools/io.h"
#include "../../../tests/unit/hdtest.h"

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

struct Fixture {
    hd8::Strings st;
    hd8::StructureData data;
    std::map<std::string, std::string> irBySrc;  // ja → structure IR JSON line
};

Fixture& Load() {
    static Fixture f;
    static bool done = false;
    if (done) return f;
    done = true;
    std::string dir = HD8_STRUCTURE_DATA, err;
    if (!hd8::LoadStructureData(ReadFile(dir + "/aux.tsv"), ReadFile(dir + "/ud.tsv"), ReadFile(dir + "/forms.tsv"),
                                ReadFile(dir + "/head.tsv"), f.st, f.data, err)) {
        hdtest::Fail(__FILE__, __LINE__, "data: " + err);
        return f;
    }
    std::istringstream in(ReadFile(std::string(HD8_STRUCTURE_TESTS) + "/fixtures/clauses.toks.jsonl"));
    std::string s;
    while (std::getline(in, s)) {
        hd8json::Value v;
        hd8::Line line;
        if (!hd8json::Parser(s).Parse(v) || !hd8io::ReadLine(v, f.st, line)) continue;
        auto info = hd8::Annotate(f.data, f.st, line.path.toks);
        hd8::Chunk(f.data, f.st, info, line.path);
        hd8::AttachHeads(f.data, f.st, info, line.path, line.decisions, f.st);
        hd8::BuildClauses(f.data, f.st, info, line.path, line.decisions);
        std::ostringstream out;
        hd8io::WriteIr(line, f.st, out);
        f.irBySrc[line.src] = out.str();
    }
    return f;
}

bool HasString(const hd8json::Value* arr, const std::string& s) {
    if (arr == nullptr) return false;
    for (const auto& x : arr->arr)
        if (x.str == s) return true;
    return false;
}

}  // namespace

HD_TEST(StructureClauseCases) {
    Fixture& f = Load();
    std::istringstream cases(ReadFile(std::string(HD8_STRUCTURE_TESTS) + "/cases/clauses.cases.tsv"));
    std::string row;
    bool header = true;
    int n = 0;
    while (std::getline(cases, row)) {
        if (row.empty() || row[0] == '#') continue;
        if (header) {
            header = false;
            continue;
        }
        auto col = Split(row, '\t');
        HD_REQUIRE(col.size() >= 4);
        const std::string &id = col[0], &ja = col[1], &pred = col[2];
        auto it = f.irBySrc.find(ja);
        if (it == f.irBySrc.end()) {
            hdtest::Fail(__FILE__, __LINE__, id + ": no fixture for " + ja);
            continue;
        }
        hd8json::Value ir;
        hd8json::Parser(it->second).Parse(ir);
        const hd8json::Value* clauses = ir.Get("clauses");
        const hd8json::Value* buns = ir.Get("buns");
        const hd8json::Value* c = nullptr;
        if (clauses != nullptr && !clauses->arr.empty()) {
            if (pred == "$") {
                c = &clauses->arr.back();
            } else {
                for (const auto& x : clauses->arr)
                    if (x.S("predText").substr(0, pred.size()) == pred) {
                        c = &x;
                        break;
                    }
            }
        }
        if (c == nullptr) {
            hdtest::Fail(__FILE__, __LINE__, id + ": no clause " + pred);
            continue;
        }
        ++n;
        for (const auto& want : Split(col[3], ' ')) {
            if (want.empty()) continue;
            bool ok = false;
            std::string got;
            size_t plus = want.find("+=");
            size_t eq = want.find('=');
            if (plus != std::string::npos) {
                std::string key = want.substr(0, plus), val = want.substr(plus + 2);
                ok = HasString(c->Get(key == "modality" ? "modality" : "jreg"), val);
            } else if (want.compare(0, 4, "arg=") == 0) {
                auto rl = Split(want.substr(4), ':');
                if (const auto* args = c->Get("args"))
                    for (const auto& a : args->arr)
                        if (a.S("role") == rl[0] && rl.size() > 1) {
                            int b = static_cast<int>(a.N("bun", -1));
                            std::string lemma(a.S("lemma"));
                            std::string surf = b >= 0 && buns ? std::string(buns->arr[b].S("csurf")) : "";
                            if (lemma == rl[1] || surf == rl[1]) ok = true;
                        }
            } else if (eq != std::string::npos) {
                got = std::string(c->S(want.substr(0, eq)));
                ok = got == want.substr(eq + 1);
            }
            if (!ok) hdtest::Fail(__FILE__, __LINE__, id + ": " + want + " (got " + got + ") in " + it->second);
        }
    }
    HD_CHECK(n > 50);
}

int main() {
    int failed = 0;
    for (const auto& t : hdtest::Registry()) {
        hdtest::Cur() = hdtest::State{};
        t.fn();
        if (hdtest::Cur().failures) {
            ++failed;
            std::fprintf(stderr, "FAIL %s (%d)\n", t.name, hdtest::Cur().failures);
        }
    }
    std::fprintf(stderr, "%zu tests, %d failed\n", hdtest::Registry().size(), failed);
    return failed ? 1 : 0;
}
