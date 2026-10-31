#include "core/engine.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "core/config/config.h"
#include "core/dict/runes.h"

#include "core/dict/folders.h"
#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/engine/load_info.h"
#include "core/engine/raw.h"
#include "core/engine/rules.h"
#include "core/engine/user_watch.h"
#include "core/filter/ehnd.h"
#include "core/filter/filter.h"
#include "core/output/families.h"
#include "core/userdict/conjugating.h"
#include "core/text/lines.h"

namespace hdTrans {

namespace {

constexpr std::chrono::seconds kCheckInterval{1};

bool Loaded(const dict::LoadResult& r) { return r.snapshot && r.snapshot->HasDictionary(); }

const std::vector<dict::File> kNoFiles;

struct MemoryInput {
    std::optional<dict::File> user;
    std::optional<std::vector<EhndFile>> ehnd;
    bool strictEhnd = true;
    bool strictEhndOverride = false;
    std::optional<bool> usersStrictOverride;
    std::optional<bool> folderStrictOverride;
};

// The filters of the prefilter and postfilter files of a snapshot and of an Ehnd folder.
struct FilterLoad {
    std::shared_ptr<const filter::Filters> filters;
    std::vector<std::string> errors;  // the lines skipped, as "file:line: message"
    std::vector<dict::LoadFileInfo> info;
    std::unordered_set<std::string> rejectedSources;
    internal::FolderVersion version;  // the Ehnd folder as read
    bool read = true;                 // false when the Ehnd folder cannot be read
    bool valid = true;                // false when strict mode rejects an Ehnd file
};

bool StrictDict(const EngineOptions& options, bool verify, const internal::ConfigResult& config) {
    if (verify) return true;
    if (options.strictDict) return *options.strictDict;
    return config.settings.strictDict.value_or(options.defaultStrictDict);
}

bool ConfigMatches(const internal::ConfigResult& config, const internal::FileVersion& version) {
    return version.present == config.sourceStamp.has_value() &&
           (!version.present || version.stamp == *config.sourceStamp);
}

void RefreshMemoryPolicy(MemoryInput& memory, bool strict) {
    if (memory.user && !memory.user->strictOverride) memory.user->strict = strict;
    if (!memory.strictEhndOverride) memory.strictEhnd = strict;
}

std::string UpperAscii(std::string text) {
    for (char& c : text) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return text;
}

dict::LoadFileKind FilterFileKind(const filter::Source& source, bool memory) {
    using Kind = filter::Source::Kind;
    using FileKind = dict::LoadFileKind;
    switch (source.kind) {
    case Kind::PreFilter:
        return source.native ? FileKind::NativePreFilter
                             : memory ? FileKind::MemoryEhndPreFilter : FileKind::EhndPreFilter;
    case Kind::PostFilter:
        return source.native ? FileKind::NativePostFilter
                             : memory ? FileKind::MemoryEhndPostFilter : FileKind::EhndPostFilter;
    case Kind::SkipLayer:
        return memory ? FileKind::MemoryEhndSkipLayer : FileKind::EhndSkipLayer;
    case Kind::UserDict:
        return memory ? FileKind::MemoryEhndUserDict : FileKind::EhndUserDict;
    }
    return FileKind::Shipped;
}

// The filters of snap and of one Ehnd input (none when empty).
FilterLoad LoadFilters(const dict::Snapshot* snap, const std::string& dir, bool strict,
                       const std::optional<std::vector<EhndFile>>& memory = {}, bool memoryStrict = true,
                       bool explicitPath = true, const std::optional<dict::UserSource>& known = {}) {
    FilterLoad out;
    out.version = internal::FolderVersionOf(dir);
    std::vector<filter::Source> sources;
    std::vector<filter::Error> errors;
    std::unordered_map<std::string, bool> userFilters;
    for (const dict::File& f : snap ? snap->PlainFiles() : kNoFiles) {
        if (f.layer != dict::FileLayer::PreFilter && f.layer != dict::FileLayer::PostFilter) continue;
        const auto kind = f.layer == dict::FileLayer::PreFilter ? filter::Source::Kind::PreFilter
                                                                 : filter::Source::Kind::PostFilter;
        sources.push_back({kind, f.name, std::string(dict::internal::WithoutHeader(f.text)), true});
        if (f.user) userFilters.insert_or_assign(f.name, f.strict);
    }
    if (memory) {
        std::vector<filter::Source> added;
        for (const EhndFile& file : *memory) {
            if (file.name.empty() || file.name == "." || file.name == ".." ||
                file.name.find_first_of("/\\\0", 0, 3) != std::string::npos ||
                !dict::internal::ValidUtf8(file.name) || !dict::internal::ValidUtf8(file.text)) {
                errors.push_back({file.name, 0, "invalid UTF-8 text or Ehnd filename"});
                out.read = false;
                continue;
            }
            const std::string name = UpperAscii(file.name);
            if (!name.ends_with(".TXT")) continue;
            filter::Source::Kind kind;
            if (name.starts_with("PREFILTER")) kind = filter::Source::Kind::PreFilter;
            else if (name.starts_with("POSTFILTER")) kind = filter::Source::Kind::PostFilter;
            else if (name.starts_with("SKIPLAYER")) kind = filter::Source::Kind::SkipLayer;
            else if (name.starts_with("USERDICT")) kind = filter::Source::Kind::UserDict;
            else continue;
            added.push_back({kind, file.name, filter::EhndText(file.text), false});
        }
        std::stable_sort(added.begin(), added.end(), [](const filter::Source& a, const filter::Source& b) {
            if (a.kind != b.kind) return a.kind < b.kind;
            return UpperAscii(a.name) < UpperAscii(b.name);
        });
        sources.insert(sources.end(), added.begin(), added.end());
    } else if (!dir.empty()) {
        if (dir.find('\0') != std::string::npos || !dict::internal::ValidUtf8(dir)) {
            errors.push_back({dir, 0, "invalid Ehnd folder path"});
            out.read = false;
        } else {
            std::error_code error;
            const auto path = std::filesystem::path(std::u8string(
                reinterpret_cast<const char8_t*>(dir.data()), dir.size()));
            const bool missing = !std::filesystem::exists(path, error) && !error;
            if (missing && !(explicitPath && known && known->explicitPath && known->path == dir && known->available)) {
                if (explicitPath) errors.push_back({dir, 0, "the explicit Ehnd folder is missing"});
            } else {
                out.read = filter::ReadEhndFolder(dir, sources, errors);
            }
        }
    }
    std::unordered_set<std::string> seen;
    const auto remember = [&](const std::vector<filter::Error>& found) {
        for (const auto& error : found) {
            std::string text = error.file + ":" + std::to_string(error.line) + ": " + error.message;
            if (seen.insert(text).second) out.errors.push_back(std::move(text));
        }
    };
    remember(errors);
    for (;;) {
        std::vector<filter::Error> rows;
        std::vector<std::size_t> acceptedCounts;
        auto built = filter::Build(sources, rows, &acceptedCounts);
        remember(rows);
        std::unordered_set<std::string> failed;
        for (const auto& source : sources) {
            if (source.native) {
                const auto policy = userFilters.find(source.name);
                if (policy == userFilters.end() || !policy->second) continue;
            } else if (!(memory ? memoryStrict : strict)) {
                continue;
            }
            for (const auto& error : rows) {
                if (error.file == source.name && error.kind != filter::ErrorKind::IgnoredShortFields) {
                    failed.insert(source.name);
                }
            }
        }
        if (failed.empty()) {
            out.filters = std::move(built);
            for (std::size_t i = 0; i < sources.size(); ++i) {
                std::string path = sources[i].name;
                if (!sources[i].native && !memory) {
                    const auto folder = std::filesystem::path(std::u8string(
                        reinterpret_cast<const char8_t*>(dir.data()), dir.size()));
                    const auto name = std::filesystem::path(std::u8string(
                        reinterpret_cast<const char8_t*>(path.data()), path.size()));
                    const auto joined = (folder / name).u8string();
                    path.assign(reinterpret_cast<const char*>(joined.data()), joined.size());
                }
                out.info.push_back({std::move(path), FilterFileKind(sources[i], memory.has_value()), acceptedCounts[i]});
            }
            if (!memory && dir.empty()) out.info.push_back({{}, dict::LoadFileKind::EhndDisabled, {}});
            break;
        }
        out.valid = false;
        out.rejectedSources.insert(failed.begin(), failed.end());
        std::erase_if(sources, [&](const filter::Source& source) {
            return (!source.native || userFilters.contains(source.name)) && failed.contains(source.name);
        });
    }
    return out;
}

}  // namespace

namespace internal {

// Copies the marker families the dictionaries made at load into out, under the ids they have
// there; the dictionaries' targets refer to these ids.
void CopyGenerated(const userdict::Families& from, output::Families& out) {
    for (const auto& [id, rows] : from.GeneratedFamilies()) out.Insert(id[0], id, rows);
}

// The rules program from the bodies the dictionary rows carry, with the global rules of
// the built-in program. A number no row names gets an empty rule, which is never true.
std::string RuleText(const std::vector<std::string>& bodies, const rules::Program& builtin);

bool AssembleRules(const dict::Snapshot& snap, rules::Program& program) {
    const std::vector<std::string>& bodies = snap.RuleBodies();
    if (bodies.empty()) return false;
    const std::string text = RuleText(bodies, RulesProgram());
    rules::LoadError error{};
    if (!rules::Load(text, program, error)) return false;
    internal::AttachCompiled(program, text);
    return true;
}

// The program text the assembly loads, so the rule compiler can be given the same
// lines the compiled code is matched against.
std::string RuleText(const std::vector<std::string>& bodies, const rules::Program& builtin) {
    std::string text = "rules 1 " + std::to_string(bodies.size()) + " " +
                       std::to_string(builtin.globalRules.size()) + "\n";
    for (std::size_t n = 0; n < bodies.size(); n++) {
        text += "L " + std::to_string(n) + " " + (bodies[n].empty() ? "||0" : bodies[n]) + "\n";
    }
    // The global rules keep the text they were compiled from; a dictionary row never
    // names one, so they stay in the rule files.
    for (const std::string_view line : RuleLines()) {
        if (line.starts_with("G ")) text += line;
    }
    return text;
}

std::string RuleProgramText(const dict::Snapshot& snap) {
    if (snap.RuleBodies().empty()) return {};
    return RuleText(snap.RuleBodies(), RulesProgram());
}

std::shared_ptr<Generated> NewGenerated(const dict::Snapshot& snap) {
    auto generated = std::make_shared<Generated>();
    CopyGenerated(snap.GeneratedFamilies(), generated->families);
    generated->hasRules = AssembleRules(snap, generated->rules);
    return generated;
}

struct Access {
    static bool Reference(const Engine& e) { return e.reference_; }
    static std::string TranslateObserved(Session& session, std::string_view text, const StageObserver& observer,
                                         std::string_view context) {
        return session.TranslateObserved(text, observer, context);
    }

