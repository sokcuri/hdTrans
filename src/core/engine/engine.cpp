#include "core/engine.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include "core/dict/folders.h"
#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
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

// The filters of the prefilter and postfilter files of a snapshot and of an Ehnd folder.
struct FilterLoad {
    std::shared_ptr<const filter::Filters> filters;
    std::vector<std::string> errors;  // the lines skipped, as "file:line: message"
    internal::FolderVersion version;  // the Ehnd folder as read
    bool read = true;                 // false when the Ehnd folder cannot be read
};

// The filters of snap and of the Ehnd folder dir (none when empty).
FilterLoad LoadFilters(const dict::Snapshot* snap, const std::string& dir) {
    FilterLoad out;
    out.version = internal::FolderVersionOf(dir);
    std::vector<filter::Source> sources;
    std::vector<filter::Error> errors;
    for (const dict::File& f : snap ? snap->PlainFiles() : kNoFiles) {
        if (f.layer != dict::FileLayer::PreFilter && f.layer != dict::FileLayer::PostFilter) continue;
        const auto kind = f.layer == dict::FileLayer::PreFilter ? filter::Source::Kind::PreFilter
                                                                 : filter::Source::Kind::PostFilter;
        sources.push_back({kind, f.name, std::string(dict::internal::WithoutHeader(f.text)), true});
    }
    out.read = dir.empty() || filter::ReadEhndFolder(dir, sources, errors);
    out.filters = filter::Build(sources, errors);
    for (const filter::Error& e : errors) out.errors.push_back(e.file + ":" + std::to_string(e.line) + ": " + e.message);
    return out;
}

}  // namespace

namespace internal {

struct SessionState {
    Memory memory;
};

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
    static std::string TranslateObserved(Session& session, std::string_view text, const StageObserver& observer) {
        return session.TranslateObserved(text, observer);
    }

