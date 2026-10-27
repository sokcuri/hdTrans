#include "core/word/constructions.h"

#include <algorithm>
#include <span>
#include <string>
#include <unordered_map>

#include "core/dict/builtin.h"
#include "core/dict/classes.h"
#include "core/dict/codes.h"
#include "core/output/compose.h"
#include "core/output/korean.h"
#include "core/output/particle.h"
#include "core/output/pieces.h"
#include "core/output/inflect.h"
#include "core/rules/match.h"
#include "core/text/kana.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/tokenizer/classify.h"
#include "core/tokenizer/token.h"
#include "core/word/flush.h"
#include "core/word/util.h"

namespace hdTrans::word {

namespace internal {

namespace {

// Whether the word is only marks that may stand between copies (pause.copy: 、).
bool CopyPause(const Item* it) {
    const std::string_view text = Slice(it->text, it->start, it->length);
    for (std::string_view t = text; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (!dict::InClassRune(HDTRANS_CLASS("pause.copy"), r.cp)) return false;
        t.remove_prefix(r.size);
    }
    return !text.empty();
}

// Whether the word is only the clipping っ or ッ of mark.clip (だめ|っ|だめっ).
bool ClipWord(const Item* it) {
    const std::string_view text = Slice(it->text, it->start, it->length);
    for (std::string_view t = text; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (!dict::InClassRune(HDTRANS_CLASS("mark.clip"), r.cp)) return false;
        t.remove_prefix(r.size);
    }
    return !text.empty();
}

// Whether text holds a long mark of mark.long-vowel after its first character (すごーい).
bool LongMarked(std::string_view text) {
    for (std::string_view t = text.substr(text::DecodeRune(text).size); !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        if (dict::InClassRune(HDTRANS_CLASS("mark.long-vowel"), r.cp)) return true;
        t.remove_prefix(std::max<std::size_t>(r.size, 1));
    }
    return false;
}

// text without the long marks of mark.long-vowel after its first character (すごーい すごい).
std::string Unlengthened(std::string_view text) {
    std::string out;
    for (std::string_view t = text; !t.empty();) {
        const text::Rune r = text::DecodeRune(t);
        const std::string_view c = t.substr(0, std::max<std::size_t>(r.size, 1));
        if (out.empty() || !dict::InClassRune(HDTRANS_CLASS("mark.long-vowel"), r.cp)) out.append(c);
        t.remove_prefix(c.size());
    }
    return out;
}

// Whether a ends its token and the one token between it and the token of b is a run of the
// closing marks of the sound stage (output::IsClosingMark) or of mark.stretch, as after a drawn
// copy (痛いぃ、痛いぃ, 痛いー、痛いー).
bool MarksBetween(const Item* a, const Item* b) {
    if (a->tokenIndex + 2 != b->tokenIndex || !From(a->text, a->start + a->length).empty()) return false;
    const std::string_view t = a->detail->nextText;
    if (t.empty()) return false;
    for (std::string_view r = t; !r.empty();) {
        const text::Rune c = text::DecodeRune(r);
        if (!output::IsClosingMark(c.cp) && !dict::InClassRune(HDTRANS_CLASS("mark.stretch"), c.cp)) return false;
        r.remove_prefix(std::max<std::size_t>(c.size, 1));
    }
    return true;
}

// Whether b goes on the token of a, or comes right after the closing marks after a.
bool SameOrMarked(const Item* a, const Item* b) { return a->tokenIndex == b->tokenIndex || MarksBetween(a, b); }

// The word before it that it repeats: the same text with the same ending, of two
// characters or more, in the same token with nothing, a CopyPause word, or the ClipWord of
// the copy before it and maybe a CopyPause word after that between (いや|いや, 待って、待って,
// だめっ、だめっ); a copy drawn out with long marks of mark.long-vowel is the same (すごい|すごーい),
// and the closing marks after a drawn copy may stand between (痛いぃ、痛いぃ).
const Item* CopyBefore(const Item* it) {
    const Item* p = it->prev;
    const Item* at = it;
    if (p != nullptr && p->tokenIndex == it->tokenIndex && CopyPause(p)) {
        at = p;
        p = p->prev;
    }
    if (p != nullptr && SameOrMarked(p, at) && ClipWord(p)) {
        at = p;
        p = p->prev;
    }
    if (p == nullptr || !SameOrMarked(p, at)) return nullptr;
    const std::string_view text = Slice(it->text, it->start, it->length), before = Slice(p->text, p->start, p->length);
    if (text::RuneCount(text) < 2) return nullptr;
    if (before != text && (!(LongMarked(text) || LongMarked(before)) || Unlengthened(before) != Unlengthened(text))) return nullptr;
    return p;
}

}  // namespace

// The word after it that repeats it.
const Item* CopyAfter(const Item* it) {
    const Item* n = it->next;
    if (n != nullptr && SameOrMarked(it, n) && ClipWord(n)) n = n->next;
    if (n != nullptr && SameOrMarked(it, n) && CopyPause(n)) n = n->next;
    return n != nullptr && CopyBefore(n) != nullptr ? n : nullptr;
}

namespace {

// The last copy of the run that it starts or goes on.
const Item* LastCopy(const Item* it) {
    const Item* last = it;
    while (const Item* n = CopyAfter(last)) last = n;
    return last;
}

// Whether the copy after it follows it with no pause between, or only its clipping っ
// (いやだいやだ, だめっだめっ, not 好き、好き).
bool CopiedStraight(const Item* it) {
    const Item* n = CopyAfter(it);
    return n != nullptr && (it->next == n || (ClipWord(it->next) && it->next->next == n));
}

// Whether the copies of its run are the last predicate of their sentence: no word after the
// last copy conjugates and writes something, except one that modifies the noun after it
// (見て見て可愛いお菓子).
bool CopiesEnd(const Item* it) {
    for (const Item* n = LastCopy(it)->next; n != nullptr; n = n->next) {
        const Item* after = n->next;
        const bool modifies = after != nullptr && after->selected >= 0 && At(after->Code(after->selected), 0) == '1' &&
                              tokenizer::Class(text::DecodeRune(after->group->surface).cp) != 0;
        if (n->detail->Yougo() && n->selected >= 0 && !n->EntryText(n->selected).empty() && !modifies) return false;
    }
    return true;
}

// Whether the last copy of its run ends in a connective of form 6 (歩いて 걸어, 嬉しくて 기뻐서;
// not an imperative of the ; class, 寝ろ 자라).
bool ConnectiveCopies(const Item* it) {
    const Item* last = LastCopy(it);
    const int from = last->DirectIndex();
    return it->detail->Yougo() && from >= 0 && last->DirectText(from).starts_with("6") &&
           last->DirectCode(from).find(';') == std::string_view::npos;
}

// The first copy of the run that it ends or goes on.
const Item* FirstCopy(const Item* it) {
    const Item* first = it;
    while (const Item* p = CopyBefore(first)) first = p;
    return first;
}

// Whether it is a copy of a run of an adjective or an adjectival noun said again whose last
// copy ends bare in い or だ, read 1다 or the exclamative 6 (痛い痛い, いやだいやだ, すごいすごい).
bool PredicateCopies(const Item* it) {
    if (CopyBefore(it) == nullptr && CopyAfter(it) == nullptr) return false;
    if (it->detail->cls != kDetailKeiyo && it->detail->cls != kDetailDakeiyo) return false;
    const Item* last = LastCopy(it);
    const int from = last->DirectIndex();
    if (from < 0) return false;
    // A drawn-out ending is the bare one (すごーい).
    const std::string_view ending = last->DirectSurface();
    const bool drawn = !ending.empty() && dict::InClassRune(HDTRANS_CLASS("kana.drawn-out"), text::DecodeRune(ending).cp);
    const std::string_view plain = drawn ? ending.substr(text::DecodeRune(ending).size) : ending;
    return dict::InClass(HDTRANS_CLASS("ending.copies-bare"), plain) && dict::InClass(HDTRANS_CLASS("ending.copies-plain"), last->DirectText(from));
}

// The text right after the word: the rest of its token, or the text after the token.
std::string_view TextAfter(const Item* it) {
    const std::string_view rest = From(it->text, it->start + it->length);
    return rest.empty() ? it->detail->nextText : rest;
}

// Whether a clipping っ or ッ of mark.clip, or the clipped cry mark of a drawn word that took
// one, follows the word right after it (痛いっ, 熱いぃっ).
bool Clipped(const Item* it) {
    const std::string_view rest = TextAfter(it);
    return dict::InClassRune(HDTRANS_CLASS("mark.clip"), text::DecodeRune(rest).cp) ||
           text::DecodeRune(rest).cp == output::kClippedCryMark;
}

// Whether its run of copies is a cry whatever the words: three copies or more, or a mark of
// mark.cry right after the last copy (寒い寒い寒い, 寒い寒いっ, 寒い寒い！, 寒い寒い……).
bool CriedCopies(const Item* it) {
    int copies = 1;
    for (const Item* c = FirstCopy(it); (c = CopyAfter(c)) != nullptr;) copies++;
    if (copies >= 3) return true;
    const std::string_view rest = TextAfter(LastCopy(it));
    const std::string_view mark = rest.substr(0, text::DecodeRune(rest).size);
    const char32_t cp = text::DecodeRune(rest).cp;
    return dict::InClass(HDTRANS_CLASS("mark.cry"), mark) || output::IsDrawnEndMark(cp);
}

// Whether it stands in a thought between the marks of mark.thought-open and mark.thought-close
// （ああ、難しい〜）, or in a word spelled out (逃・が・さ・な・い), which the sound stage puts between
// output::kEmphasisOpen and output::kEmphasisClose, in the text of its line before it.
bool ThoughtOrSpelled(const Item* it) {
    const OpenMarks before = it->w != nullptr ? it->w->OpenMarksAt(it->tokenIndex) : OpenMarks{};
    const OpenMarks open = OpenMarksAfter(Slice(it->text, 0, it->start), before);
    return open.spelled > 0 || open.thought > 0;
}

// Whether it is an い-adjective ending bare in い, read 1다 or the exclamative 6, with a drawn end
// mark of the sound stage right after it (痛いいいいっ, 痛ーい, 熱いいいっ) or a run of mark.stretch
// that ends its utterance, no word or comma after it (暑いー！, 痛い～; not ずるいー、早く); not a
// copula drawn out after an adjectival noun (無事だああああ), nor in a thought or a word spelled
// out (ThoughtOrSpelled), which no one cries.
bool DrawnPredicate(const Item* it) {
    if (it->detail->cls != kDetailKeiyo) return false;
    const int from = it->DirectIndex();
    if (from < 0 || !dict::InClass(HDTRANS_CLASS("ending.copies-bare"), it->DirectSurface()) ||
        !dict::InClass(HDTRANS_CLASS("ending.copies-plain"), it->DirectText(from))) {
        return false;
    }
    // The drawn end marks and mark.stretch after the word, in its token and the next ones, and
    // the text after them: no word, and no comma, before which the phrase goes on (痛いぃ、やめて,
    // ずるいー、早く).
    const auto marked = [](std::string_view t) {
        const text::Rune r = text::DecodeRune(t);
        return output::IsDrawnEndMark(r.cp) || dict::InClassRune(HDTRANS_CLASS("mark.stretch"), r.cp);
    };
    std::string_view after = TextAfter(it);
    bool drawn = false;
    int tok = it->tokenIndex + (From(it->text, it->start + it->length).empty() ? 1 : 0);
    for (int k = 0; k < 3; k++) {
        while (!after.empty() && marked(after)) {
            after.remove_prefix(text::DecodeRune(after).size);
            drawn = true;
        }
        if (!after.empty() || it->w == nullptr || ++tok >= it->w->TokenCount()) break;
        after = it->w->tokens.Raw(static_cast<std::size_t>(tok));
    }
    const text::Rune next = text::DecodeRune(after);
    return drawn && !text::IsLetter(next.cp) && !dict::InClassRune(HDTRANS_CLASS("mark.comma"), next.cp) &&
           !ThoughtOrSpelled(it);
}

bool Exclaimed(Flusher& f, const Item* it) {
    const Item* tail = it;
    while (tail->next != nullptr) tail = tail->next;
    // The mark is the last word of the sentence, or the token after it.
    for (std::size_t j = static_cast<std::size_t>(tail->tokenIndex); j <= static_cast<std::size_t>(tail->tokenIndex) + 1; j++) {
        if (j >= f.w.tokens.size()) break;
        const std::string_view mark = f.w.tokens.Canonical(j);
        if (mark.starts_with("!") || mark.starts_with("?")) return true;
    }
    return false;
}

// Whether s holds only marks of a pause and spaces.
bool OnlyPauses(std::string_view s) {
    while (!s.empty()) {
        std::size_t n = dict::StartsWithMember(HDTRANS_CLASS("mark.pause"), s);
        if (n == 0 && text::IsSpace(text::DecodeRune(s).cp)) n = text::DecodeRune(s).size;
        if (n == 0) return false;
        s.remove_prefix(n);
    }
    return true;
}

// Whether the speech goes on after the item, past pause marks and spaces, within its sentence
// or past a ！ or ？ that ends it inside a quote the next sentence goes on: a word follows that
// is not a closing bracket (……そうだ、ほら, 「……そうだ！　田中さん」; not ……そうだ。 間違いない,
// ……そうだ！ 間違いない outside quotes, 「……そうだ」 or an unclosed 「……そうだ at the end of the text).
bool SpeechGoesOn(Worker& w, const Item* it) {
    const std::string_view rest = From(it->text, it->start + it->length);
    if (!rest.empty() && !OnlyPauses(rest)) return dict::StartsWithMember(HDTRANS_CLASS("mark.close-after-space"), rest) == 0;
    bool exclaimed = false;
    for (int j = it->tokenIndex + 1; j < w.TokenCount(); j++) {
        const int type = w.Tok(j).type;
        if (type == tokenizer::kTypeEnd) return exclaimed && w.QuoteDepth(j) > 0;
        if (type == tokenizer::kTypeNewline) return false;
        if (type == tokenizer::kTypeSentenceEnd || type == tokenizer::kTypeAsciiPunct) {
            const std::string_view mark = w.tokens.Canonical(static_cast<std::size_t>(j));
            if (!mark.starts_with("!") && !mark.starts_with("?")) return false;
            exclaimed = true;
            continue;
        }
        const std::string_view raw = w.tokens.Raw(static_cast<std::size_t>(j));
        if (!OnlyPauses(raw)) {
            return (!exclaimed || w.QuoteDepth(j) > 0) && dict::StartsWithMember(HDTRANS_CLASS("mark.close-after-space"), raw) == 0;
        }
    }
    return false;
}

bool Narrated(Flusher& f, const Item* it) {
    if (it->InQuote()) return false;
    const Item* tail = it;
    while (tail->next != nullptr) tail = tail->next;
    const auto j = static_cast<std::size_t>(tail->tokenIndex + 1);
    if (j >= f.w.tokens.size()) return false;
    const std::string_view mark = f.w.tokens.Canonical(j);
    return !mark.empty() && text::IsPunct(text::DecodeRune(mark).cp) && f.w.tokens.tokens[j].type == tokenizer::kTypeSymbol;
}

struct NamedFeature {
    std::string_view name;
    CxFeature test;
};

// The features a condition names with F. Each is a property of the item at the atom's position.
// Whether the word is an ellipsis or a dash, which ends a sound said on its own (ドキドキ……).
bool EllipsisItem(const Item* it) {
    const std::string_view s = it->group->surface;
    return s.find("…") != std::string_view::npos || s.find("‥") != std::string_view::npos ||
           s.find("―") != std::string_view::npos;
}

constexpr NamedFeature kFeatures[] = {
    {"exists", [](Flusher&, const Item*) { return true; }},
    {"selected", [](Flusher&, const Item* it) { return it->selected >= 0; }},
    {"conjugating", [](Flusher&, const Item* it) { return it->detail->Yougo(); }},
    {"fixed", [](Flusher&, const Item* it) { return it->Fixed(); }},
    {"first", [](Flusher&, const Item* it) { return it->prev == nullptr; }},
    {"last", [](Flusher&, const Item* it) { return it->After() == nullptr; }},
    {"verb", [](Flusher&, const Item* it) { return it->detail->cls == kDetailVerb; }},
    {"boundary", [](Flusher&, const Item* it) { return PauseOrEnd(it); }},
    // The Korean of the next word starts with this word's Korean and a space.
    {"lemma-starts-next",
     [](Flusher& f, const Item* it) {
         const std::string lemma = ItemLemma(f, it, it->selected);
         const Item* n = it->After();
         return !lemma.empty() && n != nullptr && ItemLemma(f, n, n->selected).starts_with(lemma + " ");
     }},
    // The Korean is a doubled mimetic (삐걱삐걱) whose half starts the Korean of a verb after it in its
    // clause (삐걱거리다): the verb already says the sound.
    {"echoed-by-verb",
     [](Flusher& f, const Item* it) {
         if (it->selected < 0) return false;
         const std::string_view ko = it->EntryText(it->selected);
         if (ko.size() < 6 || ko.size() % 2 != 0 || ko.substr(0, ko.size() / 2) != ko.substr(ko.size() / 2)) return false;
         const std::string half(ko.substr(0, ko.size() / 2));
         // Not a sound said on its own before a pause (ドキドキ……心臓が).
         if (const Item* n = it->After(); n != nullptr) {
             for (int j = it->tokenIndex + 1; j < n->tokenIndex; j++) {
                 const std::string_view t = f.w.Tok(j).text;
                 for (const std::string_view mark : {"…", "‥", "―", "、", "。", "！", "？", "!", "?"}) {
                     if (t.find(mark) != std::string_view::npos) return false;
                 }
             }
         }
         int words = 0;
         for (const Item* n = it->After(); n != nullptr && words < 4 && !PauseOrEnd(n) && !EllipsisItem(n);
              n = n->After(), words++) {
             if (n->detail->cls == kDetailVerb && n->selected >= 0 && ItemLemma(f, n, n->selected).starts_with(half)) return true;
         }
         return false;
     }},
    // A question mark follows the word, with only other marks between (ないの!? ないの？).
    {"question-after",
     [](Flusher&, const Item* it) {
         std::string_view rest = TextAfter(it);
         while (!rest.empty()) {
             const text::Rune r = text::DecodeRune(rest);
             if (r.cp == U'?' || r.cp == U'？') return true;
             if (r.size == 0 || !(r.cp == U'!' || r.cp == U'！' || r.cp == U'…' || r.cp == U'‥')) return false;
             rest.remove_prefix(r.size);
         }
         return false;
     }},
    {"opens-line", [](Flusher& f, const Item* it) { return OpensLine(f.w, it); }},
    // The line uses the tic ぽい (PoiLine).
    {"poi-line", [](Flusher& f, const Item*) { return f.w.settings.poiTic; }},
    {"after-restart", [](Flusher& f, const Item* it) { return AfterRestart(f.w, it); }},
    // An ellipsis or a dash stands right before the word (……そうだ).
    {"after-pause", [](Flusher&, const Item* it) { return it->AfterPause(false); }},
    {"after-space", [](Flusher& f, const Item* it) {
         return it->start == 0 && it->tokenIndex >= 0 &&
                f.w.tokens[static_cast<std::size_t>(it->tokenIndex)].afterSpace;
     }},
    {"after-blank", [](Flusher& f, const Item* it) {
         if (it->start != 0 || it->tokenIndex <= 0) return false;
         const int type = f.w.Tok(it->tokenIndex - 1).type;
         return type == tokenizer::kTypeAsciiSpace || type == tokenizer::kTypeWideSpace;
     }},
    {"filler", [](Flusher&, const Item* it) {
         return (it->Code(it->selected).starts_with("32") ||
                 (it->detail->cls == kDetailInfo &&
                  dict::InClass(HDTRANS_CLASS("word.pause-filler"), it->group->surface)));
     }},
    {"number", [](Flusher&, const Item* it) { return NumberSurface(it->group->surface); }},
    {"quote-verb", [](Flusher&, const Item* it) { return QuoteVerbFollows(it); }},
    // A fixed word span that the phrase or sound stage already read (an utterance in brackets).
    {"word-span", [](Flusher& f, const Item* it) { return it->Fixed() && f.w.Span(it->tokenIndex) == tokenizer::kSpanWord; }},
    // The sense table read the word as an adjectival noun (senses.tsv, adjective).
    {"adjectival", [](Flusher&, const Item* it) { return it->adjectival; }},
    // The word repeats the word before it (CopyBefore), or the word after it repeats it.
    {"copy", [](Flusher&, const Item* it) { return CopyBefore(it) != nullptr; }},
    {"copied", [](Flusher&, const Item* it) { return CopyAfter(it) != nullptr; }},
    // The copy after it follows with no pause (CopiedStraight); the copies are the last
    // predicate of their sentence (CopiesEnd); they end in the connective form 6 (ConnectiveCopies).
    {"copied-straight", [](Flusher&, const Item* it) { return CopiedStraight(it); }},
    {"copies-end", [](Flusher&, const Item* it) { return CopiesEnd(it); }},
    {"connective-copies", [](Flusher&, const Item* it) { return ConnectiveCopies(it); }},
    // The copies are an adjective or adjectival noun whose last copy ends in 1다 or 6
    // (PredicateCopies); the run is a cry (CriedCopies).
    {"predicate-copies", [](Flusher&, const Item* it) { return PredicateCopies(it); }},
    {"cried-copies", [](Flusher&, const Item* it) { return CriedCopies(it); }},
    // The ending of the word starts with a mark of kana.drawn-out (すご|ーい).
    {"drawn-ending",
     [](Flusher&, const Item* it) {
         const int at = it->DirectIndex();
         const std::string_view ending = at >= 0 ? it->DirectSurface() : std::string_view();
         return !ending.empty() && dict::InClassRune(HDTRANS_CLASS("kana.drawn-out"), text::DecodeRune(ending).cp);
     }},
    // An い-adjective in 1다 or 6 drawn out right after it (DrawnPredicate).
    {"drawn-predicate", [](Flusher&, const Item* it) { return DrawnPredicate(it); }},
    // A clipping っ or ッ (mark.clip) follows the word right after it (痛いっ).
    {"clipped", [](Flusher&, const Item* it) { return Clipped(it); }},
    // The last copy of its run is Clipped (だめっ、だめっ; not ないっ、ない、ない).
    {"copies-clipped", [](Flusher&, const Item* it) { return Clipped(LastCopy(it)); }},
    // The sentence of the word ends in ! or ? (an utterance, not a narrated sentence).
    {"exclaimed", [](Flusher& f, const Item* it) { return Exclaimed(f, it); }},
    // The sentence of the word trails off in narration: outside quotes, it ends in an ellipsis
    // (a て form that ends a sentence with a period is a request: え、待って待って。).
    {"narrated", [](Flusher& f, const Item* it) { return Narrated(f, it); }},
    // The word stands outside quotes in a document that writes its speech in 「」: narration.
    {"narration", [](Flusher& f, const Item* it) { return f.w.settings.narrated && !it->InQuote(); }},
    // A digit ends the text before the word in its token (3から).
    {"after-digit", [](Flusher&, const Item* it) { return text::IsDigit(DecodeLastRune(Slice(it->text, 0, it->start)).cp); }},
    // The selected candidate has a sem code of a person, an animal or a god (Person).
    {"person", [](Flusher&, const Item* it) { return Person(it->Attr(it->selected)); }},
    // The surface has a kanji and the name layer has it as a person's name (美咲, read as a town in
    // the base dictionary).
    {"person-name",
     [](Flusher& f, const Item* it) {
         const std::string_view s = it->group->surface;
         bool kanji = false;
         for (std::string_view t = s; !t.empty() && !kanji;) {
             const text::Rune r = text::DecodeRune(t);
             kanji = text::IsKanji(r.cp);
             t.remove_prefix(std::max<std::size_t>(r.size, 1));
         }
         if (!kanji) return false;
         const std::vector<dict::Record> names = f.w.snapshot.NameRecords(s);
         return std::any_of(names.begin(), names.end(), [](const dict::Record& r) { return r.codes->sem.HasPrefix("QPN"); });
     }},
    // The verb was analysed as a causative.
    {"causative", [](Flusher&, const Item* it) {
         return it->detail->record == 2 ||
                (it->detail->syntheticCausative && it->detail->voice == Voice::Potential);
     }},
    // The verb was analysed as a passive that no Korean passive word has taken over yet (撫でられ).
    {"passive", [](Flusher&, const Item* it) { return it->DetailRecord() == 1 && !it->lexical; }},
    // A dictionary word that is the passive of a dictionary verb (書かれ, 使われ, 見られ; PassiveBaseVerb).
    {"lexical-passive", [](Flusher& f, const Item* it) { return !PassiveBaseVerb(f, it->group->surface).empty(); }},
    // The verb was analysed as a causative-passive (待たされ, 読ませられ).
    {"causative-passive", [](Flusher&, const Item* it) {
         return it->DetailRecord() == 5 && it->detail->voice != Voice::Potential;
     }},
    // The (ら)れ of the verb reads as the respectful form (DecideVoice: 確認されますか).
    {"respectful", [](Flusher&, const Item* it) { return it->detail->Yougo() && it->detail->voice == Voice::Respectful; }},
    // The word starts where the word before it ends: in the same token, or at the start of the next
    // token when the word before ends its token.
    {"touches-prev",
     [](Flusher&, const Item* it) {
         const Item* p = it->prev;
         if (p == nullptr) return false;
         const int end = p->start + p->length;
         return (it->tokenIndex == p->tokenIndex && it->start == end) ||
                (it->tokenIndex == p->tokenIndex + 1 && end == static_cast<int>(p->text.size()) && it->start == 0);
     }},
    // The surface starts with a letter or a digit (Unicode L or Nd).
    {"alnum-start",
     [](Flusher&, const Item* it) {
         const char32_t r = text::DecodeRune(it->group->surface).cp;
         return text::IsLetter(r) || text::IsDigit(r);
     }},
    // The surface is katakana only, with ー (KatakanaSurface).
    {"katakana", [](Flusher&, const Item* it) { return KatakanaSurface(it->group->surface); }},
    // The Korean of the selected candidate is empty or spaces.
    {"blank", [](Flusher&, const Item* it) { return TrimSpace(it->EntryText(it->selected)).empty(); }},
    // The Korean lemma of the selected candidate ends in a final consonant (어떤 것, 책).
    {"final-consonant",
     [](Flusher& f, const Item* it) {
         const char c = output::ParticleClass(ItemLemma(f, it, it->selected));
         return c == 'c' || c == 'l';
     }},
    // The Korean of the selected candidate is in a copula family (큰일$AH@, output::CopulaTarget).
    {"copula-target", [](Flusher& f, const Item* it) { return output::CopulaTarget(it->EntryText(it->selected), f.families); }},
    // The word is a みたい that can follow a clause (Item::MitaiLike).
    {"mitai-like", [](Flusher&, const Item* it) { return it->MitaiLike(); }},
    // A contrast goes on after the stem: ではないが (ContrastFollows).
    {"contrast", [](Flusher&, const Item* it) { return ContrastFollows(it); }},
    // The item ends the utterance or the quote (QuoteEnd); UtteranceEnd also passes over a comma,
    // a dash or a small つ.
    {"quote-end", [](Flusher& f, const Item* it) { return QuoteEnd(f.w, it); }},
    {"utterance-end", [](Flusher& f, const Item* it) { return UtteranceEnd(f.w, it); }},
    // More words follow on the line past the end of the sentence (SpeechGoesOn).
    {"speech-goes-on", [](Flusher& f, const Item* it) { return SpeechGoesOn(f.w, it); }},
    // The item ends its sentence: the end of the utterance, or ！？!? after it (SentenceEnd).
    {"sentence-end", [](Flusher& f, const Item* it) { return SentenceEnd(f.w, it); }},
    // A verb follows in the sentence, not an auxiliary of the て form (PredicateFollows).
    {"predicate-follows", [](Flusher&, const Item* it) { return PredicateFollows(it); }},
    // The verb was analysed with no passive, potential or causative.
    {"plain-voice", [](Flusher&, const Item* it) { return it->detail->record == 0; }},
    // A noun that the word describes follows it in its token (DescribedNoun).
    {"described-noun", [](Flusher& f, const Item* it) { return DescribedNoun(f.w, it); }},
    // The Korean lemma is one of ko-verb.holding or a phrase that ends in one (이를 악물다).
    {"holding-verb",
     [](Flusher& f, const Item* it) {
         const std::string lemma = ItemLemma(f, it, it->selected);
         return std::ranges::any_of(dict::ClassMembers(HDTRANS_CLASS("ko-verb.holding")), [&](std::string_view v) {
             return lemma == v || (lemma.ends_with(v) && std::string_view(lemma).substr(0, lemma.size() - v.size()).ends_with(" "));
         });
     }},
    // The surface holds a hiragana.
    {"hiragana",
     [](Flusher&, const Item* it) {
         for (std::string_view t = it->group->surface; !t.empty();) {
             const text::Rune r = text::DecodeRune(t);
             if (tokenizer::ClassFlags(r.cp, 2) == tokenizer::kClassHiragana) return true;
             t.remove_prefix(r.size);
         }
         return false;
     }},
    // The conjugation of a verb in the potential (潜れる, 使える).
    {"potential", [](Flusher&, const Item* it) {
         return it->DetailRecord() == 4 ||
                (it->detail->syntheticCausative && it->detail->voice == Voice::Potential);
     }},
    // The Korean of the chosen candidate is an adjective or 있다, 없다 (배고프다, 효과가 있다): a
    // marker of an adjective family, of a family whose declarative is bare, or a stem that ends like
    // an adjective of ko-adjective.stem-end.
    {"ko-state",
     [](Flusher&, const Item* it) {
         namespace oi = output::internal;
         const std::string_view t = it->EntryText(it->selected);
         if (t.size() < 4 || t.back() != '@') return false;
         const oi::KoreanFamily* k = oi::FindKoreanFamily(t.substr(t.size() - 4));
         if (k == nullptr) return false;
         if (k->partOfSpeech == oi::KoreanPartOfSpeech::Adjective || k->inflectionClass == oi::InflectionClass::Existential ||
             (k->flags & oi::kBareDeclarative) != 0) {
             return true;
         }
         const std::string stem = std::string(t.substr(0, t.size() - 4)) + std::string(k->stem);
         return dict::EndsWithMember(HDTRANS_CLASS("ko-adjective.stem-end"), stem) > 0;
     }},
    // A mark of mark.lengthening, or the lengthen or drawn mark that stands for one, is right after the
    // word (思うなぁ, 思うなー).
    {"lengthened",
     [](Flusher&, const Item* it) {
         const std::string_view rest = TextAfter(it);
         const text::Rune r = text::DecodeRune(rest);
         return r.cp == output::kLengthenMark || r.cp == output::kDrawnMark ||
                dict::InClassRune(HDTRANS_CLASS("mark.lengthening"), r.cp);
     }},
    // A vowel kana of mark.drawn follows the word (燃えるねぇ, 言うねェ).
    {"drawn",
     [](Flusher&, const Item* it) {
         std::string_view rest = From(it->text, it->start + it->length);
         if (rest.empty()) rest = it->detail->nextText;
         return dict::InClassRune(HDTRANS_CLASS("mark.drawn"), text::DecodeRune(rest).cp);
     }},
    // The text ends after the word, or a closing bracket or an exclamation mark follows it: a line
    // of speech ends there (授けよう」, 渡そう！), not a sentence of narration in 。.
    {"speech-close",
     [](Flusher&, const Item* it) {
         const std::string_view rest = TextAfter(it);
         return rest.empty() || RuneIn(text::DecodeRune(rest).cp, "！!」』）)");
     }},
    // An exclamation mark of mark.exclaim is one of the two characters after the word
    // (待ち伏せますか！, ますか？！, ますかっ！).
    {"exclaim-after",
     [](Flusher&, const Item* it) {
         std::string_view rest = From(it->text, it->start + it->length);
         if (rest.empty()) rest = it->detail->nextText;
         for (int n = 0; n < 2 && !rest.empty(); n++) {
             const std::size_t size = text::DecodeRune(rest).size;
             if (size == 0) break;
             if (dict::InClass(HDTRANS_CLASS("mark.exclaim"), rest.substr(0, size))) return true;
             rest.remove_prefix(size);
         }
         return false;
     }},
    // The text after the word, past white space, starts with one of mark.prompt-follow, which shows
    // a question as a prompt on screen: a quoting と, a closing 》 or the answers (開始しますか？という,
    // はい／いいえ, （はい／いいえ）), or holds one of the answers of mark.prompt-answer (乗るやめる).
    {"prompt-after",
     [](Flusher&, const Item* it) {
         std::string_view rest = From(it->text, it->start + it->length);
         if (rest.empty()) rest = it->detail->nextText;
         rest = TrimSpace(rest);
         return dict::StartsWithMember(HDTRANS_CLASS("mark.prompt-follow"), rest) > 0 ||
                std::ranges::any_of(dict::ClassMembers(HDTRANS_CLASS("mark.prompt-answer")),
                                    [&](std::string_view m) { return rest.find(m) != std::string_view::npos; });
     }},
};

struct FieldSpelling {
    char letter;
    CxField field;
};

constexpr FieldSpelling kFields[] = {
    {'S', CxField::Surface}, {'L', CxField::Lemma},         {'D', CxField::Target},
    {'P', CxField::Part},    {'A', CxField::Sem},           {'K', CxField::Class},
    {'U', CxField::EndingSurface}, {'Y', CxField::EndingTarget}, {'B', CxField::Word},
    {'I', CxField::Text},    {'H', CxField::Head},          {'F', CxField::Feature},
    {'C', CxField::Candidate}, {'G', CxField::Gram},        {'O', CxField::FirstWord},
    {'T', CxField::Tail},
};

bool SetField(CxField field) {
    return field == CxField::Surface || field == CxField::Lemma || field == CxField::Target ||
           field == CxField::EndingSurface || field == CxField::EndingTarget || field == CxField::Text ||
           field == CxField::Head || field == CxField::Word || field == CxField::Candidate || field == CxField::FirstWord ||
           field == CxField::Tail;
}

// The nodes and atoms of one condition appended to a table.
class CxSink final : public rules::BodySink {
public:
    explicit CxSink(ConstructionTable& t) : t_(t) {}

