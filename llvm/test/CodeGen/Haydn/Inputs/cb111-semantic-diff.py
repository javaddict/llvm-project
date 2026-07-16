#!/usr/bin/env python3
"""compare bundle instruction multisets (ignore nop / free-slot cosmetics)."""
import re
import sys
from collections import Counter
from pathlib import Path


def bundles(path: str):
    out = []
    for line in Path(path).read_text().splitlines():
        m = re.search(r"\{(.*)\}", line)
        if not m:
            continue
        parts = [x.strip() for x in m.group(1).split(";")]
        out.append(tuple(x for x in parts if x and x != "nop"))
    return out


def main():
    if len(sys.argv) != 3:
        print("usage: cb111-semantic-diff.py a.dis b.dis", file=sys.stderr)
        return 2
    c, s = bundles(sys.argv[1]), bundles(sys.argv[2])
    if len(c) != len(s):
        print(f"bundle count mismatch: {len(c)} vs {len(s)}", file=sys.stderr)
        return 1
    bad = [
        (i, a, b)
        for i, (a, b) in enumerate(zip(c, s))
        if Counter(a) != Counter(b)
    ]
    if bad:
        for i, a, b in bad[:10]:
            print(f"bundle {i}:\n  a={a}\n  b={b}", file=sys.stderr)
        print(f"{len(bad)} content multiset diffs", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
