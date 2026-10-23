// Dictionary lookups of the word stage and the filters on their candidates.
#include <cstdint>
#include <algorithm>

#include "core/text/utf8.h"
#include "core/text/unicode.h"
#include "core/tokenizer/classify.h"
#include "core/word/validity_rules.h"
#include "core/word/conjugation.h"
#include "core/word/kana.h"
#include "core/word/strings.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

// The particles after which a kana spelling may start in a run of hiragana
// (ゆめをみて, ほしのかず), and those that may follow a kana spelling of a word that
// does not conjugate (ゆめを, なみだが).
constexpr std::string_view kKanaBefore = "をにがはもでとへの";
constexpr std::string_view kKanaAfter = "がをにはもへのと";
// The particles that may follow a name key that ends in hiragana.
constexpr std::string_view kNameParticles = "はがをのにへともやで";

bool IsKanaInflection(std::string_view c) { return !c.empty() && Has("いうえけるれろ", c); }
bool IsNameParticle(std::string_view c) { return !c.empty() && Has(kNameParticles, c); }

// s without the leading characters of set.
std::string_view TrimLeftChars(std::string_view s, std::string_view set) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.cp == text::kReplacement && r.size == 1) break;
        bool in = false;
        for (std::string_view t = set; !t.empty();) {
            const text::Rune c = text::DecodeRune(t);
            if (c.cp == r.cp) in = true;
            t.remove_prefix(c.size);
        }
        if (!in) break;
        s.remove_prefix(r.size);
    }
    return s;
}

// Whether key ends like the imperative of a godan verb (落ち着け, 黙れ).
bool ERowImperative(std::string_view key) {
    for (const std::string_view k : {"け", "げ", "せ", "ね", "べ", "め", "れ", "え"}) {
        if (key.ends_with(k)) return true;
    }
    return false;
}

// A plain interjection row in て, an imperative (待て 기다려), that query continues as
// a verb: with ない (待てない) or a quoting って (待てって); or an e-row imperative
// continued by hiragana other than よ, っ and ぇ (落ち着ける).
bool BoundImperative(const dict::UserEntry& u, std::string_view query) {
    if (!u.codes->partText.starts_with("E") || !query.starts_with(u.key)) return false;
    const std::string_view after = query.substr(u.key.size());
    if (u.key.ends_with("て")) return after.starts_with("な") || after.starts_with("って");
    return ERowImperative(u.key) && NameClassOf(FirstChar(after)) == kClassHiragana && !after.starts_with("よ") &&
           !after.starts_with("っ") && !after.starts_with("ぇ");
}

// The character c, before or after a word, ends the utterance: none, or punctuation,
// a bracket or a space.
bool UtteranceBoundary(std::string_view c) {
    return c.empty() || (NameClassOf(c) == kClassOther && !(c.size() == 1 && (IsAlpha(c[0]) || IsDigit(c[0]))));
}

// The first Hangul syllable of s, "" when it has none.
std::string_view FirstHangul(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        if (r.cp >= U'가' && r.cp <= U'힣') return s.substr(i, r.size);
        i += r.size;
    }
    return {};
}

bool HangulSyllable(char32_t c) { return c >= 0xAC00 && c <= 0xD7A3; }

// Letters with at least one lower case, spaces, - . and '.
bool LatinGloss(std::string_view s) {
    bool lower = false;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        const char32_t c = r.cp;
        if (c >= 'a' && c <= 'z') {
            lower = true;
        } else if (!((c >= 'A' && c <= 'Z') || c == ' ' || c == '-' || c == '.' || c == '\'')) {
            return false;
        }
        s.remove_prefix(r.size);
    }
    return lower;
}

// The parenthesised parts of a Korean target that alternate with the consonant before
// them ((이)라고, (으)로).
bool ParticleAlternation(std::string_view note) {
    for (const std::string_view p : {"이", "을", "은", "으", "와", "과", "이었", "이에", "이어", "로", "로서", "오"}) {
        if (note == p) return true;
    }
    return false;
}

bool HasHangulSyllable(std::string_view s) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (HangulSyllable(r.cp)) return true;
        s.remove_prefix(r.size);
    }
    return false;
}

struct SkipContext {
    const Worker* w;
    std::string_view query;
};

// An i-adjective row with かと after the ない of じゃない or ではない: that ない asks
// whether it is so (行為なんじゃないかと 행위가 아닐까), so the guess or belief of the row
// (높을 것 같습니다, 높은 줄 알았다) would turn it around.
bool AskedNegativeGuess(const dict::UserEntry& u, std::string_view left, std::string_view query) {
    if (At(u.codes->partText, 0) != 'T' || u.key.find("かと") == std::string_view::npos || left.size() < query.size()) {
        return false;
    }
    const std::string_view before = left.substr(0, left.size() - query.size());
    return before.ends_with("じゃな") || before.ends_with("ではな");
}

