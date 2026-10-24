#!/usr/bin/env python3
"""Lists the inputs whose words the CLI drops: two or more adjacent characters (kana or kanji) that the
word stage read one at a time and gave no Korean, so that their text vanishes from the translation
(褒められちゃった 포).

  tools/drops.py CLI DICTS FILE... [--known FILE] [-- CLI-OPTIONS...]
  tools/drops.py --self-test

Each FILE is a text file with one input per line, a rule case table (.tsv, its input column) or a
folder, which stands for its .txt and .tsv files other than the --known file.
Every input is translated as its own line (-each) with -why. Prints each input that drops text with
the dropped characters. With --known, the inputs listed there (one per line, `#` comments) are known
gaps: they are not reported, but a listed input that no longer drops is, so the list only shrinks.
Exit: 0 nothing new, 1 an input drops text or a known gap is fixed, 2 invalid arguments or a failed CLI.
"""
import os
import re
import subprocess
import sys
import tempfile

# A character the word stage may drop: hiragana, katakana or a kanji.
WORD_CHAR = re.compile(r"^[ぁ-ゖァ-ヺー一-鿿々]$")


def inputs_of(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    if not path.endswith(".tsv"):
        return [l for l in lines if l.strip()]
    out, column = [], None
    for line in lines:
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if column is None:
            column = cells.index("input")
        elif column < len(cells) and cells[column].strip():
            out.append(cells[column].replace("\\n", " "))
    return out


def files_of(path, known_path):
    if not os.path.isdir(path):
        return [path]
    skip = os.path.realpath(known_path) if known_path else None
    return [os.path.join(path, n) for n in sorted(os.listdir(path))
            if n.endswith((".txt", ".tsv")) and os.path.realpath(os.path.join(path, n)) != skip]


def drops(why_text):
    """{input line number: [dropped runs]} from a -why log of an -each run."""
    found = {}
    last = None
    run = []

    def close():
        if len(run) >= 2:
            found.setdefault(last, []).append("".join(run))
        run.clear()

    for line in why_text.split("\n"):
        if not line.startswith("WHY\t"):
            continue
        cells = line.split("\t")
        if len(cells) < 10:
            continue
        number = int(cells[1])
        if number != last:
            close()
            last = number
        if cells[7] == "Single" and cells[9] == "" and WORD_CHAR.match(cells[5]):
            run.append(cells[5])
        else:
            close()
    close()
    return found


def known_of(path):
    if path is None:
        return set()
    with open(path, encoding="utf-8") as f:
        return {l for l in f.read().split("\n") if l.strip() and not l.startswith("#")}


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    options = []
    if "--" in argv:
        options = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    known_path = None
    if "--known" in argv:
        k = argv.index("--known")
        if k + 1 >= len(argv):
            return usage()
        known_path = argv[k + 1]
        argv = argv[:k] + argv[k + 2:]
    if len(argv) < 3:
        return usage()
    cli, dicts, files = argv[0], argv[1], argv[2:]
    texts = []
    for path in files:
        for name in files_of(path, known_path):
            texts.extend(inputs_of(name))
    known = known_of(known_path)
    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, "in.txt")
        with open(source, "w", encoding="utf-8") as f:
            f.write("\n".join(texts) + "\n")
        env = dict(os.environ, HOME=tmp, HDTRANS_CACHE_DIR="off")
        done = subprocess.run([cli, "-dicts", dicts, "-each", "-why", "-in", source, "-out", os.devnull] + options,
                              stderr=subprocess.PIPE, env=env)
    if done.returncode != 0:
        print(f"drops: {cli} failed", file=sys.stderr)
        return 2
    found = drops(done.stderr.decode("utf-8", "replace"))
    bad = 0
    dropping = set()
    for number, runs in sorted(found.items()):
        text = texts[number - 1] if 0 < number <= len(texts) else "?"
        dropping.add(text)
        if text in known:
            continue
        print(f"DROP\t{'|'.join(runs)}\t{text}")
        bad += 1
    for text in sorted(known & set(texts) - dropping):
        print(f"FIXED\t{text}\t(remove it from {known_path})")
        bad += 1
    print(f"drops: {len(texts)} inputs, {len(dropping)} drop text, {bad} to act on")
    return 1 if bad else 0


def usage():
    print("usage: tools/drops.py CLI DICTS FILE... [--known FILE] [-- CLI-OPTIONS...]", file=sys.stderr)
    return 2


def self_test():
    log = "\n".join([
        "[REQUEST] x",
        "WHY\t1\t0\t0\t0:0\t褒\t0\tSingle\t-1\t포\t-1",
        "WHY\t1\t0\t0\t0:3\tめ\t0\tSingle\t-1\t\t-1",
        "WHY\t1\t0\t0\t0:6\tら\t0\tSingle\t-1\t\t-1",
        "WHY\t2\t0\t0\t0:0\tお\t0\tSingle\t-1\t\t-1",
        "WHY\t2\t0\t0\t0:3\t茶\t0\tLocalRule\t5\t차\t-1",
        "WHY\t3\t0\t0\t0:0\t、\t0\tSingle\t-1\t\t-1",
        "WHY\t3\t0\t0\t0:3\t。\t0\tSingle\t-1\t\t-1",
        "WHY\t4\t0\t0\t0:0\tあ\t0\tSingle\t-1\t\t-1",
        "WHY\t5\t0\t0\t0:0\tい\t0\tSingle\t-1\t\t-1",
    ])
    got = drops(log)
    assert got == {1: ["めら"]}, got
    with tempfile.TemporaryDirectory() as tmp:
        table = os.path.join(tmp, "q.tsv")
        with open(table, "w", encoding="utf-8") as f:
            f.write("# 규칙: X\nrule\tinput\texpected\tkind\tlayer\nL1\t行く\\n来る\t가다\tfix\tlocal\nL1\t\t\tkeep\tlocal\n")
        assert inputs_of(table) == ["行く 来る"], inputs_of(table)
        plain = os.path.join(tmp, "p.txt")
        with open(plain, "w", encoding="utf-8") as f:
            f.write("a\n\nb\n")
        assert inputs_of(plain) == ["a", "b"]
        known = os.path.join(tmp, "k.txt")
        with open(known, "w", encoding="utf-8") as f:
            f.write("# gaps\nx\n\n")
        assert known_of(known) == {"x"}
        assert files_of(tmp, known) == [plain, table], files_of(tmp, known)
    print("PASS drops self-test")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
