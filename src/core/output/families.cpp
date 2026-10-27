#include "core/output/families.h"

#include <algorithm>
#include <atomic>

#include "core/output/markers.h"

namespace hdTrans::output {

namespace {

int MarkerIndex(char marker) {
    switch (marker) {
        case '&': return 0;
        case '$': return 1;
        case '%': return 2;
    }
    return -1;
}

}  // namespace

std::size_t Families::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rows_.size();
}

std::string_view Families::Row(std::string_view key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (rows_.empty()) return {};
    const auto it = rows_.find(key);
    return it == rows_.end() ? std::string_view() : std::string_view(it->second);
}

namespace {

// The id of the n-th family of marker that Generate makes; "" when the ids are used up.
std::string IdAt(char marker, int n) {
    constexpr std::string_view kSecond = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const std::string_view used = MarkerFamilyLetters(marker);
    for (char first = 'A'; first <= 'z'; first++) {
        if (used.find(first) != std::string_view::npos || (first > 'Z' && first < 'a')) continue;
        if (n < static_cast<int>(kSecond.size())) return {marker, first, kSecond[n], '@'};
        n -= static_cast<int>(kSecond.size());
    }
    return {};
}

}  // namespace

bool Families::IdTaken(std::string_view id) const {
    for (size_t c = 0; c < kFormCodes.size(); c++) {
        if (rows_.count(std::string(id) + kFormCodes[c]) > 0) return true;
    }
    return false;
}

std::string Families::Generate(char marker, const FamilyRows& rows) {
    const int m = MarkerIndex(marker);
    if (m < 0) return {};
    std::string sig(1, marker);
    for (const auto& row : rows) {
        sig += '\0';
        sig += row;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = ids_.find(sig); it != ids_.end()) return it->second;
    std::string id;
    for (id = IdAt(marker, count_[m]); !id.empty() && IdTaken(id); id = IdAt(marker, count_[m])) count_[m]++;
    if (id.empty()) return {};
    count_[m]++;
    ids_.emplace(std::move(sig), id);
    for (size_t c = 0; c < rows.size(); c++) {
        if (!rows[c].empty()) rows_[id + kFormCodes[c]] = rows[c];
    }
    return id;
}

bool Families::Insert(char marker, std::string_view id, const FamilyRows& rows) {
    const int m = MarkerIndex(marker);
    if (m < 0 || id.size() != 4 || id[0] != marker || id[3] != '@') return false;
    std::string sig(1, marker);
    for (const auto& row : rows) {
        sig += '\0';
        sig += row;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (IdTaken(id) || ids_.count(sig) > 0) return false;
    ids_.emplace(std::move(sig), std::string(id));
    for (size_t c = 0; c < rows.size(); c++) {
        if (!rows[c].empty()) rows_[std::string(id) + kFormCodes[c]] = rows[c];
    }
    return true;
}

std::string_view LookupSource(std::string_view key, const Families& generated) {
    const std::string_view row = SourceMarkerRow(key);
    return row.empty() ? generated.Row(key) : row;
}

std::string_view LookupDollar(std::string_view key, const Families& generated) {
    const std::string_view row = DollarMarkerRow(key);
    return row.empty() ? generated.Row(key) : row;
}

std::string_view LookupPercent(std::string_view key, const Families& generated) {
    const std::string_view row = PercentMarkerRow(key);
    return row.empty() ? generated.Row(key) : row;
}

std::string_view LookupByMarker(std::string_view key, const Families& generated) {
    switch (key.empty() ? 0 : key[0]) {
        case '%': return LookupPercent(key, generated);
        case '$': return LookupDollar(key, generated);
        case '&': return LookupSource(key, generated);
    }
    return {};
}

namespace {

bool FamilyChar(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

std::atomic<std::uint64_t>& Unexpanded() {
    static std::atomic<std::uint64_t> count{0};
    return count;
}

}  // namespace

std::uint64_t UnexpandedMarkers() { return Unexpanded().load(std::memory_order_relaxed); }

std::string DictionaryForms(std::string text, const Families& generated) {
    for (size_t at = 3; (at = text.find('@', at)) != std::string::npos;) {
        const size_t start = at - 3;
        if (MarkerIndex(text[start]) < 0 || !FamilyChar(text[start + 1]) || !FamilyChar(text[start + 2])) {
            at++;
            continue;
        }
        std::string key = text.substr(start, 4);
        key += '1';
        const std::string_view stem = LookupByMarker(key, generated);
        const std::string form = stem.empty() ? std::string() : std::string(stem) + "다";
        text.replace(start, 4, form);
        Unexpanded().fetch_add(1, std::memory_order_relaxed);
        at = std::max<size_t>(start + form.size(), 3);
    }
    return text;
}

std::string Lemma(std::string_view target, const Families& generated) {
    const size_t n = target.size();
    if (n < 4 || target[n - 1] != '@' || MarkerIndex(target[n - 4]) < 0) return std::string(target);
    std::string key(target.substr(n - 4));
    key += '1';
    std::string out(target.substr(0, n - 4));
    out += LookupByMarker(key, generated);
    out += "다";
    return out;
}

}  // namespace hdTrans::output