    // e.mu_ is held. A later successful load supersedes this ticket.
    static bool Finish(Engine& e, std::uint64_t ticket, const UserVersion& version, dict::LoadResult r,
                       std::vector<dict::LoadError> userErrors, FilterLoad filters, bool keepFailed,
                       MemoryInput memory, const dict::UserSource& ehndSource,
                       const std::vector<dict::UserSource>& requested) {
        if (ticket < e.current_) return Loaded(r);
        UserWatch& w = *e.watch_;
        const bool filterFailed = !filters.read || !filters.valid;
        const bool failed = filterFailed || std::any_of(userErrors.begin(), userErrors.end(),
            [](const dict::LoadError& error) { return dict::FailsLoad(error.kind); });
        w.errors = std::move(userErrors);
        if (!Loaded(r) || (keepFailed && failed)) {
            if (filterFailed) {
                w.filterFailed = true;
                e.filterErrors_ = std::move(filters.errors);
            }
            w.ehnd.failed = std::move(filters.version);
            w.ehnd.hasFailed = true;
            w.watch.failed = version;
            w.watch.hasFailed = true;
            for (dict::LoadError& error : r.errors) {
                if (dict::FailsLoad(error.kind)) w.errors.push_back(std::move(error));
            }
            return false;
        }
        w.filterFailed = filterFailed;
        w.sourceHistorySeeded = false;
        w.acceptedSources.clear();
        for (const auto& source : version.sources) {
            if (!source.available) continue;
            const bool skipped = !source.directory && std::any_of(w.errors.begin(), w.errors.end(), [&](const auto& error) {
                return error.file == source.path && dict::FailsLoad(error.kind);
            });
            if (!skipped && (source.directory || !filters.rejectedSources.contains(source.path))) {
                w.acceptedSources.push_back(source);
            }
        }
        for (const auto& [path, stamp] : version.files) {
            if (!stamp.present || stamp.headerProbeFailed) continue;
            const bool rejected = std::any_of(w.errors.begin(), w.errors.end(), [&](const auto& error) {
                return error.file == path && dict::FailsLoad(error.kind);
            });
            const bool listed = std::any_of(w.acceptedSources.begin(), w.acceptedSources.end(), [&](const auto& source) {
                return source.path == path;
            });
            if (!rejected && !listed && !filters.rejectedSources.contains(path)) {
                w.acceptedSources.push_back({path, false, true, false});
            }
        }
        w.memoryUser = std::move(memory.user);
        w.memoryEhnd = std::move(memory.ehnd);
        w.memoryEhndStrict = memory.strictEhnd;
        w.memoryEhndStrictOverride = memory.strictEhndOverride;
        w.userStrictOverride = memory.usersStrictOverride;
        w.ehndStrictOverride = memory.folderStrictOverride;
        e.ehndDir_ = ehndSource.path;
        w.requestedSources = requested;
        w.requestedEhnd = ehndSource;
        w.acceptedEhnd.reset();
        if (!w.memoryEhnd && !ehndSource.path.empty() && filters.read && filters.version.available) {
            w.acceptedEhnd = ehndSource;
            w.acceptedEhnd->directory = true;
            w.acceptedEhnd->available = true;
        }
        w.dictionaryInfo = std::move(r.info);
        w.filterInfo = std::move(filters.info);
        w.watch.loaded = version;
        w.watch.hasFailed = false;
        e.current_ = ticket;
        e.snapshot_ = std::move(r.snapshot);
        e.generated_ = NewGenerated(*e.snapshot_);
        e.filters_ = std::move(filters.filters);
        e.filterErrors_ = std::move(filters.errors);
        w.ehnd.loaded = std::move(filters.version);
        w.ehnd.hasFailed = false;
        return true;
    }

