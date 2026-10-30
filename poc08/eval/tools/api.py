"""Messages API helper for the eval tools: one structured-output call with retries, and a JSONL cache.

Credentials: ANTHROPIC_AUTH_TOKEN (Bearer) or ANTHROPIC_API_KEY; ANTHROPIC_BASE_URL (default
https://api.anthropic.com). Evaluation only: nothing here ships.
"""
import hashlib
import json
import os
import threading
import time
import urllib.error
import urllib.request


def _headers():
    h = {"content-type": "application/json", "anthropic-version": "2023-06-01"}
    if os.environ.get("ANTHROPIC_AUTH_TOKEN"):
        h["authorization"] = "Bearer " + os.environ["ANTHROPIC_AUTH_TOKEN"]
    elif os.environ.get("ANTHROPIC_API_KEY"):
        h["x-api-key"] = os.environ["ANTHROPIC_API_KEY"]
    else:
        raise SystemExit("set ANTHROPIC_AUTH_TOKEN or ANTHROPIC_API_KEY")
    return h


def call(model, prompt, schema, effort="medium", max_tokens=32000, validate=None, tries=6):
    """Return the parsed JSON object the model wrote under `schema`; `validate(obj)` may raise to retry."""
    url = os.environ.get("ANTHROPIC_BASE_URL", "https://api.anthropic.com").rstrip("/") + "/v1/messages"
    body = json.dumps({
        "model": model, "max_tokens": max_tokens,
        "output_config": {"effort": effort, "format": {"type": "json_schema", "schema": schema}},
        "messages": [{"role": "user", "content": prompt}],
    }).encode()
    last = None
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, body, _headers())
            with urllib.request.urlopen(req, timeout=900) as r:
                data = json.load(r)
            if data.get("stop_reason") == "max_tokens":
                raise ValueError("truncated response")
            text = "".join(c.get("text", "") for c in data["content"] if c["type"] == "text")
            obj = json.loads(text)
            if validate:
                validate(obj)
            return obj
        except (urllib.error.URLError, ValueError, KeyError, TimeoutError, OSError) as e:
            detail = e.read()[:300] if hasattr(e, "read") else b""
            last = f"{e} {detail!r}"
            time.sleep(min(60, 5 * 2 ** attempt))
    raise RuntimeError(f"{model}: gave up after {tries} tries: {last}")


def key(*parts):
    return hashlib.sha256("\x1f".join(parts).encode("utf-8")).hexdigest()


class Cache:
    """Append-only JSONL: {"k": key, "v": value}; later rows win."""

    def __init__(self, path):
        self.path, self.lock, self.d = path, threading.Lock(), {}
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    if line.strip():
                        try:
                            row = json.loads(line)
                        except json.JSONDecodeError:
                            continue  # a torn last line from an interrupted run
                        self.d[row["k"]] = row["v"]

    def get(self, k):
        return self.d.get(k)

    def put(self, k, v):
        with self.lock:
            self.d[k] = v
            with open(self.path, "a", encoding="utf-8") as f:
                f.write(json.dumps({"k": k, "v": v}, ensure_ascii=False) + "\n")
