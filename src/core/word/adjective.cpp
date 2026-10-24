// Words without conjugation, adjectives and adjectival nouns: what follows them and
// their endings.
#include <cstdint>
#include <algorithm>

#include "core/output/inflect.h"
#include "core/text/unicode.h"
#include "core/tokenizer/token.h"
#include "core/word/strings.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

int Byte(char c) { return static_cast<unsigned char>(c); }

// The adverbs that take です on the text path (そろそろです, まだです), which the
// validity rules reject after an adverb.
bool PredicativeAdverb(std::string_view s) {
    for (const std::string_view a : {"そろそろ", "まだ", "もうすぐ", "もうちょっと", "もう少し", "あとちょっと", "あと少し",
                                     "まもなく", "間もなく", "もうじき"}) {
        if (s == a) return true;
    }
    return false;
}

// Two width units of numerals (兆億万千百十九..一○).
bool NumeralPair(const Window& w) { return !w.broken && WidthOf(w.text) == 2 && Has(kNumeralOrder, w.text); }

// Whether an entry of the stem group g says no: a negative of the dictionary (gram Xn) or
// a Korean whose last word is the predicate 없다, 않다 or 모르다 (知らない $C4@, 行けない
// 갈 수 $FA@), not an adjective that only ends in one (馬鹿馬鹿しい 어처구니$FA@).
bool NegativeStem(const Group& g) {
    for (const Entry* e : g.entries) {
        if (Has(e->gattr, "Xn")) return true;
        const std::string_view t = e->dst;
        if (t.size() < 4 || !t.ends_with("@") || (t.size() > 4 && t[t.size() - 5] != ' ')) continue;
        const output::internal::KoreanFamily* f = output::internal::FindKoreanFamily(t.substr(t.size() - 4));
        if (f != nullptr && (f->stem == "없" || f->stem == "않" || f->stem == "모르")) return true;
    }
    return false;
}

// An ending row in いことはない or いことは無い reads a double negative (知らないことはない 모르는
// 것은 아니다); after a stem that does not say no, ことはない is a superlative (こんなに切ない
// ことはない 이렇게 안타까운 일은 없다).
bool DoubleNegativeRow(const Entry& e) {
    return e.user != nullptr && At(e.user->codes->partText, 0) == 'T' && e.user->key.starts_with("いことは");
}

}  // namespace

// Accepts a candidate without conjugation unless a validity rule rejects what follows
// it, or it is a two-character numeral word after a numeral. Only the first entry's J
// digit could decide, and it never rejects.
bool Item::RunDefaultTrieQuery() {
    const std::string_view surface = group->surface;
    const std::string_view markerText = text.substr(0, static_cast<std::size_t>(start));
    std::string_view remaining;
    if (group->length + start <= static_cast<int>(text.size())) remaining = From(text, start + group->length);
    const char marker = At(Code(0), 0);
    if (!w->MatchDictionary(remaining, surface, marker, 'X') &&
        !(w->settings.text && PredicativeAdverb(surface) && remaining.starts_with("です"))) {
        return false;
    }
    if (group->entries.empty()) return false;
    // A noun ending in ら before しい, しく or しかっ leaves ら to らしい (お前らしい: お前 + らしい).
    if (marker == '1' && surface.ends_with("ら") &&
        (remaining.starts_with("しい") || remaining.starts_with("しく") || remaining.starts_with("しかっ"))) {
        return false;
    }
    if (const Window first = Left(surface, 2); marker == '1' && NumeralPair(first) && WidthOf(surface) == 4) {
        if (NumeralPair(Right(markerText, 2))) return false;
    }
    return true;
}