    // Retry without failed user files. Dictionary-folder errors still fail the load.
    static dict::LoadResult LoadUsers(const std::vector<std::string>& dirs, bool verify, bool cacheWrite,
                                      const UserVersion& version, const std::vector<dict::UserSource>& accepted,
                                      bool strict, const std::optional<dict::File>& memory,
                                      std::vector<dict::LoadError>& userErrors) {
        std::vector<std::string> files;
        for (const auto& [path, stamp] : version.files) {
            if (stamp.headerProbeFailed) {
                const bool known = std::any_of(accepted.begin(), accepted.end(), [&](const auto& source) {
                    return source.path == path && source.available;
                });
                userErrors.push_back({path, known ? dict::LoadErrorKind::Read : dict::LoadErrorKind::OptionalSource,
                    0, dict::TsvErrorKind::Read, known ? std::string() : "cannot read the optional filter header"});
            } else if (stamp.present) {
                files.push_back(path);
            } else {
                userErrors.push_back({path, dict::LoadErrorKind::Read, 0, dict::TsvErrorKind::Read, {}});
            }
        }
        for (const auto& source : version.sources) {
            const auto known = std::find_if(accepted.begin(), accepted.end(), [&](const auto& old) {
                return old.path == source.path;
            });
            const bool unavailable = !source.available ||
                (known != accepted.end() && known->directory != source.directory);
            if ((source.directory && !source.available) ||
                (source.explicitPath && known != accepted.end() && known->explicitPath && unavailable)) {
                const bool folder = source.directory || (known != accepted.end() && known->directory);
                userErrors.push_back({source.path, folder ? dict::LoadErrorKind::Folder : dict::LoadErrorKind::Read,
                                      0, dict::TsvErrorKind::Read, {}});
            } else if (!source.available && source.explicitPath && known == accepted.end() &&
                       std::none_of(version.files.begin(), version.files.end(), [&](const auto& file) {
                           return file.first == source.path;
                       })) {
                userErrors.push_back({source.path, dict::LoadErrorKind::OptionalSource, 0,
                                      dict::TsvErrorKind::Read, "the explicit user dictionary source is missing"});
            }
        }
        for (;;) {
            auto result = dict::LoadFolders(dirs, {}, verify, cacheWrite, files, strict, memory ? &*memory : nullptr);
            if (Loaded(result)) {
                for (const auto& error : result.errors) {
                    // A Japanese target is a notice of --verify-dicts; the row loads.
                    if (!dict::FailsLoad(error.kind) && error.kind != dict::LoadErrorKind::JapaneseTarget &&
                        (std::find(files.begin(), files.end(), error.file) != files.end() ||
                         (memory && error.file == memory->name))) userErrors.push_back(error);
                }
                return result;
            }
            const auto attempted = files;
            const auto count = files.size();
            std::erase_if(files, [&result](const std::string& path) {
                return std::any_of(result.errors.begin(), result.errors.end(), [&path](const dict::LoadError& error) {
                    return error.file == path && dict::FailsLoad(error.kind);
                });
            });
            if (files.size() == count) return result;
            for (auto& error : result.errors) {
                if (dict::FailsLoad(error.kind) &&
                    std::find(attempted.begin(), attempted.end(), error.file) != attempted.end() &&
                    std::find(files.begin(), files.end(), error.file) == files.end()) {
                    userErrors.push_back(std::move(error));
                }
            }
        }
    }

