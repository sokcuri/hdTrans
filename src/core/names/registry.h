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
#include "core/names/list.h"
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
    // The registry of snap (null for none): the FanShort preset writes the names, dropping
    // the long vowels ou, oo and uu of hiragana readings one pair at a time (しどう 시도,
    // ほうおうじ 호오지), and ちゃん is written 짱.
    explicit Registry(const dict::Snapshot* snap);

    // Records the Hangul rendering kr of the name t of a plain dictionary (kr must be
    // Hangul): t is written kr as a part of a full name.
    void SetName(std::string_view t, std::string_view kr);
    // Adds a readings table: JP <TAB> reading (kana or Hangul) per line. Returns false
    // when a line is malformed; the other lines are kept.
    bool LoadReadings(std::string_view text);
    // Takes a listed name: a person (PersonKind) is an accepted name written name.ko, any
    // other kind no accepted name. The lines' evidence is read as without it.
    void AddListed(const Listed& name);
    // Reads the evidence of one line.
    void Add(std::string_view line);

    // The accepted surfaces, in byte order.
    std::vector<std::string> Names();
    // The surname and given name of each full name the document writes as one kanji run,
    // each of one to three kanji with a person row of the name layer (古手梨花: 古手, 梨花; spec LEX-4829):
    // the document means the person where it writes either alone. A part of one kanji counts only when it
    // is no common word.
    std::vector<std::string> FullNameParts();
    // Whether the accepted surface t is a part of a full name or a speaker label (Name::surname, given,
    // label), evidence that names the person wherever the document writes it.
    bool FullNamePart(std::string_view t);
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
    // The accepted names that are not listed, as name-list lines with their Korean: a full
    // name, a surname as family, a name only before an honorific with hon.
    std::vector<Listed> Proposed();
    // The registered names of line (ruby markup removed), leftmost-longest, that fill
    // their kanji or katakana run.
    std::vector<Occurrence> Occurrences(std::string_view line);
    // line without the readings after kanji (FindReading) that are the kanji's reading
    // (BaseReading): a hiragana one, and a katakana one whose hiragana spelling is a word
    // (生（ナマ）). A katakana word written over the kanji (炎（ブレス）) and a pronoun over a
    // person noun (提督(あなた)) stay as glosses. A name's immediate kana reading inside a
    // closed caption label reads that occurrence. cueText receives the kanji-preserving
    // text for name cues in that case; otherwise it is cleared. captionNames receives
    // the explicitly read label names, without adding them to the registry.
    std::string DropReadings(std::string_view line, std::string* cueText = nullptr,
                             std::vector<std::string>* captionNames = nullptr);

    text::Preset preset = text::Preset::FanShort;
    // The rendering of ちゃん written to Dict; "" leaves ちゃん to the engine.
    std::string chan = "짱";
    // The kana in Hangul by preset, FanShort with one long vowel pair at a time; katakana keeps
    // its long vowels (ハイロウ 하이로우), as katakana names are written.
    std::string Written(std::string_view kana) const;

