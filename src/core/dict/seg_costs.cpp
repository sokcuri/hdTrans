#include "core/dict/seg_costs.h"

#include <charconv>
#include <string>

namespace hdTrans::dict {

std::vector<std::pair<int, std::string>> ParseSegCosts(std::string_view text, SegCosts& out) {
    std::vector<std::pair<int, std::string>> errors;
    std::array<bool, kSegCostCount> seen{};
    int line = 1;  // the header
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view row = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        if (pos == 1 && row.empty()) continue;  // the newline that ended the header
        line++;
        while (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        if (row.empty() || row.starts_with('#')) continue;
        const std::size_t t1 = row.find('\t');
        const std::size_t t2 = t1 == std::string_view::npos ? t1 : row.find('\t', t1 + 1);
        if (t1 == std::string_view::npos || t2 == std::string_view::npos) {
            errors.emplace_back(line, "a row is name, value, reason");
            continue;
        }
        const std::string_view name = row.substr(0, t1);
        const std::string_view value = row.substr(t1 + 1, t2 - t1 - 1);
        std::size_t k = 0;
        while (k < kSegCostNames.size() && kSegCostNames[k] != name) k++;
        if (k == kSegCostNames.size()) {
            errors.emplace_back(line, "no seg cost is named " + std::string(name));
            continue;
        }
        int v = 0;
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
        if (ec != std::errc() || end != value.data() + value.size() || v < 0 || v > kSegCostMax || row.size() == t2 + 1) {
            errors.emplace_back(line, "the value of " + std::string(name) + " is no whole number from 0 to " + std::to_string(kSegCostMax) +
                                          ", or the reason is missing");
            seen[k] = true;  // reported here, not again as missing
            continue;
        }
        if (seen[k]) errors.emplace_back(line, "the seg cost " + std::string(name) + " comes twice");
        seen[k] = true;
        out.values[k] = v;
    }
    for (std::size_t k = 0; k < seen.size(); k++) {
        if (!seen[k]) errors.emplace_back(0, "the seg cost " + std::string(kSegCostNames[k]) + " is missing");
    }
    out.loaded = errors.empty();
    return errors;
}

}  // namespace hdTrans::dict
