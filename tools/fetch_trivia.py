#!/usr/bin/env python3
"""Download every verified Open Trivia DB question (opentdb.com, CC BY-SA 4.0)
into assets/trivia/opentdb_raw.json. Claude's side (Linux); the firmware
never fetches anything. The API allows one request every 5 seconds per
address, so this takes about ten minutes.

Usage: python3 tools/fetch_trivia.py
"""
import json, os, sys, time, urllib.parse, urllib.request

API = "https://opentdb.com"
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "trivia", "opentdb_raw.json")


def get(path):
    for attempt in range(12):
        try:
            with urllib.request.urlopen(API + path, timeout=30) as r:
                data = json.loads(r.read().decode())
        except OSError as e:                         # the network dropped: wait and try again
            print(f"retry: {e}", file=sys.stderr, flush=True)
            time.sleep(10)
            continue
        if data.get("response_code") == 5:          # too many requests
            time.sleep(6)
            continue
        return data
    raise SystemExit("rate limited too often")


def save(out):
    with open(OUT + ".tmp", "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=0)
    os.replace(OUT + ".tmp", OUT)


def main():
    token = get("/api_token.php?command=request")["token"]
    out = []
    if os.path.exists(OUT):                          # resume: keep what an earlier run got
        with open(OUT, encoding="utf-8") as f:
            out = json.load(f)
    seen = {q["question"] for q in out}
    while True:
        time.sleep(5.2)
        d = get(f"/api.php?amount=50&encode=url3986&token={token}")
        code = d.get("response_code")
        if code == 1 or code == 4:                  # fewer than 50 left: take them in smaller bites
            got = False
            for amount in (20, 10, 5, 1):
                time.sleep(5.2)
                d = get(f"/api.php?amount={amount}&encode=url3986&token={token}")
                if d.get("response_code") == 0:
                    got = True
                    break
            if not got:
                break
        elif code != 0:
            raise SystemExit(f"response code {code}")
        for q in d["results"]:
            item = {k: (urllib.parse.unquote(v) if isinstance(v, str) else [urllib.parse.unquote(x) for x in v])
                    for k, v in q.items()}
            key = item["question"]
            if key in seen:
                continue
            seen.add(key)
            out.append(item)
        print(len(out), file=sys.stderr, flush=True)
        save(out)
    save(out)
    print(f"{len(out)} questions -> {OUT}")


if __name__ == "__main__":
    main()
