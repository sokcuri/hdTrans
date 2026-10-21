// Writing the composed items into their token texts, the spacing rules, and the output
// of a sentence.
#include <algorithm>

#include "core/dict/char_readings.h"
#include "core/output/cleanup.h"
#include "core/output/compose.h"
#include "core/output/particle.h"
#include "core/output/text.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/tokenizer/canonical.h"
#include "core/tokenizer/classify.h"

namespace hdTrans::output {

namespace {

using internal::At;
using internal::Contains;
using internal::First;
using internal::Selected;
using internal::WidthOf;

constexpr std::string_view kAsciiBlockedSymbols = "`~!@#$%^&*()_+-={}[]:;\"'|\\<>,.?/";
constexpr std::string_view kBlockedSymbolSet = "‘￣！＠＃＄％＾＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・";
constexpr std::string_view kNumeralDigits[] = {"○", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
constexpr std::string_view kNumeralUnits[] = {"十", "百", "千", "万", "億"};

// The symbol set read from inside one of its characters: decoded from each odd byte
// offset of its Shift-JIS form.
constexpr std::string_view kBlockedMisreads[] = {
    "e￣！＠＃＄％＾＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "P！＠＃＄％＾＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "I＠＃＄％＾＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "浴煤吹刀O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "煤吹刀O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "吹刀O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "刀O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "O＆＊（）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "普磨i）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "磨i）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "i）＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "j＿ー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "Qー＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "[＋＝｛「｝」：；”’｜￥＜、＞。？・",
    "{＝｛「｝」：；”’｜￥＜、＞。？・",
    "＝o「｝」：；”’｜￥＜、＞。？・",
    "o「｝」：；”’｜￥＜、＞。？・",
    "u｝」：；”’｜￥＜、＞。？・",
    "p」：；”’｜￥＜、＞。？・",
    "v：；”’｜￥＜、＞。？・",
    "F；”’｜￥＜、＞。？・",
    "G”’｜￥＜、＞。？・",
    "h’｜￥＜、＞。？・",
    "f｜￥＜、＞。？・",
    "b￥＜、＞。？・",
    "潤メA＞。？・",
    "メA＞。？・",
    "A＞。？・",
    "пB？・",
    "B？・",
    "H・",
    "E",
};

// One-kana particles that close a sentence; one left alone after a pause (静かだ……な)
// is not written.
constexpr std::string_view kFinalParticles = "なねよさわぞぜかの";

std::string_view EntryText(const ResolvedItem& it) { return Selected(it).text; }
std::string_view Code(const ResolvedItem& it) { return Selected(it).code; }
std::string_view GAttr(const ResolvedItem& it) { return Selected(it).gattr; }

int ClassifySelected(const ResolvedItem& it) {
    const std::string_view g = GAttr(it);
    if (Contains(g, "Z6")) return 500;
    if (Contains(g, "Z7")) return 510;
    return 0;
}

char32_t FirstRune(std::string_view s) { return text::DecodeRune(s).cp; }

// Whether an adverb is a mimetic said again right after itself (そぼそぼ そぼそぼ): the
// same surface and Korean, a Korean of two equal halves (부슬부슬).
bool RepeatedMimetic(const ResolvedItem& a, const ResolvedItem& b) {
    const std::string_view ko = EntryText(a);
    const size_t half = ko.size() / 2;
    return First(Code(a)) == '3' && First(Code(b)) == '3' && a.surface == b.surface && ko == EntryText(b) &&
           half > 0 && ko.size() % 2 == 0 && ko.substr(0, half) == ko.substr(half);
}

bool HasRune(std::u32string_view set, char32_t r) { return set.find(r) != std::u32string_view::npos; }

// A character after which a word may start: a space, punctuation other than a closing
// bracket, or a symbol such as ♪.
bool Closes(char32_t r) { return text::IsClosePunctuation(r); }
bool Opens(char32_t r) { return text::IsSpace(r) || (text::IsPunct(r) && !Closes(r)) || text::IsOtherSymbol(r); }

bool IsKanji(char32_t r) {
    return (r >= 0x4E00 && r <= 0x9FFF) || (r >= 0x3400 && r <= 0x4DBF) || (r >= 0xF900 && r <= 0xFAFF) || r == U'々';
}

// Hiragana, katakana and ー.
bool IsKanaRune(char32_t r) { return (r >= U'ぁ' && r <= U'ゖ') || (r >= U'ァ' && r <= U'ヺ') || r == U'ー'; }

// The bytes of the kana that start s.
size_t KanaPrefix(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        if (!IsKanaRune(r.cp)) return i;
        i += r.size;
    }
    return s.size();
}

bool Blank(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        if (!text::IsSpace(r.cp) || (r.cp == text::kReplacement && r.size == 1)) return false;
        i += r.size;
    }
    return true;
}

// An item that no dictionary word reads its text as: its surface is the text, and the
// text is one kana or none of its entries holds Hangul.
bool Unread(const ResolvedItem& it, std::string_view text) {
    if (it.surface != text) return false;
    if (text::RuneCount(text) == 1) return true;
    if (internal::HasSyllable(it.first.text)) return false;
    if (it.selected > 0 && internal::HasSyllable(it.entry.text)) return false;
    return !it.otherHangul;
}

// b-a not counting the opaque span tokens between them.
int TokenDistance(const std::vector<TokenText>& tokens, int a, int b) {
    int d = b - a;
    for (int i = a + 1; i < b; i++) {
        if (tokens[i].span != 0 && tokens[i].type == tokenizer::kTypeMarkup) d--;
    }
    return d;
}

// The token types that are written without a space next to a word span when the input
// has none: ASCII letters and digits and other word spans.
bool KeepsAdjacency(int type) {
    switch (type) {
        case tokenizer::kTypeNumber: case tokenizer::kTypeAlpha: case 15: case 16: case tokenizer::kTypeProtected:
            return true;
    }
    return false;
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

std::string_view TextTail(const Pieces& p) {
    return !p.empty() && p.back().span == SpanText ? std::string_view(p.back().text) : std::string_view();
}

}  // namespace

bool IsOutputBuilderBlockedText(std::string_view text) {
    if (text.empty()) return false;
    if (kAsciiBlockedSymbols.find(text[0]) != std::string_view::npos) return true;
    if (text == "（" || text == "）" || text == "？" || text == "。" || text == "「" || text == "」") return true;
    if (kBlockedSymbolSet.find(text) != std::string_view::npos) return true;
    for (const auto m : kBlockedMisreads) {
        if (m.starts_with(text)) return true;
    }
    return false;
}

std::string FilterKana(std::string_view text, const Settings& s) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const auto rune = text::DecodeRune(text.substr(i));
        i += rune.size;
        char32_t r = rune.cp;
        if (r < 0x80) {
            out += static_cast<char>(r);
            continue;
        }
        const int type = tokenizer::Class(r);
        if (type == tokenizer::kTypeJapanese && s.hanjaToHangul == 1) {
            if (const auto w = dict::Written(r)) r = *w;
            text::AppendRune(out, r);
        } else if (type == tokenizer::kTypeJapanese ||
                   (type != tokenizer::kClassHiragana && type != tokenizer::kClassKatakana && r != U'、')) {
            text::AppendRune(out, r);
        } else if (s.deleteJapan != 1 || r == U'・' || r == U'、') {
            // The canonical form cut to two width units; a shorter form ends the text.
            std::string canonical;
            if (const auto f = tokenizer::CanonicalForm(r)) {
                canonical = *f;
            } else {
                text::AppendRune(canonical, r);
            }
            const std::string_view form = internal::CutFirst(canonical, 2).head;
            out += form;
            if (WidthOf(form) < 2) return out;
        }
    }
    return out;
}

