#pragma once
// Minimal test registry. A test is a void function registered with HD_TEST; checks
// use HD_CHECK (records and continues) or HD_REQUIRE (records and returns from the
// test). No exceptions are used, so the library can be built with -fno-exceptions.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace hdtest {

struct Test {
    const char* name;
    void (*fn)();
    const char* file;
};

inline std::vector<Test>& Registry() {
    static std::vector<Test> tests;
    return tests;
}

struct State {
    int failures = 0;
    bool skipped = false;
    std::string skipReason;
};

inline bool RequireAll() {
    const char* value = std::getenv("HDTRANS_REQUIRE_ALL");
    return value != nullptr && std::string(value) == "1";
}

// Whether wall-clock ratio checks run. A shared CI runner (CI set) times too unevenly for them, and
// HDTRANS_SKIP_TIMING turns them off by hand; there a test writes the measured ratio to stderr instead.
inline bool TimingChecked() {
    for (const char* name : {"CI", "HDTRANS_SKIP_TIMING"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && *value != '\0') return false;
    }
    return true;
}

inline State& Cur() {
    static State s;
    return s;
}

inline void Fail(const char* file, int line, const std::string& msg) {
    Cur().failures++;
    std::fprintf(stderr, "%s:%d: %s\n", file, line, msg.c_str());
}

inline void Skip(const std::string& reason) {
    Cur().skipped = true;
    Cur().skipReason = reason;
}

struct Register {
    Register(const char* name, void (*fn)(), const char* file) { Registry().push_back({name, fn, file}); }
};

}  // namespace hdtest

#define HD_TEST(name)                                        \
    static void hdtest_##name();                             \
    static hdtest::Register hdtest_reg_##name(#name, hdtest_##name, __FILE__); \
    static void hdtest_##name()

#define HD_CHECK(cond) \
    do { if (!(cond)) hdtest::Fail(__FILE__, __LINE__, "check failed: " #cond); } while (0)

#define HD_SKIP(reason) \
    do { hdtest::Skip(reason); return; } while (0)

#define HD_REQUIRE(cond) \
    do { if (!(cond)) { hdtest::Fail(__FILE__, __LINE__, "require failed: " #cond); return; } } while (0)

#define HD_CHECK_EQ(a, b)                                                             \
    do {                                                                              \
        auto hd_a = (a);                                                              \
        auto hd_b = (b);                                                              \
        if (!(hd_a == hd_b))                                                          \
            hdtest::Fail(__FILE__, __LINE__, std::string(#a " != " #b ": ") +        \
                                                 hdtest::Show(hd_a) + " vs " + hdtest::Show(hd_b)); \
    } while (0)

namespace hdtest {
inline std::string Show(const std::string& s) { return "\"" + s + "\""; }
inline std::string Show(const char* s) { return Show(std::string(s)); }
inline std::string Show(bool b) { return b ? "true" : "false"; }
template <class T>
inline std::string Show(T v) { return std::to_string(v); }
}  // namespace hdtest
