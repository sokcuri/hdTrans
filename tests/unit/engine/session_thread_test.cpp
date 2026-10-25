// Sessions of one Engine translating on several threads at once, with a Reload while they
// translate. Every thread must produce the same output for the same text, and the output
// must match a single-threaded run.
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/engine.h"
#include "core/dict/snapshot_file.h"
#include "core/engine/driver.h"
#include "core/engine/trace.h"
#include "core/engine/raw.h"
#include "frontend/dictionary_version.h"
#include "core/text/lines.h"
#include "hdtest.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define HDTRANS_TEST_INSTRUMENTED 1
#endif
#endif

namespace {

std::string LocalDictionary() {
    return (std::filesystem::path(__FILE__).parent_path() / "testdata" / "session").string();
}

struct DictionaryFolder {
    std::filesystem::path path;

    explicit DictionaryFolder(std::string_view rows) {
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error);
        if (error) return;
        path = root / ("hdtrans-session-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(path, error)) {
            path.clear();
            return;
        }
        std::ofstream file(path / "base.tsv", std::ios::binary);
        file << rows;
        HD_CHECK(file.good());
    }

    ~DictionaryFolder() {
        std::error_code error;
        if (!path.empty()) std::filesystem::remove_all(path, error);
    }
};

struct SessionCacheVariable {
#ifdef _WIN32
    std::wstring name, previous;
#else
    std::string name, previous;
#endif
    bool present = false;

    SessionCacheVariable(std::string_view key, const std::filesystem::path* value) : name(key.begin(), key.end()) {
#ifdef _WIN32
        SetLastError(ERROR_SUCCESS);
        const DWORD size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
        present = size != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        if (size != 0) {
            previous.resize(size);
            previous.resize(GetEnvironmentVariableW(name.c_str(), previous.data(), size));
        }
        HD_CHECK(SetEnvironmentVariableW(name.c_str(), value ? value->c_str() : nullptr) != 0 ||
                 (!value && GetLastError() == ERROR_ENVVAR_NOT_FOUND));
#else
        const char* old = std::getenv(name.c_str());
        present = old != nullptr;
        if (old != nullptr) previous = old;
        HD_CHECK((value ? setenv(name.c_str(), value->c_str(), 1) : unsetenv(name.c_str())) == 0);
#endif
    }

    ~SessionCacheVariable() {
#ifdef _WIN32
        HD_CHECK(SetEnvironmentVariableW(name.c_str(), present ? previous.c_str() : nullptr) != 0 ||
                 (!present && GetLastError() == ERROR_ENVVAR_NOT_FOUND));
#else
        HD_CHECK((present ? setenv(name.c_str(), previous.c_str(), 1) : unsetenv(name.c_str())) == 0);
#endif
    }
};

}  // namespace

HD_TEST(engine_session_threads) {
    const std::string dicts = LocalDictionary();
    std::error_code ec;
    HD_REQUIRE(!dicts.empty());
    HD_REQUIRE(std::filesystem::exists(dicts, ec));
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({dicts}));
    const std::string text = "学生\n学生\nASCII\n";

    std::string expected;
    {
        hdTrans::Session session(engine);
        expected = session.Translate(text);
    }
    HD_CHECK_EQ(expected, std::string("학생님\n학생님\nASCII\n"));

    constexpr int kThreads = 4;
    constexpr int kRounds = 3;
    std::vector<std::string> results(kThreads * kRounds);
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; t++) {
        workers.emplace_back([&engine, &text, &results, t] {
            for (int r = 0; r < kRounds; r++) {
                hdTrans::Session session(engine);
                results[static_cast<std::size_t>(t * kRounds + r)] = session.Translate(text);
            }
        });
    }
    // Reloads swap the dictionaries under the sessions; each session keeps its own.
    std::atomic<bool> reloaded{true};
    workers.emplace_back([&engine, &reloaded] {
        for (int r = 0; r < 2; r++) {
            if (!engine.Reload()) reloaded = false;
        }
    });
    for (std::thread& w : workers) w.join();
    HD_CHECK(reloaded.load());

    int differing = 0;
    for (const std::string& got : results) {
        if (got != expected) differing++;
    }
    HD_CHECK_EQ(differing, 0);
}