// An i-adjective row in いことはない or いことは無い after a stem that is no negative in な: the
// row reads a double negative (知らないことはない 모르는 것은 아니다), while after another
// adjective ことはない is a superlative (こんなに嬉しいことはない 이렇게 기쁜 일은 없다).
bool PositiveDoubleNegative(const dict::UserEntry& u, std::string_view left, std::string_view query) {
    if (At(u.codes->partText, 0) != 'T' || !u.key.starts_with("いことは") || left.size() < query.size()) return false;
    return !left.substr(0, left.size() - query.size()).ends_with("な");
}

}  // namespace

bool NounBefore(std::string_view text) {
    // Pronouns and nouns that end in hiragana (予定通り).
    static constexpr std::string_view kPronouns[] = {"これ", "それ", "あれ", "どれ", "ここ", "そこ", "あそこ",
                                                     "どこ", "だれ", "みんな", "こいつ", "そいつ", "あいつ", "わたし",
                                                     "あたし", "あなた", "おれ", "ぼく", "きみ", "かれ", "かのじょ",
                                                     "通り", "どおり"};
    const text::Rune last = DecodeLastRune(text);
    const char32_t r = last.cp;
    if (r >= 0x3041 && r <= 0x309F) {
        for (const std::string_view p : kPronouns) {
            if (text.ends_with(p)) return true;
        }
        return false;
    }
    if (r == text::kReplacement) return false;
    return (!text::IsSpace(r) && !text::IsPunct(r)) || r == U'」' || r == U'』' || r == U'）' || r == U')';
}

uint8_t Worker::Register(const dict::UserEntry& u) const { return u.polite ? settings.registers[1] : settings.registers[0]; }

Lookup Worker::DoLookup(std::string_view query, int mode) {
    if (fault) return {};
    dict::LookupContext ctx = lookupContext;
    SkipContext sc{this, query};
    ctx.skip.fn = [](const void* c, const dict::UserEntry& e) {
        const SkipContext* s = static_cast<const SkipContext*>(c);
        return s->w->Skip(e, s->query);
    };
    ctx.skip.context = &sc;
    dict::Candidates found = snapshot.GetWordInfo(query, static_cast<dict::LookupMode>(mode), ctx);
    if (!found.names.empty()) {
        if (arena.namesUsed == arena.names.size()) arena.names.emplace_back();
        arena.names[arena.namesUsed++].swap(found.names);
    }
    if (mode == kModeGeneral && settings.text) ApplyConstraints(found, query);
    Lookup l;
    l.count = found.count;
    Record* last = nullptr;
    for (const dict::Candidate& c : found.list) {
        Record* r = NewRecord();
        r->source = c.source;
        r->target.assign(c.target);
        if (settings.improvements.imperativeEu && c.user != nullptr && c.user->euImperative) {
            r->target[0] = '2';
        }
        r->partCode = c.codes->partText;
        r->semantic = c.codes->sem.Text();
        r->attr.assign(c.codes->gram.Text());
        r->dictCode = c.codes->dict.Text();
        r->ruleNo = c.rule;
        r->id = c.id;
        r->user = c.user;
        if (last == nullptr) l.records = r;
        else last->child = r;
        last = r;
    }
    if (mode == kModeGeneral && settings.text) {
        DropBoundUtterances(l, query);
        DropNegatedAdverbs(l, query);
        DropFinalAdnominals(l, query);
    }
    if (settings.text) {
        DropModalSplits(l, query);
        TransliterateGlosses(l.records);
        if (fault) return {};
    }
    if (observer.fn != nullptr) observer.fn(observer.context, current, query, mode, l.records);
    return l;
}

bool Worker::Skip(const dict::UserEntry& u, std::string_view query) const {
    return !EndRowFits(u, query) || (u.nameLayer && !KanaBounded(u.key, query)) || (u.kana && !KanaWord(u, query)) ||
           (settings.text && BoundImperative(u, query));
}