    // The filters of r, the outcome of a load with the Ehnd folder dir.
    static FilterLoad FiltersOf(const dict::LoadResult& r, const dict::UserSource& source, bool strict,
                                const MemoryInput& memory, const std::optional<dict::UserSource>& known, bool verify) {
        const std::string& dir = source.path;
        if (!Loaded(r)) {
            FilterLoad failed;
            failed.version = FolderVersionOf(dir);
            return failed;
        }
        return LoadFilters(r.snapshot.get(), dir, strict, memory.ehnd, verify || memory.strictEhnd, source.explicitPath, known);
    }

    static bool Load(Engine& e, const std::vector<std::string>& dirs, bool verify,
                     const std::vector<std::string>& userPaths, bool open, const DictionaryOptions& local = {},
                     const dict::File* newUser = nullptr, const std::vector<EhndFile>* newEhnd = nullptr,
                     const std::vector<dict::UserSource>* origins = nullptr,
                     const ConfigResult* suppliedConfig = nullptr) {
        std::uint64_t ticket, revision;
        bool active;
        dict::UserSource ehnd;
        std::optional<dict::UserSource> knownEhnd;
        EngineOptions options;
        MemoryInput memory;
        std::vector<dict::UserSource> accepted, requested;
        if (origins) requested = *origins;
        else for (const auto& path : userPaths) requested.push_back({path});
        {
            std::lock_guard<std::mutex> lock(e.mu_);
            ticket = ++e.loads_;
            active = e.snapshot_ != nullptr || e.watch_->sourceHistorySeeded;
            ehnd = e.watch_->requestedEhnd;
            knownEhnd = e.watch_->acceptedEhnd;
            options = e.watch_->options;
            revision = e.watch_->optionsRevision;
            accepted = e.watch_->acceptedSources;
            memory = {e.watch_->memoryUser, e.watch_->memoryEhnd, e.watch_->memoryEhndStrict,
                      e.watch_->memoryEhndStrictOverride, e.watch_->userStrictOverride, e.watch_->ehndStrictOverride};
        }
        const UserVersion version = UserVersionOf(requested, options.configPath, revision);
        const ConfigResult config = suppliedConfig ? *suppliedConfig :
            options.configPath.empty() ? ConfigResult{} : LoadConfig(options.configPath);
        const bool baseStrict = StrictDict(options, verify, config);
        if (open) memory.usersStrictOverride.reset();
        if (newUser) {
            memory.user = *newUser;
            memory.user->strict = local.strict.value_or(baseStrict);
            memory.user->strictOverride = local.strict.has_value();
        }
        if (newEhnd) {
            memory.ehnd = *newEhnd;
            memory.strictEhnd = local.strict.value_or(baseStrict);
            memory.strictEhndOverride = local.strict.has_value();
            memory.folderStrictOverride.reset();
            ehnd.path.clear();
        }
        if (!newUser && !newEhnd && local.strict) {
            memory.usersStrictOverride = local.strict;
            memory.folderStrictOverride = local.strict;
            if (memory.user) {
                memory.user->strict = *local.strict;
                memory.user->strictOverride = true;
            }
            if (memory.ehnd) {
                memory.strictEhnd = *local.strict;
                memory.strictEhndOverride = true;
            }
        }
        RefreshMemoryPolicy(memory, baseStrict);
        const bool strict = verify || memory.usersStrictOverride.value_or(baseStrict);
        const bool ehndStrict = verify || memory.folderStrictOverride.value_or(baseStrict);
        const bool cacheWrite = e.cacheWrite_ == CacheWrite::Sync;
        std::vector<dict::LoadError> userErrors;
        dict::LoadResult r = LoadUsers(dirs, verify, cacheWrite, version, accepted, strict, memory.user, userErrors);
        FilterLoad filters = FiltersOf(r, ehnd, ehndStrict, memory, knownEhnd, verify);
        if ((!options.configPath.empty() && !ConfigMatches(config, version.config)) ||
            UserVersionOf(requested, options.configPath, revision) != version) {
            userErrors.push_back({requested.empty() ? std::string() : requested.front().path,
                dict::LoadErrorKind::Read, 0, dict::TsvErrorKind::Read, "the user sources changed while loading"});
            r.snapshot.reset();
        }
        std::lock_guard<std::mutex> lock(e.mu_);
        if (!Finish(e, ticket, version, std::move(r), std::move(userErrors), std::move(filters), active || !open,
                    std::move(memory), ehnd, requested)) return false;
        if (open && ticket == e.current_) {
            e.dirs_ = dirs;
            e.verifyDicts_ = verify;
            e.userPaths_ = userPaths;
        }
        return true;
    }

