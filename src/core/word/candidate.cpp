// The per-token step: segmentation of a token into dictionary words, candidate groups
// and the record of words the dictionary does not know.
#include <algorithm>

#include "core/dict/char_readings.h"
#include "core/output/cleanup.h"
#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/tokenizer/canonical.h"
#include "core/tokenizer/charsets.h"
#include "core/tokenizer/classify.h"
#include "core/word/conjugation.h"
#include "core/word/kana.h"
#include "core/word/strings.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

// A kanji digit, ○ or 〇.
bool IsNumeral(std::string_view c) { return (!c.empty() && Has(kNumeralDigits, c)) || c == kNumeralCircle; }

// The run of letters, digits, ',' and '.' at s.
std::string_view AsciiRun(std::string_view s) {
    std::size_t n = 0;
    while (n < s.size() && (IsDigit(s[n]) || IsAlpha(s[n]) || s[n] == ',' || s[n] == '.')) n++;
    return s.substr(0, n);
}

// The first two width units of s: one wide character or two narrow ones; a narrow
// character before a wide one stands alone.
std::string_view PairText(std::string_view s) {
    const text::Rune r = text::DecodeRune(s);
    if (Width(r.cp) == 2) return s.substr(0, r.size);
    const text::Rune r2 = text::DecodeRune(s.substr(r.size));
    if (r2.size > 0 && Width(r2.cp) == 1) return s.substr(0, r.size + r2.size);
    return s.substr(0, r.size);
}

// 、（）「」『』.
bool StartsWithSymbol(std::string_view s) { return !s.empty() && Has("、（）「」『』", FirstChar(s)); }

// A kanji numeral of up to two digits as ASCII digits (二十五 -> "25", 十 -> "10",
// 二十 -> "20", 三 -> "3"); a first character that is not a numeral reads as 0. Text
// that starts with an ASCII character is returned as is.
std::string EncodeNumericPair(std::string_view text) {
    if (text.empty() || static_cast<unsigned char>(text[0]) < 0x80) return std::string(text);
    std::u32string rs;
    for (std::string_view s = text; !s.empty();) {
        const text::Rune r = text::DecodeRune(s);
        rs.push_back(r.cp);
        s.remove_prefix(r.size);
    }
    auto position = [&](std::u32string_view set, std::size_t i) -> int {
        if (i >= rs.size()) return 0;
        const std::size_t k = set.find(rs[i]);
        return k == std::u32string_view::npos ? -1 : static_cast<int>(k);
    };
    constexpr std::u32string_view kDigits = U"○一二三四五六七八九";
    constexpr std::u32string_view kUnits = U"十百千万億";
    if (position(kUnits, 0) != -1) {
        if (const int p = position(kDigits, 1); p != -1) return std::string{'1', static_cast<char>(p + '0')};
        return "10";
    }
    std::string digits(1, static_cast<char>(std::max(position(kDigits, 0), 0) + '0'));
    if (rs.size() > 1 && position(kUnits, 1) != -1) {
        if (const int p = position(kDigits, 2); p != -1) digits += static_cast<char>(p + '0');
    }
    return digits;
}

// After a noun parent, the particle records (kind 2) that follow the first verb or
// adjective record move to the front (behind the leading plain-dictionary records).
Record* ReorderAfterNoun(const Item& parent, Record* records) {
    if (parent.EntryText(0) == kKoCloseQuote || parent.EntryText(0) == ")" || At(parent.Code(0), 0) != '1') {
        return records;
    }
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    bool reorder = false;
    Record* detached = nullptr;
    for (Record* p = first; p != nullptr; p = p->child) {
        const std::string_view part = p->partCode;
        if (At(part, 0) >= 'A' && At(part, 0) < 'G') break;
        if (const char k = At(part, 1); k == '4') {
            if (Has(p->attr, "Pj")) break;
            if (!reorder) {
                if (WidthOf(p->source) == 2) {
                    Record* next = p->child;
                    while (next != nullptr && (next->source != p->source || At(next->partCode, 1) != '2')) next = next->child;
                    reorder = next != nullptr;
                } else {
                    reorder = true;
                }
            }
        } else if (k > '6' && k <= '8') {
            if (Has(p->attr, "Pj")) break;
            reorder = true;
        }
        if (!reorder) continue;
        if (Record* c = p->child; c != nullptr && At(c->partCode, 1) == '2' && c->source != kJaNa) {
            detached = c;
            p->child = nullptr;
            break;
        }
    }
    if (!reorder || detached == nullptr) return records;
    Record* last = detached;
    while (last->child != nullptr) last = last->child;
    last->child = first;
    if (owned == nullptr) return detached;
    owned->child = detached;
    return records;
}

// Text that starts with a particle or punctuation, or is empty.
bool NounEnd(std::string_view text) {
    if (text.empty()) return true;
    const std::string_view c = FirstChar(text);
    return text::DecodeRune(c).cp != text::kReplacement && Has("にがをはもでとへの、。，．！？」』）…", c);
}