// Checks what follows an adjective stem and looks up its ending (adjective endings)
// into the direct group.
bool Item::RunKeiyoTrieQuery() {
    Detail& base = *detail;
    const std::string_view surface = group->surface;
    const int surfaceLength = group->length;
    const std::string_view remaining = From(text, surfaceLength + start);
    const int offset = base.scanOffset;
    if (!w->MatchDictionary(remaining, surface, '7', 'X')) return false;
    // No adverbial く goes on into くらい or くせに: those follow a past in た (したくらい, not
    // the stem of したい).
    if (remaining.starts_with(kJaKurai) || remaining.starts_with(kJaKuseni)) return false;
    // A stem in た (the desire たい, 冷たい) has no adnominal な, which only a few stems such
    // as 大き and 小さ take: たな is the past and a particle (慣れてきたな, not 来たい).
    if (surface.ends_with("た") && remaining.starts_with(kJaNa)) return false;
    length = base.kind == 'x' ? surfaceLength : offset + surfaceLength;
    const std::string_view second = From(remaining, offset);
    if (!w->MatchDictionary(second, "", '6', '7')) return false;
    if (base.kind == 'x') {
        if (remaining.starts_with(kJaSaSugiKanji) || remaining.starts_with(kJaSaSugi)) length += static_cast<int>(kJaSa.size());
        base.group = nullptr;
        base.resultCount = 0;
        return true;
    }
    base.punct = 0;
    base.ResetDirect(0);
    if (second.starts_with(kJaSou)) {
        Record* r = w->NewRecord();
        r->target = "1";
        r->partCode = "T500";
        r->attr = "Nm";
        Group* g = w->NewGroup(r, ' ');
        base.ResetDirect(g->entries.size(), false);
        base.enabled[0] = true;
        base.group = g;
        base.resultCount = 0;
        return true;
    }
    Group* g = nullptr;
    if (!second.empty()) {
        if (const Lookup found = w->DoLookup(second, kModeAdjectiveEnding); found.count != 0) g = w->NewGroup(found.records, ' ');
    }
    if (g != nullptr && g->length > 0) {
        for (;;) {
            bool keepScanning = true;
            base.ResetDirect(g->entries.size());
            base.resultCount = static_cast<int>(g->entries.size());
            const std::string_view after = From(remaining, offset + g->length);
            base.punct = base.ClassifyTrailingPunctuation(after);
            for (std::size_t i = 0; i < g->entries.size(); i++) base.classes[i] = static_cast<uint8_t>(base.punct);
            bool matched = false;
            for (int index = 0; keepScanning && index < static_cast<int>(g->entries.size()); index++) {
                const Entry& e = *g->entries[static_cast<std::size_t>(index)];
                if (Byte(At(e.code, 1)) != base.kind || (Has(e.gattr, "Sh") && Classify(remaining) != 0) ||
                    (DoubleNegativeRow(e) && !NegativeStem(*group)) ||
                    !w->EndingApplies(e, after, tokenIndex) ||
                    w->CutsWord(remaining.substr(0, remaining.size() - second.size()), second.substr(0, std::min<std::size_t>(static_cast<std::size_t>(g->length), second.size())), after)) {
                    base.enabled[index] = false;
                    continue;
                }
                base.group = g;
                if (CheckDirectJ(index) == 0) {
                    base.enabled[index] = false;
                    continue;
                }
                if (g->length <= 0 || w->MatchDictionary(after, g->surface, '5', '7')) {
                    if (g->length > 0 || group->surface != kJaI) matched = true;
                } else {
                    keepScanning = false;
                }
            }
            if (matched) break;
            Record* rest = g->records;
            g->records = nullptr;
            if (rest == nullptr) {
                base.group = nullptr;
                return false;
            }
            g = w->NewGroup(rest, ' ');
        }
    }
    if (g == nullptr && second.empty() && StandsAlone()) {
        Record* r = w->NewRecord();
        r->target = PlainEnding();
        r->partCode = "T500";
        g = w->NewGroup(r, ' ');
        base.ResetDirect(g->entries.size(), false);
        base.enabled[0] = true;
        base.group = g;
        base.resultCount = 0;
        return true;
    }
    if (g == nullptr) {
        base.group = nullptr;
        return false;
    }
    base.resultCount = static_cast<int>(g->entries.size());
    base.group = g;
    length += g->length;
    return true;
}

