#include "core/engine/driver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/dict/char_readings.h"
#include "core/dict/classes.h"
#include "core/engine/glue.h"
#include "core/engine/resolve.h"
#include "core/engine/rules.h"
#include "core/engine/wrapped.h"
#include "core/filter/filter.h"
#include "core/names/document.h"
#include "core/output/cleanup.h"
#include "core/output/compose.h"
#include "core/pipeline/normalize.h"
#include "core/pipeline/protect.h"
#include "core/pipeline/restore.h"
#include "core/pipeline/runes.h"
#include "core/pipeline/segment.h"
#include "core/pipeline/sound.h"
#include "core/pipeline/speech.h"
#include "core/register/retrofit.h"
#include "core/tokenizer/tokenize.h"
#include "core/text/hangul.h"
#include "core/text/lines.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
#include "core/word/conjugation.h"
#include "core/word/flush.h"
#include "core/word/util.h"

namespace hdTrans::internal {

namespace {

// The class digit of a dictionary candidate ('4' a verb, '1' a noun, '2' a particle).
char ClassOf(const dict::Candidate& c) {
    return c.codes ? c.codes->part.ClassDigit() : '\0';
}

// Whether the dictionary has an ending row whose key is text (こと, もの, ところ): a noun
// that the ending of the word before it reads with it.
bool EndingKey(const dict::Snapshot& snap, std::string_view text) {
    const dict::Candidates found = snap.GetWordInfo(text, dict::LookupMode::VerbEnding, dict::LookupContext{});
    return std::any_of(found.list.begin(), found.list.end(),
                       [&](const dict::Candidate& c) { return c.source == text; });
}

// The first word of the word analysis of base + kana when it is a verb or adjective that
// starts the text and takes some of the kana; null otherwise.
const word::Item* InflectedBase(const word::Analysis& analysis, std::size_t base) {
    if (analysis.sentences.empty()) return nullptr;
    const word::Item* first = analysis.sentences.front().chain.head;
    if (first == nullptr || first->tokenIndex != 0 || first->start != 0) return nullptr;
    const int kind = first->detail->cls;
    if (kind != word::kDetailVerb && kind != word::kDetailKeiyo) return nullptr;
    return static_cast<std::size_t>(first->length) > base ? first : nullptr;
}

// The Korean of the candidates of class digit cls whose key is text.
std::vector<std::string_view> Targets(const dict::Snapshot& snap, std::string_view text, char cls) {
    std::vector<std::string_view> out;
    for (const dict::Candidate& c : snap.GetWordInfo(text, dict::LookupMode::General, dict::LookupContext{}).list) {
        if (c.source == text && ClassOf(c) == cls) out.push_back(c.target);
    }
    return out;
}

// Whether the verb stem group of base and the particle that starts kana conjugates as する,
// ずる, くる or 来る (kind 9, 10, 11 or 13 of its part code "429"), no local rule reads it, and
// a Korean of it splits into a noun of base, the particle and the verb (word::IdiomNoun): the
// word stage reads it around the ruby tags (word::RubyIdiomVerb: 声が 목소리가 들&AA@).
bool SplitIdiomStem(const dict::Snapshot& snap, const word::Group& group, std::string_view base, std::string_view kana) {
    if (std::any_of(group.entries.begin(), group.entries.end(), [](const word::Entry* e) { return e->ruleId > 0; })) {
        return false;
    }
    const std::vector<std::string_view> nouns = Targets(snap, base, '1');
    const std::vector<std::string_view> particles = Targets(snap, kana.substr(0, text::DecodeRune(kana).size), '2');
    return std::any_of(group.entries.begin(), group.entries.end(), [&](const word::Entry* e) {
        if (e->code.size() < 3) return false;
        const char kind = e->code[2];
        return (kind == '9' || kind == ':' || kind == ';' || kind == '=') && word::IdiomNoun(e->dst, nouns, particles) >= 0;
    });
}

// Whether the word analysis of kana alone starts with a verb, or with a particle and a verb: the
// light verb that word::RubyIdiomVerb reads after a ruby base (がした, にした; not がしない,
// whose しな is no verb there).
bool StartsLightVerb(const dict::Snapshot& snap, std::string_view kana) {
    tokenizer::Tokens tokens;
    tokenizer::Tokenize(kana, tokens);
    word::Settings settings;
    settings.text = true;
    word::Arena arena;
    word::Analysis analysis;
    word::Analyze(tokens, snap, dict::LookupContext{}, settings, arena, analysis);
    if (analysis.sentences.empty()) return false;
    const word::Item* it = analysis.sentences.front().chain.head;
    if (it != nullptr && it->Code(0).starts_with("2")) it = it->next;
    return it != nullptr && it->detail->cls == word::kDetailVerb;
}

// The bytes of base + kana that the inflected base word takes when the next word inside the
// kana is a dictionary noun that is not also an ending (掬った|まぐろ, not 躓いてるって|こと)
// or an auxiliary verb (Ds, Du, Da: 庇って|くれた, 囚われて|いる); 0 otherwise. Endings read
// with the word (呟いたあと, 寂しいけれど) and verbs it compounds with (尋ね|かける) stay with it.
std::size_t BaseWordEnd(const dict::Snapshot& snap, std::string_view text, const word::Item& first) {
    const auto end = static_cast<std::size_t>(first.length);
    const word::Item* next = first.next;
    if (end >= text.size() || next == nullptr || next->tokenIndex != 0 || static_cast<std::size_t>(next->start) != end) {
        return 0;
    }
    const std::string_view code = next->Code(0);
    const char cls = code.empty() ? '\0' : code[0];
    if (cls == '1') return word::Has(next->GAttr(0), "Nm") || EndingKey(snap, next->group->surface) ? 0 : end;
    if (cls != '4') return 0;
    for (int i = 0; next->group->At(i) != nullptr; i++) {
        const std::string_view g = next->GAttr(i);
        if (word::Has(g, "Ds") || word::Has(g, "Du") || word::Has(g, "Da")) return end;
    }
    return 0;
}

// How the dictionary reads a ruby base with the kana after it (pipeline::JoinBraceRuby).
pipeline::BaseReading ReadBase(const void* context, std::string_view base, std::string_view kana) {
    const auto& snap = *static_cast<const dict::Snapshot*>(context);
    const std::string text = std::string(base) + std::string(kana);
    const std::size_t okurigana = base.size() + text::DecodeRune(kana).size;
    pipeline::BaseReading r;
    const dict::Candidates found = snap.GetWordInfo(text, dict::LookupMode::General, dict::LookupContext{});
    for (const dict::Candidate& c : found.list) {
        const char cls = ClassOf(c);
        if (cls == '4' || cls == '7' || cls == '8') r.continues = r.continues || c.source.size() >= base.size();
    }

    tokenizer::Tokens tokens;
    tokenizer::Tokenize(text, tokens);
    word::Settings settings;
    settings.text = true;
    word::Arena arena;
    word::Analysis analysis;
    word::Analyze(tokens, snap, dict::LookupContext{}, settings, arena, analysis);
    if (const word::Item* first = InflectedBase(analysis, base.size())) {
        // A stem key that goes on into the kana past their first character is an idiom
        // (人気があ), not the base's inflection, and so is a key that takes the first kana
        // as the particle of a noun before する or 来る (声がした, 手にした).
        const std::size_t key = static_cast<std::size_t>(first->group->length);
        r.inflected = key <= okurigana &&
                     !(key > base.size() && SplitIdiomStem(snap, *first->group, base, kana) && StartsLightVerb(snap, kana));
        // The end cuts the kana, so it must fall inside them on a character boundary.
        const std::size_t end = BaseWordEnd(snap, text, *first);
        if (end > base.size() && end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) != 0x80) {
            r.wordEnd = end - base.size();
        }
    }
    return r;
}

// The dictionary as the line's lookup sees it, for pipeline::RespellVariants.
struct RespellLookup {
    const dict::Snapshot& snap;
    const dict::LookupContext& lookup;
    const pipeline::LineContext* line = nullptr;  // for EndsInVerb
};

dict::Candidates Find(const RespellLookup& d, std::string_view text) {
    return d.snap.GetWordInfo(text, dict::LookupMode::General, d.lookup);
}

bool HasKey(const RespellLookup& d, std::string_view key) {
    const dict::Candidates found = Find(d, key);
    return std::any_of(found.list.begin(), found.list.end(), [&](const dict::Candidate& c) { return c.source == key; });
}

// Whether the candidate is an ending (S, T) or a particle (P) row rather than a word of its own.
bool Grammar(const dict::Candidate& c) { return c.codes != nullptr && c.codes->part.TagIn("STP"); }

// Words are looked for from up to this many runes before a variant, and the respelled text is
// looked up with this many bytes after it; a longer key around a variant is not seen.
constexpr int kLookBackRunes = 4;
constexpr size_t kLookAheadBytes = 192;

// Whether a dictionary word cuts across line[at, end) or holds it (pipeline::RespellVariants): a word
// that starts before it and ends inside it, one that starts inside it and ends past it, or one that
// holds all of it and more (身の程, 先程, ときめく), or a word other than an ending or particle that
// the respelled text would make before it (ある時 from あるとき). An ending or particle row holding
// the variant does not keep one respelled in kana when its kana respelling is also a key (程に ほどに).
bool WordKeepsVariant(const void* context, std::string_view line, size_t at, std::string_view variant,
                      std::string_view spelling) {
    const auto& d = *static_cast<const RespellLookup*>(context);
    const size_t end = at + variant.size();
    const bool toKana = !text::IsKanji(text::DecodeRune(spelling).cp);
    const auto keeps = [&](size_t start) {
        for (const dict::Candidate& c : Find(d, line.substr(start)).list) {
            const size_t stop = start + c.source.size();
            if (stop <= at || (start == at && stop <= end)) continue;
            if (start > at) {
                if (stop > end) return true;
                continue;
            }
            if (stop < end || !toKana || !Grammar(c)) return true;
            std::string respelled(c.source.substr(0, at - start));
            respelled += spelling;
            respelled += c.source.substr(end - start);
            if (!HasKey(d, respelled)) return true;
        }
        return false;
    };
    size_t first = at;
    for (int back = 0; back < kLookBackRunes && first > 0; back++) first -= pipeline::internal::LastRune(line.substr(0, first)).size;
    // The respelled text from first, so that the spelling starts at at - first.
    std::string respelled(line.substr(first, at - first));
    respelled += spelling;
    respelled += line.substr(end, kLookAheadBytes);
    const size_t spelled = at - first;
    const auto joins = [&](size_t from) {
        for (const dict::Candidate& c : Find(d, std::string_view(respelled).substr(from)).list) {
            if (from + c.source.size() < spelled + spelling.size() || Grammar(c)) continue;
            std::string original(c.source.substr(0, spelled - from));
            original += variant;
            original += c.source.substr(spelled - from + spelling.size());
            if (!HasKey(d, original)) return true;
        }
        return false;
    };
    for (size_t start = at;; start -= pipeline::internal::LastRune(line.substr(0, start)).size) {
        if (start < at && joins(start - first)) return true;
        if (keeps(start)) return true;
        if (start == first) break;
    }
    for (size_t inner = at + text::DecodeRune(line.substr(at)).size; !toKana && inner < end;
         inner += text::DecodeRune(line.substr(inner)).size) {
        if (keeps(inner)) return true;
    }
    return false;
}

// The lookups of a FusedTest (pipeline::CasualQuotes, pipeline::SpokenLine).
struct FusedLookup {
    const pipeline::LineContext& ctx;
    const dict::Snapshot& snap;
};

bool HoldsFusedAdjective(const void* context, std::string_view text) {
    const auto& f = *static_cast<const FusedLookup*>(context);
    return pipeline::internal::HoldsCasualAdjective(text, f.ctx, f.snap);
}

// Whether a line asks which of its polite quotes seek agreement politely with でしょ: it has
// でしょ and a quote of polite speech (TranslateBody).
bool PoliteTalkAsked(std::string_view text, const std::vector<std::uint8_t>& styles) {
    return text.find("でしょ") != std::string_view::npos &&
           std::ranges::any_of(styles, [](std::uint8_t style) { return (style & output::kPoliteStyle) != 0; });
}

// Keys holding a run are looked for from up to this many runes before it (a long name).
constexpr int kRunLookBackRunes = 32;

// Whether a dictionary word holds line[at, end) (pipeline::CollapseTsuRuns, pipeline::RespellOldForms):
// a key that starts at or up to kRunLookBackRunes letters before it and reaches its end.
bool WordHoldsRun(const void* context, std::string_view line, size_t at, size_t end) {
    const auto& d = *static_cast<const RespellLookup*>(context);
    size_t start = at;
    for (int back = 0;; back++) {
        for (const dict::Candidate& c : Find(d, line.substr(start)).list) {
            if (start + c.source.size() >= end) return true;
        }
        if (back == kRunLookBackRunes || start == 0) break;
        const text::Rune last = pipeline::internal::LastRune(line.substr(0, start));
        if (!pipeline::internal::IsJapaneseRune(last.cp) && last.cp != U'・') break;
        start -= last.size;
    }
    return false;
}

// Whether line[at, end) is a kanji that the dictionary holds as a common noun read other than
// by its Hangul reading (星 별, not 易 역), which 々 makes plural (pipeline::RespellIterationMarks).
bool KanjiNoun(const void* context, std::string_view line, size_t at, size_t end) {
    const auto& d = *static_cast<const RespellLookup*>(context);
    const std::string_view kanji = line.substr(at, end - at);
    const auto reading = dict::Written(text::DecodeRune(kanji).cp);
    std::string sound;
    if (reading) text::AppendRune(sound, *reading);
    for (const dict::Candidate& c : Find(d, kanji).list) {
        if (c.source != kanji || c.codes == nullptr || c.codes->part.Tag() != 'N' || c.codes->part.ClassDigit() != '1' ||
            c.codes->part.Sub() != '1' || c.target.empty() || c.target == sound) {
            continue;
        }
        return true;
    }
    return false;
}

// Whether text ends with a verb (pipeline::RunLookup::verb, the rebuke of SpokenForms).
bool EndsInVerb(const void* context, std::string_view text) {
    const auto& d = *static_cast<const RespellLookup*>(context);
    return d.line != nullptr && pipeline::internal::EndsInDictionaryVerb(text, *d.line, d.snap);
}

// The start of the word other than an ending or a particle that holds the っ at line[at] and goes on
// past it, as written or with the っ dropped (あったりまえ as あたりまえ): the earliest that starts up
// to kRunLookBackRunes Japanese runes before it, npos for none (pipeline::CollapseTsuRuns).
size_t StressedWordStart(const void* context, std::string_view line, size_t at) {
    const auto& d = *static_cast<const RespellLookup*>(context);
    const size_t past = at + text::DecodeRune(line.substr(at)).size;
    size_t found = std::string::npos;
    size_t start = at;
    for (int back = 0; back < kRunLookBackRunes && start > 0; back++) {
        const text::Rune last = pipeline::internal::LastRune(line.substr(0, start));
        if (!pipeline::internal::IsJapaneseRune(last.cp)) break;
        start -= last.size;
        bool holds = false;
        for (const dict::Candidate& c : Find(d, line.substr(start)).list) holds = holds || (!Grammar(c) && start + c.source.size() > past);
        std::string dropped(line.substr(start, at - start));
        dropped += line.substr(past, kLookAheadBytes);
        for (const dict::Candidate& c : Find(d, dropped).list) holds = holds || (!Grammar(c) && start + c.source.size() > at);
        if (holds) found = start;
    }
    return found;
}

auto FallbackOf(const dict::Snapshot& snap) {
    const auto fallback = snap.Fallback();
    return std::pair{fallback, fallback ? NewGenerated(*fallback) : nullptr};
}

// The buffers of one translation, reused from chunk to chunk.
struct Scratch {
    tokenizer::Tokens tokens;
    word::Arena arena;
    std::vector<output::ResolvedItem> items;
    std::vector<output::TokenText> texts;
    std::vector<tokenizer::ControlMarker> markers;
    output::Composer composer;
    word::SessionSenses* sessionSenses = nullptr;
    // The decision of the speech-level stage for the line being translated when the stage writes it
    // (UseDecision), and the levels of its quotes.
    const reg::RegDecision* decision = nullptr;
    std::vector<word::Settings::LineLevel> quoteLevels;
};

word::Settings::LineLevel LineLevelOf(reg::Level level) {
    using L = word::Settings::LineLevel;
    switch (level) {
        case reg::Level::none: return L::none;
        case reg::Level::haera: return L::plain;
        case reg::Level::hae: return L::casual;
        case reg::Level::haeyo: return L::polite;
        case reg::Level::hapsyo: return L::formal;
        default: return L::other;
    }
}

// Sets the decision of the stage for lines [first, last] after the context: that of the line, for a run of
// lines the one they all share, none where the stage does not write them.
void UseDecision(Scratch& s, const reg::SourceDecisions* d, std::size_t first, std::size_t last) {
    s.decision = nullptr;
    s.quoteLevels.clear();
    if (d == nullptr || last >= d->lines.size() || last >= d->dialogue.size()) return;
    const reg::RegDecision& one = d->lines[first];
    for (std::size_t k = first; k <= last; k++) {
        const reg::RegDecision& x = d->lines[k];
        if (!d->dialogue[k] || x.narration || x.level != one.level || x.authoritative != one.authoritative ||
            x.prose != one.prose || (first != last && !x.quotes.empty())) {
            return;
        }
    }
    s.decision = &one;
    for (const reg::RegDecision::Quote& q : one.quotes) s.quoteLevels.push_back(LineLevelOf(q.level));
}

// The pieces of the output as the tokenizer reads them (the span kinds are the same numbers).
std::vector<tokenizer::Piece> TokenizerPieces(const output::Pieces& pieces) {
    std::vector<tokenizer::Piece> out;
    out.reserve(pieces.size());
    for (const output::Piece& p : pieces) out.push_back({p.text, static_cast<int>(p.span)});
    return out;
}

// A word that ends where the source had a space between two copies (tokenizer::CopySpace) keeps
// that space before the next word, also where the last copy ends its token before a word after a
// source space (えっ えっ それは 어 어 그것은). A word that spells two or more such copies and reads
// as one copy's Korean or its sound said as often (毎日毎日 매일매일, プリプリプリ 프리프리프리) is
// written with a space between the copies (매일 매일); not where that Korean is no reading of the copy
// (これこれ 이러이러).
void MarkCopySpaces(const dict::Snapshot& snap, const dict::LookupContext& lookup, const tokenizer::Tokens& tokens,
                    std::vector<output::ResolvedItem>& items) {
    if (tokens.copySpaces.empty()) return;
    for (std::size_t k = 0; k < items.size(); k++) {
        output::ResolvedItem& it = items[k];
        if (it.tokenIndex < 0 || static_cast<std::size_t>(it.tokenIndex) >= tokens.size()) continue;
        const auto t = static_cast<std::size_t>(it.tokenIndex);
        const std::size_t unit = tokens.CopyUnitAt(t, static_cast<std::size_t>(it.offset));
        std::string& ko = it.entry.text;
        if (unit > 0 && it.length > static_cast<int>(unit) && it.length % unit == 0 && it.surface.size() == static_cast<std::size_t>(it.length)) {
            const std::size_t copies = static_cast<std::size_t>(it.length) / unit;
            bool apart = true;
            for (std::size_t c = 1; c < copies && apart; c++) {
                apart = tokens.CopySpaceAt(t, static_cast<std::size_t>(it.offset) + c * unit) &&
                        it.surface.compare(c * unit, unit, it.surface, 0, unit) == 0;
            }
            const std::u32string rs = pipeline::internal::DecodeText(ko);
            const std::size_t part = rs.size() / copies;
            apart = apart && part > 0 && rs.size() % copies == 0 &&
                    std::all_of(rs.begin(), rs.end(), [](char32_t r) { return r >= text::kBase && r <= text::kLast; });
            for (std::size_t c = 1; c < copies && apart; c++) apart = rs.compare(c * part, part, rs, 0, part) == 0;
            if (apart) {
                const std::string copy = it.surface.substr(0, unit);
                const std::string half = pipeline::internal::EncodeText(rs.substr(0, part));
                // A katakana run written as heard (Nm: プリ 프리), the copy as heard (コッ 콧) or a reading
                // of it (毎日 매일).
                bool reads = it.entry.gattr.find("Nm") != std::string::npos || pipeline::internal::ReadKana(copy) == half;
                for (const dict::Candidate& c : snap.GetWordInfo(copy, dict::LookupMode::General, lookup).list) {
                    reads = reads || (c.source == copy && c.target == half);
                }
                if (reads) {
                    ko = half;
                    for (std::size_t c = 1; c < copies; c++) ko += " " + half;
                }
            }
        }
        if (k + 1 == items.size()) continue;
        const output::ResolvedItem& next = items[k + 1];
        const auto end = static_cast<std::size_t>(it.offset + it.length);
        if (next.tokenIndex == it.tokenIndex && static_cast<std::size_t>(next.offset) == end) {
            if (tokens.CopySpaceAt(t, end)) it.spaceAfter = true;
        } else if (next.tokenIndex == it.tokenIndex + 1 && next.offset == 0 && tokens[t + 1].afterSpace &&
                   end == tokens.Raw(t).size()) {
            // The last copy of a run ends the token.
            for (auto c = tokens.FirstCopySpace(t, 0); c != tokens.copySpaces.end() && c->tokenIndex == it.tokenIndex; ++c) {
                it.spaceAfter = it.spaceAfter || c->at + c->unit == end;
            }
        }
    }
}

// Each Japanese token of the sentence first..last that goes on after a space parting two words
// (Token::afterSpace) joined back to the Japanese token before it, so that the compose stage
// writes them as the one token they were typed as: its text, items and markers move into that
// token, it leaves texts, and the indices after it and last count one less. texts is BuildTexts
// of the sentence. Returns whether a token was joined, after which texts no longer lines up with
// tokens.
bool JoinAfterSpace(const tokenizer::Tokens& tokens, int first, int& last, std::vector<output::TokenText>& texts,
                    std::vector<output::ResolvedItem>& items, std::vector<tokenizer::ControlMarker>& markers) {
    const int n = static_cast<int>(std::min(texts.size(), tokens.size()));
    // The first and last place of the items of each token in items: a token joins only when its
    // items come after those of the token before it.
    std::vector<int> from(static_cast<std::size_t>(n), -1), to(static_cast<std::size_t>(n), -1);
    for (int k = 0; k < static_cast<int>(items.size()); k++) {
        const int t = items[static_cast<std::size_t>(k)].tokenIndex;
        if (t < 0 || t >= n) continue;
        if (from[static_cast<std::size_t>(t)] < 0) from[static_cast<std::size_t>(t)] = k;
        to[static_cast<std::size_t>(t)] = k;
    }
    std::vector<int> index(static_cast<std::size_t>(n));
    std::vector<int> shift(static_cast<std::size_t>(n), 0);
    std::vector<output::TokenText> joined;
    int into = -1;     // the token before, in joined
    int intoLast = -1; // the last place of the items joined into it
    for (int j = 0; j < n; j++) {
        output::TokenText& t = texts[static_cast<std::size_t>(j)];
        const int head = from[static_cast<std::size_t>(j)];
        if (j > first && j <= last && tokens[static_cast<std::size_t>(j)].afterSpace && into >= 0 &&
            t.type == tokenizer::kTypeJapanese && joined[static_cast<std::size_t>(into)].type == tokenizer::kTypeJapanese &&
            (head < 0 || head > intoLast)) {
            const int tail = to[static_cast<std::size_t>(j - 1)];
            if (tail >= 0) {
                output::ResolvedItem& previous = items[static_cast<std::size_t>(tail)];
                if (previous.entry.code.starts_with("32") ||
                    (previous.detailClass == output::DetailClass::None &&
                     dict::InClass(HDTRANS_CLASS("word.pause-filler"), tokens.Raw(static_cast<std::size_t>(j - 1))))) {
                    previous.spaceAfter = true;
                }
            }
            intoLast = std::max(intoLast, to[static_cast<std::size_t>(j)]);
            index[static_cast<std::size_t>(j)] = into;
            shift[static_cast<std::size_t>(j)] = static_cast<int>(joined[static_cast<std::size_t>(into)].text.size());
            joined[static_cast<std::size_t>(into)].text += t.text;
            continue;
        }
        into = static_cast<int>(joined.size());
        intoLast = to[static_cast<std::size_t>(j)];
        index[static_cast<std::size_t>(j)] = into;
        joined.push_back(std::move(t));
    }
    if (static_cast<int>(joined.size()) == n) {
        for (int j = 0; j < n; j++) texts[static_cast<std::size_t>(j)] = std::move(joined[static_cast<std::size_t>(j)]);
        return false;
    }
    texts = std::move(joined);
    for (output::ResolvedItem& it : items) {
        if (it.tokenIndex < 0 || it.tokenIndex >= n) continue;
        it.offset += shift[static_cast<std::size_t>(it.tokenIndex)];
        it.tokenIndex = index[static_cast<std::size_t>(it.tokenIndex)];
    }
    for (tokenizer::ControlMarker& m : markers) {
        if (m.tokenIndex < 0 || m.tokenIndex >= n) continue;
        m.textLength += shift[static_cast<std::size_t>(m.tokenIndex)];
        m.tokenIndex = index[static_cast<std::size_t>(m.tokenIndex)];
    }
    last = index[static_cast<std::size_t>(last)];
    return true;
}

// The token texts the compose stage reads for the sentence first..last. Tokens after the
// sentence are as scanned; the tokens of the sentence are as the token loop left them, and
// a wide space that the analysis reads as a space becomes one.
void BuildTexts(const tokenizer::Tokens& tokens, const word::Analysis& analysis, int first, int last,
                std::vector<output::TokenText>& texts) {
    if (texts.size() != tokens.size()) {
        texts.assign(tokens.size(), {});
        for (std::size_t j = 0; j < tokens.size(); j++) {
            texts[j].type = tokens[j].type;
            texts[j].span = tokens[j].span;
            texts[j].text = std::string(tokens.Raw(j));
            if (tokens[j].span != 0) texts[j].spanText = std::string(tokens.Raw(j));
        }
    }
    for (int j = first; j <= last; j++) {
        const auto v = tokens.View(static_cast<std::size_t>(j), static_cast<std::size_t>(last));
        texts[static_cast<std::size_t>(j)].type = v.type;
        texts[static_cast<std::size_t>(j)].text = std::string(v.text);
        if (v.text == "!" || v.text == "?") {
            // A run of marks keeps every mark (！！ !!, ！？ !?); the analysis read its first.
            if (std::string run = tokenizer::MarkRun(tokens.Raw(static_cast<std::size_t>(j))); run.size() > 1) {
                texts[static_cast<std::size_t>(j)].text = std::move(run);
            }
        }
        if (analysis.spaced[static_cast<std::size_t>(j)]) {
            texts[static_cast<std::size_t>(j)].type = tokenizer::kTypeWideSpace;
            texts[static_cast<std::size_t>(j)].text = " ";
        }
    }
    if (static_cast<std::size_t>(last) + 1 < tokens.size()) {
        const std::size_t next = static_cast<std::size_t>(last) + 1;
        const auto v = tokens.View(next, static_cast<std::size_t>(last));
        texts[next].type = v.type;
        texts[next].text = std::string(v.text);
    }
}

output::Settings ComposeSettings(bool text) {
    output::Settings s;
    s.cleanup = text;
    s.text = text;
    return s;
}

// The lines a quote may run on without a closing bracket before it is taken as unclosed.
constexpr int kCarryLines = 5;

// Whether a line ends every quote (a blank line).
bool Blank(std::string_view line) { return word::TrimRunes(line, " 　\r\t").empty(); }

// The quotes of a line that starts after carry: the carry it starts in (a blank line, a
// line that opens its own quote at its start and a quote that ran for kCarryLines end the
// carried one), the carry it leaves and whether it closes the quote it starts in.
struct LineQuotes {
    QuoteCarry start;
    QuoteCarry end;
    bool closes = false;
};

LineQuotes QuotesOf(std::string_view line, QuoteCarry carry) {
    const std::string_view head = word::TrimRunes(line, " 　");
    if (Blank(line) || head.starts_with("「") || head.starts_with("『") || head.starts_with("｢") ||
        carry.lines >= kCarryLines) {
        carry = {};
    }
    LineQuotes q{carry, {}, false};
    int depth = carry.depth;
    for (std::size_t at = 0; at < line.size();) {
        const text::Rune r = text::DecodeRune(line.substr(at));
        at += r.size == 0 ? 1 : r.size;
        if (r.cp == U'「' || r.cp == U'『' || r.cp == U'｢') {
            depth++;
        } else if ((r.cp == U'」' || r.cp == U'』' || r.cp == U'｣') && depth > 0 && --depth == 0) {
            q.closes = q.closes || carry.depth > 0;
        }
    }
    q.end = {depth, depth > 0 ? (carry.depth > 0 && !q.closes ? carry.lines + 1 : 1) : 0};
    return q;
}

// For each line, whether the quote it starts in (from carry and the lines before it) is
// closed by it or a later line of lines; a quote that none of them closes is taken as
// unclosed and leaves the lines it reaches as they are.
std::vector<bool> ClosedCarries(std::span<const std::string> lines, QuoteCarry carry) {
    std::vector<LineQuotes> q(lines.size());
    for (std::size_t i = 0; i < lines.size(); i++) {
        q[i] = QuotesOf(lines[i], carry);
        carry = q[i].end;
    }
    std::vector<bool> closed(lines.size(), false);
    bool later = false;  // the quote the line after i starts in is closed
    for (std::size_t i = lines.size(); i-- > 0;) {
        closed[i] = q[i].start.depth > 0 && (q[i].closes || (q[i].end.depth > 0 && later));
        later = q[i].start.depth > 0 && (q[i].closes || later);
    }
    return closed;
}

// The speech of a line for the compose stage: whether its plain sentence ends become 해체,
// whether its questions take the forms of speech, its quotes at the start of the next chunk
// and the styles of its quotes.
struct LineSpeech {
    bool casual = false;
    bool narrated = false;
    pipeline::Spoken spoken = pipeline::Spoken::No;
    bool markedOnly = false;
    bool asked = false;
    bool poiTic = false;
    output::QuoteState quotes;
    std::vector<std::uint8_t> styles;
};

word::Settings AnalysisSettings(bool text, bool verse, const TranslationOptions& options,
                                word::SessionSenses* sessionSenses) {
    word::Settings s = options.reference ? word::Settings::Reference() : word::Settings{};
    if (!options.reference) s.improvements = options.wordImprovements;
    s.text = text;
    s.terminal = text;
    s.verse = verse;
    if (!options.reference) {
        s.scorers = options.scorers;
        s.shadowScorers = options.shadowScorers;
        s.selectShadow = options.selectShadow;
        s.selectRules = options.selectRules;
        s.misreadModel = options.misreadModel;
    }
    s.sessionSenses = text ? sessionSenses : nullptr;
    s.documentPacks = text ? options.documentPacks : nullptr;
    s.ruleTally = options.ruleTally;
    if (options.filters != nullptr) s.filterMarks = options.filters->Marks();
    s.filterWritten = options.filterWritten;
    return s;
}

// Adds a "&#CS" control marker to tokens for each byte offset of breaks in the text they were
// scanned from: inside the token that holds the offset, else after the last word before it, else
// before the first word after it.
void AddBreakMarkers(tokenizer::Tokens& tokens, std::span<const std::size_t> breaks, std::span<const char> inWord) {
    const auto word = [&](std::size_t t) {
        const int type = tokens.TypeAfter(t);
        return type != tokenizer::kTypeEnd && type != tokenizer::kTypeMarkup && type != tokenizer::kTypeAsciiSpace &&
               type != tokenizer::kTypeWideSpace && !tokens.Merged(t).empty();
    };
    for (std::size_t b = 0; b < breaks.size(); b++) {
        const std::size_t at = breaks[b];
        int index = -1, length = 0;
        for (std::size_t t = 0; t < tokens.size() && index < 0; t++) {
            const tokenizer::Token& token = tokens[t];
            if (word(t) && token.sourceBegin < at && at < token.sourceEnd) {
                // Counted in characters, as the token's text folds some (１ as 1) and may drop the
                // spaces between Japanese words: the spaces count when the token kept them.
                std::size_t all = 0, head = 0, headSpaces = 0;
                for (std::size_t c = 0; c < tokens.chars.size() && c < tokens.offsets.size(); c++) {
                    if (tokens.offsets[c] < token.sourceBegin || tokens.offsets[c] >= token.sourceEnd) continue;
                    all++;
                    if (tokens.offsets[c] >= at) continue;
                    head++;
                    if (tokens.chars[c] == U' ') headSpaces++;
                }
                const std::string_view merged = tokens.Merged(t);
                std::size_t runes = all == text::RuneCount(merged) ? head : head - headSpaces;
                std::size_t bytes = 0;
                for (; runes > 0 && bytes < merged.size(); runes--) bytes += std::max<std::size_t>(1, text::DecodeRune(merged.substr(bytes)).size);
                index = static_cast<int>(t);
                length = static_cast<int>(std::min(bytes, merged.size()));
            }
        }
        for (std::size_t t = tokens.size(); t-- > 0 && index < 0;) {
            if (word(t) && tokens[t].sourceEnd <= at) {
                index = static_cast<int>(t);
                length = static_cast<int>(tokens.Merged(t).size());
            }
        }
        for (std::size_t t = 0; t < tokens.size() && index < 0; t++) {
            if (word(t)) index = static_cast<int>(t);
        }
        if (index >= 0) tokens.markers.push_back({index, length, 1, b < inWord.size() && inWord[b] != 0});
    }
    std::stable_sort(tokens.markers.begin(), tokens.markers.end(),
                     [](const tokenizer::ControlMarker& a, const tokenizer::ControlMarker& b) {
                         return a.tokenIndex < b.tokenIndex;
                     });
}

// The span piece of break k of joined lines.
std::string_view BreakSpan(std::span<const char> cutBreaks, std::size_t k) {
    return k < cutBreaks.size() && cutBreaks[k] != 0 ? kCutBreakSpan : kBreakSpan;
}

// marked with a kBreakSpan piece where each line of lines (the marked texts of joined lines through
// the stages one by one) ends: after the lines up to it when they start marked, else before the
// lines after it when they end it. None when a break falls in neither way or inside a span.
// cutBreaks says which breaks fall inside a word (kCutBreakSpan).
std::optional<output::Pieces> BreakAt(const output::Pieces& marked, std::span<const std::string> lines,
                                      std::span<const char> cutBreaks) {
    const std::string whole = output::String(marked);
    std::vector<std::size_t> breaks;
    for (std::size_t k = 0; k + 1 < lines.size(); k++) {
        std::string before, after;
        for (std::size_t i = 0; i <= k; i++) before += lines[i];
        for (std::size_t i = k + 1; i < lines.size(); i++) after += lines[i];
        std::size_t at = std::string::npos;
        if (whole.starts_with(before)) {
            at = before.size();
        } else if (whole.ends_with(after)) {
            at = whole.size() - after.size();
        }
        if (at == std::string::npos || (!breaks.empty() && at <= breaks.back())) return std::nullopt;
        breaks.push_back(at);
    }
    output::Pieces out;
    std::size_t pos = 0, next = 0;
    for (const output::Piece& p : marked) {
        std::size_t from = 0;
        for (; next < breaks.size() && breaks[next] <= pos + p.text.size(); next++) {
            const std::size_t cut = breaks[next] - pos;
            if (p.span != output::SpanText && cut > 0 && cut < p.text.size()) return std::nullopt;
            if (cut > from) out.push_back({p.text.substr(from, cut - from), p.span});
            out.push_back({std::string(BreakSpan(cutBreaks, next)), output::SpanOpen});
            from = cut;
        }
        if (from < p.text.size() || (from == 0 && p.text.empty())) out.push_back({p.text.substr(from), p.span});
        pos += p.text.size();
    }
    return out;
}

// Translates one chunk: its tokens, the word analysis, then per sentence the resolve stage
// and the compose stage. The sentences are appended to out with the chunk index.
// speech is read from the start of the chunk.
void TranslateChunk(const dict::Snapshot& snap, const dict::LookupContext& lookup, const output::Pieces& chunk,
                    int index, bool text, bool verse, std::string_view before, const LineSpeech& speech, Scratch& s,
                    Generated& generated, const StageObserver& observer, const TranslationOptions& options,
                    std::vector<output::Sentence>& out) {
    s.texts.clear();
    // Joined wrapped lines: the break spans become break markers at their places in the tokens.
    std::vector<std::size_t> breaks;
    std::vector<char> inWord;
    output::Pieces unbroken;
    if (options.joinedLines) {
        std::size_t at = 0;
        for (const output::Piece& p : chunk) {
            if (p.span == output::SpanOpen && (p.text == kBreakSpan || p.text == kCutBreakSpan)) {
                breaks.push_back(at);
                inWord.push_back(p.text == kCutBreakSpan);
                continue;
            }
            if (p.span == output::SpanText) {
                output::Add(unbroken, p.text);
            } else {
                unbroken.push_back(p);
            }
            at += p.text.size();
        }
    }
    const std::vector<tokenizer::Piece> pieces = TokenizerPieces(options.joinedLines ? unbroken : chunk);
    // A word of the dictionary that starts in the text before a space and goes on past it.
    const tokenizer::SpaceJoins joins = [&](std::string_view head, std::string_view after, tokenizer::SpaceWord word) {
        using tokenizer::SpaceWord;
        if (word == SpaceWord::Ending) return dict::InClass(HDTRANS_CLASS("particle.after-space"), after);
        if (word == SpaceWord::Filler) {
            if (dict::InClass(HDTRANS_CLASS("word.pause-filler"), head)) return true;
            const dict::Candidates found = snap.GetWordInfo(head, dict::LookupMode::General, lookup);
            return std::any_of(found.list.begin(), found.list.end(), [&](const dict::Candidate& c) {
                return c.source == head && c.codes != nullptr && c.codes->part.ClassDigit() == '3' &&
                       c.codes->part.Sub() == '2';
            });
        }
        const std::size_t letterEnd = after.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        const std::size_t letters = word == SpaceWord::Any ? std::min(letterEnd, after.size()) : 0;
        for (std::size_t start = 0; start < head.size();) {
            const std::string_view left = head.substr(start);
            if (word == SpaceWord::Binding && text::RuneCount(left) < 2 &&
                !(start == 0 && text::IsKanji(text::DecodeRune(left).cp))) {
                break;
            }
            if (word == SpaceWord::Phrase && (start > 0 || text::RuneCount(left) < 2)) break;
            // The candidates point into the query, so it lives as long as they do.
            const std::string query = std::string(left) + std::string(after);
            if (!snap.KeyPast(query, left.size(), lookup)) {
                start += std::max<std::size_t>(1, text::DecodeRune(left).size);
                continue;
            }
            const dict::Candidates words = snap.GetWordInfo(query, dict::LookupMode::General, lookup);
            for (const dict::Candidate& c : words.list) {
                if (c.source.size() <= left.size()) continue;
                if (c.source.size() - left.size() < letters) continue;
                // A name: a person or a work, or a row of the name layer other than a place.
                const bool place = c.codes != nullptr && c.codes->sem.HasPrefix("QCT");
                const bool name = (c.codes != nullptr && (c.codes->sem.HasPrefix("QPN") || c.codes->sem.HasPrefix("QWK"))) ||
                                  (c.user != nullptr && c.user->nameLayer && !place);
                bool bound = false;
                if (word == SpaceWord::Binding && ClassOf(c) == '4' && c.codes->gram.HasCode("Pj") && c.codes->gram.HasCode("Tu") &&
                    c.codes->gram.HasCode("Ma")) {
                    const std::string_view rest = std::string_view(query).substr(c.source.size());
                    for (int branch = 0; branch < word::kConjugationBranches; branch++) {
                        const std::string_view form = word::Conjugation(c.codes->part.VerbKind(), branch);
                        if (!form.empty() && rest.starts_with(form)) bound = true;
                    }
                    if (bound) {
                        const dict::Candidates right = snap.GetWordInfo(after, dict::LookupMode::General, lookup);
                        for (const dict::Candidate& r : right.list) {
                            if (r.source.size() > c.source.size() - left.size() && r.codes != nullptr &&
                                std::string_view("0134789").find(ClassOf(r)) != std::string_view::npos) {
                                bound = false;
                            }
                        }
                    }
                }
                const bool phrase = word == SpaceWord::Phrase && text::RuneCount(c.source.substr(left.size())) >= 2;
                // Spelled out, the word ends before no hiragana but a particle or a copula: it does not
                // take the stem of a word written after the space (通 知 表が; not 先 行くね, 先行 + くね).
                bool spelled = false;
                if (word == SpaceWord::Spelled) {
                    const std::string_view rest = std::string_view(query).substr(c.source.size());
                    const char32_t next = rest.empty() ? 0 : text::DecodeRune(rest).cp;
                    spelled = !(next >= U'ぁ' && next <= U'ゖ') || std::u32string_view(U"がをはもでとへのにやかだじ").find(next) != std::u32string_view::npos;
                }
                const bool fits = word == SpaceWord::Any || spelled || (word == SpaceWord::Binding && (name || bound)) || phrase;
                if (fits) return true;
            }
            start += std::max<std::size_t>(1, text::DecodeRune(left).size);
        }
        return false;
    };
    tokenizer::TokenizeSpans(pieces, text, s.tokens, text && !options.reference ? &joins : nullptr);
    if (!breaks.empty()) {
        AddBreakMarkers(s.tokens, breaks, inWord);
    }
    if (observer.tokens) observer.tokens(observer.context, index, s.tokens);

    word::Analysis analysis;
    word::Settings settings = AnalysisSettings(text, verse, options, s.sessionSenses);
    settings.before = before;
    settings.quoteDepth = speech.quotes.depth;
    settings.quoteCount = speech.quotes.count;
    settings.quoteStyles = speech.styles;
    if (s.decision != nullptr) {
        settings.lineLevel = LineLevelOf(s.decision->level);
        settings.authoritative = s.decision->authoritative;
        settings.prose = s.decision->prose;
        settings.quoteLevels = s.quoteLevels;
    }
    settings.poiTic = speech.poiTic;
    settings.narrated = speech.narrated;
    word::Analyze(s.tokens, snap, lookup, settings, s.arena, analysis);
    if (observer.analysis) observer.analysis(observer.context, index, s.tokens, analysis);

    output::Settings composeSettings = ComposeSettings(text);
    composeSettings.casual = text && speech.casual;
    composeSettings.casualMarkedOnly = speech.markedOnly;
    composeSettings.asked = text && speech.asked;
    composeSettings.narrated = speech.narrated;
    composeSettings.spoken = speech.spoken != pipeline::Spoken::No;
    composeSettings.spokenByEnd = speech.spoken == pipeline::Spoken::End;
    composeSettings.literal = text && !options.reference && options.wordImprovements.literalCleanup;
    composeSettings.quotes = speech.quotes;
    composeSettings.quoteStyles = &speech.styles;
    composeSettings.markerBeforeWord = options.joinedLines;
    std::size_t counted = 0;  // tokens read into composeSettings.quotes
    for (std::size_t k = 0; k < analysis.sentences.size(); k++) {
        const word::Sentence& sentence = analysis.sentences[k];
        const std::size_t start = std::min(static_cast<std::size_t>(sentence.first), s.tokens.size());
        for (; (composeSettings.casual || composeSettings.asked) && counted < start; counted++) {
            composeSettings.quotes = output::QuotesAfter(composeSettings.quotes, s.tokens.Raw(counted));
        }
        output::Sentence done;
        done.chunk = index;
        {
            std::unique_lock<std::mutex> lock(generated.mu, std::defer_lock);
            if (options.ruleTally != nullptr) lock.lock();
            const rules::Program& program = options.rules ? *options.rules
                                             : generated.hasRules ? generated.rules : RulesProgram();
            ResolveSentence(program, generated.families, analysis, k);
            if (observer.resolved) observer.resolved(observer.context, index, static_cast<int>(k), sentence.chain);
            ResolvedItems(sentence.chain, s.items);
            MarkCopySpaces(snap, lookup, s.tokens, s.items);
            BuildTexts(s.tokens, analysis, sentence.first, sentence.last, s.texts);
            s.markers.assign(s.tokens.markers.begin() + sentence.markerBegin,
                             s.tokens.markers.begin() + sentence.markerEnd);
            int last = sentence.last;
            const bool joined = JoinAfterSpace(s.tokens, sentence.first, last, s.texts, s.items, s.markers);
            s.composer.Compose(s.items, s.texts, sentence.first, last, s.markers, composeSettings,
                               generated.families, done.pieces);
            if (joined) s.texts.clear();
        }
        if (observer.composed) observer.composed(observer.context, index, static_cast<int>(k), done.pieces);
        out.push_back(std::move(done));
    }
}

// Whether observer takes any stage of a line.
bool Observes(const StageObserver& o) {
    return !o.levelsOnly && (o.text || o.pieces || o.tokens || o.analysis || o.resolved || o.composed || o.cleaned || o.restored ||
                             o.filterLog);
}

// One line through every stage of the text path; the line's trailing "\r" is written
// back by the caller.
// carry is the quote an earlier line left open for this one.
// narrated, when set, replaces the document's narration (wrapped lines read it from their own text).
// parts, when given, are the lines that body joins: each is normalized, sounded and protected on its
// own, with a kBreakSpan between them that the chunks turn into break markers.
std::string TranslateBody(const dict::Snapshot& snap, pipeline::DocumentContext& doc, std::string_view body,
                          QuoteCarry carry, Scratch& s, Generated& generated, const StageObserver& observer,
                          const TranslationOptions& options, std::optional<bool> narrated,
                          std::span<const std::string> parts) {
    const pipeline::LineFacts facts = pipeline::ReadLine(doc, body);
    if (facts.wholeLine && parts.empty()) return *facts.wholeLine;
    const pipeline::LineLookup lookup = pipeline::LookupOf(doc, facts);
    pipeline::LineContext ctx = pipeline::ContextOf(doc, lookup);
    if (narrated) ctx.narrated = *narrated;
    if (options.filters) ctx.lookup.user = options.filters->User();
    ctx.exclamationNa = !options.reference && options.wordImprovements.exclamationNa;
    ctx.dialectForms = !options.reference && options.wordImprovements.dialectForms;

    const RespellLookup respell{snap, ctx.lookup, &ctx};
    const auto normalize = [&](std::string_view text) {
        std::string normalized = pipeline::JoinBraceRuby(
            pipeline::Normalize(text, ctx,
                                {WordHoldsRun, options.reference ? nullptr : StressedWordStart, &respell,
                                 options.reference ? nullptr : EndsInVerb}),
            ReadBase, &snap);
        if (!options.reference) {
            normalized = pipeline::RespellVariants(normalized, WordKeepsVariant, &respell);
            normalized = pipeline::RespellKatakanaEndings(normalized);
            normalized = pipeline::RespellKanjiCries(normalized);
            normalized = pipeline::RespellOldForms(normalized, WordHoldsRun, &respell);
            normalized = pipeline::RespellIterationMarks(normalized, WordHoldsRun, KanjiNoun, &respell);
        }
        return normalized;
    };
    const auto front = [&](std::string_view text, output::Pieces& typedOut, output::Pieces& markedOut,
                           std::string& normalized, std::string& sounded) {
        normalized = normalize(text);
        sounded = pipeline::Sound(normalized, ctx, snap);
        typedOut = pipeline::ProtectTyped(sounded, ctx, snap);
        markedOut = pipeline::MarkCornerBrackets(typedOut);
    };
    output::Pieces typed, marked;
    std::string normalized, sounded;
    front(facts.text, typed, marked, normalized, sounded);
    if (!parts.empty()) {
        // The breaks fall where the lines through the stages one by one meet in the joined text;
        // where a stage changed the text across a break, the lines keep their own stages.
        std::vector<std::string> partMarked;
        output::Pieces partsTyped, partsMarked;
        std::string partsNormalized, partsSounded;
        for (std::size_t k = 0; k < parts.size(); k++) {
            output::Pieces partTyped, partMarkedPieces;
            std::string partNormalized, partSounded;
            front(pipeline::ReadLine(doc, parts[k]).text, partTyped, partMarkedPieces, partNormalized, partSounded);
            partMarked.push_back(output::String(partMarkedPieces));
            if (k > 0) partsMarked.push_back({std::string(BreakSpan(options.cutBreaks, k - 1)), output::SpanOpen});
            output::Append(partsMarked, std::move(partMarkedPieces));
            output::Append(partsTyped, std::move(partTyped));
            partsNormalized += partNormalized;
            partsSounded += partSounded;
        }
        std::optional<output::Pieces> broken = BreakAt(marked, partMarked, options.cutBreaks);
        if (broken) {
            marked = std::move(*broken);
        } else {
            typed = std::move(partsTyped);
            marked = std::move(partsMarked);
            normalized = std::move(partsNormalized);
            sounded = std::move(partsSounded);
        }
    }
    if (observer.text) observer.text(observer.context, "N", normalized);
    if (observer.text) observer.text(observer.context, "SN", sounded);
    if (observer.pieces) observer.pieces(observer.context, "P", -1, marked);

    std::vector<std::string> gaps;
    const std::vector<pipeline::Chunk> chunks = pipeline::Segment(marked, &gaps);
    LineSpeech speech;
    std::vector<bool> casual;
    if (!options.reference) {
        speech.asked = true;
        speech.poiTic = word::PoiTicLine(facts.text);
        speech.narrated = ctx.narrated;
        speech.quotes = {carry.depth, carry.depth > 0 ? 1 : 0};
        speech.styles = pipeline::QuoteStyles(output::String(marked), carry.depth);
        speech.casual = options.speech == Speech::Soft;
        const std::string collapsed = pipeline::internal::CollapseTsuRuns(facts.text, {WordHoldsRun, nullptr, &respell});
        const FusedLookup fusedLookup{ctx, snap};
        const pipeline::FusedTest fused{HoldsFusedAdjective, &fusedLookup};
        // Soft reads the casual quotes only to tell polite talk in a line with でしょ (below).
        if (!speech.casual || PoliteTalkAsked(facts.text, speech.styles)) casual = pipeline::CasualQuotes(collapsed, carry.depth, fused);
        // Soft: a line of a document without 「」 that reads as speech (spec SPC-18).
        if (speech.casual && !speech.narrated) speech.spoken = pipeline::SpokenLine(collapsed, carry.depth, fused);
        if (!speech.casual) {
            // Strict: the quotes whose Japanese is casual speech, read before normalizing
            // (知らねえ, 言っとく).
            if (casual.size() == speech.styles.size()) {
                for (std::size_t k = 0; k < casual.size(); k++) {
                    if (!casual[k] || speech.styles[k] != 0) continue;
                    speech.styles[k] = output::kCasualSpeech;
                    speech.casual = speech.markedOnly = true;
                }
            }
        }
    }
    const auto compose = [&](LineSpeech pass, const StageObserver& watch, const TranslationOptions& opts) {
        std::vector<output::Sentence> sentences;
        std::string before;
        if (watch.pass) watch.pass(watch.context);
        for (std::size_t i = 0; i < chunks.size(); i++) {
            if (watch.pieces) watch.pieces(watch.context, "CH", static_cast<int>(i), chunks[i]);
            if (!gaps[i].empty() && gaps[i] != output::kPhraseGap) before.clear();  // a column starts as a line does
            TranslateChunk(snap, ctx.lookup, chunks[i], static_cast<int>(i), true, facts.verse, before, pass, s,
                           generated, watch, opts, sentences);
            std::string chunkText = output::String(chunks[i]);
            for (const std::string_view span : {kBreakSpan, kCutBreakSpan}) {
                for (std::size_t at; (at = chunkText.find(span)) != std::string::npos;) chunkText.erase(at, span.size());
            }
            pass.quotes = output::QuotesAfter(pass.quotes, chunkText);
            if (const std::string_view t = word::TrimRunes(chunkText, " 　"); !t.empty()) before = word::LastChar(t);
        }
        const output::Pieces cleaned = output::Cleanup(sentences, gaps);
        if (watch.cleaned) watch.cleaned(watch.context, cleaned);
        const pipeline::RestoreSteps steps = pipeline::RestoreAll(
            cleaned, typed, /*keepBrackets=*/true, !opts.reference && opts.wordImprovements.literalCleanup);
        if (watch.restored) watch.restored(watch.context, steps);
        return steps.line;
    };
    // でしょ in a quote of polite talk seeks agreement politely (知らなかったでしょ 몰랐죠): a quote of
    // polite speech whose Korean ends a sentence politely (그렇습니다, 봐 주세요; not 일어나 of
    // なさい, not a 済ませた or ますます) and whose Japanese has no casual mark (よ, ね).
    if (!options.reference && PoliteTalkAsked(facts.text, speech.styles)) {
        TranslationOptions trial = options;
        trial.ruleTally = nullptr;
        const std::vector<bool> polite =
            output::PoliteQuotes(compose(speech, observer.levelsOnly ? observer : StageObserver{}, trial), carry.depth);
        for (std::size_t k = 0; k < speech.styles.size() && k < polite.size(); k++) {
            const bool casualMark = k < casual.size() && casual[k];
            if ((speech.styles[k] & output::kPoliteStyle) != 0 && polite[k] && !casualMark) {
                speech.styles[k] |= output::kPoliteTalkStyle;
            }
        }
    }
    if (!speech.markedOnly) {
        if (!speech.casual) return output::SpokenWords(compose(speech, observer, options), carry.depth, speech.styles);
        // Soft: every plain quote is casual speech and takes the spoken words (것은 건).
        std::vector<std::uint8_t> spoken = speech.styles;
        for (std::uint8_t& style : spoken) {
            if (style == 0) style = output::kCasualSpeech;
        }
        return output::SpokenWords(compose(speech, observer, options), carry.depth, spoken,
                                   speech.spoken != pipeline::Spoken::No);
    }
    // A casual quote whose Korean keeps a polite end (고마워요, 좋네요) keeps its plain ends:
    // 해체 next to 해요체 reads as two speakers. The line is composed again without it, or
    // for an observer, which the first pass does not report to.
    // The first pass counts no rules either; the line is composed again when they are counted.
    TranslationOptions trial = options;
    trial.ruleTally = nullptr;
    const std::string first = compose(speech, observer.levelsOnly ? observer : StageObserver{}, trial);
    const std::vector<bool> polite = output::PoliteQuotes(first, carry.depth);
    bool again = Observes(observer) || options.ruleTally != nullptr;
    for (std::size_t k = 0; k < speech.styles.size(); k++) {
        if (speech.styles[k] != output::kCasualSpeech) continue;
        if (polite.size() == speech.styles.size() && !polite[k]) continue;
        speech.styles[k] = 0;
        again = true;
    }
    return output::SpokenWords(again ? compose(speech, observer, options) : first, carry.depth, speech.styles);
}

// TranslateBody with the ruby tags <R reading>base</R> read as brace ruby (LAY-4) and written back.
std::string TranslateRubyBody(const dict::Snapshot& snap, pipeline::DocumentContext& doc, std::string_view body,
                              QuoteCarry carry, Scratch& s, Generated& generated, const StageObserver& observer,
                              const TranslationOptions& options, std::optional<bool> narrated = std::nullopt,
                              std::span<const std::string> parts = {}) {
    const std::string braced = pipeline::RTagsAsBraceRuby(body);
    if (braced.empty() || !parts.empty()) {
        return TranslateBody(snap, doc, body, carry, s, generated, observer, options, narrated, parts);
    }
    return pipeline::BraceRubyAsRTags(
        TranslateBody(snap, doc, braced, carry, s, generated, observer, options, narrated, parts));
}

// Watches the resolved chains of a line for its first and last words.
struct EdgeWatch {
    std::vector<output::ResolvedItem> items;
    WrapEdges edges;
    // The speech-level stage, which reads the line's chains from this pass too; nullptr when it does not.
    const StageObserver* levels = nullptr;
};

void WatchEdges(void* context, int chunk, int sentence, const word::Chain& chain) {
    auto* watch = static_cast<EdgeWatch*>(context);
    ResolvedItems(chain, watch->items);
    AddWrapEdges(watch->items, watch->edges);
    if (watch->levels != nullptr && watch->levels->resolved)
        watch->levels->resolved(watch->levels->context, chunk, sentence, chain);
}

void WatchEdgePass(void* context) {
    const StageObserver* levels = static_cast<EdgeWatch*>(context)->levels;
    if (levels != nullptr && levels->pass) levels->pass(levels->context);
}

// Watches two joined lines (MayCutWord) for a word read across their break: a word the dictionary
// or a conjugation read (ReadWord) that starts before the break, holds the break in its stem or
// conjugation, not in the ending after them (思います / よう), covers the unread kana after the break
// and makes a stem with them (CutStem).
struct CutWatch {
    std::string run;  // the unread kana after the break (WrapEdges::unreadStart)
    char32_t after = 0;  // the character after run
    // The break markers of each chunk, with the text of the token each stands in.
    std::vector<std::vector<std::pair<tokenizer::ControlMarker, std::string>>> markers;
    std::vector<output::ResolvedItem> items;
    bool across = false;
};

void WatchCutTokens(void* context, int chunk, const tokenizer::Tokens& tokens) {
    auto* watch = static_cast<CutWatch*>(context);
    if (chunk < 0) return;
    if (watch->markers.size() <= static_cast<std::size_t>(chunk)) watch->markers.resize(static_cast<std::size_t>(chunk) + 1);
    watch->markers[static_cast<std::size_t>(chunk)].clear();
    for (const tokenizer::ControlMarker& m : tokens.markers) {
        if (m.markerKind == 1 && m.tokenIndex >= 0 && static_cast<std::size_t>(m.tokenIndex) < tokens.size()) {
            watch->markers[static_cast<std::size_t>(chunk)].emplace_back(m, std::string(tokens.Merged(static_cast<std::size_t>(m.tokenIndex))));
        }
    }
}

void WatchCutWords(void* context, int chunk, int, const word::Chain& chain) {
    auto* watch = static_cast<CutWatch*>(context);
    if (chunk < 0 || static_cast<std::size_t>(chunk) >= watch->markers.size()) return;
    ResolvedItems(chain, watch->items);
    for (const output::ResolvedItem& it : watch->items) {
        for (const auto& [m, token] : watch->markers[static_cast<std::size_t>(chunk)]) {
            if (it.tokenIndex == m.tokenIndex && it.offset < m.textLength &&
                static_cast<std::size_t>(it.offset + it.length) >= m.textLength + watch->run.size() && ReadWord(it) &&
                // The break falls in the stem or its conjugation, not in the ending after them (思います / よう).
                m.textLength - it.offset <= it.length - static_cast<int>(it.endingSurface.size()) &&
                CutStem(std::string_view(token).substr(std::min<std::size_t>(it.offset, token.size()),
                                                       static_cast<std::size_t>(m.textLength - it.offset)), watch->run, watch->after)) {
                watch->across = true;
            }
        }
    }
}

// The most lines one sentence is joined from.
constexpr std::size_t kMaxJoined = 12;

// The text of a line without its "\r" and the blanks around it, and those blanks.
struct LineParts {
    std::string_view lead, body, trail;
    bool cr = false;
};

LineParts PartsOf(std::string_view line) {
    LineParts p;
    p.cr = line.ends_with('\r');
    if (p.cr) line.remove_suffix(1);
    std::size_t from = 0, to = line.size();
    while (from < to) {
        if (line[from] == ' ' || line[from] == '\t') {
            from++;
        } else if (line.substr(from).starts_with("　")) {
            from += std::string_view("　").size();
        } else {
            break;
        }
    }
    while (to > from) {
        if (line[to - 1] == ' ' || line[to - 1] == '\t') {
            to--;
        } else if (line.substr(from, to - from).ends_with("　")) {
            to -= std::string_view("　").size();
        } else {
            break;
        }
    }
    p.lead = line.substr(0, from);
    p.body = line.substr(from, to - from);
    p.trail = line.substr(to);
    return p;
}

// The text path over wrapped lines (TranslationOptions::wrapped). Each line is translated on its
// own first, its narration read from its own text; a run of lines that go on in the next
// (GoesOn) is then translated as one line, its breaks carried to the translation as kBreakMark,
// and split back there. When the marks do not come back one for each break, with text between
// each two, the lines keep their own translations. The lines before context are read only.
std::vector<std::string> TranslateWrapped(const dict::Snapshot& snap, pipeline::DocumentContext& doc,
                                          std::span<const std::string> lines, const std::vector<bool>& closed,
                                          Scratch& s, Generated& generated, const StageObserver& observer,
                                          const TranslationOptions& options, std::size_t context) {
    const auto narratedOf = [](std::string_view text) { return text.find("「") != std::string_view::npos; };
    // The decision of the speech-level stage for lines [first, last] of the call.
    const auto use = [&](std::size_t first, std::size_t last) {
        UseDecision(s, first >= context ? options.sourceDecisions : nullptr, first - context, last - context);
    };
    // The lines are translated as AloneText; their translations keep the arrows they end with.
    std::vector<std::string> bare(lines.size()), alone(lines.size());
    std::vector<std::string_view> arrows(lines.size());
    std::vector<WrapEdges> edges(lines.size());
    std::vector<QuoteCarry> used(lines.size());
    QuoteCarry carry;
    for (std::size_t i = 0; i < lines.size(); i++) {
        arrows[i] = ArrowOf(lines[i]);
        bare[i] = WithoutArrow(lines[i]);
        const LineQuotes quotes = QuotesOf(lines[i], carry);
        used[i] = closed[i] ? quotes.start : QuoteCarry{};
        carry = quotes.end;
        EdgeWatch watch;
        StageObserver edgeObserver;
        edgeObserver.context = &watch;
        edgeObserver.resolved = WatchEdges;
        use(i, i);
        // a line translated on its own keeps this translation: the speech-level stage reads it here
        if (observer.levelsOnly && i >= context) {
            watch.levels = &observer;
            edgeObserver.pass = WatchEdgePass;
            if (observer.line) observer.line(observer.context, static_cast<int>(i - context));
        }
        alone[i] = TranslateRubyBody(snap, doc, AloneText(lines[i]), used[i], s, generated, edgeObserver, options,
                                 narratedOf(lines[i]));
        if (s.sessionSenses != nullptr) s.sessionSenses->FinishLine();
        edges[i] = std::move(watch.edges);
    }
    // Whether line k and the next one are read with a word across their break (始め / るとか), read as the
    // joined lines are: with the white space before line k when it starts them.
    const auto readAcross = [&](std::size_t k, bool starts) {
        if (!MayCutWord(lines[k], lines[k + 1], edges[k + 1])) return false;
        const LineParts head = PartsOf(bare[k]);
        const std::vector<std::string> texts{std::string(starts ? head.lead : std::string_view()) + std::string(head.body),
                                             std::string(PartsOf(bare[k + 1]).body)};
        CutWatch watch;
        watch.run = edges[k + 1].unreadStart;
        const std::string_view rest = std::string_view(texts[1]).substr(std::min(watch.run.size(), texts[1].size()));
        watch.after = rest.empty() ? 0 : text::DecodeRune(rest).cp;
        StageObserver cutObserver;
        cutObserver.context = &watch;
        cutObserver.tokens = WatchCutTokens;
        cutObserver.resolved = WatchCutWords;
        static constexpr char kInWord[] = {1};
        TranslationOptions joinedOptions = options;
        joinedOptions.joinedLines = true;
        joinedOptions.cutBreaks = kInWord;
        joinedOptions.ruleTally = nullptr;
        word::SessionSenses* const senses = s.sessionSenses;
        s.sessionSenses = nullptr;
        use(k, k + 1);
        TranslateRubyBody(snap, doc, texts[0] + texts[1], used[k], s, generated, cutObserver, joinedOptions,
                          narratedOf(lines[k]) || narratedOf(lines[k + 1]), texts);
        s.sessionSenses = senses;
        return watch.across;
    };
    std::vector<std::string> out(lines.size());
    for (std::size_t i = 0; i < lines.size();) {
        std::size_t j = i;
        std::vector<char> cuts;
        while (j + 1 < lines.size() && j + 1 - i < kMaxJoined) {
            if (GoesOn(lines[j], edges[j], lines[j + 1], edges[j + 1])) {
                cuts.push_back(0);
            } else if (readAcross(j, j == i)) {
                cuts.push_back(1);
            } else {
                break;
            }
            j++;
        }
        std::vector<std::string> pieces;
        std::vector<LineParts> parts;
        if (j > i && j >= context) {
            std::string joined;
            std::vector<std::string> texts;
            bool narrated = false;
            for (std::size_t k = i; k <= j; k++) {
                parts.push_back(PartsOf(bare[k]));
                texts.emplace_back(k == i ? parts.back().lead : std::string_view());
                texts.back() += parts.back().body;
                joined += texts.back();
                narrated = narrated || narratedOf(lines[k]);
            }
            TranslationOptions joinedOptions = options;
            joinedOptions.joinedLines = true;
            joinedOptions.cutBreaks = cuts;
            if (observer.line) observer.line(observer.context, static_cast<int>(std::max(i, context) - context));
            use(i, j);
            pieces = SplitAtBreaks(
                TranslateRubyBody(snap, doc, joined, used[i], s, generated, observer, joinedOptions, narrated, texts),
                j - i + 1);
            if (s.sessionSenses != nullptr) s.sessionSenses->FinishLine();
        }
        for (std::size_t k = i; k <= j; k++) {
            if (k < context) continue;
            if (!pieces.empty()) {
                // the joined translation is read under its first line: the lines after it have no reading of
                // their own
                if (observer.levelsOnly && k > i && observer.line) observer.line(observer.context, static_cast<int>(k - context));
                const LineParts& p = parts[k - i];
                std::string piece = pieces[k - i];
                if (!arrows[k].empty()) piece = WithArrow(piece, arrows[k]);
                out[k] = std::string(k == i ? std::string_view() : p.lead) + piece + std::string(p.trail);
                if (p.cr) out[k] += '\r';
                continue;
            }
            if (Observes(observer)) {
                // The stages of the line on its own, for the observer; the translation stays the one made
                // before the joined lines (the speech-level stage alone read them then).
                if (observer.line) observer.line(observer.context, static_cast<int>(k - context));
                use(k, k);
                TranslateRubyBody(snap, doc, AloneText(lines[k]), used[k], s, generated, observer, options,
                              narratedOf(lines[k]));
            }
            if (arrows[k].empty()) {
                out[k] = alone[k];
            } else if (ArrowOf(alone[k]) == kGoesOn) {
                out[k] = WithArrow(WithoutArrow(alone[k]), arrows[k]);
            } else if (AloneText(lines[k]) == bare[k]) {
                out[k] = WithArrow(alone[k], arrows[k]);
            } else {
                // The stages did not keep kGoesOn at the end: the line without it.
                use(k, k);
                out[k] = WithArrow(
                    TranslateRubyBody(snap, doc, bare[k], used[k], s, generated, StageObserver{}, options, narratedOf(lines[k])),
                    arrows[k]);
                if (s.sessionSenses != nullptr) s.sessionSenses->FinishLine();
            }
            if (lines[k].ends_with('\r')) out[k] += '\r';
        }
        i = j + 1;
    }
    return out;
}

std::vector<std::string> TranslateDocumentPieces(const dict::Snapshot& snap, Generated& generated,
                                                 std::span<const std::string> lines,
                                                 const StageObserver& observer, const TranslationOptions& options,
                                                 std::span<const std::string> sourceLines, std::size_t context) {
    if (!snap.Healthy()) {
        const auto [fallback, replacement] = FallbackOf(snap);
        if (!fallback) return std::vector<std::string>(lines.begin(), lines.end());
        return TranslateDocumentPieces(*fallback, *replacement, lines, observer, options, sourceLines, context);
    }
    if (std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
            return line.size() > text::kMaxLineBytes;
        })) {
        // A call with a long line is not read as wrapped lines.
        std::vector<std::string> bounded;
        std::vector<std::size_t> ends;
        for (const std::string& line : lines) {
            for (std::string_view piece : text::TranslationPieces(line)) bounded.emplace_back(piece);
            ends.push_back(bounded.size());
        }
        TranslationOptions unwrapped = options;
        unwrapped.wrapped = false;
        const std::size_t boundedContext = context > 0 ? ends[context - 1] : 0;
        // Each piece of a line takes the line's decision.
        reg::SourceDecisions pieceDecisions;
        if (const reg::SourceDecisions* d = options.sourceDecisions; d != nullptr && !d->lines.empty()) {
            for (std::size_t i = context; i < lines.size() && i - context < d->lines.size(); i++) {
                for (std::size_t piece = i > 0 ? ends[i - 1] : 0; piece < ends[i]; piece++) {
                    pieceDecisions.lines.push_back(d->lines[i - context]);
                    pieceDecisions.dialogue.push_back(d->dialogue[i - context]);
                }
            }
        }
        unwrapped.sourceDecisions = &pieceDecisions;
        const auto translated =
            TranslateDocumentPieces(snap, generated, bounded, observer, unwrapped, sourceLines, boundedContext);
        if (translated.size() != bounded.size()) return std::vector<std::string>(lines.begin(), lines.end());
        std::vector<std::string> joined(lines.size());
        for (std::size_t i = 0, piece = 0; i < ends.size(); i++) {
            while (piece < ends[i]) joined[i] += translated[piece++];
        }
        return joined;
    }
    const std::unique_ptr<pipeline::DocumentContext> doc = pipeline::BuildDocument(
        snap, sourceLines, !options.reference && options.wordImprovements.extraBuiltin, options.names);
    TranslationOptions named = options;
    named.documentPacks = doc->packs;

    Scratch s;
    word::SessionSenses senses;
    if (!options.reference && (options.wordImprovements.documentSenses || options.selectShadow)) s.sessionSenses = &senses;
    std::vector<std::string> out;
    out.reserve(lines.size());
    QuoteCarry carry;
    const std::vector<bool> closed = ClosedCarries(lines, carry);
    if (options.wrapped) {
        out = TranslateWrapped(snap, *doc, lines, closed, s, generated, observer, named, context);
    } else {
        for (std::size_t i = 0; i < lines.size(); i++) {
            UseDecision(s, i >= context ? options.sourceDecisions : nullptr, i - context, i - context);
            const StageObserver& watch = i < context ? StageObserver{} : observer;
            if (watch.line) watch.line(watch.context, static_cast<int>(i - context));
            // ReadLine takes the line without its one trailing "\r"; the "\r" is written back.
            const bool cr = !lines[i].empty() && lines[i].back() == '\r';
            // A quote left open counts on a line only when the line or a later one closes it.
            const LineQuotes quotes = QuotesOf(lines[i], carry);
            const QuoteCarry used = closed[i] ? quotes.start : QuoteCarry{};
            std::string translated = TranslateRubyBody(snap, *doc, lines[i], used, s, generated, watch, named);
            carry = quotes.end;
            if (s.sessionSenses != nullptr) s.sessionSenses->FinishLine();
            if (cr) translated += '\r';
            out.push_back(std::move(translated));
        }
    }
    UseDecision(s, nullptr, 0, 0);
    if (!snap.Healthy()) {
        const auto [fallback, replacement] = FallbackOf(snap);
        if (!fallback) return std::vector<std::string>(lines.begin(), lines.end());
        return TranslateDocumentPieces(*fallback, *replacement, lines, observer, options, sourceLines, context);
    }
    return out;
}

