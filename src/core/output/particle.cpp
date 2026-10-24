#include "core/output/particle.h"

#include "core/dict/char_readings.h"
#include "core/output/cleanup.h"
#include "core/text/hangul.h"

namespace hdTrans::output {

namespace {

constexpr JosaForms kJosaTable[kJosaRows] = {
    {"(이)가", "이", "가"},
    {"(으)로", "으로", "로"},
    {"(은)는", "은", "는"},
    {"(을)를", "을", "를"},
    {"(이)다", "이다", "다"},
    {"(이)라", "이라", "라"},
    {"(이)래", "이래", "래"},
    {"(이)예", "이예", "예"},
    {"(이)군", "이군", "군"},
    {"(이)구", "이구", "구"},
    {"(이)지", "이지", "지"},
    {"(이)면", "이면", "면"},
    {"(이)니", "이니", "니"},
    {"(이)겠", "이겠", "겠"},
    {"(이)야", "이야", "야"},
    {"(와)과", "과", "와"},
    {"(이)든", "이든", "든"},
    {"(이)란", "이란", "란"},
    {"(이)나", "이나", "나"},
    {"(이)예", "이예", "예"},
    {"(이에)예", "이에", "예"},
    {"(이어)여", "이어", "여"},
    {"(이었)였", "이었", "였"},
    {"(이)고", "이고", "고"},
    {"(이)서", "이", "서"},
    {"(이)던가", "이던가", "던가"},
    {"(이)랍", "이랍", "랍"},
    {"(이)네", "이네", "네"},
    {"(이)잖", "이잖", "잖"},
};

// The text a piece reads as in a TextBefore.
std::string_view ReadAs(const Piece& p) {
    switch (p.span) {
        case SpanText: return p.text;
        case SpanWord: return ClassSyllable(SpanFinal(p.text));
        default: return "*";
    }
}

// The length of ReadAs(p), without computing a word span's class.
size_t ReadLength(const Piece& p) {
    switch (p.span) {
        case SpanText: return p.text.size();
        case SpanWord: return 3;
        default: return 1;
    }
}

char HangulClass(int final) {
    switch (final) {
        case text::kFinalNone: return 'v';
        case text::kFinalRieul: return 'l';
    }
    return 'c';
}

// The value of an ASCII or full-width digit, -1 for other characters.
int DigitValue(char32_t r) {
    if (r >= U'0' && r <= U'9') return static_cast<int>(r - U'0');
    if (r >= U'０' && r <= U'９') return static_cast<int>(r - U'０');
    return -1;
}

// The upper-case ASCII form of an ASCII or full-width Latin letter, 0 for other
// characters.
char LatinValue(char32_t r) {
    if (r >= U'A' && r <= U'Z') return static_cast<char>(r);
    if (r >= U'a' && r <= U'z') return static_cast<char>(r - U'a' + U'A');
    if (r >= U'Ａ' && r <= U'Ｚ') return static_cast<char>(r - U'Ａ' + U'A');
    if (r >= U'ａ' && r <= U'ｚ') return static_cast<char>(r - U'ａ' + U'A');
    return 0;
}

bool IsLowerLatin(char32_t r) { return (r >= U'a' && r <= U'z') || (r >= U'ａ' && r <= U'ｚ'); }

// A character after which a particle has no word to fit.
bool EndsSearch(char32_t r) {
    switch (r) {
        case U'\n': case U',': case U'、': case U'，': case U'.': case U'。': case U'．':
            return true;
    }
    return IsOpening(r);
}

// The classes of 영 일 이 삼 사 오 육 칠 팔 구.
constexpr char kDigitClasses[] = "clvcvvcllv";

// The class of the number that ends the first end bytes of text, read in Korean: by
// its last digit after a decimal point (3.10 삼점일영), otherwise by its last non-zero
// digit or, when zeros follow it, by the unit they make (10 십, 100 백, 1000 천, 10⁴
// 만, 10⁸ 억, 10¹² 조, 10¹⁶ 경). Commas between digits are passed over; zeros alone are 영.
char NumberClass(const TextBefore& text, size_t end) {
    int count = 0;
    int last = 0;   // the last digit
    int zeros = 0;  // zeros after the last non-zero digit
    bool nonZero = false;
    bool decimal = false;
    for (size_t i = end; i > 0;) {
        const auto r = DecodeLastRune(text, i);
        if (r.size == 0 || r.size > i) break;
        i -= r.size;
        if (const int d = DigitValue(r.cp); d >= 0) {
            if (count == 0) last = d;
            count++;
            if (!nonZero) {
                if (d == 0) {
                    zeros++;
                } else {
                    nonZero = true;
                }
            }
            continue;
        }
        if (DigitValue(DecodeLastRune(text, i).cp) < 0) break;
        if (r.cp == U'.' || r.cp == U'．') {
            decimal = true;
        } else if (r.cp != U',' && r.cp != U'，') {
            break;
        }
    }
    if (decimal || zeros == 0) return kDigitClasses[last];
    if (zeros == count) return 'c';  // 영
    if (zeros >= 12 && zeros < 16) return 'v';  // 조
    return 'c';  // 십 백 천 만 억 경
}

// The classes of the names of the letters A to Z as Korean writes them (에이 비 씨 …
// 엘 엠 엔 오 … 알 …).
constexpr char kLetterNameClasses[] = "vvvvvvvvvvvlccvvvlvvvvvvvv";

// The classes of units written after a number, by their Korean reading (2m 미터, 5kg
// 킬로그램).
struct UnitClass {
    std::string_view unit;
    char final;
};
constexpr UnitClass kUnitClasses[] = {
    {"M", 'v'},  {"CM", 'v'}, {"MM", 'v'}, {"KM", 'v'}, {"L", 'v'},  {"ML", 'v'},
    {"KG", 'c'}, {"MG", 'c'}, {"T", 'c'},  {"KB", 'v'}, {"MB", 'v'}, {"GB", 'v'},
    {"TB", 'v'}, {"HZ", 'v'}, {"PX", 'l'}, {"DB", 'l'}, {"FPS", 'v'},
};

// The class of the Latin word that ends the first end bytes of text: a unit after a
// number by its reading, a single letter or a word in capitals (an abbreviation: BGM
// 비지엠) by the name of its last letter, another word by its English spelling
// (internal::LatinWordClass).
char LatinClass(const TextBefore& text, size_t end) {
    char last[internal::kLatinWindow] = {};  // the last letters, upper case, last first
    size_t count = 0;
    bool lower = false;
    size_t i = end;
    while (i > 0) {
        const auto r = DecodeLastRune(text, i);
        const char c = LatinValue(r.cp);
        if (c == 0 || r.size == 0 || r.size > i) break;
        lower = lower || IsLowerLatin(r.cp);
        if (count < internal::kLatinWindow) last[count] = c;
        count++;
        i -= r.size;
    }
    size_t before = i;
    if (before > 0 && text.At(before - 1) == ' ') before--;
    char word[internal::kLatinWindow];
    const size_t kept = count < internal::kLatinWindow ? count : internal::kLatinWindow;
    for (size_t n = 0; n < kept; n++) word[n] = last[kept - 1 - n];
    if (count <= 3 && DigitValue(DecodeLastRune(text, before).cp) >= 0) {
        const std::string_view unit(word, count);
        for (const auto& u : kUnitClasses) {
            if (u.unit == unit) return u.final;
        }
    }
    if (count == 1 || !lower) return kLetterNameClasses[last[0] - 'A'];
    return internal::LatinWordClass(std::string_view(word, kept));
}

bool IsAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

char ToLower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// Whether the letter at i of the lower-case word w spells a vowel: a e i o u, and y
// after a consonant (gym, happy).
bool SpellsVowel(std::string_view w, size_t i) {
    if (i >= w.size()) return false;
    switch (w[i]) {
        case 'a': case 'e': case 'i': case 'o': case 'u': return true;
        case 'y': return i > 0 && !SpellsVowel(w, i - 1);
    }
    return false;
}

// Whether text is only Latin letters and digits, and not empty.
bool Alphanumeric(std::string_view text) {
    if (text.empty()) return false;
    while (!text.empty()) {
        const auto r = text::DecodeRune(text);
        if (LatinValue(r.cp) == 0 && DigitValue(r.cp) < 0) return false;
        text.remove_prefix(r.size);
    }
    return true;
}

}  // namespace

TextBefore::TextBefore(std::string_view tail) : tail_(tail) {}

TextBefore::TextBefore(std::string_view head, std::string_view tail) : head_(head), tail_(tail) {}

TextBefore::TextBefore(std::span<const Piece> pieces, std::string_view tail) : pieces_(pieces), tail_(tail) {
    for (const auto& p : pieces_) piecesSize_ += ReadLength(p);
}

unsigned char TextBefore::At(size_t i) const {
    if (i >= piecesSize_ + head_.size()) return static_cast<unsigned char>(tail_[i - piecesSize_ - head_.size()]);
    if (i >= piecesSize_) return static_cast<unsigned char>(head_[i - piecesSize_]);
    size_t end = piecesSize_;
    for (size_t k = pieces_.size(); k-- > 0;) {
        const size_t start = end - ReadLength(pieces_[k]);
        if (i >= start) return static_cast<unsigned char>(ReadAs(pieces_[k])[i - start]);
        end = start;
    }
    return 0;
}

text::Rune DecodeLastRune(const TextBefore& t, size_t end) {
    if (end == 0) return {text::kReplacement, 0};
    const unsigned char b = t.At(end - 1);
    if (b < 0x80) return {b, 1};
    const size_t lim = end >= 4 ? end - 4 : 0;
    size_t start = end - 1;
    while (start > lim && (t.At(start) & 0xC0) == 0x80) start--;
    char buf[4];
    const size_t n = end - start;
    if (n > 4) return {text::kReplacement, 1};
    for (size_t k = 0; k < n; k++) buf[k] = static_cast<char>(t.At(start + k));
    const auto r = text::DecodeRune(std::string_view(buf, n));
    if (r.size != n) return {text::kReplacement, 1};
    return r;
}

char ParticleClass(const TextBefore& text) {
    bool spaced = false;  // a space stands between the character and the particle
    for (size_t i = text.Size(); i > 0;) {
        const auto rune = DecodeLastRune(text, i);
        const char32_t r = rune.cp;
        if (EndsSearch(r)) return 0;
        // ! and ? end the search where a space follows them: 돈!으로 and 「빵!」이라고 read
        // 돈 and 빵, 했어!? 라는 reads nothing.
        if ((r == U'!' || r == U'?' || r == U'！' || r == U'？') && spaced) return 0;
        if (r == U' ' || r == U'　') spaced = true;
        if (const int f = text::Final(r); f >= 0) return HangulClass(f);
        if (r == U'ㄹ') return 'l';
        if (r >= U'ㄱ' && r <= U'ㅎ') return 'c';
        if (r >= U'ㅏ' && r <= U'ㅣ') return 'v';
        if (DigitValue(r) >= 0) return NumberClass(text, i);
        if (LatinValue(r) != 0) return LatinClass(text, i);
        if (r == U'%' || r == U'％') return 'v';
        if ((r >= 0x3041 && r <= 0x30FA) || r == U'ー' || (r >= 0xFF66 && r <= 0xFF9F)) return 'v';
        if ((r >= 0x3400 && r <= 0x9FFF) || (r >= 0xF900 && r <= 0xFAFF) || r == U'々' || r == U'〆') {
            if (const auto reading = dict::Reading(r)) return HangulClass(text::Final(*reading));
            return 'v';
        }
        if (rune.size == 0 || rune.size > i) return 0;
        i -= rune.size;
    }
    return 0;
}

char ParticleClass(std::string_view text) { return ParticleClass(TextBefore(text)); }

std::span<const JosaForms, kJosaRows> JosaTable() { return std::span<const JosaForms, kJosaRows>(kJosaTable); }

std::string_view ParticleForm(int row, char final) {
    if ((final != 'c' && final != 'l') || (final == 'l' && row == 1)) return kJosaTable[row].vowel;
    return kJosaTable[row].consonant;
}

namespace internal {

std::string AlternateParticles(std::string_view text, std::string_view context) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '(') {
            if (const int row = JosaRow(text.substr(i)); row >= 0) {
                const std::string_view pattern = kJosaTable[row].pattern;
                char final = 0;
                if (!out.empty() || !Alphanumeric(context)) final = ParticleClass(TextBefore(context, out));
                if (final != 0) {
                    out += ParticleForm(row, final);
                } else {
                    out += pattern;
                }
                i += pattern.size();
                continue;
            }
        }
        out += text[i];
        i++;
    }
    return out;
}