    // Starts a background reload when the user dictionary file or the Ehnd folder is due
    // at now; e.mu_ is held.
    static bool StartReload(const Engine& e, std::chrono::steady_clock::time_point now,
                            std::chrono::steady_clock::duration interval) {
        UserWatch& w = *e.watch_;
        if (!w.automatic || w.detached || w.running || !e.snapshot_) return false;
        const bool watchUser = !e.userPaths_.empty() || !w.options.configPath.empty() ||
                               w.optionsRevision != 0 || w.memoryUser || w.memoryEhnd;
        const bool userChecked = !watchUser || now >= w.watch.next;
        const bool ehndChecked = e.ehndDir_.empty() || now >= w.ehnd.next;
        UserVersion found;
        FolderVersion foundEhnd;
        const bool user = watchUser && w.watch.Due(w.requestedSources, now, interval, found,
                                                   w.options.configPath, w.optionsRevision);
        const bool ehnd = !e.ehndDir_.empty() && w.ehnd.Due(e.ehndDir_, now, interval, foundEhnd);
        // A failed load belongs to both source versions, not to either one alone.
        const bool retry = userChecked && ehndChecked && w.watch.hasFailed && w.ehnd.hasFailed &&
            (found != w.watch.failed || foundEhnd != w.ehnd.failed);
        if (!user && !ehnd && !retry) return false;
        w.worker.Join();
        const std::uint64_t ticket = ++e.loads_;
        // The thread reaches the engine only through target, which the engine clears
        // before it goes; it never needs the engine to wait for it.
        w.running = w.worker.Start([target = w.target, ticket, dirs = e.dirs_, verify = e.verifyDicts_,
                                    paths = w.requestedSources, ehndSource = w.requestedEhnd,
                                    knownEhnd = w.acceptedEhnd, options = w.options,
                                    revision = w.optionsRevision, accepted = w.acceptedSources,
                                    memory = MemoryInput{w.memoryUser, w.memoryEhnd, w.memoryEhndStrict,
                                        w.memoryEhndStrictOverride, w.userStrictOverride, w.ehndStrictOverride},
                                    cacheWrite = e.cacheWrite_ == CacheWrite::Sync]() mutable {
            const UserVersion version = UserVersionOf(paths, options.configPath, revision);
            const ConfigResult config = options.configPath.empty() ? ConfigResult{} : LoadConfig(options.configPath);
            const bool baseStrict = StrictDict(options, verify, config);
            RefreshMemoryPolicy(memory, baseStrict);
            const bool strict = verify || memory.usersStrictOverride.value_or(baseStrict);
            const bool ehndStrict = verify || memory.folderStrictOverride.value_or(baseStrict);
            std::vector<dict::LoadError> userErrors;
            dict::LoadResult r = LoadUsers(dirs, verify, cacheWrite, version, accepted, strict, memory.user, userErrors);
            FilterLoad filters = FiltersOf(r, ehndSource, ehndStrict, memory, knownEhnd, verify);
            if ((!options.configPath.empty() && !ConfigMatches(config, version.config)) ||
                UserVersionOf(paths, options.configPath, revision) != version) {
                userErrors.push_back({paths.empty() ? std::string() : paths.front().path,
                    dict::LoadErrorKind::Read, 0, dict::TsvErrorKind::Read, "the user sources changed while loading"});
                r.snapshot.reset();
            }
            std::lock_guard<std::mutex> owner(target->mu);
            if (!target->engine) return;
            Engine& engine = *target->engine;
            std::lock_guard<std::mutex> lock(engine.mu_);
            Finish(engine, ticket, version, std::move(r), std::move(userErrors), std::move(filters), true,
                   std::move(memory), ehndSource, paths);
            engine.watch_->running = false;
            engine.watch_->done.notify_all();
        });
        return w.running;
    }

    // Takes the engine's current dictionaries into a session that holds others.
    static void Refresh(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated, std::shared_ptr<const filter::Filters>& filters) {
        std::lock_guard<std::mutex> lock(e.mu_);
        StartReload(e, std::chrono::steady_clock::now(), kCheckInterval);
        if (snap == e.snapshot_ && filters == e.filters_) return;
        snap = e.snapshot_;
        generated = e.generated_;
        filters = e.filters_;
    }

    static bool CheckNow(const Engine& e, bool wait) {
        std::unique_lock<std::mutex> lock(e.mu_);
        UserWatch& w = *e.watch_;
        w.done.wait(lock, [&w] { return !w.running; });
        w.watch.next = {};
        w.ehnd.next = {};
        if (!StartReload(e, std::chrono::steady_clock::now(), kCheckInterval)) return false;
        if (wait) w.done.wait(lock, [&w] { return !w.running; });
        return true;
    }

