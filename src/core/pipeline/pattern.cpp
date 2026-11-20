#include "core/pipeline/pattern.h"

#include <array>
#include <cstdint>
#include <optional>

#include "core/pipeline/runes.h"

namespace hdTrans::pipeline::internal {

namespace {

// The text of the Find that runs; id 0 outside a Find. A Find inside a pattern takes its own id and
// gives the outer one back when it ends, unless a ScanOf holds one for the same text.
struct Scan {
    const char* data = nullptr;
    size_t size = 0;
    uint64_t id = 0;
    bool pinned = false;  // a ScanOf: the Finds over the same text keep it
};
thread_local Scan gScan;
thread_local uint64_t gScans = 0;

class ScanScope {
  public:
    explicit ScanScope(std::string_view s) : outer_(gScan) { gScan = {s.data(), s.size(), ++gScans}; }
    ~ScanScope() { gScan = outer_; }
    ScanScope(const ScanScope&) = delete;
    ScanScope& operator=(const ScanScope&) = delete;

  private:
    Scan outer_;
};

// The run found last for a class during one Find: [start, end) is the whole run that ends at end.
struct RunMemo {
    bool (*in)(char32_t) = nullptr;
    uint64_t scan = 0;
    size_t end = 0;
    size_t start = 0;
};
thread_local std::array<RunMemo, 2> gRunMemos;

}  // namespace

size_t RunStartBefore(std::string_view s, size_t pos, bool (*in)(char32_t)) {
    // The memo holds for s when s starts the text of the Find that made it: both see the same bytes before end.
    RunMemo* memo = nullptr;
    if (gScan.id != 0 && s.data() == gScan.data && s.size() <= gScan.size) {
        memo = gRunMemos[0].in == in ? &gRunMemos[0] : &gRunMemos[1];
        if (memo->in != in) memo = gRunMemos[0].scan != gScan.id ? &gRunMemos[0] : &gRunMemos[1];
    }
    const bool known = memo != nullptr && memo->in == in && memo->scan == gScan.id && memo->end <= pos;
    size_t start = pos;
    while (start > 0) {
        if (known && start == memo->end) {
            start = memo->start;
            break;
        }
        const text::Rune r = LastRune(s.substr(0, start));
        if (!in(r.cp)) break;
        start -= r.size;
    }
    if (memo != nullptr) *memo = {in, gScan.id, pos, start};
    return start;
}

ScanOf::ScanOf(std::string_view s) : data_(gScan.data), size_(gScan.size), id_(gScan.id), pinned_(gScan.pinned) {
    gScan = {s.data(), s.size(), ++gScans, true};
}

ScanOf::~ScanOf() { gScan = {data_, size_, id_, pinned_}; }

bool Find(std::string_view s, size_t from, Pattern p, Match& m) {
    std::optional<ScanScope> scope;
    if (!gScan.pinned || gScan.data != s.data() || gScan.size != s.size()) scope.emplace(s);
    for (size_t pos = from; pos <= s.size();) {
        m = Match{};
        if (p(s, pos, m)) {
            m.begin = pos;
            m.group[0] = Span(s, pos, m.end);
            return true;
        }
        if (pos == s.size()) break;
        pos += At(s, pos).size;
    }
    return false;
}

Match Rematch(const Match& m, Pattern p) {
    Match g;
    if (!Find(m.group[0], 0, p, g)) g = Match{};
    return g;
}

std::string Cat(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (std::string_view p : parts) out.append(p);
    return out;
}

}  // namespace hdTrans::pipeline::internal
