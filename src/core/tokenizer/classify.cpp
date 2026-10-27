#include "core/tokenizer/classify.h"

#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/tokenizer/canonical.h"
#include "core/tokenizer/charsets.h"
#include "core/tokenizer/token.h"

namespace hdTrans::tokenizer {

int Class(char32_t c) { return ClassFlags(c, 15); }

int ClassFlags(char32_t c, int flags) {
    if (c < 0x2E80) return 0;
    if (c >= U'ぁ' && c <= U'ゖ') return (flags & 2) != 0 ? kClassHiragana : 0;
    if (c >= U'ァ' && c <= U'ヺ') return (flags & 4) != 0 ? kClassKatakana : 0;
    if (c == U'ー') {
        if ((flags & 4) != 0) return kClassKatakana;
        return (flags & 8) != 0 ? kTypeJapanese : 0;
    }
    if (c == U'・' || c == U'々') return (flags & 8) != 0 ? kTypeJapanese : 0;
    if ((flags & 1) != 0 && text::IsKanji(c) && !IsNonKanjiHan(c)) return kTypeJapanese;
    return 0;
}

bool Foreign(char32_t c) {
    if (c < 0x80 || (c >= 0xFF61 && c <= 0xFF9F) || (c >= U'ぁ' && c <= U'ん') || (c >= U'ァ' && c <= U'ヺ') ||
        c == U'ー' || text::IsKanji(c)) {
        return false;
    }
    return !IsCp932Symbol(c);
}

bool SignedNumber(std::string_view s) {
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) s.remove_prefix(1);
    if (s.empty() || !digit(s.front())) return false;
    const char last = s.back();
    if (last == '%' || last == '$') s.remove_suffix(1);
    else if (!digit(last)) return false;
    return s.find_first_not_of("0123456789.,-~+") == std::string_view::npos;
}

void AppendCanonical(std::string& out, char32_t c) {
    if (const auto form = CanonicalForm(c)) {
        out.append(*form);
    } else {
        text::AppendRune(out, c);
    }
}

}  // namespace hdTrans::tokenizer