    static std::vector<dict::LoadError> Errors(const Engine& e) {
        std::lock_guard<std::mutex> lock(e.mu_);
        return e.watch_->errors;
    }

    static LoadInfo SourceInfo(const Engine& e) {
        std::lock_guard<std::mutex> lock(e.mu_);
        LoadInfo info;
        info.generation = e.current_;
        info.files = e.watch_->dictionaryInfo;
        info.files.insert(info.files.end(), e.watch_->filterInfo.begin(), e.watch_->filterInfo.end());
        return info;
    }

    static std::optional<std::uint64_t> DiagnosticsVersion(const Engine& e) {
        std::lock_guard<std::mutex> lock(e.mu_);
        if (e.watch_->running) return {};
        return e.loads_;
    }

    static bool FilterFailed(const Engine& e) {
        std::lock_guard<std::mutex> lock(e.mu_);
        return e.watch_->filterFailed;
    }

    static std::vector<dict::UserSource> AcceptedSources(const Engine& e,
                                                        std::optional<dict::UserSource>* ehnd = nullptr) {
        std::lock_guard<std::mutex> lock(e.mu_);
        if (ehnd) *ehnd = e.watch_->acceptedEhnd;
        return e.watch_->acceptedSources;
    }

    static void SeedSources(Engine& e, std::vector<dict::UserSource> sources,
                            std::optional<dict::UserSource> ehnd = {}) {
        std::lock_guard<std::mutex> lock(e.mu_);
        if (e.snapshot_) return;
        e.watch_->acceptedSources = std::move(sources);
        e.watch_->acceptedEhnd = std::move(ehnd);
        e.watch_->sourceHistorySeeded = true;
    }

    static void Current(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated, std::shared_ptr<const filter::Filters>& filters) {
        std::lock_guard<std::mutex> lock(e.mu_);
        snap = e.snapshot_;
        generated = e.generated_;
        filters = e.filters_;
    }

    static bool Reload(Engine& e, const DictionaryOptions& local, const dict::File* user = nullptr,
                       const std::vector<EhndFile>* ehnd = nullptr, const ConfigResult* config = nullptr) {
        std::vector<std::string> dirs, paths;
        std::vector<dict::UserSource> sources;
        bool verify;
        {
            std::lock_guard<std::mutex> lock(e.mu_);
            if (!e.snapshot_) return false;
            dirs = e.dirs_;
            paths = e.userPaths_;
            sources = e.watch_->requestedSources;
            verify = e.verifyDicts_;
        }
        return Load(e, dirs, verify, paths, false, local, user, ehnd, &sources, config);
    }

    static bool EntryError(Engine& e, int line) {
        std::lock_guard<std::mutex> lock(e.mu_);
        ++e.loads_;
        e.watch_->errors = {{"userdict.tsv", dict::LoadErrorKind::Plain, line, dict::TsvErrorKind::Read,
                              "a field contains a tab, line break or NUL"}};
        return false;
    }

