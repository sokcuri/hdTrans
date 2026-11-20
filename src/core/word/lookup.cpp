// Dictionary lookups of the word stage and the filters on their candidates.
#include <cstdint>
#include <algorithm>

#include "core/dict/classes.h"
#include "core/text/utf8.h"
#include "core/text/unicode.h"
#include "core/tokenizer/classify.h"
#include "core/word/validity_rules.h"
#include "core/word/conjugation.h"
#include "core/word/constraint_rules.h"
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

// Whether a name-layer key is a phrase: a particle of particle.title-phrase between two
// kanji or katakana (星に願いを, 世界を救う).
bool PhraseKey(std::string_view key) {
    // Kanji or katakana: a name class other than hiragana and the rest.
    auto content = [](int cls) { return cls != kClassOther && cls != kClassHiragana; };
    int prev = kClassOther;
    for (std::string_view rest = key; !rest.empty();) {
        const std::string_view c = FirstChar(rest);
        rest.remove_prefix(c.size());
        if (content(prev) && dict::InClass(HDTRANS_CLASS("particle.title-phrase"), c) && content(NameClassOf(FirstChar(rest)))) return true;
        prev = NameClassOf(c);
    }
    return false;
}

// Whether a name-layer row is a title that is a phrase: a phrase key that names no
// organisation or place (共に民主党 and ファへ市 stand inside sentences).
bool PhraseTitle(const dict::UserEntry& u) {
    if (u.codes != nullptr && (u.codes->sem.HasPrefix("VGt") || u.codes->sem.HasPrefix("QCT"))) return false;
    return PhraseKey(u.key);
}

// s without the leading characters of set.
// Whether the verb or adjective of record c, whose key ends where after starts, takes after as its ending: an
// adjective stem one of kana.adjective-ending (さえな + い, not さえな + やみ), a godan stem a cell of its row of
// conjugation (からか + う, not からか + ぶさって); an ichidan stem, bare as its continuative, and a row of another
// shape always.
bool StemTakes(const dict::Candidate& c, std::string_view after) {
    const std::string_view pt = c.codes == nullptr ? std::string_view() : c.codes->partText;
    if (after.empty() || pt.size() < 2) return true;
    if (pt[1] == '7') return dict::InClass(HDTRANS_CLASS("kana.adjective-ending"), FirstChar(after));
    if (pt.size() != 4) return true;
    const int kind = pt[3] - '0';
    if (kind == 7 || ((kind < 0 || kind >= kConjugationKinds) && kind != kKindIku)) return true;
    for (int b = 0; b < kConjugationBranches; b++) {
        const std::string_view cell = Conjugation(kind, b);
        if (!cell.empty() && cell[0] != '(' && cell != "XX" && after.starts_with(cell)) return true;
    }
    return false;
}

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
// a verb by a start of follower.te-imperative (待てない, 待てって, 待てば, 待てる); or an
// e-row imperative continued by hiragana other than よ, っ and ぇ (落ち着ける).
bool BoundImperative(const dict::UserEntry& u, std::string_view query) {
    if (!u.codes->partText.starts_with("E") || !query.starts_with(u.key)) return false;
    const std::string_view after = query.substr(u.key.size());
    if (u.key.ends_with("て")) {
        return dict::StartsWithMember(HDTRANS_CLASS("follower.te-imperative"), after) > 0;
    }
    return ERowImperative(u.key) && NameClassOf(FirstChar(after)) == kClassHiragana && !after.starts_with("よ") &&
           !after.starts_with("っ") && !after.starts_with("ぇ");
}

// The character c, before or after a word, ends the utterance: none, or punctuation,
// a bracket or a space.
bool UtteranceBoundary(std::string_view c) {
    return c.empty() || (NameClassOf(c) == kClassOther && !(c.size() == 1 && (IsAlpha(c[0]) || IsDigit(c[0]))));
}

