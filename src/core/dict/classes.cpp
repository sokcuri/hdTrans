#include "core/dict/classes.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "core/dict/builtin.h"
#include "core/text/utf8.h"

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

std::string Utf8Of(char32_t cp) {
    std::string out;
    text::AppendRune(out, cp);
    return out;
}

// Fills the buckets and runes of c from its members.
void Index(WordClass& c) {
    bool single = true;
    for (std::size_t k = 0; k < c.members.size(); k++) {
        const std::string_view m = c.members[k];
        auto bucket = std::find_if(c.buckets.begin(), c.buckets.end(), [&](const ClassBucket& b) { return b.size == m.size(); });
        if (bucket == c.buckets.end()) {
            c.buckets.push_back({m.size(), {}});
            bucket = c.buckets.end() - 1;
        }
        bucket->members.emplace_back(m, k);
        const text::Rune r = text::DecodeRune(m);
        single = single && Utf8Of(r.cp) == m;
    }
    std::sort(c.buckets.begin(), c.buckets.end(), [](const ClassBucket& a, const ClassBucket& b) { return a.size < b.size; });
    for (ClassBucket& b : c.buckets) std::sort(b.members.begin(), b.members.end());
    if (single) {
        for (const std::string_view m : c.members) c.runes.push_back(text::DecodeRune(m).cp);
        std::sort(c.runes.begin(), c.runes.end());
    }
}

// The place in table order of the member of bucket b that equals s, none for none.
std::size_t PlaceIn(const ClassBucket& b, std::string_view s, std::size_t none) {
    const auto found = std::lower_bound(b.members.begin(), b.members.end(), s,
                                        [](const std::pair<std::string_view, std::size_t>& m, std::string_view v) { return m.first < v; });
    return found != b.members.end() && found->first == s ? found->second : none;
}

// The size of the first member in table order equal to at(size), the piece of s of a member's size.
template <typename At>
std::size_t FirstMember(const WordClass& c, std::string_view s, At at) {
    std::size_t first = c.members.size(), size = 0;
    for (const ClassBucket& b : c.buckets) {
        if (b.size > s.size()) break;
        const std::size_t k = PlaceIn(b, at(b.size), c.members.size());
        if (k < first) {
            first = k;
            size = b.size;
        }
    }
    return size;
}

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
                t.classes.push_back({set, doc, {}, {}, {}, {}});
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
    for (WordClass& c : t.classes) {
        if (c.members.empty()) fail(0, "no rows for the set " + std::string(c.name));
        Index(c);
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

std::size_t MemberAtStart(const WordClass& c, std::string_view s) {
    return FirstMember(c, s, [&](std::size_t n) { return s.substr(0, n); });
}

std::size_t MemberAtEnd(const WordClass& c, std::string_view s) {
    return FirstMember(c, s, [&](std::size_t n) { return s.substr(s.size() - n); });
}

bool RuneMember(const WordClass& c, char32_t cp) {
    if (!c.runes.empty()) return std::binary_search(c.runes.begin(), c.runes.end(), cp);
    return c.index.contains(Utf8Of(cp));
}

}  // namespace internal

ClassId ClassId::Of(std::string_view name) {
    struct Registry {
        std::mutex mutex;
        std::unordered_map<std::string, std::uint32_t> ids;
    };
    static Registry registry;
    const internal::WordClass* data =
        internal::FindClass(internal::BuiltinClasses(), name, internal::ClassHash(name));
    const std::lock_guard<std::mutex> lock(registry.mutex);
    const auto found = registry.ids.try_emplace(std::string(name), static_cast<std::uint32_t>(registry.ids.size())).first;
    return {found->second, data};
}

bool InClass(ClassId set, std::string_view surface) { return set.data != nullptr && set.data->index.contains(surface); }

bool InClass(std::string_view set, std::string_view surface) { return InClass(ClassId::Of(set), surface); }

std::span<const std::string_view> ClassMembers(ClassId set) {
    if (set.data == nullptr) return {};
    return set.data->members;
}

bool InClassRune(ClassId set, char32_t cp) { return set.data != nullptr && internal::RuneMember(*set.data, cp); }

std::size_t StartsWithMember(ClassId set, std::string_view s) {
    return set.data != nullptr ? internal::MemberAtStart(*set.data, s) : 0;
}

std::size_t EndsWithMember(ClassId set, std::string_view s) {
    return set.data != nullptr ? internal::MemberAtEnd(*set.data, s) : 0;
}

}  // namespace hdTrans::dict
