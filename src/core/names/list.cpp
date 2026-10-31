#include "core/names/list.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/names/chars.h"

namespace hdTrans::names {

namespace {

constexpr std::array<std::pair<ListKind, std::string_view>, 7> kKinds = {{
    {ListKind::Given, "given"},
    {ListKind::Family, "family"},
    {ListKind::Full, "full"},
    {ListKind::Nick, "nick"},
    {ListKind::Place, "place"},
    {ListKind::Org, "org"},
    {ListKind::Title, "title"},
}};

std::vector<std::string_view> Fields(std::string_view line) {
    std::vector<std::string_view> out;
    for (;;) {
        const std::size_t tab = line.find('\t');
        out.push_back(internal::TrimSpace(line.substr(0, tab)));
        if (tab == std::string_view::npos) return out;
        line.remove_prefix(tab + 1);
    }
}

}  // namespace

std::string_view KindName(ListKind k) {
    for (const auto& [kind, name] : kKinds) {
        if (kind == k) return name;
    }
    return "given";
}

bool ParseList(std::string_view text, List& out, std::vector<std::string>* errors) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    bool ok = true;
    auto fail = [&](std::size_t number, std::string_view message) {
        ok = false;
        if (errors != nullptr) errors->push_back(std::to_string(number) + ": " + std::string(message));
    };
    for (std::size_t number = 1; !text.empty(); number++) {
        const std::size_t nl = text.find('\n');
        const std::string_view raw = internal::TrimCR(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        const std::string_view line = internal::TrimSpace(raw);
        if (line.empty() || line.starts_with('#')) continue;
        const std::vector<std::string_view> f = Fields(raw);
        if (f.size() < 2 || f[0].empty() || f[1].empty()) {
            fail(number, "a name needs JA and KO");
            continue;
        }
        if (f.size() > 4) {
            fail(number, "more than four fields");
            continue;
        }
        Listed name{std::string(f[0]), std::string(f[1])};
        if (f.size() > 2 && !f[2].empty()) {
            const auto kind = std::find_if(kKinds.begin(), kKinds.end(), [&](const auto& k) { return k.second == f[2]; });
            if (kind == kKinds.end()) {
                fail(number, "unknown kind " + std::string(f[2]));
                continue;
            }
            name.kind = kind->first;
        }
        bool good = true;
        for (std::string_view opts = f.size() > 3 ? f[3] : std::string_view(); !opts.empty();) {
            const std::size_t sp = opts.find(' ');
            const std::string_view opt = opts.substr(0, sp);
            opts = sp == std::string_view::npos ? std::string_view() : opts.substr(sp + 1);
            if (opt.empty()) continue;
            if (opt == "hon") {
                name.honorificOnly = true;
            } else {
                fail(number, "unknown option " + std::string(opt));
                good = false;
            }
        }
        if (!good) continue;
        std::erase_if(out.names, [&](const Listed& n) { return n.ja == name.ja; });
        out.names.push_back(std::move(name));
    }
    return ok;
}

std::string ListLine(const Listed& name) {
    std::string out = name.ja + "\t" + name.ko + "\t" + std::string(KindName(name.kind));
    if (name.honorificOnly) out += "\thon";
    return out;
}

}  // namespace hdTrans::names
