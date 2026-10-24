#!/usr/bin/env python3
"""Lists the inputs whose words the CLI drops: two or more adjacent characters (kana or kanji) that the
word stage read one at a time and gave no Korean, so that their text vanishes from the translation
(褒められちゃった 포).

  tools/drops.py CLI DICTS FILE... [--known FILE] [-- CLI-OPTIONS...]
  tools/drops.py CLI DICTS --sets [--budget FILE [--update]] [-- CLI-OPTIONS...]
  tools/drops.py --self-test

Unplaced kana are counted apart, at the word stage: a run of kana items the word stage made up for
characters no dictionary word covers and left empty (ネジの鼻がもげるまで 나사의 코까지: が|も|げ|る), but for
runs that are silent on their own (small kana, ー, a vowel that draws out the kana before it, お or ご before a
word). A later stage that writes such a run as heard (む、無理 무, 무리) does not place it. Only --sets counts
them; the CLI runs with --trace for them.
Each FILE is a text file with one input per line, a rule case table (.tsv, its input column) or a
folder, which stands for its .txt and .tsv files other than the --known file.
Every input is translated as its own line (-each) with -why. A run the word stage left empty that a
later stage still wrote (るんるん 룬룬: the run translated alone is in the line's translation) is no
drop. Prints each input that drops text with the dropped characters. With --known, the inputs listed there (one per line, `#` comments) are known
gaps: they are not reported, but a listed input that no longer drops is, so the list only shrinks.
With --sets the inputs are the measured sets of tools/measure.sh whose variables are set: the lines of
HDTRANS_CORPUS, of HDTRANS_REF_SAMPLE/ja.txt, of the documents HDTRANS_TESTDATA/*/*.ja.txt after their
first line and, when HDTRANS_EXAMPLES is set, of the web examples web/examples.tsv lists (each from
web/examples/, else from HDTRANS_EXAMPLES). It prints the inputs (lines), the distinct inputs that drop
text and the distinct inputs with unplaced kana of each set; every line is translated on its own (-each),
the documents too. With --budget (rows `set inputs drops unplaced`) a set fails when one of its counts
differs from its row (more is a regression, fewer is a fix that lowers the row). A set whose input count
differs from its row (the data changed: --update with that data writes the row again) or that has no row
is not compared and gets a warning on stderr (a ::warning:: annotation under GitHub Actions). --update
writes the counts of the sets into the file and keeps the other rows.
Exit: 0 nothing new, 1 an input drops text, a known gap is fixed or a set is off its budget, 2 invalid
arguments, no set or a failed CLI; with --sets and no set variable, 77.
"""
import os
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor
import sys
import tempfile

# A character the word stage may drop: hiragana, katakana or a kanji.
WORD_CHAR = re.compile(r"^[ぁ-ゖァ-ヺー一-鿿々]$")
KANA = re.compile(r"^[ぁ-ゖァ-ヺー]+$")
# Kana silent on their own (spec MNG-5): small kana, the long-vowel and iteration marks.
SILENT = set("ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮーゝゞヽヾ")
VOWELS = ["あかがさざただなはばぱまやらわぁゃゎアカガサザタダナハバパマヤラワァャヮ",
          "いきぎしじちぢにひびぴみりぃイキギシジチヂニヒビピミリィ",
          "うくぐすずつづぬふぶぷむゆるぅゅウクグスズツヅヌフブプムユルゥュ",
          "えけげせぜてでねへべぺめれぇエケゲセゼテデネヘベペメレェ",
          "おこごそぞとどのほぼぽもよろをぉょオコゴソゾトドノホボポモヨロヲォョ"]
# The vowel kana that draws out a kana of each vowel row (よお, のう, ねえ, きい).
DRAWS = {0: "あア", 1: "いイ", 2: "うウ", 3: "えいエイ", 4: "おうオウ"}


