// Unit test runner. Usage: hdtrans_tests [name-substring-filter]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "hdtest.h"

namespace {

namespace fs = std::filesystem;
using CacheListing = std::map<fs::path, std::pair<fs::file_type, fs::file_time_type>>;

fs::path EnvironmentPath(const char* name) {
#ifdef _WIN32
    const std::wstring wideName(name, name + std::strlen(name));
    const DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(wideName.c_str(), value.data(), size);
    if (written == 0 || written >= size) return {};
    value.resize(written);
    return fs::path(value);
#else
    const char* value = std::getenv(name);
    return value == nullptr ? fs::path{} : fs::path(value);
#endif
}

fs::path UserCache() {
#ifdef _WIN32
    const auto base = EnvironmentPath("LOCALAPPDATA");
    return base.empty() ? fs::path{} : base / "hdtrans" / "cache";
#elif defined(__APPLE__)
    const auto home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / "Library" / "Caches" / "hdtrans";
#else
    const auto xdg = EnvironmentPath("XDG_CACHE_HOME");
    if (!xdg.empty()) return xdg / "hdtrans";
    const auto home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / ".cache" / "hdtrans";
#endif
}

bool ListCache(const fs::path& root, CacheListing& out) {
    if (root.empty()) return false;
    std::error_code ec;
    if (!fs::exists(root, ec)) return !ec;
    auto add = [&](const fs::path& path) {
        const auto type = fs::symlink_status(path, ec).type();
        if (ec) return false;
        const auto time = fs::last_write_time(path, ec);
        if (ec) return false;
        out.emplace(path.lexically_relative(root), std::pair{type, time});
        return true;
    };
    if (!add(root)) return false;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!add(it->path())) return false;
    }
    return !ec;
}

bool EnvironmentPresent(const char* name) {
#ifdef _WIN32
    const std::wstring wideName(name, name + std::strlen(name));
    SetLastError(ERROR_SUCCESS);
    GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    return GetLastError() != ERROR_ENVVAR_NOT_FOUND;
#else
    return std::getenv(name) != nullptr;
#endif
}

bool SetEnvironmentPath(const char* name, const fs::path* value) {
#ifdef _WIN32
    const std::wstring wideName(name, name + std::strlen(name));
    if (_wputenv_s(wideName.c_str(), value == nullptr ? L"" : value->c_str()) != 0) return false;
    return SetEnvironmentVariableW(wideName.c_str(), value == nullptr ? nullptr : value->c_str()) != 0;
#else
    return (value == nullptr ? unsetenv(name) : setenv(name, value->c_str(), 1)) == 0;
#endif
}

struct SavedVariable {
    const char* name;
    fs::path value;
    bool present;
};

struct CacheGuard {
    fs::path real = UserCache();
    fs::path temporary;
    CacheListing before;
    std::vector<SavedVariable> saved;
    bool initialized = false;
    bool watchReal = true;

    bool Init() {
        const auto requested = EnvironmentPath("HDTRANS_CACHE_DIR");
        if (!requested.empty() && !real.empty()) {
            std::error_code ec;
            const auto canonicalReal = fs::weakly_canonical(real, ec);
            if (ec) return false;
            const auto canonicalRequested = fs::weakly_canonical(requested, ec);
            if (ec) return false;
            watchReal = canonicalReal != canonicalRequested;
        }
        if (watchReal && !ListCache(real, before)) return false;
        for (const char* name : {"HOME", "LOCALAPPDATA", "XDG_CACHE_HOME", "HDTRANS_CACHE_DIR", "HDTRANS_SNAPSHOT", "HDTRANS_VERIFY_DICTS"}) {
            saved.push_back({name, EnvironmentPath(name), EnvironmentPresent(name)});
        }
        const bool supplied = EnvironmentPresent("HDTRANS_CACHE_DIR");
        std::error_code ec;
        const auto base = fs::temp_directory_path(ec);
        if (ec) return false;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            const auto candidate = base / ("hdtrans-tests-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (fs::create_directory(candidate, ec)) { temporary = candidate; break; }
            if (ec) return false;
        }
        if (temporary.empty()) return false;
#ifndef _WIN32
        fs::permissions(temporary, fs::perms::owner_all, fs::perm_options::replace, ec);
        if (ec) return false;
#endif
        const char* names[] = {"HOME", "LOCALAPPDATA", "XDG_CACHE_HOME"};
        const char* folders[] = {"home", "local", "xdg"};
        for (size_t i = 0; i < 3; ++i) {
            const auto path = temporary / folders[i];
            if (!fs::create_directory(path, ec) || ec || !SetEnvironmentPath(names[i], &path)) return false;
        }
        if (!supplied) {
            const auto cache = temporary / "cache";
            if (!fs::create_directory(cache, ec) || ec || !SetEnvironmentPath("HDTRANS_CACHE_DIR", &cache)) return false;
        }
        if (!SetEnvironmentPath("HDTRANS_SNAPSHOT", nullptr) || !SetEnvironmentPath("HDTRANS_VERIFY_DICTS", nullptr)) return false;
        initialized = true;
        return true;
    }