// Text path: the noun records of records move ahead of a verb whose continuative they
// spell, the stem and one kana of the i or e row (争い, not 争 + い), when no record is
// longer, the word before is a modifier a verb cannot follow (の, a word in な, この) and
// a particle or punctuation follows the noun. Plain-dictionary records stay in front.
Record* NounAfterModifier(const Item& parent, Record* records, std::string_view text) {
    const std::string_view p = parent.group->surface;
    if (parent.detail->Yougo() ||
        (p != "の" && !p.ends_with("な") && p != "この" && p != "その" && p != "あの" && p != "どの")) {
        return records;
    }
    Record* owned = nullptr;
    Record* first = records;
    while (first != nullptr && first->user != nullptr) {
        owned = first;
        first = first->child;
    }
    if (first == nullptr || At(first->partCode, 1) != '4') return records;
    std::string_view noun;
    for (Record* r = first; r != nullptr; r = r->child) {
        if (!r->source.starts_with(first->source)) continue;
        const std::string_view tail = r->source.substr(first->source.size());
        if (At(r->partCode, 0) == 'N' && text::RuneCount(tail) == 1 &&
            Has("いきしちにひみりぎじびぴえけせてねへめれげぜでべぺ", tail) && text.starts_with(r->source) &&
            NounEnd(text.substr(r->source.size()))) {
            noun = r->source;
            break;
        }
    }
    if (noun.empty()) return records;
    for (Record* r = first; r != nullptr; r = r->child) {
        if (r->source.size() > noun.size()) return records;
    }
    Record* nouns = nullptr;
    Record* rest = nullptr;
    Record** nounTail = &nouns;
    Record** restTail = &rest;
    for (Record* r = first; r != nullptr;) {
        Record* next = r->child;
        r->child = nullptr;
        if (At(r->partCode, 0) == 'N' && r->source == noun) {
            *nounTail = r;
            nounTail = &r->child;
        } else {
            *restTail = r;
            restTail = &r->child;
        }
        r = next;
    }
    *nounTail = rest;
    if (owned == nullptr) return nouns;
    owned->child = nouns;
    return records;
}

// Rejects some kind-1 records next to level-2 kanji and the characters after them in
// the code order (Shift-JIS lead byte 0xE0 or above); kinds 0, 1, 2, 3 and 9 are
// otherwise valid, others never.
bool ValidateCandidate(std::string_view source, int start, int token, const Record& record, const Item* previous) {
    const std::string_view surface = record.source;
    const std::string_view prefix = source.substr(0, static_cast<std::size_t>(start));
    if (surface.size() + static_cast<std::size_t>(start) > source.size()) return false;
    const std::string_view tail = From(source, start + static_cast<long>(surface.size()));
    switch (At(record.partCode, 1)) {
    case '0':
    case '2':
    case '3':
    case '9':
        return true;
    case '1':
        break;
    default:
        return false;
    }
    auto highLeadPair = [](std::string_view s) {
        const text::Rune r = text::DecodeRune(s);
        return r.size == s.size() && tokenizer::IsSjisHighLead(r.cp);
    };
    if (WidthOf(surface) != 2 || !highLeadPair(surface)) return true;
    const Window l = Right(prefix, 2), r = Left(tail, 2);
    if (prefix.ends_with("□") || tail.starts_with("□")) return false;
    auto hanja = [&](const Window& s) { return !s.broken && WidthOf(s.text) == 2 && highLeadPair(s.text); };
    if (!hanja(l) && !hanja(r) && !(l == "○")) return true;
    if (previous != nullptr && previous->tokenIndex == token && previous->start + previous->length >= start) return true;
    return !l.broken && WidthOf(l.text) == 2 && Has(kNumeralOrder, l.text);
}

// The canonical form of r cut to two width units; a form shorter than that ends the
// filtered text after it (cut).
std::string_view CanonicalPair(char32_t r, std::string& scratch, bool& cut) {
    scratch.clear();
    if (const auto form = tokenizer::CanonicalForm(r)) scratch.assign(*form);
    else text::AppendRune(scratch, r);
    std::size_t i = 0;
    int w = 0;
    while (i < scratch.size()) {
        const text::Rune c = text::DecodeRune(std::string_view(scratch).substr(i));
        if (w + Width(c.cp) > 2) break;
        w += Width(c.cp);
        i += c.size;
    }
    scratch.resize(i);
    cut = WidthOf(scratch) < 2;
    return scratch;
}

}  // namespace

