#pragma once
// The end-of-sentence passes of the word stage. They run over the item chain of one
// sentence (Worker::chain) in a fixed order and turn the analyzed items into the
// resolved chain: the replacements of katakana runs and noun phrases, the selection
// of each item's candidate and ending by the rules, and the passes that fix class-3
// words, negatives and terminal forms.
#include <cstddef>
#include <span>
#include <string_view>

#include "core/output/families.h"
#include "core/rules/rules.h"
#include "core/word/worker.h"

namespace hdTrans::dict {
struct PackRow;
}

namespace hdTrans::word {

// What the passes read besides the chain: the rule program of the selection and the
// marker families of the output lookups. The passes add the families they make (the
// conjugating targets of the text path), so the families are shared with the output stage.
struct Flusher {
    Worker& w;
    const rules::Program& program;
    output::Families& families;
    // The packs layer's attachment of the chain (packs.cpp), asked of Settings::attach once per pass.
    struct Attached {
        bool asked = false, held = false;
        Attachment attachment;
        std::vector<const Item*> items;  // the chain's items, in the attachment's order
    } attached{};
};

// Runs the sentence passes over w.chain (the chain of one sentence after the token loop).
void Flush(Flusher& f);
// The names of the text-path passes in order; the families of the construction table run after them.
std::span<const std::string_view> TextPassNames();

// Selection (selectors.cpp).
void CleanupSelectors(Flusher& f);
void ApplyKj(Item* it);
bool IsStative(Flusher& f, Item* it);

// Terminal form (terminal.cpp).
void PreferTerminal(Flusher& f);

// Passes (passes.cpp).
void DrainNmTail(Flusher& f);
void ReadUnplacedHeads(Flusher& f);
void ReadUnplacedParticles(Flusher& f);
void ReinsertPhrases(Flusher& f);
void CleanupClass3(Flusher& f);
void ApplyNegatives(Flusher& f);

// Packs (packs.cpp, docs/packs.md). PackVoice sets v from the voice row that decides verb item
// it (keyed by its surface, else by stem, the verb a dictionary word in (ら)れ is made from; its
// cue reads v as the voice the chain decided) and returns the row, null for none; a row that reads
// the passive does not apply when atLast (an adverb of adverb.at-last in the clause, where the (ら)れ may
// be the potential). ApplyPacks applies the sense, name, polarity and tense rows.
const dict::PackRow* PackVoice(Flusher& f, Item* it, std::string_view stem, Voice& v, bool atLast);
void ApplyPacks(Flusher& f);

// Helpers of the text-path passes. TakeTarget (sense.cpp) selects the candidate of class
// with target, or writes target over the selected one. SelectLemma (grammar.cpp) selects
// the first entry whose dictionary form is lemma and reports whether there was one. Thing
// and Person classify dictionary attributes.
void TakeTarget(Item* it, char cls, std::string_view target, Selection source = {Provenance::Rewrite});
bool SelectLemma(Flusher& f, Item* it, std::string_view lemma);
bool Thing(std::string_view attr);
// The dictionary form of entry slot of it (an empty string for no entry).
std::string ItemLemma(Flusher& f, const Item* it, int slot);
bool Person(std::string_view attr);
// Whether the item is a comma, a sentence-end mark, a closing quote or bracket, or 。 (grammar.cpp).
bool PauseOrEnd(const Item* it);
// The text before the item on its line without the spaces next to it: the rest of the item's token
// before it, or else the nearest earlier token that is not only spaces; empty at the line start.
// token receives the index of the token the text is from (grammar.cpp).
std::string_view TextBefore(const Worker& w, const Item* it, int& token);
// Whether nothing but spaces stands before the item on its line, or a sentence end does (grammar.cpp).
bool OpensLine(const Worker& w, const Item* it);
// Whether marks of mark.restart (…, ―) stand right before the item and some text before them: the
// item restarts what was being said (私が…私が). Not at the start of a line (grammar.cpp).
bool AfterRestart(const Worker& w, const Item* it);
// Whether s has only digits (ASCII or full-width) and kanji numerals, and is not empty (grammar.cpp).
bool NumberSurface(std::string_view s);
// The target of the dictionary verb whose passive surface is (書かれ 書 쓰다, 見られ 見 보다), its
// default record first; "" when there is none or the verb is one whose passive stays passive with an
// object (verb.passive-any-object, verb.passive-after-object, verb.receiver-passive) (grammar.cpp).
std::string PassiveBaseVerb(Flusher& f, std::string_view surface);
// Whether the item starts with a verb of saying or thinking, or is 聞く (grammar.cpp).
bool QuoteVerbFollows(const Item* it);
// Whether the item is followed by the end of the text, by closing punctuation (！？。」』）) or by a
// span boundary (grammar.cpp).
bool QuoteEnd(const Worker& w, const Item* it);
// Whether the item ends its sentence: the end of the utterance (UtteranceEnd), or ！？!? after it
// (grammar.cpp).
bool SentenceEnd(const Worker& w, const Item* it);
// Whether the text after the stem of the item is い or かった, a comma or none, and a particle of
// particle.contrast: a contrast that goes on (大した傷ではないが) (grammar.cpp).
bool ContrastFollows(const Item* it);
// Whether s is katakana ァ..ヶ and ー only, and not empty (grammar.cpp).
bool KatakanaSurface(std::string_view s);
// Whether the item is followed in its token by a noun that it describes, not at the end of the
// utterance (楽しみにしてたさ); before selection the first candidate of the next item counts
// (grammar.cpp).
bool DescribedNoun(const Worker& w, const Item* it);
// Whether a verb follows it in its sentence (not an auxiliary of its て form), with only words and
// particles between (connective.cpp).
bool PredicateFollows(const Item* it);

// Text-path grammar (grammar.cpp).
// Decides once, before selection, how each verb's (ら)れ reads (Detail::voice, spec GRM-10): the
// passive, the potential, the respectful form or a feeling that comes by itself, and writes the
// passive and potential into the record, the dictionary candidates and the agent's に.
void DecideVoice(Flusher& f);
// The (ら)れ that DecideVoice reads as respectful is written as the honorific: 書かれた 쓰셨다,
// 来られた 오셨다.
void RespectfulPassive(Flusher& f);
// A selected passive verb whose Korean has a passive of its own (먹히다, 고용되다) is written
// with it instead of 어지다.
void OwnPassives(Flusher& f);
// A singular personal pronoun before the plural suffix たち, 達, ら or ども reads as its
// plural and the suffix as nothing: 나 우리, 저 저희, 너 너희 (ボクたち 나들 → 우리).
void PluralPronoun(Flusher& f);
// The counter 度 after a number of 10 or more or with a decimal point reads as the degree 도,
// not the count 번, when the number is written in digits (180度 180도, 0.5度 0.5도) or in
// kanji other than the bare 十 before a word of angle or temperature or after a noun of one
// in the sentence (百八十度変わった 180도, 角度は四十五度 45도); 度 before も, ほど or 目
// counts (十度も 열 번이나, 五度 5번, 十度目 열 번째).
void DegreeAfterNumber(Flusher& f);
void HumbleFirstPerson(Flusher& f);
void InitialSoundRule(Flusher& f);

// Text-path て forms (connective.cpp). UtteranceEnd reports an item followed by the end
// of the utterance (closing punctuation, a pause or the end of the chunk).
void TeThanks(Flusher& f);
void VerseTe(Flusher& f);
void ResultState(Flusher& f);
// The ている of a verb of thinking or perceiving, or of one whose Korean is an adjective, read
// as the simple form (思っている 생각한다, 見えている 보인다); that of a verb of saying or of
// 付き合う except in the plain present and in 지 (言っていた 말했다, 付き合っている人 사귀는 사람).
void SimpleAspect(Flusher& f);
// The てある of an action done beforehand read as 어 두었다 (用意してある 준비해 두었다); after a が
// subject, the state, and for a Korean passive it stays 있다 (窓が開けてある 창이 열려 있다).
void PreparedState(Flusher& f);
void NotYet(Flusher& f);
// A verb stem not in the dictionary before 込む takes the ending of 込む, which writes nothing
// (惚れ込む 반하다, 老け込んだ 늙었다): the Korean verb alone carries the action.
void InwardAuxiliary(Flusher& f);
// A verb continuative before も and する: V-지도 않다 with a negative する, V-기도 하다 otherwise.
void EmphaticMo(Flusher& f);
void PastSequence(Flusher& f);
void QuoteTo(Flusher& f);
void TerminalBeforeQuote(Flusher& f);
// A sentence-final だろう after a question word in its clause asks (何だろう 뭘까, どっちだろう
// 어느 쪽일까); a concessive も, (よ)うと or (よ)うが or a か between keeps the guess.
void WonderingDarou(Flusher& f);
void LongVowelAfterKatakana(Flusher& f);
// Two or more のか clauses side by side (良かったのか、悪かったのか) read alike, in 지.
void ParallelNoka(Flusher& f);
// An offer that ends a quoted sentence, or any sentence with a final particle (教えてやるよ,
// 手伝ってあげます), reads as a promise (가르쳐 줄게, 도와 드릴게요), or 주마 in old or
// haughty speech.
void OfferPromise(Flusher& f);

// Text-path terminal and negative passes (grammar.cpp).
void ExclaimedAdjective(Flusher& f);
void VerbNegative(Flusher& f);
bool UtteranceEnd(const Worker& w, const Item* it);

// Whether the item's surface is one of set.
template <std::size_t N>
bool IsOneOf(const Item* it, const std::string_view (&set)[N]) {
    return it != nullptr && InSet(set, it->group->surface);
}

// Whether the item is word, a key or the dictionary form of the verb or adjective the item is a
// stem of (sense.cpp). IsAny is IsWord over a list of words.
bool IsWord(const Item* it, std::string_view word);
bool IsWord(const Item* it, std::string_view word, int candidate);
template <class Words>
bool IsAny(const Item* it, const Words& words) {
    for (const auto& w : words) {
        if (IsWord(it, w)) return true;
    }
    return false;
}

// Text-path senses and conjunctions (sense.cpp).
void ChooseSenses(Flusher& f);
void ChooseInitialConjunctions(Flusher& f);
void HanBeforeNa(Flusher& f);
void RubyIdiomVerb(Flusher& f);
// The index in nouns of the noun that target, the Korean of a verb stem of a noun, a particle
// and a light verb, starts with: the noun, a form of one of particles (가 of (이)가) and a space
// before a conjugating verb (声が 목소리가 들&AA@); -1 when it does not split so (口に 말&AV@,
// 気に 신경 &AF@, 損を 손해를 &AY@ after 손).
int IdiomNoun(std::string_view target, std::span<const std::string_view> nouns,
              std::span<const std::string_view> particles);

}  // namespace hdTrans::word
