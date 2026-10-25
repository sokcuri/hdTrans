#!/bin/sh
# Runner contract checks use an isolated registry, not the engine fixtures.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-runner.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/tests/unit/probe"
probe=$tmp/tests/unit/probe/probe_test.cpp
cat > "$probe" <<'CPP'
#include "hdtest.h"
#include <chrono>
#include <filesystem>
#include <fstream>
HD_TEST(pass) { HD_CHECK(true); }
HD_TEST(skip) { HD_SKIP("fixture absent"); }
HD_TEST(fail) { HD_CHECK(false); }
HD_TEST(mixed) { HD_CHECK(false); HD_SKIP("fixture absent"); }
HD_TEST(cache_write) {
    std::ofstream(std::filesystem::path(std::getenv("HDTEST_REAL_CACHE")) / "changed") << "changed";
}
HD_TEST(cache_touch) {
    const auto path = std::filesystem::path(std::getenv("HDTEST_REAL_CACHE")) / "existing";
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    HD_REQUIRE(!ec);
    std::filesystem::last_write_time(path, time + std::chrono::seconds(1), ec);
    HD_CHECK(!ec);
}
HD_TEST(environment) {
    namespace fs = std::filesystem;
    for (const char* name : {"HOME", "LOCALAPPDATA", "XDG_CACHE_HOME", "HDTRANS_CACHE_DIR"}) {
        HD_REQUIRE(std::getenv(name) != nullptr);
    }
    const fs::path home(std::getenv("HOME"));
    const fs::path local(std::getenv("LOCALAPPDATA"));
    const fs::path xdg(std::getenv("XDG_CACHE_HOME"));
    const auto root = home.parent_path();
    HD_CHECK(home != std::getenv("HDTEST_OUTER_HOME"));
    HD_CHECK(local != std::getenv("HDTEST_OUTER_LOCAL"));
    HD_CHECK(xdg != std::getenv("HDTEST_OUTER_XDG"));
    HD_CHECK(local.parent_path() == root);
    HD_CHECK(xdg.parent_path() == root);
    HD_CHECK(home != local && home != xdg && local != xdg);
    std::error_code ec;
    HD_CHECK(fs::status(root, ec).permissions() == fs::perms::owner_all);
    HD_CHECK(!ec);
    for (const auto& path : {home, local, xdg}) {
        std::ofstream file(path / "probe");
        file << "private";
        HD_CHECK(file.good());
    }
    const auto* supplied = std::getenv("HDTEST_SUPPLIED_CACHE");
    if (supplied != nullptr) HD_CHECK_EQ(std::string(std::getenv("HDTRANS_CACHE_DIR")), std::string(supplied));
    else HD_CHECK(fs::path(std::getenv("HDTRANS_CACHE_DIR")) == root / "cache");
    HD_CHECK(std::getenv("HDTRANS_SNAPSHOT") == nullptr);
    HD_CHECK(std::getenv("HDTRANS_VERIFY_DICTS") == nullptr);
    std::printf("temporary-root=%s\n", root.string().c_str());
    if (std::getenv("HDTEST_FAIL_ENVIRONMENT") != nullptr) HD_CHECK(false);
}
CPP
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
    -I "$here/tests/unit" "$here/tests/unit/main.cpp" "$probe" -o "$tmp/runner"
mkdir -p "$tmp/home/Library/Caches/hdtrans" "$tmp/xdg/hdtrans" "$tmp/local"
case $(uname -s) in
    Darwin) real=$tmp/home/Library/Caches/hdtrans ;;
    *) real=$tmp/xdg/hdtrans ;;