// Rewrites a synthetic target: kanji become their Hangul reading (hanjaToHangul),
// hiragana, katakana and 、 are written in their canonical forms or dropped
// (deleteJapan; 、 and ・ are always written), other characters are copied.
std::string Worker::FilterKana(std::string_view text) const {
    std::string out, scratch;
    while (!text.empty()) {
        const text::Rune d = text::DecodeRune(text);
        text.remove_prefix(d.size);
        char32_t r = d.cp;
        if (r < 0x80) {
            out += static_cast<char>(r);
            continue;
        }
        const int typ = tokenizer::Class(r);
        if (typ == tokenizer::kTypeJapanese && settings.hanjaToHangul == 1) {
            if (const auto reading = dict::Written(r)) r = *reading;
            text::AppendRune(out, r);
        } else if (typ == tokenizer::kTypeJapanese ||
                   (typ != tokenizer::kClassHiragana && typ != tokenizer::kClassKatakana && r != U'、')) {
            text::AppendRune(out, r);
        } else if (settings.deleteJapan != 1 || r == U'・' || r == U'、') {
            bool cut = false;
            out += CanonicalPair(r, scratch, cut);
            if (cut) return out;
        }
    }
    return out;
}

bool Worker::Transliterate(std::string_view s, std::string& out) {
    if (!settings.text) return TransliterateKatakana(s, out);
    if (std::string kr = snapshot.PersonName(s); !kr.empty()) {
        out = std::move(kr);
        return true;
    }
    if (std::string kr = text::FromKana(s, text::Preset::Fan); !kr.empty() && s.ends_with("ッ") && text::RuneCount(s) <= 6) {
        out = std::move(kr);
        return true;
    }
    return TransliterateName(s, out);
}

bool Worker::Dispatch(int i, int& last) {
    current = i;
    last = i;
    const std::string_view merged = tokens.Merged(static_cast<std::size_t>(i));
    const int type = tokens.TypeAfter(static_cast<std::size_t>(i));
    const std::string_view left = tokens.Canonical(static_cast<std::size_t>(i));
    switch (type) {
    case 1: case 15: case 2: case 16:
        BuildTokenTransformText(i);
        break;
    case 4: case 5: case 8:
        if (left == "(" || left == ")" || Has(left, "%")) BuildTokenTransformText(i);
        break;
    case 24:
        if (Classify(left) == 13 && WidthOf(left) == 2 && AnchorAround(i)) BuildTokenTransformText(i);
        else BuildSurfaceTransform(i, left);
        if (const Item* tail = chain.tail) {
            for (int e = 0; e < static_cast<int>(tail->group->entries.size()); e++) {
                if (Has(tail->GAttr(e), "Wa") && i + 1 < TokenCount() && Tok(i + 1).type == tokenizer::kTypeWideSpace) {
                    spaced[static_cast<std::size_t>(i + 1)] = 1;
                    last = i + 1;
                    break;
                }
            }
        }
        break;
    case 18: case 20: case 22:
        if (left == "(" || left == ")" || left == "?" || left == "%") BuildTokenTransformText(i);
        break;
    case tokenizer::kTypeProtected:
        Insert(ProtectedItem(i));
        break;
    case tokenizer::kTypeMarkup:
        return Span(i) == 0 && ((HasPrefixFold(merged, "<LI>") && merged.size() == 4) ||
                                (HasPrefixFold(merged, "<BR>") && merged.size() == 4));
    case 3: case 6: case 9: case 17: case 23: case 26: case 78:
        if (left == "(" || left == ")" || left == "?") BuildTokenTransformText(i);
        return true;
    }
    return false;
}

// Token i sits between "<A..." and "</A>" tokens (not spans).
bool Worker::AnchorAround(int i) const {
    return i - 1 >= 0 && Span(i - 1) == 0 && HasPrefixFold(Tok(i - 1).text, "<A") && TokenCount() > i + 1 &&
           Span(i + 1) == 0 && HasPrefixFold(Tok(i + 1).text, "</A>");
}

void Worker::Insert(Item* it) {
    if (it != nullptr) insertedLength += chain.Insert(it, true);
}

void Worker::BuildTokenTransformText(int i) {
    const std::string_view surface = Tok(i).text;
    Record* record = CreateSyntheticRecord(i, surface, false);
    Insert(InsertLateCandidate(surface, 0, i, record));
}

void Worker::BuildSyntheticTransform(std::string_view surface, int token, int offset, bool emptyTarget) {
    Record* record = CreateSyntheticRecord(token, surface, emptyTarget);
    Item* item = InsertLateCandidate(transformSurface, offset, token, record);
    if (item == nullptr) {
        // The validity rules reject a synthetic word when a longer dictionary word starts
        // here, even when that longer word did not fit the sentence. The span still needs
        // an item: without one the compose stage writes its source text as it is.
        item = NewItem(NewDetail(kDetailInfo, token), NewGroup(record, ' '), transformSurface, offset, token);
    }
    Insert(item);
}

