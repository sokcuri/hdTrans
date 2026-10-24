#include "core/engine/driver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/engine/glue.h"
#include "core/engine/resolve.h"
#include "core/engine/rules.h"
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
#include "core/tokenizer/tokenize.h"
#include "core/text/lines.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"
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
        // (人気があ), not the base's inflection.
        r.inflected = static_cast<std::size_t>(first->group->length) <= okurigana;
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
};

// The pieces of the output as the tokenizer reads them (the span kinds are the same numbers).
std::vector<tokenizer::Piece> TokenizerPieces(const output::Pieces& pieces) {
    std::vector<tokenizer::Piece> out;
    out.reserve(pieces.size());
    for (const output::Piece& p : pieces) out.push_back({p.text, static_cast<int>(p.span)});
    return out;
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
    bool markedOnly = false;
    bool asked = false;
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
    s.ruleTally = options.ruleTally;
    return s;
}

// Translates one chunk: its tokens, the word analysis, then per sentence the resolve stage
// and the compose stage. The sentences are appended to out with the chunk index.
// speech is read from the start of the chunk.
void TranslateChunk(const dict::Snapshot& snap, const dict::LookupContext& lookup, const output::Pieces& chunk,
                    int index, bool text, bool verse, std::string_view before, const LineSpeech& speech, Scratch& s,
                    Generated& generated, const StageObserver& observer, const TranslationOptions& options,
                    std::vector<output::Sentence>& out) {
    s.texts.clear();
    const std::vector<tokenizer::Piece> pieces = TokenizerPieces(chunk);
    // A word of the dictionary that starts in the text before a space and goes on past it.
    const tokenizer::SpaceJoins joins = [&](std::string_view before, std::string_view after) {
        for (std::size_t start = 0; start < before.size();) {
            const std::string_view left = before.substr(start);
            // The candidates point into the query, so it lives as long as they do.
            const std::string query = std::string(left) + std::string(after);
            const dict::Candidates words = snap.GetWordInfo(query, dict::LookupMode::General, lookup);
            for (const dict::Candidate& c : words.list) {
                if (c.source.size() > left.size()) return true;
            }
            start += std::max<std::size_t>(1, text::DecodeRune(left).size);
        }
        return false;
    };
    tokenizer::TokenizeSpans(pieces, text, s.tokens, text && !options.reference ? &joins : nullptr);
    if (observer.tokens) observer.tokens(observer.context, index, s.tokens);

    word::Analysis analysis;
    word::Settings settings = AnalysisSettings(text, verse, options, s.sessionSenses);
    settings.before = before;
    settings.quoteDepth = speech.quotes.depth;
    word::Analyze(s.tokens, snap, lookup, settings, s.arena, analysis);
    if (observer.analysis) observer.analysis(observer.context, index, s.tokens, analysis);

    output::Settings composeSettings = ComposeSettings(text);
    composeSettings.casual = text && speech.casual;
    composeSettings.casualMarkedOnly = speech.markedOnly;
    composeSettings.asked = text && speech.asked;
    composeSettings.literal = text && !options.reference && options.wordImprovements.literalCleanup;
    composeSettings.quotes = speech.quotes;
    composeSettings.quoteStyles = &speech.styles;
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
            BuildTexts(s.tokens, analysis, sentence.first, sentence.last, s.texts);
            s.markers.assign(s.tokens.markers.begin() + sentence.markerBegin,
                             s.tokens.markers.begin() + sentence.markerEnd);
            s.composer.Compose(s.items, s.texts, sentence.first, sentence.last, s.markers, composeSettings,
                               generated.families, done.pieces);
        }
        if (observer.composed) observer.composed(observer.context, index, static_cast<int>(k), done.pieces);
        out.push_back(std::move(done));
    }
}

// Whether observer takes any stage of a line.
bool Observes(const StageObserver& o) {
    return o.text || o.pieces || o.tokens || o.analysis || o.resolved || o.composed || o.cleaned || o.restored ||
           o.filterLog;
}