// Notes in parentheses are dropped from the Korean of base nouns (아바타(Avatar) 아바타, 틈(간격)
// 틈, but not the particle alternations (이)라고), and a katakana source is written in
// Hangul where the Korean is a Latin gloss (モンスター monster): letters with at least
// one lower case, so that acronyms (PC, TV) stay.
void Worker::TransliterateGlosses(Record* records) {
    for (Record* r = records; r != nullptr; r = r->child) {
        if (r->user != nullptr || r->source.empty()) continue;
        if (r->partCode.starts_with("N") && Has(r->target, "(")) {
            const std::string glossed = r->target;
            std::string out;
            std::size_t done = 0;
            for (std::size_t p = 0; p < glossed.size(); p++) {
                if (glossed[p] != '(') continue;
                std::size_t q = p + 1;
                while (q < glossed.size() && glossed[q] != '(' && glossed[q] != ')') q++;
                if (q == p + 1 || q >= glossed.size() || glossed[q] != ')') continue;
                const std::string_view m = std::string_view(glossed).substr(p, q + 1 - p);
                const std::size_t start = glossed.find(m);
                const std::size_t i = start + m.size();
                const std::string_view note = m.substr(1, m.size() - 2);
                bool drop = false;
                if (start != 0) {
                    drop = LatinGloss(note) || (HasHangulSyllable(note) && !ParticleAlternation(note) &&
                                                (i == glossed.size() || glossed[i] == ' '));
                }
                out.append(glossed, done, p - done);
                if (!drop) out.append(m);
                done = q + 1;
                p = q;
            }
            out.append(glossed, done, std::string::npos);
            r->target = std::move(out);
        }
        bool katakana = true;
        for (std::string_view s = r->source; !s.empty();) {
            const text::Rune c = text::DecodeRune(s);
            if (!IsKatakana(c.cp) && c.cp != U'・') katakana = false;
            s.remove_prefix(c.size);
        }
        if (LatinGloss(r->target) && katakana) {
            std::string english = r->target;
            for (char& c : english) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
            }
            std::string source(r->source);
            for (std::size_t k; (k = source.find("・")) != std::string::npos;) source.replace(k, std::string_view("・").size(), " ");
            if (!TransliterateKatakana(source, r->target)) {
                fault = true;
                return;
            }
            // a final ァー of an English -er or -or is 어 (モンスター 몬스터)
            if (r->source.ends_with("ー") && (english.ends_with("er") || english.ends_with("or")) && !r->target.empty()) {
                const text::Rune last = DecodeLastRune(r->target);
                if (HangulSyllable(last.cp) && (last.cp - 0xAC00) / 28 % 21 == 0) {
                    r->target.resize(r->target.size() - last.size);
                    text::AppendRune(r->target, last.cp + 4 * 28);
                }
            }
        }
    }
}

// End rows: an end row only with the predicate's register and, for a non-empty query,
// only when the end of the sentence follows its key. The copula of a noun row (P tag)
// also needs a word before it in the token, a particle row a noun, and must not trail
// off. The endings of an empty query follow a conjugation whose end the caller checks.
bool Worker::EndRowFits(const dict::UserEntry& u, std::string_view query) const {
    if (!u.end) return PatternFits(u, query);
    if (u.registerLevel != 0 && Register(u) != u.registerLevel) return false;
    if (query.empty()) return true;
    if (current < 0) return false;
    const std::string_view left = Tok(current).text;
    const std::string_view after = query.substr(std::min(u.key.size(), query.size()));
    if (AskedNegativeGuess(u, left, query)) return false;
    if (At(u.codes->partText, 0) == 'P') {
        const long before = static_cast<long>(left.size()) - static_cast<long>(query.size());
        auto wordBefore = [&]() {
            if (before < 1 || before > static_cast<long>(left.size())) return false;
            const text::Rune r = DecodeLastRune(left.substr(0, static_cast<std::size_t>(before)));
            if (Width(r.cp) == 2) {
                const char32_t c = r.cp;
                return (c >= U'０' && c <= U'９') || (c >= U'Ａ' && c <= U'Ｚ') || (c >= U'ａ' && c <= U'ｚ') ||
                       (c >= U'ぁ' && c <= U'ん') || (c >= U'ァ' && c <= U'ヶ') || (c >= U'Α' && c <= U'ω') ||
                       (tokenizer::ClassFlags(c, 1) == 24 && c != U'〇') || c == U'ー' || c == U'々';
            }
            const char b = left[static_cast<std::size_t>(before - 1)];
            return IsDigit(b) || IsAlpha(b);
        };
        if (!wordBefore()) return false;
        bool particleRow = true;
        for (const std::string_view c : {"だ", "で", "じゃ", "や"}) {
            if (u.key.starts_with(c)) particleRow = false;
        }
        if (particleRow && !NounBefore(left.substr(0, static_cast<std::size_t>(before)))) return false;
        std::string_view next = after;
        if (next.empty() && current + 1 < TokenCount() && Span(current + 1) == 0) next = NextText(current + 1);
        if (next.starts_with(kJaEllipsis) || next.starts_with(kJaTwoDots)) return false;
    }
    // やられたって…まだ: concessive; 長すぎるってぇ？: a question; 美味いぜ？ seeks agreement
    if ((u.key.ends_with("って") || u.key.ends_with("ぜ")) && TrailsOff(after)) return false;
    // 気のせいじゃないか？ asks (탓 아니야?); 楽じゃないか…… and 楽じゃないか！ do not (편하잖아).
    if (u.key.ends_with("じゃないか") && Asks(after)) return false;
    const bool closingParticle = u.key.ends_with("のう") || u.key.ends_with("のぅ");
    return SentenceFinal(after, current, closingParticle);
}