// A record for text the dictionary does not know: kana and kanji get a transliterated
// target, everything else is its own target.
Record* Worker::CreateSyntheticRecord(int token, std::string_view source, bool emptyTarget) {
    Record* r = NewRecord();
    r->source = source;
    const int typ = Classify(source);
    bool symbol = false;
    if (typ != 14 && typ != 24 && typ != 13) {
        if (WidthOf(source) == 1 && !IsDigit(source[0])) symbol = !IsAlpha(source[0]);
        r->target.assign(source);
    } else if (!emptyTarget) {
        std::string target;
        if (typ == 14) {
            if (!Transliterate(source, target)) fault = true;
            if (target == source) target.clear();
            if (Classify(target) != 0) target = FilterKana(target);
        } else if (typ == 13 && WidthOf(source) == 2 && AnchorAround(token)) {
            target = TransliterateHiragana(source);
            if (target.empty()) target = FilterKana(source);
        } else {
            target = FilterKana(source);
        }
        r->target = std::move(target);
    }
    int category = 0;
    if (typ == 24 || typ == 14 || typ == 13) category = typ;
    else if (IsDigit(At(source, 0))) category = 1;
    else if (IsAlpha(At(source, 0))) category = 2;
    if (category != 0 && symbol) r->partCode = "N600";
    else if (category != 0) r->partCode = "N110";
    else if (source == "(" || source == ")" || source == "（" || source == "）") r->partCode = "N600";
    else r->partCode = "N110";
    r->attr = category == 1 ? "Nd" : "Nm";
    if (category == 24 && WidthOf(source) == 2) r->attr += "On";
    return r;
}

// The dictionary record whose source is exactly source (with the records after it), or
// a numeral or punctuation record for it.
Record* Worker::LookupFallbackRecord(int token, std::string_view source, int left, int right) {
    if (const Lookup l = DoLookup(source, kModeGeneral); l.count != 0) {
        for (Record* r = l.records; r != nullptr; r = r->child) {
            if (r->source == source) return r;
        }
    }
    std::string attrs = "N";
    char second = 0;
    switch (left) {
    case 12: case 24: second = 'j'; break;
    case 13: second = 'h'; break;
    case 14: second = 'k'; break;
    case 15: second = 'd'; break;
    case 50: second = 'p'; break;
    }
    if (second != 0) {
        attrs += second;
        if (right == 15 && left != 12) attrs += 'd';
    }
    Record* r = NewRecord();
    r->source = source;
    r->target.assign(source);
    if (left == 12 && right == 15) r->target = EncodeNumericPair(source);
    switch (tokens.TypeAfter(static_cast<std::size_t>(token))) {
    case 1: case 2: case 15: case 16: case 24:
        r->partCode = source == "'" || left == 50 || right == 50 ? "N600" : "N110";
        break;
    default:
        r->partCode = "NA00";
    }
    r->attr = attrs;
    if ((left == 24 || right == 24) && WidthOf(source) == 2) r->attr += "On";
    return r;
}

// A numeral (kanji or ASCII digits) or an ASCII punctuation character at source and
// its record, or null.
Record* Worker::ProbeNumericPrefix(int token, std::string_view source) {
    if (source.empty()) return nullptr;
    std::size_t prefix = 0;
    int left = 0, right = 0;
    // The character after n bytes when it is not ASCII.
    auto next = [&](std::size_t n) {
        const std::string_view rest = source.substr(n);
        return !rest.empty() && static_cast<unsigned char>(rest[0]) >= 0x80 ? FirstChar(rest) : std::string_view();
    };
    switch (Classify(source)) {
    case 24: {
        const std::string_view c = FirstChar(source);
        if (IsNumeral(c)) {
            left = 12, right = 15;
            prefix = c.size();
            if (const std::string_view c2 = next(c.size()); !IsNumeral(c2) && c2 == "十") {
                prefix += c2.size();
                if (IsNumeral(next(prefix))) prefix += next(prefix).size();
            }
        } else if (c == "十") {
            left = 12, right = 15;
            prefix = c.size();
            if (const std::string_view c2 = next(c.size()); IsNumeral(c2)) prefix += c2.size();
        }
        if (left == 0 && right == 0) return nullptr;
        break;
    }
    case 14:
        return nullptr;
    default: {
        const char c = source[0];
        if (static_cast<unsigned char>(c) < 0x80 && IsDigit(c)) {
            left = 15;
            std::size_t n = 1;
            while (n < source.size() && static_cast<unsigned char>(source[n]) < 0x80 &&
                   (IsDigit(source[n]) || source[n] == ',' || source[n] == '.')) {
                n++;
            }
            prefix = n;
        } else {
            if (static_cast<unsigned char>(c) >= 0x80 || !IsPunct(c)) return nullptr;
            left = 50;
            prefix = 1;
        }
    }
    }
    return LookupFallbackRecord(token, source.substr(0, prefix), left, right);
}

