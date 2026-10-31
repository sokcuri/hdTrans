#pragma once
// The speech level of each line of a document by the rules of spec/register.md (REG-n): the cues of the
// line (register/cues.tsv, levels.tsv), the speaker's lines in the scene, and the default; then the
// addressee -시- on the V nodes of polite utterances.
#include <cstdint>
#include <string>
#include <vector>

#include "core/register/ir.h"
#include "core/register/tables.h"

namespace hdTrans::reg {

// One line of a document.
struct RegLine {
    std::string ja;       // the source line
    std::string speaker;  // the speaker's name, "" when unknown
    uint32_t scene = 0;   // lines of one scene share it
    bool dialogue = false;
};

struct RegDecision {
    Level level = Level::none;
    Persona persona = Persona::none;
    bool narration = false;    // the line is narration, or ends in narration after a quote
    std::string key;           // the cue key that decided it
    std::string spec;          // the rule of spec/register.md that decided it (REG-n)
    bool byProfile = false;    // the speaker's lines in the scene changed the level
    bool firstPerson = false;  // the source names the speaker (私, 俺 …): the subject is not the addressee
    bool keep = false;         // levels.tsv keep: the level the translation already has, haera without one
    struct Quote {             // REG-15: one per quote of a line of several quotes
        Level level = Level::none;
        bool keep = false;
        std::string key, spec;
    };
    std::vector<Quote> quotes;
    bool trail = false;      // the utterance trails off after a て-form (言われて……): no command at its end
    bool trailConj = false;  // the line trails off after a conjunction (けど……): its last predicate keeps its level
    bool holdQuote = false;  // REG-1: the line is a quote and the narrator's outburst; the quote keeps its level
    bool honSonkei = false;  // REG-16: an honorific verb backs -시- on the final predicate
    bool honGive = false;    // REG-16: the addressee gives: -시- on 주다
    bool exclaimNe = false;  // REG-18: the last sentence exclaims with な on a predicate: 해체 -네
    bool declBang = false;   // REG-21: the last sentence declares with a verb cut off or exclaimed (出るっ, 行く！)
    bool giveDecl = false;   // REG-21: the speaker's own will (教えてあげる, してみる, お前に貸してやる): -ㄹ게
};

// Decides every line of one document, lines in order.
std::vector<RegDecision> DecideLevels(const RegisterTables& t, const std::vector<RegLine>& lines);

// The cue key of one line on its own.
std::string CueKey(const RegisterTables& t, const std::string& ja);

// REG-16: -시- on the V nodes of a polite utterance whose subject is the addressee, only where a request or
// command, an honorific verb in a question (d.honSonkei) or the addressee giving (d.honGive, on 주다) backs
// it; never with a subject or topic in the clause, on an adjective, 이다 or 있다, or with a first person.
std::vector<Reg> AssignRegister(const RegisterTables& t, const Tables& gen, const std::vector<Node>& nodes,
                                const RegDecision& d);

}  // namespace hdTrans::reg
