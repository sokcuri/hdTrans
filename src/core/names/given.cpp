#include "core/names/given.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/builtin.h"
#include "core/names/historical.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"

namespace hdTrans::names {

namespace {

constexpr RoCore kCores[] = {
    {"一郎", "イチロウ", "いちろう", "いちろう"}, {"一朗", "", "", "いちろう"},   {"一郎", "イチロー", "", "いちろう"},
    {"二郎", "", "", "じろう"},                   {"次郎", "ジロウ", "", "じろう"}, {"次郎", "ジロー", "", "じろう"},
    {"三郎", "サブロウ", "", "さぶろう"},         {"三郎", "サブロー", "", "さぶろう"},
    {"太郎", "タロウ", "", "たろう"},             {"太郎", "タロー", "", "たろう"},
    {"四郎", "シロウ", "", "しろう"},             {"五郎", "ゴロウ", "", "ごろう"},
    {"六郎", "ロクロウ", "", "ろくろう"},         {"七郎", "シチロウ", "", "しちろう"},
    {"八郎", "ハチロウ", "", "はちろう"},         {"九郎", "", "", "くろう"},
    {"十郎", "ジュウロウ", "", "じゅうろう"},
};

// The rows KANJI <TAB> KANA of the builtin extra table name.
std::map<char32_t, std::string> KanjiTable(std::string_view name) {
    std::map<char32_t, std::string> out;
    const std::string text = dict::ExtraBuiltinText(name);
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string_view line = std::string_view(text).substr(at, end - at);
        at = end + 1;
        const std::size_t tab = line.find('\t');
        if (line.empty() || line.starts_with("#") || tab == std::string_view::npos) continue;
        out[text::DecodeRune(line).cp] = std::string(line.substr(tab + 1));
    }
    return out;
}

std::string_view Lookup(const std::map<char32_t, std::string>& table, char32_t kanji) {
    const auto it = table.find(kanji);
    return it == table.end() ? std::string_view() : std::string_view(it->second);
}

// The Korean of a one-kanji person of the name layer (呉 쿠레), "" for none.
std::string AloneReading(const dict::Snapshot& snap, std::string_view kanji) {
    for (const dict::Record& r : snap.NameRecords(kanji)) {
        if (r.codes != nullptr && r.codes->sem.HasCode("QPN") && text::AllSyllables(r.target)) return std::string(r.target);
    }
    return {};
}

// The Korean of kanji in the given names that join it to a core other than skip, the most
// frequent, from the name layer, else from the base dictionary; "" for none.
std::string LayerReading(const dict::Snapshot& snap, std::string_view kanji, std::string_view skip, bool base) {
    std::map<std::string, int> votes;
    std::vector<std::string> order;
    std::string_view last;
    for (const RoCore& c : kCores) {
        if (c.kanji == skip || c.kanji == last) continue;
        last = c.kanji;
        const std::string tail = text::FromKana(c.kana, text::Preset::FanShort);
        const std::string longTail = text::FromKana(c.kana, text::Preset::Fan);
        const std::string key = std::string(kanji) + std::string(c.kanji);
        for (const dict::Record& r : base ? snap.Base().Records(key) : snap.NameRecords(key)) {
            if (r.codes == nullptr || !r.codes->sem.HasCode("QPN") || !text::AllSyllables(r.target)) {
                continue;
            }
            for (const std::string& t : {tail, longTail}) {
                if (r.target.size() <= t.size() || !r.target.ends_with(t)) continue;
                const std::string head = text::WithoutNameLongVowels(r.target.substr(0, r.target.size() - t.size()), false);
                if (votes[head]++ == 0) order.push_back(head);
                break;
            }
        }
    }
    std::string best;
    for (const std::string& h : order) {
        if (best.empty() || votes[h] > votes[best]) best = h;
    }
    return best;
}

}  // namespace

std::span<const RoCore> RoCores() { return kCores; }

std::string RoGivenName(const dict::Snapshot& snap, std::string_view kanji, const RoCore& core) {
    const std::string tail = text::FromKana(core.kana, text::Preset::FanShort);
    for (const bool base : {false, true}) {
        if (std::string head = LayerReading(snap, kanji, core.kanji, base); !head.empty()) return head + tail;
    }
    if (text::RuneCount(kanji) != 1) return {};
    const char32_t c = text::DecodeRune(kanji).cp;
    const std::string_view head = internal::HeadReading(c);
    const std::string headWritten = head.empty() ? std::string() : text::FromKana(head, text::Preset::FanShort, true);
    if (const std::string alone = AloneReading(snap, kanji); !alone.empty() && alone.starts_with(headWritten)) {
        return alone + tail;
    }
    std::string_view kana = head;
    if (kana.empty()) kana = CommonNamePrefix(kanji);
    if (kana.empty()) kana = internal::OnReading(c);
    if (kana.empty()) return {};
    return text::FromKana(std::string(kana) + std::string(core.kana), text::Preset::FanShort, true);
}

namespace internal {

std::string_view HeadReading(char32_t kanji) {
    static const std::map<char32_t, std::string> table = KanjiTable("name_heads.tsv");
    return Lookup(table, kanji);
}

std::string_view OnReading(char32_t kanji) {
    static const std::map<char32_t, std::string> table = KanjiTable("kanji_on.tsv");
    return Lookup(table, kanji);
}

}  // namespace internal

}  // namespace hdTrans::names