// Splits a Japanese token into dictionary words, inserting each into the chain;
// unknown characters become synthetic words.
void Worker::BuildSurfaceTransform(int token, std::string_view surface) {
    const std::string_view full = surface;
    transformSurface = surface;
    int cursor = 0;
    const int size = static_cast<int>(full.size());
    while (cursor < size) {
        insertedLength = 0;
        candidateRetryCount = 0;
        const bool translated = TranslateCandidateAt(full, cursor, token, nullptr);
        cursor += insertedLength;
        if (translated && insertedLength != 0) continue;
        if (cursor >= size) break;
        const std::string_view rest = full.substr(static_cast<std::size_t>(cursor));
        if (Record* numeric = ProbeNumericPrefix(token, rest)) {
            if (const Lookup l = DoLookup(rest, kModeGeneral); l.count != 0) {
                Record* cur = l.records;
                while (cur != nullptr && cur->source.size() <= numeric->source.size()) cur = cur->child;
                if (cur != nullptr) {
                    ProcessCandidateRecord(full, cursor, token, cur, nullptr);
                    return;
                }
            }
            if (Item* candidate = InsertLateCandidate(full, cursor, token, numeric)) {
                insertedLength = chain.Insert(candidate, true);
                cursor += insertedLength;
            } else {
                cursor += static_cast<int>(FirstChar(rest).size());
            }
            continue;
        }
        const char lead = rest[0];
        if (static_cast<unsigned char>(lead) >= 0x80) {
            const std::string_view c = FirstChar(rest);
            BuildSyntheticTransform(c, token, cursor, FindHonorificPrefix(c) != -1);
            cursor += static_cast<int>(c.size());
            continue;
        }
        const int start = cursor;
        if (IsAlpha(lead)) {
            while (cursor < size && (IsAlpha(full[static_cast<std::size_t>(cursor)]) || IsDigit(full[static_cast<std::size_t>(cursor)]))) {
                cursor++;
            }
        } else {
            cursor++;
        }
        // The offset is taken after the run.
        BuildSyntheticTransform(full.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(cursor - start)), token,
                                cursor, false);
    }
}

// Looks up the words at full[cursor:] and processes them; parent is the word before,
// whose candidates may be reordered.
bool Worker::TranslateCandidateAt(std::string_view full, int cursor, int token, Item* parent) {
    const std::string_view text = From(full, cursor);
    if (text.empty()) return true;
    if (Item* hon = HonorificItem(full, cursor, token, parent)) {
        TranslateCandidateAt(full, cursor + hon->length, token, hon);
        Insert(hon);
        return true;
    }
    Lookup found;
    if (Record* numeric = ProbeNumericPrefix(token, text)) {
        const Lookup dictionary = DoLookup(text, kModeGeneral);
        if (dictionary.count == 0) return ProcessCandidateRecord(full, cursor, token, numeric, parent);
        found = dictionary;
    } else {
        found = DoLookup(text, kModeGeneral);
    }
    if (found.count != 0) return ProcessCandidateRecord(full, cursor, token, found.records, parent);
    std::string_view surface = FirstChar(text);
    const bool emptyTarget = FindHonorificPrefix(surface) != -1;
    if (!emptyTarget) {
        if (const int typ = Classify(text); typ != 24 && typ != 14) {
            const char c = text[0];
            if (static_cast<unsigned char>(c) < 0x80 && (IsDigit(c) || IsAlpha(c))) surface = AsciiRun(text);
            else if (static_cast<unsigned char>(c) < 0x80) surface = text.substr(0, 1);
            else return false;
        }
    }
    return ProcessCandidateRecord(full, cursor, token, CreateSyntheticRecord(token, surface, emptyTarget), parent);
}

// Makes an item from records at full[cursor:], then translates the rest of the token
// after it; when that fails it retries with the shorter candidates.
bool Worker::ProcessCandidateRecord(std::string_view full, int cursor, int token, Record* records, Item* parent) {
    if (parent != nullptr) {
        records = ReorderAfterNoun(*parent, records);
        if (settings.text) records = NounAfterModifier(*parent, records, From(full, cursor));
    }
    const std::string_view text = From(full, cursor);
    Item* candidate = InsertLateCandidate(full, cursor, token, records);
    if (candidate == nullptr) {
        const std::string_view pair = PairText(text);
        if (FindHonorificPrefix(pair) != -1) {
            BuildSyntheticTransform(pair, token, cursor, true);
            return true;
        }
        const int typ = Classify(text);
        if (typ == 24 || typ == 14) {
            BuildSyntheticTransform(pair, token, cursor, false);
            return true;
        }
        if (const char c = text[0]; static_cast<unsigned char>(c) < 0x80 && (IsDigit(c) || IsAlpha(c)) && typ == 0) {
            BuildSyntheticTransform(AsciiRun(text), token, cursor, false);
            return true;
        }
        if (candidateRetryCount >= 3) {
            BuildSyntheticTransform(pair, token, cursor, false);
            return true;
        }
        return false;
    }
    const int savedRetries = candidateRetryCount;
    if (settings.improvements.perLevelRetries) candidateRetryCount = 0;
    const bool translated = TranslateCandidateAt(full, cursor + candidate->length, token, candidate);
    if (settings.improvements.perLevelRetries) candidateRetryCount = savedRetries;
    if (translated) {
        Insert(candidate);
        return true;
    }
    const std::string_view source = candidate->group->surface;
    const int typ = Classify(source);
    const char part = At(candidate->Code(0), 0);
    if (part == '6' || ((typ == 24 || typ == 14) && part != '4' && part != '7' && part != '8') ||
        IsDigit(At(source, 0)) || IsAlpha(At(source, 0)) || StartsWithSymbol(source) || candidateRetryCount >= 3) {
        Insert(candidate);
        return true;
    }
    if (Record* remaining = candidate->group->records) {
        candidateRetryCount++;
        candidate->group->records = nullptr;
        while (remaining != nullptr && remaining->source.empty()) remaining = remaining->child;
        if (remaining != nullptr) return ProcessCandidateRecord(full, cursor, token, remaining, candidate);
        return false;
    }
    if (Has(candidate->GAttr(0), "Nm")) {
        Insert(candidate);
        return true;
    }
    candidateRetryCount++;
    return false;
}

