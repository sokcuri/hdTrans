#include "core/word/flush.h"
#include "core/word/cg.h"
#include "core/word/misread.h"
#include "core/word/select.h"

namespace hdTrans::word {

// The sentence passes in order. The text-path passes run only when settings.text is set.
void Flush(Flusher& f) {
    DrainNmTail(f);
    ReinsertPhrases(f);
    if (f.w.settings.text) PassiveRecords(f);
    CleanupSelectors(f);
    CleanupClass3(f);
    ApplyNegatives(f);
    if (f.w.settings.text) {
        Select(f);
        ChooseInitialConjunctions(f);
        GoNotSay(f);
        HanBeforeNa(f);
        AdjectiveNeighbours(f);
        GeotBeforeMitai(f);
        PositiveIke(f);
        CopulaDeAru(f);
        NegativeCopula(f);
        SubjectBeforeGa(f);
        SubjectNo(f);
        VerseTe(f);
        TeRequest(f);
        ResultState(f);
        NotYet(f);
        SeeAnd(f);
        TeSequence(f);
        TeThanks(f);
        TeObjectComma(f);
        PastSequence(f);
        QuoteTo(f);
        NodaTo(f);
        TerminalBeforeQuote(f);
        HodoDegree(f);
        QuestionCopula(f);
        GenitiveBetweenNames(f);
        LongVowelAfterKatakana(f);
        ReturnTo(f);
        KaraSource(f);
        NiAfterAdverb(f);
        NoAfterNa(f);
        ShortPossessive(f);
        DeScene(f);
        YounaBeforeModifier(f);
        ParticleInNext(f);
        JiBeforeJiman(f);
        ParallelNoka(f);
        OfferPromise(f);
        EvenBeforePassive(f);
        ComitativeTo(f);
        NounTo(f);
    }
    if (f.w.settings.terminal) {
        PreferTerminal(f);
        if (f.w.settings.text) ExclaimedAdjective(f);
    }
    if (f.w.settings.text) VerbNegative(f);
    if (f.w.settings.text && f.w.settings.improvements.misreadFilter) FilterMisreads(f);
    if (f.w.settings.text && (f.w.settings.improvements.cgRules || f.w.settings.improvements.cgLearned)) FilterCg(f);
}

}  // namespace hdTrans::word