// The columns of s, each with the column separator or escape after it ("" after the last),
// as pipeline::Segment cuts a line of the text path.
std::vector<std::pair<std::string_view, std::string_view>> Columns(std::string_view s) {
    std::vector<std::pair<std::string_view, std::string_view>> out;
    std::size_t from = 0;
    for (std::size_t i = 0; i < s.size();) {
        std::size_t end = pipeline::internal::ColumnGap(s, i, false, false);
        if (end == i) end = pipeline::internal::EscapeGap(s, i, false, false);
        if (end != i) {
            out.emplace_back(s.substr(from, i - from), s.substr(i, end - i));
            from = i = end;
            continue;
        }
        const text::Rune r = text::DecodeRune(s.substr(i));
        i += r.size == 0 ? 1 : r.size;
    }
    out.emplace_back(s.substr(from), std::string_view());
    return out;
}

// The raw path of TranslateRawLine, without the filters.
std::string RawLine(const dict::Snapshot& snap, Generated& generated, std::string_view line,
                    const StageObserver& observer, const TranslationOptions& options) {
    if (!snap.Healthy()) {
        const auto [fallback, replacement] = FallbackOf(snap);
        if (!fallback) return std::string(line);
        return RawLine(*fallback, *replacement, line, observer, options);
    }
    Scratch s;
    if (observer.line) observer.line(observer.context, 0);
    dict::LookupContext lookup;
    lookup.plain = &snap.Plain(!options.reference && options.wordImprovements.extraBuiltin);
    if (options.filters) lookup.user = options.filters->User();
    std::string result;
    for (std::string_view piece : text::TranslationPieces(line)) {
        for (const auto& [column, gap] : Columns(piece)) {
            output::Pieces chunk;
            if (!column.empty()) chunk.push_back({std::string(column), output::SpanText});
            std::vector<output::Sentence> sentences;
            TranslateChunk(snap, lookup, chunk, 0, false, false, {}, LineSpeech{}, s, generated, observer, options,
                           sentences);
            output::Pieces composed;
            for (const output::Sentence& sentence : sentences) output::Append(composed, sentence.pieces);
            result += output::String(composed);
            result += gap;
        }
    }
    if (!snap.Healthy()) {
        const auto [fallback, replacement] = FallbackOf(snap);
        if (!fallback) return std::string(line);
        return RawLine(*fallback, *replacement, line, observer, options);
    }
    return result;
}