std::string EncodeNumericPair(std::string_view text) {
    if (static_cast<unsigned char>(First(text)) < 0x80) return std::string(text);
    std::vector<std::string_view> rs;
    for (size_t i = 0; i < text.size();) {
        const size_t n = text::DecodeRune(text.substr(i)).size;
        rs.push_back(text.substr(i, n));
        i += n;
    }
    // The digit value of character i in set; past the end of text it is 0.
    auto position = [&](std::span<const std::string_view> set, size_t i) {
        if (i >= rs.size()) return 0;
        for (size_t k = 0; k < set.size(); k++) {
            if (set[k] == rs[i]) return static_cast<int>(k);
        }
        return -1;
    };
    if (position(kNumeralUnits, 0) != -1) {
        if (const int p = position(kNumeralDigits, 1); p != -1) return std::string{'1', static_cast<char>(p + '0')};
        return "10";
    }
    std::string digits(1, static_cast<char>(std::max(position(kNumeralDigits, 0), 0) + '0'));
    if (rs.size() > 1 && position(kNumeralUnits, 1) != -1) {
        if (const int p = position(kNumeralDigits, 2); p != -1) digits += static_cast<char>(p + '0');
    }
    return digits;
}

void Composer::Start(std::vector<ResolvedItem>& items, const Settings& settings, const Families& generated) {
    items_ = &items;
    settings_ = settings;
    generated_ = &generated;
    state_.resize(items.size());
    for (auto& s : state_) {
        s.text.clear();
        s.saved.clear();
    }
    written_.clear();
    inflected.clear();
}