// An ellipsis or a question mark after rest (past small kana that lengthen it), or at
// the start of the next token when rest is empty.
bool Worker::TrailsOff(std::string_view rest) const {
    constexpr std::string_view kLengthen = "ぁぃぅぇぉっー";
    rest = TrimLeftChars(rest, kLengthen);
    if (rest.empty() && current + 1 < TokenCount() && Span(current + 1) == 0) {
        rest = TrimLeftChars(NextText(current + 1), kLengthen);
    }
    for (const std::string_view p : {kJaEllipsis, kJaTwoDots, std::string_view("？"), std::string_view("?")}) {
        if (rest.starts_with(p)) return true;
    }
    return false;
}

// A question mark after rest, past small kana and ellipses, or at the start of the next
// token when rest is empty.
bool Worker::Asks(std::string_view rest) const {
    constexpr std::string_view kTrail = "ぁぃぅぇぉっー…‥";
    rest = TrimLeftChars(rest, kTrail);
    if (rest.empty() && current + 1 < TokenCount() && Span(current + 1) == 0) rest = TrimLeftChars(NextText(current + 1), kTrail);
    return rest.starts_with("？") || rest.starts_with("?");
}

// Patterns and noun rows that are not end rows: a noun pattern needs a noun before
// it, and a pattern whose Korean ends in the terminal 다 needs the end of the sentence
// or a comma after it.
bool Worker::PatternFits(const dict::UserEntry& u, std::string_view query) const {
    const std::string_view rest = query.substr(std::min(u.key.size(), query.size()));
    // A key ending in からって does not take the quotation of a verb of saying or
    // thinking (だからって思う).
    if (u.key.ends_with("からって")) {
        for (const std::string_view s : {"言", "いう", "いっ", "いわ", "思", "おも", "聞"}) {
            if (rest.starts_with(s)) return false;
        }
    }
    const char tag = At(u.codes->partText, 0);
    if (!AnyOf("PSTU", tag) || query.empty() || current < 0) return true;
    // A key ending in で does not split です, でしょう, でございます, でも, では or である
    // (もんで, ことではない, ことであれば).
    if (u.key.ends_with("で")) {
        for (const std::string_view s : {"す", "し", "ご", "も", "は", "あ"}) {
            if (rest.starts_with(s)) return false;
        }
    }
    const std::string_view left = Tok(current).text;
    if (AskedNegativeGuess(u, left, query) || PositiveDoubleNegative(u, left, query)) return false;
    if (tag == 'P') {
        const long before = std::max(static_cast<long>(left.size()) - static_cast<long>(query.size()), 0L);
        if (!NounBefore(left.substr(0, static_cast<std::size_t>(before)))) return false;
    }
    if (!u.target.ends_with(kKoDa)) return true;
    return SentenceFinal(rest, current, false) || rest.starts_with(kJaComma) || rest.starts_with(kJaFullComma) ||
           rest.starts_with(",");
}

// A name-layer key, matched at query (the rest of the surface being split), fills its
// kana run.
bool Worker::KanaBounded(std::string_view key, std::string_view query) const {
    const int first = NameClassOf(FirstChar(key)), last = NameClassOf(LastChar(key));
    if (first == kClassHiragana || first == kClassKatakana) {
        const std::string_view surface = transformSurface;
        if (!surface.ends_with(query)) return false;
        const int prev = NameClassOf(LastChar(surface.substr(0, surface.size() - query.size())));
        if (prev == first || (first == kClassHiragana && prev == kClassKanji)) return false;
    }
    if (last == kClassHiragana || last == kClassKatakana) {
        const std::string_view next = FirstChar(query.substr(std::min(key.size(), query.size())));
        const int c = NameClassOf(next);
        if ((c == last && (c == kClassKatakana || !IsNameParticle(next))) ||
            (last == kClassKatakana && IsKanaInflection(next))) {
            return false;
        }
    }
    return true;
}