    // e.mu_ is held. A later successful load supersedes this ticket.
    static bool Finish(Engine& e, std::uint64_t ticket, const UserVersion& version, dict::LoadResult r,
                       std::vector<dict::LoadError> userErrors, FilterLoad filters, bool keepFailed) {
        if (ticket < e.current_) return Loaded(r);
        UserWatch& w = *e.watch_;
        const bool allFailed = !userErrors.empty() && std::all_of(version.begin(), version.end(),
            [&userErrors](const auto& source) {
                return std::any_of(userErrors.begin(), userErrors.end(), [&source](const dict::LoadError& error) {
                    return error.file == source.first;
                });
            });
        w.errors = std::move(userErrors);
        if (!Loaded(r) || (keepFailed && allFailed)) {
            w.ehnd.failed = std::move(filters.version);
            w.ehnd.hasFailed = true;
            w.watch.failed = version;
            w.watch.hasFailed = true;
            for (dict::LoadError& error : r.errors) {
                if (dict::FailsLoad(error.kind)) w.errors.push_back(std::move(error));
            }
            return false;
        }
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
                                      const std::vector<std::string>& paths,
                                      std::vector<dict::LoadError>& userErrors) {
        auto files = dict::UserFiles(paths);
        for (;;) {
            auto result = dict::LoadFolders(dirs, {}, verify, cacheWrite, files);
            if (Loaded(result)) return result;
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
    static FilterLoad FiltersOf(const dict::LoadResult& r, const std::string& dir) {
        if (!Loaded(r)) {
            FilterLoad failed;
            failed.version = FolderVersionOf(dir);
            return failed;
        }
        return LoadFilters(r.snapshot.get(), dir);
    }

    static bool Load(Engine& e, const std::vector<std::string>& dirs, bool verify,
                     const std::vector<std::string>& userPaths, bool open) {
        std::uint64_t ticket;
        std::string ehnd;
        {
            std::lock_guard<std::mutex> lock(e.mu_);
            ticket = ++e.loads_;
            ehnd = e.ehndDir_;
        }
        const bool cacheWrite = e.cacheWrite_ == CacheWrite::Sync;
        const UserVersion version = UserVersionOf(userPaths);
        std::vector<dict::LoadError> userErrors;
        dict::LoadResult r = LoadUsers(dirs, verify, cacheWrite, userPaths, userErrors);
        FilterLoad filters = FiltersOf(r, ehnd);
        std::lock_guard<std::mutex> lock(e.mu_);
        if (!Finish(e, ticket, version, std::move(r), std::move(userErrors), std::move(filters), !open)) return false;
        if (open) {
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
        UserVersion found;
        FolderVersion foundEhnd;
        const bool user = !e.userPaths_.empty() && w.watch.Due(e.userPaths_, now, interval, found);
        const bool ehnd = !e.ehndDir_.empty() && w.ehnd.Due(e.ehndDir_, now, interval, foundEhnd);
        if (!user && !ehnd) return false;
        w.worker.Join();
        const std::uint64_t ticket = ++e.loads_;
        // The thread reaches the engine only through target, which the engine clears
        // before it goes; it never needs the engine to wait for it.
        w.running = w.worker.Start([target = w.target, ticket, dirs = e.dirs_, verify = e.verifyDicts_,
                                    paths = e.userPaths_, ehndDir = e.ehndDir_,
                                    cacheWrite = e.cacheWrite_ == CacheWrite::Sync] {
            const UserVersion version = UserVersionOf(paths);
            std::vector<dict::LoadError> userErrors;
            dict::LoadResult r = LoadUsers(dirs, verify, cacheWrite, paths, userErrors);
            FilterLoad filters = FiltersOf(r, ehndDir);
            std::lock_guard<std::mutex> owner(target->mu);
            if (!target->engine) return;
            Engine& engine = *target->engine;
            std::lock_guard<std::mutex> lock(engine.mu_);
            Finish(engine, ticket, version, std::move(r), std::move(userErrors), std::move(filters), true);
            engine.watch_->running = false;
            engine.watch_->done.notify_all();
        });
        return w.running;
    }

    // Takes the engine's current dictionaries into a session that holds others.
    static void Refresh(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated, std::shared_ptr<const filter::Filters>& filters,
                        std::unique_ptr<SessionState>& state) {
        std::lock_guard<std::mutex> lock(e.mu_);
        StartReload(e, std::chrono::steady_clock::now(), kCheckInterval);
        if (snap == e.snapshot_ && filters == e.filters_) return;
        snap = e.snapshot_;
        generated = e.generated_;
        filters = e.filters_;
        state = std::make_unique<SessionState>();
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

    static void Current(const Engine& e, std::shared_ptr<const dict::Snapshot>& snap,
                        std::shared_ptr<Generated>& generated, std::shared_ptr<const filter::Filters>& filters) {
        std::lock_guard<std::mutex> lock(e.mu_);
        snap = e.snapshot_;
        generated = e.generated_;
        filters = e.filters_;
    }

    static bool OpenEhnd(Engine& e, const std::string& dir) {
        for (;;) {
            std::shared_ptr<const dict::Snapshot> snap;
            {
                std::lock_guard<std::mutex> lock(e.mu_);
                if (!e.snapshot_) {
                    e.ehndDir_ = dir;
                    return true;
                }
                snap = e.snapshot_;
            }
            FilterLoad filters = LoadFilters(snap.get(), dir);
            if (!filters.read) return false;
            std::lock_guard<std::mutex> lock(e.mu_);
            if (snap != e.snapshot_) continue;
            // Loads under way read the folder before; they are dropped when they finish.
            e.current_ = ++e.loads_;
            e.ehndDir_ = dir;
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
    : reference_(reference), cacheWrite_(cacheWrite), watch_(std::make_unique<internal::UserWatch>()) {
    watch_->automatic = userReload == UserReload::Auto;
    watch_->target->engine = this;
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

bool Engine::Reload() {
    std::vector<std::string> dirs;
    bool verify;
    std::vector<std::string> userPaths;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!snapshot_) return false;
        dirs = dirs_;
        verify = verifyDicts_;
        userPaths = userPaths_;
    }
    return internal::Access::Load(*this, dirs, verify, userPaths, false);
}

bool Engine::OpenEhnd(std::string_view dir) { return internal::Access::OpenEhnd(*this, std::string(dir)); }

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
    : engine_(engine), reference_(engine.reference_ || reference), cg_(cg), misread_(misread),
      state_(std::make_unique<internal::SessionState>()) {
    std::lock_guard<std::mutex> lock(engine.mu_);
    snapshot_ = engine.snapshot_;
    generated_ = engine.generated_;
    filters_ = engine.filters_;
}

Session::~Session() = default;

std::string Session::Translate(std::string_view text) { return TranslateObserved(text, {}); }

std::string Session::TranslateObserved(std::string_view text, const internal::StageObserver& observer) {
    if (engine_.watch_->automatic) internal::Access::Refresh(engine_, snapshot_, generated_, filters_, state_);
    if (!snapshot_) return {};
    std::vector<std::string> lines;
    for (const std::string_view line : text::Lines(text)) lines.emplace_back(line);
    internal::TranslationOptions options;
    options.reference = reference_;
    options.wordImprovements.cgRules = cg_;
    options.wordImprovements.misreadFilter = misread_;
    options.speech = speech_;
    options.filters = filters_.get();
    const std::vector<std::string> translated =
        internal::TranslateDocument(*snapshot_, *generated_, lines, state_->memory, observer, options);
    std::string out;
    for (std::size_t i = 0; i < translated.size(); i++) {
        if (i > 0) out += '\n';
        out += translated[i];
    }
    return out;
}

void Session::Reset() {
    std::lock_guard<std::mutex> lock(engine_.mu_);
    state_ = std::make_unique<internal::SessionState>();
    snapshot_ = engine_.snapshot_;
    generated_ = engine_.generated_;
    filters_ = engine_.filters_;
}

namespace internal {

std::string TranslateObserved(Session& session, std::string_view text, const StageObserver& observer) {
    return Access::TranslateObserved(session, text, observer);
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