void Composer::Compose(std::vector<ResolvedItem>& items, std::vector<TokenText>& tokens, int first, int last,
                       std::span<tokenizer::ControlMarker> markers, const Settings& settings,
                       const Families& generated, Pieces& out) {
    BuildItems(items, tokens, nodes_);
    FinalOutputPrepass(items, nodes_, tokens, first, last, markers, settings, generated);
    EmitSynchronousOutput(tokens, first, last, settings, out);
}

void Composer::FinalOutputPrepass(std::vector<ResolvedItem>& items, const Nodes& nodes,
                                  std::vector<TokenText>& tokens, int first, int last,
                                  std::span<tokenizer::ControlMarker> markers,
                                  const Settings& settings, const Families& generated) {
    Start(items, settings, generated);
    tokens_ = &tokens;
    if (settings.text) {
        if (source_.size() < tokens.size()) source_.resize(tokens.size());
        for (int i = first; i <= last; i++) source_[i] = tokens[i].text;
    }
    for (int n = static_cast<int>(nodes.size()) - 1; n >= 0; n--) {
        const int begin = n > 0 ? nodes[n - 1] : 0;
        for (int index = nodes[n] - 1; index >= begin; index--) WriteItem(index, index == nodes[n] - 1, markers);
    }
}

void Composer::WriteItem(int i, bool isLast, std::span<tokenizer::ControlMarker> markers) {
    std::vector<ResolvedItem>& items = *items_;
    std::vector<TokenText>& tokens = *tokens_;
    const ResolvedItem& item = items[i];
    const int next = i + 1 < static_cast<int>(items.size()) ? i + 1 : -1;
    const int previous = i - 1;
    const std::string tags(GAttr(item));
    const char kind = First(Code(item));
    std::string nextTags;
    char nextKind = ' ';
    if (next >= 0) {
        nextTags = GAttr(items[next]);
        nextKind = First(Code(items[next]));
    }
    const std::string& tokenText = tokens[item.tokenIndex].text;
    const std::string prefix = tokenText.substr(0, std::min<size_t>(static_cast<size_t>(item.offset), tokenText.size()));
    std::string remainder;
    bool converted = false;
    if (const size_t end = static_cast<size_t>(item.offset + item.length); end < tokenText.size()) {
        remainder = tokenText.substr(end);
        if (std::string_view(remainder).starts_with("、")) {
            converted = true;
            remainder = FilterKana(remainder, settings_);
        }
    }
    std::string translated = ApplyParenthesisTextFilter(Build(i, 0, {}), i);
    const bool protectedToken = tokens[item.tokenIndex].type == tokenizer::kTypeProtected;
    if (settings_.cleanup && !protectedToken) RemoveGlosses(translated);
    std::string composed;
    if (record) composed = translated;
    auto [spaced, space] = Spacing(i, std::move(translated), tags, nextTags, kind, nextKind, isLast, converted, remainder);
    if (record) inflected.push_back({i, std::move(composed), spaced, space});
    if (protectedToken) {
        spaced = tokenText;
    } else if (settings_.text) {
        spaced = KeepDropped(i, std::move(spaced), remainder);
    }
    if (space) spaced += ' ';
    for (auto& m : markers) {
        if (m.markerKind == 0 || item.tokenIndex != m.tokenIndex) continue;
        const std::string_view marker = m.markerKind == 1 ? "&#CS" : "</a>";
        if (item.offset != m.textLength) {
            if (item.offset <= m.textLength) {
                spaced += marker;
                m.markerKind = 0;
                continue;
            }
            if (previous < 0 || items[previous].offset >= m.textLength) continue;
        }
        spaced.insert(0, marker);
        m.markerKind = 0;
    }
    std::string& left = tokens[item.tokenIndex].text;
    left.clear();
    left += prefix;
    left += spaced;
    left += remainder;
}