// Whether the kana spelling u reads as a word where query starts.
bool Worker::KanaWord(const dict::UserEntry& u, std::string_view query) const {
    const std::string_view surface = transformSurface;
    if (!surface.ends_with(query)) return false;
    const std::string_view prev = LastChar(surface.substr(0, surface.size() - query.size()));
    switch (NameClassOf(prev)) {
    case kClassHiragana:
    case kClassKanji:
    case kClassKatakana:
        if (!Has(kKanaBefore, prev)) return false;
    }
    const std::string_view pc = u.codes->partText;
    if (pc.size() > 1 && (pc[1] == '4' || pc[1] == '7')) return true;
    const std::string_view next = FirstChar(query.substr(std::min(u.key.size(), query.size())));
    switch (NameClassOf(next)) {
    case kClassHiragana:
        return Has(kKanaAfter, next);
    case kClassKanji:
        return false;
    }
    return text::RuneCount(u.key) >= 3;
}

// Text path, mode 0: the base interjections and set phrases that are a verb form ending
// in て or ます with a Korean other than the verb's are dropped where they are the verb: before
// hiragana, and for ます after anything but the start of the token or punctuation.
void Worker::DropBoundUtterances(Lookup& l, std::string_view query) {
    const std::string_view surface = transformSurface;
    const bool alone = surface.ends_with(query) &&
                       UtteranceBoundary(LastChar(surface.substr(0, surface.size() - query.size())));
    Record* out = nullptr;
    Record* last = nullptr;
    for (Record* r = l.records; r != nullptr; r = r->child) {
        const std::string_view next = FirstChar(query.substr(r->source.size()));
        const bool bound = NameClassOf(next) == kClassHiragana ||
                           (r->source.ends_with("ます") && !(alone && UtteranceBoundary(next)));
        if (r->user == nullptr && bound && VerbUtterance(*r)) {
            l.count--;
            continue;
        }
        if (out == nullptr) out = r;
        else last->child = r;
        last = r;
    }
    if (last != nullptr) last->child = nullptr;
    l.records = out;
}

// Text path, mode 0: an adnominal record (part class 0) in the past (お腹が空いた 배가 고픈) is
// dropped before the end of the line or of a sentence (。！？」』), where nothing follows for
// it to modify, so that the predicate analysis ends the sentence (배가 고팠다). Before a
// pause it stays (荒涼とした…工事現場).
void Worker::DropFinalAdnominals(Lookup& l, std::string_view query) {
    Record* out = nullptr;
    Record* last = nullptr;
    for (Record* r = l.records; r != nullptr; r = r->child) {
        bool final = false;
        if (At(r->partCode, 1) == '0' && query.size() >= r->source.size() &&
            (r->source.ends_with("た") || r->source.ends_with("だ"))) {
            std::string_view rest = query.substr(r->source.size());
            if (rest.empty() && current >= 0) {
                final = current + 1 >= TokenCount();
                if (!final) {
                    rest = NextText(current + 1);
                    final = rest.empty();
                }
            }
            for (const std::string_view p : {"。", "！", "？", "!", "?", "」", "』"}) {
                final = final || rest.starts_with(p);
            }
        }
        if (final) {
            l.count--;
            continue;
        }
        if (out == nullptr) out = r;
        else last->child = r;
        last = r;
    }
    if (last != nullptr) last->child = nullptr;
    l.records = out;
}

// Text path, mode 0: an adverb record in く of an i-adjective of the lookup (上手く 잘, 早く
// 빨리, with 上手 and 早 as adjectives) before ない, はない, もない or なる is dropped, so that the
// adjective takes the ending (上手くない 능숙하지 않다, not 잘 없다). Other adverbs in く stay
// (全くない 전혀 없다).
void Worker::DropNegatedAdverbs(Lookup& l, std::string_view query) {
    auto adjective = [&](std::string_view stem) {
        for (const Record* r = l.records; r != nullptr; r = r->child) {
            if (r->source == stem && At(r->partCode, 0) == 'H' && At(r->partCode, 1) == '7') return true;
        }
        return false;
    };
    Record* out = nullptr;
    Record* last = nullptr;
    for (Record* r = l.records; r != nullptr; r = r->child) {
        bool negated = false;
        if (r->source.ends_with("く") && At(r->partCode, 0) == 'E' && At(r->partCode, 1) == '3' &&
            query.size() >= r->source.size() &&
            adjective(std::string_view(r->source).substr(0, r->source.size() - std::string_view("く").size()))) {
            std::string_view rest = query.substr(r->source.size());
            if (rest.starts_with("は") || rest.starts_with("も")) rest.remove_prefix(std::string_view("は").size());
            for (const std::string_view n : {"ない", "なく", "なかっ", "なけれ", "なさそ", "なる", "なり", "なれ", "なっ",
                                             "なら", "なろ"}) {
                negated = negated || rest.starts_with(n);
            }
        }
        if (negated) {
            l.count--;
            continue;
        }
        if (out == nullptr) out = r;
        else last->child = r;
        last = r;
    }
    if (last != nullptr) last->child = nullptr;
    l.records = out;
}

