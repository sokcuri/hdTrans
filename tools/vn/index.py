#!/usr/bin/env python3
"""VN phenomenon index: the constructions of every pool line, with their curriculum level.

  index.py run   --out DIR [--pool DIR] [--titles a,b,...] [--bin HDTRANS08_IR] [--dicts DIR] [--threads N]
  index.py tag   --out DIR [--table constructions.tsv]
  index.py kiwi  --out DIR                      Korean morpheme view of the references (reading aid only)
  index.py shapes --out DIR --family FAM [--n 60] [--level L]   distinct shapes of one family, rarest included
  index.py --self-test

The corpus layout (pairs dir, pool titles) is read from the private config $HD08_VN_CONF.
`run` sends every pool line (column ja of POOL/<title>.tsv) through N → A → S → C → L (and the research
matcher in T) of the 0.8 side driver, one line at a time, and writes DIR/raw.jsonl: per line its raw
construction keys (below), the ending chain of every predicate bunsetsu and the grammatical tokens L did
not fold. Engine output is never kept; only keys derived from the analysis are.

`tag` maps raw keys to families and levels through the construction table (constructions.tsv beside this
file) and writes:
  DIR/index.tsv          id title kind level families keys_unmapped   (one row per line)
  DIR/families.tsv       family level kind lines lines_dialogue lines_narration share shapes example_ids
  DIR/ranking.md         families ranked by the number of lines that contain them, per level
  DIR/unmapped.tsv       raw keys no table row matches, by line count
The ranking orders the work queue; it writes no rule.

Raw keys (one namespace, matched by the table's glob patterns):
  p.<sub>:<lemma>      particle; sub = case conj final adv bind nom (UniDic 助詞 subclass)
  aux:<lemma>/<cform>  auxiliary verb with the head of its conjugation form (連用形, 意志推量形, ...)
  form:<imp|vol|cond-fused>   imperative, volitional or fused conditional form of a content predicate
  sv:<lemma>           non-independent verb or adjective after て or a continuative (いる, しまう, すぎる ...)
  suf:<lemma>, pref:<lemma>   affixes (honorific お/ご, さん, たち, っぽい ...)
  fn:<lemma>           formal noun after an adnominal (こと, わけ, はず, ため, ところ ...)
  f.<field>:<value>    clause features written by L (link, linkSub, aspect, voice, bene, mod, force, negFn,
                       polite, jreg, tense:past, pol:neg)
  cx:<id>              research-matcher row that fired (T)
  dv:<id>, tok:<flag>  derived spoken arcs and token flags (spoken, elongated, unknown)
  kv:<lemma>, keigo:<o-ninaru|o-suru|o-kudasai>   keigo verbs (おる for 居る read オ…) and お〜になる/する/ください
  intj                 an interjection
  unf:<pos>:<lemma>    a grammatical token L left unfolded
"""
import argparse
import collections
import csv
import fnmatch
import json
import os
import random
import re
import subprocess
import sys
import tempfile

csv.field_size_limit(1 << 30)
HERE = os.path.dirname(os.path.abspath(__file__))
V08 = "/Users/sokcuri/hdwork/v08"
# Private corpus layout (pairs dir, pool titles, hold-out title); kept outside the repo.
CONF = os.environ.get("HD08_VN_CONF", os.path.join(V08, "wsB", "vn.json"))


def conf():
    with open(CONF, encoding="utf-8") as f:
        return json.load(f)


BIN = "/Users/sokcuri/hdwork/wt/poc08-integration/build/mac/poc08/hdtrans08-ir"
LEVELS = ["N5", "N4", "N3", "N2", "N1", "VN"]
RANK = {l: i for i, l in enumerate(LEVELS)}
CHUNK = 5000

