#!/bin/sh
# hdtrans08 flag handling: usage tests/cli.sh HDTRANS08
set -u
cli=$1
fail=0
check() { if ! eval "$2"; then echo "FAIL: $1"; fail=1; fi; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

check "version" '"$cli" --version | grep -q "^hdtrans08 "'
check "unsupported flag exits 2" '"$cli" --ehnd x </dev/null 2>/dev/null; test $? -eq 2'
check "unknown flag exits 2" '"$cli" --bogus </dev/null 2>/dev/null; test $? -eq 2'
check "bad speech exits 2" '"$cli" --speech loud </dev/null 2>/dev/null; test $? -eq 2'
printf 'あ\nい\n' > "$tmp/in.txt"
check "each writes one line per line" 'test "$("$cli" --each --in "$tmp/in.txt" | wc -l | tr -d " ")" = 2'
check "document mode keeps the line count" 'test "$("$cli" --in "$tmp/in.txt" | wc -l | tr -d " ")" = 2'
printf 'あ\nい' > "$tmp/open.txt"
check "document mode without a final newline adds none" 'test "$("$cli" --in "$tmp/open.txt" | wc -l | tr -d " ")" = 1'
printf 'x\n' > "$tmp/one.txt"
check "document json has one record per input line" 'test "$("$cli" --json --in "$tmp/one.txt" | grep -c "^{\"line\":")" = 1'
check "document output ends with exactly one newline" 'test "$("$cli" --in "$tmp/one.txt" | od -An -c | tr -d " ")" = "x\\n"'
check "document stages round trip keeps one line" '"$cli" --stages "$tmp" --in "$tmp/one.txt" >/dev/null && test "$("$cli" --each --toks "$tmp/ir.jsonl" | wc -l | tr -d " ")" = 1'
check "toks with each keeps norm" '"$cli" --each --stages "$tmp" --in "$tmp/in.txt" >/dev/null && test "$("$cli" --each --toks "$tmp/ir.jsonl")" = "$("$cli" --each --in "$tmp/in.txt")"'
check "threads give the same output" 'test "$("$cli" --each --threads 2 --in "$tmp/in.txt")" = "$("$cli" --each --in "$tmp/in.txt")"'
check "json is one object" '"$cli" --json --in "$tmp/in.txt" | head -1 | grep -q "^{\"text\":"'
check "why writes a record per morpheme" '"$cli" --why --in "$tmp/in.txt" 2>&1 >/dev/null | grep -q "\"ko\":"'
check "stages writes ir.jsonl" '"$cli" --stages "$tmp" --in "$tmp/in.txt" >/dev/null && grep -q "\"kir2\"" "$tmp/ir.jsonl"'
check "toks input runs" '"$cli" --stages "$tmp" --in "$tmp/in.txt" >/dev/null && "$cli" --toks "$tmp/ir.jsonl" >/dev/null'
exit $fail
