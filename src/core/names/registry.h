#pragma once
// The name registry of a document: the evidence that surfaces are person names, read
// line by line, and the Hangul renderings of the accepted names. A registry reads the
// name records and the base records of a snapshot (null for none) and the readings
// tables loaded into it.
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/text/kana.h"

namespace hdTrans::names {

namespace internal {
struct Run;
}

// A registered surface in a line, as byte offsets. noun is set where a guard expects
// the common word instead of the name.
struct Occurrence {
    std::size_t start = 0, end = 0;
    std::string surface;
    bool noun = false;
};

// The evidence of a surface in the base dictionary.
struct DictFlags {
    int records = 0;
    bool person = false, surname = false, given = false, city = false;
    bool common = false;  // a record that is neither a name nor a place
    // A common record that is a word of its own: no reading of the kanji standing alone (gram
    // On: 牧 목, 周 주) and no verb or adjective stem, which a name never conjugates as.
    bool word = false;
    bool role = false;    // a person, profession or animal noun with no name record
    // A base record of a creature, fruit or food noun or of an adverb: what speech addresses
    // with an honorific (ひよこさん, いちごちゃん) or what stands before ちゃんと (いつか).
    bool thing = false;
    // A record of a pronoun (あなた, わし), and one of a person noun that is none (提督).
    bool pronoun = false, human = false;
    bool place = false;  // a record of a place noun (VPl) that is no name (うち 집)
    // The name-layer person records: a surname or given name, and the first Hangul
    // target among them.
    bool layerSurname = false, layerGiven = false;
    std::string layerName;
};

// An accepted surface.
struct Name {
    double score = 0;
    bool label = false;     // spoke as a speaker
    bool person = false;    // label, honorific, gloss or full-name evidence
    bool surname = false;   // left part of a full name
    bool given = false;     // right part of a full name found by GivenSplit
    bool hasParts = false;  // a full name; parts holds its parts
    // A hiragana run seen before honorifics without the evidence to stand for the person
    // alone: a name only where an honorific follows it.
    bool honorificOnly = false;
    std::vector<std::string> parts;
};

class Registry {
public:
    // The registry of snap (null for none): the Fan preset writes the names, and ちゃん
    // is written 짱.
    explicit Registry(const dict::Snapshot* snap);

    // Records the Hangul rendering kr of the name t of a plain dictionary (kr must be
    // Hangul): t is written kr as a part of a full name.
    void SetName(std::string_view t, std::string_view kr);
    // Adds a readings table: JP <TAB> reading (kana or Hangul) per line. Returns false
    // when a line is malformed; the other lines are kept.
    bool LoadReadings(std::string_view text);
    // Reads the evidence of one line.
    struct Evidence {
        uint64_t id = 0;
        std::array<std::vector<std::string>, 11> counts;
        std::vector<std::array<std::string, 3>> joined;
        std::vector<std::string> readings, actors;
    };
    void Add(std::string_view line, Evidence* evidence = nullptr);
    // Removes recorded lines in insertion order, without scanning their text again.
    void Forget(const Evidence& evidence);
    // Shares only immutable dictionary facts with subsequent document copies.
    void CacheDictionaryFacts();

    // The accepted surfaces, in byte order.
    std::vector<std::string> Names();
    // The accepted surfaces that are names only before an honorific (Name::honorificOnly).
    std::vector<std::string> HonorificOnlyNames();
    // Whether t is an accepted name or stands where a name stands in the lines added.
    bool Named(std::string_view t);
    // The surfaces the lines show to be names: the accepted names and the surfaces Named reports
    // beyond them, in byte order.
    std::vector<std::string> NamedSurfaces();
    // The katakana runs of the lines added that are mostly not katakana (ProseRuns), with
    // their counts.
    const std::map<std::string, int>& ProseKatakana() const;
    // Identifies the names of the registry: two registries on one snapshot with equal
    // keys translate every line alike.
    std::string Key();
    // The plain-dictionary lines of the accepted names with a rendering: JP, KR and POS,
    // then ちゃん as chan.
    std::string Dict();
    // The Korean of an accepted surface, or "".
    std::string Render(std::string_view t);
    // The registered names of line (ruby markup removed), leftmost-longest, that fill
    // their kanji or katakana run.
    std::vector<Occurrence> Occurrences(std::string_view line);
    // line without the readings after kanji (FindReading) that are the kanji's reading
    // (BaseReading): a hiragana one, and a katakana one whose hiragana spelling is a word
    // (生（ナマ）). A katakana word written over the kanji (炎（ブレス）) and a pronoun over a
    // person noun (提督(あなた)) stay as glosses.
    std::string DropReadings(std::string_view line);