std::string Composer::KeepDropped(int i, std::string translated, std::string_view remainder) {
    const ResolvedItem& item = (*items_)[i];
    const int token = item.tokenIndex, start = item.offset;
    const std::string_view src = source_[token];
    const int end = start + item.length;
    if (end > static_cast<int>(src.size())) return translated;
    const bool blank = Blank(translated);
    const bool empty = blank && Unread(item, src.substr(start, end - start));
    auto find = [&](int pos) -> const Written* {
        for (const Written& w : written_) {
            if (w.token == token && w.start == pos) return &w;
        }
        return nullptr;
    };
    if (Written* w = const_cast<Written*>(find(start))) {
        *w = {token, start, end, blank, empty};
    } else {
        written_.push_back({token, start, end, blank, empty});
    }
    if (!empty || KanaPrefix(src.substr(start, end - start)) != static_cast<size_t>(end - start)) return translated;
    const TextBefore before_(src.substr(0, start));
    const auto beforeRune = DecodeLastRune(before_, start);
    const char32_t before = beforeRune.cp;
    if (start > 0 && !Opens(before)) return translated;
    const int runEnd = end + static_cast<int>(KanaPrefix(src.substr(end)));
    int pos = end;
    bool particle = false;  // the run ends before a written one-kana particle
    while (pos < runEnd) {
        const Written* next = find(pos);
        if (next != nullptr && !next->blank && next->end <= runEnd && settings_.text &&
            text::RuneCount(src.substr(start, pos - start)) >= 2 &&
            text::RuneCount(src.substr(pos, next->end - pos)) == 1) {
            particle = true;
            break;
        }
        if (next == nullptr || (!next->blank && next->end <= runEnd)) return translated;
        if (!next->empty) break;
        pos = next->end;
    }
    const std::string_view run = src.substr(start, pos - start);
    const char32_t after = FirstRune(src.substr(pos));
    bool kanjiBefore = false;
    if (start > 0) {
        const std::string_view head = src.substr(0, start - beforeRune.size);
        kanjiBefore = IsKanji(DecodeLastRune(TextBefore(head), head.size()).cp);
    }
    const bool alone = settings_.text && pos == runEnd &&
                       (pos == static_cast<int>(src.size()) || Closes(after) || HasRune(U"…‥！？!?", after)) &&
                       kFinalParticles.find(run) == std::string_view::npos;
    bool onlySmall = true;  // run is only っ, ッ and ー
    for (size_t k = 0; k < run.size();) {
        const auto r = text::DecodeRune(run.substr(k));
        if (r.cp != U'っ' && r.cp != U'ッ' && r.cp != U'ー') onlySmall = false;
        k += r.size;
    }
    if ((text::RuneCount(run) < 2 && !alone) || onlySmall ||
        ((run.ends_with("っ") || run.ends_with("ッ")) && pos == runEnd && pos < static_cast<int>(src.size()) &&
         !Opens(after) && !Closes(after)) ||
        (kanjiBefore && HasRune(U"(（《「", before) && HasRune(U")）》」", after))) {
        return translated;
    }
    std::string marked;
    for (size_t k = 0; k < run.size();) {
        const auto r = text::DecodeRune(run.substr(k));
        text::AppendRune(marked, r.cp + kDropShift);
        k += r.size;
    }
    if (const char32_t r = FirstRune(remainder); !particle && !remainder.empty() && (text::IsLetter(r) || text::IsDigit(r))) {
        marked += ' ';
    }
    while (!translated.empty() && translated.back() == ' ') translated.pop_back();
    translated += marked;
    return translated;
}

