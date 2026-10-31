#include "core/floor/floor.h"

#include "core/dict/classes.h"
#include "core/text/hangul.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"

namespace hdTrans::floor {

namespace {

char ClassDigit(const Word& w) { return w.item->entry.code.empty() ? 0 : w.item->entry.code[0]; }

// A verb, adjective or adjectival noun with its conjugation.
bool Predicate(const Word& w) {
    const char c = ClassDigit(w);
    return w.item->detailClass != output::DetailClass::None || c == '4' || c == '7' || c == '8';
}

bool Particle(const Word& w) { return ClassDigit(w) == '2'; }

// The Korean of a particle without the alternation compose resolves later ((으)로 로, (이)가 가).
std::string_view ParticleKo(const Word& w) {
    std::string_view ko = w.ko;
    if (ko.starts_with('(')) {
        const std::size_t close = ko.find(')');
        if (close != std::string_view::npos) ko.remove_prefix(close + 1);
    }
    return ko;
}

// The last rune of s, 0 for none.
char32_t LastRune(std::string_view s) {
    char32_t last = 0;
    for (std::size_t at = 0; at < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(at));
        last = r.cp;
        at += std::max<std::size_t>(1, r.size);
    }
    return last;
}

std::string_view LastSyllable(std::string_view s) {
    std::size_t at = s.size();
    while (at > 0 && (static_cast<unsigned char>(s[at - 1]) & 0xC0) == 0x80) at--;
    if (at > 0) at--;
    return s.substr(at);
}

bool Syllable(char32_t r) { return r >= text::kBase && r <= text::kLast; }

bool HangulStart(std::string_view s) { return !s.empty() && Syllable(text::DecodeRune(s).cp); }

bool Hiragana(std::string_view s) {
    if (s.empty()) return false;
    for (std::size_t at = 0; at < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(at));
        if (r.cp < U'ぁ' || r.cp > U'ゖ') return false;
        at += std::max<std::size_t>(1, r.size);
    }
    return true;
}

// Whether b follows a in the text of their token, with nothing between them.
bool Adjacent(const Word& a, const Word& b) {
    return a.item->tokenIndex == b.item->tokenIndex &&
           b.item->offset == a.item->offset + static_cast<int>(a.source.size());
}

// The ending as its Korean text, without the form code it starts with.
std::string_view EndingText(const Word& w) {
    std::string_view t = w.item->ending.text;
    if (!t.empty() && t[0] >= '0' && t[0] <= '9') t.remove_prefix(1);
    return t;
}

void Add(std::vector<Flag>& out, std::string_view check, std::span<const Word> words, int first, int last, int at) {
    Flag f;
    f.check = check;
    f.word = at;
    f.last = last;
    for (int k = first; k <= last; k++) {
        const Word& w = words[static_cast<std::size_t>(k)];
        f.ja += w.source;
        f.ko += w.ko;
        if (w.space && k < last) f.ko += ' ';
    }
    out.push_back(std::move(f));
}

// FLR-2: a noun whose Korean is a phrase in an adnominal form (つき 붙어 있는) before a particle.
bool AdnominalGloss(const Word& w, const Word& n) {
    return !w.space && ClassDigit(w) == '1' && w.ko.find(' ') != std::string_view::npos &&
           dict::InClass(HDTRANS_CLASS("ko-floor.noun-adnominal-end"), LastSyllable(w.ko)) && Particle(n) &&
           (dict::InClass(HDTRANS_CLASS("ko-floor.case-particle"), ParticleKo(n)) ||
            dict::InClass(HDTRANS_CLASS("ko-floor.bound-particle"), ParticleKo(n)));
}

