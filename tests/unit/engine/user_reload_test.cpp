// The automatic reload of the user dictionary file.
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "core/config/config.h"
#include "core/dict/folders.h"
#include "core/engine.h"
#include "core/engine/driver.h"
#include "core/engine/load_info.h"
#include "core/engine/user_watch.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace std::chrono_literals;

namespace {

std::string Fixture() {
    return (std::filesystem::path(__FILE__).parent_path() / "testdata" / "session").string();
}

struct TempFolder {
    std::filesystem::path path;
    TempFolder() {
        std::error_code ec;
        path = std::filesystem::temp_directory_path(ec) /
               ("hdtrans-userdict-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(path, ec);
    }
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

bool Write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    return f.good();
}

dict::LoadErrorKind UserErrorKind(const Engine& engine) {
    const auto errors = internal::UserFileErrors(engine);
    HD_CHECK(!errors.empty());
    return errors.empty() ? dict::LoadErrorKind::Read : errors.front().kind;
}

}  // namespace

HD_TEST(UserFileWatchDue) {
    TempFolder temp;
    const std::string path = (temp.path / "userdict.tsv").string();
    internal::FileWatch w;
    internal::FileVersion found;
    const auto t0 = std::chrono::steady_clock::now();
    // A missing file is the version of no file.
    HD_CHECK(!w.Due(path, t0, 1s, found));
    HD_REQUIRE(Write(path, "猫\t고양이\n"));
    // At most one check per interval.
    HD_CHECK(!w.Due(path, t0 + 500ms, 1s, found));
    HD_CHECK(w.Due(path, t0 + 1s, 1s, found));
    HD_CHECK(found.present);
    HD_CHECK_EQ(found.stamp.size, uint64_t{14});
    // A version that failed is not due again until the file changes.
    w.failed = found;
    w.hasFailed = true;
    HD_CHECK(!w.Due(path, t0 + 2s, 1s, found));
    HD_REQUIRE(Write(path, "猫\t냥이\n"));
    HD_CHECK(w.Due(path, t0 + 3s, 1s, found));
    w.loaded = found;
    w.hasFailed = false;
    HD_CHECK(!w.Due(path, t0 + 4s, 1s, found));
    std::error_code ec;
    std::filesystem::remove(path, ec);
    HD_CHECK(w.Due(path, t0 + 5s, 1s, found));
    HD_CHECK(!found.present);
}

HD_TEST(UserFileReload) {
    TempFolder temp;
    const auto user = temp.path / "userdict.tsv";
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    Session session(engine);
    const std::string before = session.Translate("猫");
    HD_CHECK(before.find("고양이") == std::string::npos);
    HD_CHECK(!internal::CheckUserFile(engine));

    // A new file is loaded and taken by the session.
    HD_REQUIRE(Write(user, "# comment\n猫\t고양이\n"));
    const auto old = engine.Dictionaries();
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() != old);
    HD_CHECK(session.Translate("猫").find("고양이") != std::string::npos);

    // A file that fails to load keeps the dictionaries, and is not retried until it changes.
    HD_REQUIRE(Write(user, "猫\n"));
    const auto good = engine.Dictionaries();
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == good);
    const auto errors = internal::UserFileErrors(engine);
    HD_REQUIRE(errors.size() == 1);
    HD_CHECK(errors[0].kind == dict::LoadErrorKind::Plain);
    HD_CHECK_EQ(errors[0].file, user.string());
    HD_CHECK(!internal::CheckUserFile(engine));
    HD_CHECK(session.Translate("猫").find("고양이") != std::string::npos);
    HD_REQUIRE(Write(user, "猫\t냥이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(internal::UserFileErrors(engine).empty());
    HD_CHECK(session.Translate("猫").find("냥이") != std::string::npos);

    // A translation starts the check once the interval has passed; the new dictionaries
    // replace the old ones when loaded.
    HD_REQUIRE(Write(user, "猫\t야옹이\n"));
    const auto loaded = engine.Dictionaries();
    std::this_thread::sleep_for(1100ms);
    session.Translate("猫");
    for (int i = 0; i < 500 && engine.Dictionaries() == loaded; i++) std::this_thread::sleep_for(10ms);
    HD_CHECK(engine.Dictionaries() != loaded);
    HD_CHECK(session.Translate("猫").find("야옹이") != std::string::npos);

    // A still-configured explicit file that disappears keeps the last complete stack.
    const auto beforeRemoval = engine.Dictionaries();
    const auto filters = engine.Filters();
    std::error_code ec;
    HD_REQUIRE(std::filesystem::remove(user, ec));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == beforeRemoval);
    HD_CHECK(engine.Filters() == filters);
    HD_CHECK_EQ(session.Translate("猫"), "야옹이");
    HD_CHECK_EQ(engine.UserFileError(), user.string() + ": cannot read the file");
    HD_CHECK(UserErrorKind(engine) == dict::LoadErrorKind::Read);

    // An explicit Reload keeps working.
    HD_REQUIRE(Write(user, "猫\t고양이\n"));
    HD_CHECK(engine.Reload());
    HD_CHECK(engine.Dictionaries() != beforeRemoval);
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK(!internal::CheckUserFile(engine));
    HD_CHECK(session.Translate("猫").find("고양이") != std::string::npos);

    // Without UserReload::Auto nothing is checked.
    Engine manual(false, CacheWrite::Off);
    HD_REQUIRE(manual.Open({Fixture()}, false, user.string()));
    HD_REQUIRE(Write(user, "猫\t냥이\n"));
    HD_CHECK(!internal::CheckUserFile(manual));
}

