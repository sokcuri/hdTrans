// hd8xfer: runs the construction matcher on ir.jsonl lines (IR.md §7; test and measurement driver).
//
//   hd8xfer --data DIR [--all] < ir.jsonl > cx.jsonl     winners per line (--all: also every candidate)
//   hd8xfer --data DIR --cases CASES.ir.jsonl            fix/keep cases; each IR line carries "case": "<n>",
//                                                         the index into the loaded case list
//   hd8xfer --data DIR --lint                            load errors and lint findings, exit 1 if any
//   hd8xfer --data DIR --list-cases                      every case as "n<TAB>id<TAB>kind<TAB>ja"
// DIR is the transfer data folder (poc08/transfer/data). Exit: 0 ok, 1 failures, 2 usage or load error.
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

#include "ir/ir_json.h"
#include "transfer.h"

namespace {

bool ReadAll(std::istream& in, std::vector<hd08::Doc>& docs) {
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string err;
  if (!hd08::ReadJsonl(text, docs, err)) {
    std::cerr << "hd8xfer: " << err << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string data, casesFile;
  bool all = false, lint = false, list = false, timing = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) data = argv[++i];
    else if (a == "--cases" && i + 1 < argc) casesFile = argv[++i];
    else if (a == "--all") all = true;
    else if (a == "--lint") lint = true;
    else if (a == "--list-cases") list = true;
    else if (a == "--time") timing = true;
    else {
      std::cerr << "usage: hd8xfer --data DIR [--all | --time | --cases FILE | --lint | --list-cases]\n";
      return 2;
    }
  }
  if (data.empty()) {
    std::cerr << "hd8xfer: --data is required\n";
    return 2;
  }
  hd08::xfer::Table t;
  if (!hd08::xfer::LoadTable(data, t)) {
    for (const auto& e : t.errors) std::cerr << e << "\n";
    return 2;
  }
  if (lint) {
    for (const auto& e : t.errors) std::cout << e << "\n";
    std::cout << t.rows.size() << " rows, " << t.cases.size() << " cases, " << t.errors.size() << " findings\n";
    return t.errors.empty() ? 0 : 1;
  }
  if (list) {
    for (size_t i = 0; i < t.cases.size(); ++i)
      std::cout << i << "\t" << t.cases[i].id << "\t" << t.cases[i].kind << "\t" << t.cases[i].ja << "\n";
    return 0;
  }
  std::vector<hd08::Doc> docs;
  if (!casesFile.empty()) {
    std::ifstream in(casesFile, std::ios::binary);
    if (!in || !ReadAll(in, docs)) return 2;
    size_t pass = 0, fail = 0;
    for (const auto& doc : docs)
      for (const auto& line : doc.lines) {
        size_t ci = static_cast<size_t>(std::strtoul(line.name.c_str(), nullptr, 10));
        if (ci >= t.cases.size()) continue;
        const auto& c = t.cases[ci];
        auto view = hd08::xfer::MakeView(doc, line, t);
        auto r = hd08::xfer::Transfer(t, view);
        bool fired = false;
        std::string got;
        for (const auto& m : r.chosen) {
          const auto& id = t.rows[m.row].id;
          fired = fired || id == c.id;
          got += (got.empty() ? "" : ",") + id;
        }
        bool ok = c.kind == "fix" ? fired : !fired;
        (ok ? pass : fail) += 1;
        if (!ok)
          std::cout << "FAIL\t" << c.id << "\t" << c.kind << "\t" << c.ja << "\tgot=" << (got.empty() ? "-" : got)
                    << "\t" << c.file << "\n";
      }
    std::cout << "cases: " << pass << " pass, " << fail << " fail\n";
    return fail == 0 ? 0 : 1;
  }
  if (!ReadAll(std::cin, docs)) return 2;
  std::chrono::steady_clock::duration spent{};
  size_t lines = 0;
  for (const auto& doc : docs)
    for (const auto& line : doc.lines) {
      auto t0 = std::chrono::steady_clock::now();
      auto view = hd08::xfer::MakeView(doc, line, t);
      auto r = hd08::xfer::Transfer(t, view);
      spent += std::chrono::steady_clock::now() - t0;
      ++lines;
      std::cout << hd08::xfer::ResultJson(t, view, r, all) << "\n";
    }
  if (timing) {
    double ms = std::chrono::duration<double, std::milli>(spent).count();
    std::cerr << lines << " lines, view+match " << ms << " ms, " << (ms > 0 ? lines * 1000.0 / ms : 0) << " lines/s\n";
  }
  return 0;
}
