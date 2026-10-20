#include "core/dict/codes.h"

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace hdTrans::dict {

PartCode::PartCode(std::string_view text) {
    const std::size_t n = std::min<std::size_t>(text.size(), 4);
    std::memcpy(bytes_.data(), text.data(), n);
    size_ = static_cast<uint8_t>(std::min<std::size_t>(text.size(), 255));
}

WordClass PartCode::Class() const {
    const char c = ClassDigit();
    if (c < '0' || c > '9') return WordClass::Other;
    return static_cast<WordClass>(c - '0');
}

char GramCodes::PunctDigit() const {
    const std::string_view s = Text();
    const std::size_t j = s.find('J');
    if (j == std::string_view::npos || j + 1 >= s.size()) return 0;
    const char d = s[j + 1];
    return d >= '0' && d <= '7' ? d : 0;
}

bool PosCodes(std::string_view pos, std::string_view& partText, std::string_view& sem, std::string_view& gram) {
    // tag[,cType[,form]]; a cType of "" or "*" is not part of the tag.
    std::string_view tag = pos, rest, cType, form;
    if (const std::size_t c = pos.find(','); c != std::string_view::npos) {
        tag = pos.substr(0, c);
        rest = pos.substr(c + 1);
    }
    cType = rest;
    if (const std::size_t c = rest.find(','); c != std::string_view::npos) {
        cType = rest.substr(0, c);
        form = rest.substr(c + 1);
    }
    std::string key(tag);
    if (!cType.empty() && cType != "*") {
        key += ',';
        key += cType;
    }
    const auto table = internal::PosTagTable();
    const auto it = std::lower_bound(table.begin(), table.end(), key,
                                     [](const internal::PosTag& t, const std::string& k) { return t.tag < std::string_view(k); });
    if (it != table.end() && it->tag == std::string_view(key)) {
        if (form.empty() || form == "*" || form == "終止形-一般" || form == "語幹-一般" || form == "基本形" ||
            form == "terminal-general" || form == "stem-general") {
            partText = it->part;
            sem = it->sem;
            gram = it->gram;
            return !partText.empty();
        }
        return false;
    }
    if (pos.find_first_of("-,") != std::string_view::npos || (!pos.empty() && pos[0] >= 'a' && pos[0] <= 'z')) {
        return false;
    }
    partText = pos;
    sem = {};
    gram = {};
    return true;
}

std::string PartPatternOf(char field, std::string_view value) {
    const auto table = internal::PartPatternTable();
    auto lookup = [&](std::string_view name) -> std::string_view {
        const auto it = std::lower_bound(table.begin(), table.end(), std::pair<char, std::string_view>(field, name),
                                         [](const internal::PartPattern& p, const std::pair<char, std::string_view>& key) {
                                             return p.field[0] < key.first ||
                                                    (p.field[0] == key.first && std::string_view(p.name) < key.second);
                                         });
        return it != table.end() && it->field[0] == field && it->name == name ? std::string_view(it->pattern)
                                                                             : std::string_view();
    };
    if (const std::string_view pattern = lookup(value); !pattern.empty()) return std::string(pattern);
    if (value.find('+') == std::string_view::npos) return std::string(value);
    std::string joined;
    for (std::size_t start = 0;;) {
        const std::size_t plus = value.find('+', start);
        const std::string_view part = value.substr(start, plus == std::string_view::npos ? std::string_view::npos
                                                                                        : plus - start);
        const std::string_view pattern = lookup(part);
        if (pattern.empty()) return std::string(value);
        joined += pattern;
        if (plus == std::string_view::npos) return joined;
        start = plus + 1;
    }
}

std::string_view PartNameOf(std::string_view code) {
    const auto schema = internal::PartSchema();
    const auto it = std::lower_bound(schema.begin(), schema.end(), code,
                                     [](const internal::PartName& p, std::string_view key) {
                                         return std::string_view(p.code) < key;
                                     });
    if (it != schema.end() && it->code == code) return it->name;
    return {};
}

std::string MarkerClassError(std::string_view target, PartCode part) {
    const std::size_t n = target.size();
    if (n < 4 || target[n - 1] != '@') return {};
    const char marker = target[n - 4];
    WordClass want;
    std::string_view name;
    switch (marker) {
    case '&': want = WordClass::Verb; name = "4 (verb)"; break;
    case '$': want = WordClass::Adjective; name = "7 (adjective)"; break;
    case '%': want = WordClass::AdjectivalNoun; name = "8 (adjectival noun)"; break;
    default: return {};
    }
    if (part.Class() == want) return {};
    return "the marker family " + std::string(target.substr(n - 4)) + " conjugates class " + std::string(name) +
           " stems, not the part code " + std::string(part.Text());
}

}  // namespace hdTrans::dict