// A user dictionary file that does not load is left out by Open, which still succeeds,
// and is loaded once it is fixed.
HD_TEST(UserFileBrokenAtOpen) {
    TempFolder temp;
    const auto user = temp.path / "userdict.tsv";
    HD_REQUIRE(Write(user, "# comment\n猫\n"));
    for (const UserReload mode : {UserReload::Off, UserReload::Auto}) {
        Engine engine(false, CacheWrite::Off, mode);
        HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
        HD_REQUIRE(engine.Dictionaries() != nullptr);
        HD_CHECK(engine.Dictionaries()->PlainFiles().empty());
        HD_CHECK_EQ(engine.UserFileError(), user.string() + ":2: empty KR");
        HD_CHECK(!internal::CheckUserFile(engine));
    }
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    Session session(engine);
    HD_CHECK(session.Translate("猫").find("고양이") == std::string::npos);
    HD_REQUIRE(Write(user, "猫\t고양이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK(session.Translate("猫").find("고양이") != std::string::npos);
    // A failed explicit Reload keeps the dictionaries and reports the file.
    HD_REQUIRE(Write(user, "猫\t고양이\textra\tcolumns\tmore\n"));
    const auto good = engine.Dictionaries();
    HD_CHECK(!engine.Reload());
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(engine.UserFileError().find(user.string() + ":1: ") == 0);

    // Folders that do not load still fail Open.
    Engine broken(false, CacheWrite::Off);
    HD_CHECK(!broken.Open({(temp.path / "missing").string()}, false, user.string()));
    HD_CHECK(!broken.loaded());
}

// Destroying an engine during a reload: by default it waits for the reload; after
// DetachReload it does not, and the reload finishes on its own.
HD_TEST(UserFileReloadOutlivesEngine) {
    TempFolder temp;
    const auto user = temp.path / "userdict.tsv";
    std::string big;
    for (int i = 0; i < 300000; i++) big += "猫" + std::to_string(i) + "\t고양이\n";
    HD_REQUIRE(Write(user, "猫\t고양이\n"));
    HD_REQUIRE(internal::RunningReloads() == 0);

    std::chrono::steady_clock::duration loadTime{};
    {
        Engine engine(false, CacheWrite::Off, UserReload::Auto);
        HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
        HD_REQUIRE(Write(user, big));
        const auto start = std::chrono::steady_clock::now();
        HD_CHECK(internal::CheckUserFile(engine));
        loadTime = std::chrono::steady_clock::now() - start;
        HD_REQUIRE(Write(user, "猫\t냥이\n"));
        HD_CHECK(internal::StartUserFileCheck(engine));
    }
    HD_CHECK_EQ(internal::RunningReloads(), 0);

    HD_REQUIRE(Write(user, "猫\t고양이\n"));
    std::chrono::steady_clock::duration destroyTime{};
    {
        auto engine = std::make_unique<Engine>(false, CacheWrite::Off, UserReload::Auto);
        HD_REQUIRE(engine->Open({Fixture()}, false, user.string()));
        HD_REQUIRE(Write(user, big));
        HD_CHECK(internal::StartUserFileCheck(*engine));
        const auto start = std::chrono::steady_clock::now();
        engine->DetachReload();
        engine.reset();
        destroyTime = std::chrono::steady_clock::now() - start;
    }
    HD_CHECK(destroyTime < loadTime / 4);
    for (int i = 0; i < 3000 && internal::RunningReloads() > 0; i++) std::this_thread::sleep_for(10ms);
    HD_CHECK_EQ(internal::RunningReloads(), 0);
}

HD_TEST(UserDictionaryFilesReload) {
    TempFolder temp;
    const auto first = temp.path / "userdict-a.txt";
    const auto later = temp.path / "USERDICT_Z.TSV";
    const auto broken = temp.path / "userdict-bad.txt";
    const std::vector<std::string> sources{temp.path.string()};
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(Write(first, "猫\t고양이\tn\tprio=99\n"));
    HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, sources));
    Session session(engine);
    HD_CHECK_EQ(session.Translate("猫"), "고양이");
    HD_CHECK(!internal::CheckUserFile(engine));

    HD_REQUIRE(Write(later, "猫\t냥이\tn\tprio=1\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "냥이");
    HD_REQUIRE(Write(later, "猫\t야옹이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK_EQ(session.Translate("猫"), "야옹이");

    const auto good = engine.Dictionaries();
    const auto filters = engine.Filters();
    const std::string dog = session.Translate("犬");
    HD_REQUIRE(Write(broken, "# comment\n猫\n犬\n"));
    HD_REQUIRE(Write(first, "猫\t흰고양이\n犬\t멍멍이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(engine.Filters() == filters);
    HD_CHECK_EQ(session.Translate("猫"), "야옹이");
    HD_CHECK_EQ(session.Translate("犬"), dog);
    Session fresh(engine);
    HD_CHECK_EQ(fresh.Translate("犬"), dog);
    HD_CHECK(engine.UserFileError().find(broken.string() + ":2: empty KR") != std::string::npos);
    HD_CHECK(engine.UserFileError().find(broken.string() + ":3: empty KR") != std::string::npos);
    HD_CHECK(!internal::CheckUserFile(engine));

    std::error_code error;
    HD_REQUIRE(std::filesystem::remove(later, error));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK_EQ(session.Translate("猫"), "야옹이");
    HD_REQUIRE(std::filesystem::remove(broken, error));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() != good);
    HD_CHECK_EQ(session.Translate("猫"), "흰고양이");
    HD_CHECK_EQ(session.Translate("犬"), "멍멍이");
    HD_CHECK(engine.UserFileError().empty());
    HD_REQUIRE(std::filesystem::remove(first, error));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(session.Translate("猫") != "흰고양이");
}

HD_TEST(UserDictionaryPartialReloadKeepsAllFiles) {
    TempFolder temp;
    const auto first = temp.path / "userdict-a.txt";
    const auto second = temp.path / "userdict-b.txt";
    HD_REQUIRE(Write(first, "猫\t고양이\n"));
    HD_REQUIRE(Write(second, "犬\t멍멍이\n"));
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {first.string(), second.string()}));
    Session old(engine);
    HD_CHECK_EQ(old.Translate("猫"), "고양이");
    HD_CHECK_EQ(old.Translate("犬"), "멍멍이");
    const auto good = engine.Dictionaries();
    const auto filters = engine.Filters();

    HD_REQUIRE(Write(first, "猫\t냥이\n"));
    HD_REQUIRE(Write(second, "犬\n"));
    HD_CHECK(!engine.Reload());
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(engine.Filters() == filters);
    HD_CHECK(engine.UserFileError().find(second.string() + ":1: empty KR") == 0);
    Session failed(engine);
    HD_CHECK_EQ(failed.Translate("猫"), "고양이");
    HD_CHECK_EQ(failed.Translate("犬"), "멍멍이");

    HD_REQUIRE(Write(second, "犬\t강아지\n"));
    HD_CHECK(engine.Reload());
    HD_CHECK(engine.Dictionaries() != good);
    HD_CHECK(engine.UserFileError().empty());
    Session repaired(engine);
    HD_CHECK_EQ(repaired.Translate("猫"), "냥이");
    HD_CHECK_EQ(repaired.Translate("犬"), "강아지");
    HD_CHECK_EQ(old.Translate("猫"), "고양이");
}

namespace {

void AutoReloadPinsConcurrentCalls(bool strict) {
    TempFolder temp;
    const auto first = temp.path / "userdict-a.txt";
    const auto second = temp.path / "userdict-b.txt";
    const auto ehnd = temp.path / "ehnd";
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(ehnd, error));
    HD_REQUIRE(Write(first, "猫\t고양이\n"));
    HD_REQUIRE(Write(second, "犬\t멍멍이\n"));
    HD_REQUIRE(Write(ehnd / "UserDict_a.txt", "ポチ\t바둑이\t1\n"));
    HD_REQUIRE(Write(ehnd / "PostFilter_a.txt", "고양이\t흰고양이\t1\t0\n"));
    EngineOptions options;
    options.strictDict = strict;
    Engine engine(false, CacheWrite::Off, UserReload::Auto, options);
    HD_REQUIRE(engine.OpenEhnd(ehnd.string()));
    HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {first.string(), second.string()}));
    const std::string input = "猫\n犬\nポチ";
    const std::string before = "흰고양이\n멍멍이\n바둑이";
    const auto translate = [&] {
        Session session(engine);
        return session.Translate(input);
    };
    HD_CHECK_EQ(translate(), before);
    const std::weak_ptr<const dict::Snapshot> good = engine.Dictionaries();
    const std::weak_ptr<const filter::Filters> filters = engine.Filters();

    constexpr int kThreads = 4;
    std::barrier entered(kThreads + 1), resume(kThreads + 1);
    struct Pause {
        std::barrier<>& entered;
        std::barrier<>& resume;
    } pause{entered, resume};
    internal::StageObserver observer;
    observer.context = &pause;
    observer.line = [](void* context, int line) {
        if (line != 0) return;
        auto& p = *static_cast<Pause*>(context);
        p.entered.arrive_and_wait();
        p.resume.arrive_and_wait();
    };
    std::vector<std::string> results(kThreads);
    std::vector<std::thread> workers;
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&, i] {
            Session session(engine);
            session.SetWrapped(false);
            results[i] = internal::TranslateObserved(session, input, observer);
        });
    }
    entered.arrive_and_wait();

    // All calls hold the old stack while Auto rejects an incomplete replacement.
    HD_CHECK(Write(first, "猫\t냥이\n"));
    HD_CHECK(Write(second, "\xFF"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == good.lock());
    HD_CHECK(engine.Filters() == filters.lock());
    HD_CHECK_EQ(engine.UserFileError(), second.string() + ": invalid UTF-8");
    HD_CHECK_EQ(translate(), before);
    HD_CHECK(!internal::CheckUserFile(engine));

    HD_CHECK(Write(second, "犬\t강아지\n"));
    HD_CHECK(Write(ehnd / "UserDict_a.txt", "ポチ\t흰둥이\t1\n"));
    HD_CHECK(Write(ehnd / "PostFilter_a.txt", "냥이\t검은고양이\t1\t0\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() != good.lock());
    HD_CHECK(engine.Filters() != filters.lock());
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK_EQ(translate(), "검은고양이\n강아지\n흰둥이");
    HD_CHECK(!good.expired());
    HD_CHECK(!filters.expired());
    resume.arrive_and_wait();
    for (std::thread& worker : workers) worker.join();
    for (const auto& result : results) HD_CHECK_EQ(result, before);
}

}  // namespace

HD_TEST(UserDictionaryAutoReloadPinsConcurrentCalls) {
    for (const bool strict : {false, true}) AutoReloadPinsConcurrentCalls(strict);
}

HD_TEST(UserDictionaryReloadKeepsValidRowsBesideWarning) {
    TempFolder temp;
    const auto user = temp.path / "userdict.tsv";
    const std::string header = "#! layer=user\nkey\tko\tpos\tid\n";
    HD_REQUIRE(Write(user, header + "猫\t고양이\tN110\t1\n"));
    EngineOptions options;
    options.strictDict = false;
    Engine engine(false, CacheWrite::Off, UserReload::Off, options);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    const auto good = engine.Dictionaries();

    HD_REQUIRE(Write(user, header + "猫\t냥이\tN110\t1\n"
                                  "不明\t버림\tunknown-tag\t2\n"
                                  "犬\t멍멍이\tN110\t3\n"));
    HD_CHECK(engine.Reload());
    const auto loaded = engine.Dictionaries();
    HD_CHECK(loaded != good);
    HD_REQUIRE(loaded->User() != nullptr);
    const auto cats = loaded->User()->Records("猫");
    const auto dogs = loaded->User()->Records("犬");
    HD_REQUIRE(cats.size() == 1);
    HD_REQUIRE(dogs.size() == 1);
    HD_CHECK_EQ(std::string(cats[0].target), "냥이");
    HD_CHECK_EQ(std::string(dogs[0].target), "멍멍이");
    HD_CHECK(loaded->User()->Records("不明").empty());
    HD_CHECK_EQ(engine.UserFileError(), user.string() + ":4: unknown pos: unknown-tag");
    const auto warnings = internal::UserFileErrors(engine);
    HD_REQUIRE(warnings.size() == 1);
    HD_CHECK(warnings[0].kind == dict::LoadErrorKind::UserRow);
    HD_CHECK(!dict::FailsLoad(warnings[0].kind));
}

HD_TEST(UserDictionaryJapaneseTargetLoadsWithoutError) {
    TempFolder temp;
    const auto user = temp.path / "userdict.tsv";
    HD_REQUIRE(Write(user, "#! layer=user\nkey\tko\tpos\tid\n猫\t고양이改\tN110\t1\n"));
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    HD_REQUIRE(engine.Dictionaries()->User() != nullptr);
    HD_CHECK(engine.Dictionaries()->User()->Records("猫").size() == 1);
    HD_CHECK(engine.UserFileError().empty());
}

HD_TEST(UserDictionaryBadFilesAreIsolated) {
    TempFolder temp;
    const auto first = temp.path / "userdict-a.txt";
    const auto broken = temp.path / "userdict-b.txt";
    const auto later = temp.path / "userdict-z.tsv";
    const std::vector<std::string> sources{temp.path.string()};
    HD_REQUIRE(Write(first, "猫\t고양이\n"));
    HD_REQUIRE(Write(later, "犬\t멍멍이\n"));
    for (const char* invalid : {"猫\t버려질항목\n犬\n", "#! layer=words\n猫\t고양이\n",
                                "key\tko\tpos\tid\n犬\t개\tN110\t1\textra\n"}) {
        HD_REQUIRE(Write(broken, invalid));
        Engine engine(false, CacheWrite::Off);
        HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, sources));
        Session session(engine);
        HD_CHECK_EQ(session.Translate("猫"), "고양이");
        HD_CHECK_EQ(session.Translate("犬"), "멍멍이");
        HD_CHECK(engine.UserFileError().find(broken.string() + ":") == 0);
    }
}

