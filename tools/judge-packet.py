#!/usr/bin/env python3
"""Fewer lines to judge: one per distinct change, a few per kind of edit.

  tools/judge-packet.py make [--reps N] DIR PAIRS.tsv...
  tools/judge-packet.py expand DIR VERDICTS.tsv
  tools/judge-packet.py --self-test

PAIRS files hold key, ja, before, after (tools/measure.sh, tools/sets.py; CSV-style TSV quoting).
`make` folds rows with the same ja, before and after into one item, then groups the items by their
edit: the words (space-separated) that before has and after does not, and the other way round,
found by a word diff. It writes, in the new folder DIR:
  reps.pairs.tsv     the pairs to judge, keyed by item id (i<N>): up to N items per edit (default
                     3: the shortest, the longest and a seeded one) and every item of an edit with
                     at most N + 1 items; give it to tools/blind.py or tools/judge.py as it is;
  items.tsv          item id, edit id (e<N>), line count, ja, before, after for every item;
  members.tsv        item id and pair key for every row read;
  edits.tsv          edit id, items, lines, the edit (before words → after words);
and prints the counts.
`expand` reads verdict rows (item id, verdict, judge [note]; tools/judge.py, tools/blind.py final)
for reps.pairs.tsv and writes verdict rows for every pair key to DIR/verdicts.tsv: a judged item
gives its verdict to every line it folds; an edit whose judged items all got the same verdict,
improved or same, gives it to its other items, marked `edit:<judge>`. The items of an edit with a
worse or mixed verdict, or with an unjudged item, go to DIR/next.pairs.tsv to be judged one by one
(make a new packet of it, or judge it directly). Exit 1 when next.pairs.tsv is not empty.
Exit 2 on invalid input.
"""
import collections
import csv
import difflib
import random
import sys
import tempfile
from pathlib import Path


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(2)


def read(path, width):
    try:
        with open(path, encoding="utf-8", newline="") as f:
            rows = [r for r in csv.reader(f, delimiter="\t") if r and r[0]]
    except OSError as error:
        fail(str(error))
    if any(len(r) < width for r in rows):
        fail(f"{path}: rows need {width} columns")
    return rows


def write(path, rows):
    with open(path, "w", encoding="utf-8", newline="") as f:
        csv.writer(f, delimiter="\t", lineterminator="\n").writerows(rows)


def spacing(before, after):
    """The spaces added (+) and removed (-) between two outputs that differ only in spaces, each
    with the two characters on either side."""
    marks, i, j = [], 0, 0
    while i < len(before) or j < len(after):
        if i < len(before) and j < len(after) and before[i] == after[j]:
            i, j = i + 1, j + 1
        elif j < len(after) and after[j].isspace():
            marks.append("+" + after[max(j - 2, 0):j].strip() + "_" + after[j + 1:j + 3].strip())
            j += 1
        else:
            marks.append("-" + before[max(i - 2, 0):i].strip() + "_" + before[i + 1:i + 3].strip())
            i += 1
    return " ".join(marks)


def edit(before, after):
    """The word diff of two outputs: (removed words, added words) per changed run; for outputs that
    differ only in spaces, the places where a space came or went."""
    if before.split() != after.split() and "".join(before.split()) == "".join(after.split()):
        return "(spacing) " + spacing(before, after)
    a, b = before.split(), after.split()
    runs = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag != "equal":
            runs.append(" ".join(a[i1:i2]) + " → " + " ".join(b[j1:j2]))
    return " | ".join(runs) if runs else "(spacing) " + before + " → " + after


def make(argv):
    reps = 3
    if len(argv) >= 2 and argv[0] == "--reps":
        reps, argv = int(argv[1]), argv[2:]
    if len(argv) < 2:
        fail(__doc__.strip().split("\n\n")[1])
    out = Path(argv[0])
    if out.exists():
        fail(f"{out} exists")
    items, members = {}, []
    for path in argv[1:]:
        for key, ja, before, after, *_ in read(path, 4):
            item = items.setdefault((ja, before, after), {"id": f"i{len(items) + 1}", "lines": 0})
            item["lines"] += 1
            members.append([item["id"], key])
    edits = collections.OrderedDict()
    for (ja, before, after), item in items.items():
        e = edits.setdefault(edit(before, after), {"id": f"e{len(edits) + 1}", "items": []})
        e["items"].append((ja, before, after, item))
        item["edit"] = e["id"]
    picked = []
    for name, e in edits.items():
        group = e["items"]
        if len(group) <= reps + 1:
            chosen = group
        else:
            by_length = sorted(group, key=lambda g: (len(g[0]), g[3]["id"]))
            chosen = [by_length[0], by_length[-1]]
            rest = [g for g in group if g not in chosen]
            chosen += random.Random(name).sample(rest, min(reps - 2, len(rest)))
        picked += [[g[3]["id"], g[0], g[1], g[2]] for g in chosen]
    out.mkdir(parents=True)
    write(out / "reps.pairs.tsv", picked)
    write(out / "items.tsv", [[i["id"], i["edit"], i["lines"], ja, b, a] for (ja, b, a), i in items.items()])
    write(out / "members.tsv", members)
    write(out / "edits.tsv", [[e["id"], len(e["items"]), sum(g[3]["lines"] for g in e["items"]), name]
                              for name, e in edits.items()])
    print(f"{len(members)} lines, {len(items)} distinct changes, {len(edits)} edits, {len(picked)} to judge "
          f"({out / 'reps.pairs.tsv'})")
    return 0