// Text path: the records that split the modal かもしれない or the sentence-final かしら
// are dropped.
void Worker::DropModalSplits(Lookup& l, std::string_view query) {
    const std::string_view surface = transformSurface;
    const bool afterKa = surface.ends_with(query) && surface.substr(0, surface.size() - query.size()).ends_with("か");
    Record* out = nullptr;
    Record* last = nullptr;
    for (Record* r = l.records; r != nullptr; r = r->child) {
        if (SplitsModal(*r, query, afterKa) || SplitsKashira(*r, query) || SplitsVerb(*r, query)) {
            l.count--;
            continue;
        }
        if (out == nullptr) out = r;
        else last->child = r;
        last = r;
    }
    if (last != nullptr) last->child = nullptr;
    l.records = out;
}

bool Worker::SplitsModal(const Record& r, std::string_view query, bool afterKa) const {
    if (!query.starts_with(r.source)) return false;
    const std::string_view rest = query.substr(r.source.size());
    if (r.source == "もし") return afterKa && (rest.starts_with("れ") || rest.starts_with("知れ"));
    if (At(r.partCode, 0) != 'P' || r.source == "か" || r.source == "かも") return false;
    if (r.source.ends_with("かも")) return rest.starts_with("しれま") || rest.starts_with("知れま");
    if (!r.source.ends_with("か")) return false;
    if (rest.starts_with("もしれ") || rest.starts_with("も知れ")) return true;
    if (!rest.starts_with("も") || !r.source.ends_with("のか") || current < 0) return false;
    const std::string_view after = TrimLeftChars(rest.substr(std::string_view("も").size()), "ねよなぁっー");
    return SentenceFinal(after, current, true);
}

// A word longer than か that ends in か before しら and the end of the sentence (なのか +
// しら, 何故か + しら): the か belongs to the sentence-final かしら. Ending rows are kept,
// and so are the polite ですか and ませんか.
bool Worker::SplitsKashira(const Record& r, std::string_view query) const {
    if (r.source.size() <= std::string_view("か").size() || !r.source.ends_with("か") || !query.starts_with(r.source) ||
        current < 0 || AnyOf("STU", At(r.partCode, 0)) || r.source.ends_with("すか") || r.source.ends_with("せんか")) {
        return false;
    }
    const std::string_view rest = query.substr(r.source.size());
    if (!rest.starts_with("しら")) return false;
    return SentenceFinal(TrimLeftChars(rest.substr(std::string_view("しら").size()), "ねよぁっー"), current, true);
}

// A person name of two kanji or more whose last kanji is the stem of an ichidan verb that
// the hiragana after the name conjugates (る, た, て), the kanji before it being a word
// (今居る: 今 and 居る, not the name 今居): the name takes the verb's stem. Not before a
// person suffix (里見たち, 里見ちゃん).
bool Worker::SplitsVerb(const Record& r, std::string_view query) const {
    if (!r.semantic.starts_with("QPN") || !query.starts_with(r.source) || text::RuneCount(r.source) < 2) return false;
    const std::string_view rest = query.substr(r.source.size());
    const text::Rune next = text::DecodeRune(rest);
    if (next.cp < U'ぁ' || next.cp > U'ゖ' || StartsWithHonorific(rest) || PersonSuffixAt(rest)) return false;
    const std::string_view last = LastChar(r.source);
    const std::string_view head = r.source.substr(0, r.source.size() - last.size());
    if (NameClassOf(last) != kClassKanji || snapshot.RetrieveRecords(head, dict::LookupMode::General).empty()) {
        return false;
    }
    for (const dict::Candidate& c : snapshot.RetrieveRecords(last, dict::LookupMode::General)) {
        const std::string_view pc = c.codes->partText;
        int branch = 0, length = 0;
        if (pc.size() == 4 && pc.starts_with("M4") && pc[3] == '7' && AnalyseVerbTail(last, pc[3], rest, branch, length) &&
            length > 0) {
            return true;
        }
    }
    return false;
}

// Whether s starts with a noun of persons of two kana or more that the dictionary
// writes attached to the word before it (たち, ども: N1, VHn and Tu or Z7).
bool Worker::PersonSuffixAt(std::string_view s) const {
    std::size_t end = 0;
    for (int n = 1; n <= 3 && end < s.size(); n++) {
        const text::Rune c = text::DecodeRune(s.substr(end));
        if (c.size == 0 || c.cp < U'ぁ' || c.cp > U'ゖ') break;
        end += c.size;
        if (n < 2) continue;
        for (const dict::Candidate& rec : snapshot.RetrieveRecords(s.substr(0, end), dict::LookupMode::General)) {
            const dict::Codes* codes = rec.codes;
            if (codes != nullptr && codes->partText.starts_with("N1") && codes->sem.HasCode("VHn") &&
                (codes->gram.HasCode("Tu") || codes->gram.HasCode("Z7"))) {
                return true;
            }
        }
    }
    return false;
}