HD_TEST(UserDictionaryBadBodyRowDoesNotChangeRules) {
    TempFolder temp;
    const auto user = temp.path / "userdict.tsv";
    const std::string header = "#! layer=user\nkey\tko\tpos\trule\tctx\tid\n";
    HD_REQUIRE(Write(user, header + "猫\t고양이\tN110\t0\t\t1\n"));
    EngineOptions options;
    options.strictDict = false;
    Engine engine(false, CacheWrite::Off, UserReload::Off, options);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    const auto rules = engine.Dictionaries()->RuleBodies();
    HD_REQUIRE(Write(user, header + "猫\t냥이\tN110\t0\t\t1\n"
                                  "犬\t버림\tN110\t1\tNOT_A_RULE\t2\n"
                                  "鳥\t새\tN110\t0\t\t3\n"));
    HD_CHECK(engine.Reload());
    const auto loaded = engine.Dictionaries();
    HD_CHECK(loaded->RuleBodies() == rules);
    HD_REQUIRE(loaded->User() != nullptr);
    HD_CHECK(loaded->User()->Records("犬").empty());
    HD_CHECK(loaded->User()->Records("猫").size() == 1);
    HD_CHECK(loaded->User()->Records("鳥").size() == 1);
    HD_CHECK_EQ(engine.UserFileError(), user.string() + ":4: the rule body does not parse: bad atom delta");
    HD_CHECK(UserErrorKind(engine) == dict::LoadErrorKind::UserRow);
}

HD_TEST(UserDictionaryRowsFollowStrictOption) {
    for (const bool strict : {false, true}) {
        for (const auto mode : {UserReload::Off, UserReload::Auto}) {
            TempFolder temp;
            const auto first = temp.path / "userdict-a.txt";
            const auto second = temp.path / "userdict-b.txt";
            HD_REQUIRE(Write(first, "猫\t고양이\n"));
            HD_REQUIRE(Write(second, "犬\t멍멍이\n"));
            EngineOptions options;
            options.strictDict = strict;
            Engine engine(false, CacheWrite::Off, mode, options);
            HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {first.string(), second.string()}));
            const auto good = engine.Dictionaries();
            HD_REQUIRE(Write(first, "猫\t냥이\n"));
            HD_REQUIRE(Write(second, "犬\t강아지\n壊\t버림\tn\tunknown=1\n猫\t버림\tn\tx\textra\n"));
            if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload() == !strict);
            HD_CHECK((engine.Dictionaries() == good) == strict);
            Session changed(engine);
            HD_CHECK_EQ(changed.Translate("猫"), strict ? "고양이" : "냥이");
            HD_CHECK_EQ(changed.Translate("犬"), strict ? "멍멍이" : "강아지");
            HD_CHECK(engine.UserFileError().find(second.string() + ":2: ") != std::string::npos);
            HD_CHECK(engine.UserFileError().find(second.string() + ":3: ") != std::string::npos);
            const auto errors = internal::UserFileErrors(engine);
            HD_REQUIRE(errors.size() == 2);
            for (const auto& error : errors) HD_CHECK(dict::FailsLoad(error.kind) == strict);
            HD_CHECK(!internal::CheckUserFile(engine));

            HD_REQUIRE(Write(second, "犬\n猫\n"));
            if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload() == !strict);
            Session empty(engine);
            HD_CHECK_EQ(empty.Translate("猫"), strict ? "고양이" : "냥이");
            if (strict) HD_CHECK_EQ(empty.Translate("犬"), "멍멍이");
            else HD_CHECK(empty.Translate("犬") != "강아지");
            HD_REQUIRE(internal::UserFileErrors(engine).size() == 2);
            HD_CHECK(!internal::CheckUserFile(engine));

            HD_REQUIRE(Write(second, "犬\t흰둥이\n"));
            if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload());
            Session repaired(engine);
            HD_CHECK_EQ(repaired.Translate("猫"), "냥이");
            HD_CHECK_EQ(repaired.Translate("犬"), "흰둥이");
            HD_CHECK(engine.UserFileError().empty());
        }
    }
}

HD_TEST(UserDictionaryInitialRowsUseResolvedPolicy) {
    for (const bool strict : {false, true}) {
        TempFolder temp;
        const auto first = temp.path / "userdict-a.txt";
        const auto second = temp.path / "userdict-b.txt";
        HD_REQUIRE(Write(first, "猫\t고양이\n"));
        HD_REQUIRE(Write(second, "犬\t멍멍이\n猫\n"));
        EngineOptions options;
        options.strictDict = strict;
        Engine engine(false, CacheWrite::Off, UserReload::Off, options);
        HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {first.string(), second.string()}));
        Session session(engine);
        HD_CHECK_EQ(session.Translate("猫"), "고양이");
        HD_CHECK((session.Translate("犬") == "멍멍이") == !strict);
        const auto errors = internal::UserFileErrors(engine);
        HD_REQUIRE(errors.size() == 1);
        HD_CHECK_EQ(errors[0].line, 2);
        HD_CHECK(dict::FailsLoad(errors[0].kind) == strict);
    }
}