// One line through every stage of the text path; the line's trailing "\r" is written
// back by the caller.
// carry is the quote an earlier line left open for this one.
std::string TranslateBody(const dict::Snapshot& snap, pipeline::DocumentContext& doc, std::string_view body,
                          QuoteCarry carry, Scratch& s, Generated& generated, const StageObserver& observer,
                          const TranslationOptions& options) {
    const pipeline::LineFacts facts = pipeline::ReadLine(doc, body);
    if (facts.wholeLine) return *facts.wholeLine;
    const pipeline::LineLookup lookup = pipeline::LookupOf(doc, facts);
    pipeline::LineContext ctx = pipeline::ContextOf(doc, lookup);
    if (options.filters) ctx.lookup.user = options.filters->User();
    ctx.exclamationNa = !options.reference && options.wordImprovements.exclamationNa;
    ctx.dialectForms = !options.reference && options.wordImprovements.dialectForms;

    std::string normalized = pipeline::JoinBraceRuby(pipeline::Normalize(facts.text, ctx), ReadBase, &snap);
    if (!options.reference) {
        const RespellLookup respell{snap, ctx.lookup};
        normalized = pipeline::RespellVariants(normalized, WordKeepsVariant, &respell);
    }
    if (observer.text) observer.text(observer.context, "N", normalized);
    const std::string sounded = pipeline::Sound(normalized, ctx, snap);
    if (observer.text) observer.text(observer.context, "SN", sounded);

    const output::Pieces typed = pipeline::ProtectTyped(sounded, ctx, snap);
    const output::Pieces marked = pipeline::MarkCornerBrackets(typed);
    if (observer.pieces) observer.pieces(observer.context, "P", -1, marked);

    std::vector<std::string> gaps;
    const std::vector<pipeline::Chunk> chunks = pipeline::Segment(marked, &gaps);
    LineSpeech speech;
    std::vector<bool> casual;
    if (!options.reference) {
        speech.asked = true;
        speech.quotes = {carry.depth, carry.depth > 0 ? 1 : 0};
        speech.styles = pipeline::QuoteStyles(output::String(marked), carry.depth);
        speech.casual = options.speech == Speech::Soft;
        casual = pipeline::CasualQuotes(facts.text, carry.depth);
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
        for (std::size_t i = 0; i < chunks.size(); i++) {
            if (watch.pieces) watch.pieces(watch.context, "CH", static_cast<int>(i), chunks[i]);
            if (!gaps[i].empty()) before.clear();  // a column starts as a line does
            TranslateChunk(snap, ctx.lookup, chunks[i], static_cast<int>(i), true, facts.verse, before, pass, s,
                           generated, watch, opts, sentences);
            const std::string chunkText = output::String(chunks[i]);
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
    if (!options.reference && facts.text.find("でしょ") != std::string_view::npos &&
        std::ranges::any_of(speech.styles, [](std::uint8_t s) { return (s & output::kPoliteStyle) != 0; })) {
        TranslationOptions trial = options;
        trial.ruleTally = nullptr;
        const std::vector<bool> polite = output::PoliteQuotes(compose(speech, StageObserver{}, trial), carry.depth);
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
        return output::SpokenWords(compose(speech, observer, options), carry.depth, spoken);
    }
    // A casual quote whose Korean keeps a polite end (고마워요, 좋네요) keeps its plain ends:
    // 해체 next to 해요체 reads as two speakers. The line is composed again without it, or
    // for an observer, which the first pass does not report to.
    // The first pass counts no rules either; the line is composed again when they are counted.
    TranslationOptions trial = options;
    trial.ruleTally = nullptr;
    const std::string first = compose(speech, StageObserver{}, trial);
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

std::vector<std::string> TranslateDocumentPieces(const dict::Snapshot& snap, Generated& generated,
                                                 std::span<const std::string> lines, Memory& memory,
                                                 const StageObserver& observer, const TranslationOptions& options,
                                                 std::span<const std::string> sourceLines) {
    if (!snap.Healthy()) {
        const auto [fallback, replacement] = FallbackOf(snap);
        if (!fallback) return std::vector<std::string>(lines.begin(), lines.end());
        return TranslateDocumentPieces(*fallback, *replacement, lines, memory, observer, options, sourceLines);
    }
    if (std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
            return line.size() > text::kMaxLineBytes;
        })) {
        std::vector<std::string> bounded;
        std::vector<std::size_t> ends;
        for (const std::string& line : lines) {
            for (std::string_view piece : text::TranslationPieces(line)) bounded.emplace_back(piece);
            ends.push_back(bounded.size());
        }
        const auto translated = TranslateDocumentPieces(snap, generated, bounded, memory, observer, options, sourceLines);
        if (translated.size() != bounded.size()) return std::vector<std::string>(lines.begin(), lines.end());
        std::vector<std::string> joined(lines.size());
        for (std::size_t i = 0, piece = 0; i < ends.size(); i++) {
            while (piece < ends[i]) joined[i] += translated[piece++];
        }
        return joined;
    }
    const std::unique_ptr<pipeline::DocumentContext> doc = pipeline::BuildDocument(
        snap, sourceLines, !options.reference && options.wordImprovements.extraBuiltin, &memory.document);

    Scratch s;
    word::SessionSenses pending;
    if (!options.reference && (options.wordImprovements.documentSenses || options.selectShadow)) {
        if (snap.Mapped()) { pending = memory.senses; s.sessionSenses = &pending; }
        else s.sessionSenses = &memory.senses;
    }
    std::vector<std::string> out;
    out.reserve(lines.size());
    QuoteCarry carry = memory.quotes;
    const std::vector<bool> closed = ClosedCarries(lines, carry);
    for (std::size_t i = 0; i < lines.size(); i++) {
        if (observer.line) observer.line(observer.context, static_cast<int>(i));
        // ReadLine takes the line without its one trailing "\r"; the "\r" is written back.
        const bool cr = !lines[i].empty() && lines[i].back() == '\r';
        // A quote left open counts on a line only when the line or a later one closes it.
        const LineQuotes quotes = QuotesOf(lines[i], carry);
        const QuoteCarry used = closed[i] ? quotes.start : QuoteCarry{};
        std::string translated = TranslateBody(snap, *doc, lines[i], used, s, generated, observer, options);
        carry = quotes.end;
        if (s.sessionSenses != nullptr) s.sessionSenses->FinishLine();
        if (cr) translated += '\r';
        out.push_back(std::move(translated));
    }
    if (!snap.Healthy()) {
        const auto [fallback, replacement] = FallbackOf(snap);
        if (!fallback) return std::vector<std::string>(lines.begin(), lines.end());
        return TranslateDocumentPieces(*fallback, *replacement, lines, memory, observer, options, sourceLines);
    }
    if (s.sessionSenses == &pending) memory.senses = std::move(pending);
    memory.senses.Trim();
    memory.quotes = carry;
    pipeline::RememberDocument(snap, sourceLines, memory.document);
    return out;
}

// The columns of s, each with the column separator after it ("" after the last), as
// pipeline::Segment cuts a line of the text path.
std::vector<std::pair<std::string_view, std::string_view>> Columns(std::string_view s) {
    std::vector<std::pair<std::string_view, std::string_view>> out;
    std::size_t from = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (const std::size_t end = pipeline::internal::ColumnGap(s, i, false, false); end != i) {
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

std::vector<std::string> TranslateDocument(const dict::Snapshot& snap, Generated& generated,
                                           std::span<const std::string> lines, Memory& memory,
                                           const StageObserver& observer, const TranslationOptions& options) {
    if (!options.filters) {
        if (observer.filterLog) {
            for (const auto& line : lines) {
                if (line.empty() || line == "\r") continue;
                FilterSnapshot(observer, "REQUEST", line);
                FilterSnapshot(observer, "PRE", line);
            }
        }
        auto out = TranslateDocumentPieces(snap, generated, lines, memory, observer, options, lines);
        if (observer.filterLog) {
            for (std::size_t i = 0; i < out.size() && i < lines.size(); ++i) {
                if (lines[i].empty() || lines[i] == "\r") continue;
                FilterSnapshot(observer, "TRANS", out[i]);
                FilterSnapshot(observer, "POST", out[i]);
            }
        }
        return out;
    }
    const filter::Filters& f = *options.filters;
    std::vector<std::string> filtered(lines.begin(), lines.end());
    std::vector<filter::Filters::Toggles> toggles(filtered.size(), f.Start());
    for (std::size_t i = 0; i < filtered.size(); i++) {
        if (!lines[i].empty() && lines[i] != "\r") FilterSnapshot(observer, "REQUEST", lines[i]);
        Filter(f, filter::Stage::Pre, filtered[i], toggles[i], observer);
        filter::KeepSpacesAfterNumbers(lines[i], filtered[i]);
        if (!lines[i].empty() && lines[i] != "\r") FilterSnapshot(observer, "PRE", filtered[i]);
    }
    std::vector<std::string> out = TranslateDocumentPieces(snap, generated, filtered, memory, observer, options, filtered);
    // An empty input line is not post-filtered either.
    for (std::size_t i = 0; i < out.size() && i < lines.size(); i++) {
        if (!lines[i].empty() && lines[i] != "\r") {
            FilterSnapshot(observer, "TRANS", out[i]);
            Filter(f, filter::Stage::Post, out[i], toggles[i], observer);
            FilterSnapshot(observer, "POST", out[i]);
        }
    }
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
