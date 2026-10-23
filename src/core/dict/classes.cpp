#include "core/dict/classes.h"

#include <unordered_map>

#include "core/dict/builtin.h"

namespace hdTrans::dict {

namespace internal {

namespace {

std::string_view Chomp(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
    return line;
}

// Splits at the first tab: {before, after}; after is empty without one.
std::pair<std::string_view, std::string_view> Cut(std::string_view s) {
    const std::size_t tab = s.find('\t');
    if (tab == std::string_view::npos) return {s, {}};
    return {s.substr(0, tab), s.substr(tab + 1)};
}

// Editors drop a trailing space; a leading one is part of the member.
bool Trailing(std::string_view s) { return !s.empty() && s.back() == ' '; }

}  // namespace

ClassTable ParseClasses(std::span<const std::string_view> lines) {
    ClassTable t;
    std::unordered_map<std::string_view, std::size_t> byName;
    const auto fail = [&](int line, std::string message) { t.errors.push_back({line, std::move(message)}); };
    int number = 0;
    for (const std::string_view raw : lines) {
        number++;
        const std::string_view line = Chomp(raw);
        if (line.empty()) continue;
        if (line.starts_with("//=")) {
            std::string_view head = line.substr(3);
            while (head.starts_with(' ')) head.remove_prefix(1);
            const auto [set, doc] = Cut(head);
            if (set.empty() || doc.empty()) {
                fail(number, "a doc line needs a set name and a text");
            } else if (!byName.emplace(set, t.classes.size()).second) {
                fail(number, "a second doc line for " + std::string(set));
            } else {
                t.classes.push_back({set, doc, {}, {}});
            }
            continue;
        }
        if (line.starts_with("//")) continue;
        const auto [set, rest] = Cut(line);
        const std::string_view member = Cut(rest).first;
        if (set.empty() || member.empty() || Trailing(member)) {
            fail(number, "a row is SET <TAB> MEMBER, the member neither empty nor ending in a space");
            continue;
        }
        const auto found = byName.find(set);
        if (found == byName.end()) {
            fail(number, "no doc line for the set " + std::string(set));
            continue;
        }
        WordClass& c = t.classes[found->second];
        if (!c.index.insert(member).second) {
            fail(number, "a second row of " + std::string(member) + " in " + std::string(set));
            continue;
        }
        c.members.push_back(member);
    }
    for (const WordClass& c : t.classes) {
        if (c.members.empty()) fail(0, "no rows for the set " + std::string(c.name));
    }
    std::size_t size = 8;
    while (size < 2 * t.classes.size()) size *= 2;
    t.slots.assign(size, {0, -1});
    for (std::size_t k = 0; k < t.classes.size(); k++) {
        const std::uint64_t hash = ClassHash(t.classes[k].name);
        std::size_t i = hash & (size - 1);
        while (t.slots[i].second >= 0) i = (i + 1) & (size - 1);
        t.slots[i] = {hash, static_cast<int>(k)};
    }
    return t;
}

const WordClass* FindClass(const ClassTable& t, std::string_view name, std::uint64_t hash) {
    if (t.slots.empty()) return nullptr;
    const std::size_t mask = t.slots.size() - 1;
    for (std::size_t i = hash & mask; t.slots[i].second >= 0; i = (i + 1) & mask) {
        const WordClass& c = t.classes[static_cast<std::size_t>(t.slots[i].second)];
        if (t.slots[i].first == hash && c.name == name) return &c;
    }
    return nullptr;
}

const ClassTable& BuiltinClasses() {
    static const ClassTable table = [] {
        for (const BuiltinTable& b : BuiltinTables()) {
            if (b.name == "classes.tsv") return ParseClasses(b.lines);
        }
        return ClassTable{};
    }();
    return table;
}

}  // namespace internal

bool InClass(ClassName set, std::string_view surface) {
    const internal::WordClass* c = internal::FindClass(internal::BuiltinClasses(), set.name, set.hash);
    return c != nullptr && c->index.contains(surface);
}

std::span<const std::string_view> ClassMembers(ClassName set) {
    const internal::WordClass* c = internal::FindClass(internal::BuiltinClasses(), set.name, set.hash);
    if (c == nullptr) return {};
    return c->members;
}

}  // namespace hdTrans::dict