HD_TEST(UserDictionaryDecodeFailureKeepsWholeStack) {
    for (const bool strict : {false, true}) {
        for (const auto mode : {UserReload::Off, UserReload::Auto}) {
            TempFolder temp;
            const auto first = temp.path / "userdict-a.txt";
            const auto second = temp.path / "userdict-b.txt";
            HD_REQUIRE(Write(first, "猫\t고양이\n"));
            HD_REQUIRE(Write(second, "犬\t멍멍이\n"));
            EngineOptions options;
            options.strictDict = strict;
            Engine engine(false, CacheWrite::Off, mode, options);
            HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {first.string(), second.string()}));
            const auto good = engine.Dictionaries();
            const auto filters = engine.Filters();
            HD_REQUIRE(Write(first, "猫\t냥이\n"));
            HD_REQUIRE(Write(second, "\xFF"));
            if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
            else HD_CHECK(!engine.Reload());
            HD_CHECK(engine.Dictionaries() == good);
            HD_CHECK(engine.Filters() == filters);
            HD_CHECK_EQ(engine.UserFileError(), second.string() + ": invalid UTF-8");
            Session failed(engine);
            HD_CHECK_EQ(failed.Translate("猫"), "고양이");
            HD_CHECK_EQ(failed.Translate("犬"), "멍멍이");
            HD_CHECK(!internal::CheckUserFile(engine));
            HD_REQUIRE(Write(second, "犬\t강아지\n"));
            if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload());
            Session repaired(engine);
            HD_CHECK_EQ(repaired.Translate("猫"), "냥이");
            HD_CHECK_EQ(repaired.Translate("犬"), "강아지");
            HD_CHECK(engine.UserFileError().empty());
        }
    }
}

HD_TEST(UserDictionaryMissingSourceKinds) {
    TempFolder temp;
    const auto first = temp.path / "userdict-a.txt";
    const auto second = temp.path / "userdict-b.txt";
    const auto away = temp.path / "saved.txt";
    HD_REQUIRE(Write(first, "猫\t고양이\n"));
    HD_REQUIRE(Write(second, "犬\t멍멍이\n"));
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {first.string(), second.string()}));
    const auto good = engine.Dictionaries();
    std::error_code error;
    std::filesystem::rename(second, away, error);
    HD_REQUIRE(!error);
    HD_REQUIRE(Write(first, "猫\t냥이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(engine.UserFileError().find(second.string() + ": cannot read the file") == 0);
    Session failed(engine);
    HD_CHECK_EQ(failed.Translate("猫"), "고양이");
    HD_CHECK_EQ(failed.Translate("犬"), "멍멍이");
    HD_CHECK(!internal::CheckUserFile(engine));
    std::filesystem::rename(away, second, error);
    HD_REQUIRE(!error);
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() != good);
    HD_CHECK_EQ(failed.Translate("猫"), "냥이");
    HD_CHECK(engine.UserFileError().empty());

    const auto folder = temp.path / "users";
    const auto folderAway = temp.path / "users-away";
    HD_REQUIRE(std::filesystem::create_directory(folder, error));
    const auto child = folder / "userdict.txt";
    HD_REQUIRE(Write(child, "犬\t강아지\n"));
    HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {first.string(), folder.string()}));
    HD_REQUIRE(std::filesystem::remove(child, error));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.UserFileError().empty());
    Session removed(engine);
    HD_CHECK(removed.Translate("犬") != "강아지");
    const auto emptyFolder = engine.Dictionaries();
    std::filesystem::rename(folder, folderAway, error);
    HD_REQUIRE(!error);
    HD_REQUIRE(Write(first, "猫\t흰고양이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == emptyFolder);
    HD_CHECK(engine.UserFileError().find(folder.string() + ": cannot list the folder") == 0);
    HD_CHECK_EQ(removed.Translate("猫"), "냥이");
    std::filesystem::rename(folderAway, folder, error);
    HD_REQUIRE(!error);
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK_EQ(removed.Translate("猫"), "흰고양이");
    HD_CHECK(engine.UserFileError().empty());

    const auto optional = temp.path / "not-created";
    HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {optional.string()}));
    const auto optionalErrors = internal::UserFileErrors(engine);
    HD_REQUIRE(optionalErrors.size() == 1);
    HD_CHECK(optionalErrors[0].kind == dict::LoadErrorKind::OptionalSource);
    HD_CHECK(!dict::FailsLoad(optionalErrors[0].kind));
    HD_CHECK_EQ(optionalErrors[0].file, optional.string());
    HD_CHECK_EQ(optionalErrors[0].line, 0);
    HD_CHECK(!internal::CheckUserFile(engine));
    HD_REQUIRE(std::filesystem::create_directory(optional, error));
    HD_REQUIRE(Write(optional / "userdict.txt", "犬\t멍멍이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    Session appeared(engine);
    HD_CHECK_EQ(appeared.Translate("犬"), "멍멍이");
}

HD_TEST(UserDictionaryCandidateKeepsSourceHistory) {
    TempFolder temp;
    const auto user = temp.path / "userdict.txt";
    const auto renamed = temp.path / "renamed.txt";
    HD_REQUIRE(Write(user, "猫\t고양이\n"));
    Engine original(false, CacheWrite::Off);
    HD_REQUIRE(original.Open({Fixture()}, false, user.string()));
    const auto good = original.Dictionaries();
    const auto history = internal::AcceptedUserSources(original);
    HD_REQUIRE(history.size() == 1);
    HD_CHECK(!history[0].directory);
    std::error_code error;
    HD_REQUIRE(std::filesystem::remove(user, error));
    Engine candidate(false, CacheWrite::Off);
    internal::SeedUserSources(candidate, history);
    HD_CHECK(!candidate.Open({Fixture()}, false, user.string()));
    HD_CHECK(!candidate.loaded());
    HD_CHECK(candidate.UserFileError().find(user.string() + ": cannot read the file") == 0);
    HD_CHECK(original.Dictionaries() == good);
    Session old(original);
    HD_CHECK_EQ(old.Translate("猫"), "고양이");
    HD_REQUIRE(Write(user, "猫\t냥이\n"));
    HD_CHECK(candidate.Open({Fixture()}, false, user.string()));
    HD_CHECK(candidate.UserFileError().empty());
    Session repaired(candidate);
    HD_CHECK_EQ(repaired.Translate("猫"), "냥이");
    std::filesystem::rename(user, renamed, error);
    HD_REQUIRE(!error);
    Engine moved(false, CacheWrite::Off);
    internal::SeedUserSources(moved, history);
    HD_CHECK(moved.Open({Fixture()}, false, renamed.string()));
    HD_CHECK(moved.UserFileError().empty());
    Engine cleared(false, CacheWrite::Off);
    internal::SeedUserSources(cleared, history);
    HD_CHECK(cleared.OpenUserDicts({Fixture()}, false, {}));
    HD_CHECK(cleared.UserFileError().empty());
    Engine emptyHistory(false, CacheWrite::Off);
    internal::SeedUserSources(emptyHistory, {});
    HD_REQUIRE(Write(user, "犬\n"));
    HD_CHECK(!emptyHistory.Open({Fixture()}, false, user.string()));
    HD_CHECK(!emptyHistory.loaded());
}

HD_TEST(UserDictionaryActiveReconfigurationIsTransactional) {
    TempFolder temp;
    const auto oldPath = temp.path / "userdict.txt";
    const auto renamed = temp.path / "renamed.txt";
    const auto bad = temp.path / "bad.txt";
    HD_REQUIRE(Write(oldPath, "猫\t고양이\n"));
    HD_REQUIRE(Write(bad, "犬\n"));
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {oldPath.string()}));
    const auto good = engine.Dictionaries();
    const auto filters = engine.Filters();
    HD_CHECK(!engine.OpenUserDicts({Fixture()}, false, {bad.string()}));
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(engine.Filters() == filters);
    HD_REQUIRE(Write(oldPath, "猫\t냥이\n"));
    HD_CHECK(engine.Reload());
    Session current(engine);
    HD_CHECK_EQ(current.Translate("猫"), "냥이");
    std::error_code error;
    std::filesystem::rename(oldPath, renamed, error);
    HD_REQUIRE(!error);
    HD_CHECK(!engine.Reload());
    HD_CHECK(engine.OpenUserDicts({Fixture()}, false, {renamed.string()}));
    Session moved(engine);
    HD_CHECK_EQ(moved.Translate("猫"), "냥이");
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK(engine.OpenUserDicts({Fixture()}, false, {}));
    Session cleared(engine);
    HD_CHECK(cleared.Translate("猫") != "냥이");
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK(engine.Reload());
}