    bool Unchanged() const {
        if (!watchReal) return true;
        CacheListing after;
        if (!ListCache(real, after)) {
            std::fprintf(stderr, "cannot inspect per-user cache after the run\n");
            return false;
        }
        if (after == before) return true;
        int shown = 0;
        for (const auto& [path, metadata] : before) {
            const auto found = after.find(path);
            if ((found == after.end() || found->second != metadata) && shown++ < 10) {
                std::fprintf(stderr, "cache entry changed: %s; mtime ticks %lld -> %lld\n", path.string().c_str(),
                             static_cast<long long>(metadata.second.time_since_epoch().count()),
                             found == after.end() ? 0LL : static_cast<long long>(found->second.second.time_since_epoch().count()));
            }
        }
        for (const auto& [path, metadata] : after) {
            if (!before.contains(path) && shown++ < 10) {
                std::fprintf(stderr, "cache entry added: %s; mtime ticks %lld\n", path.string().c_str(),
                             static_cast<long long>(metadata.second.time_since_epoch().count()));
            }
        }
        return false;
    }

    ~CacheGuard() {
        bool ok = !initialized || Unchanged();
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
            ok = SetEnvironmentPath(it->name, it->present ? &it->value : nullptr) && ok;
        }
        if (!temporary.empty()) {
            std::error_code ec;
            fs::remove_all(temporary, ec);
            ok = !ec && ok;
        }
        if (!ok) {
            std::fprintf(stderr, "FAIL runner cache isolation or cleanup\n");
            std::fflush(nullptr);
            std::_Exit(1);
        }
    }
};

CacheGuard* cacheGuard = nullptr;
void CleanupCache() { delete cacheGuard; }

}  // namespace

int main(int argc, char** argv) {
    cacheGuard = new CacheGuard;
    // Register before tests create static snapshots, so mapped files close before cleanup.
    if (std::atexit(CleanupCache) != 0 || !cacheGuard->Init()) {
        std::fprintf(stderr, "FAIL runner cache initialization\n");
        return 1;
    }
    const char* filter = argc > 1 ? argv[1] : nullptr;
    const bool requireAll = hdtest::RequireAll();
    const char* exclude = std::getenv("HDTRANS_TEST_EXCLUDE");
    const std::string excluded = "," + std::string(exclude ? exclude : "") + ",";
    int run = 0, failed = 0, skipped = 0;
    for (const auto& t : hdtest::Registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        hdtest::Cur() = {};
        const auto started = std::chrono::steady_clock::now();
        if (excluded.find("," + std::string(t.name) + ",") != std::string::npos)
            hdtest::Skip("excluded by HDTRANS_TEST_EXCLUDE");
        else
            t.fn();
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        run++;
        if (hdtest::Cur().skipped) {
            skipped++;
            std::printf("SKIP %s: %s (%.3fs)\n", t.name, hdtest::Cur().skipReason.c_str(), elapsed);
        }
        if (hdtest::Cur().failures) {
            failed++;
            std::printf("FAIL %s (%.3fs)\n", t.name, elapsed);
        } else if (!hdtest::Cur().skipped) {
            std::printf("ok   %s (%.3fs)\n", t.name, elapsed);
        }
    }
    if (!cacheGuard->Unchanged()) {
        failed++;
        std::fprintf(stderr, "FAIL runner: per-user cache changed during the run\n");
    }
    if (run == 0) std::fprintf(stderr, "No tests matched%s%s\n", filter ? ": " : "", filter ? filter : "");
    std::printf("%d run, %d failed, %d skipped\n", run, failed, skipped);
    return failed || run == 0 || (requireAll && skipped) ? 1 : 0;
}
