#pragma once
// The Korean morpheme nodes a line is rebuilt as, and the speech levels decided for them.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::reg {

enum class Kind : uint8_t { N, P, V, Punct, Raw };
enum class Level : uint8_t { none, haera, hae, haeyo, hapsyo, hao, hage };
enum class Persona : uint8_t { none, rough, oldMale, lady, formal, pompous, archaic, grand };
enum class Tense : uint8_t { nonpast, past };
enum class Mood : uint8_t { decl, q, imp, prop, excl };
// quote: an indirect quotation ending (-다고, -냐고); stem: a bare stem in a compound (가-).
enum class CType : uint8_t { final_, conn, adn, nom, quote, stem };
enum class Neg : uint8_t { none, jiAnh, jiMot, jiMa };
enum class Punct : uint8_t { none, comma, pause, end, q, excl, qopen, qclose, ellipsis, dash, mark };

struct Node {
    uint32_t id = 0;
    Kind kind = Kind::N;
    std::string lemma;  // 가다, 학생; P: the particle when kase is lex; Raw: the text it carries
    std::string kcls;   // V: REG HA D B BWA S H REU REO U EU L COP EXIST NC; "" from ko/stems.tsv
    std::string vpos;   // V: V A X C N; "" from ko/stems.tsv and ko/lemmas.tsv
    Tense tense = Tense::nonpast;
    Mood mood = Mood::decl;
    CType ctype = CType::final_;
    std::string rel;    // conn: seq and cause cond …; adn: nonpast past fut retro; nom: m gi geot
    std::string eomi;   // an ending id ("-는데") or a final family ("ne", "ji")
    std::string modal;  // "" or gess (-겠-)
    Neg neg = Neg::none;
    bool aux = false, dep = false;
    bool glue = false;  // written without a space after the previous word
    std::string kase;   // P: subj top obj gen dat goal src loc instr com quot voc | lex
    Punct punct = Punct::none;
    std::string jaMark;  // Punct: the mark as written
    // While a line is read: the level the word's ending had in the translation.
    Level was = Level::none;
};

// The level of one V node.
struct Reg {
    uint32_t node = 0;
    Level level = Level::none;
    Persona persona = Persona::none;
    bool hon = false;  // addressee -시-
};

// The level of the line's utterance.
struct UttLevel {
    Level level = Level::none;
    Persona persona = Persona::none;
    bool narration = false;  // spoken fusions only outside narration
};

const char* Name(Level level);
const char* Name(Persona persona);
bool ParseLevel(std::string_view s, Level& out);
bool ParsePersona(std::string_view s, Persona& out);

}  // namespace hdTrans::reg