    static bool OpenEhnd(Engine& e, const std::string& dir, const DictionaryOptions& local,
                         bool explicitPath = true, const ConfigResult* suppliedConfig = nullptr) {
        for (;;) {
            std::shared_ptr<const dict::Snapshot> snap;
            EngineOptions options;
            std::optional<dict::UserSource> known;
            bool verify;
            std::uint64_t ticket;
            {
                std::lock_guard<std::mutex> lock(e.mu_);
                if (!e.snapshot_) {
                    e.ehndDir_ = dir;
                    e.watch_->requestedEhnd = {dir, true, false, explicitPath};
                    e.watch_->ehndStrictOverride = local.strict;
                    e.watch_->memoryEhnd.reset();
                    return true;
                }
                ticket = ++e.loads_;
                snap = e.snapshot_;
                options = e.watch_->options;
                known = e.watch_->acceptedEhnd;
                verify = e.verifyDicts_;
            }
            const FileVersion version = VersionOf(options.configPath);
            const ConfigResult config = suppliedConfig ? *suppliedConfig :
                options.configPath.empty() ? ConfigResult{} : LoadConfig(options.configPath);
            const bool strict = verify || local.strict.value_or(StrictDict(options, verify, config));
            FilterLoad filters = LoadFilters(snap.get(), dir, strict, {}, true, explicitPath, known);
            if ((!options.configPath.empty() && !ConfigMatches(config, version)) ||
                VersionOf(options.configPath) != version) {
                filters.read = false;
                filters.errors.push_back(options.configPath + ":0: the config changed while loading");
            }
            std::lock_guard<std::mutex> lock(e.mu_);
            if (snap != e.snapshot_) continue;
            if (ticket < e.current_) return false;
            if (!filters.read || !filters.valid) {
                e.watch_->filterFailed = true;
                e.filterErrors_ = std::move(filters.errors);
                return false;
            }
            e.current_ = ticket;
            e.ehndDir_ = dir;
            e.watch_->requestedEhnd = {dir, true, false, explicitPath};
            e.watch_->ehndStrictOverride = local.strict;
            e.watch_->acceptedEhnd.reset();
            if (!dir.empty() && filters.version.available) {
                e.watch_->acceptedEhnd = dict::UserSource{dir, true, true, explicitPath};
            }
            e.watch_->filterInfo = std::move(filters.info);
            e.watch_->memoryEhnd.reset();
            e.watch_->memoryEhndStrictOverride = false;
            e.watch_->watch.hasFailed = false;
            e.watch_->filterFailed = false;
            e.filters_ = std::move(filters.filters);
            e.filterErrors_ = std::move(filters.errors);
            e.watch_->ehnd.loaded = std::move(filters.version);
            e.watch_->ehnd.hasFailed = false;
            return true;
        }
    }
};


bool CheckUserFile(const Engine& e) { return Access::CheckNow(e, true); }

bool StartUserFileCheck(const Engine& e) { return Access::CheckNow(e, false); }

std::vector<dict::LoadError> UserFileErrors(const Engine& e) { return Access::Errors(e); }

bool FilterFileFailed(const Engine& e) { return Access::FilterFailed(e); }

std::optional<std::uint64_t> LoadDiagnosticsVersion(const Engine& e) { return Access::DiagnosticsVersion(e); }

LoadInfo LoadedSourceInfo(const Engine& e) { return Access::SourceInfo(e); }

std::vector<dict::UserSource> AcceptedUserSources(const Engine& e) { return Access::AcceptedSources(e); }

void SeedUserSources(Engine& e, std::vector<dict::UserSource> sources) {
    Access::SeedSources(e, std::move(sources));
}

std::vector<dict::UserSource> AcceptedUserSources(const Engine& e, std::optional<dict::UserSource>& ehnd) {
    return Access::AcceptedSources(e, &ehnd);
}

void SeedUserSources(Engine& e, std::vector<dict::UserSource> sources, std::optional<dict::UserSource> ehnd) {
    Access::SeedSources(e, std::move(sources), std::move(ehnd));
}

bool OpenUserSources(Engine& e, const std::vector<std::string>& dirs, bool verify,
                     const std::vector<dict::UserSource>& sources) {
    if (!RulesLoaded()) return false;
    std::vector<std::string> paths;
    for (const auto& source : sources) paths.push_back(source.path);
    return Access::Load(e, dirs, verify, paths, true, {}, nullptr, nullptr, &sources);
}

bool OpenEhndSource(Engine& e, const dict::UserSource& source, const DictionaryOptions& options) {
    return Access::OpenEhnd(e, source.path, options, source.explicitPath);
}

bool OpenUserSources(Engine& e, const std::vector<std::string>& dirs, bool verify,
                     const std::vector<dict::UserSource>& sources, const ConfigResult& config) {
    if (!RulesLoaded()) return false;
    std::vector<std::string> paths;
    for (const auto& source : sources) paths.push_back(source.path);
    return Access::Load(e, dirs, verify, paths, true, {}, nullptr, nullptr, &sources, &config);
}

bool OpenEhndSource(Engine& e, const dict::UserSource& source, const ConfigResult& config,
                    const DictionaryOptions& options) {
    return Access::OpenEhnd(e, source.path, options, source.explicitPath, &config);
}

bool ReloadWithConfig(Engine& e, const ConfigResult& config, const DictionaryOptions& options) {
    return Access::Reload(e, options, nullptr, nullptr, &config);
}

// The text of a load error.
std::string ErrorText(const dict::LoadError& e) {
    std::string s = e.file;
    if (e.line > 0) s += ":" + std::to_string(e.line);
    s += ": ";
    if (!e.message.empty()) return s + e.message;
    switch (e.kind) {
    case dict::LoadErrorKind::Folder: return s + "cannot list the folder";
    case dict::LoadErrorKind::Read: return s + "cannot read the file";
    case dict::LoadErrorKind::Tsv: return s + "malformed line";
    default: return s + "does not load";
    }
}

}  // namespace internal

Engine::Engine(bool reference, CacheWrite cacheWrite, UserReload userReload)
    : Engine(reference, cacheWrite, userReload, EngineOptions{}) {}

Engine::Engine(bool reference, CacheWrite cacheWrite, UserReload userReload, const EngineOptions& options)
    : reference_(reference), cacheWrite_(cacheWrite), watch_(std::make_unique<internal::UserWatch>()) {
    watch_->options = options;
    watch_->automatic = userReload == UserReload::Auto;
    watch_->target->engine = this;
}

void Engine::SetOptions(const EngineOptions& options) {
    std::lock_guard<std::mutex> lock(mu_);
    if (watch_->options == options) return;
    watch_->options = options;
    ++watch_->optionsRevision;
}

Engine::~Engine() {
    {
        std::lock_guard<std::mutex> lock(watch_->target->mu);
        watch_->target->engine = nullptr;
    }
    watch_->worker.Join();
}

void Engine::DetachReload() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        watch_->detached = true;
        watch_->worker.Detach();
    }
    std::lock_guard<std::mutex> lock(watch_->target->mu);
    watch_->target->engine = nullptr;
}

std::string Engine::UserFileError() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::string s;
    for (const dict::LoadError& e : watch_->errors) s += (s.empty() ? "" : "\n") + internal::ErrorText(e);
    return s;
}

bool Engine::Open(const std::vector<std::string>& dirs, bool verifyDicts, std::string_view userFile) {
    return OpenUserDicts(dirs, verifyDicts, {std::string(userFile)});
}

bool Engine::OpenUserDicts(const std::vector<std::string>& dirs, bool verifyDicts,
                           const std::vector<std::string>& userPaths) {
    if (!internal::RulesLoaded()) return false;
    return internal::Access::Load(*this, dirs, verifyDicts, userPaths, true);
}

bool Engine::Reload() { return Reload(DictionaryOptions{}); }

bool Engine::Reload(const DictionaryOptions& options) { return internal::Access::Reload(*this, options); }

bool Engine::SetUserDictRaw(std::string_view text, const DictionaryOptions& options) {
    dict::File file;
    file.name = "userdict.tsv";
    file.text = text;
    file.user = true;
    file.memory = true;
    return internal::Access::Reload(*this, options, &file);
}