HD_TEST(engine_session_threads_stress) {
    const struct { const char* text; const char* expected; } cases[] = {
        {"学生", "학생님"}, {"がぼっ！", "가봇!"}, {"ASCII", "ASCII"}, {"학생", "학생"}, {"123", "123"},
    };
    std::vector<std::string> lines, expected;
    for (std::size_t i = 0; i < 1000; i++) {
        const auto& c = cases[i % std::size(cases)];
        lines.emplace_back(c.text);
        expected.emplace_back(c.expected);
    }
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    auto translateAll = [&engine, &lines] {
        std::vector<std::string> out;
        out.reserve(lines.size());
        for (const std::string& line : lines) {
            hdTrans::Session session(engine);
            out.push_back(session.Translate(line));
        }
        return out;
    };
    HD_CHECK(translateAll() == expected);
    constexpr int kThreads = 4;
    std::vector<std::vector<std::string>> got(kThreads);
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; t++) {
        workers.emplace_back([&got, &translateAll, t] { got[static_cast<std::size_t>(t)] = translateAll(); });
    }
    for (std::thread& w : workers) w.join();
    int differing = 0;
    for (const auto& out : got) {
        HD_REQUIRE(out.size() == expected.size());
        for (std::size_t i = 0; i < out.size(); i++) {
            if (out[i] != expected[i]) differing++;
        }
    }
    HD_CHECK_EQ(differing, 0);
}

// A call is translated on its own: what an earlier call of the session held (a speech cue,
// a reading) does not reach it, and the lines of one call are read together.
HD_TEST(engine_session_call_context) {
    const std::string dicts = LocalDictionary();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({dicts}));
    const std::vector<std::string> lines = {"「あたし」", "お兄様！", "|宝生《たからお》", "宝生ミナ"};
    hdTrans::Session session(engine);
    for (const std::string& line : lines) {
        hdTrans::Session fresh(engine);
        HD_CHECK_EQ(session.Translate(line), fresh.Translate(line));
    }
    HD_CHECK_EQ(session.Translate("お兄様！"), "오라버니!");
    HD_CHECK(session.Translate("「あたし」\nお兄様！").ends_with("\n오빠!"));
}

HD_TEST(engine_reference_mode) {
    const std::string dicts = LocalDictionary();
    hdTrans::Engine improved(false, hdTrans::CacheWrite::Off);
    hdTrans::Engine reference(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(improved.Open({dicts}));
    HD_REQUIRE(reference.Open({dicts}));
    const std::string text = "がぼっ！";
    hdTrans::Session normal(improved), explicitReference(improved, true), inherited(reference);
    HD_CHECK_EQ(normal.Translate(text), "커헉!");
    HD_CHECK_EQ(explicitReference.Translate(text), "가봇!");
    HD_CHECK_EQ(inherited.Translate(text), "가봇!");
    explicitReference.Reset();
    inherited.Reset();
    HD_CHECK_EQ(explicitReference.Translate(text), "가봇!");
    HD_CHECK_EQ(inherited.Translate(text), "가봇!");
    HD_CHECK_EQ(hdTrans::internal::TranslateRaw(reference, "ASCII"), "ASCII");
    HD_CHECK_EQ(hdTrans::internal::TranslateRaw(improved, "ASCII"), "ASCII");

    const auto snap = improved.Dictionaries();
    const auto generated = hdTrans::internal::NewGenerated(*snap);
    hdTrans::internal::TranslationOptions options;
    options.reference = true;
    options.wordImprovements.extraBuiltin = true;
    HD_CHECK_EQ(hdTrans::internal::TranslateDocument(*snap, *generated, {&text, 1}, {}, options)[0],
                "가봇!");
    options.reference = false;
    HD_CHECK_EQ(hdTrans::internal::TranslateDocument(*snap, *generated, {&text, 1}, {}, options)[0],
                "커헉!");
}

HD_TEST(engine_reference_session_builtin_isolation) {
    const std::string dicts = LocalDictionary();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({dicts}));
    hdTrans::Session improved(engine);
    hdTrans::Session reference(engine, true);
    HD_CHECK_EQ(improved.Translate("がぼっ！"), "커헉!");
    HD_CHECK_EQ(reference.Translate("がぼっ！"), "가봇!");
    reference.Reset();
    HD_CHECK_EQ(reference.Translate("がぼっ！"), "가봇!");
    HD_CHECK_EQ(improved.Translate("がぼっ！"), "커헉!");
}

