#pragma once
// The premodern names of a document: the names of persons written with titles and
// court offices (朽木左兵衛尉成綱, 上総介三郎平朝臣信長), read into plain-dictionary rows.
#include <map>
#include <set>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>
#include <string>

#include "core/dict/snapshot.h"

namespace hdTrans::names {

struct HistoricalReading {
    std::string text;
    std::size_t run = 0;
};

// Line-local scans, before resolving list entries against the document's surnames.
struct HistoricalLine {
    std::map<std::string, HistoricalReading> found;
    std::set<std::string> surnames;
    // The surnames and common names of names that start in a surname of the dictionary, which are
    // read as names even where they are common words.
    std::set<std::string> strong;
    std::vector<std::vector<char32_t>> listed;
};

HistoricalLine ReadHistoricalLine(const dict::Snapshot& snap, std::string_view line);
std::string HistoricalRows(const dict::Snapshot& snap, std::span<const HistoricalLine* const> lines);

// The rows JP TAB KR TAB name, one per line and sorted by JP, of the premodern names
// in lines. Among the readings of a surface found in several runs, the longest run wins.
std::string HistoricalNames(const dict::Snapshot& snap, std::span<const std::string> lines);

// The kana reading of prefix as the prefix of a common name (新 しん, 孫 まご, 鶴 つる), ""
// for none.
std::string_view CommonNamePrefix(std::string_view prefix);

}  // namespace hdTrans::names