def silent(run, before):
    """Whether every character of an unplaced run is silent after the character before it."""
    for c in run:
        row = next((v for v, chars in enumerate(VOWELS) if before and before in chars), None)
        if c not in SILENT and not (row is not None and c in DRAWS[row]):
            return False
        before = c
    return True


def unplaced(why_text, trace_text):
    """{input line number: [runs]} of the unplaced kana of a -why log and the --trace file of the same run."""
    made = set()
    for line in trace_text.split("\n"):
        cells = line.split("\t")
        if len(cells) >= 16 and cells[0] == "CAND" and cells[8] == "1" and cells[15] == "" and "Nm" in cells[12]:
            made.add((cells[1], cells[2], cells[3], cells[4] + ":" + cells[5]))
    items = {}
    for line in why_text.split("\n"):
        cells = line.split("\t")
        if len(cells) >= 10 and cells[0] == "WHY":
            items.setdefault(int(cells[1]), []).append(cells)
    found = {}
    for number, row in items.items():
        def empty(c):
            return c[7] == "Single" and c[9] == "" and KANA.match(c[5]) and (c[1], c[2], c[3], c[4]) in made
        i = 0
        while i < len(row):
            if not empty(row[i]):
                i += 1
                continue
            j = i
            while j < len(row) and empty(row[j]):
                j += 1
            run = "".join(c[5] for c in row[i:j])
            before = row[i - 1][5][-1:] if i > 0 else ""
            if not (run in ("お", "ご") and j < len(row)) and not silent(run, before):
                found.setdefault(number, []).append(run)
            i = j
    return found


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


def lost(found, outputs, alone):
    """The runs of found whose own translation (alone) is not in the translation of their line."""
    out = {}
    for number, runs in found.items():
        line = outputs[number - 1].replace(" ", "") if 0 < number <= len(outputs) else ""
        gone = [r for r in runs if not alone.get(r, "").strip() or alone[r].replace(" ", "") not in line]
        if gone:
            out[number] = gone
    return out


def measured_sets(here):
    """[(name, inputs)] of the sets whose variables are set, in the order of tools/measure.sh."""
    def lines(path):
        with open(path, encoding="utf-8") as f:
            return [l for l in f.read().split("\n") if l.strip()]
    sets = []
    if os.environ.get("HDTRANS_CORPUS"):
        sets.append(("corpus", lines(os.environ["HDTRANS_CORPUS"])))
    if os.environ.get("HDTRANS_REF_SAMPLE"):
        sets.append(("sample", lines(os.path.join(os.environ["HDTRANS_REF_SAMPLE"], "ja.txt"))))
    if os.environ.get("HDTRANS_TESTDATA"):
        root, docs = os.environ["HDTRANS_TESTDATA"], []
        for folder in sorted(os.listdir(root)):
            path = os.path.join(root, folder)
            if os.path.isdir(path):
                for name in sorted(os.listdir(path)):
                    if name.endswith(".ja.txt") and os.path.isfile(os.path.join(path, name)):
                        docs.extend(lines(os.path.join(path, name))[1:])
        sets.append(("docs", docs))
    if os.environ.get("HDTRANS_EXAMPLES"):
        examples = []
        with open(os.path.join(here, "web", "examples.tsv"), encoding="utf-8") as f:
            for row in f.read().split("\n"):
                name, _, label = row.partition("\t")
                if not name or not label:
                    continue
                for path in (os.path.join(here, "web", "examples", name), os.path.join(os.environ["HDTRANS_EXAMPLES"], name)):
                    if os.path.isfile(path):
                        examples.extend(lines(path))
                        break
        sets.append(("examples", examples))
    return sets


