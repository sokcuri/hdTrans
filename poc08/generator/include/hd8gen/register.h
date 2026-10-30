#pragma once
// R: the register model. Decides the speech level of each utterance from its cues, the speaker's profile in
// scope (scene, and earlier scenes of the same document with weight halving at each scene boundary), and the
// default. Then marks addressee -시- on the V nodes of polite utterances.
#include <memory>
#include <string>
#include <vector>

#include "hd8gen/kir.h"

namespace hd8gen {

struct RegisterTables;
std::shared_ptr<const RegisterTables> LoadRegisterTables(const std::string& dir, std::string& err);

// One line of a document as R sees it. Until S and L exist, the source text stands in for their cue summary.
struct RegLine {
    std::string ja;       // source line
    std::string speaker;  // speaker label, "" when unknown
    uint32_t scene = 0;   // scene id (S); a change of id is a scene boundary
    bool dialogue = false;
};

struct RegDecision {
    Level level = Level::none;
    Persona persona = Persona::none;
    bool narration = false;   // the line is narration (or ends in narration after a quote)
    std::string key;          // cue key that decided it, for --why
    bool byProfile = false;   // the speaker profile changed the level
    bool firstPerson = false; // the source names the speaker (私, 俺 …): the subject is not the addressee
};

struct RegOptions {
    double decay = 0.0;       // weight of earlier scenes per boundary (0 = this scene only)
    double threshold = 0.6;   // polite share above which a profile counts as polite
    bool profiles = true;
};

// Decides every line of one document (lines in order). Each decision depends only on lines in its scope.
std::vector<RegDecision> DecideLevels(const RegisterTables&, const std::vector<RegLine>&, const RegOptions& = {});

// The cue key of one line alone (for tests and --why).
std::string CueKey(const RegisterTables&, const std::string& ja);

// voices.tsv: whether a Japanese word is an honorific name suffix (さん, 様), and the voice of a line ("" none).
bool IsHonorificSuffix(const RegisterTables&, const std::string& word);
Persona VoiceOf(const RegisterTables&, const std::string& ja);

// Addressee honorific: V nodes of a polite utterance whose subject is the addressee get hon. Without clause
// arguments (no L yet) the subject is approximated: a question or request with no explicit subject or topic
// in its clause, a verb (not an adjective, 이다 or 있다), and no first person in the source.
std::vector<Reg> AssignRegister(const RegisterTables&, const std::vector<Node>& kir, const RegDecision& d,
                                bool firstPersonInSource, const struct Tables* gen = nullptr);

}  // namespace hd8gen