bool Engine::SetUserDict(const std::vector<UserDictEntry>& entries, const DictionaryOptions& options) {
    std::string text;
    int line = 0;
    for (const auto& entry : entries) {
        ++line;
        for (const auto* field : {&entry.ja, &entry.ko, &entry.pos, &entry.options}) {
            if (field->find_first_of("\t\r\n\0", 0, 4) != std::string::npos) {
                return internal::Access::EntryError(*this, line);
            }
        }
        text += entry.ja + "\t" + entry.ko + "\t" + entry.pos + "\t" + entry.options + "\n";
    }
    return SetUserDictRaw(text, options);
}

std::vector<UserDictEntry> Engine::GetUserDict() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<UserDictEntry> rows;
    if (!snapshot_) return rows;
    for (const auto& row : snapshot_->MemoryUserEntries()) rows.push_back({row.ja, row.ko, row.pos, row.options});
    return rows;
}

bool Engine::SetEhnd(const std::vector<EhndFile>& files, const DictionaryOptions& options) {
    return internal::Access::Reload(*this, options, nullptr, &files);
}

bool Engine::OpenEhnd(std::string_view dir) { return OpenEhnd(dir, DictionaryOptions{}); }

bool Engine::OpenEhnd(std::string_view dir, const DictionaryOptions& options) {
    return internal::Access::OpenEhnd(*this, std::string(dir), options);
}

std::vector<std::string> Engine::FilterErrors() const {
    std::lock_guard<std::mutex> lock(mu_);
    return filterErrors_;
}

std::shared_ptr<const filter::Filters> Engine::Filters() const {
    std::lock_guard<std::mutex> lock(mu_);
    return filters_;
}

bool Engine::loaded() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snapshot_ != nullptr;
}

std::shared_ptr<const dict::Snapshot> Engine::Dictionaries() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snapshot_;
}

Session::Session(const Engine& engine, bool reference, bool cg, bool misread)
    : engine_(engine), reference_(engine.reference_ || reference), cg_(cg), misread_(misread) {
    std::lock_guard<std::mutex> lock(engine.mu_);
    snapshot_ = engine.snapshot_;
    generated_ = engine.generated_;
    filters_ = engine.filters_;
}

Session::~Session() = default;

std::string Session::Translate(std::string_view text) { return TranslateObserved(text, {}); }

std::string Session::Translate(std::string_view text, std::string_view context) {
    return TranslateObserved(text, {}, context);
}

std::string Session::TranslateObserved(std::string_view text, const internal::StageObserver& observer,
                                       std::string_view context) {
    if (engine_.watch_->automatic) internal::Access::Refresh(engine_, snapshot_, generated_, filters_);
    if (!snapshot_) return {};
    if (nameList_ && namesSnapshot_ != snapshot_) static_cast<void>(CompileNames());
    std::vector<std::string> lines;
    std::size_t contextLines = 0;
    // One "\n" that ends the context ends its last line.
    if (context.ends_with('\n')) context.remove_suffix(1);
    if (!context.empty()) {
        for (const std::string_view line : text::Lines(context)) lines.emplace_back(line);
        contextLines = lines.size();
    }
    for (const std::string_view line : text::Lines(text)) lines.emplace_back(line);
    internal::TranslationOptions options;
    options.reference = reference_;
    options.wordImprovements.cgRules = cg_;
    options.wordImprovements.misreadFilter = misread_;
    options.speech = speech_;
    options.speechLevels = speechLevels_;
    options.wrapped = wrapped_;
    options.names = names_.get();
    options.filters = filters_.get();
    const std::vector<std::string> translated =
        internal::TranslateDocument(*snapshot_, *generated_, lines, observer, options, contextLines);
    std::string out;
    for (std::size_t i = 0; i < translated.size(); i++) {
        if (i > 0) out += '\n';
        out += translated[i];
    }
    return out;
}

bool Session::SetNames(std::string_view text) {
    auto list = std::make_shared<names::List>();
    const bool ok = names::ParseList(text, *list);
    names_.reset();
    namesSnapshot_.reset();
    nameList_.reset();
    if (!list->names.empty()) nameList_ = std::move(list);
    return CompileNames() && ok;
}

bool Session::CompileNames() {
    if (!nameList_ || !snapshot_) return true;
    std::unique_ptr<pipeline::DocumentNames> compiled;
    const bool ok = pipeline::CompileNames(*snapshot_, *nameList_, compiled);
    names_ = std::move(compiled);
    namesSnapshot_ = snapshot_;
    return ok;
}

void Session::Reset() {
    std::lock_guard<std::mutex> lock(engine_.mu_);
    snapshot_ = engine_.snapshot_;
    generated_ = engine_.generated_;
    filters_ = engine_.filters_;
}

namespace internal {

std::string TranslateObserved(Session& session, std::string_view text, const StageObserver& observer,
                              std::string_view context) {
    return Access::TranslateObserved(session, text, observer, context);
}

std::string TranslateRaw(const Engine& e, std::string_view line) {
    std::shared_ptr<const dict::Snapshot> snap;
    std::shared_ptr<Generated> generated;
    std::shared_ptr<const filter::Filters> filters;
    Access::Current(e, snap, generated, filters);
    if (!snap) return {};
    TranslationOptions options;
    options.reference = Access::Reference(e);
    options.filters = filters.get();
    return TranslateRawLine(*snap, *generated, line, {}, options);
}

}  // namespace internal
}  // namespace hdTrans