def budget_of(path):
    """{set: (inputs, drops, unplaced)} of a budget file."""
    rows = {}
    with open(path, encoding="utf-8") as f:
        for line in f.read().split("\n"):
            cells = line.split("\t")
            if len(cells) == 4 and not line.startswith("#") and cells[0] != "set":
                rows[cells[0]] = (int(cells[1]), int(cells[2]), int(cells[3]))
    return rows


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
    if "--sets" in argv:
        return usage() if known_path else check_sets(argv, options)
    cli, dicts, files = argv[0], argv[1], argv[2:]
    texts = []
    for path in files:
        for name in files_of(path, known_path):
            texts.extend(inputs_of(name))
    known = known_of(known_path)
    result = dropping(cli, dicts, texts, options)
    if result is None:
        print(f"drops: {cli} failed", file=sys.stderr)
        return 2
    found = result[0]
    bad = 0
    dropping_texts = set()
    for number, runs in sorted(found.items()):
        text = texts[number - 1] if 0 < number <= len(texts) else "?"
        dropping_texts.add(text)
        if text in known:
            continue
        print(f"DROP\t{'|'.join(runs)}\t{text}")
        bad += 1
    for text in sorted(known & set(texts) - dropping_texts):
        print(f"FIXED\t{text}\t(remove it from {known_path})")
        bad += 1
    print(f"drops: {len(texts)} inputs, {len(dropping_texts)} drop text, {bad} to act on")
    return 1 if bad else 0


def check_sets(argv, options):
    argv = list(argv)
    update = "--update" in argv
    budget_path = None
    if "--budget" in argv:
        k = argv.index("--budget")
        if k + 1 >= len(argv):
            return usage()
        budget_path = argv[k + 1]
        del argv[k:k + 2]
    argv = [a for a in argv if a not in ("--sets", "--update")]
    if len(argv) != 2 or (update and budget_path is None):
        return usage()
    if budget_path and not update and not os.path.exists(budget_path):
        print(f"drops: no budget at {budget_path}", file=sys.stderr)
        return 2
    cli, dicts = argv
    sets = measured_sets(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    if not sets:
        print("drops: no set variable (HDTRANS_CORPUS, HDTRANS_REF_SAMPLE, HDTRANS_TESTDATA, HDTRANS_EXAMPLES)")
        return 77
    with ThreadPoolExecutor(len(sets)) as pool:
        results = list(pool.map(lambda s: dropping(cli, dicts, s[1], options, kana=True), sets))
    if any(r is None for r in results):
        print(f"drops: {cli} failed", file=sys.stderr)
        return 2
    counts = {name: (len(texts), len({texts[n - 1] for n in found}), len({texts[n - 1] for n in kana}))
              for (name, texts), (found, kana) in zip(sets, results)}
    if update:
        for name, (inputs, count, kana) in counts.items():
            print(f"drops: {name} {inputs} inputs, {count} drop text, {kana} with unplaced kana")
        write_budget(budget_path, counts)
        return 0
    lines, warnings, bad = compare(counts, budget_of(budget_path) if budget_path else None)
    for line in lines:
        print(line)
    for warning in warnings:
        print(f"::warning::drops: {warning}" if os.environ.get("GITHUB_ACTIONS") == "true" else f"drops: warning: {warning}",
              file=sys.stderr)
    return 1 if bad else 0


def compare(counts, budget):
    """The report lines of counts ({set: (inputs, drops, unplaced)}) against budget (the same shape, None
    for no budget), the warnings about sets that are not compared and the number of counts off the budget."""
    lines, warnings, bad = [], [], 0
    for name, (inputs, count, kana) in counts.items():
        line = f"drops: {name} {inputs} inputs, {count} drop text, {kana} with unplaced kana"
        row = budget.get(name) if budget is not None else None
        if budget is None:
            pass
        elif row is None:
            line += ", not compared"
            warnings.append(f"{name} has no row in the budget; --update with this data adds it")
        elif row[0] != inputs:
            line += ", not compared"
            warnings.append(f"{name} has {inputs} inputs but its budget row is for {row[0]}; the data changed, "
                            "--update with this data writes the row again")
        else:
            for what, got, want in (("drops", count, row[1]), ("unplaced", kana, row[2])):
                if got != want:
                    line += f", FAIL: {what} budget {want}, " + ("more than the budget" if got > want else "lower the budget (--update)")
                    bad += 1
        lines.append(line)
    return lines, warnings, bad


def write_budget(path, counts):
    """Writes counts into the budget file at path, keeping its comment lines and the rows of other sets."""
    budget = budget_of(path) if os.path.exists(path) else {}
    budget.update(counts)
    head = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            head = [l for l in f.read().split("\n") if l.startswith("#")]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(head + ["set\tinputs\tdrops\tunplaced"] +
                          [f"{n}\t{i}\t{d}\t{k}" for n, (i, d, k) in budget.items()]) + "\n")