// The item of a word span token: one fixed noun entry (code 110, no attributes, no
// local rule) whose target is the span's stand-in.
Item* Worker::ProtectedItem(int token) {
    const std::string_view text = tokens.Canonical(static_cast<std::size_t>(token));
    Entry* e = NewEntry();
    e->dst.assign(output::StandIn(text));
    e->code = "110";
    e->cls = 'N';
    e->ruleId = -1;
    e->fixed = true;
    Group* g = NewGroup(nullptr, ' ');
    g->surface.assign(text);
    g->length = static_cast<int>(text.size());
    g->entries.push_back(e);
    return NewItem(NewDetail(kDetailInfo, token), g, text, 0, token);
}

// The item of an honorific at full[cursor:] when the word before it, parent or (at the
// start of a token) the last item of the chain, is a name item that ends at the cursor;
// otherwise null. The item is a fixed noun (code 110) with the grammar code Tu
// (attached) or Ha (spaced).
Item* Worker::HonorificItem(std::string_view full, int cursor, int token, Item* parent) {
    struct Honorific {
        std::string_view jp, kr;
        bool space;
    };
    static constexpr Honorific kHonorifics[] = {
        {"さん", "씨", false}, {"ちゃん", "짱", false}, {"くん", "군", false},     {"君", "군", false},
        {"様", "님", false},   {"さま", "님", false},   {"氏", "씨", false},       {"殿", "님", false},
        {"たん", "짱", false}, {"先輩", "선배", true},  {"先生", "선생님", true},
    };
    // Words that start with an honorific: after a name they are the title.
    static constexpr std::string_view kTitleWords[] = {"殿下", "殿方", "様子", "様々", "君主", "君臨", "氏族"};
    // The Korean endings after which a following honorific is written as nothing.
    static constexpr std::string_view kTitleEnds[] = {"님", "씨", "군", "양", "짱", "선배"};
    Item* name = parent;
    if (const Item* tail = chain.tail; name == nullptr && tail != nullptr) {
        if ((tail->tokenIndex == token && tail->start + tail->length == cursor) ||
            (cursor == 0 && tail->tokenIndex == token - 1 &&
             tokens[static_cast<std::size_t>(tail->tokenIndex)].type == tokenizer::kTypeProtected)) {
            name = chain.tail;
        }
    }
    if (name == nullptr || !name->IsNameItem()) return nullptr;
    const std::string_view text = From(full, cursor);
    bool titleWord = false;
    for (const std::string_view t : kTitleWords) {
        if (text.starts_with(t)) titleWord = true;
    }
    std::string_view key, kr;
    bool space = false;
    for (const Honorific& h : kHonorifics) {
        if (h.jp.empty() || h.jp.size() <= key.size() || !text.starts_with(h.jp) || titleWord) continue;
        key = h.jp, kr = h.kr, space = h.space;
    }
    if (lookupContext.plain != nullptr) {
        if (const dict::UserEntry* e = lookupContext.plain->Honorific(text); e != nullptr && e->key.size() >= key.size()) {
            key = e->key, kr = e->target, space = e->codes->gram.Contains("Ha");
        }
    }
    if (key.empty()) return nullptr;
    const std::string_view nameText = name->group->entries[0]->dst;
    for (const std::string_view end : kTitleEnds) {
        if (nameText.ends_with(end)) kr = {};
    }
    Entry* e = NewEntry();
    e->dst.assign(kr);
    e->code = "110";
    e->attr = "VHn";
    e->gattr = space ? "Ha" : "Tu";
    e->cls = 'N';
    e->ruleId = -1;
    e->fixed = true;
    Group* g = NewGroup(nullptr, ' ');
    g->surface.assign(key);
    g->length = static_cast<int>(key.size());
    g->entries.push_back(e);
    return NewItem(NewDetail(kDetailInfo, token), g, full, cursor, token);
}

