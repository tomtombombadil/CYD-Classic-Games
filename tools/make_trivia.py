#!/usr/bin/env python3
"""Build the trivia question bank for the trivia games.

Reads assets/trivia/opentdb_raw.json (Open Trivia DB, opentdb.com, CC BY-SA
4.0 - fetched by tools/fetch_trivia.py), leaves out the questions listed in
assets/trivia/drop.txt (screened: subjects inappropriate for minors, stale
answers), turns the text into plain ASCII (the screen fonts have no accents),
and writes src/games/common/trivia_data.cpp: the questions packed in blocks
of 32, each block raw-DEFLATE compressed, plus one byte of facts per
question. That file is the screened and edited Open Trivia DB data and is
CC BY-SA 4.0 like its source; the code that reads it stays MIT.

Usage: python3 tools/make_trivia.py
"""
import json, os, re, sys, unicodedata, zlib

ROOT = os.path.join(os.path.dirname(__file__), "..")
RAW = os.path.join(ROOT, "assets", "trivia", "opentdb_raw.json")
DROP = os.path.join(ROOT, "assets", "trivia", "drop.txt")
OUT = os.path.join(ROOT, "src", "games", "common", "trivia_data.cpp")
BLOCK = 32

# Open Trivia DB's categories, in a fixed order (the index is in the bank -
# append only), with the short names the games show
CATEGORIES = [
    ("General Knowledge", "General Knowledge"),
    ("Entertainment: Books", "Books"),
    ("Entertainment: Film", "Film"),
    ("Entertainment: Music", "Music"),
    ("Entertainment: Musicals & Theatres", "Musicals & Theatre"),
    ("Entertainment: Television", "Television"),
    ("Entertainment: Video Games", "Video Games"),
    ("Entertainment: Board Games", "Board Games"),
    ("Science & Nature", "Science & Nature"),
    ("Science: Computers", "Computers"),
    ("Science: Mathematics", "Mathematics"),
    ("Mythology", "Mythology"),
    ("Sports", "Sports"),
    ("Geography", "Geography"),
    ("History", "History"),
    ("Politics", "Politics"),
    ("Art", "Art"),
    ("Celebrities", "Celebrities"),
    ("Animals", "Animals"),
    ("Vehicles", "Vehicles"),
    ("Entertainment: Comics", "Comics"),
    ("Science: Gadgets", "Gadgets"),
    ("Entertainment: Japanese Anime & Manga", "Anime & Manga"),
    ("Entertainment: Cartoon & Animations", "Cartoons"),
]
DIFFS = {"easy": 0, "medium": 1, "hard": 2}

# Characters the screen fonts lack, by code point
SWAPS = {chr(k): v for k, v in {
    0x2018: "'", 0x2019: "'", 0x201C: '"', 0x201D: '"', 0x2013: "-", 0x2014: "-", 0x2026: "...",
    0x00A0: " ", 0x00D7: "x", 0x00B0: " degrees", 0x00BD: "1/2", 0x00BC: "1/4", 0x00BE: "3/4",
    0x00B2: "^2", 0x00B3: "^3", 0x00DF: "ss", 0x00E6: "ae", 0x00C6: "AE", 0x0153: "oe", 0x00F8: "o",
    0x00D8: "O", 0x0142: "l", 0x0141: "L", 0x00F0: "d", 0x00FE: "th", 0x2032: "'", 0x2033: '"',
    0x2212: "-", 0x00AE: "", 0x2122: "", 0x00A9: "(c)", 0x20AC: "EUR ", 0x00A3: "GBP ", 0x00A5: "JPY ",
    0x03C0: "pi", 0x2192: "->", 0x00B1: "+/-", 0x00B5: "u", 0x00E5: "a", 0x00C5: "A",
    0x00AD: "", 0x200E: "", 0x02BB: "'", 0x00BF: "",
}.items()}


def ascii_text(s):
    for a, b in SWAPS.items():
        s = s.replace(a, b)
    s = unicodedata.normalize("NFKD", s)
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = re.sub(r"\s+", " ", s).strip()
    return s if all(32 <= ord(c) < 127 for c in s) else None


