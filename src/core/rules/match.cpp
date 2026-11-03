#include "core/rules/match.h"

#include <algorithm>

namespace hdTrans::rules {

namespace {

// Up to n bytes of s from i.
std::string_view Chunk(std::string_view s, std::size_t i, std::size_t n) { return s.substr(i, std::min(n, s.size() - i)); }

bool Contains(std::string_view s, std::string_view sub) { return s.find(sub) != std::string_view::npos; }

// Whether a pattern chunk (up to 3 bytes) selects the family: each byte is '*', 'X'
// with allowX, or the family name's byte.
bool FamilyMatches(int family, std::string_view c, bool allowX) {
    const char* name = internal::AttributeFamilies()[static_cast<std::size_t>(family)].name;
    for (std::size_t i = 0; i < c.size(); i++) {
        if (c[i] != '*' && !(allowX && c[i] == 'X') && c[i] != name[i]) return false;
    }
    return true;
}

bool MinValues(std::string_view values, std::string_view pattern, bool base) {
    const int families = static_cast<int>(internal::AttributeFamilies().size());
    for (std::size_t off = 0; off < pattern.size(); off += 3) {
        const std::string_view c = Chunk(pattern, off, 3);
        if (base && c == "XXX") continue;
        for (int f = 0; f < families; f++) {
            if (!FamilyMatches(f, c, base)) continue;
            for (std::size_t n = 0; n < values.size(); n += 3) {
                const std::string_view v = Chunk(values, n, 3);
                if (v != "XXX" && MatchAttributeFamily(f, v, c)) return true;
            }
        }
    }
    return false;
}

}  // namespace

bool MatchRuleSurface(std::string_view surface, std::string_view pattern) {
    if (pattern.starts_with("L")) return surface.starts_with(pattern.substr(1));
    if (pattern.starts_with("R")) return surface.ends_with(pattern.substr(1));
    if (pattern.starts_with("11")) return surface.starts_with(pattern.substr(2));
    if (pattern.starts_with("22")) return surface.ends_with(pattern.substr(2));
    if (pattern.starts_with("00")) return Contains(pattern.substr(2), surface);
    if (pattern.starts_with("99")) return surface == pattern.substr(2);
    return surface == pattern;
}

bool MatchRuleCode(std::string_view code, std::string_view pattern) {
    for (std::size_t off = 0; off < pattern.size(); off += 3) {
        const std::string_view alt = Chunk(pattern, off, 3);
        if (code.size() < alt.size()) continue;
        std::size_t i = 0;
        while (i < alt.size() && (alt[i] == '*' || alt[i] == code[i])) i++;
        if (i == alt.size()) return true;
    }
    return false;
}

bool HasCode(std::string_view name, std::string_view values) {
    for (std::size_t row = 0; row + 3 <= values.size(); row += 3) {
        std::size_t i = 0;
        while (i < 3 && i < name.size() && (name[i] == '*' || name[i] == values[row + i])) i++;
        if (i == 3) return true;
    }
    return false;
}

bool MatchAttributeFamily(int family, std::string_view values, std::string_view pattern) {
    const auto families = internal::AttributeFamilies();
    const internal::AttributeFamily& f = families[static_cast<std::size_t>(family)];
    char name[3] = {f.name[0], f.name[1], f.name[2]};
    for (std::size_t i = 0; i < pattern.size() && i < 3; i++) {
        if (pattern[i] == '*') name[i] = '*';
    }
    if (HasCode(std::string_view(name, 3), values)) return true;
    for (int i = 0; i < 5; i++) {
        const int p = f.parent[i];
        if (p == -1) break;
        if (MatchAttributeFamily(p, values, pattern)) return true;
    }
    for (const int e : f.expansion) {
        if (e == -1) break;
        if (HasCode(families[static_cast<std::size_t>(e)].name, values)) return true;
    }
    return false;
}

bool AttrChunksIn(std::string_view values, std::string_view pattern) {
    for (std::size_t off = 0; off < pattern.size(); off += 3) {
        if (Contains(values, Chunk(pattern, off, 3))) return true;
    }
    return false;
}

bool GobiAttrValues(std::string_view values, std::string_view pattern) {
    const int families = static_cast<int>(internal::AttributeFamilies().size());
    for (std::size_t off = 0; off < pattern.size(); off += 3) {
        const std::string_view c = Chunk(pattern, off, 3);
        for (int f = 0; f < families; f++) {
            if (FamilyMatches(f, c, false) && MatchAttributeFamily(f, values, pattern)) return true;
        }
    }
    return false;
}

bool MinAttrValues(std::string_view values, std::string_view pattern) { return MinValues(values, pattern, true); }

bool MinGobiAttrValues(std::string_view values, std::string_view pattern) { return MinValues(values, pattern, false); }

bool MinGAttrValues(std::string_view values, std::string_view pattern) {
    for (std::size_t off = 0; off < values.size(); off += 2) {
        if (Contains(pattern, Chunk(values, off, 2))) return true;
    }
    return false;
}

}  // namespace hdTrans::rules