void FilterSnapshot(const StageObserver& observer, std::string_view stage, std::string_view line) {
    if (!observer.filterLog) return;
    const std::string message = "[" + std::string(stage) + "] " + std::string(line) + "\n\n";
    observer.filterLog(observer.context, message);
}

// Runs a filter stage over line without its trailing "\r".
void Filter(const filter::Filters& f, filter::Stage stage, std::string& line, filter::Filters::Toggles& toggles,
            const StageObserver& observer) {
    const bool cr = line.ends_with('\r');
    if (cr) line.pop_back();
    f.Apply(stage, line, toggles, {observer.context, observer.filterLog});
    if (cr) line += '\r';
}

}  // namespace

namespace {

// What the speech-level stage reads of a call: the resolved chains of the lines after the context,
// taken ahead of the caller's observer.
struct LevelWatch {
    reg::Capture capture;
    StageObserver inner;
};

LevelWatch& Watched(void* context) { return *static_cast<LevelWatch*>(context); }

StageObserver WatchLevels(LevelWatch& w, const StageObserver& inner) {
    w.inner = inner;
    StageObserver o = inner;
    o.context = &w;
    o.levelsOnly = !Observes(inner) && !inner.line;
    o.line = [](void* c, int line) {
        LevelWatch& lw = Watched(c);
        lw.capture.Line(line);
        if (lw.inner.line) lw.inner.line(lw.inner.context, line);
    };
    o.resolved = [](void* c, int chunk, int sentence, const word::Chain& chain) {
        LevelWatch& lw = Watched(c);
        lw.capture.Resolved(sentence, chain);
        if (lw.inner.resolved) lw.inner.resolved(lw.inner.context, chunk, sentence, chain);
    };
    if (inner.text)
        o.text = [](void* c, std::string_view tag, std::string_view text) {
            Watched(c).inner.text(Watched(c).inner.context, tag, text);
        };
    o.pass = [](void* c) {
        LevelWatch& lw = Watched(c);
        lw.capture.Pass();
        if (lw.inner.pass) lw.inner.pass(lw.inner.context);
    };
    if (inner.pieces)
        o.pieces = [](void* c, std::string_view tag, int chunk, const output::Pieces& pieces) {
            Watched(c).inner.pieces(Watched(c).inner.context, tag, chunk, pieces);
        };
    if (inner.tokens)
        o.tokens = [](void* c, int chunk, const tokenizer::Tokens& tokens) {
            Watched(c).inner.tokens(Watched(c).inner.context, chunk, tokens);
        };
    if (inner.analysis)
        o.analysis = [](void* c, int chunk, const tokenizer::Tokens& tokens, const word::Analysis& analysis) {
            Watched(c).inner.analysis(Watched(c).inner.context, chunk, tokens, analysis);
        };
    if (inner.composed)
        o.composed = [](void* c, int chunk, int sentence, const output::Pieces& pieces) {
            Watched(c).inner.composed(Watched(c).inner.context, chunk, sentence, pieces);
        };
    if (inner.cleaned)
        o.cleaned = [](void* c, const output::Pieces& pieces) { Watched(c).inner.cleaned(Watched(c).inner.context, pieces); };
    if (inner.restored)
        o.restored = [](void* c, const pipeline::RestoreSteps& steps) {
            Watched(c).inner.restored(Watched(c).inner.context, steps);
        };
    if (inner.filterLog)
        o.filterLog = [](void* c, std::string_view message) { Watched(c).inner.filterLog(Watched(c).inner.context, message); };
    return o;
}

// The line of each piece after the context when a line of the call is cut into pieces
// (text::kMaxLineBytes), else empty: the observer then counts pieces.
std::vector<int> PieceLines(std::span<const std::string> lines, std::size_t context) {
    std::vector<int> out;
    if (std::none_of(lines.begin(), lines.end(), [](const std::string& l) { return l.size() > text::kMaxLineBytes; }))
        return out;
    for (std::size_t i = context; i < lines.size(); i++)
        out.insert(out.end(), text::TranslationPieces(lines[i]).size(), static_cast<int>(i - context));
    return out;
}

}  // namespace