HD_TEST(FilterFailureStatusDistinguishesWarnings) {
    TempFolder temp;
    const auto ehnd = temp.path / "ehnd";
    const auto away = temp.path / "away";
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(ehnd, error));
    HD_REQUIRE(Write(ehnd / "PostFilter_a.txt", "(x\tz\t1\t1\n"));
    Engine engine(false, CacheWrite::Off);
    HD_CHECK(!internal::FilterFileFailed(engine));
    HD_REQUIRE(engine.OpenEhnd(ehnd.string()));
    HD_CHECK(!internal::FilterFileFailed(engine));
    HD_REQUIRE(engine.Open({Fixture()}));
    HD_CHECK(internal::FilterFileFailed(engine));
    HD_REQUIRE(!engine.FilterErrors().empty());
    HD_CHECK(internal::UserFileErrors(engine).empty());
    HD_REQUIRE(engine.OpenEhnd(ehnd.string(), DictionaryOptions{false}));
    HD_CHECK(!internal::FilterFileFailed(engine));
    HD_CHECK(!engine.FilterErrors().empty());
    HD_REQUIRE(engine.Reload());
    HD_CHECK(!internal::FilterFileFailed(engine));
    HD_CHECK(!engine.FilterErrors().empty());
    const auto good = engine.Dictionaries();
    const auto filters = engine.Filters();
    HD_CHECK(!engine.Reload(DictionaryOptions{true}));
    HD_CHECK(internal::FilterFileFailed(engine));
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(engine.Filters() == filters);
    HD_REQUIRE(Write(ehnd / "PostFilter_a.txt", "a\tb\t1\t0\n"));
    HD_REQUIRE(engine.Reload());
    HD_CHECK(!internal::FilterFileFailed(engine));
    HD_CHECK(engine.FilterErrors().empty());
    const auto repaired = engine.Dictionaries();
    std::filesystem::rename(ehnd, away, error);
    HD_REQUIRE(!error);
    HD_CHECK(!engine.Reload());
    HD_CHECK(internal::FilterFileFailed(engine));
    HD_CHECK(engine.Dictionaries() == repaired);
    std::filesystem::rename(away, ehnd, error);
    HD_REQUIRE(!error);
    HD_REQUIRE(engine.Reload());
    HD_CHECK(!internal::FilterFileFailed(engine));
    HD_CHECK(engine.FilterErrors().empty());
}

