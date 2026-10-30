#pragma once
// The Korean morpheme IR as the generator reads it (IR.md §6), with strings in place of interned ids.
// Field names and enum values follow IR.md. Fields marked "additive" are proposed additions to IR.md.
#include <cstdint>
#include <string>
#include <vector>

namespace hd8gen {

enum class Kind : uint8_t { N, P, V, Adv, Det, Intj, Punct, Raw };
enum class Level : uint8_t { none, haera, hae, haeyo, hapsyo, hao, hage };
enum class Persona : uint8_t { none, rough, oldMale, lady, formal, pompous, archaic, grand };
enum class Tense : uint8_t { nonpast, past };
enum class Aspect : uint8_t { simple, prog, result, compl_, dirCome, dirGo, try_, give };
enum class Mood : uint8_t { decl, q, imp, prop, excl };
// final | conn | adn | nom (IR.md); quote and stem are additive: quote = indirect quotation ending
// (-다고, -냐고), stem = bare stem in a compound (가-).
enum class CType : uint8_t { final_, conn, adn, nom, quote, stem };
enum class Neg : uint8_t { none, an, jiAnh, mot, jiMot, jiMa };
enum class Punct : uint8_t { none, comma, pause, end, q, excl, qopen, qclose, ellipsis, dash, mark };

// sem bits (IR.md: person place time counter num sino native animate)
enum Sem : uint32_t { kPerson = 1, kPlace = 2, kTime = 4, kCounter = 8, kNum = 16, kSino = 32, kNative = 64, kAnimate = 128 };

struct Span { uint32_t begin = 0, end = 0; };

struct Node {
    uint32_t id = 0, origin = 0;
    Kind kind = Kind::N;
    std::string lemma;    // 가다, 학생, 듯; P: the particle when kase = lex (도, 만, (이)나); Raw: carried text
    std::string kcls;     // V: REG HA D B BWA S H REU REO U EU L COP EXIST NC; "" = from ko/stems tables
    std::string vpos;     // additive. V: V A X C N; "" = from ko/stems and ko/lemmas
    uint32_t sem = 0;
    Tense tense = Tense::nonpast;
    Aspect aspect = Aspect::simple;
    Mood mood = Mood::decl;
    CType ctype = CType::final_;
    std::string rel;      // conn: seq and cause cond concess … ; adn: nonpast past fut retro; nom: m gi geot
    std::string eomi;     // additive: an explicit ending (eomi id "-는데", or a final family "ne", "ji")
    std::string modal;    // additive: "" | gess (-겠-)
    Neg neg = Neg::none;
    bool aux = false, dep = false, passive = false, causative = false;
    bool glue = false;    // additive: written without a space after the previous word
    uint16_t ja = 0;
    std::string kase;     // P: subj top obj gen dat goal src loc instr com quot voc | lex
    Punct punct = Punct::none;
    std::string jaMark;
    Span src;
    uint16_t clause = 0, utt = 0;
    uint32_t decision = 0;
};

struct Reg {  // R: one per V node
    uint32_t node = 0;
    Level level = Level::none;
    Persona persona = Persona::none;
    bool hon = false;
};

struct UttLevel {  // R: one per utterance
    uint16_t utt = 0;
    Level level = Level::none;
    Persona persona = Persona::none;
    bool narration = false;  // additive copy of Utterance.narration: spoken fusions only outside narration
};

struct OutSpan { uint32_t begin = 0, end = 0, node = 0; };

const char* Name(Level);
const char* Name(Persona);
bool ParseLevel(const std::string&, Level&);
bool ParsePersona(const std::string&, Persona&);

}  // namespace hd8gen