def expand(argv):
    if len(argv) != 2:
        fail(__doc__.strip().split("\n\n")[1])
    out = Path(argv[0])
    items = {r[0]: r for r in read(out / "items.tsv", 6)}
    verdicts = {}
    for row in read(argv[1], 3):
        if row[0] not in items:
            fail(f"{argv[1]}: no item {row[0]} in {out / 'items.tsv'}")
        if row[1] not in ("improved", "same", "worse"):
            fail(f"{argv[1]}: bad verdict {row[1]}")
        verdicts[row[0]] = row[1:]
    judged = {r[0] for r in read(out / "reps.pairs.tsv", 4)}
    by_edit = collections.defaultdict(list)
    for item_id, row in items.items():
        by_edit[row[1]].append(item_id)
    final, later = {}, []
    for edit_id, ids in by_edit.items():
        reps = [i for i in ids if i in judged]
        got = {verdicts[i][0] for i in reps if i in verdicts}
        whole = len(got) == 1 and got <= {"improved", "same"} and all(i in verdicts for i in reps)
        for i in ids:
            if i in verdicts:
                final[i] = verdicts[i]
            elif whole:
                v = verdicts[reps[0]]
                final[i] = [v[0], "edit:" + v[1], *v[2:]]
            else:
                later.append(i)
    rows = [[key, *final[i]] for i, key in read(out / "members.tsv", 2) if i in final]
    write(out / "verdicts.tsv", rows)
    write(out / "next.pairs.tsv", [[i, *items[i][3:6]] for i in later])
    counts = collections.Counter(r[1] for r in rows)
    print(f"{len(rows)} lines with a verdict ({counts['improved']} improved, {counts['same']} same, "
          f"{counts['worse']} worse), {len(later)} changes still to judge ({out / 'next.pairs.tsv'})")
    return 1 if later else 0


def self_test():
    assert edit("경주로 이겼다.", "경주에서 이겼다.") == "경주로 → 경주에서"
    assert edit("몫의자리를 비웠다", "몫의 자리를 비웠다") == "(spacing) +몫의_자리"
    assert edit("그의 몫의자리", "그의 몫의 자리") == edit("몫의자리를 비웠다", "몫의 자리를 비웠다")
    assert edit("갈 수 있다", "갈수 있다") == "(spacing) -갈_수"
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        rows = [[f"c:{i}", f"聖さん{'、' * i}こんにちは", "성찬, 안녕" + "," * i, "히지리 씨, 안녕" + "," * i]
                for i in range(1, 8)]
        rows += [["c:20", "競走で勝った。", "경주로 이겼다.", "경주에서 이겼다."],
                 ["d:a:3", "競走で勝った。", "경주로 이겼다.", "경주에서 이겼다."],
                 ["c:21", "力で勝る。", "힘으로 우수하다.", "힘에서 우수하다."]]
        write(t / "a.pairs.tsv", rows[:5])
        write(t / "b.pairs.tsv", rows[5:])
        assert make([str(t / "p"), str(t / "a.pairs.tsv"), str(t / "b.pairs.tsv")]) == 0
        reps = read(t / "p/reps.pairs.tsv", 4)
        items = read(t / "p/items.tsv", 6)
        assert len(items) == 9 and len(reps) == 5, (items, reps)
        edits = {r[3]: r for r in read(t / "p/edits.tsv", 4)}
        assert edits["경주로 → 경주에서"][1:3] == ["1", "2"]
        sig = [r for r in edits.values() if r[3].startswith("성찬,")]
        assert len(sig) == 1 and sig[0][1:3] == ["7", "7"], sig
        write(t / "v.tsv", [[r[0], "improved", "j1"] for r in reps])
        assert expand([str(t / "p"), str(t / "v.tsv")]) == 0
        v = read(t / "p/verdicts.tsv", 3)
        assert len(v) == 10 and {r[0] for r in v} >= {"c:20", "d:a:3"}
        write(t / "q.pairs.tsv", [[f"x:{i}", f"{'あ' * i}聖さん", f"{'아' * i} 성찬 왔다", f"{'아' * i} 히지리 씨 왔다"]
                                  for i in range(1, 11)])
        assert make([str(t / "q"), str(t / "q.pairs.tsv")]) == 0
        reps = read(t / "q/reps.pairs.tsv", 4)
        assert len(reps) == 3
        write(t / "w.tsv", [[r[0], "improved", "j1"] for r in reps])
        assert expand([str(t / "q"), str(t / "w.tsv")]) == 0
        v = read(t / "q/verdicts.tsv", 3)
        assert len(v) == 10 and sum(r[2] == "edit:j1" for r in v) == 7
        write(t / "w.tsv", [[reps[0][0], "worse", "j1"]] + [[r[0], "improved", "j1"] for r in reps[1:]])
        assert expand([str(t / "q"), str(t / "w.tsv")]) == 1
        assert len(read(t / "q/next.pairs.tsv", 4)) == 7 and len(read(t / "q/verdicts.tsv", 3)) == 3
    print("judge-packet self-test OK")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv and argv[0] == "make":
        return make(argv[1:])
    if argv and argv[0] == "expand":
        return expand(argv[1:])
    fail(__doc__.strip().split("\n\n")[1])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