// Whether the utterance ends where rest starts, after any spaces: nothing, or punctuation, a
// bracket or a mark that is no comma and no question mark.
bool UtteranceEnds(std::string_view rest) {
    while (!rest.empty() && text::IsSpace(text::DecodeRune(rest).cp)) rest.remove_prefix(text::DecodeRune(rest).size);
    const std::string_view c = FirstChar(rest);
    return UtteranceBoundary(c) && !dict::InClass(HDTRANS_CLASS("mark.comma"), c) && !dict::InClass(HDTRANS_CLASS("mark.question"), c);
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

// The means ことで (送ることで 보냄으로써) neither after 言う (ということで, と言うことで: the
// connective 그래서, 라는 것으로) nor before the negative copula (満たすことでなく
// 채우는 것이 아니라); query is the text from ことで on, left its token.
bool SaysOrNegates(std::string_view left, std::string_view query) {
    const std::string_view rest = query.substr(std::min(query.size(), std::string_view("ことで").size()));
    for (const std::string_view no : {"ない", "なく", "なけ", "なかっ", "無"}) {
        if (rest.starts_with(no)) return true;
    }
    if (left.size() < query.size()) return false;
    const std::string_view before = left.substr(0, left.size() - query.size());
    for (const std::string_view say : {"いう", "言う", "云う", "ゆう"}) {
        if (before.ends_with(say)) return true;
    }
    return false;
}

// Whether a code is that of a proper noun: class N130 or a Q meaning.
bool ProperCodes(std::string_view part, std::string_view sem) {
    return (part.size() == 4 && part.substr(2, 2) == "30") || sem.starts_with("Q");
}

// The で of a word or ending in もので or わけで, or of なので, is the copula when the copula goes on after it:
// である and its forms, でございます, でござる, でしょう, でしかない, ではない, and でした before the end, a
// mark or a particle (弱いものである: もの + である, not もので + ある; 一つなのでございます, 静かなのであった,
// 可愛いものでしょ, 書くわけである); so is that of any ending in ので before ござ (叫ぶのでござる). Before any other
// word the で stays (忙しいものであまり来られない, 暇なのでしたいことをした, 好きなのでしかたがない,
// 暇なのでしょっちゅう来る, 好きなのでしょうがない), and so does でも, which before ある may ask whether there is
// something (食べたいものでもあるか).
bool CopulaAfterNominal(std::string_view ending, std::string_view after) {
    if (!ending.ends_with("もので") && !ending.ends_with("わけで") && ending != "なので") {
        return ending.ends_with("ので") && after.starts_with("ござ");
    }
    for (const std::string_view s : {"ある", "あった", "あって", "あろう", "ありま", "あり、", "あれば", "ござ", "しかな",
                                     "はな", "はあ", "はご"}) {
        if (after.starts_with(s)) return true;
    }
    if (after.starts_with("しょ")) {
        return !after.starts_with("しょっ") && !after.starts_with("しょうがな") && !after.starts_with("しょうがね") &&
               !after.starts_with("しょうが無");
    }
    if (!after.starts_with("した")) return false;
    const char32_t next = text::DecodeRune(after.substr(std::string_view("した").size())).cp;
    if (std::u32string_view(U"かねよがけのわなし").find(next) != std::u32string_view::npos) return true;
    const bool letter = (next >= U'ぁ' && next <= U'ヿ') || (next >= U'㐀' && next <= U'鿿') || next == U'々';
    return !letter;
}

// Whether two lookups give the same records, field by field, with views to the same bytes.
bool SameRecords(const Lookup& a, const Lookup& b) {
    if (a.count != b.count) return false;
    auto same = [](std::string_view x, std::string_view y) { return x.data() == y.data() && x.size() == y.size(); };
    const Record* x = a.records;
    const Record* y = b.records;
    for (; x != nullptr && y != nullptr; x = x->child, y = y->child) {
        if (!same(x->source, y->source) || x->target != y->target || !same(x->partCode, y->partCode) ||
            !same(x->semantic, y->semantic) || x->attr != y->attr || !same(x->dictCode, y->dictCode) || x->ruleNo != y->ruleNo ||
            x->id != y->id || x->user != y->user) {
            return false;
        }
    }
    return x == nullptr && y == nullptr;
}

// Whether two candidate lists hold the same candidates, field by field, with views to the same bytes.
bool SameCandidates(const std::vector<dict::Candidate>& a, const std::vector<dict::Candidate>& b) {
    if (a.size() != b.size()) return false;
    auto same = [](std::string_view x, std::string_view y) { return x.data() == y.data() && x.size() == y.size(); };
    for (std::size_t i = 0; i < a.size(); i++) {
        const dict::Candidate& x = a[i];
        const dict::Candidate& y = b[i];
        if (!same(x.source, y.source) || !same(x.target, y.target) || x.codes != y.codes || x.rule != y.rule || x.id != y.id ||
            x.user != y.user) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool NounBefore(std::string_view text) {
    const text::Rune last = DecodeLastRune(text);
    const char32_t r = last.cp;
    if (r >= 0x3041 && r <= 0x309F) {
        return dict::EndsWithMember(HDTRANS_CLASS("pronoun.hiragana-end"), text) > 0;
    }
    if (r == text::kReplacement) return false;
    return (!text::IsSpace(r) && !text::IsPunct(r)) || r == U'」' || r == U'』' || r == U'）' || r == U')';
}

uint8_t Worker::Register(const dict::UserEntry& u) const { return u.polite ? settings.registers[1] : settings.registers[0]; }

std::size_t Worker::CopyUnit(std::string_view text) const {
    if (tokens.copySpaces.empty() || current < 0 || current >= TokenCount()) return 0;
    const std::string_view own = tokens.Canonical(static_cast<std::size_t>(current));
    if (own.empty() || text.data() < own.data() || text.data() >= own.data() + own.size()) return 0;
    return tokens.CopyUnitAt(static_cast<std::size_t>(current), static_cast<std::size_t>(text.data() - own.data()));
}

dict::Candidates Worker::WordInfo(std::string_view query, int mode) const {
    dict::LookupContext ctx = lookupContext;
    SkipContext sc{this, query};
    ctx.skip.fn = [](const void* c, const dict::UserEntry& e) {
        const SkipContext* s = static_cast<const SkipContext*>(c);
        return s->w->Skip(e, s->query);
    };
    ctx.skip.context = &sc;
    if (current + 1 < TokenCount()) ctx.after = Tok(current + 1).text;
    return snapshot.GetWordInfo(query, static_cast<dict::LookupMode>(mode), ctx);
}

Lookup Worker::DoLookup(std::string_view query, int mode) {
    if (fault) return {};
    kanaAfterHeld_ = false;
    dict::Candidates found = WordInfo(query, mode);
    if (mode == kModeGeneral && !kanaAfterHeld_ && settings.text && snapshot.SegCostTable().loaded) {
        lastGeneral_.data = query.data();
        lastGeneral_.size = query.size();
        lastGeneral_.current = current;
        lastGeneral_.surface = transformSurface;
        lastGeneral_.list = found.list;
    }
    if (!found.names.empty()) {
        if (arena.namesUsed == arena.names.size()) arena.names.emplace_back();
        arena.names[arena.namesUsed++].swap(found.names);
    }
    if (mode == kModeGeneral && settings.text) {
        ApplyConstraints(found, query);
        ReadGivenNames(found, query);
    }
    // The ending table again with the candidates of its last lookup: the records would equal endings_ field by field
    // (they are made from the candidates alone, and with an empty query no split drops one by the text around), so
    // endings_ is what the check below would give.
    const bool endingTable = query.empty() && mode == kModeVerbEnding;
    if (endingTable && found.count == endingsFoundCount_ && SameCandidates(found.list, endingsFound_)) {
        if (observer.fn != nullptr) observer.fn(observer.context, current, query, mode, endings_.records);
        return endings_;
    }
    Lookup l;
    l.count = found.count;
    const std::size_t mark = arena.records.Used();
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
        DropOpenUtterances(l, query);
    }
    if (settings.text) {
        DropModalSplits(l, query);
        TransliterateGlosses(l.records);
        if (fault) return {};
    }
    // The ending table comes back whole at every verb. When the records equal those of its last
    // lookup, those are given again and the new ones go back to the pool: only this lookup made
    // records since mark, and no record of the table is ever changed after it is made (groups
    // copy them into entries).
    if (endingTable) {
        if (SameRecords(l, endings_)) {
            arena.records.Rewind(mark);
            l = endings_;
        } else {
            endings_ = l;
        }
        endingsFound_ = found.list;
        endingsFoundCount_ = found.count;
    }
    if (observer.fn != nullptr) observer.fn(observer.context, current, query, mode, l.records);
    return l;
}

bool Worker::Skip(const dict::UserEntry& u, std::string_view query) const {
    return !EndRowFits(u, query) || (u.nameLayer && !KanaBounded(u.key, query)) ||
           (u.nameLayer && PhraseTitle(u) && !StandsApart(u.key, query)) || (u.kana && !KanaWord(u, query)) ||
           (settings.text && BoundImperative(u, query)) ||
           (u.utteranceEnd && query.starts_with(u.key) && !UtteranceEnds(query.substr(u.key.size())));
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
        // After a person's name of the name layer, やん is the nickname -やん (まりやん 마리얀), not the
        // Kansai ending (学生やん 학생이잖아; spec GRM-3832).
        if (u.key.starts_with("やん")) {
            std::string_view head = left.substr(0, static_cast<std::size_t>(before));
            // The name fills its kana run: two characters or more after a break, not the tail of a
            // predicate (関係あるんやん).
            for (int n = 0; n < 6 && !head.empty(); n++) {
                const std::size_t cut = head.size() - DecodeLastRune(head).size;
                const char32_t prior = cut > 0 ? DecodeLastRune(left.substr(0, cut)).cp : U' ';
                const bool start = !(prior >= U'ぁ' && prior <= U'ゖ') && !(prior >= U'ァ' && prior <= U'ヺ') && !text::IsKanji(prior);
                if (n >= 1 && start && snapshot.NamePerson(left.substr(cut, static_cast<std::size_t>(before) - cut))) return false;
                head = head.substr(0, cut);
            }
        }
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
    if (AskedNegativeGuess(u, left, query)) return false;
    if (u.key.ends_with("ことで") && SaysOrNegates(left, query)) return false;
    if (tag == 'P') {
        const long before = std::max(static_cast<long>(left.size()) - static_cast<long>(query.size()), 0L);
        if (!NounBefore(left.substr(0, static_cast<std::size_t>(before)))) return false;
    }
    if (!u.target.ends_with(kKoDa)) return true;
    return SentenceFinal(rest, current, false) || rest.starts_with(kJaComma) || rest.starts_with(kJaFullComma) ||
           rest.starts_with(",");
}

// A name-layer key, matched at query (the rest of the surface being split), fills its
// kana run, and one that starts with a digit does not start on a number written straight
// after katakana or Latin letters: the number labels that word (チャプター5ボス).
bool Worker::KanaBounded(std::string_view key, std::string_view query) const {
    if (const char32_t d = text::DecodeRune(FirstChar(key)).cp; (d >= U'0' && d <= U'9') || (d >= U'０' && d <= U'９')) {
        const std::string_view surface = transformSurface;
        std::string_view before;
        if (EndsSurface(query)) before = LastChar(surface.substr(0, surface.size() - query.size()));
        if (before.empty() && current > 0) before = LastChar(Tok(current - 1).text);
        char32_t b = text::DecodeRune(before).cp;
        if (b >= U'Ａ' && b <= U'ｚ') b -= 0xFEE0;
        if (NameClassOf(before) == kClassKatakana || (b >= U'A' && b <= U'Z') || (b >= U'a' && b <= U'z')) return false;
    }
    const int first = NameClassOf(FirstChar(key)), last = NameClassOf(LastChar(key));
    if (first == kClassHiragana || first == kClassKatakana) {
        const std::string_view surface = transformSurface;
        if (!EndsSurface(query)) return false;
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

// Whether the key matched at query has no kana, kanji or Hangul right before or after it, in its
// token or the tokens next to it: a phrase reads as a title only standing apart from the
// sentence around it (星に願いを込めた is no title).
bool Worker::StandsApart(std::string_view key, std::string_view query) const {
    const std::string_view surface = transformSurface;
    std::string_view before;
    if (EndsSurface(query) && surface.size() > query.size()) {
        before = LastChar(surface.substr(0, surface.size() - query.size()));
    } else if (current > 0) {
        // The space written after a span read in Hangul stands for no space of the text.
        int b = current - 1;
        if (b > 0 && Tok(b).type == tokenizer::kTypeAsciiSpace && Tok(b - 1).type == tokenizer::kTypeProtected) b--;
        before = LastChar(Tok(b).text);
    }
    std::string_view after = FirstChar(query.substr(std::min(key.size(), query.size())));
    if (after.empty() && current + 1 < TokenCount()) after = FirstChar(Tok(current + 1).text);
    // A span already read in Hangul (ぼんやりと 멍하니) is a word of the sentence too.
    auto apart = [](std::string_view c) {
        return NameClassOf(c) == kClassOther && !HangulSyllable(text::DecodeRune(c).cp);
    };
    return apart(before) && apart(after);
}

// Whether the text before query in its token ends with a unit of two or three kana said twice, a
// sound (ぴちぴち, ぴょこぴょこ), after which a verb spelled in kana starts a word (ぴちぴちはねる 팔팔 뛴다;
// LEX-5830).
bool Worker::DoubledSoundBefore(std::string_view query) const {
    const std::string_view before = transformSurface.substr(0, transformSurface.size() - query.size());
    // Kana take three bytes each in UTF-8.
    for (std::size_t n = 2; n <= 3; n++) {
        if (before.size() < 6 * n) continue;
        const std::string_view unit = before.substr(before.size() - 3 * n), first = before.substr(before.size() - 6 * n, 3 * n);
        if (unit != first || unit.substr(0, 3) == unit.substr(3, 3)) continue;
        bool kana = true;
        for (std::size_t k = 0; k < unit.size(); k += 3) {
            const int c = NameClassOf(unit.substr(k, 3));
            kana = kana && (c == kClassHiragana || c == kClassKatakana);
        }
        if (kana) return true;
    }
    return false;
}

// Whether the text before query in its token ends with a hiragana adverb of two kana or more that the
// dictionary holds and that starts the token or follows a particle of kKanaBefore, after which a verb
// spelled in kana starts a word (はやくにげて 빨리 도망쳐, ゆっくりはなれる 천천히 떨어진다; LEX-5922).
bool Worker::KanaModifierBefore(std::string_view query) const {
    const std::string_view surface = transformSurface;
    if (!EndsSurface(query)) return false;
    const std::string_view before = surface.substr(0, surface.size() - query.size());
    // Kana take three bytes each in UTF-8; up to five kana back.
    for (std::size_t n = 2; n <= 5 && 3 * n <= before.size(); n++) {
        const std::string_view word = before.substr(before.size() - 3 * n);
        bool kana = true;
        for (std::size_t k = 0; k < word.size(); k += 3) kana = kana && NameClassOf(word.substr(k, 3)) == kClassHiragana;
        if (!kana) break;
        const std::string_view lead = LastChar(before.substr(0, before.size() - word.size()));
        if (!lead.empty() && NameClassOf(lead) != kClassOther && !Has(kKanaBefore, lead)) continue;
        for (const dict::Candidate& c : snapshot.RetrieveRecords(word, dict::LookupMode::General)) {
            const std::string_view p = c.codes->partText;
            // An adverb (N3xx, E3xx): a noun before is too often the noun and a particle (これはねえ).
            if (p.size() > 1 && (p[0] == 'N' || p[0] == 'E') && p[1] == '3') return true;
        }
    }
    return false;
}

// Whether the word the search read before query ends a word there, so that the kana spelling u may start one
// after a character that kKanaBefore does not hold: a particle of particle.kana-word-after (口からもんくを 입에서 불평을),
// also as the end of the word before (これから, 今こそ, 死ぬほど, 笑いながら + なやみ), or a common noun written in kanji
// whose kanji and the kana of u after them, kana by kana, are no key of the dictionary (水しげん; not 分かんねん, whose 分か is the
// stem of 分かる; LEX-6801).
bool Worker::KanaAfterWord(const dict::UserEntry& u, std::string_view query) const {
    const bool held = KanaAfterWordIn(u, query);
    kanaAfterHeld_ = kanaAfterHeld_ || held;
    return held;
}

bool Worker::KanaAfterWordIn(const dict::UserEntry& u, std::string_view query) const {
    const Item* p = kanaAfter;
    if (p == nullptr || query.data() != kanaAfterAt || p->group == nullptr || p->detail == nullptr) return false;
    std::string_view s = p->group->surface;
    const std::string_view surface = transformSurface;
    if (s.empty() || p->length <= 0 || !EndsSurface(query)) return false;
    const std::string_view before = surface.substr(0, surface.size() - query.size());
    const std::size_t length = static_cast<std::size_t>(p->length);
    if (before.size() < length) return false;
    const std::string_view spelled = before.substr(before.size() - length);
    // A verb or adjective of two kana or more in its stem after the て form of a verb or adjective, whose ending stops
    // there (つかれてうごけない 지쳐 움직일 수 없다); a stem of one kana may be an auxiliary (てみる, ている).
    const std::string_view pc = u.codes == nullptr ? std::string_view() : u.codes->partText;
    if (p->detail->Yougo() && spelled.size() > p->group->surface.size() && (spelled.ends_with("て") || spelled.ends_with("で")) &&
        pc.size() > 1 && (pc[1] == '4' || pc[1] == '7') && text::RuneCount(u.key) >= 2) {
        return true;
    }
    // A particle of the class that ends the word before: inside the ending of a verb (笑いながら) or after a word whose
    // head is not hiragana or holds two kana or more (今こそ, 死ぬほど, これから); not ちから, たしか or おさえ, whose
    // one kana before is no word.
    bool tail = false;
    if (const std::size_t n = dict::EndsWithMember(HDTRANS_CLASS("particle.kana-word-after"), spelled);
        n > 0 && n < spelled.size()) {
        const std::string_view head = spelled.substr(0, spelled.size() - n);
        bool headKana = true;
        for (std::size_t k = 0; k < head.size();) {
            const text::Rune r = text::DecodeRune(head.substr(k));
            if (r.size == 0) return false;
            headKana = headKana && r.cp >= U'ぁ' && r.cp <= U'ゖ';
            k += r.size;
        }
        tail = spelled.starts_with(s) && (spelled.size() > s.size() ? head.size() >= s.size()
                                                                     : !headKana || text::RuneCount(head) >= 2);
        if (tail) s = spelled.substr(head.size());
    }
    // A verb or adjective only with the particle in its ending (笑いながら).
    if ((!tail && spelled != s) || (p->detail->Yougo() && (!tail || s.size() > spelled.size() - p->group->surface.size()))) {
        return false;
    }
    bool hiragana = true;
    bool kanji = true;
    for (std::size_t k = 0; k < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(k));
        if (r.size == 0) return false;
        hiragana = hiragana && r.cp >= U'ぁ' && r.cp <= U'ゖ';
        kanji = kanji && text::IsKanji(r.cp);
        k += r.size;
    }
    // After a verbal noun in kanji a stem of する starts its verb (立候補させんの, 説明しかけた: not 左遷, not しか + 桁).
    const std::string_view head = surface.substr(0, surface.size() - query.size() - (kanji ? 0 : s.size()));
    const std::string_view lead = kanji ? FirstChar(query) : FirstChar(s);
    if (lead == "さ" || lead == "し" || lead == "す" || lead == "せ") {
        std::size_t start = head.size();
        while (start > 0) {
            const std::string_view c = LastChar(head.substr(0, start));
            if (c.empty() || !text::IsKanji(text::DecodeRune(c).cp)) break;
            start -= c.size();
        }
        for (const dict::Candidate& c : snapshot.RetrieveRecords(head.substr(start), dict::LookupMode::General)) {
            if (start < head.size() && c.codes != nullptr && c.codes->gram.HasCode("Hd")) return false;
        }
    }
    const char part = p->Code(0).empty() ? 0 : p->Code(0)[0];
    // A particle whose kana and the kana of u after it are the stem of a verb or adjective that takes the text after
    // as its ending is that word (納得しかけた: しかけ + た, not しか + 桁); one whose ending the text does not go on with
    // stays the particle (私しかなやみ: not しかな + い, 上からかぶさって: not からか + う).
    // A member of the class spelled alone is the particle whatever the part code of its row (ぐらい, read as 정도).
    const bool member = hiragana && dict::InClass(HDTRANS_CLASS("particle.kana-word-after"), s);
    if (tail || part == '2' || member) {
        if (!member) return false;
        std::string key(s);
        for (std::size_t k = 0; k < u.key.size() && k < query.size();) {
            const std::size_t n = FirstChar(query.substr(k)).size();
            if (n == 0) break;
            key.append(query.substr(k, n));
            k += n;
            for (const dict::Candidate& c : snapshot.RetrieveRecords(key, dict::LookupMode::General)) {
                const std::string_view pt = c.codes == nullptr ? std::string_view() : c.codes->partText;
                if (pt.size() > 1 && (pt[1] == '4' || pt[1] == '7') && StemTakes(c, query.substr(k))) return false;
            }
        }
        return true;
    }
    // Not after a proper noun (阿部かれん, a name and its given name), nor where u starts with an honorific (那珂さんか)
    // or a particle (金にもの), which follow a noun far more often than a word spelled in kana.
    const std::string_view code = p->Code(0);
    // Nor after a question word, which a quote or a question in kana follows (何つうか, not 何 + 通過).
    if (part != '1' || code.size() < 2 || code[1] == '3' || !kanji || StartsWithHonorific(query) ||
        dict::InClass(HDTRANS_CLASS("word.wh-ask"), s)) {
        return false;
    }
    for (const dict::Candidate& c : snapshot.RetrieveRecords(FirstChar(query), dict::LookupMode::General)) {
        if (c.codes != nullptr && c.codes->part.Tag() == 'P') return false;
    }
    std::string key(s);
    for (std::size_t k = 0; k < u.key.size() && k < query.size();) {
        const std::size_t n = FirstChar(query.substr(k)).size();
        if (n == 0) break;
        key.append(query.substr(k, n));
        k += n;
        if (!snapshot.RetrieveRecords(key, dict::LookupMode::General).empty()) return false;
    }
    return true;
}

// Whether the kana spelling u reads as a word where query starts.
bool Worker::KanaWord(const dict::UserEntry& u, std::string_view query) const {
    const std::string_view surface = transformSurface;
    if (!EndsSurface(query)) return false;
    const std::string_view prev = LastChar(surface.substr(0, surface.size() - query.size()));
    const std::string_view pc = u.codes->partText;
    // An adnominal that only a noun can follow (その, 大きな; LEX-5925), as a word of its own: it starts the line or
    // follows a mark, a space, a kanji, a katakana or a particle after one (犬がそのかえるを), not the end of a kana
    // word (いとこのかえる日 is いとこ, の and the verb).
    bool adnominal = false;
    if (pc.size() > 1 && pc[1] == '1') {
        const std::string_view head = surface.substr(0, surface.size() - query.size());
        const std::size_t n = dict::EndsWithMember(HDTRANS_CLASS("word.adnominal-before-noun"), head);
        if (n > 0) {
            const std::string_view lead_text = head.substr(0, head.size() - n);
            const std::string_view lead = LastChar(lead_text);
            adnominal = NameClassOf(lead) != kClassHiragana ||
                        (Has(kKanaBefore, lead) &&
                         NameClassOf(LastChar(lead_text.substr(0, lead_text.size() - lead.size()))) != kClassHiragana);
        }
    }
    switch (NameClassOf(prev)) {
    case kClassHiragana:
    case kClassKanji:
    case kClassKatakana:
        if (!Has(kKanaBefore, prev) && !adnominal &&
            !(pc.size() > 1 && pc[1] == '4' && (DoubledSoundBefore(query) || KanaModifierBefore(query))) &&
            !KanaAfterWord(u, query)) {
            return false;
        }
    }
    const std::string_view rest = query.substr(std::min(u.key.size(), query.size()));
    // A verb stem with no kana ending after it (before a mark, a kanji, a katakana or the end) never beats a name
    // or a noun of the base under the same key (ゆめみ。 유메미, はえ、 파리; LEX-5923).
    if (pc.size() > 1 && pc[1] == '4' && NameClassOf(FirstChar(rest)) != kClassHiragana) {
        if (!snapshot.NameRecords(u.key).empty()) return false;
        for (const dict::Candidate& c : snapshot.RetrieveRecords(u.key, dict::LookupMode::General)) {
            const std::string_view p = c.codes->partText;
            if (c.user == nullptr && p.size() > 1 && p[1] == '1') return false;
        }
    }
    // A verb or adjective spelling that reads as the particle は and a final particle (はね: は + ね) needs a
    // full hiragana after it: before a mark, a kanji or the end it is は and the final particle (私はね、,
    // はね配合比); はだけ、 stays the verb.
    if (pc.size() > 1 && (pc[1] == '4' || pc[1] == '7')) {
        // After に, と, へ or の a は that starts the spelling is the particle of には, とは, へは or のは
        // (くらいにはなれる 정도는 될 수 있다, そういうのはやめて; LEX-5922); after で it may be the verb (手探りではだけ).
        if (u.key.starts_with("は") && !prev.empty() && Has("にとへの", prev)) return false;
        // A small kana or a long mark draws the particle out (あとはねぇ, はねっかえり), which no verb ending starts.
        return !u.key.starts_with("は") ||
               !dict::InClass(HDTRANS_CLASS("particle.final-after-past"), u.key.substr(std::string_view("は").size())) ||
               (NameClassOf(FirstChar(rest)) == kClassHiragana && !Has("ぁぃぅぇぉっゃゅょゎ", FirstChar(rest)));
    }
    const std::string_view next = FirstChar(rest);
    // A noun whose spelling is also the dictionary form, or in two kana the continuative, of a verb reads as the noun
    // only before が, を, や or the で of a noun (かえるや 개구리나, つきを 달을): elsewhere it is the verb (いえにかえる。,
    // かえるだけ, つきにくい; LEX-5925).
    // After an adnominal that only a noun follows it is the noun (そのかえるは 그 개구리는, あのつきは 저 달은) before a
    // particle, a mark or the end; before a kanji or katakana noun it is the verb of a relative clause (このかえる道
    // 이 돌아가는 길).
    // So does a noun of two kana whose first is the particle は: elsewhere it is は and the word after (ことはなにも,
    // はなから; はなが咲く 꽃이 핀다).
    if (pc.size() > 1 && pc[1] == '1' &&
        (VerbFormSpelling(u.key) || (text::RuneCount(u.key) == 2 && u.key.starts_with("は")))) {
        // After に, と, へ or の the は is the particle of には, とは, へは or のは, as for the verb spellings above
        // (それにはながい時間 그러기에는 긴 시간); an adnominal before it keeps the noun (そのはなが 그 꽃이).
        if (!VerbFormSpelling(u.key) && !prev.empty() && Has("にとへの", prev) && !adnominal) return false;
        // After the adnominal the key and the kana after it may be a word of their own, whose rules read the key
        // (このつきの光: つきの 付きの, whose rule gives 달).
        return next == "が" || next == "を" || next == "や" || KanaBeforeDe(u, rest) ||
               (adnominal && (NameClassOf(next) == kClassOther ||
                              (NameClassOf(next) == kClassHiragana &&
                               snapshot.RetrieveRecords(u.key + std::string(next), dict::LookupMode::General).empty())));
    }
    switch (NameClassOf(next)) {
    case kClassHiragana:
        // な and がら after it are the ながら of a verb stem (みながら 보면서).
        if (u.key.ends_with("な") && rest.starts_with("がら")) return false;
        // An adjectival noun of three letters or more takes its adnominal な (まことしやかな噂; LEX-5116).
        if (next == "な" && pc.size() > 1 && pc[1] == '8' && text::RuneCount(u.key) >= 3 && !rest.starts_with("なが")) return true;
        return Has(kKanaAfter, next) || KanaBeforeCopula(u, query) || KanaBeforeDe(u, rest) || KanaBeforeSuru(u, rest) ||
               KanaBeforeParticle(u, rest);
    case kClassKanji:
        return dict::InClass(HDTRANS_CLASS("word.kana-before-word"), u.key);
    }
    return text::RuneCount(u.key) >= 3;
}

// Whether the kana spelling u, a noun of two letters or more, stands before a particle of particle.kana-word-after
// (もんくばかり 불평만, なやみだけ 고민만): not where the key and the kana after it are the stem of a verb or adjective
// that takes the text after as its ending (かな + しかった is かなしかった, かわ + からない is か + わからない).
bool Worker::KanaBeforeParticle(const dict::UserEntry& u, std::string_view rest) const {
    const std::string_view pc = u.codes->partText;
    if (pc.size() < 2 || pc[1] != '1' || text::RuneCount(u.key) < 2 ||
        dict::StartsWithMember(HDTRANS_CLASS("particle.kana-word-after"), rest) == 0) {
        return false;
    }
    // The stem may also start inside the key (ことかわからない: わか + らない, not かわ + から).
    for (std::size_t start = 0; start < u.key.size(); start += FirstChar(std::string_view(u.key).substr(start)).size()) {
        std::string key = u.key.substr(start);
        for (std::size_t k = 0; k < rest.size();) {
            const std::size_t n = FirstChar(rest.substr(k)).size();
            if (n == 0 || NameClassOf(rest.substr(k, n)) != kClassHiragana) break;
            key.append(rest.substr(k, n));
            k += n;
            for (const dict::Candidate& c : snapshot.RetrieveRecords(key, dict::LookupMode::General)) {
                const std::string_view pt = c.codes == nullptr ? std::string_view() : c.codes->partText;
                if (pt.size() > 1 && (pt[1] == '4' || pt[1] == '7') && StemTakes(c, rest.substr(k))) return false;
            }
        }
    }
    return true;
}

// Whether the kana spelling u, a noun or adjectival noun of two letters or more that the name layer
// does not hold as a person, is followed by the particle で (このへんで, ぶんざいで, かわで; LEX-4721,
// LEX-5925): not where the key without a last い or ん is the stem of a godan verb whose て form that で ends (およいで, ならんで), as
// for the copula だ (KanaBeforeCopula), nor after a last ま or の, whose まで or ので it is (こうまで), nor where
// the end of the key and で are a word of their own (みたところで).
bool Worker::KanaBeforeDe(const dict::UserEntry& u, std::string_view rest) const {
    const std::string_view pc = u.codes->partText;
    if (!rest.starts_with("で") || text::RuneCount(u.key) < 2 || pc.size() < 2 || (pc[1] != '1' && pc[1] != '8') ||
        snapshot.NamePerson(u.key) || ProvenName(u.key)) {
        return false;
    }
    const std::string_view last = LastChar(u.key);
    // The まで and ので its last kana starts with that で ends (こうまで, not 仔馬 + で), and a word of the
    // dictionary that the end of the key and で spell (みたところで, ところで).
    if (last == "ま" || last == "の") return false;
    for (std::size_t at = 0; at < u.key.size(); at += text::DecodeRune(std::string_view(u.key).substr(at)).size) {
        const std::string tail = std::string(u.key.substr(at)) + "で";
        if (text::RuneCount(tail) >= 3 && !snapshot.RetrieveRecords(tail, dict::LookupMode::General).empty()) return false;
    }
    const std::string_view rows = last == "い" ? "2" : last == "ん" ? "56<" : "";
    if (rows.empty()) return true;
    const std::string_view stem = std::string_view(u.key).substr(0, u.key.size() - last.size());
    for (const dict::Candidate& c : snapshot.RetrieveRecords(stem, dict::LookupMode::General)) {
        const std::string_view vp = c.codes->partText;
        if (vp.size() == 4 && vp[1] == '4' && Has(rows, vp.substr(3, 1))) return false;
    }
    return true;
}

// LEX-6403: whether the kana spelling u, a verbal noun (a noun row with Hd) of three letters or more that the name
// layer does not hold as a person, is followed by its する: し and a form of する (あやとりしよう 실뜨기를 하자,
// れんしゅうしてる 연습하고 있다), or する and すれ (あやとりするの). The さ, せ and す of other words stay theirs
// (ねんどさん, ねんどすき).
bool Worker::KanaBeforeSuru(const dict::UserEntry& u, std::string_view rest) const {
    const std::string_view pc = u.codes->partText;
    if (text::RuneCount(u.key) < 3 || pc.size() < 2 || pc[1] != '1' || !u.codes->gram.HasCode("Hd") ||
        snapshot.NamePerson(u.key) || ProvenName(u.key)) {
        return false;
    }
    if (rest.starts_with("する") || rest.starts_with("すれ")) return true;
    if (!rest.starts_with("し")) return false;
    const std::string_view after = rest.substr(std::string_view("し").size());
    return SuruFormAfterShi(after) || dict::StartsWithMember(HDTRANS_CLASS("suru.tail"), after) > 0;
}

// Whether the kana spelling u, a noun or adjectival noun of three letters or more that the name
// layer does not hold as a person, is followed by the copula だ, です, でした, でしょ, じゃ (LEX-94), or the な of
// なの or なん.
// だ and the じゃ of じゃう, じゃった, じゃえ after a key ending in い or ん are the past and the
// contracted てしまう of a godan verb when the key without that kana is the stem of a verb of the
// matching row in the dictionary: が after い (ゆらいだ), ま, ば, な after ん (こばんだ, 飲んじゃった).
bool Worker::KanaBeforeCopula(const dict::UserEntry& u, std::string_view query) const {
    const std::string_view pc = u.codes->partText;
    if (text::RuneCount(u.key) < 3 || pc.size() < 2 || (pc[1] != '1' && pc[1] != '8') ||
        snapshot.NamePerson(u.key)) {
        return false;
    }
    const std::string_view rest = query.substr(std::min(u.key.size(), query.size()));
    // The な of なの and なん after a noun is the copula too (かれしなんだ, かれしなの; LEX-5116).
    for (const std::string_view c : {"です", "でした", "でしょ", "なの", "なん"}) {
        if (rest.starts_with(c)) return true;
    }
    bool verbShape = false;
    if (rest.starts_with("だ")) {
        verbShape = true;
    } else if (rest.starts_with("じゃ")) {
        const std::string_view after = FirstChar(rest.substr(std::string_view("じゃ").size()));
        verbShape = after == "う" || after == "っ" || after == "え";
    } else {
        return false;
    }
    if (!verbShape) return true;
    const std::string_view last = LastChar(u.key);
    const std::string_view rows = last == "い" ? "2" : last == "ん" ? "56<" : "";
    if (rows.empty()) return true;
    const std::string_view stem = std::string_view(u.key).substr(0, u.key.size() - last.size());
    const std::vector<dict::Candidate> records = snapshot.RetrieveRecords(stem, dict::LookupMode::General);
    for (const dict::Candidate& c : records) {
        const std::string_view vp = c.codes->partText;
        if (vp.size() == 4 && vp[1] == '4' && Has(rows, vp.substr(3, 1))) return false;
    }
    return true;
}

// Text path, mode 0: the base interjections and set phrases that are a verb form ending
// in て or ます with a Korean other than the verb's are dropped where they are the verb: before
// hiragana, and for ます after anything but the start of the token or punctuation.
void Worker::DropBoundUtterances(Lookup& l, std::string_view query) {
    const std::string_view surface = transformSurface;
    const bool alone = EndsSurface(query) &&
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
// Text path, mode 0: a word of word.closing-utterance (ゴメンな) is an utterance of its
// own only where the sentence or a clause ends after it and no particle of
// particle.before-predicate stands before it; elsewhere the words it starts with read it
// (ゴメンなら, そんなのゴメンよ 싫어). A word of word.closing-greeting needs only the end
// (これからもよろしくな; not よろしくない).
void Worker::DropOpenUtterances(Lookup& l, std::string_view query) {
    const std::string_view surface = transformSurface;
    std::string_view before;
    if (EndsSurface(query) && surface.size() > query.size()) {
        before = LastChar(surface.substr(0, surface.size() - query.size()));
    } else if (current > 0) {
        before = LastChar(Tok(current - 1).text);
    }
    const bool predicate = dict::InClass(HDTRANS_CLASS("particle.before-predicate"), before);
    Record* out = nullptr;
    Record* last = nullptr;
    for (Record* r = l.records; r != nullptr; r = r->child) {
        const bool fits = r->source.size() <= query.size();
        const bool utterance = fits && dict::InClass(HDTRANS_CLASS("word.closing-utterance"), r->source);
        const bool greeting = fits && dict::InClass(HDTRANS_CLASS("word.closing-greeting"), r->source);
        if ((utterance || greeting) &&
            ((utterance && predicate) || !SentenceFinal(query.substr(r->source.size()), current, true))) {
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
    const bool afterKa = EndsSurface(query) && surface.substr(0, surface.size() - query.size()).ends_with("か");
    Record* out = nullptr;
    Record* last = nullptr;
    for (Record* r = l.records; r != nullptr; r = r->child) {
        if (SplitsModal(*r, query, afterKa) || SplitsKashira(*r, query) || SplitsDeshou(*r, query) || SplitsVerb(*r, query) ||
            SplitsPrefixWord(*r, query) || (At(r->partCode, 0) == 'P' && query.starts_with(r->source) &&
                                            CopulaAfterNominal(r->source, query.substr(r->source.size())))) {
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

// A word that ends in で and is no particle or ending, before しょ: the で belongs to the copula
// でしょう (初めてでしょう 처음이죠), not to the word (初めてで 처음으로). Not before しょうがない
// (それでしょうがない).
bool Worker::SplitsDeshou(const Record& r, std::string_view query) const {
    if (r.source.size() <= std::string_view("で").size() || !r.source.ends_with("で") || !query.starts_with(r.source) ||
        AnyOf("PSTU", At(r.partCode, 0))) {
        return false;
    }
    const std::string_view rest = query.substr(r.source.size());
    return rest.starts_with("しょ") && !rest.starts_with("しょうがな") && !rest.starts_with("しょうがね") &&
           !rest.starts_with("しょうが無");
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

// A proper noun of a prefix kanji and one more kanji, outside a run of kanji, whose last kanji
// starts a longer common word of two kanji or more that ends before hiragana, katakana,
// punctuation or the end (大間違い: 大 and 間違い, not the place 大間 and 違い; 大満足, 大手柄): the
// prefix stands alone. A kanji or a person suffix after that word, or a word of place, keeps the
// record (小西洋子, 大東さん, 大川沿い).
bool Worker::SplitsPrefixWord(const Record& r, std::string_view query) const {
    if (!ProperCodes(r.partCode, r.semantic) || !query.starts_with(r.source) || text::RuneCount(r.source) != 2 ||
        NameClassOf(LastChar(r.source)) != kClassKanji) {
        return false;
    }
    const std::string_view surface = transformSurface;
    if (!EndsSurface(query) || NameClassOf(LastChar(surface.substr(0, surface.size() - query.size()))) == kClassKanji) {
        return false;
    }
    const std::string_view last = LastChar(r.source);
    const std::string_view head = r.source.substr(0, r.source.size() - last.size());
    if (NameClassOf(head) != kClassKanji) return false;
    bool prefix = false;
    for (const dict::Candidate& c : snapshot.RetrieveRecords(head, dict::LookupMode::General)) {
        prefix = prefix || c.codes->gram.HasCode("Z6");
    }
    if (!prefix) return false;
    const std::string_view tail = query.substr(head.size());
    const std::size_t word = snapshot.WordAt(tail);
    if (word <= last.size()) return false;
    int kanji = 0;
    for (std::string_view w = tail.substr(0, word); !w.empty(); w.remove_prefix(FirstChar(w).size())) {
        kanji += NameClassOf(FirstChar(w)) == kClassKanji ? 1 : 0;
    }
    const std::string_view rest = tail.substr(word);
    if (kanji < 2 || NameClassOf(FirstChar(rest)) == kClassKanji || StartsWithHonorific(rest) || PersonSuffixAt(rest)) {
        return false;
    }
    // A word of place keeps the name it follows (大川沿い 오카와 강가, not 대 강가).
    const std::vector<dict::Candidate> common = snapshot.RetrieveRecords(tail.substr(0, word), dict::LookupMode::General);
    if (std::any_of(common.begin(), common.end(), [](const dict::Candidate& c) { return c.codes->sem.HasCode("VPl"); })) {
        return false;
    }
    return std::any_of(common.begin(), common.end(),
                       [](const dict::Candidate& c) { return !ProperCodes(c.codes->partText, c.codes->sem.Text()); });
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

bool Worker::StemTakesSu(std::string_view stem, std::string_view ending) const {
    if (!ending.starts_with("す")) return false;
    // the kana that end the stem, two to four of them: a kana verb in す (いやす, ためす, なおす)
    std::size_t at = stem.size();
    for (int n = 1; n <= 4 && at > 0; n++) {
        const text::Rune r = DecodeLastRune(stem.substr(0, at));
        if (!(r.cp >= U'ぁ' && r.cp <= U'ゖ')) break;
        at -= r.size;
        if (n >= 2 && VerbFormSpelling(std::string(stem.substr(at)) + "す")) return true;
    }
    return false;
}

// Whether form is also the dictionary form of a verb of the dictionary, or a continuative of two kana: form without
// its last kana is the stem of a verb whose dictionary form or continuative ends in that kana (かえる of 帰る and
// 変える, つき of 付く).
bool Worker::VerbFormSpelling(std::string_view form) const {
    const std::string_view last = LastChar(form);
    const std::string_view stem = form.substr(0, form.size() - last.size());
    if (stem.empty()) return false;
    // A longer noun in a continuative is mostly the noun grown from it (くぼみ, ひかり).
    const bool two = text::RuneCount(form) == 2;
    // A stem of one kana is a verb of its own only in a record that needs no context: one with a rule is read so only
    // where its rule says (the お of おぎ 荻 is 오다 only after に and a verb).
    const bool short_stem = text::RuneCount(stem) == 1;
    for (const dict::Candidate& rec : snapshot.RetrieveRecords(stem, dict::LookupMode::General)) {
        const std::string_view pc = rec.codes->partText;
        if (pc.size() != 4 || pc[0] != 'M' || pc[1] != '4' || (short_stem && rec.rule != 0)) continue;
        const int kind = pc[3] - '0';
        if (kind >= 0 && kind < kConjugationKinds && (Conjugation(kind, 0) == last || (two && Conjugation(kind, 10) == last))) return true;
    }
    // In two kana it may be the stem, and so the continuative, of an ichidan verb (つき of 尽きる).
    if (two) {
        for (const dict::Candidate& rec : snapshot.RetrieveRecords(form, dict::LookupMode::General)) {
            const std::string_view pc = rec.codes->partText;
            if (pc.size() == 4 && pc[0] == 'M' && pc[1] == '4' && pc[3] == '7') return true;
        }
    }
    return false;
}

// Whether a cue of the line or its document has shown the name-layer key to be a person's name (ももさんが来た。
// before ももで).
bool Worker::ProvenName(std::string_view key) const {
    return lookupContext.cued != nullptr && lookupContext.cued->find(key) != lookupContext.cued->end();
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

// Whether stem is a dictionary verb in させ that is the causative of a shorter word the dictionary
// has, an ichidan verb or a verbal noun (食べさせ of 食べる, 入院させ). Verbs in せ of a godan verb
// (合わせる, 驚かせる) are words of their own.
bool Worker::CausativeOfShorter(std::string_view stem) const {
    constexpr std::string_view kSase = "させ";
    if (!stem.ends_with(kSase) || stem.size() == kSase.size()) return false;
    const std::string_view shorter = stem.substr(0, stem.size() - kSase.size());
    for (const dict::Candidate& rec : snapshot.RetrieveRecords(shorter, dict::LookupMode::General)) {
        const std::string_view pc = rec.codes->partText;
        if (rec.source != shorter || pc.size() != 4) continue;
        if ((pc.starts_with("M4") && pc[3] == '7') || (pc[0] == 'N' && rec.codes->gram.HasCode("Hd"))) return true;
    }
    return false;
}

// Whether stem is a す-verb stem all of whose Korean is a causative in 게 (引か 끌게 하다; not 動か
// 움직이다, whose せ is the potential 動かせる) and that ends in the negative stem of a godan verb
// (引く): its せ is that verb's causative (引かせる), not the potential of the す-verb.
bool Worker::GodanCausative(std::string_view stem) const {
    const std::string_view last = LastChar(stem);
    const std::string_view godan = stem.substr(0, stem.size() - last.size());
    if (godan.empty()) return false;
    bool causative = false;
    for (const dict::Candidate& rec : snapshot.RetrieveRecords(stem, dict::LookupMode::General)) {
        const std::string_view pc = rec.codes->partText;
        if (rec.source != stem || pc.size() != 4 || !pc.starts_with("M4") || pc[3] != '3') continue;
        if (rec.target.find("게 ") == std::string_view::npos) return false;
        causative = true;
    }
    if (!causative) return false;
    for (const dict::Candidate& rec : snapshot.RetrieveRecords(godan, dict::LookupMode::General)) {
        const std::string_view pc = rec.codes->partText;
        if (rec.source != godan || pc.size() != 4 || !pc.starts_with("M4")) continue;
        const int kind = pc[3] - '0';
        if (kind != 3 && kind != 7 && kind >= 0 && kind <= 8 && Conjugation(kind, 8) == last) return true;
    }
    return false;
}

// LEX-5014: whether stem, or its part from its last kanji (唇を尖ら: 尖ら), is the negative stem of a godan verb of
// the dictionary (尖ら of 尖る, 滑ら of 滑る), the verb a す verb of that stem makes act (尖らす).
bool Worker::GodanBase(std::string_view stem) const {
    std::size_t from = 0;
    for (std::size_t i = 0; i < stem.size(); i += text::DecodeRune(stem.substr(i)).size) {
        if (text::IsKanji(text::DecodeRune(stem.substr(i)).cp)) from = i;
    }
    const std::string_view part = stem.substr(from);
    const std::string_view last = LastChar(part);
    const std::string_view godan = part.substr(0, part.size() - last.size());
    if (godan.empty()) return false;
    for (const dict::Candidate& rec : snapshot.RetrieveRecords(godan, dict::LookupMode::General)) {
        const std::string_view pc = rec.codes->partText;
        if (rec.source != godan || pc.size() != 4 || !pc.starts_with("M4")) continue;
        const int kind = pc[3] - '0';
        if (kind != 3 && kind != 7 && kind >= 0 && kind <= 8 && Conjugation(kind, 8) == last) return true;
    }
    return false;
}

// LEX-5014: whether the last character of a godan stem and the e-row kana of its potential spell the
// stem of an ichidan verb of the dictionary (入 + れ: 入れる, 付 + け: 付ける, 売 + れ: 売れる), the
// transitive or intransitive partner that a て form reads before the potential (勢い付けて, 売れており).
bool Worker::IchidanPartner(std::string_view stem, std::string_view kana) const {
    const std::string_view last = LastChar(stem);
    if (last.empty() || !text::IsKanji(text::DecodeRune(last).cp)) return false;
    const std::string key = std::string(last) + std::string(kana);
    for (const dict::Candidate& rec : snapshot.RetrieveRecords(key, dict::LookupMode::General)) {
        const std::string_view pc = rec.codes->partText;
        if (rec.source == key && pc.size() == 4 && pc.starts_with("M4") && pc[3] == '7') return true;
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
                                                    "ー", "―", "—", "─", "♪", "♡", "♥", "❤", "☆", "★", "／", "≫", "》", "】", "〉", "＞", ".", "!", "?", "っ", "ッ",
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
    // A space before the mark that ends the sentence, as captions write it (そうっすね 。).
    // Only a mark that ends a sentence counts there (. ! ? 。 ！ ？ ｡), not a comma, a ー or a closing bracket.
    if ((next.type == tokenizer::kTypeAsciiSpace || next.type == tokenizer::kTypeWideSpace) && token + 2 < TokenCount()) {
        const std::string_view after = Tok(token + 2).text;
        for (const std::string_view mark : {".", "!", "?", "。", "！", "？", "｡"}) {
            if (after.starts_with(mark)) return true;
        }
    }
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
// An ending in です also gives its す to an adverb or interjection that starts with it
// (寝込んだのですっかり: ので + すっかり, 遅れたのですみません: ので + すみません).
// An ending in か before ら gives its か to the particle から (疲れただろうから: だろう + から,
// いいでしょうから, 渡してるんですから), and one in もので or わけで gives its で to the copula after it
// (CopulaAfterNominal).
bool Worker::CutsWord(std::string_view before, std::string_view ending, std::string_view after) const {
    if (!settings.text) return false;
    if (ending.ends_with("ため") && after.starts_with("息")) return true;
    // An ending that ends in a kanji (度, 際, 他, 暇, 訳 after a verb) gives that kanji to a common noun of the dictionary
    // that starts with it and reaches at least as far as the kanji and the word after it (辞める度胸 그만둘 배짱, 戦う際限,
    // 見る他人; spec LEX-4982).
    // Not 時, which also stands for the とき of an adverb (あるとき人の 어느 날 사람의, not 時人).
    if (!ending.empty() && !after.empty() && text::IsKanji(DecodeLastRune(ending).cp) && DecodeLastRune(ending).cp != U'時') {
        const text::Rune kanji = DecodeLastRune(ending);
        const std::string_view rest = after.substr(0, std::min<std::size_t>(after.size(), 24));
        std::size_t word = 0;
        for (const dict::Candidate& c : snapshot.GetWordInfo(rest, dict::LookupMode::General, lookupContext).list) {
            if (c.user == nullptr || !c.user->nameLayer) word = std::max(word, text::RuneCount(c.source));
        }
        std::string query(ending.substr(ending.size() - kanji.size));
        query += rest;
        for (const dict::Candidate& c : snapshot.GetWordInfo(query, dict::LookupMode::General, lookupContext).list) {
            const std::string_view part = c.codes != nullptr ? std::string_view(c.codes->partText) : std::string_view();
            const std::size_t n = text::RuneCount(c.source);
            if ((c.user == nullptr || !c.user->nameLayer) && part.starts_with("N1") && part.substr(2, 1) != "3" && n >= 2 &&
                n >= 1 + word) {
                return true;
            }
        }
    }
    if (ending.ends_with("か") && after.starts_with("ら")) return true;
    if (CopulaAfterNominal(ending, after)) return true;
    // An ending in けども or とも gives its も, and one in と its と, to an adverb that starts with it and
    // reaches further than the words after the ending do (分からないけどもう一度: けど + もう一度, けどもしかしたら,
    // 笑われるともはや: 笑われると + もはや, 分からないとても: 分からない + とても, とにかく; not けども + うん).
    const auto givesToAdverb = [&](std::string_view kana) {
        if (after.empty()) return false;
        const std::string_view rest = after.substr(0, std::min<std::size_t>(after.size(), 24));
        std::size_t word = 0;
        for (const dict::Candidate& c : snapshot.GetWordInfo(rest, dict::LookupMode::General, lookupContext).list) {
            if (c.user == nullptr || !c.user->nameLayer) word = std::max(word, text::RuneCount(c.source));
        }
        std::string query(kana);
        query += rest;
        for (const dict::Candidate& c : snapshot.GetWordInfo(query, dict::LookupMode::General, lookupContext).list) {
            const std::string_view part = c.codes != nullptr ? std::string_view(c.codes->partText) : std::string_view();
            const std::size_t n = text::RuneCount(c.source);
            if ((c.user == nullptr || !c.user->nameLayer) && At(part, 0) == 'E' && n >= 2 && n >= word + 1) return true;
        }
        return false;
    };
    if ((ending.ends_with("けども") || ending.ends_with("とも")) && givesToAdverb("も")) return true;
    // An ending in ての or での (来ての) gives its の to a verb or an adverb that starts with it and reaches further than the
    // words after the ending do (倒れてのたうち回った: 倒れて + のたうち回った, 走ってのぞいた: のぞく; spec GRM-4002).
    const std::string teNo = std::string(before) + std::string(ending);
    if ((teNo.ends_with("ての") || teNo.ends_with("での")) && !after.empty()) {
        const std::string_view rest = after.substr(0, std::min<std::size_t>(after.size(), 24));
        std::size_t word = 0;
        for (const dict::Candidate& c : snapshot.GetWordInfo(rest, dict::LookupMode::General, lookupContext).list) {
            if (c.user == nullptr || !c.user->nameLayer) word = std::max(word, text::RuneCount(c.source));
        }
        std::string query = "の";
        query += rest;
        for (const dict::Candidate& c : snapshot.GetWordInfo(query, dict::LookupMode::General, lookupContext).list) {
            const std::string_view part = c.codes != nullptr ? std::string_view(c.codes->partText) : std::string_view();
            const std::size_t n = text::RuneCount(c.source);
            const bool verbOrAdverb = (At(part, 0) == 'M' && At(part, 1) == '4') || At(part, 0) == 'E';
            if ((c.user == nullptr || !c.user->nameLayer) && verbOrAdverb && n >= 2 && n >= word + 1) return true;
        }
    }
    // An ending in か gives it to かなり (してしまうかなり: してしまう + かなり).
    if (ending.ends_with("か") && after.starts_with("なり") && givesToAdverb("か")) return true;
    static constexpr std::string_view kToAdverbRests[] = {"ても", "っても", "もかく", "にかく"};
    if (ending.ends_with("と") &&
        std::any_of(std::begin(kToAdverbRests), std::end(kToAdverbRests), [&](std::string_view r) { return after.starts_with(r); }) &&
        givesToAdverb("と")) {
        return true;
    }
    if (ending.ends_with("です") && !after.empty()) {
        std::string query = "す";
        query += after.substr(0, std::min<std::size_t>(after.size(), 24));
        for (const dict::Candidate& c : snapshot.GetWordInfo(query, dict::LookupMode::General, lookupContext).list) {
            const std::string_view part = c.codes != nullptr ? std::string_view(c.codes->partText) : std::string_view();
            const bool adverb = At(part, 0) == 'E' || At(part, 0) == 'A' || (At(part, 0) == 'P' && At(part, 1) != '2');
            if ((c.user == nullptr || !c.user->nameLayer) && adverb && text::RuneCount(c.source) >= 2) {
                return true;
            }
        }
    }
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

// Whether the hiragana after an adnominal な, after, start with く and a word of the
// dictionary of two or more kana, no ending, that reaches further than the keys that start
// at the な, which are all forms of a verb or an adjective (なく of ない): the な stays the
// adnominal before it where the validity rule of な before く reads なく (ような + くさい, not
// なく + さい; but 遠慮 + なく, and 遠慮 + なくして, whose なくし reaches as far as くし).
bool Worker::WordAfterAdnominal(std::string_view after) const {
    if (!settings.text || !after.starts_with("く")) return false;
    const std::string_view rest = after.substr(0, std::min<std::size_t>(after.size(), 24));
    std::size_t word = 0;
    for (const dict::Candidate& c : snapshot.GetWordInfo(rest, dict::LookupMode::General, lookupContext).list) {
        if ((c.user != nullptr && (c.user->nameLayer || c.user->end)) || c.codes == nullptr || c.codes->part.TagIn("PSTU")) {
            continue;
        }
        word = std::max(word, text::RuneCount(c.source));
    }
    if (word < 2) return false;
    std::string query(kJaNa);
    query += rest;
    std::size_t na = 0;
    for (const dict::Candidate& c : snapshot.GetWordInfo(query, dict::LookupMode::General, lookupContext).list) {
        if (c.user != nullptr && c.user->nameLayer) continue;
        const std::size_t n = text::RuneCount(c.source);
        // A word of its own at the な (なし, など, 内容 of ないよう) keeps the rule.
        if (n >= 2 && (c.codes == nullptr || !c.codes->part.TagIn("HM"))) return false;
        na = std::max(na, n);
    }
    return 1 + word > na;
}

bool OwnedGroup(const Group* g) { return g != nullptr && !g->entries.empty() && g->entries[0]->user != nullptr; }

// Whether no validity rule rejects the text that follows source (a word of class first,
// followed by class second).
bool Worker::MatchDictionary(std::string_view following, std::string_view source, char first, char second) {
    std::string query;
    for (const ValidityRule& r : ValidityRules()) {
        if (r.sourceClass != first) continue;
        // A source of XX applies to every word of its class.
        if ((r.source[0] != 'X' || r.source[1] != 'X') && (second != r.followClass || std::string_view(r.source) != source)) {
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
