#!/usr/bin/env python3
"""Korean segmentation into the token notation of include/hd8/smt_ko.h (build time; Kiwi + the W5 generator).

  kseg.py IN.ko OUT.tok [--rt OUT.rt] [--jobs N] [--stats]

OUT.tok: level-neutral tokens (training form). OUT.rt: the same with @level on finals (round-trip form).
Every abstract token is checked against the generator when it is made (the bundle must realize the word, the
particle must realize its surface); a word that fails any check is written as surface pieces, so recombination
is exact by construction and the coverage of the abstraction is what the stats report.
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smtlib

HD8SMT = os.environ.get("HD8SMT", os.path.join(smtlib.ROOT, "build", "mac", "poc08", "smt", "hd8smt"))
STEMS = {"VV", "VA", "VX", "VCP", "VCN", "XSV", "XSA"}
SYM = {"SF", "SP", "SS", "SSO", "SSC", "SE", "SO", "SW", "SB"}
PART = {
    ("이", "JKS"): "이/가", ("가", "JKS"): "이/가", ("은", "JX"): "은/는", ("는", "JX"): "은/는",
    ("을", "JKO"): "을/를", ("를", "JKO"): "을/를",
}
ALT = {"과": "과/와", "와": "과/와", "로": "(으)로", "으로": "(으)로", "로서": "(으)로서", "으로서": "(으)로서",
       "로써": "(으)로써", "으로써": "(으)로써", "랑": "(이)랑", "이랑": "(이)랑", "나": "(이)나", "이나": "(이)나",
       "라도": "(이)라도", "이라도": "(이)라도", "든지": "(이)든지", "이든지": "(이)든지", "야": "(이)야",
       "이야": "(이)야", "라고": "(이)라고", "이라고": "(이)라고", "며": "(이)며", "이며": "(이)며"}


def notation(form, tag):
    if (form, tag) in PART:
        return PART[(form, tag)]
    if tag == "JKV" and form in ("아", "야"):
        return "아/야"
    return ALT.get(form, form)


def esc(t):
    return "\\" + t if t[:1] in ("+", "\\") else t


def base(tag):
    return tag.split("-")[0]


class Word:
    """Plan for one word: a list of items; queries are filled in later."""
    __slots__ = ("items", "text")

    def __init__(self, text):
        self.text = text
        self.items = []  # ("lit", s) | ("P", key, lemma_token_kind, lemma_tok, cls_cands) | ("K", key, notation, surf)


def plan_word(text, ms, want_q):
    """ms: Kiwi morphs of this word with offsets relative to the word. Returns (items, queries) or None."""
    n = len(ms)
    # leading / trailing symbols
    i, j = 0, n
    while i < j and base(ms[i].tag) in SYM:
        i += 1
    while j > i and base(ms[j - 1].tag) in SYM:
        j -= 1

    def span(k):
        return ms[k].start, ms[k].start + ms[k].len

    # clean cut positions: a cut at morph k start is clean if it does not overlap morph k-1
    def cut_ok(k):
        return k == 0 or k == n or ms[k].start >= ms[k - 1].start + ms[k - 1].len

    if not (cut_ok(i) and cut_ok(j)):
        return None
    core_b = ms[i].start if i < n else len(text)
    core_e = ms[j].start if j < n else len(text)
    if i == 0:
        core_b = 0
    if j == n:
        core_e = len(text)
    lead = [text[ms[k].start:(ms[k + 1].start if k + 1 < n else len(text))] for k in range(0, i)]
    trail_b = core_e
    trail = []
    for k in range(j, n):
        e = ms[k + 1].start if k + 1 < n else len(text)
        trail.append(text[ms[k].start:e])
    if any(not cut_ok(k) for k in range(1, i + 1)) or any(not cut_ok(k) for k in range(j, n)):
        return None
    items = []
    for s in lead:
        items.append(("lit", s))
    core = ms[i:j]
    if core:
        ci = plan_core(text, core, core_b, core_e, want_q, i, ms)
        if ci is None:
            return None
        items += ci
    for s in trail:
        items.append(("lit", s))
    if "".join(x[1] if x[0] == "lit" else "" for x in items) == text:
        pass
    return items


def plan_core(text, core, b, e, want_q, off, ms):
    stems = [k for k, m in enumerate(core) if base(m.tag) in STEMS]
    parts = [k for k, m in enumerate(core) if m.tag.startswith("J")]

    def clean(k):  # cut before core[k]
        return k == 0 or core[k].start >= core[k - 1].start + core[k - 1].len

    def particles(first, prev_end):
        out = []
        for k in range(first, len(core)):
            if not core[k].tag.startswith("J") or not clean(k):
                return None
            s = core[k].start
            if s != prev_end:
                return None
            end = core[k + 1].start if k + 1 < len(core) else e
            out.append(("K", notation(core[k].form, core[k].tag), text[s:end]))
            prev_end = end
        return out

    if not stems:
        if not parts:
            return [("lit", text[b:e])]
        k0 = parts[0]
        if k0 == 0 or not clean(k0):
            return None
        noun = text[b:core[k0].start]
        ps = particles(k0, core[k0].start)
        if ps is None:
            return None
        return [("lit", noun)] + ps
    if len(stems) != 1:
        return None
    si = stems[0]
    st = core[si]
    tag = base(st.tag)
    # endings after the stem, then optional particles
    k = si + 1
    while k < len(core) and core[k].tag.startswith("E"):
        k += 1
    endings = core[si + 1:k]
    if not endings:
        return None
    for m in core[k:]:
        if not m.tag.startswith("J"):
            return None
    end_e = core[k].start if k < len(core) else e
    if k < len(core) and not clean(k):
        return None
    last = base(endings[-1].tag)
    want = {"EF": "final", "EC": "conn", "ETM": "adn", "ETN": "nom"}.get(last, "any")
    mood = "q" if want_q and want == "final" else "decl"
    items = []
    if tag == "VCP":
        if si == 0:
            return None
        else:
            if not clean(si):
                return None
            host = text[b:st.start]
            pre_b = st.start
        if host:
            items.append(("lit", host))
        items.append(("P", "V", "이다", "이다", "C", host, text[pre_b:end_e], want, mood))
    elif tag in ("XSV", "XSA"):
        if si == 0:
            return None
        pre = text[b:st.start] if clean(si) else None
        if not pre or not re.fullmatch(r"[가-힣]+", pre):
            return None
        lemma = pre + st.form + "다"
        items.append(("lit", pre))
        items.append(("P", "X", lemma, st.form + "다", "V" if tag == "XSV" else "A", "", text[b:end_e], want, mood))
    else:
        if si != 0:
            return None
        lemma = st.form + "다"
        pos = {"VV": "V", "VA": "A"}.get(tag, "")
        if lemma in ("있다", "없다", "계시다"):
            pos = ""
        items.append(("P", "L", lemma, lemma, pos, "", text[b:end_e], want, mood, st.tag.endswith("-I")))
    if k < len(core):
        ps = particles(k, core[k].start)
        if ps is None:
            return None
        items += ps
    return items


CLS_TRY = ["", "D", "B", "S", "H", "REU", "L", "U", "REO"]


def run_queries(qs, jobs):
    """qs: list of query lines → answers, in order (sharded over processes)."""
    if not qs:
        return []
    shards = [[] for _ in range(jobs)]
    for n, q in enumerate(qs):
        lem = q.split("\t")[1] if q.startswith("P") else ""
        shards[hash(lem) % jobs].append((n, q))
    procs = []
    for sh in shards:
        p = subprocess.Popen([HD8SMT, "ko-query"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        procs.append(p)
    import threading
    res = [None] * len(qs)

    def feed(p, sh):
        p.stdin.write("".join(q + "\n" for _, q in sh))
        p.stdin.close()

    ths = [threading.Thread(target=feed, args=(p, sh)) for p, sh in zip(procs, shards)]
    for t in ths:
        t.start()
    for p, sh in zip(procs, shards):
        out = p.stdout.read().split("\n")
        p.wait()
        for (n, _), a in zip(sh, out):
            res[n] = a
    for t in ths:
        t.join()
    return res


def segment(lines, jobs=16):
    """→ (neutral token lines, round-trip token lines, stats)"""
    kt = smtlib.KoTok(workers=jobs)
    norm = [re.sub(r"\s+", " ", unicodedata.normalize("NFC", l)).strip() for l in lines]
    analyses = kt.kiwi.tokenize(norm)
    plans = []  # per line: list of (word_text, items or None, morphs)
    qset = {}
    stats = collections.Counter()
    for text, toks in zip(norm, analyses):
        words = [(m.start(), m.end()) for m in re.finditer(r"\S+", text)]
        per = [[] for _ in words]
        wi = 0
        for t in toks:
            while wi < len(words) and t.start >= words[wi][1]:
                wi += 1
            if wi < len(words) and t.start >= words[wi][0]:
                per[wi].append(t)
        lp = []
        for wn, ((ws, we), ms) in enumerate(zip(words, per)):
            wt = text[ws:we]

            class M:
                __slots__ = ("form", "tag", "start", "len")

            rel = []
            for t in ms:
                m = M()
                m.form, m.tag, m.start, m.len = t.form, str(t.tag), t.start - ws, t.len
                rel.append(m)
            nxt = text[we:we + 1]
            want_q = "?" in wt or "？" in wt
            items = plan_word(wt, rel, want_q) if rel else None
            if items:
                for it in items:
                    if it[0] == "P":
                        for cls in (CLS_TRY if (len(it) > 9 and it[9]) else [""]):
                            q = "\t".join(["P", it[2], cls, it[4], it[5], it[6], it[7], it[8]])
                            qset.setdefault(q, None)
            lp.append((wt, items, rel))
        plans.append(lp)
    # particle queries need the realized prefix, which is the literal surface by construction
    for lp in plans:
        for wt, items, _ in lp:
            if not items:
                continue
            prefix = ""
            for it in items:
                if it[0] == "lit":
                    prefix += it[1]
                elif it[0] == "P":
                    if it[1] == "X":
                        prefix = prefix[:len(prefix) - (len(it[2]) - len(it[3]))]
                    prefix += it[6]
                elif it[0] == "K":
                    qset.setdefault("\t".join(["K", prefix or "", it[1], it[2]]), None)
                    prefix += it[2]
    keys = list(qset)
    ans = run_queries(keys, jobs)
    qa = dict(zip(keys, ans))
    out_n, out_rt = [], []
    for lp, text in zip(plans, norm):
        tn, tr = [], []
        for wt, items, rel in lp:
            toks_n, toks_r = word_tokens(wt, items, rel, qa, stats)
            tn += toks_n
            tr += toks_r
        out_n.append(" ".join(tn))
        out_rt.append(" ".join(tr))
    return out_n, out_rt, norm, stats


def fallback_pieces(wt, rel):
    class T:
        pass
    ts = []
    for m in rel:
        t = T()
        t.start, t.len = m.start, m.len
        ts.append(t)
    ps = smtlib.ko_pieces_from_tokens(wt, ts) if ts else [wt]
    return [esc(p) if n == 0 else "+" + p[len(smtlib.JOIN):] for n, p in enumerate(ps)]


def word_tokens(wt, items, rel, qa, stats):
    stats["words"] += 1
    if not items:
        stats["fallback"] += 1
        t = fallback_pieces(wt, rel)
        return t, t
    tn, tr = [], []
    prefix = ""
    for it in items:
        first = not tn
        if it[0] == "lit":
            tok = esc(it[1]) if first else "+" + it[1]
            tn.append(tok)
            tr.append(tok)
            prefix += it[1]
        elif it[0] == "P":
            kind, lemma, ltok, pos, host, surf, want, mood = it[1:9]
            hit = None
            for cls in (CLS_TRY if (len(it) > 9 and it[9]) else [""]):
                a = qa.get("\t".join(["P", lemma, cls, pos, host, surf, want, mood]))
                if a and a != "-":
                    hit = (cls, a)
                    break
            if not hit:
                stats["fallback"] += 1
                stats["fallback-pred"] += 1
                t = fallback_pieces(wt, rel)
                return t, t
            cls, a = hit
            body, _, level = a.partition("\t")
            lt = ltok + ("~" + cls if cls else "") + ("^" + pos if pos else "")
            if kind == "L":
                lt = ("V:" if first else "+L:") + lt
            elif kind == "X":
                lt = "+X:" + lt
            else:
                lt = "+V:" + lt
            tn.append(lt)
            tr.append(lt)
            tn.append("+" + body)
            tr.append("+" + body + ("@" + level if level else ""))
            if kind == "X":
                prefix = prefix[:len(prefix) - (len(lemma) - len(ltok))]
            prefix += surf
            stats["pred"] += 1
            if level:
                stats["final"] += 1
        else:
            notation, surf = it[1], it[2]
            if qa.get("\t".join(["K", prefix, notation, surf])) == "1":
                tok = "+J:" + notation
                stats["particle"] += 1
            else:
                tok = "+" + surf
                stats["particle-lit"] += 1
            tn.append(tok)
            tr.append(tok)
            prefix += surf
    stats["abstract"] += 1
    return tn, tr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--rt")
    ap.add_argument("--norm")
    ap.add_argument("--jobs", type=int, default=16)
    a = ap.parse_args()
    lines = open(a.inp, encoding="utf-8").read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    tn, tr, norm, stats = segment(lines, a.jobs)
    open(a.out, "w", encoding="utf-8").write("\n".join(tn) + "\n")
    if a.rt:
        open(a.rt, "w", encoding="utf-8").write("\n".join(tr) + "\n")
    if a.norm:
        open(a.norm, "w", encoding="utf-8").write("\n".join(norm) + "\n")
    w = stats["words"] or 1
    print(f"lines {len(lines)} words {stats['words']} abstracted {stats['abstract']/w:.1%} "
          f"fallback {stats['fallback']/w:.1%} (pred {stats['fallback-pred']}) predicates {stats['pred']} "
          f"finals {stats['final']} particles {stats['particle']} literal particles {stats['particle-lit']}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