// An item from the first usable record group of records at text[offset:], running the
// conjugation checks of its kind: verbs ('4'), adjectives ('7'), adjectival nouns
// ('8'), words without ending ('6') and the rest. Null when no record group passes.
Item* Worker::InsertLateCandidate(std::string_view text, int offset, int token, Record* records) {
    Record* rec = records;
    while (rec != nullptr) {
        if (rec->source.empty() || rec->partCode.size() != 4 || rec->partCode[1] == '5') {
            rec = rec->child;
            continue;
        }
        const std::size_t length = rec->source.size();
        const char kind = rec->partCode[1];
        auto sameGroup = [&](const Record* r) { return r->source.size() == length && At(r->partCode, 1) == kind; };
        switch (kind) {
        case '4': {
            Detail* word = NewDetail(kDetailVerb, token);
            const int pattern = rec->partCode[3] - '0';
            word->patternKind = pattern;
            Record* stop = nullptr;
            const bool ok = MatchConjugation(*word, text, offset, rec, stop);
            rec = stop;
            if (!ok) {
                while (rec != nullptr && sameGroup(rec) && At(rec->partCode, 3) - '0' == pattern) {
                    rec = rec->child;
                }
            } else {
                Group* group = NewGroup(rec, static_cast<char>(word->kind + '0'));
                Item* candidate = NewItem(word, group, text, offset, token);
                bool accepted = candidate->AcceptVerbContinuation(group->records);
                if (accepted && candidate->group->surface == kJaShi && candidate->EntryText(0) == "&HT@" &&
                    candidate->DirectSurface() == PrefixGate(14) &&
                    !From(text, offset + candidate->length).starts_with(PrefixGate(14))) {
                    accepted = false;
                }
                if (accepted) {
                    switch (word->record) {
                    case 1: candidate->AppendRuleCandidateMarker("Pi"); break;
                    case 2: candidate->AppendRuleCandidateMarker("Sa"); break;
                    case 4: candidate->AppendRuleCandidateMarker("Ka"); break;
                    }
                    return candidate;
                }
                // A く verb of the same stem reads the て or た that the stem in い of a わ-row
                // verb cannot take (彷徨いて: 彷徨く in て, not 彷徨う + て).
                Record* other = stop->child;
                while (other != nullptr && other->source == stop->source && At(other->partCode, 1) == '4' &&
                       At(other->partCode, 3) == At(stop->partCode, 3)) {
                    other = other->child;
                }
                const std::string_view afterStem = From(text, offset + static_cast<long>(stop->source.size()));
                if (word->patternKind == 0 && word->kind == 10 && afterStem.starts_with("い") &&
                    TeAfterWaStem(stop->source, afterStem.substr(std::string_view("い").size())) && other != nullptr &&
                    other->source == stop->source && At(other->partCode, 1) == '4') {
                    rec = other;
                    group->records = nullptr;
                    continue;
                }
                rec = group->records;
                group->records = nullptr;
            }
            if (rec == nullptr || At(rec->partCode, 1) != '4') {
                if (Item* candidate = QueryPrefixTable(text, offset, token)) {
                    candidate->group->records = rec;
                    return candidate;
                }
            }
            break;
        }
        case '6': {
            Detail* word = NewDetail(kDetailInfo, token);
            return NewItem(word, NewGroup(rec, ' '), text, offset, token);
        }
        case '7':
        case '8': {
            Detail* word = NewDetail(kind == '8' ? kDetailDakeiyo : kDetailKeiyo, token);
            const std::string_view suffix = From(text, offset + static_cast<long>(length));
            word->kind = '0';
            if (suffix.starts_with(kJaSugiKanji) || suffix.starts_with(kJaSugi) || suffix.starts_with(kJaSaSugiKanji) ||
                suffix.starts_with(kJaSaSugi)) {
                word->kind = kind == '8' ? 'X' : 'x';
            }
            Group* group = NewGroup(rec, ' ');
            Item* candidate = NewItem(word, group, text, offset, token);
            if ((kind == '7' && candidate->RunKeiyoTrieQuery()) || (kind == '8' && candidate->RunDakeiyoCase8Query())) {
                return candidate;
            }
            rec = group->records;
            group->records = nullptr;
            break;
        }
        default: {
            Detail* word = NewDetail(kDetailInfo, token);
            if (!ValidateCandidate(text, offset, token, *rec, chain.tail)) {
                while (rec != nullptr && sameGroup(rec)) rec = rec->child;
                continue;
            }
            Group* group = NewGroup(rec, ' ');
            Item* candidate = NewItem(word, group, text, offset, token);
            if (candidate->RunDefaultTrieQuery()) return candidate;
            rec = group->records;
            group->records = nullptr;
        }
        }
    }
    return nullptr;
}