// RunKeiyoTrieQuery for adjectival nouns: the ending comes from the adjectival-noun
// endings and must be a 5x entry of the item's kind.
bool Item::RunDakeiyoCase8Query() {
    Detail& base = *detail;
    const std::string_view groupText = group->surface;
    const int directLength = group->length;
    const std::string_view tail = From(text, start + directLength);
    const int scanOffset = base.scanOffset;
    if (!w->MatchDictionary(tail, groupText, '8', 'X')) return false;
    length = base.kind != 'X' ? scanOffset + directLength : directLength;
    const std::string_view second = From(tail, scanOffset);
    if (!w->MatchDictionary(second, "", '6', '8')) return false;
    if (base.kind == 'X') {
        if (tail.starts_with(kJaSaSugiKanji) || tail.starts_with(kJaSaSugi)) length += static_cast<int>(kJaSa.size());
        base.resultCount = 0;
        base.group = nullptr;
        return true;
    }
    base.ResetDirect(0);
    base.punct = 0;
    auto pseudo = [&](std::string_view target, std::string_view attr) {
        Record* r = w->NewRecord();
        r->target.assign(target);
        r->partCode = "U500";
        r->attr.assign(attr);
        Group* g = w->NewGroup(r, ' ');
        base.ResetDirect(g->entries.size(), false);
        base.enabled[0] = true;
        base.resultCount = static_cast<int>(g->entries.size());
        base.group = g;
        return g;
    };
    if (second.starts_with(kJaRashi)) {
        length += pseudo("1", "Nm")->length;
        return true;
    }
    if ((second.empty() || second.starts_with(kJaComma)) && w->settings.text && MitaiLike()) {
        std::string ending = "1";
        ending += kKoDa;  // 같다; 같아 in a question
        std::string_view following = second;
        if (const int n = tokenIndex + 1; following.empty() && n < w->TokenCount() && w->Span(n) == 0) following = w->Tok(n).text;
        if (following.starts_with(kJaQuestion) || following.starts_with("?")) ending = "6";
        pseudo(ending, "");
        return true;
    }
    if (const std::string_view ending = BareStemEnding(second); !ending.empty()) {
        pseudo(ending, "");
        return true;
    }
    if (second.empty()) {
        if (StandsAlone()) {
            pseudo(PlainEnding(), "");
            return true;
        }
        base.group = nullptr;
        return false;
    }
    const Lookup found = w->DoLookup(second, kModeAdjectivalNounEnding);
    if (found.count == 0) {
        base.group = nullptr;
        return false;
    }
    Group* g = w->NewGroup(found.records, ' ');
    if (g->length <= 0) {
        base.ResetDirect(g->entries.size());
        base.group = g;
        base.resultCount = static_cast<int>(g->entries.size());
        length += g->length;
        return true;
    }
    for (;;) {
        base.ResetDirect(g->entries.size());
        base.resultCount = static_cast<int>(g->entries.size());
        const std::string_view after = From(tail, scanOffset + g->length);
        base.punct = base.ClassifyTrailingPunctuation(after);
        for (std::size_t i = 0; i < g->entries.size(); i++) base.classes[i] = static_cast<uint8_t>(base.punct);
        bool matched = false;
        const std::string_view scanned = From(tail, scanOffset);
        for (int entry = 0; entry < static_cast<int>(g->entries.size()); entry++) {
            const Entry& e = *g->entries[static_cast<std::size_t>(entry)];
            // The adnominal な goes before a noun, never before the particle って (こうなって is
            // こう and なって, not こうな; the quote of an adjectival noun is だって).
            if (g->surface == kJaNa && after.starts_with(kJaTte)) {
                base.enabled[entry] = false;
                continue;
            }
            if ((Has(e.gattr, "Sh") && Classify(tail) != 0) || At(e.code, 0) != '5' || Byte(At(e.code, 1)) != base.kind ||
                !w->EndingApplies(e, after, tokenIndex) ||
                w->CutsWord(tail.substr(0, tail.size() - scanned.size()), scanned.substr(0, std::min<std::size_t>(static_cast<std::size_t>(g->length), scanned.size())), after)) {
                base.enabled[entry] = false;
                continue;
            }
            base.group = g;
            if (CheckDirectJ(entry) == 0) {
                base.enabled[entry] = false;
                continue;
            }
            if (g->length <= 0 || w->MatchDictionary(after, g->surface, '5', '8')) matched = true;
            else break;
        }
        if (matched) {
            base.group = g;
            base.resultCount = static_cast<int>(g->entries.size());
            length += g->length;
            return true;
        }
        Record* rest = g->records;
        g->records = nullptr;
        if (rest == nullptr) break;
        g = w->NewGroup(rest, ' ');
    }
    base.group = nullptr;
    return false;
}

