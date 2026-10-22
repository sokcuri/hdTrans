// Rendering of the accepted names as Hangul: the plain-dictionary rendering, the kana
// reading of the document, the readings table, the head of a longer katakana name, the
// name layer's person rendering, and the katakana transliteration.
#include <string>
#include <string_view>
#include <vector>

#include "core/names/chars.h"
#include "core/names/readings.h"
#include "core/names/registry.h"
#include "core/word/kana.h"

namespace hdTrans::names {

using namespace internal;

namespace {

// The syllables of kr, the rendering of the katakana word x+y, that render x: kr less the
// transliteration of y when kr ends with it, else as many syllables as x has morae.
std::string SyllableHead(const std::string& kr, std::string_view x, std::string_view y) {
    std::string tail;
    if (word::TransliterateName(y, tail) && !tail.empty() && kr.size() > tail.size() && kr.ends_with(tail)) {
        return kr.substr(0, kr.size() - tail.size());
    }
    const std::size_t n = RuneCount(text::FromKana(x, text::Preset::Fan));
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < kr.size(); i += RuneAt(kr, i).size) starts.push_back(i);
    if (n > 0 && n < starts.size()) return kr.substr(0, starts[n]);
    return "";
}

}  // namespace

std::string Registry::Dict() {
    std::string b;
    for (const std::string& t : Names()) {
        const Name n = Accepted().at(t);
        const std::string kr = RenderName(t, n);
        if (kr.empty()) continue;
        const DictFlags f = Flags(t);
        std::string pos = "n";
        // A place of the dictionaries that only a gloss names (五村（ごのむら）) stays a place.
        const bool place = f.city && !f.person && !n.label && hon_.count(t) == 0 && kanaHon_.count(t) == 0;
        if (!n.hasParts && (n.surname || (f.surname && !f.given))) {
            pos = "surname";
        } else if (place) {
            pos = "place";
        } else if (n.person) {
            pos = "name";
        }
        b += t + "\t" + kr + "\t" + pos + "\n";
    }
    if (!chan.empty()) b += "ちゃん\t" + chan + "\thon\n";
    return b;
}

std::string Registry::Render(std::string_view t) {
    const auto& acc = Accepted();
    const auto it = acc.find(std::string(t));
    if (it == acc.end()) return "";
    const Name n = it->second;
    return RenderName(t, n);
}

std::string Registry::RenderName(std::string_view t, const Name& n) {
    if (!n.hasParts) return RenderPart(t, true);
    std::string out;
    for (std::size_t i = 0; i < n.parts.size(); i++) {
        const std::string part = RenderPart(n.parts[i]);
        if (part.empty()) return "";
        if (i > 0) out += " ";
        out += part;
    }
    return out;
}

std::string Registry::RenderPart(std::string_view t, bool whole) {
    const std::string key(t);
    if (const auto f = tables_->fixed.find(key); f != tables_->fixed.end() && !f->second.empty()) return f->second;
    if (const auto r = reading_.find(key); r != reading_.end() && !r->second.empty()) {
        return text::FromKana(r->second, preset);
    }
    if (const auto v = tables_->readings.find(key); v != tables_->readings.end() && !v->second.empty()) {
        if (IsKanaText(v->second)) return text::FromKana(v->second, preset);
        return v->second;
    }
    if (const std::string kr = PrefixForm(t); !kr.empty()) return kr;
    const std::string layer = Flags(t).layerName;
    if (snap_ != nullptr) {
        for (const dict::Candidate& rec : snap_->RetrieveRecords(t, dict::LookupMode::General)) {
            if (rec.codes == nullptr) continue;
            const std::string_view sem = rec.codes->sem.Text(), attr = rec.codes->gram.Text();
            if (sem.starts_with("QPN") || attr.starts_with("Wa") || attr.starts_with("Wb") || rec.target == layer) {
                if (IsHangulText(rec.target)) return std::string(rec.target);
            }
        }
    }
    const bool katakana = AllRunes(t, IsKatakana);
    const auto acc = Accepted().find(key);
    const bool given = acc != Accepted().end() && acc->second.given;
    if (!layer.empty() && !katakana && ((!Flags(t).common && RuneCount(t) > 1) || given)) return layer;
    if (std::string kr = whole && katakana && snap_ != nullptr ? snap_->NameReading(t) : PersonName(t);
        IsHangulText(kr)) {
        return kr;
    }
    if (katakana) {
        std::string tr;
        if (word::TransliterateName(t, tr) && IsHangulText(tr)) return tr;
    }
    if (AllRunes(t, IsHiragana)) {
        if (std::string kr = text::FromKana(t, preset); IsHangulText(kr)) return kr;
    }
    return "";
}

std::string Registry::PersonName(std::string_view t) {
    if (snap_ == nullptr || !AllRunes(t, IsKatakana)) return "";
    const std::string kr = snap_->PersonName(t);
    return IsHangulText(kr) ? kr : "";
}

std::string Registry::PrefixForm(std::string_view x) {
    if (!AllRunes(x, IsKatakana)) return "";
    for (const std::string& t : Names()) {
        if (!t.starts_with(x) || t.size() == x.size() || !AllRunes(t, IsKatakana)) continue;
        const std::string_view y = std::string_view(t).substr(x.size());
        const std::string h = SyllableHead(RenderPart(t), x, y);
        if (!h.empty()) return h;
    }
    return "";
}

}  // namespace hdTrans::names
