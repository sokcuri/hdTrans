#pragma once
// The compose stage: the resolved items of a sentence become Korean text written into
// the texts of their tokens, which are then joined into the output pieces of the
// sentence.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/output/families.h"
#include "core/output/item.h"
#include "core/output/pieces.h"
#include "core/tokenizer/token.h"

namespace hdTrans::output {

// The 「」 and 『』 open at a point of a line, and the number of quotes opened outside any
// quote before it (with the one an earlier line left open).
struct QuoteState {
    int depth = 0;
    int count = 0;
};

// A quote style besides those of pipeline::QuoteStyles: the Japanese of the quote is
// casual speech (pipeline::CasualQuotes).
constexpr std::uint8_t kCasualSpeech = 32;
constexpr std::uint8_t kPoliteStyle = 1;  // pipeline::kPoliteSpeech, a quote in です and ます
// A quote of polite speech whose Korean ends a sentence politely and whose Japanese has no casual
// mark (the engine sets it for でしょ).
constexpr std::uint8_t kPoliteTalkStyle = 64;

// The switches of the stage.
struct Settings {
    int hanjaToHangul = 1;  // 1: kanji left in the text are written as their Hangul reading
    int deleteJapan = 1;    // 1: kana left in the text are dropped
    bool cleanup = false;   // drop dictionary glosses and doubled generated spaces
    bool text = false;      // input of the text path: kana runs the items drop are kept, particles
                            // alternate by ParticleClass
    // Text path: the plain sentence ends (간다, 다르다, 학생이다) of the text inside 「」 and
    // 『』 in 해체 (갈 거야, 달라, 학생이야); quotes is the state at the first token of the
    // sentence, and the quoteStyles of the line (pipeline::QuoteStyles) keep polite and old
    // speech.
    bool casual = false;
    // Only the quotes whose style has kCasualSpeech.
    bool casualMarkedOnly = false;
    // Text path: the questions of speech (WriteCasual) in their spoken forms without casual
    // too: a plain question in a quote of no style in 해체 (大丈夫か？ 괜찮아?), だろう before
    // ？ as agreement asked or a wonder.
    bool asked = false;
    // The text outside 「」 and 『』 is narration (pipeline::LineContext::narrated): an end before
    // a mark stays plain there.
    bool narrated = false;
    // Text path outside the reference mode: counts in native Korean (두 마리, 세 번) and the
    // spacing of particle and free nouns (바위뿐, 키 정도, 작업 중).
    bool literal = false;
    QuoteState quotes;
    const std::vector<std::uint8_t>* quoteStyles = nullptr;
};

// For each quote of the Korean line (indexed as pipeline::QuoteStyles, depth the quote
// an earlier line left open), whether its own text ends a sentence politely (고마워요,
// 좋네요, 합니다).
std::vector<bool> PoliteQuotes(std::string_view line, int depth);

// line with the written words of its quotes in their spoken form, in every plain or polite
// quote: 무엇 (무엇을 뭘, 무엇입니까 뭡니까), 것 (것은 건, 그것을 그걸; 것 같다 stays) and 주어,
// 두어, 되어 (줘, 둬, 돼); quotes and depth as for PoliteQuotes.
std::string SpokenWords(std::string_view line, int depth, const std::vector<std::uint8_t>& styles);

// The state of state after text: 「 and 『 open a quote, 」 and 』 close one (a closing
// bracket outside a quote is ignored), and count counts the quotes opened outside any.
QuoteState QuotesAfter(QuoteState state, std::string_view text);

// A token of the chunk as the compose stage reads and writes it: its type and text
// after the per-token stage (the text receives the Korean of the items), its span kind
// and, for a span, the span text.
struct TokenText {
    int type = 0;
    int span = 0;
    std::string text;
    std::string spanText;
};

// An item as FinalOutputPrepass wrote it: its composed text (after the gloss removal),
// that text after the spacing rules, and whether a space follows it.
struct Inflected {
    int item = 0;
    std::string composed;
    std::string spaced;
    bool space = false;
};

// The output nodes of a sentence: consecutive runs of items, as the ends of the runs.
using Nodes = std::vector<int>;

// Splits the items of a sentence (in chain order) into output nodes: an item plus what
// attaches to it (a Z6 run with the following item, a kanji item of a type-24 token
// with the single kanji items linked to it in the same token, a following Z7 run, and
// the particles of the same token).
void BuildItems(const std::vector<ResolvedItem>& items, const std::vector<TokenText>& tokens, Nodes& nodes);

// Composes sentences, keeping its buffers from one sentence to the next.
class Composer {
public:
    // Composes the items of the sentence of tokens first..last (BuildItems,
    // FinalOutputPrepass) and writes the sentence's output (EmitSynchronousOutput)
    // to out. tokens is the whole chunk: those of the sentence as the per-token stage
    // left them, the one after it as scanned. Items and markers are edited in place.
    void Compose(std::vector<ResolvedItem>& items, std::vector<TokenText>& tokens, int first, int last,
                 std::span<tokenizer::ControlMarker> markers, const Settings& settings, const Families& generated,
                 Pieces& out);