esac
export HOME=$tmp/home XDG_CACHE_HOME=$tmp/xdg LOCALAPPDATA=$tmp/local HDTEST_REAL_CACHE=$real
export HDTEST_OUTER_HOME=$HOME HDTEST_OUTER_LOCAL=$LOCALAPPDATA HDTEST_OUTER_XDG=$XDG_CACHE_HOME
printf 'unchanged\n' > "$real/existing"
check() {
    want=$1; summary=$2; filter=$3; require=$4
    status=0
    HDTRANS_REQUIRE_ALL=$require "$tmp/runner" "$filter" > "$tmp/log" 2>&1 || status=$?
    [ "$status" = "$want" ] || { cat "$tmp/log"; echo "unexpected exit: $status" >&2; exit 1; }
    grep -Fx "$summary" "$tmp/log" > /dev/null || { cat "$tmp/log"; exit 1; }
}
check 0 '1 run, 0 failed, 0 skipped' pass 1
check 0 '1 run, 0 failed, 1 skipped' skip 0
! grep '^ok .*skip' "$tmp/log"
grep -E '^SKIP skip: fixture absent \([0-9.]+s\)$' "$tmp/log" > /dev/null
check 1 '1 run, 0 failed, 1 skipped' skip 1
check 1 '1 run, 1 failed, 0 skipped' fail 0
check 1 '1 run, 1 failed, 1 skipped' mixed 0
check 1 '0 run, 0 failed, 0 skipped' no_such_test 0
check 1 '1 run, 1 failed, 0 skipped' cache_write 1
grep 'per-user cache changed' "$tmp/log" > /dev/null
check 1 '1 run, 1 failed, 0 skipped' cache_touch 1
grep 'per-user cache changed' "$tmp/log" > /dev/null
export HDTRANS_SNAPSHOT=$tmp/unused HDTRANS_VERIFY_DICTS=1
unset HDTRANS_CACHE_DIR HDTEST_SUPPLIED_CACHE
lastRoot=
environment_check() {
    check "$1" "$2" environment 1
    root=$(sed -n 's/^temporary-root=//p' "$tmp/log")
    [ -n "$root" ] && [ ! -e "$root" ] && [ "$root" != "$lastRoot" ]
    lastRoot=$root
    [ "$HOME" = "$HDTEST_OUTER_HOME" ]
    [ "$LOCALAPPDATA" = "$HDTEST_OUTER_LOCAL" ]
    [ "$XDG_CACHE_HOME" = "$HDTEST_OUTER_XDG" ]
}
environment_check 0 '1 run, 0 failed, 0 skipped'
for cache in '' off "$tmp/supplied"; do
    export HDTRANS_CACHE_DIR=$cache HDTEST_SUPPLIED_CACHE=$cache
    environment_check 0 '1 run, 0 failed, 0 skipped'
    [ "$HDTRANS_CACHE_DIR" = "$cache" ]
done
export HDTEST_FAIL_ENVIRONMENT=1
environment_check 1 '1 run, 1 failed, 0 skipped'
unset HDTEST_FAIL_ENVIRONMENT
printf '# shards\none probe/probe_test.cpp:pass\ntwo probe/probe_test.cpp:skip probe/probe_test.cpp:fail\nrest probe/\n' > "$tmp/shards"
"$tmp/runner" --check-shards "$tmp/shards" > "$tmp/log" 2>&1 || { cat "$tmp/log"; exit 1; }
grep -Fx '7 tests in 3 shards, 0 in no shard, 0 entries without a test' "$tmp/log" > /dev/null || { cat "$tmp/log"; exit 1; }
"$tmp/runner" --shard "$tmp/shards" one > "$tmp/log" 2>&1 || { cat "$tmp/log"; exit 1; }
grep -E '^ok   pass ' "$tmp/log" > /dev/null && grep -Fx '1 run, 0 failed, 0 skipped' "$tmp/log" > /dev/null || { cat "$tmp/log"; exit 1; }
status=0; "$tmp/runner" --shard "$tmp/shards" two > "$tmp/log" 2>&1 || status=$?
[ "$status" = 1 ] && grep -Fx '2 run, 1 failed, 1 skipped' "$tmp/log" > /dev/null ||
    { cat "$tmp/log"; exit 1; }
printf 'one probe/probe_test.cpp:pass\nnone probe/missing_test.cpp\n' > "$tmp/shards"
status=0; "$tmp/runner" --check-shards "$tmp/shards" > "$tmp/log" 2>&1 || status=$?
[ "$status" = 1 ] && grep -Fx 'no shard: probe/probe_test.cpp:skip' "$tmp/log" > /dev/null &&
    grep -Fx 'no test: shard none entry probe/missing_test.cpp' "$tmp/log" > /dev/null || { cat "$tmp/log"; exit 1; }
printf 'one probe/\ntwo probe/\n' > "$tmp/shards"
! "$tmp/runner" --check-shards "$tmp/shards" > "$tmp/log" 2>&1 && grep -Fx 'entry probe/ is in shards one and two' "$tmp/log" > /dev/null ||
    { cat "$tmp/log"; exit 1; }
echo 'runner semantics: PASS'