PSUB = {"格助詞": "case", "接続助詞": "conj", "終助詞": "final", "副助詞": "adv", "係助詞": "bind", "準体助詞": "nom"}
INFL = {"v1", "v5", "vs", "vk", "vz", "adj"}
# Formal nouns (UniDic lemmas): read as a construction only after an adnominal.
FORMAL = {"事", "物", "訳", "筈", "為", "所", "積り", "方", "内", "時", "侭", "所為", "様", "前", "後", "途端",
          "限り", "以上", "通り", "代わり", "次第", "末", "際", "場合", "最中", "頃", "度", "癖", "振り", "せい",
          "もの", "こと", "ところ", "わけ", "はず", "ため", "つもり", "ほう", "うち", "とき", "まま", "よう", "気"}
# Keigo verbs (UniDic lemmas), read as content verbs; 居る read オ… is おる.
KEIGO_V = {"いらっしゃる", "おっしゃる", "召し上がる", "為さる", "下さる", "御座る", "参る", "申す", "申し上げる", "致す",
           "伺う", "頂く", "戴く", "差し上げる", "存じる", "存ずる", "承る", "畏まる"}
CONTENT = {"n", "pn", "v1", "v5", "vs", "vk", "vz", "adj", "na", "adv", "adn", "conj", "intj"}
CLAUSE_FIELDS = ["link", "linkSub", "aspect", "voice", "bene", "force", "negFn", "polite"]
CLAUSE_LISTS = {"modality": "mod", "jreg": "jreg"}