def dropping(cli, dicts, texts, options, kana=False):
    """({input number: [runs it drops]}, {input number: [unplaced kana runs]} or None without kana) of texts, or
    None when the CLI fails."""
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, HOME=tmp, HDTRANS_CACHE_DIR="off")
        trace = os.path.join(tmp, "trace.tsv")

        def translate(lines, why):
            source, target = os.path.join(tmp, "in.txt"), os.path.join(tmp, "out.txt")
            with open(source, "w", encoding="utf-8") as f:
                f.write("\n".join(lines) + "\n")
            done = subprocess.run([cli, "-dicts", dicts, "-each", "-in", source, "-out", target] +
                                  (["-why"] if why else []) + (["--trace", trace] if why and kana else []) + options,
                                  stderr=subprocess.PIPE, env=env)
            if done.returncode != 0:
                return None, None
            with open(target, encoding="utf-8") as f:
                return f.read().split("\n"), done.stderr.decode("utf-8", "replace")

        outputs, log = translate(texts, True)
        found = placed = None
        if log is not None:
            found = drops(log)
            if kana:
                with open(trace, encoding="utf-8") as f:
                    placed = unplaced(log, f.read())
        runs = sorted({r for rs in (found or {}).values() for r in rs})
        alone, _ = translate(runs, False) if runs else ([], "")
    if found is None or alone is None:
        return None
    return lost(found, outputs, dict(zip(runs, alone))), placed


