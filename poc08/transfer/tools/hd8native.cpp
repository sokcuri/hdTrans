// hd8native: the native route alone (DESIGN-v3 §3), for the lint, the row cases and the level grids.
//   hd8native [--data DIR] lint                 rows: spec ids, cases, lists; exit 1 on an error
//   hd8native [--data DIR] cases [--verbose]    every row case through the native route; exit 1 on a failure
//   hd8native [--data DIR] run [--each] [--route FILE] < ja   native output per line (uncovered lines too)
//   hd8native [--data DIR] facts < ja           per token: surface, lemma, pos, form, the aux.tsv rows and facts
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "driver/driver.h"
#include "ir/file.h"
#include "native.h"

using namespace hd08;

namespace {

int Usage() {
    std::fputs("usage: hd8native [--data DIR] lint | cases [--verbose] | run [--each] [--route FILE]\n", stderr);
    return 2;
}

// One document per line (each) or all lines in one document, through the native route.
struct Out {
    std::string text;
    int route = 0;
    std::string unmatched, rules;
};

std::vector<Out> Translate(const Engine& e, const std::vector<std::string>& lines, bool each) {
    std::vector<Out> out;
    auto runDoc = [&](Doc& doc) {
        Analyse(e, doc);
        for (Line& l : doc.lines) {
            RunNative(e, doc, l);
            Out o;
            o.text = l.out;
            o.route = l.route;
            if (l.route != 1) {
                // the native-only text of an uncovered line, for reading (G runs on it anyway)
                Config rc = e.cfg;
                if (rc.speech == Speech::Strict || rc.speech == Speech::Soft) rc.speech = Speech::Auto;
                StageR(*e.discourse, rc, doc, l);
                l.kir2 = l.kir;
                for (KNode& n : l.kir2) n.origin = n.id;
                StageG(*e.generator, e.cfg, doc, l);
                o.text = l.out;
            }
            for (uint32_t u : l.unmatched) o.unmatched += (o.unmatched.empty() ? "" : " ") + std::string(doc.S(u));
            for (const Decision& d : l.decisions)
                if (d.stage == 'T' && doc.S(d.rule).starts_with("xfer:"))
                    o.rules += (o.rules.empty() ? "" : " ") + std::string(doc.S(d.rule).substr(5));
            out.push_back(std::move(o));
        }
    };
    if (each) {
        for (const std::string& s : lines) {
            Doc doc;
            Line l;
            l.id = 1;
            l.src = s;
            doc.lines.push_back(std::move(l));
            runDoc(doc);
        }
    } else {
        Doc doc;
        for (size_t i = 0; i < lines.size(); i++) {
            Line l;
            l.id = static_cast<uint32_t>(i + 1);
            l.src = lines[i];
            doc.lines.push_back(std::move(l));
        }
        runDoc(doc);
    }
    return out;
}

int Lint(const std::string& data) {
    nat::Data d;
    nat::Load(data, d);
    int errors = 0;
    for (const std::string& e : d.errors) {
        std::printf("error\t%s\n", e.c_str());
        errors++;
    }
    const std::vector<std::string> ids = nat::SpecIds(data + "/spec");
    const std::set<std::string> spec(ids.begin(), ids.end());
    std::map<std::string, std::pair<int, int>> cases;  // id → fix, keep
    for (const nat::Case& c : d.cases) (c.kind == "fix" ? cases[c.id].first : cases[c.id].second)++;
    std::set<std::string> rowIds;
    for (const nat::Row& r : d.rows) {
        rowIds.insert(r.id);
        auto err = [&](const std::string& what) {
            std::printf("error\t%s\t%s\t%s\n", r.file.c_str(), r.id.c_str(), what.c_str());
            errors++;
        };
        if (!spec.count(r.spec)) err("spec id " + r.spec + " is not in spec/");
        if (r.st.empty() || r.prov.empty()) err("st or prov missing");
        const auto c = cases.find(r.id);
        if (c == cases.end() || c->second.first == 0) err("no fix case");
        if (c == cases.end() || c->second.second == 0) err("no keep case");
        for (const nat::Atom& a : r.atoms)
            if (a.lemmas.size() >= 3) err("a lemma list of 3 or more: use a class or a feature");
        for (const nat::Cond& cd : r.conds)
            if ((cd.key == "lemma" || cd.key == "ko") && cd.values.size() >= 3)
                err("a lemma list of 3 or more: use a class or a feature");
    }
    for (const nat::Case& c : d.cases)
        if (!rowIds.count(c.id)) {
            std::printf("error\t%s\tcase for an unknown row %s\n", c.file.c_str(), c.id.c_str());
            errors++;
        }
    std::printf("%zu rows, %zu cases, %zu lexicon entries, %d errors\n", d.rows.size(), d.cases.size(),
                d.entries.size(), errors);
    return errors ? 1 : 0;
}

int Cases(const Engine& e, const std::string& data, bool verbose) {
    nat::Data d;
    nat::Load(data, d);
    std::vector<std::string> ja;
    for (const nat::Case& c : d.cases) ja.push_back(c.ja);
    const std::vector<Out> out = Translate(e, ja, true);
    int failed = 0;
    for (size_t i = 0; i < d.cases.size(); i++) {
        const nat::Case& c = d.cases[i];
        const Out& o = out[i];
        // ko "-": the line must go to 0.7.x (a hold row)
        bool ok = c.ko == "-" ? o.route != 1 : o.route == 1 && o.text == c.ko;
        // a fix case must be written by its row
        if (ok && c.kind == "fix" && (' ' + o.rules + ' ').find(' ' + c.id + ' ') == std::string::npos) ok = false;
        if (!ok || verbose)
            std::printf("%s\t%s\t%s\t%s\twant %s\tgot %s\t%s\t%s\n", ok ? "ok" : "FAIL", c.id.c_str(), c.kind.c_str(),
                        c.ja.c_str(), c.ko.c_str(), o.text.c_str(), o.route == 1 ? "native" : o.unmatched.c_str(),
                        o.rules.c_str());
        failed += !ok;
    }
    std::printf("%zu cases, %d failed\n", d.cases.size(), failed);
    return failed ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
#ifdef HD08_DEFAULT_DATA
    cfg.data = HD08_DEFAULT_DATA;
#endif
    std::string cmd, routeFile;
    bool each = false, verbose = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) cfg.data = argv[++i];
        else if (a == "--each") each = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--route" && i + 1 < argc) routeFile = argv[++i];
        else if (cmd.empty() && a[0] != '-') cmd = a;
        else return Usage();
    }
    if (cmd == "lint") return Lint(cfg.data);
    if (cmd != "cases" && cmd != "run" && cmd != "facts") return Usage();
    cfg.document = !each;
    Engine e;
    std::string err;
    if (!LoadEngine(cfg, e, err)) {
        std::fprintf(stderr, "hd8native: %s\n", err.c_str());
        return 1;
    }
    int rc = 0;
    if (cmd == "facts") {
        std::string l;
        while (std::getline(std::cin, l)) {
            Doc doc;
            Line line;
            line.id = 1;
            line.src = l;
            doc.lines.push_back(line);
            Analyse(e, doc);
            const Line& a = doc.lines[0];
            const std::vector<TokAux> aux = AuxOf(*e.structure, doc, a.toks);
            std::printf("%s\n", l.c_str());
            for (size_t i = 0; i < a.toks.size(); i++) {
                const Token& t = a.toks[i];
                std::string f;
                for (const std::string& x : aux[i].facts) f += (f.empty() ? "" : ",") + x;
                int bun = -1;
                for (size_t b = 0; b < a.buns.size(); b++)
                    if (i >= a.buns[b].first && i <= a.buns[b].last) bun = static_cast<int>(b);
                std::printf("  %zu\t%s\t%s\t%s\t%s\tb%d\t%s %s\t%s\n", i, std::string(a.Src(t.src)).c_str(),
                            std::string(doc.S(t.lemma)).c_str(), std::string(Name(F::pos, static_cast<unsigned>(t.pos))).c_str(),
                            doc.cforms.Str(t.cform).data(), bun, aux[i].row.c_str(), aux[i].form.c_str(), f.c_str());
            }
        }
    } else if (cmd == "cases") {
        rc = Cases(e, cfg.data, verbose);
    } else {
        std::vector<std::string> lines;
        std::string l;
        while (std::getline(std::cin, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
        const std::vector<Out> out = Translate(e, lines, each);
        std::string text, route;
        for (const Out& o : out) {
            text += o.text + '\n';
            route += std::string(o.route == 1 ? "native" : "fallback") + '\t' + o.unmatched + '\t' + o.rules + '\n';
        }
        std::fwrite(text.data(), 1, text.size(), stdout);
        if (!routeFile.empty()) {
            std::FILE* f = OpenFile(routeFile, true);
            if (!f || std::fwrite(route.data(), 1, route.size(), f) != route.size()) rc = 1;
            if (f) std::fclose(f);
        }
    }
    FreeEngine(e);
    return rc;
}
