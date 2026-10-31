// Minimal TSV reading for the data tools: `#` lines are comments, the first other line is the header.
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace hdTrans::attach::ana {

inline std::vector<std::string> SplitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

inline std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

template <class Fn>
bool ReadTsv(const std::string& path, Fn fn, std::string* err, bool header = true) {
    std::ifstream in(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);  // UTF-8 path
    if (!in) {
        *err = "cannot open " + path;
        return false;
    }
    std::string line;
    bool seenHeader = !header;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (!seenHeader) {
            seenHeader = true;
            continue;
        }
        fn(SplitOn(line, '\t'));
    }
    return true;
}

}  // namespace hdTrans::attach::ana
