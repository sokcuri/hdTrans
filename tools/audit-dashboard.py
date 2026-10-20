#!/usr/bin/env python3
"""Serve the progress of a tools/judge-dict.py run over HTTP.

Usage: tools/audit-dashboard.py [--port 8765] [--progress FILE.json] [--rejected FILE.tsv]
                                [--bind 0.0.0.0]
The page refreshes itself, shows the rows checked and rejected, the rate and the
time left, and lists the last rejected rows. /state.json returns the same numbers.
"""
import argparse
import csv
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import time

PAGE = """<!doctype html>
<meta charset="utf-8">
<title>dictionary audit</title>
<style>
 body { font: 15px system-ui, sans-serif; margin: 2rem auto; max-width: 60rem; color: #1b1b1b; }
 h1 { font-size: 1.3rem; font-weight: 600; margin: 0 0 1.2rem; }
 .bar { height: 18px; background: #e6e6e6; border-radius: 9px; overflow: hidden; }
 .bar div { height: 100%; background: #3b6fd4; width: 0; transition: width .5s; }
 .grid { display: grid; grid-template-columns: repeat(5, 1fr); gap: 1rem; margin: 1.2rem 0; }
 .tile { border: 1px solid #ddd; border-radius: 8px; padding: .7rem .9rem; }
 .tile span { display: block; font-size: .8rem; color: #666; }
 .tile strong { font-size: 1.4rem; font-weight: 600; }
 table { border-collapse: collapse; width: 100%; font-size: .9rem; }
 th, td { text-align: left; padding: .35rem .5rem; border-bottom: 1px solid #eee; }
 th { color: #666; font-weight: 500; }
 .muted { color: #888; }
 .strong { font-weight: 600; color: #b23a3a; }
</style>
<h1>사전 감사 진행</h1>
<div class="bar"><div id="fill"></div></div>
<p id="line" class="muted"></p>
<div class="grid">
 <div class="tile"><span>검사 행</span><strong id="done">–</strong></div>
 <div class="tile"><span>전체</span><strong id="total">–</strong></div>
 <div class="tile"><span>오류 의심</span><strong id="rejected">–</strong></div>
 <div class="tile"><span>남은 시간</span><strong id="eta">–</strong></div>
</div>
<p class="muted" id="rate"></p>
<table><thead><tr><th>키</th><th>사전 한국어</th><th>JEV 선택</th><th>신뢰도</th><th>위치</th></tr></thead>
<tbody id="rows"></tbody></table>
<script>
function duration(s) {
  if (s === null || s === undefined) return '–';
  s = Math.round(s);
  if (s < 60) return s + '초';
  if (s < 3600) return Math.floor(s / 60) + '분 ' + (s % 60) + '초';
  return Math.floor(s / 3600) + '시간 ' + Math.floor((s % 3600) / 60) + '분';
}
async function tick() {
  try {
    const state = await (await fetch('/state.json')).json();
    document.getElementById('done').textContent = state.done.toLocaleString();
    document.getElementById('total').textContent = state.total.toLocaleString();
    document.getElementById('rejected').textContent = state.rejected.toLocaleString();
    document.getElementById('strong').textContent = state.strong.toLocaleString();
    document.getElementById('eta').textContent = duration(state.eta);
    const pct = state.total ? 100 * state.done / state.total : 0;
    document.getElementById('fill').style.width = pct.toFixed(1) + '%';
    document.getElementById('line').textContent = pct.toFixed(1) + '%';
    document.getElementById('rate').textContent =
      state.rate.toLocaleString() + '행/초 · ' + duration(state.elapsed) + ' 경과 · ' +
      state.updated + ' 갱신';
    const body = document.getElementById('rows');
    body.innerHTML = state.last.map(r =>
      '<tr><td>' + r.key + '</td><td>' + r.ko + '</td><td>' + r.pick + '</td><td' +
      (parseFloat(r.confidence) >= 0.6 ? ' class="strong"' : ' class="muted"') + '>' +
      r.confidence + '</td><td class="muted">' + r.source + '</td></tr>').join('');
  } catch (e) {
    document.getElementById('line').textContent = '감사 실행을 기다리는 중';
  }
}
tick();
setInterval(tick, 2000);
</script>
"""


def tail_rejected(path, count):
    if path is None or not path.is_file():
        return []
    with path.open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    return [{"key": r.get("key", ""), "ko": r.get("ko", ""), "pick": r.get("pick", ""),
             "confidence": r.get("confidence", ""), "source": r.get("source", "")}
            for r in rows[-count:]][::-1]


def strong_count(path):
    if path is None or not path.is_file():
        return 0
    with path.open(encoding="utf-8", newline="") as source:
        return sum(1 for r in csv.DictReader(source, delimiter="\t") if float(r.get("confidence", 0) or 0) >= 0.6)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--progress", type=Path, default=Path("/tmp/dict-audit/progress.json"))
    parser.add_argument("--rejected", type=Path, default=Path("/tmp/dict-audit/rejected.tsv"))
    args = parser.parse_args()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith("/state.json"):
                state = {"total": 0, "done": 0, "rejected": 0, "elapsed": 0, "rate": 0,
                         "eta": None, "updated": "", "last": tail_rejected(args.rejected, 25)}
                if args.progress.is_file():
                    try:
                        state.update(json.loads(args.progress.read_text(encoding="utf-8")))
                    except (OSError, ValueError):
                        pass
                state["last"] = tail_rejected(args.rejected, 25)
                state["strong"] = strong_count(args.rejected)
                body = json.dumps(state, ensure_ascii=False).encode()
                self.send_response(200)
                self.send_header("content-type", "application/json; charset=utf-8")
            else:
                body = PAGE.encode()
                self.send_response(200)
                self.send_header("content-type", "text/html; charset=utf-8")
            self.send_header("content-length", str(len(body)))
            self.send_header("cache-control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *arguments):
            pass

    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    print(f"dashboard on http://{args.bind}:{args.port}/ (progress {args.progress})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