def main():
    raw = json.load(open(RAW, encoding="utf-8"))
    drop = set()
    for line in open(DROP, encoding="utf-8"):
        line = line.rstrip("\n")
        if line and not line.startswith("#"):
            drop.add(line.strip())
    cats = {c[0]: i for i, c in enumerate(CATEGORIES)}
    keep, dropped, non_ascii = [], 0, 0
    for q in raw:
        if q["question"].strip() in drop:
            dropped += 1
            continue
        fields = [ascii_text(q["question"]), ascii_text(q["correct_answer"])] + [ascii_text(a) for a in q["incorrect_answers"]]
        if any(f is None or f == "" for f in fields):
            non_ascii += 1
            continue
        boolean = q["type"] == "boolean"
        if len(fields) != (3 if boolean else 5):
            continue
        if max(len(f) for f in fields[1:]) > 127 or len(fields[0]) > 319:
            sys.exit(f"too long for the screen's buffers: {fields[0]}")
        meta = cats[q["category"]] | DIFFS[q["difficulty"]] << 5 | (0x80 if boolean else 0)
        keep.append((meta, fields))
    # A fixed shuffle (so neighbours aren't all one category) that the same input always repeats
    keep.sort(key=lambda k: zlib.crc32(k[1][0].encode()))
    blocks, metas, sizes = [], [], []
    for b in range(0, len(keep), BLOCK):
        part = keep[b:b + BLOCK]
        text = b"".join(("\x1f".join(f) + "\x1e").encode("ascii") for _, f in part)
        sizes.append(len(text))
        co = zlib.compressobj(9, zlib.DEFLATED, -15)
        blocks.append(co.compress(text) + co.flush())
        metas.extend(m for m, _ in part)
    data = b"".join(blocks)
    offsets = [0]
    for blk in blocks:
        offsets.append(offsets[-1] + len(blk))
    checksum = 0
    for _, f in keep:
        for s in f:
            for c in s.encode():
                checksum = (checksum * 31 + c) & 0xFFFFFFFF
    with open(OUT, "w", encoding="ascii") as o:
        o.write("// Generated by tools/make_trivia.py - do not edit (edit assets/trivia/drop.txt or the tool).\n")
        o.write("//\n// Trivia questions from Open Trivia DB (https://opentdb.com), licensed CC BY-SA 4.0\n")
        o.write("// (https://creativecommons.org/licenses/by-sa/4.0/). Changed: screened (questions\n")
        o.write("// listed in assets/trivia/drop.txt left out) and edited (text made plain ASCII).\n")
        o.write("// This data file is CC BY-SA 4.0 like its source; the rest of the firmware is MIT.\n")
        o.write('#include "trivia_bank.h"\n\nnamespace trivia {\n\n')
        o.write(f"const int kQuestions = {len(keep)};\nconst int kBlockSize = {BLOCK};\n")
        o.write(f"const int kBlocks = {len(blocks)};\nconst int kMaxBlockText = {max(sizes)};\n")
        o.write(f"const uint32_t kChecksum = 0x{checksum:08X}u;\n\n")
        o.write("const uint32_t kBlockOffset[] = {\n")
        for i in range(0, len(offsets), 10):
            o.write("    " + ", ".join(str(x) for x in offsets[i:i + 10]) + ",\n")
        o.write("};\n\nconst uint8_t kMeta[] = {\n")
        for i in range(0, len(metas), 24):
            o.write("    " + ", ".join(f"0x{m:02X}" for m in metas[i:i + 24]) + ",\n")
        o.write("};\n\nconst uint8_t kData[] = {\n")
        for i in range(0, len(data), 24):
            o.write("    " + ", ".join(str(x) for x in data[i:i + 24]) + ",\n")
        o.write("};\n\n} // namespace trivia\n")
    print(f"{len(keep)} questions ({dropped} screened out, {non_ascii} not plain text), "
          f"{len(blocks)} blocks, {len(data)} bytes packed (largest block {max(sizes)} bytes)")


if __name__ == "__main__":
    main()
