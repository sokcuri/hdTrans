#pragma once
// The sense table: the target a word takes when a trigger word stands near it.
//
// One row per line, tab-separated:
//
//   KEY <TAB> KR [<TAB> TRIGGERS <TAB> SIDE [<TAB> POS[:class]]]
//
// KEY and KR are a word and its Korean as in the plain format; a KEY may be a phrase
// that the dictionary holds as one word (気が強い). TRIGGERS is a comma-separated
// list of words, verbs and adjectives in dictionary form; SIDE is where they stand
// from KEY: before or after it in its clause, or anywhere in its sentence. A row
// with no triggers (and no SIDE) always applies. POS (n, v, v1, v5, adj or na)
// defaults to adj for a KEY ending in い, v for one ending in another dictionary-form
// kana, else n. A noun KR in 하다 (大事 중요하다) is the root of a 하다 adjective: the
// noun takes the root, and 한 before な. The first row of a key whose trigger is found
// wins. POS p sets a particle target without conjugation. A trigger may instead be
// sem:CODE or gram:CODE, optionally followed by @PARTICLE (before only) to test
// the noun immediately before that particle. /gram:CODE or /sem:CODE then tests
// the first following predicate, within eight items and without a clause boundary.
// Comments and blank lines are as in the plain format.
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/dict/user_text.h"
#include "core/userdict/conjugating.h"
#include "core/userdict/userdict.h"

namespace hdTrans::userdict {

// Where the triggers of a sense stand from its key.
enum class Side : uint8_t {
    Before,    // within a few words before it, in its clause
    After,     // within a few words after it, in its clause
    Sentence,  // anywhere in its sentence
};

struct Sense {
    std::string key;     // the key of the word (the stem of a conjugating word)
    char classDigit = 0; // the class digit (part code byte 1) of the candidates it applies to
    char kind = 0;       // a verb's conjugation kind (part code byte 3) from its dictionary form, 0 for none
    std::string target;
    std::vector<std::string> triggers;  // with none the sense always applies
    Side side = Side::Before;
    // A noun sense whose target is the root of a 하다 adjective (KR 중요하다, target
    // 중요): before な it takes 한.
    bool adjective = false;
    int row = 0;  // 1-based source line in the sense table
};

// The senses of each key, in file order.
using SenseTable = std::unordered_map<std::string, std::vector<Sense>, dict::StringHash, std::equal_to<>>;

struct CompiledSenses {
    SenseTable table;
    std::vector<Error> errors;
};

CompiledSenses ParseSenses(std::string_view text, Families& families);

}  // namespace hdTrans::userdict
