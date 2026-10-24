#pragma once
// Rules that reject the senses a word cannot have next to its neighbours, stated by class.
// They run on the candidates of each text-path lookup (Worker::ApplyConstraints).
#include <cstdint>
#include <span>
#include <string_view>

namespace hdTrans::word {

// The word a rule applies to.
enum class ConstraintWord : uint8_t {
    Katakana,  // a whole katakana run of two or more characters
    Hiragana,  // a hiragana run of two or more characters outside a kana or kanji word
    Kanji,     // a kanji word not after another kanji or the prefixes お and ご
    Source,    // the word whose source is the rule's source
    EndsInSource,  // a word whose source ends in the rule's source (していただけ, いただけ)
    EndsInDe,  // a word of two or more characters whose source ends in で (歳で, 途中で)
    OStem,     // お and a verb stem ending in hiragana, three characters or more (お待ち, おやめ),
               // not the greetings お帰り and お休み
    EndsInKu,  // a word of two or more characters whose source ends in く (したく, きたく)
    IruForm,   // a word of word.iru-form: spelled like a form of いる and more (いろ 色, いよう 異様)
    Cued,      // a word whose source a cue of the line or its document names (LookupContext::cued)
    WithKanji, // a word of two or more characters with a kanji
};

// What must be next to the word.
enum class ConstraintContext : uint8_t {
    Honorific,          // an honorific suffix follows, and no longer word starts with the word
    FamiliarHonorific,  // the same with くん, 君, ちゃん or たん, a name rather than a role before it
    AfterKatakana,      // the word follows katakana in its token
    AfterMask,          // the word follows a mask character such as ○ or △ (past spaces)
    AfterExclamation,   // the word follows ！ or ？ (past spaces) and is not drawn out (とっ, とぉ)
    AfterPause,         // the same with 、 and … as well, before 奴, こと, 話 and like nouns
    LineOpening,        // only spaces and opening brackets before the word in its line, 、 after it
    Elsewhere,          // not LineOpening
    BeforeNumeralAgo,   // a numeral follows, and 前 within eight characters after it, before any punctuation
    AfterVerbEnding,    // right after a verb in た, だ or る (行った先, 見やる先), not after まだ
    AfterDemonstrative, // right after この, その or あの
    BeforeNaru,         // なる follows (こうなる), not the old attributive of an adjective (こうなるべく)
    BeforeAgaru,        // 上が follows (一人立ち上がり), so the shorter word ends inside a longer one
    AfterPredicate,     // right after た, だ, る, う, い, す, ん or a drawn-out え, ー in the token or the
                        // token before (だったわ, ですわ, 知らねーわ)
    NotAfterQuantity,   // no numeral among the three characters before the word (化け物め, not 三回め or 5人め)
    NotBeforeNoun,      // no kanji or katakana right after the word (そう言った。, そう言ったが)
    BeforeStem,         // a kanji or a hiragana that starts no particle, copula or auxiliary of the
                        // continuative right after the word, and no kanji or katakana word right before
                        // it (余り好き, 余り伸びない; not 余りの, 余りに, 余り、, 余り過ぎる, 余ります, 在庫余り)
    AfterTeNotAdjective, // right after て or で, and no い, か, く, け, し or だ after the word (見ていたんだ)
    AfterTe,            // right after て or で (待っていろ, 黙っていたんだ)
    AfterTeBeforeTa,    // the same with た after the word (待っていたって)
    AfterSmallTsu,      // right after っ or ッ (行ったんと: the た of a past form)
    Detached,           // nothing, a space or punctuation right before the word (名前: め)
    BeforeSuruForm,     // a form of する goes on from the word's last し (ことしないで, ことしてる)
    OpeningKotode,      // only spaces, opening brackets or an ellipsis before the word in its line, うことで、
                        // after it (ということで、 at the start of a sentence)
    BeforeAru,          // ある, あり, あっ, あれ or あろ follows and every sense of the word is a noun
                        // (歳であり: the copula である; not まである)
    BeforeRai,          // らい follows, so the word's last く starts くらい (したくらい, きたくらい)
    NounSlot,           // not right after を, に, へ or で, and punctuation, the end, a particle, a copula,
                        // ある or ない follows the word rather than a verb ending (わけ。, わけで, わけあって,
                        // not わけて, わけた, わけよう)
    BeforeNasai,        // なさい or なさいませ follows (お待ちなさい)
    AfterWordBeforeTe,  // a word right before and て after (感じはしていた; not はしで食べる)
    BeforeRassharu,     // らっしゃ follows (座っていらっしゃる: the い starts いらっしゃる)
    BeforeCopula,       // だ, で, じゃ, に, の, か or なん follows, or no letter, which no verb stem
                        // takes (いただけだ, いただけなんです, いただけ〜)
    AfterTeBeforeDake,  // right after て or で, with け after the word and then what BeforeCopula
                        // takes (いただ of いただけ〜)
    BeforeDake,         // the same without the て or で (していただ of していただけ。)
    NotCommonSense,     // no start of name.common-after follows, which keeps the common sense of
                        // weather, sky and sea words (時雨が降る, 雷が落ちた)
    BeforeChan,         // the word ends in ち and ゃん follows, the rest of ちゃん (夕立ち before ゃん), not ゃんと
};

// The senses a rule rejects.
enum class ConstraintReject : uint8_t {
    // Every sense, when none has a person, animal or role code nor sounds like the kana of
    // the word, and the name layer has a person reading that does; that reading replaces
    // them as a name.
    NonPerson,
    // The senses other than a name sense (QPN, Wa or Wb), when the word has a name sense
    // and no person sense, and no verb or adjective as long starts where it does; the first
    // name sense replaces them as a name.
    NonName,
    // The same, when every other sense is a place name (QCT, QNN).
    NonNameBesidePlace,
    // The pronoun senses (N12x), when another sense remains.
    Pronoun,
    // The senses that are no verb (M), when a verb sense of the word remains.
    NonVerbs,
    // The senses that do not quote (whose Korean has no 라고, 다고, 라는 or 다는), when
    // one quotes.
    NonQuote,
    // The senses that are not adverbs (E), when one is.
    NonAdverb,
    // The adverb senses (E), when another sense remains.
    Adverb,
    // The senses whose Korean is not the rule's target, when one is.
    OtherTargets,
    // The senses whose Korean is the rule's target, when another sense remains.
    Target,
    // Every sense of the word, when a candidate with a longer source remains.
    Word,
    // The verb senses (M) of the word and of the shorter words it starts with, when a sense
    // of the word that is no verb remains.
    Verbs,
    // Every sense, when the word has a name: a person sense among the candidates or in the
    // name layer (for a kana word one that sounds like the kana) that is no Korean word of
    // its other senses; that name replaces them. Not when a person noun, a user entry or a
    // verb or adjective that inflects past the word reads it.
    NonCuedName,
};

struct ConstraintRule {
    std::string_view id;
    ConstraintWord word;
    std::string_view source;  // ConstraintWord::Source and EndsInSource only
    ConstraintContext context;
    ConstraintReject reject;
    std::string_view note;    // the rule in Korean
    std::string_view target;  // ConstraintReject::OtherTargets and Target only
};

// In table order; each rule runs on the candidates the earlier ones left.
std::span<const ConstraintRule> ConstraintRules();

}  // namespace hdTrans::word