namespace {

// Rejects some prefix-table matches by what follows them; it may move index to branch
// 8 or 10.
bool CheckPrefixTableGate(int group, int& index, std::string_view token, std::string_view sourceTail) {
    auto g = [](int i) { return PrefixGate(i); };
    bool rejected = false;
    if (group == 1 && index == 6 && WidthOf(sourceTail) >= 4) {
        if (const Window last = Right(sourceTail, 4); last == g(11) || last == g(12) || last == g(13)) rejected = true;
    }
    const Window two = Cut(token, 2, 2);
    if (!two.Empty()) {
        if (rejected) return true;
        if ((group == 1 || group == 2) && index == 8) {
            const Window four = Cut(token, 2, 4), six = Cut(token, 2, 6), eight = Cut(token, 2, 8);
            if (!(two == g(15))) {
                if (two == g(14)) {
                    index = 8;
                    return false;
                }
                if (two == g(16) || two == g(17) || six == g(18) || six == g(19) || six == g(20) || four == g(21) ||
                    four == g(22) || two == g(23) || four == g(24) || two == g(25) || four == g(26) || four == g(27) ||
                    eight == g(28) || four == g(29) || four == g(30) || eight == g(31) || four == g(32)) {
                    index = 10;
                    return false;
                }
                if (!(Left(token, 2) == kJaShi)) rejected = true;
            }
        }
    }
    if (!rejected && (group == 1 || group == 2) && index == 10 && !(two == ",") && !(two == g(33)) && !(two == g(16)) &&
        !(two == g(17))) {
        rejected = true;
    }
    return rejected;
}

}  // namespace

// A conjugated する, くる or 来る at text[offset:] as a verb item.
Item* Worker::QueryPrefixTable(std::string_view text, int offset, int token) {
    if (offset > static_cast<int>(text.size())) return nullptr;
    const std::string_view tail = From(text, offset);
    // The n width units after byte off of tail, "" when fewer are left.
    auto tailAt = [&](std::size_t off, int n) {
        const std::string_view rest = tail.substr(off);
        return WidthOf(rest) >= n ? Left(rest, n) : Window{};
    };
    for (int tableGroup = 1; tableGroup < 4; tableGroup++) {
        for (int entry = 0; entry < kConjugationBranches; entry++) {
            const std::string_view prefix = PrefixForm(50 * tableGroup + entry);
            const bool contractedConditional = tableGroup == 1 && entry == 3 && tail.starts_with("すりゃ");
            if (prefix.empty() || (!tail.starts_with(prefix) && !contractedConditional)) continue;
            const std::size_t pl = prefix.size();
            if (tableGroup == 1) {
                if (entry == 6) {
                    const Window two = tailAt(pl, 2), six = tailAt(pl, 6);
                    if (two == PrefixGate(0) || six == PrefixGate(1) || two == PrefixGate(2) || six == PrefixGate(3) ||
                        two == PrefixGate(4) || six == kJaOwaru) {
                        continue;
                    }
                } else if (entry == 8 || entry == 10) {
                    if (tailAt(0, 6) == PrefixForm(67)) entry = 17;
                    else if (tailAt(0, 4) == PrefixForm(61)) entry = 11;
                }
            } else if (tableGroup == 3 && entry == 6) {
                // As for する: 来た before い, かった, く, ければ or き is the 来たい family, not the past.
                const Window two = tailAt(pl, 2), six = tailAt(pl, 6);
                if (two == PrefixGate(0) || six == PrefixGate(1) || two == PrefixGate(2) || six == PrefixGate(3) ||
                    two == PrefixGate(4)) {
                    continue;
                }
            } else if (tableGroup == 3 && (entry == 8 || entry == 10) && tail.starts_with(PrefixForm(50 * 3 + 17))) {
                entry = 17;  // as for する: 来ちゃ is one form, not 来 and ちゃ
            } else if (tableGroup == 3 && (entry == 8 || entry == 10)) {
                const Window two = tailAt(pl, 2), four = tailAt(pl, 4), eight = tailAt(pl, 8), ten = tailAt(pl, 10);
                if (two == PrefixGate(0) || two == PrefixGate(5) || four == PrefixGate(6) || four == PrefixGate(7) ||
                    eight == PrefixGate(8) || eight == PrefixGate(9) || ten == PrefixGate(10)) {
                    continue;
                }
            }
            int index = entry;
            if (CheckPrefixTableGate(tableGroup, index, tail, text.substr(0, static_cast<std::size_t>(offset)))) continue;
            const Lookup found = DoLookup(PrefixForm(50 * tableGroup), kModePrefixVerb);
            if (found.count == 0) continue;
            Detail* verb = NewDetail(kDetailVerb, token);
            static constexpr int kKinds[] = {0, 9, 11, 13};
            verb->patternKind = kKinds[tableGroup];
            verb->patternMatched = 1;
            verb->patternLength = static_cast<int>(PrefixForm(50 * tableGroup + entry).size());
            Group* group = NewGroup(found.records, static_cast<char>(verb->kind + '0'));
            Item* candidate = NewItem(verb, group, text, offset, token);
            verb->kind = index;
            if (candidate->AcceptVerbContinuation(group->records)) return candidate;
        }
    }
    return nullptr;
}

}  // namespace hdTrans::word