// A stem that is a sentence of its own (厳し。, 静か。): nothing but punctuation or
// spaces before it, the end of a sentence right after it, and no other word of its
// length in the dictionary (出た。 is 出 and た). It then reads in its plain form.
// Of the adjectives only the し stems qualify, which are also the old terminal form;
// the others are cut-off forms whose last kana is a verb ending or a particle
// (試してみた, ではな). An adjectival-noun stem ending in な is a cut-off ない; one
// ending in みたい is left to MitaiLike.
bool Item::StandsAlone() const {
    const std::string_view surface = group->surface;
    if (detail->cls == kDetailKeiyo ? !surface.ends_with(kJaShi)
                                    : surface.ends_with(kJaNa) || surface.ends_with(kJaMitai)) {
        return false;
    }
    if (start + group->length != static_cast<int>(text.size())) return false;
    if (const int n = detail->nextType; n != tokenizer::kTypeAsciiPunct && n != tokenizer::kTypeSentenceEnd) return false;
    if (const int p = detail->ClassifyTrailingPunctuation({}); p < 2 || p > 4) return false;
    if (start > 0) {
        if (!text::IsPunct(DecodeLastRune(text.substr(0, static_cast<std::size_t>(start))).cp)) return false;
    } else if (tokenIndex > 0) {
        const tokenizer::TokenView before = w->Tok(tokenIndex - 1);
        switch (before.type) {
        case tokenizer::kTypeAsciiPunct:
        case tokenizer::kTypeAsciiOpen:
        case tokenizer::kTypeNewline:
        case tokenizer::kTypeAsciiSpace:
        case tokenizer::kTypeSentenceEnd:
        case tokenizer::kTypeBracket:
        case tokenizer::kTypeWideSpace: break;
        case tokenizer::kTypeJapanese:
            if (text::IsPunct(DecodeLastRune(before.text).cp)) break;
            return false;
        default: return false;
        }
    }
    const Item* other = w->InsertLateCandidate(text, start, tokenIndex, group->records);
    return other == nullptr || other->length < group->length;
}

// The ending of a stem that stands alone: 어렵다, 어려워 before a question mark.
namespace {

bool SentenceMark(char32_t r) {
    return r == U'。' || r == U'！' || r == U'？' || r == U'…' || r == U'‥' || r == U'．' || r == U'!' || r == U'?' ||
           r == U'.' || r == U'♪' || r == U'♡' || r == U'♥' || r == U'❤';
}

bool Hiragana(char32_t r) { return r >= U'ぁ' && r <= U'ゖ'; }

// Whether rest, a comma and the text after the word, goes on to another comma before
// any mark that ends a clause: the word is then an item of a list (「陰険、脳筋、…」),
// unless the item after it repeats it (駄目、駄目、…).
bool ListsNouns(std::string_view word, std::string_view rest) {
    rest.remove_prefix(text::DecodeRune(rest).size);
    for (std::size_t i = 0; i < rest.size();) {
        const text::Rune r = text::DecodeRune(rest.substr(i));
        if (r.cp == U'、' || r.cp == U'，') return rest.substr(0, i) != word;
        if (SentenceMark(r.cp) || r.cp == U'」' || r.cp == U'』') return false;
        i += r.size;
    }
    return false;
}

}  // namespace

// What stands right before the word: 0 the start of a sentence (nothing, a mark that
// ends a sentence, a space, an opening quote), 1 another punctuation mark (、, （), 2 a
// word.
int Item::ContextBefore() const {
    char32_t r = 0;
    if (start > 0) {
        r = DecodeLastRune(text.substr(0, static_cast<std::size_t>(start))).cp;
    } else if (tokenIndex > 0) {
        const tokenizer::TokenView before = w->Tok(tokenIndex - 1);
        switch (before.type) {
        case tokenizer::kTypeNewline:
        case tokenizer::kTypeAsciiSpace:
        case tokenizer::kTypeWideSpace:
        case tokenizer::kTypeSentenceEnd: return 0;
        case tokenizer::kTypeAsciiOpen: return 1;
        case tokenizer::kTypeBracket:
        case tokenizer::kTypeAsciiPunct:
        case tokenizer::kTypeJapanese:
        case tokenizer::kTypeSymbol: r = DecodeLastRune(before.text).cp; break;
        default: return 2;
        }
    } else {
        return 0;
    }
    if (SentenceMark(r) || r == U' ' || r == U'\u3000' || r == U'「' || r == U'『') return 0;
    return text::IsPunct(r) ? 1 : 2;
}