HD_TEST(engine_reference_session_cleanup_isolation) {
    DictionaryFolder dictionary(
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "何度も\t몇번\tN110\tXXX\t\tGN\t0\t1\n");
    HD_REQUIRE(!dictionary.path.empty());
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({dictionary.path.string()}));
    hdTrans::Session improved(engine);
    hdTrans::Session reference(engine, true);
    HD_CHECK_EQ(improved.Translate("何度も"), "몇 번");
    HD_CHECK_EQ(reference.Translate("何度も"), "몇번");
    HD_CHECK_EQ(improved.Translate("몇번"), "몇번");
    reference.Reset();
    HD_CHECK_EQ(reference.Translate("何度も"), "몇번");
    HD_CHECK_EQ(improved.Translate("何度も"), "몇 번");
}

HD_TEST(engine_megabyte_line) {
    const std::string dicts = LocalDictionary();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({dicts}));
    std::string line;
    while (line.size() < 1024 * 1024) line += "あいうえ。 ";
    std::string expected;
    for (const std::string_view piece : hdTrans::text::TranslationPieces(line)) {
        hdTrans::Session fresh(engine);
        expected += fresh.Translate(piece);
    }
    const auto start = std::chrono::steady_clock::now();
    hdTrans::Session session(engine);
    HD_CHECK_EQ(session.Translate(line), expected);
    std::printf("megabyte line: %.3f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    const std::string ascii(1024 * 1024, 'a');
    HD_CHECK_EQ(hdTrans::internal::TranslateRaw(engine, ascii), ascii);
    std::string symbols;
    while (symbols.size() < 100000) symbols += "0123456789., ";
    const std::string translated = session.Translate(symbols);
    for (char c : std::string_view("0123456789.,")) {
        HD_CHECK_EQ(std::count(translated.begin(), translated.end(), c), std::count(symbols.begin(), symbols.end(), c));
    }
}

HD_TEST(engine_quoted_pieces) {
    const std::string dicts = LocalDictionary();
    HD_REQUIRE(!dicts.empty());
    hdTrans::Engine engine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({dicts}));
    std::string line;
    const std::string unit = "「今日は天気です。明日も晴れるでしょう。」と彼は言った。";
    while (line.size() < 70000) line += unit;
    std::string expected;
    for (const auto piece : hdTrans::text::TranslationPieces(line)) {
        HD_CHECK_EQ(piece.size() % unit.size(), std::size_t{0});
        hdTrans::Session fresh(engine);
        expected += fresh.Translate(piece);
    }
    hdTrans::Session session(engine);
    HD_CHECK_EQ(session.Translate(line), expected);
}

