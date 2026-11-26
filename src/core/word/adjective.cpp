// Words without conjugation, adjectives and adjectival nouns: what follows them and
// their endings.
#include <cstdint>
#include <algorithm>

#include "core/dict/classes.h"
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

std::size_t NameBeforeHonorific(const dict::Snapshot& snapshot, std::string_view s) {
    std::size_t name = 0;
    for (std::size_t n = 0, runes = 0; n < s.size() && runes < 8; runes++) {
        const text::Rune r = text::DecodeRune(s.substr(n));
        if (r.cp < 0x3041 || r.cp > 0x309F) break;
        n += std::max<std::size_t>(1, r.size);
        if (runes < 1 || !snapshot.NamePerson(s.substr(0, n))) continue;
        const std::size_t h = dict::StartsWithMember(HDTRANS_CLASS("honorific.name"), s.substr(n));
        // An honorific in small っ joins the last mora of a word as often as a name (みっちり).
        if (h == 0 || s.substr(n).starts_with("っ")) continue;
        // Not where a dictionary word that starts inside the name runs through the honorific
        // (がんばるみなさん: みなさん, 連れてこっち: こっち, 始まりみっちり: みっちり).
        bool word = false;
        for (std::size_t p = std::max<std::size_t>(1, text::DecodeRune(s).size); p < n && !word;
             p += std::max<std::size_t>(1, text::DecodeRune(s.substr(p)).size)) {
            for (std::size_t e = n + h, more = 0; e <= s.size() && more < 4 && !word; more++) {
                for (const dict::Candidate& c : snapshot.RetrieveRecords(s.substr(p, e - p), dict::LookupMode::General)) {
                    if (c.source == s.substr(p, e - p)) word = true;
                }
                if (e == s.size()) break;
                e += std::max<std::size_t>(1, text::DecodeRune(s.substr(e)).size);
            }
        }
        if (!word) name = n;
    }
    return name;
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
    // A そう that starts a name before an honorific has no stem before it (走ってきたそうすけ君
    // 달려온 소스케 군, not 来たい and そう).
    if (second.starts_with(kJaSou) && NameBeforeHonorific(w->snapshot, second) > 0) return false;
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
                // An ending that makes a noun (gram Pn: the nominal み) stands only before a
                // particle, punctuation or the end, and only where the stem and the ending are no
                // verb form or noun of the dictionary (楽しみ、 苦しみながら, 高み, 甘み), which the
                // later records then read.
                if (Byte(At(e.code, 1)) != base.kind || (Has(e.gattr, "Sh") && Classify(remaining) != 0) ||
                    (Has(e.gattr, "Pn") &&
                     (!NounEndAt(after) || VerbOrNounKey(w->snapshot, surface, g->surface))) ||
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
    // LEX-5793: where the dictionary has the stem with な as an adjective (ご存じな of ご存じない,
    // だいじな), ない or なければ after the stem is that adjective, not the adnominal な and いる.
    if (tail.starts_with(kJaNa) &&
        (tail.substr(kJaNa.size()).starts_with("い") || tail.substr(kJaNa.size()).starts_with("けれ"))) {
        const std::string key = std::string(groupText) + std::string(kJaNa);
        const std::string query = std::string(groupText) + std::string(tail);
        for (const Record* r = w->DoLookup(query, kModeGeneral).records; r != nullptr; r = r->child) {
            if (At(r->partCode, 1) == '7' && r->source == key) return false;
        }
    }
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
    // An ending of gram code Lo (a particle that wants a noun or a quote: とか, が, の) follows only a
    // stem of two or more characters that starts with a kanji and reads as nothing else: its group
    // has no row of another class for its spelling, the names have no person of that spelling
    // (優美が来た, 風雅を), and no longer row starts with it here (駄目を押す 다짐하다). Such a stem
    // would otherwise be read kanji by kanji (駄目とか 안 된다든가, 気丈を 다부짐을; but 元気とか
    // stays the noun 건강이라든가). The quoting と is not the と of a stem before する or なる
    // (飄々とした, 切実となって), nor the と of manner of a stem that ends in a member of
    // stem.taru-final (飄々と歩いた, 汲々と働く; spec LEX-7600).
    auto loBlocked = [&](std::string_view ending, std::string_view after) {
        const text::Rune first = text::DecodeRune(groupText);
        if (!text::IsKanji(first.cp) || first.size == groupText.size()) return true;
        if (w->snapshot.NamePerson(groupText)) return true;
        if (ending == "と" && (dict::StartsWithMember(HDTRANS_CLASS("follower.taru-to"), after) > 0 ||
                               dict::EndsWithMember(HDTRANS_CLASS("stem.taru-final"), groupText) > 0)) {
            return true;
        }
        const std::string_view here = From(text, start);
        for (const Record* r = group->records; r != nullptr; r = r->child) {
            if (r->source == groupText && At(r->partCode, 1) != '8' && At(r->partCode, 1) != '5') return true;
            if (r->source.size() > groupText.size() && r->source.starts_with(groupText) && here.starts_with(r->source)) return true;
        }
        return false;
    };
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
                (Has(e.gattr, "Lo") && loBlocked(g->surface, after)) || !w->EndingApplies(e, after, tokenIndex) ||
                (scanOffset == 0 && w->StemTakesSu(groupText, scanned.substr(0, std::min<std::size_t>(static_cast<std::size_t>(g->length), scanned.size())))) ||
                w->CutsWord(tail.substr(0, tail.size() - scanned.size()), scanned.substr(0, std::min<std::size_t>(static_cast<std::size_t>(g->length), scanned.size())), after)) {
                base.enabled[entry] = false;
                continue;
            }
            base.group = g;
            if (CheckDirectJ(entry) == 0) {
                base.enabled[entry] = false;
                continue;
            }
            if (g->length <= 0 || w->MatchDictionary(after, g->surface, '5', '8') ||
                (g->surface == kJaNa && w->WordAfterAdnominal(after))) {
                matched = true;
            } else {
                break;
            }
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
// ending in みたい is left to MitaiLike, and よう alone is the call (よう 요우), not 様.
bool Item::StandsAlone() const {
    const std::string_view surface = group->surface;
    if (detail->cls == kDetailKeiyo ? !surface.ends_with(kJaShi)
                                    : surface.ends_with(kJaNa) || surface.ends_with(kJaMitai) || surface == kJaYou) {
        return false;
    }
    if (start + group->length != static_cast<int>(text.size())) return false;
    // The end of the text, unless lines may break mid-sentence, or an ellipsis or a drawn-out wave dash that
    // the stem's ending does not follow ends the sentence too (すてき, かわいそう…, なめらか〜。; not おいし…い,
    // すてき…な人, なめらか〜な肌).
    const int n = detail->nextType;
    bool closed = n == tokenizer::kTypeEnd && !w->settings.wrapped;
    const auto drawn = [](std::string_view s) {
        return s.starts_with("…") || s.starts_with("‥") || s.starts_with("〜") || s.starts_with("～") || s.starts_with("ー");
    };
    if ((n == tokenizer::kTypeSymbol || n == tokenizer::kTypeSentenceEnd) && drawn(detail->nextText)) {
        std::string_view after = detail->nextText;
        while (drawn(after) || after.starts_with("　")) after.remove_prefix(std::string_view("…").size());
        while (after.starts_with(" ")) after.remove_prefix(1);
        for (int k = tokenIndex + 2; after.empty() && k < w->TokenCount(); k++) {
            const tokenizer::TokenView token = w->Tok(k);
            std::string_view piece = token.text;  // past spaces
            while (piece.starts_with(" ")) piece.remove_prefix(1);
            while (piece.starts_with("　")) piece.remove_prefix(std::string_view("　").size());
            after = piece;
        }
        const auto goesOn = [&](std::initializer_list<std::string_view> endings) {
            return std::any_of(endings.begin(), endings.end(), [&](std::string_view e) { return after.starts_with(e); });
        };
        closed = !(detail->cls == kDetailKeiyo ? goesOn({"い", "く", "か", "け", "さ", "そう", "すぎ"}) : goesOn({"な", "だ", "で", "に"}));
    }
    if (!closed && n != tokenizer::kTypeAsciiPunct && n != tokenizer::kTypeSentenceEnd) return false;
    if (const int p = detail->ClassifyTrailingPunctuation({}); !closed && (p < 2 || p > 4)) return false;
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
// in hiragana read bare only after が, は, も or より (下さいまし is not まし). "" when the stem does
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
    const int before = ContextBefore();
    // A stem in hiragana only, not よう, reads bare only where its clause starts (すてき」, ホントに すてき) or
    // after a particle a predicate follows (方がまし。, よりはまし」, 本当にすてき), not after a verb stem
    // (下さいまし); one with kanji after a hiragana prefix (お好き) does not.
    if (Hiragana(first.cp)) {
        const std::string_view head = text.substr(0, static_cast<std::size_t>(start));
        bool kana = surface != kJaYou;
        for (std::string_view left = surface; kana && !left.empty(); left.remove_prefix(text::DecodeRune(left).size)) {
            kana = Hiragana(text::DecodeRune(left).cp);
        }
        // A token of its own after Japanese text was set apart by a space (ホントに すてき).
        const bool apart = start == 0 && tokenIndex > 0 && w->Tok(tokenIndex - 1).type == tokenizer::kTypeJapanese;
        // Not before lengthening kana there: a clipped adjective (おもっ！) is no stem.
        // Nor before a comma (いいかげん、), nor for a stem the text spells in katakana.
        const bool written = Hiragana(text::DecodeRune(text.substr(static_cast<std::size_t>(start))).cp);
        // A word after a pause (……アラタ……) is no clause start of its own.
        const std::string_view prior = !head.empty() ? head : tokenIndex > 0 ? w->Tok(tokenIndex - 1).text : std::string_view();
        const bool pause = prior.ends_with("…") || prior.ends_with("‥");
        const bool opens = written && !pause && rest.size() == after.size() && !rest.starts_with("、") && !rest.starts_with("，") &&
                           (before == 0 || apart || head.ends_with("に"));
        if (!kana || (!opens && !head.ends_with("が") && !head.ends_with("は") && !head.ends_with("も") && !head.ends_with("より"))) {
            return {};
        }
    }
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
            // An ellipsis, or a note or heart that closes an utterance (素敵♪ 멋지다♪).
            if (!detail->nextText.starts_with("…") && !detail->nextText.starts_with("‥") &&
                !detail->nextText.starts_with("♪") && !detail->nextText.starts_with("♡") && !detail->nextText.starts_with("♥")) {
                return {};
            }
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
    if (start <= static_cast<int>(text.size()) && WidthAtLeast(text.substr(0, static_cast<std::size_t>(start)), 2)) {
        return Right(text.substr(0, static_cast<std::size_t>(start)), 2).Str();
    }
    if (start != 0) return {};
    for (int t = tokenIndex - 1; t >= 0; t--) {
        if (w->Span(t) != 0) return {};
        std::string_view typed = w->Typed(t);
        while (typed.ends_with(kJaEllipsis) || typed.ends_with(kJaTwoDots)) typed.remove_suffix(kJaEllipsis.size());
        if (WidthAtLeast(typed, 2)) return Right(typed, 2).Str();
        if (!typed.empty()) return {};
    }
    return {};
}

}  // namespace hdTrans::word