std::pair<std::string, bool> Composer::Spacing(int i, std::string t, std::string_view tags,
                                               std::string_view nextTags, char kind, char nextKind, bool isLast,
                                               bool converted, std::string_view remainder) const {
    const std::vector<ResolvedItem>& items = *items_;
    const std::vector<TokenText>& tokens = *tokens_;
    const ResolvedItem& item = items[i];
    const int count = static_cast<int>(items.size());
    int nextIndex = i + 1;
    if (kind == '3') {
        // Omitted trailing particles do not erase an adverb's word boundary.
        while (nextIndex < count && items[nextIndex].tokenIndex == item.tokenIndex &&
               First(Code(items[nextIndex])) == '2' && EntryText(items[nextIndex]).empty()) {
            nextIndex++;
        }
        if (nextIndex != i + 1) {
            isLast = true;
            if (nextIndex < count) {
                nextTags = GAttr(items[nextIndex]);
                nextKind = First(Code(items[nextIndex]));
            }
        }
    }
    if (nextIndex == count) nextIndex = -1;
    const int previous = i - 1;
    using Result = std::pair<std::string, bool>;
    if (nextIndex < 0 || t.empty() || item.selectionFlag == 1 ||
        TokenDistance(tokens, item.tokenIndex, items[nextIndex].tokenIndex) >= 2) {
        return Result(std::move(t), false);
    }
    const ResolvedItem& next = items[nextIndex];
    if (const int a = tokens[item.tokenIndex].type, b = tokens[next.tokenIndex].type;
        (a == tokenizer::kTypeProtected || b == tokenizer::kTypeProtected) && KeepsAdjacency(a) && KeepsAdjacency(b)) {
        return Result(std::move(t), false);
    }
    auto nd = [](std::string_view s) { return Contains(s, "Nd") || Contains(s, "Nj"); };
    if (item.surface == "上" && previous >= 0 && EntryText(items[previous]).empty()) t.insert(0, " ");
    bool allowSpacing = true;
    if (t == "\\") {
        if (previous >= 0 && nd(internal::EntryAt(items[previous], 0).gattr)) {
            t = "Y";
            allowSpacing = false;
        }
        if (t == "\\" && nd(nextTags)) return Result("Y", false);
        if (!allowSpacing) return Result(std::move(t), false);
    }
    const std::string_view nextEntry = EntryText(next);
    if (RepeatedMimetic(item, next)) return Result(std::move(t), false);  // 부슬부슬부슬부슬
    if (nextEntry == "「") return Result(std::move(t), true);
    if (!allowSpacing || nextEntry == "%") return Result(std::move(t), false);
    if (!Contains(nextTags, "Tu")) {
        if (Contains(nextTags, "Ha")) return Result(std::move(t), true);
    } else if (t != ",") {
        return Result(std::move(t), false);
    }
    if (Contains(tags, "Wa") || Contains(tags, "Wb")) {
        if ((Contains(nextTags, "Wa") || Contains(nextTags, "Wb")) && !Contains(nextTags, "Z7")) {
            return Result(std::move(t), true);
        }
        if (nextKind == '1' && WidthOf(nextEntry) > 2) return Result(std::move(t), true);
    }
    if (kind == '1' && At(Code(item), 1) == '3' && nextKind == '1' && At(Code(next), 1) == '3') {
        return Result(std::move(t), true);
    }
    if (Contains(tags, "Z7") && nextKind == '1') {
        if (WidthOf(nextEntry) > 2) return Result(std::move(t), true);
        for (const std::string_view p : {"お", "ご", "御"}) {
            if (next.surface == p) return Result(std::move(t), true);
        }
    }
    if (Contains(nextEntry, "」") || (!nextEntry.empty() && Closes(FirstRune(nextEntry)))) {
        return Result(std::move(t), false);
    }
    if (nextKind != '2') {
        if (Contains(tags, "Bt")) return Result(std::move(t), false);
        if (Contains(tags, "Bh")) return Result(std::move(t), true);
    }
    if (kind == '1' && nextKind == '1' && Contains(tags, "Tm")) return Result(std::move(t), true);
    if (!isLast) {
        if (kind != '1' || WidthOf(t) != 2) return Result(std::move(t), false);
        if (nextKind != '1' || WidthOf(nextEntry) <= 2) return Result(std::move(t), false);
        const bool space = Contains(tags, "Z7") || !Contains(tags, "Z6");
        return Result(std::move(t), space);
    }
    switch (kind) {
        case '0': case '2':
            return Result(std::move(t), nextKind != '2' && nextKind != '6');
        case '1': {
            const TokenText& tok = tokens[item.tokenIndex];
            if (t == "(" || tok.span != 0 || WidthOf(tok.text) == 1 || nextKind == '2') return Result(std::move(t), false);
            if (nextKind == '6') return Result(std::move(t), nd(nextTags));
            const std::string_view nextFirst = internal::EntryAt(next, 0).text;
            if (nextFirst == "(") break;
            if (nextFirst == ")") return Result(std::move(t), false);
            if (Contains(tags, "Wa") || Contains(tags, "Wb")) {
                const bool space = Contains(tags, "Wb") && nextKind != '1';
                return Result(std::move(t), space);
            }
            if (IsDigit(First(t))) {
                if (nextKind == '1') return Result(std::move(t), !Contains(nextTags, "Ct"));
                if (item.tokenIndex - 1 >= 0 && previous >= 0 && tokens[next.tokenIndex].type != 15 &&
                    tokens[next.tokenIndex].type != tokenizer::kTypeNumber &&
                    item.tokenIndex - items[previous].tokenIndex == 1) {
                    t.insert(0, " ");
                }
                // A number is written apart from a following one-word predicate (12 올랐다);
                // a counter (3개) and a predicate that starts with the noun it counts
                // (3시간 들였다) stay attached.
                return Result(std::move(t), nextKind == '4' && nextEntry.find(' ') == std::string_view::npos);
            }
            if (nd(nextTags)) return Result(std::move(t), true);
            if (WidthOf(t) == 2 && previous >= 0) {
                if ((nd(GAttr(items[previous])) && Contains(tags, "Ct")) || nd(tags)) return Result(std::move(t), true);
            }
            if (tokenizer::Class(FirstRune(item.surface)) == tokenizer::kClassKatakana) return Result(std::move(t), true);
            const bool spaced = !converted && (WidthOf(t) > 2 || nextKind != kind);
            if (Contains(nextEntry, "&AV@") || Contains(nextEntry, "&C6@") || Contains(nextEntry, "%A1@") ||
                Contains(nextEntry, "%D5@") || Contains(nextEntry, "$AB@") || Contains(nextEntry, "$AE@")) {
                return Result(std::move(t), spaced);
            }
            if (ClassifySelected(item) == 510) {
                t += ' ';
                return Result(std::move(t), false);
            }
            return Result(std::move(t), spaced);
        }
        case '3': case '4': case '5': case '7': case '8': case '9': {
            const char np = First(Code(next));
            const bool space = np != '2' && np != '6' && (!IsOutputBuilderBlockedText(remainder) || WidthOf(remainder) > 4);
            return Result(std::move(t), space);
        }
        case '6':
            return Result(std::move(t), t == "," || (t == ")" && nextKind != '2' && nextKind != '6'));
    }
    return Result(std::move(t), false);
}