    // Composes every item of the nodes (last item first) and writes the composed
    // text, spaced by the spacing rules and with the pending control markers (&#CS,
    // </a>) put back, into the item's token text in place of the source it covers.
    void FinalOutputPrepass(std::vector<ResolvedItem>& items, const Nodes& nodes, std::vector<TokenText>& tokens,
                            int first, int last,
                            std::span<tokenizer::ControlMarker> markers, const Settings& settings,
                            const Families& generated);

    // With record, the items FinalOutputPrepass writes, in the order it writes them.
    bool record = false;
    std::vector<Inflected> inflected;

    // The composed text of item i, as the composer's marker rules make it (action 0).
    std::string Build(int i);

    // Starts a composition of items without tokens (Build only).
    void Start(std::vector<ResolvedItem>& items, const Settings& settings, const Families& generated);

private:
    struct Scratch {
        std::string text;   // composed text, " " once consumed
        std::string saved;  // text of a kanji item replaced by its surface
        bool spoken = false;  // WriteCasual gave the item a casual sentence end
    };
    struct Written {
        int token;
        int start;
        int end;
        bool blank;
        bool empty;
    };

    std::string Build(int i, int action, std::string_view mode);
    void WriteCasual(int first, int last);
    // The mark that ends the sentence of speech after item i, 0 for none; asked tells whether
    // it asks, clipped whether a drawn end cut with っ comes before it (行くぅぅぅっ).
    char32_t QuotedEnd(int i, int last, bool nested, bool* asked = nullptr, bool* clipped = nullptr,
                       bool* exclaimed = nullptr, bool* drawn = nullptr) const;
    bool AskConjecture(int i, std::string_view clause, std::string_view own, bool casual);
    bool AskPlain(int i, std::string_view own);
    bool AskFormal(int i, bool pressed);
    // Whether the text after item i goes on in its sentence past the … or ～ that follow it
    // (違う…何か 다르다…뭔가), not ending there (違う……, 違う…。).
    bool GoesOn(int i, int last) const;
    void AskWord(int i, int last);
    bool Stative(std::string_view marker) const;
    std::string PresentWord(std::string_view ending, std::string_view stem) const;
    void BuildAdjective(int i);
    void BuildAction(int i, int action, std::string_view mode);
    // Whether a span token (a markup tag) stands between the tokens of a and b.
    bool SpanBetween(const ResolvedItem& a, const ResolvedItem& b) const;
    // Whether item i writes only its stem: the next item's text starts with the same
    // conjugation marker and no tag stands between them.
    bool TextGate(int i, std::string_view key);
    std::pair<std::string, std::string> CopulaForm(int i, std::string_view key, std::string_view replacement,
                                                   std::string_view suffix) const;
    std::string NormalizeDakeiyoText(int i, std::string_view value) const;
    std::string ApplyParenthesisTextFilter(std::string text, int i);
    std::string Alternate(std::string_view text, std::string_view context, bool hasContext) const;
    std::string Context(std::string text) const;
    void WriteItem(int i, bool isLast, std::span<tokenizer::ControlMarker> markers);
    std::string KeepDropped(int i, std::string translated, std::string_view remainder);
    // With settings.literal, the text of item i as a count is written: a count and its counter
    // in one item (三回 세 번, 1番目 첫 번째), a number before a counter item (三 세), or a
    // counter after a native count (回 번); other text as is. Spacing writes the space.
    std::string WriteCount(int i, std::string text) const;
    std::pair<std::string, bool> Spacing(int i, std::string t, std::string_view tags, std::string_view nextTags,
                                         char kind, char nextKind, bool isLast, bool converted,
                                         std::string_view remainder) const;
    std::string_view Lookup(char table, std::string_view key) const;

