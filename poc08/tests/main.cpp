// Runs the hdtest tests of poc08; an argument filters by name substring.
#include <cstdio>
#include <string_view>

#include "hdtest.h"

int main(int argc, char** argv) {
    const std::string_view filter = argc > 1 ? argv[1] : "";
    int failed = 0, run = 0;
    for (const hdtest::Test& t : hdtest::Registry()) {
        if (!filter.empty() && std::string_view(t.name).find(filter) == std::string_view::npos) continue;
        hdtest::Cur() = {};
        t.fn();
        run++;
        if (hdtest::Cur().failures) {
            failed++;
            std::fprintf(stderr, "FAIL %s\n", t.name);
        }
    }
    std::printf("%d tests, %d failed\n", run, failed);
    return failed ? 1 : 0;
}
