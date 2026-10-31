#pragma once
// C++ API of the engine. All strings are UTF-8.
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans {

namespace dict {
class Snapshot;
}

namespace filter {
class Filters;
}

namespace names {
struct List;
}

namespace pipeline {
struct DocumentNames;
}

namespace internal {
struct Access;
struct Generated;
struct StageObserver;
struct UserWatch;
}

// Off never publishes a cache generation; Sync attempts publication before Open returns.
// Off can write a .bad marker and rename a generation proven corrupt.
enum class CacheWrite { Off, Sync };

// Auto checks user dictionary sources at most once a second. Changes, additions and
// removals are loaded in the background. A failed load keeps the previous stack and
// is retried when its source versions or requested options change.
enum class UserReload { Off, Auto };

// The speech level of the sentence ends of speech: the text inside 「」, with a quote an
// earlier line left open. Narration keeps its 다.
enum class Speech {
    Soft,    // the plain sentence ends (간다, 학생이다) in 해체 (갈 거야, 학생이야)
    Strict,  // the sentence ends of the translation (간다, 학생이다); a plain question still asks in
             // 해체 (大丈夫か？ 괜찮아?)
};

// The speech levels of dialogue lines (spec/register.md): Auto writes a line that a quote opens again
// at the level its cues give after the translation; Keep keeps the translation's own levels.
enum class SpeechLevels { Auto, Keep };

// Dictionary parsing at load time; explicit strictDict takes precedence over the config.
struct EngineOptions {
    std::optional<bool> strictDict{};
    bool defaultStrictDict = true;
    std::string configPath{};
    bool operator==(const EngineOptions&) const = default;
};

struct DictionaryOptions {
    std::optional<bool> strict{};
};

struct UserDictEntry {
    std::string ja{};
    std::string ko{};
    std::string pos{};
    std::string options{};
};

struct EhndFile {
    std::string name{};
    std::string text{};
};

// The loaded dictionaries. Shared by any number of sessions and threads.
class Engine {
public:
    // Reference mode disables translation improvements. Sessions inherit it.
    explicit Engine(bool reference = false, CacheWrite cacheWrite = CacheWrite::Sync,
                    UserReload userReload = UserReload::Off);
    Engine(bool reference, CacheWrite cacheWrite, UserReload userReload, const EngineOptions& options);
    // Changes options for later loads; does not read or reload a dictionary.
    void SetOptions(const EngineOptions& options);
    // Waits for a background reload, unless DetachReload was called.
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Loads the folders in order, later folders overriding earlier ones, then the user
    // dictionary file userFile (none when empty or missing). A user dictionary file that
    // does not load is left out on the first Open (UserFileError says why). An active
    // engine keeps its previous stack on failure. Returns false when the load fails.
    bool Open(const std::vector<std::string>& dirs, bool verifyDicts = false, std::string_view userFile = {});
    // User sources replace the single file: explicit files or folders of userdict*.txt/tsv.
    bool OpenUserDicts(const std::vector<std::string>& dirs, bool verifyDicts,
                       const std::vector<std::string>& userPaths);

    // Reloads the folders and user dictionary file given to Open, and the Ehnd folder,
    // and swaps the new dictionaries in. Sessions keep translating with the dictionaries
    // they started with (see UserReload). Returns false when the load fails; the
    // dictionaries loaded before stay.
    bool Reload();
    bool Reload(const DictionaryOptions& options);

    bool SetUserDictRaw(std::string_view text, const DictionaryOptions& options = {});
    bool SetUserDict(const std::vector<UserDictEntry>& entries, const DictionaryOptions& options = {});
    std::vector<UserDictEntry> GetUserDict() const;
    bool SetEhnd(const std::vector<EhndFile>& files, const DictionaryOptions& options = {});

    // Takes the filters and the user dictionary of the Ehnd folder dir (its
    // PreFilter*.txt, PostFilter*.txt, SkipLayer*.txt and UserDict*.txt files, see
    // docs/ehnd-compat.md) for the sessions created or reset after it, with the
    // prefilter and postfilter files of the dictionary folders; an empty dir drops them.
    // Before Open it only names the folder that Open reads. With UserReload::Auto the
    // folder is watched like the user dictionary file. Returns false when dir cannot be
    // read; the folder taken before stays.
    bool OpenEhnd(std::string_view dir);
    bool OpenEhnd(std::string_view dir, const DictionaryOptions& options);

    // The lines of the filter files and of the Ehnd folder that the last Open, Reload or
    // OpenEhnd skipped, as "file:line: message".
    std::vector<std::string> FilterErrors() const;

    bool loaded() const;

    // User dictionary errors and skipped-row warnings of the last load, one line per
    // diagnostic as file:line: reason; empty when there are none.
    std::string UserFileError() const;

    // Stops automatic reloads and lets a running one finish on its own, so that the
    // destructor does not wait for it; for a library being unloaded.
    void DetachReload();

    // The current dictionaries; null before a successful Open.
    std::shared_ptr<const dict::Snapshot> Dictionaries() const;
    // The current filters; null without filter files and Ehnd folder.
    std::shared_ptr<const filter::Filters> Filters() const;

private:
    friend class Session;
    friend struct internal::Access;
    const bool reference_;
    const CacheWrite cacheWrite_;
    mutable std::mutex mu_;
    std::vector<std::string> dirs_;
    bool verifyDicts_ = false;
    std::vector<std::string> userPaths_;
    std::shared_ptr<const dict::Snapshot> snapshot_;
    std::shared_ptr<internal::Generated> generated_;  // the marker families made on snapshot_
    mutable std::uint64_t loads_ = 0;  // loads started
    std::uint64_t current_ = 0;        // the load that made snapshot_
    std::unique_ptr<internal::UserWatch> watch_;  // the user dictionary source state
    std::string ehndDir_;
    std::shared_ptr<const filter::Filters> filters_;
    std::vector<std::string> filterErrors_;
};

// A translation of text with the session's options. Each call is translated on its own:
// nothing from earlier calls shapes it, and a call's lines are read as one document (the
// names, speech and narration of a document come from the lines of the call). Text may
// hold one or more "\n"-separated lines ("\r" is kept). One session is used from one
// thread at a time. A session translates with the dictionaries of the engine when it was
// created or last reset; with UserReload::Auto, a translation that finds new dictionaries
// in the engine takes them.
class Session {
public:
    explicit Session(const Engine& engine, bool reference = false, bool cg = true, bool misread = false);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Lines over 64 KiB are translated in pieces of at most 16 KiB, split at sentence ends outside brackets where possible.
    std::string Translate(std::string_view text);
    // Translates text as the lines that follow the lines of context in one document: the
    // context gives its cues (names, speech, quotes, senses) and, wrapped, the start of a
    // sentence that text goes on with, but only the lines of text are returned.
    std::string Translate(std::string_view text, std::string_view context);

    // Takes the engine's current dictionaries.
    void Reset();

    // The speech level of later translations; Speech::Strict until set. Reference mode
    // writes Speech::Strict.
    void SetSpeech(Speech speech) { speech_ = speech; }

    // The speech levels of the dialogue lines of later translations; SpeechLevels::Auto until set.
    // Reference mode keeps the translation's levels.
    void SetSpeechLevels(SpeechLevels levels) { speechLevels_ = levels; }

    // Whether the lines of later translations may break in the middle of a sentence
    // (captions, hooked text); false until set. Wrapped, a line that ends where a sentence
    // cannot end is translated as one sentence with the lines after it, and the translation
    // is split back at the breaks, one line out for each line in.
    void SetWrapped(bool wrapped) { wrapped_ = wrapped; }

    // Whether later translations read a kanji spelling of a grammatical word after a predicate as
    // its kana (慌てた様に as 慌てたように, spec LEX-1050); true until set.
    void SetPredicateVariants(bool on) { predicateVariants_ = on; }

    // The names of the documents of later translations, as the lines of a name list
    // (docs/api.md): JA <TAB> KO [<TAB> KIND [<TAB> OPTIONS]]. The list applies to each call
    // on its own, beside the names the call's lines show; empty text drops it. Returns false
    // when a line is malformed or its name does not compile; the other lines are kept. The list
    // is compiled here and again when the session takes new dictionaries.
    bool SetNames(std::string_view text);

private:
    friend struct internal::Access;
    std::string TranslateObserved(std::string_view text, const internal::StageObserver& observer,
                                  std::string_view context = {});
    bool CompileNames();

    const Engine& engine_;
    const bool reference_;
    const bool cg_;
    const bool misread_;
    Speech speech_ = Speech::Strict;
    SpeechLevels speechLevels_ = SpeechLevels::Auto;
    bool wrapped_ = false;
    bool predicateVariants_ = true;
    std::shared_ptr<const names::List> nameList_;
    // nameList_ compiled for namesSnapshot_; rebuilt when snapshot_ is another.
    std::shared_ptr<const pipeline::DocumentNames> names_;
    std::shared_ptr<const dict::Snapshot> namesSnapshot_;
    std::shared_ptr<const dict::Snapshot> snapshot_;
    std::shared_ptr<internal::Generated> generated_;
    std::shared_ptr<const filter::Filters> filters_;
};

}  // namespace hdTrans