    text::Preset preset = text::Preset::Fan;
    // The rendering of ちゃん written to Dict; "" leaves ちゃん to the engine.
    std::string chan = "짱";

private:
    // Labels and the evidence of a single run (cues.cpp).
    std::string Label(std::string_view line, bool& script);
    DictFlags Flags(std::string_view s);
    bool Gikun(std::string_view base, std::string_view reading);
    std::set<std::string_view> Words(std::string_view s);
    unsigned WordClasses(std::string_view s);
    bool SameWord(std::string_view base, std::string_view reading, std::string_view after);
    bool SameKind(std::string_view s, std::string_view t);
    bool NamedWithSound(std::string_view base, std::string_view reading);
    bool BaseReading(std::string_view base, std::string_view reading, std::string_view after, bool bracket, bool styled);
    std::string HonorificRun(std::string_view line, const internal::Run& run);
    std::vector<std::string> KanaHonorificNames(std::string_view line);
    std::string_view KanaHonorificAt(std::string_view s);
    bool PredicateForm(std::string_view run);
    bool LayerReading(std::string_view t);
    bool Addressed(std::string_view line, std::string_view t);
    bool Guarded(std::string_view line, std::size_t a, std::size_t b);

    // The katakana runs that end in a katakana honorific (サーラサン) while the prose lines
    // write their head with two or more other endings that are no honorific (サーライチ,
    // サーラニ): one name, a numbered or derived one, not the head and an honorific.
    const std::set<std::string>& SiblingNames();
    bool NicknameCompound(std::string_view t);
    bool TranslatedWord(std::string_view w);
    void FindSiblingNames();

    // Scores, splits and the accepted names (registry.cpp).
    const std::map<std::string, Name>& Accepted();
    std::pair<std::string, std::string> GivenSplit(std::string_view t);
    bool Crossed(std::string_view t, std::size_t i);
    bool Split(std::string_view t, std::vector<std::string>& parts);
    void SplitReading(const std::string& x, const std::string& y, const std::string& kana);

    // Rendering (render.cpp).
    std::string RenderName(std::string_view t, const Name& n);
    // whole: t is the whole name, which a katakana name reads as its name-layer person
    // reading before its transliteration.
    std::string RenderPart(std::string_view t, bool whole = false);
    std::string PersonName(std::string_view t);
    // The word of x in the name-layer person reading of an accepted chain name
    // (ミレイユ・ベルクロア 미레유 베르크로아) that has x as a part and a word per part,
    // so that x reads alike in the chain and alone; "" for none.
    std::string ChainPart(std::string_view x);
    std::string PrefixForm(std::string_view x);

    void SetReading(const std::string& key, const std::string& value);
    void SetActor(const std::string& key, const std::string& value);
    struct Sources {
        using Values = std::map<std::string, std::deque<std::pair<uint64_t, std::string>>>;
        Values readings, actors;
        uint64_t next = 0;
    };
    std::shared_ptr<Sources> sources_;
    Evidence* recording_ = nullptr;
    const dict::Snapshot* snap_;
    struct Tables {
        std::map<std::string, std::string> readings;  // surface to reading or Hangul
        std::map<std::string, std::string> fixed;     // plain-dictionary name renderings
    };
    std::shared_ptr<Tables> tables_;
    std::map<std::string, int> labels_;                 // script-format labels
    std::map<std::string, int> prefixes_;               // line-start labels of plain-text forms
    std::string nameLine_;                              // the last line when it is a name alone
    std::map<std::string, std::string> actors_;         // script actor variables
    std::map<std::string, int> hon_;                    // surfaces before an honorific
    std::map<std::string, int> kanaHon_;                // hiragana name-layer persons before an honorific
    std::map<std::string, int> gloss_;                  // surfaces with a kana gloss or ruby
    std::map<std::string, std::string> reading_;        // first gloss or ruby reading
    std::map<std::string, int> runs_;                   // kanji runs
    std::map<std::string, int> ended_;                  // kanji runs before a particle, an honorific or no hiragana
    std::map<std::string, int> kana_;                   // katakana runs
    std::map<std::array<std::string, 3>, int> joined_;  // kanji and katakana pairs
    std::map<std::string, int> chains_;                 // katakana runs joined by ・ or ＝
    std::map<std::string, int> prose_;                  // katakana runs of lines mostly not katakana
    std::map<std::string, int> agents_;                 // katakana runs that do a favour (AgentCue)
    struct FlagCache {
        std::map<std::string, DictFlags> values;
        std::size_t bytes = 0;
    };
    std::shared_ptr<FlagCache> sharedFlags_;
    std::map<std::string, DictFlags> flags_;
    std::map<std::string, Name> scores_;                // accepted names, valid while scoresValid_
    std::set<std::string> siblings_;                    // SiblingNames, valid while scoresValid_
    std::map<std::string, std::string> numbered_;       // numbered names to their name, valid while scoresValid_
    bool scoresValid_ = false;
};

// The labels of the speaker cues of text: a name at a line start followed by 「, です,
// でち, っす or a call to action, in order.
std::vector<std::string> SpeakerCues(std::string_view text);

// The katakana runs of a line with hiragana and less katakana than other Japanese
// characters: the words a document writes in katakana in its prose, which a line of
// katakana does not read as katakana speech. Empty for other lines.
std::vector<std::string> ProseRuns(std::string_view line);

// The text of line without ruby markup, keeping the base text.
std::string StripRuby(std::string_view line);

}  // namespace hdTrans::names
