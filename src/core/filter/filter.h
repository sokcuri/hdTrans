#pragma once
// The pre-filter and the post-filter: ordered text replacements over the Japanese input
// of a line before it is translated and over the Korean output after it, read from
// filter files in the row format of Ehnd (docs/ehnd-compat.md).
//
// A stage runs its rows in order of layer, then of load order; each row sees the text,
// in UTF-16 code units, as the rows before it left it. Literal rows are found with one
// matcher over all their sources; regex rows are PCRE2 patterns, run only when the text
// holds the row's _AUX text and the code units PCRE2 finds every match needs. Toggles
// live per line: Pre may set them for Post, and the next line starts again from the
// declared states.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/dict.h"
#include "core/filter/matcher.h"

struct pcre2_real_code_16;

namespace hdTrans::filter {

enum class Stage : uint8_t { Pre, Post };

// Each message includes its newline; the view lasts until write returns.
struct LogSink {
    void* context = nullptr;
    void (*write)(void* context, std::string_view line) = nullptr;
};

// A file of a filter set; texts are UTF-8.
struct Source {
    enum class Kind : uint8_t { PreFilter, PostFilter, SkipLayer, UserDict };
    Kind kind = Kind::PreFilter;
    std::string name;
    std::string text;
    // A file of a dictionary folder, where a line starting with "#" is a comment too.
    bool native = false;
};

// A line or a file that does not load.
struct Error {
    std::string file;
    int line = 0;  // 1-based; 0 for the whole file
    std::string message;
};

class Filters {
public:
    Filters() = default;
    ~Filters();
    Filters(const Filters&) = delete;
    Filters& operator=(const Filters&) = delete;

    // The toggle states of one line.
    using Toggles = std::vector<char>;
    Toggles Start() const { return defaults_; }

    // Rewrites line (without "\n") with the rows of stage; an empty line stays empty.
    void Apply(Stage stage, std::string& line, Toggles& toggles, LogSink log = {}) const;

    // The user dictionary records of the UserDict files; null for none.
    const dict::Dict* User() const { return user_.Len() > 0 ? &user_ : nullptr; }

    // Rows per stage, for reports.
    std::size_t Rows(Stage stage) const { return (stage == Stage::Pre ? pre_ : post_).rules.size(); }

private:
    friend std::shared_ptr<const Filters> Build(const std::vector<Source>& sources, std::vector<Error>& errors);

    enum class Kind : uint8_t { Plain, Guard, Guarded };

    struct Rule {
        std::u16string src, dest;
        int layer = 0;
        std::string file;
        int line = 0;  // 0-based physical source row
        int regexFlag = 0;
        int32_t regex = -1;    // the index in regexes; -1 for a literal row
        // Matcher patterns the text must hold, -1 for none: of a literal row its src; of a
        // regex row the code units every match starts with and holds.
        int32_t units[2] = {-1, -1};
        int32_t aux = -1;      // the matcher pattern of the _AUX text; -1 for none
        int32_t gate = -1;     // the toggle that must be on (_CONN); -1 for none
        int32_t sets = -1;     // the toggle a firing row sets (_ON) or clears (_OFF); -1 for none
        bool on = false;
        bool check = false;    // DEST _MATCH_CHECK: the row only tests
        bool blockEnd = false; // a _skip_sub row whose next row is not one
        Kind kind = Kind::Plain;
    };

    // A SkipLayer condition: the rows of layer run only when cond matches.
    struct Condition {
        int layer = 0;
        std::string label;
        int32_t regex = -1;
        int32_t units[2] = {-1, -1};
    };

    struct Program {
        std::vector<Rule> rules;
        std::vector<Condition> conditions;  // ordered by layer
        Matcher matcher;
    };

    void Run(Stage stage, const Program& p, std::u16string& text, Toggles& toggles, LogSink log) const;

    Program pre_, post_;
    std::vector<pcre2_real_code_16*> regexes_;
    uint32_t pairs_ = 1;  // the most offset pairs a match of the regexes needs
    Toggles defaults_;
    std::vector<std::string> toggleNames_;
    dict::CodeTable codes_;
    dict::Dict user_;
};

// The filters of sources, which are in load order; null for no sources. Lines that do
// not load are skipped and listed in errors.
std::shared_ptr<const Filters> Build(const std::vector<Source>& sources, std::vector<Error>& errors);

// Puts back into filtered, the line source after the pre-filter, the spaces the pre-filter
// removed between a digit or a Latin letter and a word in kanji, katakana or Latin letters
// after it (ステージ3 ボス戦 → ステージ3ボス戦 → ステージ3 ボス戦). Ehnd dictionaries remove them
// for a translator that ignores spaces; hdTrans reads such a space as the source's spacing
// (3 보스전, 1 레벨). The two lines are aligned on their common start and end and, between
// them, on their longest common subsequence of characters, the full-width forms of ASCII
// matching their ASCII forms (１２　月 → 12月 → 12　月); a space comes back only where both
// characters around it are kept and adjoin. Between a common start and end that leave more
// than 2^22 character pairs, the spaces stay as the filter left them.
void KeepSpacesAfterNumbers(std::string_view source, std::string& filtered);

// UTF-8 to and from UTF-16; invalid bytes and lone surrogates read as U+FFFD.
std::u16string Utf16(std::string_view s);
std::string Utf8(std::u16string_view s);

// Appends to out the replacement fmt makes of a match of subject; ovector holds pairs
// offset pairs, the first the match. As in Perl-style replacement strings: $n and ${n}
// are groups (an unmatched or missing group is empty), $& the match, $` the text from the
// end of the previous match (previous) to the match, $' the rest of the subject, $$ a
// "$", and any other "$" is kept; after a backslash, 1 to 9 is a group, a e f n r t v the control
// character, xHH or x{H...} and cX a character, 0 an octal character of up to four
// digits, l u L U E nothing, and any other character itself.
void AppendReplacement(std::u16string& out, std::u16string_view fmt, std::u16string_view subject,
                       const std::size_t* ovector, uint32_t pairs, std::size_t previous);

}  // namespace hdTrans::filter