char LatinWordClass(std::string_view letters) {
    char buf[kLatinWindow];
    size_t n = 0;
    for (size_t i = letters.size(); i > 0 && n < kLatinWindow && IsAlpha(letters[i - 1]); i--) n++;
    if (n == 0) return 'v';
    for (size_t k = 0; k < n; k++) buf[k] = ToLower(letters[letters.size() - n + k]);
    const std::string_view w(buf, n);
    // The letter k places before the last one, 0 before the word.
    auto at = [&](size_t k) { return k < n ? w[n - 1 - k] : '\0'; };
    auto vowel = [&](size_t k) { return k < n && SpellsVowel(w, n - 1 - k); };
    const char last = at(0);
    switch (last) {
        case 'm': case 'n': return 'c';  // 룸, 톰, 레인, bomb 밤, sign 사인
        case 'l': return 'l';            // 폴, 툴, 쿨
        case 'g':
            // ng is 킹; bag and big keep the spellings 백 and 빅 (핸드백), other g take 으 (도그, 태그).
            if (at(1) == 'n') return 'c';
            return w.ends_with("bag") || w == "big" ? 'c' : 'v';
        case 'e':
            // e sounds after a vowel and in a word of two letters (트리, 블루, 미); otherwise
            // it is silent and the consonant before it decides: 게임, 라인, 애플, 실루엣, but
            // 케이스, 파이어, 사이트.
            if (n <= 2 || vowel(1)) return 'v';
            if (at(1) == 'l') return 'l';
            if (at(1) == 'm' || at(1) == 'n') return 'c';
            if (at(1) == 't' && at(2) == 't') return vowel(3) ? 'c' : 'v';
            return 'v';
        case 'b': case 'c': case 'k': case 'p': case 't':
            // A stop is written as a final consonant after a short vowel: a single vowel
            // letter with a consonant before it (캣, 봇, 웹, 매직), a doubled stop (앱, 맷), ck
            // (잭) and oo before k (북). After a vowel pair (보트, 위크) and after another
            // consonant (파트, 램프) it takes 으; mb is m (밤).
            if (last == 'b' && at(1) == 'm') return 'c';
            if (last == 'k' && at(1) == 'c') return vowel(2) ? 'c' : 'v';
            if (n >= 3 && at(1) == last) return vowel(2) ? 'c' : 'v';
            if (n <= 2 || !vowel(1)) return 'v';
            if (!vowel(2)) return 'c';
            return last == 'k' && at(1) == 'o' && at(2) == 'o' ? 'c' : 'v';
    }
    return 'v';  // vowels, r, s, x, z, f, v, d, h, w, y and the rest: 카, 버스, 레드, 쇼
}

bool EndingConsonant(std::string_view tail) {
    if (tail.empty()) return false;
    const char a1 = tail.back();
    if (IsDigit(a1)) {
        switch (a1) {
            case '0': case '1': case '3': case '6': case '7': case '8': return true;
        }
        return false;
    }
    return LatinWordClass(tail) != 'v';
}

}  // namespace internal

}  // namespace hdTrans::output