HD_TEST(UserDictionaryConfigAndPendingOptions) {
    TempFolder temp;
    const auto user = temp.path / "userdict.txt";
    const auto config = temp.path / "config.yaml";
    HD_REQUIRE(Write(user, "猫\t고양이\n"));
    HD_REQUIRE(Write(config, "strict_dict: true\n"));
    EngineOptions options;
    options.defaultStrictDict = false;
    options.configPath = config.string();
    Engine engine(false, CacheWrite::Off, UserReload::Auto, options);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    const auto good = engine.Dictionaries();
    HD_REQUIRE(Write(user, "猫\t냥이\n犬\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == good);
    HD_CHECK(!engine.UserFileError().empty());
    HD_REQUIRE(Write(config, "strict_dict: false\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    Session accepted(engine);
    HD_CHECK_EQ(accepted.Translate("猫"), "냥이");
    HD_REQUIRE(internal::UserFileErrors(engine).size() == 1);
    HD_CHECK(UserErrorKind(engine) == dict::LoadErrorKind::UserRow);
    const auto lenient = engine.Dictionaries();

    options.strictDict = true;
    engine.SetOptions(options);
    HD_CHECK(engine.Dictionaries() == lenient);
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == lenient);
    HD_CHECK(dict::FailsLoad(UserErrorKind(engine)));
    HD_REQUIRE(Write(config, "strict_dict: false\n# changed\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == lenient);
    options.strictDict = false;
    engine.SetOptions(options);
    HD_CHECK(engine.Dictionaries() == lenient);
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() != lenient);
    HD_CHECK(!dict::FailsLoad(UserErrorKind(engine)));
    HD_REQUIRE(Write(config, "strict_dict: true\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(!dict::FailsLoad(UserErrorKind(engine)));

    HD_CHECK(!engine.Reload(DictionaryOptions{true}));
    HD_CHECK(engine.Reload());
    HD_CHECK(!dict::FailsLoad(UserErrorKind(engine)));
    Engine verified(false, CacheWrite::Off, UserReload::Off, options);
    HD_REQUIRE(verified.Open({Fixture()}, true, user.string()));
    HD_REQUIRE(!internal::UserFileErrors(verified).empty());
    HD_CHECK(dict::FailsLoad(internal::UserFileErrors(verified)[0].kind));
    Session noBadFile(verified);
    HD_CHECK(noBadFile.Translate("猫") != "냥이");
}

HD_TEST(UserDictionaryMemorySettersAreTransactional) {
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.Open({Fixture()}));
    HD_REQUIRE(engine.SetUserDict({{"猫", "고양이", "n", "prio=2 force"},
                                  {"猫", "냥이", "", "prio=1"}}));
    auto entries = engine.GetUserDict();
    HD_REQUIRE(entries.size() == 2);
    HD_CHECK_EQ(entries[0].ja, "猫");
    HD_CHECK_EQ(entries[0].pos, "n");
    HD_CHECK_EQ(entries[0].options, "prio=2 force");
    HD_CHECK_EQ(entries[1].ko, "냥이");
    HD_CHECK(entries[1].pos.empty());
    const auto good = engine.Dictionaries();
    HD_CHECK(!engine.SetUserDictRaw("猫\t흰고양이\n犬\n"));
    HD_CHECK(engine.Dictionaries() == good);
    HD_REQUIRE(engine.GetUserDict().size() == 2);
    HD_CHECK_EQ(engine.GetUserDict()[0].ko, "고양이");
    HD_CHECK(engine.UserFileError().find("userdict.tsv:2: empty KR") == 0);
    HD_CHECK(engine.SetUserDictRaw("猫\t흰고양이\n犬\n", DictionaryOptions{false}));
    HD_REQUIRE(engine.GetUserDict().size() == 1);
    HD_CHECK_EQ(engine.GetUserDict()[0].ko, "흰고양이");
    HD_CHECK(!dict::FailsLoad(UserErrorKind(engine)));
    HD_CHECK(!engine.SetUserDictRaw("犬\n"));
    HD_REQUIRE(engine.GetUserDict().size() == 1);
    HD_CHECK(engine.SetUserDictRaw("犬\n", DictionaryOptions{false}));
    HD_CHECK(engine.GetUserDict().empty());
    HD_CHECK(!engine.UserFileError().empty());
    HD_CHECK(engine.SetUserDictRaw("猫\t고�양이\nあ゙\t아\n"));
    HD_REQUIRE(engine.GetUserDict().size() == 2);
    HD_CHECK_EQ(engine.GetUserDict()[0].ko, "고�양이");
    HD_CHECK(engine.UserFileError().empty());
    const auto decoded = engine.Dictionaries();
    HD_CHECK(!engine.SetUserDictRaw("\xFF", DictionaryOptions{false}));
    HD_CHECK(engine.Dictionaries() == decoded);
    HD_CHECK_EQ(engine.UserFileError(), "userdict.tsv: invalid UTF-8");
    HD_CHECK(engine.SetUserDictRaw(""));
    HD_CHECK(engine.GetUserDict().empty());
    HD_CHECK(engine.UserFileError().empty());
}

HD_TEST(UserDictionaryMemoryOverlaysKeepUnrelatedPolicy) {
    Engine engine(false, CacheWrite::Off);
    HD_REQUIRE(engine.Open({Fixture()}));
    HD_REQUIRE(engine.SetUserDictRaw("猫\t고양이\n犬\n", DictionaryOptions{false}));
    HD_REQUIRE(engine.SetEhnd({{"PostFilter_a.txt", "고양이\t냥이\t1\t0\n"}}));
    Session first(engine);
    HD_CHECK_EQ(first.Translate("猫"), "냥이");
    HD_REQUIRE(engine.GetUserDict().size() == 1);
    HD_CHECK(!engine.UserFileError().empty());
    HD_CHECK(!engine.SetUserDictRaw("犬\n"));
    HD_CHECK(engine.SetEhnd({}));
    Session cleared(engine);
    HD_CHECK_EQ(cleared.Translate("猫"), "고양이");
    HD_REQUIRE(engine.SetEhnd({{"PostFilter_a.txt", "고양이\t냥이\t1\t0\n(x\ty\t1\t1\n"}},
                              DictionaryOptions{false}));
    HD_CHECK(!engine.FilterErrors().empty());
    HD_REQUIRE(engine.SetUserDict({{"猫", "고양이", "", ""}}));
    Session second(engine);
    HD_CHECK_EQ(second.Translate("猫"), "냥이");
    HD_CHECK(engine.UserFileError().empty());
    HD_CHECK(!engine.FilterErrors().empty());
    HD_REQUIRE(engine.Reload());
    const auto accepted = engine.Dictionaries();
    HD_CHECK(!engine.Reload(DictionaryOptions{true}));
    HD_CHECK(engine.Dictionaries() == accepted);
    HD_CHECK(!engine.FilterErrors().empty());
    HD_CHECK(engine.Reload());
    HD_CHECK(engine.SetEhnd({}));
    HD_CHECK(engine.FilterErrors().empty());
    HD_CHECK(engine.SetEhnd({{"PostFilter_a.txt", "x\n"}}));
    HD_CHECK(!engine.FilterErrors().empty());
    HD_CHECK(engine.SetEhnd({{"PostFilter_a.txt", "x\n"}}, DictionaryOptions{false}));
    HD_CHECK(!engine.FilterErrors().empty());
    HD_CHECK(engine.SetUserDictRaw("猫\t고양이\n"));
    HD_CHECK(engine.GetUserDict().size() == 1);
}

HD_TEST(UserDictionaryOptionalSourceOrigins) {
    TempFolder temp;
    const auto missing = (temp.path / "not-created").string();
    for (const bool strict : {false, true}) {
        for (const bool verify : {false, true}) {
            EngineOptions options;
            options.strictDict = strict;
            Engine explicitSource(false, CacheWrite::Off, UserReload::Off, options);
            HD_REQUIRE(explicitSource.OpenUserDicts({Fixture()}, verify, {missing}));
            const auto errors = internal::UserFileErrors(explicitSource);
            HD_REQUIRE(errors.size() == 1);
            HD_CHECK(errors[0].kind == dict::LoadErrorKind::OptionalSource);
            HD_CHECK(!dict::FailsLoad(errors[0].kind));
            HD_CHECK_EQ(errors[0].file, missing);
            HD_CHECK_EQ(errors[0].line, 0);
            HD_CHECK(explicitSource.Reload());

            Engine generated(false, CacheWrite::Off, UserReload::Off, options);
            HD_REQUIRE(internal::OpenUserSources(generated, {Fixture()}, verify, {{missing, true, true, false}}));
            HD_CHECK(generated.UserFileError().empty());
            const auto info = internal::LoadedSourceInfo(generated);
            for (const auto& file : info.files) HD_CHECK(file.path != missing);
            HD_CHECK(internal::AcceptedUserSources(generated).empty());

            const auto missingEhnd = (temp.path / "not-created-ehnd").string();
            HD_CHECK(explicitSource.OpenEhnd(missingEhnd));
            const auto filterWarnings = explicitSource.FilterErrors();
            HD_REQUIRE(filterWarnings.size() == 1);
            HD_CHECK_EQ(filterWarnings[0], missingEhnd + ":0: the explicit Ehnd folder is missing");
            HD_CHECK(!internal::FilterFileFailed(explicitSource));
            const auto userWarnings = internal::UserFileErrors(explicitSource);
            HD_REQUIRE(userWarnings.size() == 1);
            HD_CHECK(userWarnings[0].kind == dict::LoadErrorKind::OptionalSource);
            HD_CHECK_EQ(userWarnings[0].file, missing);
            HD_CHECK_EQ(explicitSource.UserFileError(), missing + ": the explicit user dictionary source is missing");
            HD_CHECK(internal::OpenEhndSource(generated, {missingEhnd, false, true, false}));
            HD_CHECK(generated.UserFileError().empty());
            HD_CHECK(generated.FilterErrors().empty());
            HD_CHECK(generated.Reload());
        }
    }
}

HD_TEST(EhndCandidateKeepsAcceptedFolderHistory) {
    TempFolder temp;
    const auto folder = temp.path / "filters";
    const auto renamed = temp.path / "renamed";
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(folder, error));
    HD_REQUIRE(Write(folder / "PreFilter.txt", "猫\t犬\t1\t0\n"));
    const dict::UserSource source{folder.string(), false, false, true};
    Engine original(false, CacheWrite::Off);
    HD_REQUIRE(internal::OpenEhndSource(original, source));
    HD_REQUIRE(original.Open({Fixture()}));
    std::optional<dict::UserSource> ehnd;
    const auto users = internal::AcceptedUserSources(original, ehnd);
    HD_REQUIRE(ehnd.has_value());
    HD_CHECK(ehnd->available && ehnd->directory && ehnd->explicitPath);
    const auto before = internal::LoadedSourceInfo(original);
    std::size_t receipts = 0;
    for (const auto& file : before.files) {
        if (file.kind != dict::LoadFileKind::EhndPreFilter) continue;
        ++receipts;
        HD_CHECK_EQ(file.path, (folder / "PreFilter.txt").string());
        HD_REQUIRE(file.acceptedCount.has_value());
        HD_CHECK_EQ(*file.acceptedCount, std::size_t{1});
    }
    HD_CHECK_EQ(receipts, std::size_t{1});
    const auto filters = original.Filters();
    std::filesystem::rename(folder, renamed, error);
    HD_REQUIRE(!error);
    HD_CHECK(!original.Reload());
    HD_CHECK(original.Filters() == filters);
    HD_CHECK_EQ(internal::LoadedSourceInfo(original).generation, before.generation);
    Engine missing(false, CacheWrite::Off);
    internal::SeedUserSources(missing, users, ehnd);
    HD_REQUIRE(internal::OpenEhndSource(missing, source));
    HD_CHECK(!missing.Open({Fixture()}));
    HD_CHECK(!missing.loaded());
    HD_CHECK(internal::FilterFileFailed(missing));
    HD_CHECK(!missing.FilterErrors().empty());
    HD_CHECK(internal::LoadedSourceInfo(missing).files.empty());

    Engine moved(false, CacheWrite::Off);
    internal::SeedUserSources(moved, users, ehnd);
    HD_REQUIRE(internal::OpenEhndSource(moved, {renamed.string()}));
    HD_CHECK(moved.Open({Fixture()}));
    HD_CHECK(moved.FilterErrors().empty());
    Engine disabled(false, CacheWrite::Off);
    internal::SeedUserSources(disabled, users, ehnd);
    HD_REQUIRE(disabled.OpenEhnd(""));
    HD_CHECK(disabled.Open({Fixture()}));
    HD_CHECK(disabled.FilterErrors().empty());
    std::filesystem::rename(renamed, folder, error);
    HD_REQUIRE(!error);
    HD_CHECK(original.Reload());
    HD_CHECK(original.FilterErrors().empty());
}

HD_TEST(LoadedSourceReceiptsAreTransactional) {
    TempFolder temp;
    HD_REQUIRE(Write(temp.path / "userdict.txt", "# comment\n猫\t고양이\n猫\t냥이\n鳥\n"));
    HD_REQUIRE(Write(temp.path / "before.tsv", "#! layer=prefilter\n# comment\n猫\t犬\t1\t0\n[\tx\t2\t1\n"));
    EngineOptions options;
    options.strictDict = false;
    Engine engine(false, CacheWrite::Off, UserReload::Off, options);
    HD_REQUIRE(engine.OpenUserDicts({Fixture()}, false, {temp.path.string()}));
    const auto count = [](const internal::LoadInfo& info, dict::LoadFileKind kind,
                          const std::string& path, std::size_t expected) {
        std::size_t found = 0;
        for (const auto& file : info.files) {
            if (file.kind != kind || file.path != path) continue;
            ++found;
            HD_REQUIRE(file.acceptedCount.has_value());
            HD_CHECK_EQ(*file.acceptedCount, expected);
        }
        HD_CHECK_EQ(found, std::size_t{1});
    };
    const auto initial = internal::LoadedSourceInfo(engine);
    HD_CHECK(initial.generation != 0);
    count(initial, dict::LoadFileKind::Shipped, (std::filesystem::path(Fixture()) / "base.tsv").string(), 2);
    count(initial, dict::LoadFileKind::NativeUser, (temp.path / "userdict.txt").string(), 2);
    count(initial, dict::LoadFileKind::NativePreFilter, (temp.path / "before.tsv").string(), 1);
    std::size_t disabled = 0;
    for (const auto& file : initial.files) disabled += file.kind == dict::LoadFileKind::EhndDisabled;
    HD_CHECK_EQ(disabled, std::size_t{1});

    const std::vector<EhndFile> ehnd{
        {"PreFilter_toggles.txt", "_TGL\t_DECR:T_\t_ON\n"},
        {"PostFilter.txt", "고양이\t야옹이\t1\t0\n[\tx\t2\t1\n"},
        {"SkipLayer.txt", "PRE\t1\t猫\nPOST\t2\t[\n"},
        {"UserDict.txt", "猫\t고양이\t0\t\n猫\t냥이\t0\t\n鳥\n"},
    };
    HD_REQUIRE(engine.SetEhnd(ehnd));
    HD_REQUIRE(engine.SetUserDictRaw("猫\t야옹이\n猫\t냥이\n鳥\n"));
    const auto accepted = internal::LoadedSourceInfo(engine);
    count(accepted, dict::LoadFileKind::MemoryUser, "userdict.tsv", 2);
    count(accepted, dict::LoadFileKind::MemoryEhndPreFilter, "PreFilter_toggles.txt", 0);
    count(accepted, dict::LoadFileKind::MemoryEhndPostFilter, "PostFilter.txt", 1);
    count(accepted, dict::LoadFileKind::MemoryEhndSkipLayer, "SkipLayer.txt", 1);
    count(accepted, dict::LoadFileKind::MemoryEhndUserDict, "UserDict.txt", 2);
    for (const auto& file : accepted.files) HD_CHECK(file.kind != dict::LoadFileKind::EhndDisabled);
    HD_CHECK(!engine.SetEhnd(ehnd, DictionaryOptions{true}));
    const auto rejected = internal::LoadedSourceInfo(engine);
    HD_CHECK_EQ(rejected.generation, accepted.generation);
    HD_REQUIRE(rejected.files.size() == accepted.files.size());
    for (std::size_t i = 0; i < accepted.files.size(); ++i) {
        HD_CHECK_EQ(rejected.files[i].path, accepted.files[i].path);
        HD_CHECK(rejected.files[i].kind == accepted.files[i].kind);
        HD_CHECK(rejected.files[i].acceptedCount == accepted.files[i].acceptedCount);
    }
    HD_REQUIRE(engine.SetEhnd({}));
    for (const auto& file : internal::LoadedSourceInfo(engine).files) {
        HD_CHECK(file.kind != dict::LoadFileKind::EhndDisabled);
    }
    HD_REQUIRE(engine.OpenEhnd(""));
    disabled = 0;
    for (const auto& file : internal::LoadedSourceInfo(engine).files) disabled += file.kind == dict::LoadFileKind::EhndDisabled;
    HD_CHECK_EQ(disabled, std::size_t{1});
}

HD_TEST(AutomaticSourcesCanDisappearWithoutBlockingOtherUpdates) {
    for (const bool strict : {false, true}) {
        for (const auto mode : {UserReload::Off, UserReload::Auto}) {
            TempFolder temp;
            const auto legacy = temp.path / "userdict.tsv";
            const auto users = temp.path / "users";
            const auto ehnd = temp.path / "filters";
            std::error_code error;
            HD_REQUIRE(std::filesystem::create_directory(users, error));
            HD_REQUIRE(std::filesystem::create_directory(ehnd, error));
            HD_REQUIRE(Write(legacy, "猫\t고양이\n"));
            HD_REQUIRE(Write(users / "userdict.txt", "犬\t개\n"));
            HD_REQUIRE(Write(ehnd / "PostFilter.txt", "개\t강아지\t1\t0\n"));
            EngineOptions options;
            options.strictDict = strict;
            Engine engine(false, CacheWrite::Off, mode, options);
            HD_REQUIRE(internal::OpenEhndSource(engine, {ehnd.string(), true, false, false}));
            HD_REQUIRE(internal::OpenUserSources(engine, {Fixture()}, false,
                {{legacy.string(), false, false, false}, {users.string(), true, false, false}}));
            const auto before = engine.Dictionaries();
            HD_REQUIRE(std::filesystem::remove(legacy, error));
            HD_REQUIRE(Write(users / "userdict.txt", "犬\t멍멍이\n"));
            if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload());
            HD_CHECK(engine.Dictionaries() != before);
            HD_CHECK(engine.UserFileError().empty());
            Session updated(engine);
            HD_CHECK_EQ(updated.Translate("犬"), "멍멍이");
            HD_REQUIRE(std::filesystem::remove_all(ehnd, error) != 0);
            HD_REQUIRE(Write(users / "userdict.txt", "犬\t흰강아지\n"));
            if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
            else HD_CHECK(engine.Reload());
            HD_CHECK(engine.UserFileError().empty());
            HD_CHECK(engine.FilterErrors().empty());
            Session noFolder(engine);
            HD_CHECK_EQ(noFolder.Translate("犬"), "흰강아지");
        }
    }
}

HD_TEST(MemoryExplicitPoliciesPersistButInheritedPoliciesFollowConfig) {
    for (const auto mode : {UserReload::Off, UserReload::Auto}) {
        TempFolder temp;
        const auto config = temp.path / "config.yaml";
        const auto user = temp.path / "userdict.txt";
        HD_REQUIRE(Write(config, "strict_dict: true\n"));
        HD_REQUIRE(Write(user, "犬\t개\n"));
        EngineOptions options;
        options.configPath = config.string();
        Engine engine(false, CacheWrite::Off, mode, options);
        HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
        HD_REQUIRE(engine.SetUserDictRaw("猫\t고양이\n鳥\n", DictionaryOptions{false}));
        HD_REQUIRE(engine.SetEhnd({{"PostFilter.txt", "고양이\t냥이\t1\t0\n[\tx\t2\t1\n"}}, DictionaryOptions{false}));
        HD_REQUIRE(engine.Reload());
        HD_REQUIRE(Write(user, "犬\t강아지\n"));
        if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
        else HD_CHECK(engine.Reload());
        HD_REQUIRE(engine.GetUserDict().size() == 1);
        Session accepted(engine);
        HD_CHECK_EQ(accepted.Translate("猫"), "냥이");
        const auto good = engine.Dictionaries();
        HD_CHECK(!engine.Reload(DictionaryOptions{true}));
        HD_CHECK(engine.Dictionaries() == good);
        HD_CHECK(engine.Reload());
        HD_CHECK(!engine.SetUserDictRaw("猫\t흰고양이\n鳥\n"));
        HD_CHECK(engine.Reload());
        HD_REQUIRE(engine.SetUserDictRaw("猫\t고양이\n"));
        HD_REQUIRE(engine.SetEhnd({}));

        HD_REQUIRE(Write(config, "strict_dict: false\n"));
        HD_REQUIRE(engine.Reload());
        HD_REQUIRE(engine.SetUserDictRaw("猫\t고양이\n鳥\n"));
        HD_REQUIRE(engine.SetEhnd({{"PostFilter.txt", "고양이\t냥이\t1\t0\n[\tx\t2\t1\n"}}));
        const auto inherited = engine.Dictionaries();
        HD_REQUIRE(Write(config, "strict_dict: true\n# policy changed\n"));
        if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
        else HD_CHECK(!engine.Reload());
        HD_CHECK(engine.Dictionaries() == inherited);
        options.strictDict = false;
        engine.SetOptions(options);
        if (mode == UserReload::Auto) HD_CHECK(internal::CheckUserFile(engine));
        else HD_CHECK(engine.Reload());
        HD_CHECK(engine.Dictionaries() != inherited);
    }
}

HD_TEST(PhysicalPerCallPoliciesPersistAndSettersDoNotRelaxOtherSources) {
    TempFolder temp;
    const auto user = temp.path / "userdict.txt";
    const auto ehnd = temp.path / "filters";
    std::error_code error;
    HD_REQUIRE(std::filesystem::create_directory(ehnd, error));
    HD_REQUIRE(Write(user, "犬\t개\n"));
    HD_REQUIRE(Write(ehnd / "PostFilter.txt", "고양이\t냥이\t1\t0\n[\tx\t2\t1\n"));
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    HD_REQUIRE(engine.OpenEhnd(ehnd.string(), DictionaryOptions{false}));
    HD_REQUIRE(engine.Reload());
    HD_REQUIRE(Write(user, "犬\t강아지\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(!internal::FilterFileFailed(engine));
    HD_REQUIRE(Write(ehnd / "PostFilter.txt", "고양이\t냥이\t1\t0\n"));
    HD_REQUIRE(engine.OpenEhnd(ehnd.string()));
    HD_REQUIRE(Write(ehnd / "PostFilter.txt", "고양이\t흰고양이\t1\t0\n[\tx\t2\t1\n"));
    const auto strictFolder = engine.Dictionaries();
    HD_CHECK(!engine.SetUserDictRaw("猫\t고양이\n鳥\n", DictionaryOptions{false}));
    HD_CHECK(engine.Dictionaries() == strictFolder);
    HD_CHECK(engine.GetUserDict().empty());

    HD_REQUIRE(Write(ehnd / "PostFilter.txt", "고양이\t냥이\t1\t0\n"));
    EngineOptions lenient;
    lenient.strictDict = false;
    engine.SetOptions(lenient);
    HD_REQUIRE(engine.OpenEhnd(ehnd.string(), DictionaryOptions{true}));
    const auto strictAccepted = engine.Dictionaries();
    HD_REQUIRE(Write(ehnd / "PostFilter.txt", "고양이\t냥이\t1\t0\n[\tx\t2\t1\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    HD_CHECK(engine.Dictionaries() == strictAccepted);
    HD_CHECK(internal::FilterFileFailed(engine));
    HD_CHECK(!engine.Reload());
    HD_REQUIRE(engine.OpenEhnd(ehnd.string()));
    HD_CHECK(!engine.Reload(DictionaryOptions{true}));
    HD_REQUIRE(Write(ehnd / "PostFilter.txt", "고양이\t냥이\t1\t0\n"));
    HD_REQUIRE(engine.Reload(DictionaryOptions{true}));
    HD_REQUIRE(Write(user, "犬\t강아지\n鳥\n"));
    const auto explicitStrict = engine.Dictionaries();
    HD_CHECK(!engine.Reload());
    HD_CHECK(engine.Dictionaries() == explicitStrict);
}

HD_TEST(ConfigResultBindsPathsAndPolicyWithoutASecondParse) {
    TempFolder temp;
    const auto path = temp.path / "config.yaml";
    const auto user = temp.path / "userdict.txt";
    HD_REQUIRE(Write(user, "猫\t고양이\n鳥\n"));
    HD_REQUIRE(Write(path, "userdict: userdict.txt\nstrict_dict: false\n"));
    EngineOptions options;
    options.configPath = path.string();
    Engine engine(false, CacheWrite::Off, UserReload::Off, options);
    auto config = internal::LoadConfig(path.string());
    HD_REQUIRE(config.settings.userdict.has_value());
    HD_REQUIRE(config.sourceStamp.has_value());
    std::vector<dict::UserSource> sources;
    for (const auto& file : *config.settings.userdict) sources.push_back({file});
    HD_REQUIRE(internal::OpenUserSources(engine, {Fixture()}, false, sources, config));
    HD_CHECK(!dict::FailsLoad(UserErrorKind(engine)));
    const auto accepted = engine.Dictionaries();
    HD_REQUIRE(Write(path, "userdict: missing.txt\nstrict_dict: true\n# new version\n"));
    HD_CHECK(!internal::ReloadWithConfig(engine, config));
    HD_CHECK(engine.Dictionaries() == accepted);
    const auto staleErrors = internal::UserFileErrors(engine);
    HD_REQUIRE(staleErrors.size() == 2);
    HD_CHECK(staleErrors[0].kind == dict::LoadErrorKind::UserRow);
    HD_CHECK_EQ(staleErrors[0].file, user.string());
    HD_CHECK_EQ(staleErrors[0].line, 2);
    HD_CHECK(!dict::FailsLoad(staleErrors[0].kind));
    HD_CHECK(staleErrors[1].kind == dict::LoadErrorKind::Read);
    HD_CHECK_EQ(staleErrors[1].file, user.string());
    HD_CHECK_EQ(staleErrors[1].line, 0);
    HD_CHECK(dict::FailsLoad(staleErrors[1].kind));
    HD_CHECK_EQ(staleErrors[1].message, "the user sources changed while loading");
    config = internal::LoadConfig(path.string());
    HD_CHECK(!internal::ReloadWithConfig(engine, config));
    HD_CHECK(engine.Dictionaries() == accepted);
    HD_CHECK(dict::FailsLoad(UserErrorKind(engine)));
    HD_REQUIRE(Write(path, "userdict: userdict.txt\nstrict_dict: false\n# recovered\n"));
    config = internal::LoadConfig(path.string());
    HD_CHECK(internal::ReloadWithConfig(engine, config));
    HD_CHECK(!dict::FailsLoad(UserErrorKind(engine)));
}

HD_TEST(DiagnosticVersionChangesForFailedAndRecoveredAutomaticLoads) {
    TempFolder temp;
    const auto user = temp.path / "userdict.txt";
    HD_REQUIRE(Write(user, "猫\t고양이\n"));
    Engine engine(false, CacheWrite::Off, UserReload::Auto);
    HD_REQUIRE(engine.Open({Fixture()}, false, user.string()));
    const auto initial = internal::LoadDiagnosticsVersion(engine);
    HD_REQUIRE(initial.has_value());
    HD_CHECK(internal::LoadDiagnosticsVersion(engine) == initial);
    const auto snapshot = engine.Dictionaries();
    const auto infoGeneration = internal::LoadedSourceInfo(engine).generation;
    HD_REQUIRE(Write(user, "猫\t냥이\n鳥\n"));
    HD_REQUIRE(internal::StartUserFileCheck(engine));
    const auto pending = internal::LoadDiagnosticsVersion(engine);
    if (pending) HD_CHECK(*pending > *initial);
    internal::CheckUserFile(engine);
    const auto failed = internal::LoadDiagnosticsVersion(engine);
    HD_REQUIRE(failed.has_value());
    HD_CHECK(*failed > *initial);
    HD_CHECK(engine.Dictionaries() == snapshot);
    HD_CHECK_EQ(internal::LoadedSourceInfo(engine).generation, infoGeneration);
    HD_CHECK(!engine.UserFileError().empty());
    HD_CHECK(internal::LoadDiagnosticsVersion(engine) == failed);
    HD_REQUIRE(Write(user, "猫\t냥이\n"));
    HD_CHECK(internal::CheckUserFile(engine));
    const auto recovered = internal::LoadDiagnosticsVersion(engine);
    HD_REQUIRE(recovered.has_value());
    HD_CHECK(*recovered > *failed);
    HD_CHECK(engine.Dictionaries() != snapshot);
    HD_CHECK(engine.UserFileError().empty());
}

HD_TEST(UserDictionaryWatchInterval) {
    TempFolder temp;
    const std::vector<std::string> sources{temp.path.string()};
    internal::UserFilesWatch watch;
    internal::UserVersion found;
    const auto start = std::chrono::steady_clock::now();
    HD_CHECK(watch.Due(sources, start, 1s, found));
    watch.loaded = found;
    HD_REQUIRE(Write(temp.path / "userdict.txt", "猫\t고양이\n"));
    HD_CHECK(!watch.Due(sources, start + 500ms, 1s, found));
    HD_CHECK(watch.Due(sources, start + 1s, 1s, found));
    watch.loaded = found;
    HD_CHECK(!watch.Due(sources, start + 2s, 1s, found));
    HD_REQUIRE(Write(temp.path / "userdict-z.tsv", "猫\t냥이\n"));
    HD_CHECK(watch.Due(sources, start + 3s, 1s, found));
}