    std::vector<ResolvedItem>* items_ = nullptr;
    std::vector<TokenText>* tokens_ = nullptr;
    const Families* generated_ = nullptr;
    Settings settings_;
    std::vector<Scratch> state_;
    std::vector<std::string> source_;  // with settings.text, the token texts before composing
    int first_ = 0;                    // the first token of the sentence being composed
    std::vector<Written> written_;     // with settings.text, the items written so far
    Nodes nodes_;
};

// The output pieces of the token texts first..last of a sentence: before a token that
// is not text (types other than 1, 2, 15, 16, 22, 24, 79), and before a symbol that starts
// with an ellipsis or a mark of tone (… ‥ ♪ ☆ ★ ♡ ♥) after a syllable, trailing spaces
// are trimmed unless the previous token is a space; a space separates a token from a preceding
// "</A>", "</a>" or "&#CS". A span token writes its span text as a span piece, then
// what compose added to its text; a space written before a closing-tag span moves after
// it. An opening bracket token other than 〔 (【 〈 《 ［ ‘ ｛) after a syllable, a Latin letter, a digit, a
// word span or a comma takes a space before it. With settings.cleanup a space the engine wrote
// never forms a run with another space; input whitespace tokens are kept as they are.
void EmitSynchronousOutput(const std::vector<TokenText>& tokens, int first, int last, const Settings& settings,
                           Pieces& out);

// Empties the space token between a word in Latin letters, digits or a word span that
// ends in no punctuation (junya: と stays apart) and a Japanese token of tokens
// first..last that starts with a particle written in Korean: a
// Korean particle joins its word also where the source puts a space (wiki の記事 wiki의
// 기사, API です API입니다).
void JoinParticlesAfterSpace(const std::vector<ResolvedItem>& items, std::vector<TokenText>& tokens, int first,
                             int last);

// Appends a space after a sentence ending in ".", "!" or "?" (token types other than
// 26 and 78) when the next token is not a sentence end itself and does not start with
// a closing bracket of classes.tsv mark.close-after-space.
void ApplyOutputPostSpacing(const std::vector<TokenText>& tokens, int index, Pieces& out);

// The source text left over around a translated item: kanji as their Hangul reading
// (hanjaToHangul), hiragana, katakana and 、 in their canonical forms or dropped
// (deleteJapan; 、 and ・ are always written), other characters as they are.
std::string FilterKana(std::string_view text, const Settings& settings);

// A kanji numeral of up to two digits (二十五 -> "25", 十 -> "10", 三 -> "3") as ASCII
// digits; a first character that is not a numeral reads as 0. Text that starts with an
// ASCII character is returned as is.
std::string EncodeNumericPair(std::string_view text);

// Text that starts with an ASCII symbol, is one of 。？（）「」 or is a run of the
// full-width symbol set or the start of one of its misreads.
bool IsOutputBuilderBlockedText(std::string_view text);

// A surface of one character that is a level-2 kanji or a character after them in the
// Shift-JIS code order.
bool HighLeadPair(std::string_view s);

// A target in a copula family ($AH@, %D4@: row 1 이, row 6 (이어)여): a noun with 이다
// (큰일$AH@). The verb 보이다 (보%D4@) shares the families and is not one.
bool CopulaTarget(std::string_view target, const Families& generated);

// Added to the kana (U+3041..U+30FF) that the text path keeps where the translation
// would drop them, which makes them private-use characters.
constexpr char32_t kDropShift = 0xC000;

namespace internal {

// The entry i of an item: the selected entry, entry 0, or an empty entry for any other
// index.
const ResolvedEntry& EntryAt(const ResolvedItem& it, int i);
inline const ResolvedEntry& Selected(const ResolvedItem& it) { return EntryAt(it, it.selected); }

// The spoken contraction of an 아/어 form that ends a sentence (주어 줘, 되어 돼, 보아 봐)
// or of a past stem (되었 됐, 주었 줬). Other forms, ㅣ with 어 among them (피어, 아니어),
// are returned unchanged.
std::string SpokenInfinitive(std::string_view form);

}  // namespace internal

}  // namespace hdTrans::output