// The plain ending of an adjectival-noun stem that ends a clause with its copula left
// out (そんなの駄目！, 「綺麗」, もう駄目…): after the stem come only lengthening kana
// (っ, ー, small vowels) and then a mark that ends the sentence, a closing quote or the
// end of the text, or a comma when the stem opens the sentence. A one-kanji stem needs
// a sentence mark after it and no kanji before it (「朧」 and 李雅微 are names). Stems
// in hiragana are left to StandsAlone (下さいまし is not まし). "" when the stem does
// not end a clause, ends in な, みたい or そう (the そう row reads 것 같다 there), or a
// word of its length (a noun reading) exists.
std::string_view Item::BareStemEnding(std::string_view after) const {
    const std::string_view surface = group->surface;
    if (surface.empty() || surface.ends_with(kJaNa) || surface.ends_with(kJaMitai) || surface.ends_with(kJaSou)) return {};
    std::string_view rest = after;
    while (!rest.empty()) {
        const text::Rune r = text::DecodeRune(rest);
        if (r.cp != U'っ' && r.cp != U'ッ' && r.cp != U'ー' && r.cp != U'～' && r.cp != U'〜' && r.cp != U'ぁ' &&
            r.cp != U'ぃ' && r.cp != U'ぅ' && r.cp != U'ぇ' && r.cp != U'ぉ') {
            break;
        }
        rest.remove_prefix(r.size);
    }
    const text::Rune first = text::DecodeRune(surface);
    const bool oneKanji = first.size == surface.size() && text::IsKanji(first.cp);
    if (Hiragana(first.cp)) return {};
    const int before = ContextBefore();
    if (oneKanji && start > 0 && text::IsKanji(DecodeLastRune(text.substr(0, static_cast<std::size_t>(start))).cp)) return {};
    bool question = false;
    if (!rest.empty()) {
        const char32_t r = text::DecodeRune(rest).cp;
        if (r == U'、' || r == U'，') {
            const std::string_view head = text.substr(0, static_cast<std::size_t>(start));
            const bool repeated = head.ends_with(std::string(surface) + "、");
            if ((before != 0 && !repeated) || ListsNouns(surface, rest)) return {};
        } else if (r != U'」' && r != U'』') {
            return {};
        }
        if (oneKanji) return {};
    } else {
        switch (detail->nextType) {
        case tokenizer::kTypeAsciiPunct:
        case tokenizer::kTypeSentenceEnd:
        case tokenizer::kTypeNewline:
        case tokenizer::kTypeEnd: break;
        case tokenizer::kTypeSymbol:
            if (!detail->nextText.starts_with("…") && !detail->nextText.starts_with("‥")) return {};
            break;
        case tokenizer::kTypeBracket:
            if (oneKanji || !(detail->nextText.starts_with("」") || detail->nextText.starts_with("』"))) return {};
            break;
        default: return {};
        }
        const int p = detail->ClassifyTrailingPunctuation({});
        if (p == 1 && (before != 0 || oneKanji)) return {};
        question = p == 3;
    }
    const Item* other = w->InsertLateCandidate(text, start, tokenIndex, group->records);
    if (other != nullptr && other->length >= group->length) return {};
    return question ? "6" : "1다";
}

std::string_view Item::PlainEnding() const { return detail->ClassifyTrailingPunctuation({}) == 3 ? "6" : "1다"; }

// A word ending in みたい that reads "like" (같다): みたい after a word, possibly across
// … or ‥, but not after the て/で of a verb (見てみたい, try to see), an object
// particle を/が or a comma, where it is 見たい. A longer word that ends in みたい
// (バカみたい) always does.
bool Item::MitaiLike() const {
    const std::string_view surface = group->surface;
    if (!surface.ends_with(kJaMitai)) return false;
    if (surface.size() > kJaMitai.size()) return true;
    const std::string before = CharBefore();
    return !before.empty() && before != kJaTe && before != kJaDe && before != kJaWo && before != kJaGa && before != kJaComma;
}

std::string Item::CharBefore() const {
    if (start <= static_cast<int>(text.size()) && WidthOf(text.substr(0, static_cast<std::size_t>(start))) >= 2) {
        return Right(text.substr(0, static_cast<std::size_t>(start)), 2).Str();
    }
    if (start != 0) return {};
    for (int t = tokenIndex - 1; t >= 0; t--) {
        if (w->Span(t) != 0) return {};
        std::string_view typed = w->Typed(t);
        while (typed.ends_with(kJaEllipsis) || typed.ends_with(kJaTwoDots)) typed.remove_suffix(kJaEllipsis.size());
        if (WidthOf(typed) >= 2) return Right(typed, 2).Str();
        if (!typed.empty()) return {};
    }
    return {};
}

}  // namespace hdTrans::word