// Whether form is also the imperative of a godan verb of the dictionary: form without its
// last kana is the stem of a godan verb whose imperative ends in that kana (立て of 立つ).
bool Worker::GodanImperative(std::string_view form) const {
    const std::string_view last = LastChar(form);
    const std::string_view stem = form.substr(0, form.size() - last.size());
    if (stem.empty()) return false;
    for (const dict::Candidate& rec : snapshot.RetrieveRecords(stem, dict::LookupMode::General)) {
        const std::string_view pc = rec.codes->partText;
        if (pc.size() != 4 || pc[0] != 'M' || pc[1] != '4') continue;
        const int kind = pc[3] - '0';
        if (kind != 7 && kind >= 0 && kind < kConjugationKinds && Conjugation(kind, 11) == last) return true;
    }
    return false;
}

// An interjection (E32) or set phrase (A9) whose key ends in て or ます and is a form
// of a verb of the base dictionary: the key without its last one to three characters,
// all hiragana, is a verb stem none of whose Korean starts like r's.
bool Worker::VerbUtterance(const Record& r) const {
    const std::string_view pc = r.partCode, key = r.source;
    if (!(pc.starts_with("A9") || pc.starts_with("E32")) || !(key.ends_with("て") || key.ends_with("ます"))) {
        return false;
    }
    std::string_view stem = key;
    while (!stem.empty()) {
        const text::Rune c = DecodeLastRune(stem);
        if (!(c.cp >= U'ぁ' && c.cp <= U'ゖ')) break;
        stem.remove_suffix(c.size);
    }
    if (stem.empty() || text::RuneCount(key.substr(stem.size())) > 3) return false;
    const std::string_view first = FirstHangul(r.target);
    bool verb = false;
    for (long i = static_cast<long>(key.size() - LastChar(key).size()); i >= static_cast<long>(stem.size());
         i -= static_cast<long>(LastChar(key.substr(0, static_cast<std::size_t>(i))).size())) {
        for (const dict::Candidate& rec :
             snapshot.RetrieveRecords(key.substr(0, static_cast<std::size_t>(i)), dict::LookupMode::General)) {
            if (At(rec.codes->partText, 0) != 'M') continue;
            if (!first.empty() && FirstHangul(rec.target) == first) return false;
            verb = true;
        }
    }
    return verb;
}

// A predicate followed only by closing punctuation: rest, the text of its token after
// the ending, is closing punctuation or a dash, or rest is empty and the next token ends
// the sentence or the line, is closing punctuation (…, ――) or laughter (www). With comma,
// a comma after the predicate also ends it.
bool Worker::SentenceFinal(std::string_view rest, int token, bool comma) const {
    // The punctuation that may follow a sentence-final predicate in its token, and the
    // small kana that only lengthen or stop its last sound (書くなっ, 来たぁ).
    static constexpr std::string_view kClosing[] = {"。", "．", "！", "？", "…", "‥", "・", "」", "』", "）", "～",
                                                    "ー", "―", "—", "─", "♪", "☆", "★", "／", ".", "!", "?", "っ", "ッ",
                                                    "ぁ", "ぃ", "ぅ", "ぇ", "ぉ", "ァ", "ィ", "ゥ", "ェ", "ォ"};
    auto closing = [](std::string_view t) {
        while (!t.empty()) {
            const std::size_t n = t.size();
            for (const std::string_view c : kClosing) {
                if (t.starts_with(c)) t.remove_prefix(c.size());
            }
            if (t.size() == n) return false;
        }
        return true;
    };
    if (comma && (rest.starts_with(kJaComma) || rest.starts_with(kJaFullComma))) return true;
    if (!rest.empty()) return closing(rest);
    if (token + 1 >= TokenCount()) return true;
    tokenizer::TokenView next = Tok(token + 1);
    next.text = NextText(token + 1);
    if (comma && (next.text.starts_with(kJaComma) || next.text.starts_with(kJaFullComma))) return true;
    switch (next.type) {
    case tokenizer::kTypeAsciiPunct:
    case tokenizer::kTypeAsciiClose:
    case tokenizer::kTypeNewline:
    case tokenizer::kTypeEnd:
    case tokenizer::kTypeSentenceEnd:
    case tokenizer::kTypeBracket:
    case tokenizer::kTypeMarkup:
        return true;
    case tokenizer::kTypeSymbol:
        return closing(next.text);
    case tokenizer::kTypeAlpha:
        return next.text.find_first_not_of("wW") == std::string_view::npos;
    }
    return false;
}