// FLR-2: a particle after a word that takes none.
bool ParticleAfterEnding(const Word& w, const Word& n) {
    if (w.space || !Predicate(w) || w.ko.empty() || !Syllable(LastRune(w.ko))) return false;
    const std::string_view last = LastSyllable(w.ko);
    // A future adnominal (할, 갈) only where the ending writes no word after it (할 때).
    const bool plainPredicate = w.ko.find(' ') == std::string_view::npos && EndingText(w).find(' ') == std::string_view::npos;
    const bool adnominal = dict::InClass(HDTRANS_CLASS("ko-floor.adnominal-end"), last) ||
                           (plainPredicate && text::Final(LastRune(w.ko)) == text::kFinalRieul &&
                            (ClassDigit(w) == '4' || ClassDigit(w) == '7'));
    if (Particle(n)) {
        if (adnominal) {
            return dict::InClass(HDTRANS_CLASS("ko-floor.case-particle"), ParticleKo(n)) ||
                   dict::InClass(HDTRANS_CLASS("ko-floor.bound-particle"), ParticleKo(n));
        }
        if (!dict::InClass(HDTRANS_CLASS("ko-floor.connective-end"), last)) return false;
        if (!dict::InClass(HDTRANS_CLASS("ko-floor.case-particle"), ParticleKo(n))) return false;
        // -면서의, -어서의 and -며의 are written Korean.
        const char32_t lr = LastRune(w.ko);
        return !((lr == U'서' || lr == U'며') && ParticleKo(n) == "의");
    }
    // A predicate the Japanese particle starts (に及んで 에 이르러).
    if (adnominal && Predicate(n)) {
        const std::size_t space = n.ko.find(' ');
        return space != std::string_view::npos &&
               dict::InClass(HDTRANS_CLASS("ko-floor.case-particle"), n.ko.substr(0, space));
    }
    return false;
}

bool ParticlePair(const Word& w, const Word& n) {
    if (w.space || !Particle(w) || !Particle(n) || w.ko.empty() || n.ko.empty()) return false;
    if (dict::InClass(HDTRANS_CLASS("ko-floor.core-case"), ParticleKo(w))) {
        return dict::InClass(HDTRANS_CLASS("ko-floor.case-particle"), ParticleKo(n)) ||
               dict::InClass(HDTRANS_CLASS("ko-floor.bound-particle"), ParticleKo(n));
    }
    return dict::InClass(HDTRANS_CLASS("ko-floor.adverbial-case"), ParticleKo(w)) &&
           dict::InClass(HDTRANS_CLASS("ko-floor.after-adverbial"), ParticleKo(n));
}

bool ShortPronoun(const Word& w, const Word& n) {
    return w.item->entry.code == "120" && dict::InClass(HDTRANS_CLASS("ko-floor.pronoun-ga"), w.ko) && Particle(n) &&
           !ParticleKo(n).empty() && ParticleKo(n) != "가";
}

// FLR-3: a noun written right after a connective or final ending.
bool Glued(const Word& w, const Word& n) {
    if (w.space || !Predicate(w) || w.ko.empty() || !Syllable(LastRune(w.ko))) return false;
    const std::string_view last = LastSyllable(w.ko);
    if (!dict::InClass(HDTRANS_CLASS("ko-floor.connective-end"), last) && last != "다" && last != "지") return false;
    return ClassDigit(n) == '1' && HangulStart(n.ko);
}

// FLR-5: a verb read as a noun in -ㅁ before a verb or a light verb.
bool NominalBeforeVerb(const Word& w, const Word& n) {
    if (ClassDigit(w) != '1' || w.ko.empty() || text::Final(LastRune(w.ko)) != text::kFinalMieum) return false;
    if (!dict::InClassRune(HDTRANS_CLASS("floor.continuative-kana"), LastRune(w.source))) return false;
    if (Particle(n) || n.ko.empty()) return false;
    if (dict::StartsWithMember(HDTRANS_CLASS("ko-floor.light-verb"), n.ko) != 0) return true;
    return Predicate(n) && HangulStart(n.ko) && dict::StartsWithMember(HDTRANS_CLASS("ko-floor.particle-start"), n.ko) == 0;
}

