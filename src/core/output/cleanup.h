#pragma once
// The cleanup of the text path: the sentences and chunks of a line joined, alternating
// particles that compose left open resolved, leading commas and the spaces the engine
// writes before commas and closing brackets dropped.
#include <string>
#include <string_view>
#include <vector>

#include "core/output/pieces.h"

namespace hdTrans::output {

class TextBefore;

// The output pieces of one sentence and the chunk of the line it belongs to.
struct Sentence {
    int chunk = 0;
    Pieces pieces;
};

// The cleaned-up text of a line from its sentences in order: the sentences of a chunk
// joined (AppendSentence), the chunks joined by one space (AppendChunk), then
// CleanupText and FixSpacing.
Pieces Cleanup(const std::vector<Sentence>& sentences);

// Appends the output of a sentence to that of its chunk. A sentence only ends in a
// space added after its final punctuation; that space gives way to a space that
// starts the next sentence, and after ! or ? to a quoting (이)라 or (으)로 that starts
// it (돈!(으)로).
void AppendSentence(Pieces& result, const Pieces& sentence);

// Appends the output of a chunk to that of the line: the spaces between them become
// one space.
void AppendChunk(Pieces& out, Pieces&& got);

// Finishes the output of a whole text; span pieces are left as they are.
//   - An alternating particle left unresolved because it starts a sentence ("(은)는"),
//     and the (와), (어) and (아) of dictionary targets ((와) 같이, (어) 주다), take the
//     form for the ParticleClass of the text written before them, a word span read by
//     its class (SpanFinal) and other spans passed over; with nothing that decides,
//     the vowel form.
//   - A comma preceded in its line by nothing but spaces, tag spans and opening
//     brackets is dropped with the spaces after it.
Pieces CleanupText(Pieces text);

// In each text piece, in this order: ∼ written as ~, the spaces before a comma
// dropped, the spaces between ? or ! and a closing bracket (『무엇?』) and between a
// syllable and ―― dropped, full-width digits written in ASCII.
void FixSpacing(Pieces& pieces);

// The target of a word span: its text when it ends in a syllable, otherwise the
// syllable 가, 갈 or 각 of its SpanFinal. Views text or a static string.
std::string_view StandIn(std::string_view text);

// The particle class ('v' vowel, 'c' consonant, 'l' ㄹ) of a word span: a jamo at its
// end by its kind (ㄹ is 'l'), otherwise its ParticleClass, and 'c' for foreign text.
char SpanFinal(std::string_view text);

// The syllable 가, 갈 or 각 of a particle class.
std::string_view ClassSyllable(char final);

// The JosaTable row whose pattern starts text, or -1.
int JosaRow(std::string_view text);

// An opening bracket or quote: ( [ { ‘ “ 〔 〈 《 「 『 【 （ ［ ｛.
bool IsOpening(char32_t r);

// Drops, in place, the parenthesised groups of syllables attached to a main reading
// in a dictionary target, keeping the main reading: 하면(자) -> 하면, (분)편 -> 편. A
// group standing alone, as in (웃음), and alternating particles such as (은)는 are
// kept.
void RemoveGlosses(std::string& text);

// Replaces, in place, each run of ASCII spaces by one space.
void SqueezeSpaces(std::string& text);

namespace internal {

void ResolveParticles(Pieces& pieces);
void DropLeadingCommas(Pieces& pieces);

// Whether text ends in a syllable, or in one and a space.
bool AfterSyllable(const TextBefore& text);

void NarrowTilde(std::string& text);
void DropSpaceBeforeComma(std::string& text);
void DropSpaceBeforeClose(std::string& text);
void NarrowWideDigits(std::string& text);

}  // namespace internal

}  // namespace hdTrans::output
