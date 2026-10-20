#!/usr/bin/env python3
"""Run the public neutral translation fixtures without changing expectations."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree_hash(files):
    text = "".join(f"{name}\t{digest}\n" for name, digest in sorted(files.items()))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--dicts", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--cases", type=Path,
                        default=Path(__file__).resolve().parents[1] / "benchmarks/neutral.tsv")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    cli, dictionaries, source = (p.resolve() for p in (args.cli, args.dicts, args.source))
    with args.cases.open(encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file, delimiter="\t")
        if reader.fieldnames != ["id", "group", "kind", "input", "expected"]:
            parser.error("invalid fixture header")
        cases = list(reader)
    if not cases or len({row["id"] for row in cases}) != len(cases):
        parser.error("fixtures need unique IDs")
    if any(set(row) != set(reader.fieldnames) or None in row.values()
           or not all(row.values()) or row["kind"] not in {"fix", "keep", "example"}
           or any("\n" in value or "\r" in value for value in row.values()) for row in cases):
        parser.error("invalid fixture row")
    source_files = {}
    for directory in ("src", "include", "cmake"):
        for path in sorted((source / directory).rglob("*")):
            if path.is_file():
                source_files[path.relative_to(source).as_posix()] = sha256(path)
    for name in ("CMakeLists.txt", "release-version.txt"):
        source_files[name] = sha256(source / name)
    if dictionaries.is_dir():
        dict_files = {path.name: sha256(path) for path in sorted(dictionaries.glob("*.tsv"))}
        if not dict_files:
            parser.error("no dictionary TSV files")
        dictionary = {"mode": "tsv", "sha256": tree_hash(dict_files), "files": dict_files}
    else:
        dictionary = {"mode": "snapshot", "sha256": sha256(dictionaries)}
    with tempfile.TemporaryDirectory(prefix="hdtrans-neutral-") as temporary:
        work = Path(temporary)
        home, users = work / "home", work / "users"
        home.mkdir()
        users.mkdir()
        config, inputs = work / "empty.yaml", work / "input.txt"
        config.write_text("{}\n", encoding="utf-8")
        inputs.write_text("".join(row["input"] + "\n" for row in cases), encoding="utf-8")
        env = {key: value for key, value in os.environ.items() if not key.startswith("HDTRANS_")}
        env.update(HOME=str(home), USERPROFILE=str(home), HDTRANS_CACHE_DIR="off")
        base = [str(cli), "--dicts", str(dictionaries), "--config", str(config),
                "--userdict", str(users), "--no-cache"]
        version = subprocess.run(base + ["--version"], env=env, cwd=work,
                                 capture_output=True, text=True, encoding="utf-8", check=True)
        run = subprocess.run(base + ["--each", "--in", str(inputs)], env=env, cwd=work,
                             capture_output=True, text=True, encoding="utf-8", check=True)
    outputs = run.stdout.splitlines()
    if len(outputs) != len(cases):
        raise RuntimeError(f"expected {len(cases)} output lines, got {len(outputs)}")
    rows, groups, kinds = [], {}, {}
    for case, actual in zip(cases, outputs):
        matched = int(actual == case["expected"])
        rows.append({**case, "actual": actual, "matched": matched})
        for counts, key in ((groups, case["group"]), (kinds, case["kind"])):
            item = counts.setdefault(key, {"matched": 0, "total": 0})
            item["matched"] += matched
            item["total"] += 1
    result = {
        "label": args.label,
        "source_revision": args.revision,
        "source_sha256": tree_hash(source_files),
        "source_hash_scope": "src/**, include/**, cmake/**, CMakeLists.txt, release-version.txt; sorted path TAB SHA256 LF",
        "cli_sha256": sha256(cli),
        "dictionary": dictionary,
        "version": version.stdout.strip(),
        "runtime": {"os": platform.system(), "architecture": platform.machine()},
        "fixture_sha256": sha256(args.cases),
        "runner_sha256": sha256(Path(__file__).resolve()),
        "translation_options": ["--each", "--no-cache", "--config EMPTY", "--userdict EMPTY"],
        "score": {"matched": sum(row["matched"] for row in rows), "total": len(rows)},
        "by_kind": kinds,
        "by_group": groups,
        "stdout_sha256": hashlib.sha256(run.stdout.encode("utf-8")).hexdigest(),
    }
    args.out.mkdir(parents=True, exist_ok=False)
    with (args.out / "results.tsv").open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    (args.out / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                                            encoding="utf-8")
    print(f"{args.label}: {result['score']['matched']}/{len(rows)} exact matches")
    for kind, count in kinds.items():
        print(f"  {kind}: {count['matched']}/{count['total']}")


if __name__ == "__main__":
    main()