void EmitSynchronousOutput(const std::vector<TokenText>& tokens, int first, int last, const Settings& s, Pieces& out) {
    out.clear();
    std::string text;
    for (int i = first; i <= last; i++) {
        const TokenText& tok = tokens[i];
        switch (tok.type) {
            case tokenizer::kTypeNumber: case tokenizer::kTypeAlpha: case 15: case 16: case tokenizer::kTypeSymbol:
            case tokenizer::kTypeJapanese: case tokenizer::kTypeProtected:
                break;
            default:
                if (tok.span == 0 && !out.empty() && i - 1 >= 0 && tokens[i - 1].type != tokenizer::kTypeAsciiSpace) {
                    TrimRight(out);
                }
        }
        if (const std::string_view t = TextTail(out); t.ends_with("</A>") || t.ends_with("</a>") || t.ends_with("&#CS")) {
            Add(out, " ");
        }
        if (tok.span != 0) {
            std::string extra = tok.text.substr(std::min(tok.spanText.size(), tok.text.size()));
            if (const std::string_view t = TextTail(out); tok.span == SpanClose && i > first &&
                                                         tokens[i - 1].type != tokenizer::kTypeAsciiSpace &&
                                                         t.ends_with(" ")) {
                std::string head(t.substr(0, t.size() - 1));
                out.pop_back();
                Add(out, head);
                extra += ' ';
            }
            out.push_back({tok.spanText, static_cast<SpanKind>(tok.span)});
            Add(out, extra);
            continue;
        }
        text = tok.text;
        if (s.cleanup) {
            if (tok.type == tokenizer::kTypeAsciiSpace) {
                TrimRight(out);
            } else if (tok.type != tokenizer::kTypeMarkup) {
                SqueezeSpaces(text);
                if (TextTail(out).ends_with(" ")) text.erase(0, std::min(text.find_first_not_of(' '), text.size()));
            }
        }
        Add(out, text);
    }
    ApplyOutputPostSpacing(tokens, last, out);
}

void ApplyOutputPostSpacing(const std::vector<TokenText>& tokens, int index, Pieces& out) {
    const TokenText& tok = tokens[index];
    if (tok.type == tokenizer::kTypeWideSpace || tok.type == 78 || tok.span != 0 ||
        (tok.text != "." && tok.text != "!" && tok.text != "?")) {
        return;
    }
    const size_t next = static_cast<size_t>(index) + 1;
    if (next >= tokens.size()) return;
    switch (tokens[next].type) {
        case tokenizer::kTypeAsciiPunct: case tokenizer::kTypeNewline: case tokenizer::kTypeEnd:
        case tokenizer::kTypeSentenceEnd: case tokenizer::kTypeTruncated: case tokenizer::kTypeWideSpace: case 78:
            return;
    }
    if (tokens[next].span != 0 || !std::string_view(tokens[next].text).starts_with("」")) Add(out, " ");
}

}  // namespace hdTrans::output