std::vector<std::string> TranslateDocument(const dict::Snapshot& snap, Generated& generated,
                                           std::span<const std::string> lines,
                                           const StageObserver& observer, const TranslationOptions& options,
                                           std::size_t context) {
    context = std::min(context, lines.size());
    // The filter log of the lines translated: the context lines are read only.
    const auto logged = [&](std::size_t i) { return i >= context && !lines[i].empty() && lines[i] != "\r"; };
    // The speech-level stage, after the translation of the call and before the post-filter.
    const bool levelsAsked = !options.reference && options.speechLevels == SpeechLevels::Auto;
    LevelWatch levelWatch;
    bool levels = levelsAsked && !options.filters && reg::Prepare(lines.subspan(context), levelWatch.capture);
    if (levels) levelWatch.capture.pieceLine = PieceLines(lines, context);
    StageObserver watched = levels ? WatchLevels(levelWatch, observer) : observer;
    const auto translated = [&](std::span<std::string> out) {
        return out.size() >= context ? out.subspan(context) : std::span<std::string>();
    };
    if (!options.filters) {
        if (observer.filterLog) {
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (!logged(i)) continue;
                FilterSnapshot(observer, "REQUEST", lines[i]);
                FilterSnapshot(observer, "PRE", lines[i]);
            }
        }
        const reg::SourceDecisions decided = levels ? reg::DecideSource(lines.subspan(context)) : reg::SourceDecisions{};
        TranslationOptions decidedOptions = options;
        decidedOptions.sourceDecisions = &decided;
        auto out = TranslateDocumentPieces(snap, generated, lines, watched, decidedOptions, lines, context);
        if (levels)
            reg::Retrofit(lines.subspan(context), levelWatch.capture, translated(out), {}, options.levelChanges, &decided);
        if (observer.filterLog) {
            for (std::size_t i = context; i < out.size() && i < lines.size(); ++i) {
                if (!logged(i)) continue;
                FilterSnapshot(observer, "TRANS", out[i]);
                FilterSnapshot(observer, "POST", out[i]);
            }
        }
        out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(std::min(context, out.size())));
        return out;
    }
    const filter::Filters& f = *options.filters;
    std::vector<std::string> filtered(lines.begin(), lines.end());
    std::vector<filter::Filters::Toggles> toggles(filtered.size(), f.Start());
    for (std::size_t i = 0; i < filtered.size(); i++) {
        if (logged(i)) FilterSnapshot(observer, "REQUEST", lines[i]);
        Filter(f, filter::Stage::Pre, filtered[i], toggles[i], i < context ? StageObserver{} : observer);
        filter::KeepSpacesAfterNumbers(lines[i], filtered[i]);
        if (logged(i)) FilterSnapshot(observer, "PRE", filtered[i]);
    }
    // A character the pre-filter writes is its own only where the text before it did not hold it.
    std::vector<char32_t> written(f.Written().begin(), f.Written().end());
    for (const std::string& line : lines) {
        for (std::string_view rest = line; !rest.empty();) {
            const text::Rune r = text::DecodeRune(rest);
            std::erase(written, r.cp);
            rest.remove_prefix(std::max<std::size_t>(r.size, 1));
        }
    }
    TranslationOptions writtenOptions = options;
    writtenOptions.filterWritten = written;
    levels = levelsAsked && reg::Prepare(std::span<const std::string>(filtered).subspan(context), levelWatch.capture);
    const reg::SourceDecisions decided =
        levels ? reg::DecideSource(std::span<const std::string>(filtered).subspan(context)) : reg::SourceDecisions{};
    writtenOptions.sourceDecisions = &decided;
    if (levels) levelWatch.capture.pieceLine = PieceLines(filtered, context);
    watched = levels ? WatchLevels(levelWatch, observer) : observer;
    std::vector<std::string> out =
        TranslateDocumentPieces(snap, generated, filtered, watched, writtenOptions, filtered, context);
    if (levels)
        reg::Retrofit(std::span<const std::string>(filtered).subspan(context), levelWatch.capture, translated(out), {},
                      options.levelChanges, &decided);
    // An empty input line is not post-filtered either.
    for (std::size_t i = context; i < out.size() && i < lines.size(); i++) {
        if (!lines[i].empty() && lines[i] != "\r") {
            FilterSnapshot(observer, "TRANS", out[i]);
            Filter(f, filter::Stage::Post, out[i], toggles[i], observer);
            FilterSnapshot(observer, "POST", out[i]);
        }
    }
    out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(std::min(context, out.size())));
    return out;
}

std::string TranslateRawLine(const dict::Snapshot& snap, Generated& generated, std::string_view line,
                             const StageObserver& observer, const TranslationOptions& options) {
    if (line.empty()) return RawLine(snap, generated, line, observer, options);
    FilterSnapshot(observer, "REQUEST", line);
    if (!options.filters) {
        FilterSnapshot(observer, "PRE", line);
        std::string out = RawLine(snap, generated, line, observer, options);
        FilterSnapshot(observer, "TRANS", out);
        FilterSnapshot(observer, "POST", out);
        return out;
    }
    filter::Filters::Toggles toggles = options.filters->Start();
    std::string text(line);
    const filter::LogSink log{observer.context, observer.filterLog};
    options.filters->Apply(filter::Stage::Pre, text, toggles, log);
    filter::KeepSpacesAfterNumbers(line, text);
    FilterSnapshot(observer, "PRE", text);
    std::string out = RawLine(snap, generated, text, observer, options);
    FilterSnapshot(observer, "TRANS", out);
    options.filters->Apply(filter::Stage::Post, out, toggles, log);
    FilterSnapshot(observer, "POST", out);
    return out;
}

}  // namespace hdTrans::internal