def usage():
    print("usage: tools/drops.py CLI DICTS FILE... [--known FILE] [-- CLI-OPTIONS...]\n"
          "       tools/drops.py CLI DICTS --sets [--budget FILE [--update]] [-- CLI-OPTIONS...]", file=sys.stderr)
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
    why = "\n".join([
        "WHY\t1\t0\t0\t0:0\t鼻\t0\tFirstXxx\t-1\t코\t-1",
        "WHY\t1\t0\t0\t0:3\tが\t0\tSingle\t-1\t\t-1",
        "WHY\t1\t0\t0\t0:6\tも\t0\tSingle\t-1\t\t-1",
        "WHY\t1\t0\t0\t0:9\tまで\t0\tDigit\t-1\t까지\t-1",
        "WHY\t2\t0\t0\t0:0\tよ\t0\tSingle\t-1\t\t-1",
        "WHY\t2\t0\t0\t0:3\tお\t0\tSingle\t-1\t\t-1",
        "WHY\t3\t0\t0\t0:0\tお\t0\tSingle\t-1\t\t-1",
        "WHY\t3\t0\t0\t0:3\t茶\t0\tSingle\t-1\t차\t-1",
        "WHY\t4\t0\t0\t0:0\tわ\t0\tSingle\t-1\t\t-1",
        "WHY\t5\t0\t0\t0:0\tの\t0\tSingle\t-1\t의\t-1",
        "WHY\t5\t0\t0\t0:3\tう\t0\tSingle\t-1\t\t-1",
    ])
    trace = "\n".join("CAND\t" + "\t".join(k) + "\t0\t1\tN\t110\t\tNm\t0\t\t" for k in [
        ("1", "0", "0", "0", "3", "が"), ("1", "0", "0", "0", "6", "も"), ("2", "0", "0", "0", "0", "よ"),
        ("2", "0", "0", "0", "3", "お"), ("3", "0", "0", "0", "0", "お"), ("5", "0", "0", "0", "3", "う")]) + "\nCAND\t4\t0\t0\t0\t0\tわ\t0\t1\tP\t200\t\t\t0\t\tGN"
    got = unplaced(why, trace)
    assert got == {1: ["がも"], 2: ["よお"]}, got
    assert silent("お", "よ") and silent("っー", "あ") and not silent("む", "") and not silent("が", "鼻")
    assert lost({1: ["めら"], 2: ["るんるん"]}, ["포", "룬룬♪"], {"めら": "메라", "るんるん": "룬 룬"}) == {1: ["めら"]}
    assert lost({1: ["ぶっ"]}, ["날다"], {"ぶっ": ""}) == {1: ["ぶっ"]}
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
        budget = os.path.join(tmp, "b.tsv")
        with open(budget, "w", encoding="utf-8") as f:
            f.write("# c\nset\tinputs\tdrops\tunplaced\ncorpus\t3\t1\t2\n")
        assert budget_of(budget) == {"corpus": (3, 1, 2)}, budget_of(budget)
        rows = budget_of(budget)
        assert compare({"corpus": (3, 1, 2)}, rows) == (["drops: corpus 3 inputs, 1 drop text, 2 with unplaced kana"], [], 0)
        lines, warnings, bad = compare({"corpus": (3, 2, 2)}, rows)
        assert bad == 1 and not warnings and "more than the budget" in lines[0], lines
        lines, warnings, bad = compare({"corpus": (3, 1, 1)}, rows)
        assert bad == 1 and not warnings and "unplaced budget 2" in lines[0] and "lower the budget" in lines[0], lines
        lines, warnings, bad = compare({"corpus": (4, 9, 9)}, rows)
        assert bad == 0 and "not compared" in lines[0] and len(warnings) == 1 and "for 3" in warnings[0], (lines, warnings)
        lines, warnings, bad = compare({"docs": (2, 0, 0)}, rows)
        assert bad == 0 and "not compared" in lines[0] and len(warnings) == 1 and "no row" in warnings[0], (lines, warnings)
        lines, warnings, bad = compare({"corpus": (3, 2, 2), "docs": (2, 0, 0)}, rows)
        assert bad == 1 and len(warnings) == 1, (lines, warnings)
        assert compare({"corpus": (4, 2, 0)}, None) == (["drops: corpus 4 inputs, 2 drop text, 0 with unplaced kana"], [], 0)
        write_budget(budget, {"docs": (2, 0, 1)})
        write_budget(budget, {"corpus": (4, 0, 0)})
        with open(budget, encoding="utf-8") as f:
            assert f.read() == "# c\nset\tinputs\tdrops\tunplaced\ncorpus\t4\t0\t0\ndocs\t2\t0\t1\n"
        fresh = os.path.join(tmp, "fresh.tsv")
        write_budget(fresh, {"sample": (5, 1, 0)})
        assert budget_of(fresh) == {"sample": (5, 1, 0)}, budget_of(fresh)
        os.makedirs(os.path.join(tmp, "data", "a"))
        with open(os.path.join(tmp, "data", "a", "x.ja.txt"), "w", encoding="utf-8") as f:
            f.write("# header\n行く\n\n来る\n")
        saved = {k: os.environ.pop(k, None) for k in ("HDTRANS_CORPUS", "HDTRANS_REF_SAMPLE", "HDTRANS_TESTDATA", "HDTRANS_EXAMPLES")}
        try:
            os.environ["HDTRANS_CORPUS"], os.environ["HDTRANS_TESTDATA"] = plain, os.path.join(tmp, "data")
            assert measured_sets(tmp) == [("corpus", ["a", "b"]), ("docs", ["行く", "来る"])], measured_sets(tmp)
        finally:
            for k, v in saved.items():
                os.environ.pop(k, None)
                if v is not None:
                    os.environ[k] = v
    print("PASS drops self-test")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
