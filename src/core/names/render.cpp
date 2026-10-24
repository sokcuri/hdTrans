// Rendering of the accepted names as Hangul: the plain-dictionary rendering, the kana
// reading of the document, the readings table, a numbered name as its name and number,
// the head of a longer katakana name, the name layer's person rendering, and the katakana
// transliteration.
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/names/chars.h"
#include "core/names/readings.h"
#include "core/names/registry.h"
#include "core/text/hangul.h"
#include "core/word/kana.h"

namespace hdTrans::names {

using namespace internal;

namespace {

// s with each final ㄱ, ㄹ and ㅂ written as a syllable 크, 르 and 프 after its syllable (알 아르,
// 샬 샤르), the two spellings of a katakana ク, ル and プ after a vowel, but a final ㄹ before
// an initial ㄹ dropped (알렉 아렉, 필리 피리), the Korean spelling of a single ラ-row kana.
std::string OpenFinals(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = RuneAt(s, i);
        i += r.size;
        int open = -1;  // the initial of the syllable of the final
        switch (text::Final(r.cp)) {
            case text::kFinalGiyeok: open = text::kInitialKieuk; break;
            case text::kFinalRieul: open = text::kInitialRieul; break;
            case text::kFinalBieup: open = text::kInitialPieup; break;
        }
        if (open < 0) {
            text::AppendRune(out, r.cp);
            continue;
        }
        text::AppendRune(out, text::SetFinal(r.cp, text::kFinalNone));
        const auto next = i < s.size() ? text::Split(RuneAt(s, i).cp) : std::nullopt;
        if (open != text::kInitialRieul || !next || next->initial != text::kInitialRieul) {
            text::AppendRune(out, text::Join(open, text::kVowelEu, text::kFinalNone));
        }
    }
    return out;
}

// The leading syllables of kr, short of all of kr, that read x: they spell its
// transliteration, the last maybe without a final x lacks (유필리아 유피, 레온하르트 레오),
// with finals taken as OpenFinals writes them (아르가르드 아르, 알렉산드르 알렉); else they sound
// as it (text::SameSound), as many syllables before fewer or more. "" when none do.
std::string AlignedHead(const std::string& kr, std::string_view x) {
    std::string tr;
    if (!word::TransliterateName(x, tr) || !IsHangulText(tr)) return "";
    tr = OpenFinals(tr);
    const std::size_t n = RuneCount(tr);
    std::string head, best;
    int rank = 3;
    for (std::size_t i = 0; i < kr.size();) {
        const text::Rune r = RuneAt(kr, i);
        i += r.size;
        if (i >= kr.size() || !text::Split(r.cp)) break;
        std::string open = head;
        head += kr.substr(i - r.size, r.size);
        text::AppendRune(open, text::SetFinal(r.cp, text::kFinalNone));
        for (const std::string* h : {&head, &open}) {
            const std::string key = OpenFinals(*h);
            const int ra = key == tr ? 0 : !text::SameSound(key, tr) ? 3 : RuneCount(key) == n ? 1 : 2;
            if (ra < rank) {
                rank = ra;
                best = *h;
            }
        }
    }
    return best;
}

// The syllables of kr, the rendering of the katakana word x+y, that render x: the leading
// syllables that sound as x (AlignedHead), else kr less the transliteration of y when kr
// ends with it, else as many syllables as x has morae.
std::string SyllableHead(const std::string& kr, std::string_view x, std::string_view y) {
    if (std::string h = AlignedHead(kr, x); !h.empty()) return h;
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
    if (const auto x = numbered_.find(key); x != numbered_.end()) {
        const std::string head = RenderPart(x->second, true);
        const std::string number = text::FromKana(std::string_view(key).substr(x->second.size()), preset);
        if (!head.empty() && IsHangulText(number)) return head + number;
    }
    if (const std::string kr = ChainPart(t); !kr.empty()) return kr;
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
    // A common kanji word that honorifics have shown to be a person reads as the person of
    // the name layer (葉月さん 하즈키), not as the first row of the word.
    const bool called = acc != Accepted().end() && hon_.count(key) > 0 && AllRunes(t, IsKanji) && Flags(t).common &&
                        LayerReading(t);
    if (!layer.empty() && !katakana && ((!Flags(t).common && RuneCount(t) > 1) || given || called)) return layer;
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

std::string Registry::ChainPart(std::string_view x) {
    for (const auto& [t, n] : Accepted()) {
        if (!n.hasParts || n.parts.size() < 2) continue;
        if (t.find("・") == std::string::npos && t.find("＝") == std::string::npos) continue;
        const auto at = std::find(n.parts.begin(), n.parts.end(), x);
        if (at == n.parts.end()) continue;
        const std::string layer = Flags(t).layerName;
        std::vector<std::string> words;
        for (std::size_t i = 0; i <= layer.size();) {
            const std::size_t sp = std::min(layer.find(' ', i), layer.size());
            if (sp > i) words.push_back(layer.substr(i, sp - i));
            i = sp + 1;
        }
        if (words.size() == n.parts.size()) return words[static_cast<std::size_t>(at - n.parts.begin())];
    }
    return "";
}

std::string Registry::PrefixForm(std::string_view x) {
    if (!AllRunes(x, IsKatakana)) return "";
    for (const std::string& t : Names()) {
        // A numbered name reads its head the way the head reads (RenderPart).
        if (!t.starts_with(x) || t.size() == x.size() || !AllRunes(t, IsKatakana) || numbered_.count(t) > 0) continue;
        const std::string_view y = std::string_view(t).substr(x.size());
        const std::string h = SyllableHead(RenderPart(t), x, y);
        if (!h.empty()) return h;
    }
    return "";
}

}  // namespace hdTrans::names