HD_TEST(engine_session_fallback) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path temporary = fs::temp_directory_path(ec);
    HD_REQUIRE(!ec);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{temporary / ("hdtrans-session-fallback-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    HD_REQUIRE(fs::create_directory(cleanup.path, ec) && !ec);
    const SessionCacheVariable cache("HDTRANS_CACHE_DIR", &cleanup.path);
    const SessionCacheVariable enabled("HDTRANS_SNAPSHOT", nullptr);
    const SessionCacheVariable lazy("HDTRANS_VERIFY_DICTS", nullptr);
    const auto pathText = [](const fs::path& path) {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    };
    const std::vector<std::string> folders{LocalDictionary()};
    hdTrans::Engine textEngine(true, hdTrans::CacheWrite::Off);
    HD_REQUIRE(textEngine.Open(folders));
    HD_REQUIRE(!textEngine.Dictionaries()->Mapped());
    hdTrans::Session text(textEngine);
    const std::string probe = "「あたし」\n学生 お兄様！";
    const std::string expected = text.Translate(probe);
    HD_CHECK(expected.ends_with("\n학생님 오빠!"));

    hdTrans::Engine engine(true);
    HD_REQUIRE(engine.Open(folders));
    HD_REQUIRE(engine.Open(folders));
    auto snapshot = engine.Dictionaries();
    HD_REQUIRE(snapshot->Mapped());
    hdTrans::Session session(engine);
    HD_CHECK_EQ(session.Translate(probe), expected);
    HD_REQUIRE(snapshot->Healthy());

    const auto index = hdTrans::dict::internal::SnapshotPath(folders, pathText(cleanup.path));
    const std::string resource = hdTrans::dict::internal::SnapshotResourcePath(index);
    HD_REQUIRE(!resource.empty());
    const fs::path file(std::u8string(reinterpret_cast<const char8_t*>(resource.data()), resource.size()));
    std::ifstream input(file, std::ios::binary);
    HD_REQUIRE(input.good());
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    HD_REQUIRE(bytes.size() >= 256);
    const auto field = [&](std::size_t at, std::size_t width) {
        uint64_t value = 0;
        for (std::size_t i = 0; i < width; i++) {
            value |= static_cast<uint64_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
        }
        return value;
    };
    const uint64_t sections = field(40, 4);
    HD_REQUIRE(sections <= (bytes.size() - 256) / 64);
    std::size_t offset = 0;
    for (std::size_t i = 0; i < sections; i++) {
        const std::size_t at = 256 + 64 * i;
        // The base layer's byte column is checked when a matching record is read.
        if (field(at, 4) == 17 && field(at + 4, 4) == 0) {
            offset = static_cast<std::size_t>(field(at + 8, 8));
        }
    }
    HD_REQUIRE(offset != 0 && offset < bytes.size());
    std::fstream edit(file, std::ios::in | std::ios::out | std::ios::binary);
    HD_REQUIRE(edit.good());
    edit.seekp(static_cast<std::streamoff>(offset));
    edit.put(static_cast<char>(static_cast<unsigned char>(bytes[offset]) ^ 1u));
    edit.close();
    HD_REQUIRE(edit.good());
    HD_CHECK_EQ(session.Translate(probe), expected);
    HD_CHECK(!snapshot->Healthy());
    const std::weak_ptr<const hdTrans::dict::Snapshot> cachedFallback = snapshot->Fallback();
    for (int call = 0; call < 3; call++) {
        const auto cached = cachedFallback.lock();
        HD_REQUIRE(cached != nullptr);
        HD_CHECK(cached == snapshot->Fallback());
        auto start = std::chrono::steady_clock::now();
        const std::string wanted = text.Translate("お兄様！");
        const double normalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        start = std::chrono::steady_clock::now();
        HD_CHECK_EQ(session.Translate("お兄様！"), wanted);
        const double fallbackMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("session fallback call %d: %.3f ms, text %.3f ms\n", call + 1, fallbackMs, normalMs);
#if defined(NDEBUG) && !defined(HDTRANS_TEST_INSTRUMENTED) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
        HD_CHECK(fallbackMs < std::max(20.0, normalMs * 20.0));
#endif
    }
#if !defined(NDEBUG) || defined(HDTRANS_TEST_INSTRUMENTED) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    std::printf("fallback timing assertion inactive: Debug or sanitizer build\n");
#endif
    HD_REQUIRE(engine.Reload());
    snapshot.reset();
    session.Reset();
    hdTrans::Session fresh(textEngine);
    HD_CHECK_EQ(session.Translate("お兄様！"), fresh.Translate("お兄様！"));
}

HD_TEST(session_explicit_observer_is_call_local) {
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    hdTrans::Session session(engine), independent(engine);
    struct Trace {
        int line = -1;
        int calls = 0;
        std::vector<std::string> surfaces;
        std::vector<std::string> logs;
    } trace;
    hdTrans::internal::StageObserver observer;
    observer.context = &trace;
    observer.line = [](void* context, int line) { static_cast<Trace*>(context)->line = line; };
    observer.resolved = [](void* context, int, int, const hdTrans::word::Chain& chain) {
        auto& out = *static_cast<Trace*>(context);
        ++out.calls;
        for (const auto* item = chain.head; item; item = item->next) {
            if (item->group) out.surfaces.push_back(item->group->surface);
        }
    };
    observer.filterLog = [](void* context, std::string_view message) {
        static_cast<Trace*>(context)->logs.emplace_back(message);
    };
    const std::string input = "学生\nお兄様";
    const std::string ordinary = session.Translate(input);
    session.Reset();
    HD_CHECK_EQ(hdTrans::internal::TranslateObserved(session, input, observer), ordinary);
    HD_CHECK_EQ(trace.line, 1);
    HD_CHECK(trace.calls > 0);
    HD_CHECK(std::find(trace.surfaces.begin(), trace.surfaces.end(), "学生") != trace.surfaces.end());
    const int calls = trace.calls;
    HD_CHECK(!trace.logs.empty());
    const std::size_t logCount = trace.logs.size();
    session.Reset();
    HD_CHECK_EQ(session.Translate(input), ordinary);
    HD_CHECK_EQ(independent.Translate(input), ordinary);
    HD_CHECK_EQ(trace.calls, calls);
    HD_CHECK_EQ(trace.logs.size(), logCount);
    Trace other;
    observer.context = &other;
    independent.Reset();
    HD_CHECK_EQ(hdTrans::internal::TranslateObserved(independent, input, observer), ordinary);
    HD_CHECK(other.calls > 0);
    HD_CHECK_EQ(trace.calls, calls);
    HD_CHECK_EQ(trace.logs.size(), logCount);
}

HD_TEST(session_trace_collector_forwards_and_offsets) {
    hdTrans::Engine engine(false, hdTrans::CacheWrite::Off);
    HD_REQUIRE(engine.Open({LocalDictionary()}));
    hdTrans::Session session(engine);
    hdTrans::internal::TraceCollector collector;
    collector.lineBase = 4;
    int forwarded = 0;
    hdTrans::internal::StageObserver downstream;
    downstream.context = &forwarded;
    downstream.line = [](void* context, int) { ++*static_cast<int*>(context); };
    downstream.resolved = [](void* context, int, int, const hdTrans::word::Chain&) {
        ++*static_cast<int*>(context);
    };
    downstream.filterLog = [](void* context, std::string_view) { ++*static_cast<int*>(context); };
    collector.result.text = hdTrans::internal::TranslateObserved(session, "学生\nお兄様",
        hdTrans::internal::TraceObserver(collector, downstream));
    HD_CHECK_EQ(collector.result.text, "학생님\n오라버니");
    HD_REQUIRE(collector.result.choices.size() >= 2);
    HD_CHECK_EQ(collector.result.choices.front().line, 4);
    HD_CHECK_EQ(collector.result.choices.back().line, 5);
    HD_CHECK_EQ(collector.result.choices.front().surface, "学生");
    HD_CHECK_EQ(collector.result.choices.front().choice.text, "학생님");
    HD_CHECK_EQ(collector.result.choices.front().choice.provenance, "Single");
    HD_CHECK(forwarded >= 4);
    HD_CHECK(!collector.result.logs.empty());
    const int count = forwarded;
    session.Reset();
    HD_CHECK_EQ(session.Translate("学生"), "학생님");
    HD_CHECK_EQ(forwarded, count);
}

HD_TEST(dictionary_version_follows_base_data_path) {
    DictionaryFolder root("unused");
    HD_REQUIRE(!root.path.empty());
    const auto child = root.path / std::filesystem::path(u8"辞書");
    const auto other = root.path / "other";
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(child, error));
    HD_REQUIRE(std::filesystem::create_directory(other, error));
    const auto utf8 = [](const std::filesystem::path& path) {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    };
    const auto version = [&](const std::filesystem::path& path) {
        return hdTrans::frontend::DictionaryVersion({utf8(path)});
    };
    HD_CHECK_EQ(version(child), "development (unversioned)");
    std::ofstream(root.path / "dicts.txt", std::ios::binary) << "parent-v1\r\nignored\n";
    HD_CHECK_EQ(version(child), "parent-v1");
    std::ofstream(child / "dicts.txt", std::ios::binary) << "\xEF\xBB\xBF" "child-v2\n";
    HD_CHECK_EQ(version(child), "child-v2");
    HD_CHECK_EQ(hdTrans::frontend::DictionaryVersion({utf8(child) + "/"}), "child-v2");
    for (const char* name : {"bundle.hdsnap", "bundle.HdSnAp", "bundle.HDSNAP"}) {
        std::ofstream(root.path / name, std::ios::binary) << "snapshot";
        HD_CHECK_EQ(version(root.path / name), "parent-v1");
    }
    HD_CHECK_EQ(version(other), "parent-v1");
    HD_CHECK_EQ(hdTrans::frontend::DictionaryVersion({utf8(child), utf8(other)}), "child-v2");
    HD_CHECK_EQ(hdTrans::frontend::DictionaryVersion({}), "development (unversioned)");
}
