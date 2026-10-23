#include "core/dict/classes.h"

#include <utility>

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
            } else if (!t.sets.emplace(set, WordClass{{}, {}, doc}).second) {
                fail(number, "a second doc line for " + std::string(set));
            } else {
                t.order.push_back(set);
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
        const auto found = t.sets.find(set);
        if (found == t.sets.end()) {
            fail(number, "no doc line for the set " + std::string(set));
            continue;
        }
        if (!found->second.index.insert(member).second) {
            fail(number, "a second row of " + std::string(member) + " in " + std::string(set));
            continue;
        }
        found->second.members.push_back(member);
    }
    for (const std::string_view set : t.order) {
        if (t.sets.at(set).members.empty()) fail(0, "no rows for the set " + std::string(set));
    }
    return t;
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

bool InClass(std::string_view set, std::string_view surface) {
    const internal::ClassTable& t = internal::BuiltinClasses();
    const auto found = t.sets.find(set);
    return found != t.sets.end() && found->second.index.contains(surface);
}

std::span<const std::string_view> ClassMembers(std::string_view set) {
    const internal::ClassTable& t = internal::BuiltinClasses();
    const auto found = t.sets.find(set);
    if (found == t.sets.end()) return {};
    return found->second.members;
}

}  // namespace hdTrans::dict