// FLR-6: a kanji read on its own whose okurigana after it is dropped.
bool LostOkurigana(const Word& w, const Word& n) {
    if (text::RuneCount(w.source) != 1 || !text::IsKanji(text::DecodeRune(w.source).cp)) return false;
    if (!Hiragana(n.source) || !n.ko.empty() || Particle(n)) return false;
    if (dict::InClassRune(HDTRANS_CLASS("floor.okurigana-skip"), text::DecodeRune(n.source).cp)) return false;
    return !dict::InClass(HDTRANS_CLASS("floor.dropped-suffix"), n.source);
}

// Whether the grammar codes (two bytes each) hold code.
bool HasCode(std::string_view codes, std::string_view code) {
    for (std::size_t at = 0; at + 1 < codes.size(); at += 2) {
        if (codes.substr(at, 2) == code) return true;
    }
    return false;
}

}  // namespace

Reading ReadingOf(std::span<const Word> words, int token, int start, int end) {
    Reading r;
    for (const Word& w : words) {
        const int from = w.item->offset;
        const int to = from + static_cast<int>(w.source.size());
        if (token >= 0 && (w.item->tokenIndex != token || to <= start || from >= end)) continue;
        if (!w.ko.empty()) r.words++;
        const std::string_view codes = w.item->entry.gattr;
        if (!w.ko.empty() && (HasCode(codes, "Nm") || HasCode(codes, "On"))) r.unread++;
        if (w.ko.empty() && Hiragana(w.source) && !Particle(w) &&
            !dict::InClassRune(HDTRANS_CLASS("floor.okurigana-skip"), text::DecodeRune(w.source).cp)) {
            r.dropped++;
        }
    }
    return r;
}

word::Veto VetoOf(const Word& w) {
    word::Veto v;
    v.token = w.item->tokenIndex;
    v.start = w.item->offset;
    v.length = static_cast<int>(w.item->surface.size());
    v.cls = ClassDigit(w);
    return v;
}

std::string Describe(std::span<const Word> words) {
    std::string out;
    for (const Word& w : words) {
        const output::ResolvedItem& it = *w.item;
        if (!out.empty()) out += " | ";
        out += std::to_string(it.tokenIndex) + ":" + std::to_string(it.offset) + " " + it.entry.code + " " +
               it.entry.gattr + " d" + std::to_string(static_cast<int>(it.detailClass)) + " e[" + it.endingSurface +
               "=" + it.ending.text + "] t[" + it.entry.text + "] " + std::string(w.source) + " => " + std::string(w.ko) +
               (w.space ? " _" : "");
    }
    return out;
}

void Check(std::span<const Word> words, std::vector<Flag>& out) {
    const int count = static_cast<int>(words.size());
    for (int i = 0; i + 1 < count; i++) {
        const Word& w = words[static_cast<std::size_t>(i)];
        const Word& n = words[static_cast<std::size_t>(i + 1)];
        if (!Adjacent(w, n)) continue;
        if (ParticleAfterEnding(w, n) || AdnominalGloss(w, n) || ParticlePair(w, n) || ShortPronoun(w, n)) {
            Add(out, "FLR-2", words, i, i + 1, i);
            continue;
        }
        if (NominalBeforeVerb(w, n)) {
            Add(out, "FLR-5", words, i, i + 1, i);
            continue;
        }
        if (LostOkurigana(w, n)) {
            Add(out, "FLR-6", words, i, i + 1, i);
            continue;
        }
        // The noun after the words with no Korean between (み|な|成長 보고성장).
        int j = i + 1;
        while (j + 1 < count && words[static_cast<std::size_t>(j)].ko.empty() &&
               Adjacent(words[static_cast<std::size_t>(j)], words[static_cast<std::size_t>(j + 1)])) {
            j++;
        }
        if (Glued(w, words[static_cast<std::size_t>(j)])) Add(out, "FLR-3", words, i, j, i);
    }
}

}  // namespace hdTrans::floor
