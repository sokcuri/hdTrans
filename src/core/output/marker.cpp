// The composer's marker rules: an item's Korean text from its entry, its ending and its
// neighbours.
#include <cstdint>
#include "core/output/compose.h"
#include "core/output/inflect.h"
#include "core/output/particle.h"
#include "core/output/text.h"
#include "core/text/hangul.h"
#include "core/text/utf8.h"
#include "core/tokenizer/charsets.h"

namespace hdTrans::output {

namespace {

using internal::At;
using internal::Contains;
using internal::CutFirst;
using internal::CutLast;
using internal::First;
using internal::FirstByte;
using internal::Selected;
using internal::SpokenInfinitive;
using internal::WidthOf;

// Japanese sources compared with item surfaces.
constexpr std::string_view kJaNa = "な";
constexpr std::string_view kJaIna = "いな";
constexpr std::string_view kJaSuru = "する";
constexpr std::string_view kJaKu = "く";
constexpr std::string_view kJaKi = "き";
constexpr std::string_view kJaSugi = "すぎ";
constexpr std::string_view kJaSugiKanji = "過ぎ";
constexpr std::string_view kJaTsudzuke = "つづけ";
constexpr std::string_view kJaTsudzukeKanji = "続け";
constexpr std::string_view kJaNao = "なお";
constexpr std::string_view kJaNaoKanji = "直";
constexpr std::string_view kJaA = "あ";
constexpr std::string_view kJaAiKanji = "合";
constexpr std::string_view kJaKawa = "かわ";
constexpr std::string_view kJaKawaKanji = "交わ";
constexpr std::string_view kJaMaku = "まく";
constexpr std::string_view kJaAsa = "あさ";
constexpr std::string_view kJaAsaKanji = "漁";
constexpr std::string_view kJaChira = "ちら";
constexpr std::string_view kJaChiraKanji = "散ら";
constexpr std::string_view kJaTsuku = "つく";
constexpr std::string_view kJaTsukuKanji = "尽く";
constexpr std::string_view kJaOwaKanji = "終わ";
constexpr std::string_view kJaKire = "きれ";
constexpr std::string_view kJaKireKanji = "切れ";
constexpr std::string_view kJaItada = "いただ";
constexpr std::string_view kJaItadaKanji = "頂";
constexpr std::string_view kJaNaide = "ないで";
constexpr std::string_view kJaComma = "、";

// Korean text the composer writes.
constexpr std::string_view kKoNeomu = "너무";
constexpr std::string_view kKoNeun = "는";
constexpr std::string_view kKoJuseyo = "주세요";
constexpr std::string_view kKoIlDa = "1다";
constexpr std::string_view kKoSuHnMarker = "수 &HN@";

// Texts of items whose ending candidate is missing.
constexpr std::string_view kNoDirectEntry = "<?}";
constexpr std::string_view kInversionFailed = "<???:Word도치 실패>";

std::string Cat(std::initializer_list<std::string_view> parts) {
    size_t n = 0;
    for (const auto p : parts) n += p.size();
    std::string out;
    out.reserve(n);
    for (const auto p : parts) out += p;
    return out;
}

std::string ReplaceAll(std::string_view s, std::string_view from, std::string_view to) {
    std::string out;
    for (size_t i = 0;;) {
        const size_t j = s.find(from, i);
        if (j == std::string_view::npos) {
            out += s.substr(i);
            return out;
        }
        out += s.substr(i, j - i);
        out += to;
        i = j + from.size();
    }
}

std::string_view EntryText(const ResolvedItem& it) { return Selected(it).text; }
std::string_view Code(const ResolvedItem& it) { return Selected(it).code; }
std::string_view GAttr(const ResolvedItem& it) { return Selected(it).gattr; }

std::string_view DirectText(const ResolvedItem& it) { return it.directIndex < 0 ? std::string_view() : it.ending.text; }
std::string_view DirectCode(const ResolvedItem& it) { return it.directIndex < 0 ? std::string_view() : it.ending.code; }
std::string_view DirectGAttr(const ResolvedItem& it) { return it.directIndex < 0 ? std::string_view() : it.ending.gattr; }

bool KeiyoX(const ResolvedItem& it) { return it.detailClass == DetailClass::Keiyo && it.detailKind == 'x'; }
bool DakeiyoX(const ResolvedItem& it) { return it.detailClass == DetailClass::Dakeiyo && it.detailKind == 'X'; }

void SetSelectedIndex(ResolvedItem& it, int i) {
    if (it.selected == i) return;
    it.selected = i;
    it.entry = i == 0 ? it.first : ResolvedEntry{};
}

void SetGAttr(ResolvedItem& it, int i, std::string_view v) {
    if (i >= 0 && i == it.selected) it.entry.gattr = v;
    if (i == 0) it.first.gattr = v;
}

void AssignEntry(ResolvedItem& it, int i, std::string_view s) {
    if (i >= 0 && i == it.selected) it.entry.text = s;
    if (i == 0) it.first.text = s;
}

// Replaces the target of entry di (the selected one for -1, every entry when none is
// selected) unless the item is fixed.
void AssignDst(ResolvedItem& it, int di, std::string_view s) {
    if (it.fixed) return;
    if (di == -1 && it.selected == -1) {
        it.first.text = s;
        it.otherHangul = it.otherHangul && internal::HasSyllable(s);
        return;
    }
    AssignEntry(it, di == -1 ? it.selected : di, s);
}

// Replaces the text of the selected ending, unless the ending group comes from a plain
// dictionary.
void AssignGobiDst(ResolvedItem& it, std::string_view v) {
    if (it.detailClass == DetailClass::None || it.ownedEnding || it.directIndex < 0) return;
    it.ending.text = v;
}

// The ending class of a direct text after 過ぎ/すぎ.
std::string RemapEndingClass(std::string_view suffix) {
    char c;
    switch (First(suffix)) {
        case '0': case '8': c = '4'; break;
        case '4': c = '1'; break;
        case '9': c = '7'; break;
        case 'A': case 'B': c = '8'; break;
        default: return std::string(suffix);
    }
    std::string out(1, c);
    out += suffix.substr(1);
    return out;
}

// The quoting endings 다고, 다는, 다면, 다니, 다며 and 다더 at the start of s, without
// their 다, and the text after them.
bool CutQuoting(std::string_view s, std::string_view& quoted, std::string_view& rest) {
    for (const std::string_view q : {"다고", "다는", "다면", "다니", "다며", "다더"}) {
        if (s.starts_with(q)) {
            quoted = q.substr(std::string_view("다").size());
            rest = s.substr(q.size());
            return true;
        }
    }
    return false;
}

// Forms of 않다 and the forms of 아니다 that replace them after a copula, longest first.
constexpr std::pair<std::string_view, std::string_view> kNotForms[] = {
    {"않아요", "아니에요"}, {"않았", "아니었"}, {"않는다", "아니다"}, {"않는", "아닌"},
    {"않습", "아닙"},       {"않을", "아닐"},   {"않은", "아닌"},     {"않으", "아니"},
    {"않음", "아님"},       {"않아", "아니어"}, {"않", "아니"},
};

// A text that starts with a form of 않다 with 아니다; 않아 ending the predicate is 아니야,
// and 다 before the quoting endings is 라 (아니라고).
std::string NotForm(std::string_view s) {
    if (s == "않아") return "아니야";
    for (const auto& [from, to] : kNotForms) {
        if (!s.starts_with(from)) continue;
        std::string out = Cat({to, s.substr(from.size())});
        std::string_view tail = out;
        if (tail.starts_with("아니")) tail.remove_prefix(std::string_view("아니").size());
        std::string_view quoted, rest;
        if (CutQuoting(tail, quoted, rest) && std::string_view(out).starts_with("아니다")) {
            return Cat({"아니라", quoted, rest});
        }
        return out;
    }
    return std::string(s);
}

// Forms 1 and 2 keep a stem's final ㄹ (길면, 길지); before ㄴ, ㅂ, ㅅ and 오 it drops (기니까, 기네).
std::string DropRieulBefore(std::string form, std::string_view suffix) {
    constexpr int kRieulFinal = 8, kNieunInitial = 2, kBieupInitial = 7, kSiotInitial = 9;
    if (form.size() < 3 || suffix.empty()) return form;
    const text::Rune last = text::DecodeRune(std::string_view(form).substr(form.size() - 3));
    const auto stem = text::Split(last.cp);
    const auto next = text::Split(text::DecodeRune(suffix).cp);
    if (last.size != 3 || !stem || !next || stem->final != kRieulFinal) return form;
    if (next->initial != kNieunInitial && next->initial != kBieupInitial && next->initial != kSiotInitial &&
        text::DecodeRune(suffix).cp != U'오') {
        return form;
    }
    form.resize(form.size() - 3);
    text::AppendRune(form, text::SetFinal(last.cp, 0));
    return form;
}

// 구나 and 군 follow a verb's 는 form (가는구나, 모르는구나) but an adjective's stem (다르구나),
// whichever part of speech the Japanese word has. key is a family marker and a form code:
// '&' families use the verb codes ('0' 는, '1' stem), '$' and '%' the adjective codes ('4'
// 는, '1' stem). 있다, 없다 and the copula keep the form their row gives.
void ExclamationForm(std::string& key, std::string_view suffix) {
    if (key.size() != 5 || !(suffix.starts_with("구나") || suffix.starts_with("군"))) return;
    const auto* family = internal::FindKoreanFamily(std::string_view(key).substr(0, 4));
    if (!family || family->inflectionClass == internal::InflectionClass::Existential ||
        family->inflectionClass == internal::InflectionClass::Copula || (family->flags & internal::kBareDeclarative)) {
        return;
    }
    const char neun = key[0] == '&' ? '0' : '4';
    if (key[4] != neun && key[4] != '1') return;
    key[4] = family->partOfSpeech == internal::KoreanPartOfSpeech::Verb ? neun : '1';
}

// The text of the first item after item i that has one (a dropped ぉ has none); "." after the
// last item.
std::string_view TextAfter(const std::vector<ResolvedItem>& items, int i) {
    for (std::size_t k = static_cast<std::size_t>(i) + 1; k < items.size(); k++) {
        if (const std::string_view t = EntryText(items[k]); !t.empty()) return t;
    }
    return ".";
}

// Whether text after a predicate starts with the punctuation that ends a sentence.
bool SentenceEndAfter(std::string_view after) {
    for (const std::string_view end : {"!", "?", ".", "…", "」", "』", "~", "～"}) {
        if (after.starts_with(end)) return true;
    }
    return false;
}

// Whether the form of key with suffix ends a clause in speech: the 아/어 form alone or with
// 요, or the past stem with 어 or 어요 (갔어), before the punctuation of SentenceEndAfter, or
// before a comma after a Japanese ending in a terminal form (いい、 됐어,), which a
// connective form (なり、 되어,) is not; or anywhere when ended (a casual sentence end).
bool SpokenEnd(std::string_view key, std::string_view suffix, std::string_view after, std::string_view ending,
               bool ended) {
    const bool infinitive = key.ends_with("6") && (suffix.empty() || suffix == "요");
    const bool past = key.ends_with("5") && (suffix == "어" || suffix == "어요");
    if (!infinitive && !past) return false;
    if (ended) return true;
    if (SentenceEndAfter(after)) return true;
    if (!after.starts_with(",")) return false;
    for (const std::string_view t : {"う", "く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る", "い", "た", "だ"}) {
        if (ending.ends_with(t)) return true;
    }
    return false;
}

}  // namespace

bool HighLeadPair(std::string_view s) {
    const auto r = text::DecodeRune(s);
    return r.size == s.size() && tokenizer::IsSjisHighLead(r.cp);
}

bool CopulaTarget(std::string_view target, const Families& generated) {
    if (WidthOf(target) < 4) return false;
    const auto [stem, id] = CutLast(target, 4);
    std::string key(id);
    key += '6';
    return LookupByMarker(key, generated) == "(이어)여" && !stem.ends_with("보");
}

namespace internal {

const ResolvedEntry& EntryAt(const ResolvedItem& it, int i) {
    static const ResolvedEntry kEmpty;
    if (i < 0) return kEmpty;
    if (i == it.selected) return it.entry;
    if (i == 0) return it.first;
    return kEmpty;
}


std::string SpokenInfinitive(std::string_view form) {
    std::u32string r;
    for (std::string_view s = form; !s.empty();) {
        const text::Rune rune = text::DecodeRune(s);
        if (rune.size == 0) return std::string(form);
        r += rune.cp;
        s.remove_prefix(rune.size);
    }
    if (r.size() < 2) return std::string(form);
    const auto stem = text::Split(r[r.size() - 2]);
    if (!stem || stem->final != 0) return std::string(form);
    const bool past = r.back() == U'었' || r.back() == U'았';
    const char32_t open = past ? (r.back() == U'었' ? U'어' : U'아') : r.back();
    int vowel = -1;
    if (open == U'어') {
        if (stem->vowel == 13) vowel = 14;                           // ㅜ ㅝ
        if (stem->vowel == 11) vowel = 10;                           // ㅚ ㅙ
    } else if (open == U'아' && stem->vowel == 8 && (stem->initial == 1 || stem->initial == 7 || stem->initial == 10)) {
        vowel = 9;  // 꼬 보 쏘: ㅗ ㅘ; 모아 (모으다) keeps its form
    }
    if (vowel < 0) return std::string(form);
    r.pop_back();
    r.back() = text::Join(stem->initial, vowel, past ? 20 : 0);  // 20: ㅆ
    std::string out;
    for (const char32_t c : r) text::AppendRune(out, c);
    return out;
}

}  // namespace internal

std::string_view Composer::Lookup(char table, std::string_view key) const {
    switch (table) {
        case '$': return LookupDollar(key, *generated_);
        case '%': return LookupPercent(key, *generated_);
    }
    return LookupSource(key, *generated_);
}

std::string Composer::Build(int i) { return Build(i, 0, {}); }

std::string Composer::Build(int i, int action, std::string_view mode) {
    std::vector<ResolvedItem>& items = *items_;
    Scratch& sc = state_[i];
    if (action != 0) {
        BuildAction(i, action, mode);
        return sc.text;
    }
    if (!sc.text.empty()) {
        if (sc.text == " ") sc.text.clear();
        return sc.text;
    }
    ResolvedItem& it = items[i];
    const int linked = i - 1;
    const int next = i + 1 < static_cast<int>(items.size()) ? i + 1 : -1;
    const char firstClass = First(internal::EntryAt(it, 0).code);
    if (firstClass != '4') {
        if (firstClass == '7' || firstClass == '8') {
            BuildAdjective(i);
            return sc.text;
        }
        sc.text = EntryText(it);
        if (firstClass == '1' && HighLeadPair(it.surface) && linked >= 0) {
            Scratch& ls = state_[linked];
            Build(linked, 0, {});
            if (!ls.saved.empty()) {
                sc.saved = sc.text;
                sc.text = it.surface;
            }
            ls.text.clear();
            ls.saved.clear();
        }
        return sc.text;
    }

    if (it.selected == -1) SetSelectedIndex(it, 0);
    if (it.directIndex == -1) {
        sc.text = kNoDirectEntry;
        return sc.text;
    }
    if (Contains(GAttr(it), "Da") && linked >= 0) {
        const ResolvedItem& l = items[linked];
        if (Contains(GAttr(l), "Va") && DirectCode(l).starts_with("57")) return sc.text;
        if (Contains(GAttr(l), "St") && DirectCode(l).starts_with("57")) return sc.text;
    }
    std::string stem(EntryText(it));
    std::string suffix(DirectText(it));
    std::string key, replacement;
    if (stem.empty()) return sc.text;
    const std::string_view surface = it.surface;
    if (linked >= 0 && (surface == kJaSugiKanji || surface == kJaSugi)) {
        const ResolvedItem& l = items[linked];
        switch (First(internal::EntryAt(l, 0).code)) {
            case '7':
                if (KeiyoX(l)) {
                    const auto [head, marker] = CutLast(EntryText(l), 4);
                    std::string r(head);
                    const std::string remapped = RemapEndingClass(DirectText(it));
                    const auto [cls, rest] = FirstByte(remapped);
                    if (!r.empty() && Contains(GAttr(l), "Av") && remapped == kKoIlDa) r = NormalizeDakeiyoText(linked, r);
                    r += LookupDollar(Cat({marker, cls}), *generated_);
                    if (WidthOf(suffix) > 1) r += rest;
                    sc.text = std::move(r);
                    return sc.text;
                }
                break;
            case '8':
                if (DakeiyoX(l)) {
                    const auto [head, marker] = CutLast(EntryText(l), 4);
                    std::string r(head);
                    const std::string remapped = RemapEndingClass(DirectText(it));
                    const auto [cls, rest] = FirstByte(remapped);
                    r += LookupPercent(Cat({marker, cls}), *generated_);
                    if (!r.empty() && Contains(GAttr(l), "Av") && remapped == kKoIlDa) r = NormalizeDakeiyoText(linked, r);
                    if (WidthOf(suffix) > 1) r += rest;
                    sc.text = std::move(r);
                    return sc.text;
                }
                break;
        }
    }
    if (linked >= 0 && static_cast<uint8_t>(items[linked].detailKind) == 10 && items[linked].endingLength == 0) {
        int request = 0;
        if (surface == kJaTsudzuke || surface == kJaTsudzukeKanji) {
            request = 1;
        } else if (surface == kJaSugiKanji || surface == kJaSugi) {
            request = 2;
        } else if (surface == kJaNaoKanji || surface == kJaNao) {
            request = 3;
        } else if (surface == kJaA || surface == kJaAiKanji || surface == kJaKawaKanji || surface == kJaKawa) {
            request = 4;
        } else if (surface == kJaMaku || surface == kJaAsa || surface == kJaAsaKanji || surface == kJaChira ||
                   surface == kJaChiraKanji) {
            request = 5;
        } else if (surface == kJaKi || surface == kJaTsuku || surface == kJaTsukuKanji || surface == kJaOwaKanji) {
            request = 6;
        }
        if (request != 0) {
            Build(linked, request, suffix);
            sc.text.clear();
            return sc.text;
        }
    }
    if (next >= 0 && items[next].surface == kJaNa && KeiyoX(items[next])) {
        sc.text = kKoNeomu;
        return sc.text;
    }
    if (next >= 0 && (items[next].surface == kJaKire || items[next].surface == kJaKireKanji)) {
        const auto [head, marker] = CutLast(stem, 4);
        const auto [cls, rest] = FirstByte(suffix);
        sc.text = Cat({"다 ", head, LookupSource(Cat({marker, cls}), *generated_), rest});
        return sc.text;
    }
    if (linked >= 0 && items[linked].detailRecord == 2 && (surface == kJaItadaKanji || surface == kJaItada)) {
        ResolvedItem& l = items[linked];
        AssignGobiDst(l, suffix);
        if (l.detailClass != DetailClass::None) l.detailRecord = 0;
        return sc.text;
    }
    auto cutStem = [&] {
        const auto c = CutLast(stem, 4);
        key = c.tail;
        stem.resize(c.head.size());
    };
    std::string classText;  // the ending class cut from suffix
    switch (it.detailRecord) {
        case 1: {
            cutStem();
            replacement = LookupSource(Cat({key, "6"}), *generated_);
            const auto c = FirstByte(suffix);
            classText = c.head;
            suffix = std::string(c.tail);
            key = Cat({"&AL@", classText});
            replacement += LookupSource(key, *generated_);
            break;
        }
        case 2: {
            cutStem();
            replacement = Cat({LookupSource(Cat({key, "1"}), *generated_), "게 "});
            const auto c = FirstByte(suffix);
            classText = c.head;
            suffix = std::string(c.tail);
            key = Cat({"&AV@", classText});
            replacement += LookupSource(key, *generated_);
            break;
        }
        case 5: {
            cutStem();
            replacement = Cat({LookupSource(Cat({key, "1"}), *generated_), "게 "});
            const auto c = FirstByte(suffix);
            classText = c.head;
            suffix = std::string(c.tail);
            key = Cat({"&C6@", classText});
            replacement += LookupSource(key, *generated_);
            break;
        }
        case 4: {
            cutStem();
            replacement = Cat({LookupSource(Cat({key, "3"}), *generated_), " 수 "});
            const auto c = FirstByte(suffix);
            classText = c.head;
            suffix = std::string(c.tail);
            key = Cat({"&HN@", classText});
            if (Contains(DirectGAttr(it), "Yn")) {
                key = ReplaceAll(key, "&HN@", "&M9@");
                suffix = std::string(internal::SkipWidth(suffix, At(DirectCode(it), 1) == '7' ? 7 : 5));
                if (std::string_view(suffix).starts_with("았")) suffix = ReplaceAll(suffix, "았", "었");
                if (std::string_view(suffix).starts_with("아")) suffix = ReplaceAll(suffix, "아", "어");
            }
            replacement += LookupSource(key, *generated_);
            break;
        }
        default: {
            const size_t pos = stem.find("&HN@");
            if (pos != std::string::npos && Contains(GAttr(it), "Ke")) {
                if (next >= 0 && First(Code(items[next])) == '7' &&
                    (items[next].surface.starts_with(kJaNa) || items[next].surface.starts_with(kJaIna))) {
                    if (it.endingSurface.empty()) {
                        replacement = stem.substr(0, pos);
                        stem.clear();
                        suffix.clear();
                    } else {
                        const auto c = FirstByte(suffix);
                        classText = c.head;
                        suffix = std::string(c.tail);
                        key = Cat({internal::MidWidth(stem, 6, 4), classText});
                        replacement = LookupSource(key, *generated_);
                        stem.resize(pos);
                    }
                    break;
                }
                if (Contains(stem, kKoSuHnMarker) && suffix == "1지 않고") {
                    replacement = stem.substr(0, pos);
                    stem.clear();
                    suffix = "없고";
                    break;
                }
            }
            cutStem();
            const bool code57 = DirectCode(it).starts_with("57");
            if ((Contains(GAttr(it), "Va") || Contains(GAttr(it), "St")) && code57 && next >= 0 &&
                Contains(GAttr(items[next]), "Da")) {
                const auto c = FirstByte(DirectText(items[next]));
                key += c.head;
                suffix = std::string(c.tail);
            } else if (!suffix.empty()) {
                const auto c = FirstByte(suffix);
                key += c.head;
                suffix = std::string(c.tail);
            }
            ExclamationForm(key, suffix);
            if (!Contains(key, "&CJ@") || !Contains(suffix, kKoJuseyo)) {
                replacement = LookupSource(key, *generated_);
            } else {
                const size_t s = suffix.find_first_not_of(' ');
                suffix.erase(0, s == std::string::npos ? suffix.size() : s);
            }
            if (!replacement.empty() && Contains(GAttr(it), "Va") && std::string_view(key).ends_with("4") &&
                suffix == "다") {
                replacement = NormalizeDakeiyoText(i, replacement);
            }
            if (settings_.text && !it.endingSurface.ends_with("て") && !it.endingSurface.ends_with("で") &&
                SpokenEnd(key, suffix, TextAfter(items, i), it.endingSurface, sc.spoken)) {
                // 아니다 ending a sentence in the casual form is 아니야 (아니에요), not 아니어.
                replacement = replacement == "아니어" ? (suffix.empty() ? "아니야" : "아니에") : SpokenInfinitive(replacement);
            }
            break;
        }
    }
    if (key.ends_with('1') || key.ends_with('2')) replacement = DropRieulBefore(std::move(replacement), suffix);
    if (TextGate(i, key)) {
        it.selectionFlag = 1;
        sc.text = std::move(stem);
    } else {
        sc.text = Cat({stem, replacement, suffix});
    }
    return sc.text;
}

void Composer::BuildAdjective(int i) {
    std::vector<ResolvedItem>& items = *items_;
    ResolvedItem& it = items[i];
    Scratch& sc = state_[i];
    if (it.selected == -1) SetSelectedIndex(it, 0);
    if (EntryText(it).empty()) return;
    const int linked = i - 1;
    const int next = i + 1 < static_cast<int>(items.size()) ? i + 1 : -1;
    const bool nextIsKeiyoNa = next >= 0 && items[next].surface == kJaNa && KeiyoX(items[next]);
    const char selectedClass = First(Code(it));
    switch (selectedClass) {
        case '8':
            if (DakeiyoX(it) || nextIsKeiyoNa) {
                sc.text = kKoNeomu;
                return;
            }
            if (it.surface == kJaSugiKanji || it.surface == kJaSugi) {
                if (linked >= 0) Build(linked, 82, DirectText(it));
                sc.text.clear();
                return;
            }
            break;
        case '7':
            if (KeiyoX(it)) {
                if (it.surface != kJaNa) {
                    sc.text = kKoNeomu;
                    return;
                }
                std::string_view prevStem, prevSuffix;
                if (linked >= 0) {
                    prevStem = EntryText(items[linked]);
                    prevSuffix = DirectText(items[linked]);
                }
                const auto [head, marker] = CutLast(prevStem, 4);
                const auto [cls, rest] = FirstByte(prevSuffix);
                std::string result(head);
                result += LookupByMarker(Cat({marker, cls}), *generated_);
                if (WidthOf(prevSuffix) > 1) result += rest;
                sc.text = std::move(result);
                return;
            }
            if (nextIsKeiyoNa) {
                sc.text = kKoNeomu;
                return;
            }
            break;
    }
    if (it.directIndex == -1) {
        std::string_view base = EntryText(it);
        size_t marker = base.rfind('$');
        if (marker == std::string_view::npos) marker = base.rfind('%');
        if (marker == std::string_view::npos) marker = base.rfind('&');
        if (marker != std::string_view::npos) base = base.substr(0, marker);
        sc.text = base;
        return;
    }
    std::string stem(EntryText(it));
    std::string_view suffixView = DirectText(it);
    std::string key, replacement;
    if (it.surface == kJaNa && linked >= 0 && Contains(GAttr(items[linked]), "Ke") && it.endingSurface.empty()) {
        stem = Cat({CutLast(stem, 4).head, "$FA@"});
        if (const std::string_view g = GAttr(it); Contains(g, "Av")) {
            std::string v = ReplaceAll(g, "Av", "");
            v = ReplaceAll(v, "Js", "");
            v = ReplaceAll(v, "Ri", "");
            SetGAttr(it, it.selected, v);
        }
    }
    if (WidthOf(stem) >= 4) {
        const auto c = CutLast(stem, 4);
        key = c.tail;
        stem.resize(c.head.size());
    }
    const auto [cls, rest] = FirstByte(suffixView);
    key += cls;
    std::string suffix(rest);
    ExclamationForm(key, suffix);
    switch (selectedClass) {
        case '7': replacement = LookupDollar(key, *generated_); break;
        case '8': replacement = LookupPercent(key, *generated_); break;
    }
    if (DirectText(it).starts_with(kKoIlDa)) {
        bool normalize = true;
        if (linked >= 0) {
            switch (First(Code(items[linked]))) {
                case '4':
                    if (Contains(GAttr(items[linked]), "Va")) normalize = it.surface != kJaNa;
                    break;
                case '7': case '8':
                    normalize = it.surface != kJaNa;
                    break;
            }
        }
        if (normalize) replacement = NormalizeDakeiyoText(i, replacement);
    }
    if (settings_.text && key.size() == 5) {
        auto [r, s] = CopulaForm(i, key, replacement, suffix);
        replacement = std::move(r);
        suffix = std::move(s);
        auto negates = [&](int k) {
            if (k < 0 || items[k].surface != kJaNa) return false;
            const std::string_view e = EntryText(items[k]);
            return e == "$D7@" || e == "$DH@";
        };
        auto aniNoun = [&](int k) {
            const std::string_view e = EntryText(items[k]);
            return CopulaTarget(e, *generated_) && !CutLast(e, 4).head.ends_with("적");
        };
        if (linked >= 0 && negates(i) && aniNoun(linked) && DirectText(items[linked]) == "1지") {
            replacement = NotForm(Cat({replacement, suffix}));
            suffix.clear();
        }
        // 아니다 ending a sentence in the casual form is 아니야 (아니에요), not 아니어.
        if (SpokenEnd(key, suffix, TextAfter(items, i), it.endingSurface, sc.spoken)) {
            if (replacement == "니어" && stem.ends_with("아")) {
                replacement = suffix.empty() ? "니야" : "니에";
            } else {
                replacement = SpokenInfinitive(replacement);
            }
        }
    }
    if (key.ends_with('1') || key.ends_with('2')) replacement = DropRieulBefore(std::move(replacement), suffix);
    sc.text = Cat({stem, replacement, suffix});
}

std::pair<std::string, std::string> Composer::CopulaForm(int i, std::string_view key, std::string_view replacement,
                                                         std::string_view suffix) const {
    const std::vector<ResolvedItem>& items = *items_;
    const ResolvedItem& it = items[i];
    auto same = [&] { return std::pair<std::string, std::string>(replacement, suffix); };
    if (!CopulaTarget(EntryText(it), *generated_)) return same();
    const int next = i + 1 < static_cast<int>(items.size()) ? i + 1 : -1;
    const std::string_view ending = it.endingSurface;
    auto aniNoun = [&](int k) {
        const std::string_view e = EntryText(items[k]);
        return CopulaTarget(e, *generated_) && !CutLast(e, 4).head.ends_with("적");
    };
    const bool becomes = next >= 0 && items[next].surface == kJaNa && EntryText(items[next]).starts_with("&C6@");
    bool negates = false;
    if (next >= 0 && items[next].surface == kJaNa) {
        const std::string_view e = EntryText(items[next]);
        negates = e == "$D7@" || e == "$DH@";
    }
    if ((becomes && (ending == "く" || ending == "に" || ending.ends_with("ことに"))) ||
        (negates && aniNoun(i) && key[4] == '1' && suffix == "지")) {
        return {"(이)가", ""};
    }
    switch (key[4]) {
        case '6':
            if (suffix.starts_with("요")) return {"(이에)예", std::string(suffix)};
            if (!suffix.empty()) break;
            if (!ending.ends_with("で")) return {"(이)야", std::string(suffix)};
            if (next < 0 || EntryText(items[next]).empty() ||
                (First(Code(items[next])) == '6' && items[next].surface != kJaComma)) {
                return {"(이)라서", std::string(suffix)};
            }
            return {"(이어)여서", std::string(suffix)};
        case '1': {
            if (suffix.starts_with("지 않") && aniNoun(i)) {
                return {"(이)가", Cat({" ", NotForm(Cat({"않", suffix.substr(std::string_view("지 않").size())}))})};
            }
            std::string_view quoted, rest;
            if (CutQuoting(suffix, quoted, rest)) return {"(이)", Cat({"라", quoted, rest})};
            break;
        }
    }
    return same();
}

void Composer::BuildAction(int i, int action, std::string_view mode) {
    std::vector<ResolvedItem>& items = *items_;
    ResolvedItem& it = items[i];
    Scratch& sc = state_[i];
    if (!sc.text.empty() && sc.text != " ") return;
    if (it.selected == -1) SetSelectedIndex(it, 0);
    if (action < 10 && it.directIndex == -1) {
        sc.text = kInversionFailed;
        return;
    }
    const auto [stem, marker] = CutLast(EntryText(it), 4);
    const std::string stemText(stem);
    const auto [cls, suffix] = FirstByte(mode);
    const std::string key = Cat({marker, cls});
    const std::string replacement(Lookup(First(key), key));
    const std::string suffixText(suffix);
    const int linked = i - 1;
    std::string carried, pending;
    if (linked >= 0 && (it.surface == kJaSuru || Contains(GAttr(it), "Sk")) && First(Code(items[linked])) == '1' &&
        Contains(GAttr(items[linked]), "Hd")) {
        carried = EntryText(items[linked]);
        AssignDst(items[linked], items[linked].selected, "");
    }
    if (linked >= 0) {
        int flushed = linked;
        if (!carried.empty()) flushed = linked - 1;
        if (flushed >= 0) {
            const char fc = First(Code(items[flushed]));
            if ((fc == '7' && items[flushed].endingSurface == kJaKu) || fc == '3') Build(flushed, 0, {});
            if (Scratch& fs = state_[flushed]; !fs.text.empty()) {
                pending = Cat({fs.text, " "});
                fs.text = " ";
            }
        }
    }
    std::string_view adverb;
    switch (action) {
        case 1: adverb = "계속 "; break;
        case 2: case 82: adverb = "너무 "; break;
        case 3: adverb = "다시 "; break;
        case 4: adverb = "서로 "; break;
        case 5: adverb = "마구 "; break;
        case 6: adverb = "다 "; break;
        default:
            sc.text.clear();
            return;
    }
    sc.text = Cat({adverb, pending, carried, stemText, replacement, suffixText});
}

bool Composer::TextGate(int i, std::string_view key) {
    const std::vector<ResolvedItem>& items = *items_;
    const ResolvedItem& it = items[i];
    if (i + 1 >= static_cast<int>(items.size())) return false;
    const ResolvedItem& next = items[i + 1];
    const std::string_view marker = CutFirst(key, 4).head;
    if (CutFirst(EntryText(next), 4).head == marker) {
        const char cls = At(DirectCode(it), 1);
        if (cls == '3' || cls == '0' || cls == '6' || cls == ';' || cls == '5' || At(DirectCode(next), 1) == '3') {
            return false;
        }
        // A continuative with も before する keeps its ending (疑いもした 의심하기도 했다).
        if (it.endingSurface == "も") return false;
        // A connective said again (泣いて泣いて 울어 울어) is written each time.
        if (cls == '7') return WidthOf(DirectText(it)) <= 1 && (it.surface != next.surface || it.endingSurface != next.endingSurface);
        return true;
    }
    return it.endingSurface != kJaNaide && marker == "&CJ@" && EntryText(next) == kKoJuseyo;
}

std::string Composer::NormalizeDakeiyoText(int i, std::string_view value) const {
    const std::string_view cl = GAttr((*items_)[i]);
    const bool av = Contains(cl, "Av");
    if (av) {
        if (!Contains(cl, "Ri") && Contains(cl, "Js")) return Cat({value, kKoNeun});
    } else if (!Contains(cl, "Va")) {
        return std::string(value);
    }
    const auto last = DecodeLastRune(TextBefore(value), value.size());
    const int final = text::Final(last.cp);
    char32_t r = last.cp;
    if (final < 0) return std::string(value);
    if (av) {
        r = text::SetFinal(r, text::kFinalNieun);
    } else if (final == text::kFinalNieun) {
        r = text::SetFinal(r, text::kFinalNone);
    }
    std::string out(value.substr(0, value.size() - last.size));
    text::AppendRune(out, r);
    return out;
}

}  // namespace hdTrans::output