def read_pool(pool, title):
    with open(os.path.join(pool, title + ".tsv"), encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def one_line(s):
    return " ".join(s.replace("\r", " ").split("\n")).strip()


def cform_head(c):
    return (c or "").split("-")[0]


def extras_of(j):
    """Coverage inputs: content lemmas, rule ids that decided something, and the engine's own route and
    unmatched forms when the IR carries them (DESIGN-v3 §8: Line.route, Line.unmatched)."""
    lem = sorted({f"{t.get('lemma')}/{t.get('pos')}" for t in j.get("toks", []) if t.get("pos") in CONTENT
                  and "人名" not in t.get("ud", "")})
    rules = sorted({d["rule"] for d in j.get("decisions", []) if d.get("rule") and not d["rule"].startswith("code:")
                    and d.get("kind") != "fallback"})
    ex = {"lem": lem, "rules": rules}
    if "route" in j:
        ex["route"] = j["route"]
    if "unmatched" in j:
        ex["unmatched"] = [u if isinstance(u, str) else json.dumps(u, ensure_ascii=False) for u in j["unmatched"]]
    return ex


def keys_of(j):
    """Raw construction keys, ending chains and unfolded tokens of one IR line record."""
    toks = j.get("toks", [])
    keys, chains, unf = set(), [], []
    for i, t in enumerate(toks):
        pos, lem, ud = t.get("pos", ""), t.get("lemma", ""), t.get("ud", "")
        prev = toks[i - 1] if i else {}
        if pos.startswith("p_"):
            parts = ud.split("-")
            sub = PSUB.get(parts[1] if len(parts) > 1 else "", pos[2:])
            keys.add(f"p.{sub}:{lem}")
        elif pos == "aux":
            keys.add(f"aux:{lem}/{cform_head(t.get('cform'))}")
        elif pos in INFL:
            ch = cform_head(t.get("cform"))
            if ch == "命令形" and lem != "下さる":
                keys.add("form:imp")
            elif ch == "意志推量形":
                keys.add("form:vol")
            elif t.get("cform", "").endswith("融合"):
                keys.add("form:cond-fused")
            if (lem in KEIGO_V and not (lem == "御座る" and prev.get("pos") == "adj")) or (
                    lem == "居る" and t.get("reading", "").startswith("オ")):
                keys.add(f"kv:{'おる' if lem == '居る' else lem}")
            if "非自立可能" in ud and i and (
                    (prev.get("pos") == "p_conj" and prev.get("lemma") in ("て", "で"))
                    or (prev.get("pos") in INFL and cform_head(prev.get("cform")) == "連用形")):
                keys.add(f"sv:{lem}")
        elif pos == "suf":
            keys.add(f"suf:{lem}")
        elif pos == "pref":
            keys.add(f"pref:{lem}")
            nx = toks[i + 1:i + 4]
            if lem == "御" and len(nx) == 3 and nx[0].get("pos") in INFL and nx[1].get("lemma") == "に" \
                    and nx[2].get("lemma") == "成る":
                keys.add("keigo:o-ninaru")
            elif lem == "御" and len(nx) >= 2 and nx[0].get("pos") in INFL \
                    and cform_head(nx[0].get("cform")) == "連用形" and nx[0].get("lemma") != "願う" \
                    and nx[1].get("lemma") in ("為る", "致す", "申し上げる"):
                keys.add("keigo:o-suru")
            elif lem == "御" and len(nx) >= 2 and nx[0].get("pos") in INFL \
                    and cform_head(nx[0].get("cform")) == "連用形" and nx[1].get("lemma") == "下さる":
                keys.add("keigo:o-kudasai")
        elif pos == "intj":
            keys.add("intj")
        if pos in ("n", "suf") and lem in FORMAL and i and (
                (prev.get("pos") in INFL | {"aux"} and cform_head(prev.get("cform")) in ("連体形", "終止形"))
                or (prev.get("pos") in ("p_case",) and prev.get("lemma") == "の") or prev.get("pos") == "adn"):
            keys.add(f"fn:{lem}")
        if t.get("derive"):
            d = t["derive"]
            for x in (d if isinstance(d, list) else [d]):
                keys.add(f"dv:{x}")
        for fl in t.get("flags", []):
            if fl in ("spoken", "elongated", "unknown"):
                keys.add(f"tok:{fl}")
    for c in j.get("clauses", []):
        for fld in CLAUSE_FIELDS:
            v = c.get(fld)
            if v not in (None, "", "none"):
                keys.add(f"f.{fld}:{v}")
        for fld, short in CLAUSE_LISTS.items():
            for v in c.get(fld, []) or []:
                keys.add(f"f.{short}:{v}")
        if c.get("tense") == "past":
            keys.add("f.tense:past")
        if c.get("pol") == "neg":
            keys.add("f.pol:neg")
        for k in c.get("unfolded", []) or []:
            if 0 <= k < len(toks):
                t = toks[k]
                unf.append(f"{t.get('pos')}:{t.get('lemma')}")
                keys.add(f"unf:{t.get('pos')}:{t.get('lemma')}")
    for b in j.get("buns", []):
        if b.get("type") != "P" or "content" not in b:
            continue
        seq = [toks[k].get("lemma", "") for k in range(b["content"] + 1, b.get("last", b["content"]) + 1)
               if k < len(toks) and toks[k].get("pos") != "sym"]
        if seq:
            chains.append("+".join(seq))
    for d in j.get("decisions", []):
        r = d.get("rule", "")
        if d.get("stage") == "T" and r.startswith("cx:"):
            keys.add(r)
    return sorted(keys), chains, unf


def run_engine(binary, dicts, threads, lines):
    """Run the side driver on `lines` (one per line) and yield IR line records in order."""
    with tempfile.TemporaryDirectory() as td:
        inp = os.path.join(td, "in.txt")
        with open(inp, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        st = os.path.join(td, "st")
        os.mkdir(st)
        cmd = [binary, "--in", inp, "--each", "--threads", str(threads), "--stages", st]
        if dicts:
            cmd += ["--dicts", dicts]
        env = dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR="off")
        p = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
        if p.returncode != 0:
            sys.exit(f"engine failed (rc {p.returncode}): {p.stderr.decode()[-800:]}")
        out = []
        with open(os.path.join(st, "ir.jsonl"), encoding="utf-8") as f:
            for row in f:
                j = json.loads(row)
                if "line" in j:
                    out.append(j)
        if len(out) != len(lines):
            sys.exit(f"engine returned {len(out)} IR lines for {len(lines)}")
        return out


def cmd_run(a):
    os.makedirs(a.out, exist_ok=True)
    c = conf()
    pool = a.pool or os.path.join(c["pairs"], "train")
    titles = a.titles.split(",") if a.titles else c["pool"]
    meta = {"bin": a.bin, "dicts": a.dicts, "pool": pool, "titles": {}}
    with open(os.path.join(a.out, "raw.jsonl"), "w", encoding="utf-8") as w:
        for title in titles:
            rows = read_pool(pool, title)
            meta["titles"][title] = len(rows)
            for s in range(0, len(rows), CHUNK):
                part = rows[s:s + CHUNK]
                irs = run_engine(a.bin, a.dicts, a.threads, [one_line(r["ja"]) for r in part])
                for r, j in zip(part, irs):
                    keys, chains, unf = keys_of(j)
                    rec = {"id": r["id"], "title": title, "kind": r["kind"], "ja": r["ja"], "keys": keys,
                           "chains": chains, "unf": unf}
                    rec.update(extras_of(j))
                    w.write(json.dumps(rec, ensure_ascii=False) + "\n")
                print(f"{title}: {min(s + CHUNK, len(rows))}/{len(rows)}", file=sys.stderr, flush=True)
    with open(os.path.join(a.out, "raw.meta.json"), "w") as f:
        json.dump(meta, f, indent=1)


# ---------------------------------------------------------------- the table

def load_table(path):
    """Rows: pattern family level kind note. First matching row wins (rows are ordered specific → general);
    family '-' marks a key as known but not a construction (lexical or noise)."""
    rows, fam_level = [], {}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            c = (line.split("\t") + [""] * 5)[:5]
            if c[0] == "pattern":
                continue
            pat, fam, lev, kind, note = c
            if fam != "-" and lev not in RANK:
                sys.exit(f"{path}:{n}: level {lev!r} not in {LEVELS}")
            if fam != "-" and fam_level.setdefault(fam, lev) != lev:
                sys.exit(f"{path}:{n}: family {fam} has level {fam_level[fam]} elsewhere, not {lev}")
            rows.append((pat, fam, lev, kind, n))
    return rows


class Tagger:
    def __init__(self, rows):
        self.rows, self.memo = rows, {}

    def __call__(self, key):
        if key not in self.memo:
            hit = None
            for pat, fam, lev, kind, n in self.rows:
                if fnmatch.fnmatchcase(key, pat):
                    hit = (fam, lev, kind)
                    break
            self.memo[key] = hit
        return self.memo[key]


def line_tags(tagger, keys):
    fams, unmapped = {}, []
    for k in keys:
        t = tagger(k)
        if t is None:
            unmapped.append(k)
        elif t[0] != "-":
            fams.setdefault(t[0], (t[1], t[2], k))
    return fams, unmapped


def cmd_tag(a):
    tagger = Tagger(load_table(a.table))
    fam_lines = collections.defaultdict(lambda: {"n": 0, "dlg": 0, "nar": 0, "keys": collections.Counter(),
                                                 "ids": [], "sole": 0})
    unm = collections.Counter()
    unm_ex = {}
    lev_lines = collections.Counter()
    n_lines = 0
    with open(os.path.join(a.out, "raw.jsonl"), encoding="utf-8") as f, \
            open(os.path.join(a.out, "index.tsv"), "w", encoding="utf-8") as w:
        w.write("id\ttitle\tkind\tlevel\tfamilies\tunmapped\n")
        for row in f:
            r = json.loads(row)
            n_lines += 1
            fams, unmapped = line_tags(tagger, r["keys"])
            # every key of a family, for the shape counts
            for k in r["keys"]:
                t = tagger(k)
                if t and t[0] != "-":
                    fam_lines[t[0]]["keys"][k] += 1
            level = max((v[0] for v in fams.values() if v[1] in ("g", "s")), key=lambda l: RANK[l], default="N5")
            if unmapped:
                level_tag = level + "+?"
            else:
                level_tag = level
            lev_lines[level_tag] += 1
            top = [fm for fm, v in fams.items() if v[1] in ("g", "s") and v[0] == level]
            if len(top) == 1:
                fam_lines[top[0]]["sole"] += 1
            for fam, (lev, kind, k) in fams.items():
                d = fam_lines[fam]
                d["n"] += 1
                d["dlg" if r["kind"] == "dialogue" else "nar"] += 1
                d["level"], d["kind"] = lev, kind
                if len(d["ids"]) < 5000:
                    d["ids"].append(r["id"])
            for k in unmapped:
                unm[k] += 1
                unm_ex.setdefault(k, r["ja"][:60])
            w.write(f"{r['id']}\t{r['title']}\t{r['kind']}\t{level_tag}\t"
                    f"{','.join(f'{fm}@{v[0]}' for fm, v in sorted(fams.items()))}\t{','.join(unmapped)}\n")
    rnd = random.Random(7)
    with open(os.path.join(a.out, "families.tsv"), "w", encoding="utf-8") as w:
        w.write("family\tlevel\tkind\tlines\tsole\tlines_dialogue\tlines_narration\tshare\tshapes\texample_ids\n")
        for fam, d in sorted(fam_lines.items(), key=lambda x: (RANK.get(x[1].get("level", "VN"), 9), -x[1]["n"])):
            if not d["n"]:
                continue
            ex = rnd.sample(d["ids"], min(5, len(d["ids"])))
            shapes = ";".join(f"{k}={v}" for k, v in d["keys"].most_common())
            w.write(f"{fam}\t{d['level']}\t{d['kind']}\t{d['n']}\t{d['sole']}\t{d['dlg']}\t{d['nar']}\t"
                    f"{100 * d['n'] / n_lines:.2f}\t{shapes}\t{','.join(ex)}\n")
    with open(os.path.join(a.out, "unmapped.tsv"), "w", encoding="utf-8") as w:
        w.write("key\tlines\texample\n")
        for k, v in unm.most_common():
            w.write(f"{k}\t{v}\t{unm_ex[k]}\n")
    # ranking.md
    out = [f"# VN pool: construction families ranked per level", "",
           f"{n_lines} lines. A family's level is the lowest level of the keys that put a line in it; a line's "
           f"level is the highest level of its families of kind g or s (`+?` = it also has keys no table row maps). "
           f"`sole` = lines whose level is set by this family alone (the lines it unlocks for its level).", "",
           "## Lines by line level", "", "| level | lines | share | cumulative |", "|---|---:|---:|---:|"]
    cum = 0
    for lev in LEVELS:
        for tag in (lev, lev + "+?"):
            v = lev_lines.get(tag, 0)
            cum += v
            out.append(f"| {tag} | {v} | {100 * v / n_lines:.1f}% | {100 * cum / n_lines:.1f}% |")
    for lev in LEVELS:
        fl = [(fam, d) for fam, d in fam_lines.items() if d.get("level") == lev and d["n"]]
        fl.sort(key=lambda x: -x[1]["n"])
        out += ["", f"## {lev}", "", "| rank | family | kind | lines | share | sole | dialogue | narration | top shapes |",
                "|---:|---|---|---:|---:|---:|---:|---:|---|"]
        for i, (fam, d) in enumerate(fl, 1):
            top = ", ".join(f"`{k}` {v}" for k, v in d["keys"].most_common(3))
            out.append(f"| {i} | {fam} | {d['kind']} | {d['n']} | {100 * d['n'] / n_lines:.1f}% | {d['sole']} | {d['dlg']} | "
                       f"{d['nar']} | {top} |")
    out += ["", "## Unmapped keys (top 40)", "", "| key | lines |", "|---|---:|"]
    out += [f"| `{k}` | {v} |" for k, v in unm.most_common(40)]
    with open(os.path.join(a.out, "ranking.md"), "w", encoding="utf-8") as w:
        w.write("\n".join(out) + "\n")
    print(f"tagged {n_lines} lines; {len(fam_lines)} families; {len(unm)} unmapped keys "
          f"({sum(unm.values())} occurrences)", file=sys.stderr)


def cmd_shapes(a):
    """Distinct shapes of a family: per raw key and per ending chain, up to N lines, rarest shapes first kept."""
    tagger = Tagger(load_table(a.table))
    by_shape = collections.defaultdict(list)
    with open(os.path.join(a.out, "raw.jsonl"), encoding="utf-8") as f:
        for row in f:
            r = json.loads(row)
            ks = [k for k in r["keys"] if (tagger(k) or ("-",))[0] == a.family]
            if not ks:
                continue
            for k in ks:
                by_shape[k].append(r)
    rnd = random.Random(11)
    picked, seen = [], set()
    shapes = sorted(by_shape.items(), key=lambda x: len(x[1]))
    per = max(1, a.n // max(1, len(shapes)))
    for k, rs in shapes:
        for r in rnd.sample(rs, min(per, len(rs))):
            if r["id"] not in seen:
                seen.add(r["id"])
                picked.append((k, len(rs), r))
    for k, n, r in picked[:a.n]:
        print(f"{k}\t{n}\t{r['id']}\t{r['ja']}")


def cmd_kiwi(a):
    """Korean morpheme view of the references, for reading only (Kiwi is a measurement tool, never shipped)."""
    py = os.path.join(V08, "eval-runs", "venv", "bin", "python")
    if sys.executable != py and os.path.exists(py):
        os.execv(py, [py, "-I", os.path.abspath(__file__)] + sys.argv[1:])
    from kiwipiepy import Kiwi
    kiwi = Kiwi()
    titles = json.load(open(os.path.join(a.out, "raw.meta.json")))["titles"]
    with open(os.path.join(a.out, "ko.morph.tsv"), "w", encoding="utf-8") as w:
        w.write("id\ttitle\tmorphs\n")
        for title in titles:
            rows = read_pool(json.load(open(os.path.join(a.out, "raw.meta.json")))["pool"], title)
            texts = [one_line(r["ko"]) for r in rows]
            for r, res in zip(rows, kiwi.analyze(texts, top_n=1)):
                toks = res[0][0] if res else []
                w.write(f"{r['id']}\t{title}\t{' '.join(f'{t.form}/{t.tag}' for t in toks)}\n")


def self_test():
    j = {"toks": [{"surf": "行か", "lemma": "行く", "pos": "v5", "ud": "動詞-非自立可能", "cform": "未然形-一般"},
                  {"surf": "なきゃ", "lemma": "ない", "pos": "aux", "ud": "助動詞", "cform": "仮定形-融合"},
                  {"surf": "よ", "lemma": "よ", "pos": "p_final", "ud": "助詞-終助詞"},
                  {"surf": "。", "lemma": "。", "pos": "sym", "ud": "補助記号-句点"}],
         "buns": [{"first": 0, "last": 3, "content": 0, "type": "P"}],
         "clauses": [{"negFn": "obligation", "modality": ["must"], "pol": "neg"}],
         "decisions": [{"stage": "T", "rule": "cx:mod.nakereba"}]}
    keys, chains, unf = keys_of(j)
    assert keys == ["aux:ない/仮定形", "cx:mod.nakereba", "f.mod:must", "f.negFn:obligation", "f.pol:neg",
                    "p.final:よ"], keys
    assert chains == ["ない+よ"], chains
    tg = Tagger([("p.final:よ", "mod.yo/ne", "N5", "g", 1), ("aux:ない/*", "pol.neg", "N5", "g", 2),
                 ("f.mod:must", "mod.must", "N4", "g", 3), ("cx:*", "-", "", "", 4), ("f.*", "-", "", "", 5)])
    fams, unm = line_tags(tg, keys)
    assert set(fams) == {"mod.yo/ne", "pol.neg", "mod.must"} and unm == [], (fams, unm)
    print("self-test ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", nargs="?", choices=["run", "tag", "kiwi", "shapes"])
    ap.add_argument("--out")
    ap.add_argument("--pool", default="", help="pairs dir (default: <pairs>/train of the private config)")
    ap.add_argument("--titles", default="", help="comma list (default: the pool titles of the private config)")
    ap.add_argument("--bin", default=BIN)
    ap.add_argument("--dicts", default="")
    ap.add_argument("--threads", type=int, default=8)
    ap.add_argument("--table", default=os.path.join(HERE, "constructions.tsv"))
    ap.add_argument("--family")
    ap.add_argument("--n", type=int, default=60)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.cmd or not a.out:
        ap.error("give a command and --out")
    {"run": cmd_run, "tag": cmd_tag, "kiwi": cmd_kiwi, "shapes": cmd_shapes}[a.cmd](a)


if __name__ == "__main__":
    main()
