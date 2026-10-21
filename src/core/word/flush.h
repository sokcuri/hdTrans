#pragma once
// The end-of-sentence passes of the word stage. They run over the item chain of one
// sentence (Worker::chain) in a fixed order and turn the analyzed items into the
// resolved chain: the replacements of katakana runs and noun phrases, the selection
// of each item's candidate and ending by the rules, and the passes that fix class-3
// words, negatives and terminal forms.
#include <cstddef>
#include <string_view>

#include "core/output/families.h"
#include "core/rules/rules.h"
#include "core/word/worker.h"

namespace hdTrans::word {

// What the passes read besides the chain: the rule program of the selection and the
// marker families of the output lookups. The passes add the families they make (the
// conjugating targets of the text path), so the families are shared with the output stage.
struct Flusher {
    Worker& w;
    const rules::Program& program;
    output::Families& families;
};

// Runs the sentence passes over w.chain (the chain of one sentence after the token loop).
void Flush(Flusher& f);

// Selection (selectors.cpp).
void CleanupSelectors(Flusher& f);
void ApplyKj(Item* it);
bool IsStative(Flusher& f, Item* it);

// Terminal form (terminal.cpp).
void PreferTerminal(Flusher& f);

// Passes (passes.cpp).
void DrainNmTail(Flusher& f);
void ReinsertPhrases(Flusher& f);
void CleanupClass3(Flusher& f);
void ApplyNegatives(Flusher& f);

// Helpers of the text-path passes. TakeTarget (sense.cpp) selects the candidate of class
// with target, or writes target over the selected one. SelectLemma (grammar.cpp) selects
// the first entry whose dictionary form is lemma (or starts with prefix) and reports
// whether there was one. Thing and Person classify dictionary attributes.
void TakeTarget(Item* it, char cls, std::string_view target, Selection source = {Provenance::Rewrite});
bool SelectLemma(Flusher& f, Item* it, std::string_view lemma);
bool SelectLemmaPrefix(Flusher& f, Item* it, std::string_view prefix);
bool Thing(std::string_view attr);
// The dictionary form of entry slot of it (an empty string for no entry).
std::string ItemLemma(Flusher& f, const Item* it, int slot);
bool Person(std::string_view attr);

// Text-path grammar (grammar.cpp).
void PassiveRecords(Flusher& f);
void GoNotSay(Flusher& f);
void PositiveIke(Flusher& f);
void CopulaDeAru(Flusher& f);
void NegativeCopula(Flusher& f);
void SubjectBeforeGa(Flusher& f);
void HumbleFirstPerson(Flusher& f);
void ChanAfterKatakana(Flusher& f);
void InitialSoundRule(Flusher& f);
void SubjectNo(Flusher& f);

// Text-path て forms (connective.cpp). UtteranceEnd reports an item followed by the end
// of the utterance (closing punctuation, a pause or the end of the chunk).
void TeRequest(Flusher& f);
void TeThanks(Flusher& f);
// The て or 連用 form of a verb with an object before a comma reads 고 (電話を切って、 전화를 끊고,).
void TeObjectComma(Flusher& f);
void VerseTe(Flusher& f);
void ResultState(Flusher& f);
void NotYet(Flusher& f);
void SeeAnd(Flusher& f);
void PastSequence(Flusher& f);
void QuoteTo(Flusher& f);
void NodaTo(Flusher& f);
void TerminalBeforeQuote(Flusher& f);
void HodoDegree(Flusher& f);
void QuestionCopula(Flusher& f);
void GenitiveBetweenNames(Flusher& f);
void LongVowelAfterKatakana(Flusher& f);
void ReturnTo(Flusher& f);
void KaraSource(Flusher& f);
void NiAfterAdverb(Flusher& f);
void NoAfterNa(Flusher& f);
void ShortPossessive(Flusher& f);
void DeScene(Flusher& f);
void YounaBeforeModifier(Flusher& f);
void ParticleInNext(Flusher& f);
void JiBeforeJiman(Flusher& f);
// Two or more のか clauses side by side (良かったのか、悪かったのか) read alike, in 지.
void ParallelNoka(Flusher& f);
// An offer that ends a quoted sentence, or any sentence with a final particle (教えてやるよ,
// 手伝ってあげます), reads as a promise (가르쳐 줄게, 도와 드릴게요), or 주마 in old or
// haughty speech.
void OfferPromise(Flusher& f);
void ComitativeTo(Flusher& f);
void NounTo(Flusher& f);

// Text-path terminal and negative passes (grammar.cpp).
void ExclaimedAdjective(Flusher& f);
void VerbNegative(Flusher& f);
void EvenBeforePassive(Flusher& f);
void TeSequence(Flusher& f);
bool UtteranceEnd(const Worker& w, const Item* it);

// Text-path adjectival neighbours (neighbours.cpp).
void AdjectiveNeighbours(Flusher& f);
void GeotBeforeMitai(Flusher& f);

// Whether the item's surface is one of set.
template <std::size_t N>
bool IsOneOf(const Item* it, const std::string_view (&set)[N]) {
    return it != nullptr && InSet(set, it->group->surface);
}

// Whether the item is word, a key or the dictionary form of the verb or adjective the item is a
// stem of (sense.cpp). IsAny is IsWord over a list of words.
bool IsWord(const Item* it, std::string_view word);
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

}  // namespace hdTrans::word