    std::size_t List(rules::BodyKind kind, int count) override {
        t_.nodes.push_back({kind, false, static_cast<uint32_t>(count), 0});
        return t_.nodes.size() - 1;
    }

    void End(std::size_t list) override { t_.nodes[list].end = static_cast<uint32_t>(t_.nodes.size()); }

    bool Atom(char letter, int8_t delta, std::string_view op, std::string_view arg, rules::Tokens& tokens) override {
        CxAtom a;
        a.delta = delta;
        const FieldSpelling* field = nullptr;
        for (const FieldSpelling& s : kFields) {
            if (s.letter == letter) field = &s;
        }
        if (field == nullptr) return tokens.Fail("unknown field");
        a.field = field->field;
        if (op != "==" && op != "!=") return tokens.Fail("unknown operator");
        a.negated = op == "!=";
        if (arg[0] == '@') {
            std::string_view name = arg.substr(1);
            if (!SetField(a.field)) return tokens.Fail("this field takes no word class");
            a.match = CxMatch::Member;
            if (name.starts_with('L') || name.starts_with('R')) {
                if (a.field == CxField::Word) return tokens.Fail("B takes a whole word class");
                a.match = name[0] == 'L' ? CxMatch::MemberStart : CxMatch::MemberEnd;
                name.remove_prefix(1);
            }
            a.set = dict::internal::FindClass(dict::internal::BuiltinClasses(), name, dict::internal::ClassHash(name));
            if (a.set == nullptr) return tokens.Fail("no such word class");
        } else {
            const char* error = nullptr;
            std::string raw;
            if (!rules::AppendLiteral(arg, raw, error)) return tokens.Fail(error);
            if (a.field == CxField::Part) raw = dict::PartPatternOf('P', raw);
            if (a.field == CxField::Feature) {
                for (const NamedFeature& n : kFeatures) {
                    if (n.name == raw) a.feature = n.test;
                }
                if (a.feature == nullptr) return tokens.Fail("no such feature");
                a.match = CxMatch::Feature;
            }
            if (a.field == CxField::Class && raw.size() != 1) return tokens.Fail("K takes one letter");
            if (a.field == CxField::Gram && (raw.empty() || raw.size() % 2 != 0)) return tokens.Fail("G takes two-letter codes");
            a.value = std::move(raw);
        }
        t_.nodes.push_back({rules::BodyKind::Or, true, static_cast<uint32_t>(t_.atoms.size()), 0});
        t_.nodes.back().end = static_cast<uint32_t>(t_.nodes.size());
        t_.atoms.push_back(std::move(a));
        return true;
    }

private:
    ConstructionTable& t_;
};

std::string_view Chomp(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
    return line;
}

std::vector<std::string_view> SplitTabs(std::string_view s) {
    std::vector<std::string_view> out;
    for (;;) {
        const std::size_t tab = s.find('\t');
        out.push_back(s.substr(0, tab));
        if (tab == std::string_view::npos) return out;
        s.remove_prefix(tab + 1);
    }
}

bool FamilyName(std::string_view s) {
    if (s.empty() || s[0] < 'a' || s[0] > 'z') return false;
    return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

bool ParseActions(std::string_view text, const std::vector<CxMap>& maps, std::vector<CxAction>& out, const char*& error) {
    constexpr std::pair<std::string_view, CxVerb> kVerbs[] = {
        {"select-lemma", CxVerb::SelectLemma},
        {"select-target", CxVerb::SelectTarget},
        {"take-target", CxVerb::TakeTarget},
        {"set-target", CxVerb::SetTarget},
        {"append-target", CxVerb::AppendTarget},
        {"prepend-target", CxVerb::PrependTarget},
        {"set-ending", CxVerb::SetEnding},
        {"replace-head", CxVerb::ReplaceHead},
        {"replace-ending-tail", CxVerb::ReplaceEndingTail},
        {"map-ending-tail", CxVerb::MapEndingTail},
        {"map-target-tail", CxVerb::MapTargetTail},
        {"map-lemma", CxVerb::MapLemma},
        {"map-target", CxVerb::MapTarget},
        {"select-map", CxVerb::SelectMap},
        {"set-verb", CxVerb::SetVerb},
        {"set-verb-from", CxVerb::SetVerbFrom},
        {"copy-ending", CxVerb::CopyEnding},
        {"copy-word", CxVerb::CopyWord},
        {"set-sound", CxVerb::SetSound},
        {"set-active", CxVerb::SetActive},
        {"clip-ending", CxVerb::ClipEnding},
        {"set-base-verb", CxVerb::SetBaseVerb},
        {"draw-ending", CxVerb::DrawEnding},
        {"take-prev-target", CxVerb::TakePrevTarget},
    };
    rules::Tokens tokens(text);
    while (!tokens.AtEnd()) {
        std::string_view word, literal;
        if (!tokens.Next(word)) break;
        CxAction a;
        const std::size_t at = word.find('@');
        const std::string_view verb = word.substr(0, at);
        std::string_view position = at == std::string_view::npos ? std::string_view() : word.substr(at + 1);
        a.scanned = position.starts_with("scan");
        if (a.scanned) position.remove_prefix(std::string_view("scan").size());
        if (at != std::string_view::npos && !position.empty() && !rules::ParseDelta(position, a.delta)) {
            tokens.Fail("bad action position");
            break;
        }
        if (at != std::string_view::npos && position.empty() && !a.scanned) {
            tokens.Fail("bad action position");
            break;
        }
        bool known = false;
        for (const auto& [name, v] : kVerbs) {
            if (name == verb) {
                a.verb = v;
                known = true;
            }
        }
        if (!known) {
            tokens.Fail("unknown action");
            break;
        }
        if (a.verb == CxVerb::MapLemma || a.verb == CxVerb::MapTarget || a.verb == CxVerb::SelectMap ||
            a.verb == CxVerb::MapEndingTail || a.verb == CxVerb::MapTargetTail) {
            if (!tokens.Next(literal) || !literal.starts_with('@')) {
                tokens.Fail("a map action takes @MAP");
                break;
            }
            const auto map = std::ranges::find(maps, literal.substr(1), &CxMap::name);
            if (map == maps.end()) {
                tokens.Fail("no such map");
                break;
            }
            a.map = static_cast<int>(map - maps.begin());
            out.push_back(std::move(a));
            continue;
        }
        const char* bad = nullptr;
        if (a.verb == CxVerb::CopyEnding || a.verb == CxVerb::CopyWord || a.verb == CxVerb::SetSound ||
            a.verb == CxVerb::SetActive || a.verb == CxVerb::ClipEnding ||
            a.verb == CxVerb::SetBaseVerb || a.verb == CxVerb::DrawEnding || a.verb == CxVerb::TakePrevTarget) {
            out.push_back(std::move(a));
            continue;
        }
        if (!tokens.Next(literal) || !rules::AppendLiteral(literal, a.value, bad)) {
            tokens.Fail(bad != nullptr ? bad : "an action takes a string literal");
            break;
        }
        if ((a.verb == CxVerb::ReplaceHead || a.verb == CxVerb::ReplaceEndingTail || a.verb == CxVerb::SetVerbFrom) &&
            (!tokens.Next(literal) || !rules::AppendLiteral(literal, a.with, bad))) {
            tokens.Fail(bad != nullptr ? bad : "a replace action takes the old and the new text");
            break;
        }
        out.push_back(std::move(a));
    }
    error = tokens.error;
    if (error == nullptr && out.empty()) error = "no action";
    return error == nullptr;
}

// Adds to keys the surfaces one of which the word must have for the condition at node i to hold;
// false when it needs none: an atom S+0== with an exact surface or a class, a list && or & with
// such an operand (the one with the fewest), or a list || of such operands only.
bool SurfaceKeys(const ConstructionTable& t, uint32_t i, std::vector<std::string_view>& keys) {
    const CxNode& n = t.nodes[i];
    if (n.atom) {
        const CxAtom& a = t.atoms[n.count];
        if (a.field != CxField::Surface || a.delta != 0 || a.negated) return false;
        if (a.match == CxMatch::Member) {
            keys.insert(keys.end(), a.set->members.begin(), a.set->members.end());
            return true;
        }
        if (a.match != CxMatch::Literal) return false;
        const std::string_view v = a.value;
        if (v.starts_with("99")) {
            keys.push_back(v.substr(2));
            return true;
        }
        for (const std::string_view marker : {"L", "R", "11", "22", "00"}) {
            if (v.starts_with(marker)) return false;
        }
        if (v.size() > 1 && v[0] == '~') return false;
        keys.push_back(v);
        return true;
    }
    if (n.kind != rules::BodyKind::Or && n.kind != rules::BodyKind::And && n.kind != rules::BodyKind::AndAll) return false;
    bool found = false;
    std::vector<std::string_view> best, own;
    uint32_t child = i + 1;
    for (uint32_t k = 0; k < n.count; k++, child = t.nodes[child].end) {
        own.clear();
        const bool has = SurfaceKeys(t, child, own);
        if (n.kind == rules::BodyKind::Or) {
            if (!has) return false;
            best.insert(best.end(), own.begin(), own.end());
            found = true;
        } else if (has && (!found || own.size() < best.size())) {
            best = own;
            found = true;
        }
    }
    if (found) keys.insert(keys.end(), best.begin(), best.end());
    return found;
}

// Fills the surface index of family c.
void IndexFamily(const ConstructionTable& t, CxFamily& c) {
    if (std::vector<std::string_view> guard; c.guard != kNoGuard && SurfaceKeys(t, c.guard, guard)) {
        c.guardKeyed = true;
        c.guardSurfaces.insert(guard.begin(), guard.end());
    }
    std::vector<std::vector<std::string_view>> keys(c.rows.size());
    std::vector<bool> keyed(c.rows.size());
    for (std::size_t k = 0; k < c.rows.size(); k++) {
        keyed[k] = SurfaceKeys(t, t.rows[static_cast<std::size_t>(c.rows[k])].root, keys[k]);
        if (!keyed[k]) c.open.push_back(c.rows[k]);
        c.indexed = c.indexed || keyed[k];
    }
    for (std::size_t k = 0; k < c.rows.size(); k++) {
        for (const std::string_view key : keys[k]) {
            if (c.bySurface.contains(key)) continue;
            std::vector<int>& rows = c.bySurface[std::string(key)];
            for (std::size_t j = 0; j < c.rows.size(); j++) {
                if (!keyed[j] || std::ranges::find(keys[j], key) != keys[j].end()) rows.push_back(c.rows[j]);
            }
        }
    }
}

}  // namespace

namespace {

// A condition with each token :NAME replaced by the condition named so (a //: line above it).
bool ExpandNamed(std::string_view text, const std::vector<std::pair<std::string, std::string>>& named, std::string& out,
                 std::string& error) {
    out.clear();
    rules::Tokens tokens(text);
    for (std::string_view token; !tokens.AtEnd();) {
        if (!tokens.Next(token)) {
            error = tokens.error != nullptr ? tokens.error : "bad token";
            return false;
        }
        if (!out.empty()) out += ' ';
        if (!token.starts_with(':')) {
            out += token;
            continue;
        }
        const auto it = std::ranges::find(named, token.substr(1), &std::pair<std::string, std::string>::first);
        if (it == named.end()) {
            error = "no named condition " + std::string(token);
            return false;
        }
        out += it->second;
    }
    return true;
}

}  // namespace

ConstructionTable ParseConstructions(std::span<const std::string_view> lines, std::span<const std::string_view> passes) {
    ConstructionTable t;
    std::unordered_map<std::string_view, std::size_t> families;
    std::vector<std::string> ids;
    std::vector<std::pair<std::string, std::string>> named;  // //: NAME <TAB> CONDITION, expanded
    std::string expanded, expandError;
    const auto fail = [&](int line, std::string message) { t.errors.push_back({line, std::move(message)}); };
    int number = 0;
    for (const std::string_view raw : lines) {
        number++;
        const std::string_view line = Chomp(raw);
        if (line.empty()) continue;
        if (line.starts_with("//=")) {
            std::string_view head = line.substr(3);
            while (head.starts_with(' ')) head.remove_prefix(1);
            const std::vector<std::string_view> f = SplitTabs(head);
            const bool guarded = f.size() == 4;
            const std::size_t space = f.size() == 3 || guarded ? f[1].rfind(' ') : std::string_view::npos;
            int stage = 0;
            if ((f.size() != 3 && !guarded) || !FamilyName(f[0]) || !f[1].starts_with("after ") || space < 6 ||
                !rules::ParseInt(f[1].substr(space + 1), 1, 1 << 20, stage) || f.back().empty() ||
                (guarded && !f[2].starts_with("guard "))) {
                fail(number, "a family line is //= FAMILY <TAB> after PASS STAGE [<TAB> guard CONDITION] <TAB> what it is for");
                continue;
            }
            if (families.contains(f[0])) {
                fail(number, "a second line for the family " + std::string(f[0]));
                continue;
            }
            uint32_t guard = internal::kNoGuard;
            if (guarded) {
                if (!ExpandNamed(f[2].substr(std::string_view("guard ").size()), named, expanded, expandError)) {
                    fail(number, "guard: " + expandError);
                    continue;
                }
                guard = static_cast<uint32_t>(t.nodes.size());
                rules::Tokens tokens(expanded);
                CxSink sink(t);
                if (!rules::ParseBody(tokens, sink, true) || !tokens.AtEnd()) {
                    fail(number, std::string("guard: ") + (tokens.error != nullptr ? tokens.error : "trailing tokens"));
                    continue;
                }
            }
            families.emplace(f[0], t.families.size());
            internal::CxFamily family;
            family.name = f[0];
            family.after = f[1].substr(6, space - 6);
            family.stage = stage;
            family.line = number;
            family.guard = guard;
            t.families.push_back(std::move(family));
            continue;
        }
        if (line.starts_with("//~")) {
            std::string_view head = line.substr(3);
            while (head.starts_with(' ')) head.remove_prefix(1);
            const std::vector<std::string_view> f = SplitTabs(head);
            CxMap map{f.empty() ? std::string() : std::string(f[0]), {}};
            bool ok = f.size() == 3 && FamilyName(f[0]) && !f[1].empty() && !f[2].empty();
            for (std::string_view rest = ok ? f[1] : std::string_view(); ok && !rest.empty();) {
                const std::size_t space = rest.find(' ');
                const std::string_view pair = rest.substr(0, space);
                const std::size_t eq = pair.find('=');
                ok = eq != 0 && eq != std::string_view::npos && eq + 1 < pair.size() &&
                     std::ranges::find(map.pairs, pair.substr(0, eq), &std::pair<std::string, std::string>::first) == map.pairs.end();
                if (ok) map.pairs.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
                rest = space == std::string_view::npos ? std::string_view() : rest.substr(space + 1);
                ok = ok && (space == std::string_view::npos || !rest.empty());
            }
            if (!ok) {
                fail(number, "a map line is //~ MAP <TAB> FROM=TO ... <TAB> what it is for, each FROM once");
            } else if (std::ranges::find(t.maps, map.name, &CxMap::name) != t.maps.end()) {
                fail(number, "a second line for the map " + map.name);
            } else {
                t.maps.push_back(std::move(map));
            }
            continue;
        }
        if (line.starts_with("//:")) {
            std::string_view head = line.substr(3);
            while (head.starts_with(' ')) head.remove_prefix(1);
            const std::vector<std::string_view> f = SplitTabs(head);
            if (f.size() != 3 || !FamilyName(f[0]) || f[1].empty() || f[2].empty()) {
                fail(number, "a named condition is //: NAME <TAB> CONDITION <TAB> what it is for");
                continue;
            }
            if (std::ranges::find(named, f[0], &std::pair<std::string, std::string>::first) != named.end()) {
                fail(number, "a second line for the condition " + std::string(f[0]));
                continue;
            }
            if (!ExpandNamed(f[1], named, expanded, expandError)) {
                fail(number, "condition: " + expandError);
                continue;
            }
            // The body is one expression, so that it counts as one operand where it is named.
            ConstructionTable scratch;
            CxSink sink(scratch);
            rules::Tokens tokens(expanded);
            if (!rules::ParseBody(tokens, sink, true) || !tokens.AtEnd()) {
                fail(number, std::string("condition: ") + (tokens.error != nullptr ? tokens.error : "more than one expression"));
                continue;
            }
            named.emplace_back(f[0], expanded);
            continue;
        }
        if (line.starts_with("//")) continue;
        const std::vector<std::string_view> f = SplitTabs(line);
        if (f.size() != 5 || f[2].empty() || f[3].empty()) {
            fail(number, "a row is ID <TAB> SPEC <TAB> CONDITION <TAB> ACTIONS <TAB> NOTE");
            continue;
        }
        const std::string_view id = f[0];
        const std::size_t colon = id.rfind(':');
        int prio = 0;
        if (!id.starts_with("cx:") || colon <= 3 || !rules::ParseInt(id.substr(colon + 1), 0, 1 << 20, prio) ||
            id[colon + 1] < '0' || id[colon + 1] > '9') {
            fail(number, "an id is cx:FAMILY:PRIO");
            continue;
        }
        const auto family = families.find(id.substr(3, colon - 3));
        if (family == families.end()) {
            fail(number, "no family line for " + std::string(id));
            continue;
        }
        if (std::ranges::find(ids, id) != ids.end()) {
            fail(number, "a second row " + std::string(id));
            continue;
        }
        if (!ExpandNamed(f[2], named, expanded, expandError)) {
            fail(number, "condition: " + expandError);
            continue;
        }
        CxRow row{std::string(id), std::string(f[1]), prio, static_cast<uint32_t>(t.nodes.size()), {}, false, number};
        rules::Tokens tokens(expanded);
        CxSink sink(t);
        if (!rules::ParseBody(tokens, sink, true) || !tokens.AtEnd()) {
            fail(number, std::string("condition: ") + (tokens.error != nullptr ? tokens.error : "trailing tokens"));
            continue;
        }
        const char* error = nullptr;
        if (!ParseActions(f[3], t.maps, row.actions, error)) {
            fail(number, std::string("actions: ") + error);
            continue;
        }
        row.scans = std::ranges::any_of(row.actions, &internal::CxAction::scanned);
        ids.emplace_back(id);
        t.families[family->second].rows.push_back(static_cast<int>(t.rows.size()));
        t.rows.push_back(std::move(row));
    }
    t.afterPass.assign(passes.size(), {});
    for (std::size_t k = 0; k < t.families.size(); k++) {
        CxFamily& c = t.families[k];
        if (c.rows.empty()) fail(c.line, "no rows for the family " + c.name);
        std::ranges::stable_sort(c.rows, {}, [&](int r) { return t.rows[static_cast<std::size_t>(r)].prio; });
        for (std::size_t i = 1; i < c.rows.size(); i++) {
            const CxRow& r = t.rows[static_cast<std::size_t>(c.rows[i])];
            if (r.prio == t.rows[static_cast<std::size_t>(c.rows[i - 1])].prio) {
                fail(r.line, "two rows of " + c.name + " share a PRIO");
            }
        }
        IndexFamily(t, c);
        const auto pass = std::ranges::find(passes, c.after);
        if (pass == passes.end()) {
            fail(c.line, "the family " + c.name + " runs after " + c.after + ", which is no pass");
            continue;
        }
        t.afterPass[static_cast<std::size_t>(pass - passes.begin())].push_back(static_cast<int>(k));
    }
    // The families after a pass run by stage, each stage once.
    for (std::vector<int>& after : t.afterPass) {
        std::ranges::stable_sort(after, {}, [&](int k) { return t.families[static_cast<std::size_t>(k)].stage; });
        for (std::size_t i = 1; i < after.size(); i++) {
            const CxFamily& c = t.families[static_cast<std::size_t>(after[i])];
            if (c.stage == t.families[static_cast<std::size_t>(after[i - 1])].stage) {
                fail(c.line, "two families run after " + c.after + " at stage " + std::to_string(c.stage) + "; give one another stage");
            }
        }
    }
    return t;
}

const ConstructionTable& BuiltinConstructions() {
    static const ConstructionTable table = [] {
        for (const dict::internal::BuiltinTable& b : dict::internal::BuiltinTables()) {
            if (b.name == "constructions.tsv") return ParseConstructions(b.lines, TextPassNames());
        }
        return ConstructionTable{};
    }();
    return table;
}

}  // namespace internal

namespace {

using internal::ConstructionTable;
using internal::CxAtom;
using internal::CxField;
using internal::CxMatch;
using internal::CxNode;

// The item delta items away; rightwards from a ruby base, past its reading (Item::Object).
template <typename T>
T* Neighbour(T* it, int delta) {
    const bool base = it != nullptr && it->w != nullptr && it->w->RubyPart(it->tokenIndex) == 1;
    for (; it != nullptr && delta > 0; delta--) it = base ? it->After() : it->next;
    for (; it != nullptr && delta < 0; delta++) it = it->prev;
    return it;
}

// Whether v matches the literal of a condition: "~…" holds the value somewhere; the rest are the
// surface markers of the rules.
bool MatchesLiteral(std::string_view v, std::string_view literal) {
    if (literal.size() > 1 && literal[0] == '~') return Has(v, literal.substr(1));
    return rules::MatchRuleSurface(v, literal);
}

bool MatchesValue(const CxAtom& a, std::string_view v) {
    switch (a.match) {
        case CxMatch::Literal: return MatchesLiteral(v, a.value);
        case CxMatch::Member: return a.set->index.contains(v);
        case CxMatch::MemberStart:
            return std::ranges::any_of(a.set->members, [&](std::string_view m) { return v.starts_with(m); });
        case CxMatch::MemberEnd:
            return std::ranges::any_of(a.set->members, [&](std::string_view m) { return v.ends_with(m); });
        case CxMatch::Feature: break;
    }
    return false;
}

// Whether the atom holds at it; an atom whose item or ending is missing is false, negated or not.
bool Holds(Flusher& f, const Item* origin, const CxAtom& a) {
    const Item* it = Neighbour(origin, a.delta);
    if (it == nullptr) return false;
    bool v = false;
    switch (a.field) {
        case CxField::Surface: v = MatchesValue(a, it->group->surface); break;
        case CxField::Lemma: v = MatchesValue(a, ItemLemma(f, it, it->selected)); break;
        case CxField::Target: v = MatchesValue(a, it->EntryText(it->selected)); break;
        case CxField::Part: v = rules::MatchRuleCode(it->Code(it->selected), a.value); break;
        case CxField::Sem: v = rules::AttrChunksIn(it->Attr(it->selected), a.value); break;
        case CxField::Class: {
            const Entry* e = it->group->At(it->selected);
            if (e == nullptr) return false;
            v = e->cls == a.value[0];
            break;
        }
        case CxField::EndingSurface:
            if (it->DirectIndex() < 0) return false;
            v = MatchesValue(a, it->DirectSurface());
            break;
        case CxField::EndingTarget: {
            const Entry* e = it->DirectEntry(it->DirectIndex());
            if (e == nullptr) return false;
            v = MatchesValue(a, e->dst);
            break;
        }
        case CxField::Word:
            v = a.set != nullptr ? IsAny(it, a.set->members) : IsWord(it, a.value);
            break;
        case CxField::Text: v = MatchesValue(a, Slice(it->text, it->start, it->length)); break;
        case CxField::Head: v = MatchesValue(a, Slice(it->text, 0, it->start)); break;
        case CxField::Tail: v = MatchesValue(a, From(it->text, it->start + it->length)); break;
        case CxField::Feature: v = a.feature(f, it); break;
        case CxField::Candidate: {
            const int count = static_cast<int>(it->group->entries.size());
            for (int slot = 0; slot < count && !v; slot++) v = MatchesValue(a, ItemLemma(f, it, slot));
            break;
        }
        case CxField::Gram: {
            const std::string_view codes = it->GAttr(it->selected);
            for (std::size_t off = 0; off < a.value.size() && !v; off += 2) {
                const std::string_view code = std::string_view(a.value).substr(off, 2);
                // Qw: a question word is the word or comes before it in its sentence, as in rules.
                v = code == "Qw" ? it->AfterQuestionWord() : Has(codes, code);
            }
            break;
        }
        case CxField::FirstWord: {
            const std::string_view target = it->EntryText(it->selected);
            const std::size_t space = target.find(' ');
            if (space == std::string_view::npos) return false;
            v = MatchesValue(a, target.substr(0, space));
            break;
        }
    }
    return v != a.negated;
}

bool Eval(Flusher& f, const ConstructionTable& t, uint32_t i, const Item* it) {
    const CxNode& n = t.nodes[i];
    if (n.atom) return Holds(f, it, t.atoms[n.count]);
    switch (n.kind) {
        case rules::BodyKind::Not: return !Eval(f, t, i + 1, it);
        case rules::BodyKind::ScanLeft:
        case rules::BodyKind::ScanRight: {
            const uint32_t target = i + 1;
            const uint32_t barrier = t.nodes[target].end;
            const int step = n.kind == rules::BodyKind::ScanLeft ? -1 : 1;
            int k = 0;
            for (const Item* x = Neighbour(it, step); x != nullptr && (n.count == 0 || k < static_cast<int>(n.count));
                 x = Neighbour(x, step), k++) {
                if (Eval(f, t, target, x)) return true;
                if (Eval(f, t, barrier, x)) return false;
            }
            return false;
        }
        case rules::BodyKind::Or:
        case rules::BodyKind::And:
        case rules::BodyKind::AndAll: break;
    }
    bool all = true;
    uint32_t child = i + 1;
    for (uint32_t k = 0; k < n.count; k++) {
        const bool v = Eval(f, t, child, it);
        if (n.kind == rules::BodyKind::Or && v) return true;
        if (n.kind == rules::BodyKind::And && !v) return false;
        all = all && v;
        child = t.nodes[child].end;
    }
    return n.kind != rules::BodyKind::Or && all;
}

// The word where the first scan of the condition at node i, outside a !1, finds its target at it;
// null for none. Tried only for a row that fired and places an action by @scan.
const Item* Scanned(Flusher& f, const ConstructionTable& t, uint32_t i, const Item* it) {
    const CxNode& n = t.nodes[i];
    if (n.atom || n.kind == rules::BodyKind::Not) return nullptr;
    if (n.kind == rules::BodyKind::ScanLeft || n.kind == rules::BodyKind::ScanRight) {
        const uint32_t target = i + 1;
        const uint32_t barrier = t.nodes[target].end;
        const int step = n.kind == rules::BodyKind::ScanLeft ? -1 : 1;
        int k = 0;
        for (const Item* x = Neighbour(it, step); x != nullptr && (n.count == 0 || k < static_cast<int>(n.count));
             x = Neighbour(x, step), k++) {
            if (Eval(f, t, target, x)) return x;
            if (Eval(f, t, barrier, x)) return nullptr;
        }
        return nullptr;
    }
    uint32_t child = i + 1;
    for (uint32_t k = 0; k < n.count; k++, child = t.nodes[child].end) {
        if (const Item* x = Scanned(f, t, child, it)) return x;
    }
    return nullptr;
}

// The marker a verb written into the word takes: '$' on an i-adjective, whose families conjugate
// with that sign (しな 하지 $DH@ becomes 하다 $Kz@), else '&'.
char VerbSigil(const Item* it) { return At(it->Code(it->selected), 0) == '7' ? '$' : '&'; }

void Apply(Flusher& f, const ConstructionTable& t, Item* origin, Item* scanned, const internal::CxAction& a,
           Selection source) {
    Item* it = Neighbour(a.scanned ? scanned : origin, a.delta);
    if (it == nullptr) return;
    switch (a.verb) {
        case internal::CxVerb::SelectLemma:
        case internal::CxVerb::SelectTarget: {
            const int count = static_cast<int>(it->group->entries.size());
            for (int slot = 0; slot < count; slot++) {
                const bool match = a.verb == internal::CxVerb::SelectLemma ? MatchesLiteral(ItemLemma(f, it, slot), a.value)
                                                                           : MatchesLiteral(it->EntryText(slot), a.value);
                if (match) {
                    it->SelectCandidate(slot, source);
                    break;
                }
            }
            break;
        }
        case internal::CxVerb::CopyWord: {
            // The copy takes the word of the last copy of its run (なんでなんで 왜 왜).
            // Not before a particle or a copula, whose word the last copy may read in a bound form
            // (まさかまさかの 설마 뜻밖의, ばかりばかりだ), nor from a last copy that writes nothing.
            const Item* last = internal::LastCopy(it);
            if (last == it || last->selected < 0 || it->selected < 0 || last->EntryText(last->selected).empty()) break;
            if (const Item* n = last->next; n != nullptr && n->selected >= 0) {
                if (const Entry* e = n->group->At(n->selected); e != nullptr && e->cls == 'P') break;
            }
            const std::string lemma = ItemLemma(f, last, last->selected);
            if (lemma == ItemLemma(f, it, it->selected)) break;
            bool found = false;
            for (int slot = 0; slot < static_cast<int>(it->group->entries.size()) && !found; slot++) {
                if (ItemLemma(f, it, slot) == lemma) {
                    it->SelectCandidate(slot, source);
                    found = true;
                }
            }
            // A pass may have rewritten the candidate the last copy reads (なんで 어째서 at the
            // start of a sentence): the copy then writes the last copy's Korean.
            if (!found) it->AssignEntrySurface(it->selected, std::string(last->EntryText(last->selected)), source);
            break;
        }
        case internal::CxVerb::CopyEnding: {
            // The copy takes the ending of the last copy of its run (copies rows). A copy reads
            // its own word: the same word may be written apart before a particle (私、私が 나, 내가).
            const Item* last = internal::LastCopy(it);
            if (last == it || last->selected < 0 || it->selected < 0 ||
                it->EntryText(it->selected) != last->EntryText(last->selected)) {
                break;
            }
            // The ending of the same kind as the last copy's: the slot with its code and Korean,
            // or the chosen slot of that code rewritten to its Korean.
            const int from = last->DirectIndex(), to = it->DirectIndex();
            if (from < 0 || to < 0) break;
            const std::string_view code = last->DirectCode(from), ending = last->DirectText(from);
            // Only an ending that starts with its form (6, 1고): a rule's own marker belongs to
            // the words after the last copy (照れ照れになり).
            if (ending.empty() || !text::IsDigit(static_cast<unsigned char>(ending[0])) ||
                (it->DirectCode(to) == code && it->DirectText(to) == ending)) {
                break;
            }
            int same = -1;
            for (int slot = 0; slot < it->DirectCount() && same < 0; slot++) {
                if (it->DirectCode(slot) == code && it->DirectText(slot) == ending) same = slot;
            }
            if (same >= 0) {
                it->SetDirectIndex(same, source);
            } else if (it->DirectCode(to) == code) {
                it->AssignDirectSlot(to, ending, source);
            }
            break;
        }
        case internal::CxVerb::TakeTarget: TakeTarget(it, At(it->Code(it->selected), 0), a.value, source); break;
        case internal::CxVerb::SetTarget: it->AssignEntrySurface(it->selected, a.value, source); break;
        case internal::CxVerb::AppendTarget:
            // The word keeps its candidate; -why names the row that wrote after it.
            it->appended += a.value;
            it->choice = source;
            break;
        case internal::CxVerb::PrependTarget:
            it->AssignEntrySurface(it->selected, a.value + std::string(it->EntryText(it->selected)), source);
            break;
        case internal::CxVerb::SetEnding: it->AssignDirectSlot(it->DirectIndex(), a.value, source); break;
        case internal::CxVerb::TakePrevTarget: {
            // The word writes the Korean of the word before it, which then writes nothing, word or
            // ending: the predicate moves onto this word's ending (堂々と + している 당당하다).
            Item* prev = it->prev;
            if (prev == nullptr || prev->selected < 0 || it->selected < 0) break;
            it->AssignEntrySurface(it->selected, std::string(prev->EntryText(prev->selected)), source);
            prev->AssignEntrySurface(prev->selected, std::string(), source);
            if (const int at = prev->DirectIndex(); at >= 0) prev->AssignDirectSlot(at, std::string(), source);
            break;
        }
        case internal::CxVerb::ClipEnding:
        case internal::CxVerb::DrawEnding: {
            // The mark after the ending, or after the word that has none; the restore stage writes the
            // clip as ㅅ closing the last syllable (아팟, 안됏) and the lengthening as ~ at the end of
            // the phrase (대단해~).
            const char32_t mark = a.verb == internal::CxVerb::ClipEnding ? output::kClipMark : output::kLengthenMark;
            if (const int at = it->DirectIndex(); at >= 0) {
                std::string ending(it->DirectText(at));
                text::AppendRune(ending, mark);
                it->AssignDirectSlot(at, ending, source);
            } else if (it->selected >= 0) {
                std::string word(it->EntryText(it->selected));
                text::AppendRune(word, mark);
                it->AssignEntrySurface(it->selected, word, source);
            }
            break;
        }

        case internal::CxVerb::ReplaceHead: {
            const std::string_view target = it->EntryText(it->selected);
            if (target.starts_with(a.value)) {
                it->AssignEntrySurface(it->selected, a.with + std::string(target.substr(a.value.size())), source);
            }
            break;
        }
        case internal::CxVerb::ReplaceEndingTail: {
            const Entry* e = it->DirectEntry(it->DirectIndex());
            if (e != nullptr && e->dst.ends_with(a.value)) {
                it->AssignDirectSlot(it->DirectIndex(), e->dst.substr(0, e->dst.size() - a.value.size()) + a.with, source);
            }
            break;
        }
        case internal::CxVerb::MapEndingTail: {
            // The longest tail of the map that the ending's Korean ends in is replaced by its pair.
            const Entry* e = it->DirectEntry(it->DirectIndex());
            if (e == nullptr) break;
            const std::pair<std::string, std::string>* best = nullptr;
            for (const auto& pair : t.maps[static_cast<std::size_t>(a.map)].pairs) {
                if (e->dst.ends_with(pair.first) && (best == nullptr || pair.first.size() > best->first.size())) best = &pair;
            }
            if (best != nullptr) {
                it->AssignDirectSlot(it->DirectIndex(), e->dst.substr(0, e->dst.size() - best->first.size()) + best->second,
                                     source);
            }
            break;
        }
        case internal::CxVerb::MapTargetTail: {
            // The same on the Korean of the selected candidate (돕겠습니다 → 돕겠으니까요).
            const std::string target(it->EntryText(it->selected));
            const std::pair<std::string, std::string>* best = nullptr;
            for (const auto& pair : t.maps[static_cast<std::size_t>(a.map)].pairs) {
                if (target.ends_with(pair.first) && (best == nullptr || pair.first.size() > best->first.size())) best = &pair;
            }
            if (best != nullptr) {
                it->AssignEntrySurface(it->selected, target.substr(0, target.size() - best->first.size()) + best->second, source);
            }
            break;
        }
        case internal::CxVerb::MapLemma:
        case internal::CxVerb::MapTarget: {
            const std::string key =
                a.verb == internal::CxVerb::MapLemma ? ItemLemma(f, it, it->selected) : std::string(it->EntryText(it->selected));
            for (const auto& [from, to] : t.maps[static_cast<std::size_t>(a.map)].pairs) {
                if (from == key) {
                    it->AssignEntrySurface(it->selected, to, source);
                    break;
                }
            }
            break;
        }
        case internal::CxVerb::SelectMap: {
            const std::string key = ItemLemma(f, it, it->selected);
            for (const auto& [from, to] : t.maps[static_cast<std::size_t>(a.map)].pairs) {
                if (from != key) continue;
                const int count = static_cast<int>(it->group->entries.size());
                for (int slot = 0; slot < count; slot++) {
                    if (ItemLemma(f, it, slot) == to) {
                        it->SelectCandidate(slot, source);
                        break;
                    }
                }
                break;
            }
            break;
        }
        case internal::CxVerb::SetVerb:
            it->AssignEntrySurface(it->selected, output::Conjugating(a.value, VerbSigil(it), "", f.families).target, source);
            break;
        case internal::CxVerb::SetVerbFrom: {
            // A translation that ends in the old tail is the verb whose dictionary form is its head
            // and the new tail (기다리게 &C6@ with "게 &C6@" "다" is 기다리다).
            const std::string_view target = it->EntryText(it->selected);
            // The head may be empty when the new tail is a verb of its own (시&AQ@ with "시&AQ@" "하다").
            const std::string lemma = target.ends_with(a.value)
                                          ? std::string(target.substr(0, target.size() - a.value.size())) + a.with
                                          : std::string();
            if (lemma.size() > std::string_view("다").size()) {
                // A stem whose class its spelling leaves open (붙이다: 붙여 or 붙이어) is a regular one.
                output::Conjugated c = output::Conjugating(lemma, VerbSigil(it), "", f.families);
                if (c.target.empty()) c = output::Conjugating(lemma, VerbSigil(it), "규칙", f.families);
                if (!c.target.empty()) it->AssignEntrySurface(it->selected, c.target, source);
            }
            break;
        }
        case internal::CxVerb::SetActive:
            // The verb is written in the plain voice: its Korean without the passive, the causative,
            // the potential or the causative-passive (읽혀지다, 읽게 하다, 읽을 수 있다, 읽게 되다 → 읽다).
            if (it->DetailRecord() == 1 || it->DetailRecord() == 2 || it->DetailRecord() == 4 || it->DetailRecord() == 5) {
                it->detail->record = 0;
                if (Entry* e = it->group->At(it->selected)) e->targetChoice = source;
                it->choice = source;
            }
            break;
        case internal::CxVerb::SetBaseVerb:
            // A dictionary word read as the passive of a verb is written as that verb (PassiveBaseVerb).
            if (std::string verb = PassiveBaseVerb(f, it->group->surface); !verb.empty()) {
                it->AssignEntrySurface(it->selected, verb, source);
            }
            break;
        case internal::CxVerb::SetSound: {
            if (std::string sound = text::FromKana(it->group->surface, text::Preset::Fan); !sound.empty()) {
                it->AssignEntrySurface(it->selected, sound, source);
            }
            break;
        }
    }
}

}  // namespace

void RunConstructions(Flusher& f, std::size_t pass, bool (*skip)(std::string_view name)) {
    internal::RunConstructions(f, internal::BuiltinConstructions(), pass, skip);
}

void internal::RunConstructions(Flusher& f, const ConstructionTable& t, std::size_t pass,
                                bool (*skip)(std::string_view name)) {
    if (pass >= t.afterPass.size()) return;
    rules::Tally* tally = f.w.settings.ruleTally;
    if (tally != nullptr && tally->constructionRuns.size() < t.rows.size()) {
        tally->constructionRuns.resize(t.rows.size());
        tally->constructionFires.resize(t.rows.size());
    }
    for (const int k : t.afterPass[pass]) {
        const internal::CxFamily& family = t.families[static_cast<std::size_t>(k)];
        if (skip != nullptr && skip(family.name)) continue;
        for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
            if (family.guardKeyed && !family.guardSurfaces.contains(std::string_view(it->group->surface))) continue;
            if (family.guard != internal::kNoGuard && !Eval(f, t, family.guard, it)) continue;
            // Rule counts try every row.
            const std::vector<int>* rows = &family.rows;
            if (family.indexed && tally == nullptr) {
                const auto found = family.bySurface.find(std::string_view(it->group->surface));
                rows = found != family.bySurface.end() ? &found->second : &family.open;
            }
            for (const int r : *rows) {
                const internal::CxRow& row = t.rows[static_cast<std::size_t>(r)];
                if (skip != nullptr && skip(row.id)) continue;
                const bool fired = Eval(f, t, row.root, it);
                if (tally != nullptr) {
                    tally->constructionRuns[static_cast<std::size_t>(r)]++;
                    if (fired) tally->constructionFires[static_cast<std::size_t>(r)]++;
                }
                if (!fired) continue;
                // The chain is the flusher's; the conditions read it through const pointers.
                Item* found = row.scans ? const_cast<Item*>(Scanned(f, t, row.root, it)) : nullptr;
                for (const internal::CxAction& a : row.actions) Apply(f, t, it, found, a, {Provenance::Rewrite, r});
                break;
            }
        }
    }
}

std::string_view ConstructionId(int index) {
    const ConstructionTable& t = internal::BuiltinConstructions();
    if (index < 0 || static_cast<std::size_t>(index) >= t.rows.size()) return {};
    return t.rows[static_cast<std::size_t>(index)].id;
}

}  // namespace hdTrans::word