private:
    // Labels and the evidence of a single run (cues.cpp).
    std::string Label(std::string_view line, bool& script);
    DictFlags Flags(std::string_view s);
    bool Gikun(std::string_view base, std::string_view reading);
    // Whether reading is the ordinary reading of the common kanji word base: a kana spelling of it that the
    // kana layer lists (ちから 力, びんしょう 敏捷), or a kana word with the Korean of one of its common
    // senses. Such a ruby is no evidence of a name, and none against one (spec LEX-5175); in a speaker
    // label it still names. An on reading alone is not enough: names read so too (桃花 トウカ, 黒羽 こくう).
    bool OrdinaryReading(std::string_view base, std::string_view reading);
    std::set<std::string_view> Words(std::string_view s);
    unsigned WordClasses(std::string_view s);
    bool SameWord(std::string_view base, std::string_view reading, std::string_view after);
    bool SameKind(std::string_view s, std::string_view t);
    bool NamedWithSound(std::string_view base, std::string_view reading);
    std::string CaptionNameReading(std::string_view base, std::string_view reading);
    bool BaseReading(std::string_view base, std::string_view reading, std::string_view after, bool bracket, bool styled);
    std::string HonorificRun(std::string_view line, const internal::Run& run);
    bool NoNounWord(std::string_view t);
    std::vector<std::string> KanaHonorificNames(std::string_view line);
    // The runs of two to five kana in line that no dictionary has and that stand where a name does:
    // a whole run of hiragana after no kana, kanji or katakana, before が, は, も, の, を, に, と or へ and
    // then no hiragana (みいは自分の); not with a long mark, a small vowel or tsu, or one kana said over
    // (spec LEX-4826).
    // A use of kana where a name stands, and the particle after it.
    struct LooseUse {
        std::string t;
        bool closed = false;
        char32_t particle = 0;
    };
    std::vector<LooseUse> FreeKanaNames(std::string_view line);
    // The uses in line of three or four kana that no dictionary has, before a particle, where the
    // sub-word check of FreeKanaNames does not hold: the run of kana of a name whose halves are words
    // (spec LEX-4828). A use that starts a run and whose particle closes it is closed; one after a case
    // particle inside a run, or one with hiragana after its particle, is open.
    std::vector<LooseUse> LooseKanaUses(std::string_view line);
    // Whether the hiragana t is a form of a verb or an adjective of the dictionaries (いい, 来た).
    bool InflectedRun(std::string_view t);
    // Whether the hiragana t is a word of the dictionary and a particle after it (どこまで: どこ まで,
    // それから), which is no name however often the document writes it (spec LEX-4828).
    bool WordThenParticle(std::string_view t);
    // The kana words of the dictionary in line that stand joined by と or や to a run that may be a name
    // (そらとみゆきは: そら with みゆき, みゆきとそらは), as pairs of the word and that run (spec LEX-4954).
    // weak: the word or the run ends in の or で, which also end a copula (あやとりなの), so the run must be a
    // name the document itself shows.
    struct Coordination {
        std::string word, name;
        bool weak = false;
    };
    std::vector<Coordination> CoordinatedKana(std::string_view line);
    bool WordAloud(std::string_view base, std::string_view reading);
    bool AdjectiveKanji(std::string_view k);
    bool GuessFallback(std::string_view t);
    bool OldFormWord(std::string_view t);
    bool CoveredByWords(std::string_view s);
    bool GarbledByWord(std::string_view s);
    bool LayerFullName(std::string_view s);
    bool HiraganaWord(std::string_view s);
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
    // Whether the katakana run s is a common word of the dictionaries that reads as no person
    // (ダンジョン), or splits into such words of two kana or more (セットスキル, ダンジョンゲート).
    bool CommonWords(std::string_view s);
    void FindSiblingNames();

    // Scores, splits and the accepted names (registry.cpp).
    const std::map<std::string, Name>& Accepted();
    std::pair<std::string, std::string> GivenSplit(std::string_view t);
    // The Korean of a person row of the name layer for the two-kanji common word x that reads x as names
    // read its kanji (internal::GuessedName: 氷室 히무로); "" for none.
    std::string CommonSurnameReading(std::string_view x) const;
    // Whether the name layer has a person row of t.
    bool LayerPerson(std::string_view t) const;
    bool Crossed(std::string_view t, std::size_t i);
    bool Split(std::string_view t, std::vector<std::string>& parts);
    void SplitReading(const std::string& x, const std::string& y, const std::string& kana);

    // Rendering (render.cpp).
    std::string RenderName(std::string_view t, const Name& n);
    // whole: t is the whole name, which a katakana name reads as its name-layer person
    // reading before its transliteration.
    std::string RenderPart(std::string_view t, bool whole = false);
    std::string PersonName(std::string_view t);
    // Whether a guessed reading of the kanji run t is kept out: the document writes it once only, it
    // names a shop, or its last kanji and 様 are a word (皆一様).
    std::string NonPlaceLayerReading(std::string_view t);
    bool GuessedNameBlocked(std::string_view t);
    // The word of x in the name-layer person reading of an accepted chain name
    // (ミレイユ・ベルクロア 미레유 베르크로아) that has x as a part and a word per part,
    // so that x reads alike in the chain and alone; "" for none.
    std::string ChainPart(std::string_view x);
    std::string PrefixForm(std::string_view x);

    void SetReading(const std::string& key, const std::string& value);
    void SetActor(const std::string& key, const std::string& value);
    const dict::Snapshot* snap_;
    std::map<std::string, Listed> listed_;
    // The surname and given name of the full names the document writes with one space between them
    // (小鳥遊　聖), and their Korean in the name layer's row of the name written whole (小鳥遊聖 타카나시
    // 세이): the parts read so wherever the document writes them.
    std::map<std::string, std::string> rowParts_;
    // The run of lines that open with a bracketed or colon field and text after it (BracketField): whether it is
    // a status window (values on two of its lines and on half of them: 【力】　16), its line and value counts,
    // and the labels it counted before it became one, taken back then (spec LEX-5175).
    bool fieldRunValue_ = false;
    int fieldRunLines_ = 0, fieldRunValues_ = 0;
    std::set<std::string> fieldRunNames_;
    bool fieldRunRepeats_ = false;  // a field name said twice in the run, as speakers take turns
    bool fieldRunSpeech_ = false;   // a field of the run that reads as speech (【若葉】うん)
    std::vector<std::string> fieldRunLabels_;
    // Whether line opens with a bracketed or colon field (name) and text after it; value tells whether that
    // text is a value, speech whether it reads as speech (おはよう, うん).
    static bool BracketField(std::string_view line, bool& value, std::string& name, bool& speech);
    void RowParts(const std::string& x, const std::string& y);
    // The common words GivenSplit took for the surname of a full name (氷室 of 氷室将嗣) and their
    // name-layer Korean (히무로), which they read as in that full name only.
    std::map<std::string, std::string> commonSurnames_;
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
    std::set<std::string> honGuess_;                    // those before an honorific other than 殿
    std::map<std::string, int> framed_;                 // kanji runs in a person frame (PersonFrame)
    std::map<std::string, int> kanaHon_;                // hiragana name-layer persons before an honorific
    std::map<std::string, int> kanaFree_;               // unknown hiragana runs where a name stands (FreeKanaNames)
    std::map<std::string, int> kanaBare_;               // the closed uses of those runs before は, を or へ
    struct Loose {
        int closed = 0;
        int open = 0;
        int bare = 0;  // closed uses before は, を or へ
        std::set<char32_t> particles;
    };
    std::map<std::string, Loose> kanaLoose_;            // LooseKanaUses
    std::map<std::string, std::map<std::string, bool>> coordinated_;  // CoordinatedKana: word -> run -> only weak
    std::map<std::string, std::set<std::string>> kanaJoined_;   // kana words accepted as names -> the names joined to them
    // Whether the kana name t at line[a, b) stands joined to one of its names there (みゆきとそら, そらとみゆき).
    bool JoinedAt(std::string_view line, std::size_t a, std::size_t b, const std::string& t);
    // The name layer's spelling of the katakana name t that the Korean of a longer katakana word of the
    // document starting with t starts with (グリッドマン in グリッドマンユニバース 그리드맨 유니버스), or "".
    std::string CompoundSpelling(std::string_view t);
    std::map<std::string, int> gloss_;                  // surfaces with a kana gloss or ruby
    std::map<std::string, std::string> reading_;        // first gloss or ruby reading
    std::map<std::string, int> runs_;                   // kanji runs
    std::map<std::string, int> ended_;                  // kanji runs before a particle, an honorific or no hiragana
    std::map<std::string, int> kana_;                   // katakana runs
    std::map<std::array<std::string, 3>, int> joined_;  // kanji and katakana pairs
    std::map<std::string, int> chains_;                 // katakana runs joined by ・ or ＝
    std::map<std::string, int> prose_;                  // katakana runs of lines mostly not katakana
    std::set<std::string> kataRuns_;                    // the katakana runs of the document (CompoundSpelling)
    std::map<std::string, int> kataUse_;                // whole katakana runs before a particle (spec LEX-4953)
    std::map<std::string, int> kataSubject_;            // the uses of those runs before は or が
    std::map<std::string, int> kataPlace_;              // and before に, へ or から
    std::map<std::string, int> agents_;                 // katakana runs that do a favour (AgentCue)
    std::map<std::string, DictFlags> flags_;
    std::map<std::string, Name> scores_;                // accepted names, valid while scoresValid_
    std::set<std::string> siblings_;                    // SiblingNames, valid while scoresValid_
    std::map<std::string, std::string> numbered_;       // numbered names to their name, valid while scoresValid_
    bool scoresValid_ = false;
    unsigned scoresBuilt_ = 0;  // the times scores_ was built
    // The accepted names by first rune, longest first, as of build byFirstBuilt_ of scores_.
    std::map<char32_t, std::vector<std::string>> byFirst_;
    unsigned byFirstBuilt_ = 0;
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