// An end row only applies to a sentence-final predicate of its register; a comma after
// an ending in a closing particle (のう) still ends the predicate.
bool Worker::EndingApplies(const Entry& e, std::string_view rest, int token) const {
    const dict::UserEntry* u = e.user;
    if (u == nullptr || !u->end) return true;
    if (u->registerLevel != 0 && Register(*u) != u->registerLevel) return false;
    return SentenceFinal(rest, token, u->key.ends_with("のう") || u->key.ends_with("のぅ"));
}

// Text input: an ending that would take the head of the word after it, ため before 息
// (長いため息 긴 한숨), or a sentence particle that is really the head of a noun
// (揺れるしっぽ: not 揺れる + し + っぽ but 揺れる + しっぽ). The particle moves to the noun
// only when all of these hold:
//   - the ending's last kana is a particle that follows a terminal form (し な か わ ね
//     よ さ ぞ ぜ), and the word up to it, before, is still a terminal form without it
//     (揺れる, not the bare stem 良 of 良いっしゅ);
//   - the text after the ending starts with a small kana or ー, which no free word starts
//     with, and is no particle or ending of the dictionary (って, っけ, っす);
//   - that kana and the text after it are a dictionary noun of three or more characters.
bool Worker::CutsWord(std::string_view before, std::string_view ending, std::string_view after) const {
    if (!settings.text) return false;
    if (ending.ends_with("ため") && after.starts_with("息")) return true;
    const text::Rune next = text::DecodeRune(after);
    if (next.size == 0 || ending.empty() ||
        std::u32string_view(U"ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮー").find(next.cp) == std::u32string_view::npos) {
        return false;
    }
    const text::Rune last = DecodeLastRune(ending);
    if (std::u32string_view(U"しなかわねよさぞぜ").find(last.cp) == std::u32string_view::npos) return false;
    const std::string whole = std::string(before) + std::string(ending);
    const std::string_view shortened = std::string_view(whole).substr(0, whole.size() - last.size);
    if (shortened.empty() ||
        std::u32string_view(U"るたうくぐすつぬぶむいだ").find(DecodeLastRune(shortened).cp) == std::u32string_view::npos) {
        return false;
    }
    const auto partOf = [](const dict::Candidate& c) {
        return c.codes != nullptr ? std::string_view(c.codes->partText) : std::string_view();
    };
    const std::string_view rest = after.substr(0, std::min<std::size_t>(after.size(), 24));
    for (const dict::Candidate& c : snapshot.GetWordInfo(rest, dict::LookupMode::General, lookupContext).list) {
        const std::string_view part = partOf(c);
        if (text::RuneCount(c.source) >= 2 && !part.empty() && part[0] != 'N' && part[0] != 'M' && part[0] != 'H') {
            return false;  // a particle or an ending in its own right
        }
    }
    std::string query(ending.substr(ending.size() - last.size));
    query += rest;
    for (const dict::Candidate& c : snapshot.GetWordInfo(query, dict::LookupMode::General, lookupContext).list) {
        if ((c.user == nullptr || !c.user->nameLayer) && partOf(c).starts_with("N1") && text::RuneCount(c.source) >= 3) {
            return true;
        }
    }
    return false;
}

bool OwnedGroup(const Group* g) { return g != nullptr && !g->entries.empty() && g->entries[0]->user != nullptr; }

// Whether no validity rule rejects the text that follows source (a word of class first,
// followed by class second).
bool Worker::MatchDictionary(std::string_view following, std::string_view source, char first, char second) {
    std::string query;
    for (const ValidityRule& r : ValidityRules()) {
        const std::string_view rs = r.source;
        if (rs.starts_with("XX")) {
            if (r.sourceClass != first) continue;
        } else if (first != r.sourceClass || second != r.followClass || rs != source) {
            continue;
        }
        std::string_view list = r.first;
        while (!list.empty()) {
            const std::size_t sep = list.find_first_of("; ");
            const std::string_view token = list.substr(0, sep);
            list = sep == std::string_view::npos ? std::string_view() : list.substr(sep + 1);
            if (token.empty()) continue;
            const std::size_t open = token.find('[');
            if (open == std::string_view::npos || token.find(']') == std::string_view::npos) {
                if (following.starts_with(token)) return false;
                continue;
            }
            const char kind = At(token, static_cast<long>(open + 1));
            if (!AnyOf("1234789", kind)) continue;
            query.assign(token.substr(0, open));
            query += following;
            const Lookup l = DoLookup(query, kModeGeneral);
            if (l.count <= 0) continue;
            for (const Record* rec = l.records; rec != nullptr; rec = rec->child) {
                if (kind == At(rec->partCode, 1) && std::string_view(query).starts_with(rec->source)) return false;
            }
        }
    }
    return true;
}

}  // namespace hdTrans::word
