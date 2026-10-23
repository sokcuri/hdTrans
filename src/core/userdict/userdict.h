#pragma once
// The plain dictionary format, compiled into plain-dictionary entries.
//
// One entry per line, UTF-8 (a leading BOM is allowed), tab-separated:
//
//   JP <TAB> KR [<TAB> POS[:class] [<TAB> OPTIONS]]
//
// A line whose first non-blank characters are "//" or "#" is a comment, a field that
// starts with "//" ends the line, and blank lines are ignored. POS defaults to n:
//
//   n               noun (N110)
//   phrase          fixed expression (A9D0)
//   name, surname   given or full name, family name (N130 QPNVHn, Wb or Wa)
//   intj, lead      interjection (E000)
//   hon             honorific suffix (N110, Tu or with space Ha); KR may be empty
//   v, v1, v5       verb in dictionary form (M42k, the kind k from the ending; v1 and
//                   v5 make a る-verb ichidan or godan); a する verb in 하다 or 되다
//                   adds its noun (N110 Hd). A verb in ない is held as an adjective.
//   adj             i-adjective in dictionary form (H7N0)
//   na              adjectival noun (G8U0); KR "adjective/adverb" adds JP+に (E300)
//   adv, ono        adverb (E300) and its する verb (M429); KR "adverb/verb lemma"
//   end, pat        a sentence ending (applying only to sentence-final predicates)
//                   or a grammar pattern, given by a model phrase of 書く/쓰다,
//                   高い/높다, 静か/조용하다 or 学生/학생: 書いたじゃん<TAB>썼잖아<TAB>end
//
// The class (after a colon, for v, v1, v5, adj and na) is one of kClasses. OPTIONS
// are space-separated key=value items or bare flags:
//
//   prio=N          order among entries with equal key length (higher first)
//   final=v|c|l     josa class of KR: vowel, consonant, ㄹ
//   attr=CODES      semantic codes instead of the POS default
//   gattr=CODES     grammar codes instead of the POS default
//   force           ahead of base candidates even inside a longer base key
//   hidden          parsed but not loaded
//   space, attach   hon rows: spacing before the honorific (default attach)
//   register=R      end rows: only for predicates of register R (plain, casual,
//                   polite, formal, archaic, or 해라, 해, 해요, 합쇼, 하오)
//   stative         end rows: only for stative predicates; put it before the row of
//                   the same ending for the other predicates
//   neg=LEMMA       v and adj rows: the Korean verb that negates this one
//   exclaim         intj rows: an exclamation, which the sound stage does not read as
//                   a sound before a question mark (よかった 다행이다, よかった？ 좋았어?)
//   predicate       exclaim rows: nor after a noun topic and a comma, where it is the
//                   predicate (あの頃は、よかった 좋았다)
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/dict.h"
#include "core/dict/user_text.h"
#include "core/userdict/conjugating.h"

namespace hdTrans::userdict {

// What compiling needs besides the text.
struct Context {
    dict::CodeTable* codes = nullptr;  // receives the codes of the entries
    Families* families = nullptr;      // receives the families made for new stems
    // Resolves る-verbs whose class the lemma does not show, by the verb senses of
    // the stem; null to resolve none.
    const dict::Dict* base = nullptr;
};

// A line that does not compile.
struct Error {
    int line = 0;  // 1-based
    std::string message;
};

struct Compiled {
    // The entries of the lines that compile, in line order.
    std::vector<dict::UserEntry> entries;
    std::vector<Error> errors;
};

// The validated source row, before conjugation expands it into records.
struct SourceEntry {
    std::string ja, ko, pos, options;
};

// Compiles text, the file-th source in load order: entries carry file, their line
// and their order within the line. sourceEntries receives valid noncomment rows.
Compiled Parse(std::string_view text, int file, Context& ctx, std::vector<SourceEntry>* sourceEntries = nullptr);

// The register values of end rows: 1 plain (해라체), 2 casual (해체), 3 polite (해요체),
// 4 formal (합쇼체), 5 archaic (하오체).
enum class Register : uint8_t { Any = 0, Plain, Casual, Polite, Formal, Archaic };

// The entries of a table of kana spellings: KANA <TAB> KEY <TAB> CLASS lines ("//"
// comments), where KEY is a word of base written in kanji, KANA its reading and
// CLASS the class digit of the senses read, empty for every class but 4 and 7. Each
// line gives KANA the first sense of KEY in base of each class read, marked kana,
// with file -2. Lines whose KEY base lacks give nothing.
std::vector<dict::UserEntry> KanaSpellings(std::string_view text, const dict::Dict& base, dict::CodeTable& codes);

}  // namespace hdTrans::userdict
