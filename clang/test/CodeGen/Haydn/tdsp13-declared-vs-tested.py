#!/usr/bin/env python3
"""T-DSP13 declared-vs-tested pin + PublicEnabled ⇒ Semantics publish-gate.

Empty-Semantics leftovers were authored. Fails only when:
  * a PublicEnabled HaydnBuiltin/HaydnPairBuiltin gains empty Semantics
  * IntrinsicsHaydn.td cannot be parsed (floor)
Does not author a 746-name harness.

Usage:
  tdsp13-declared-vs-tested.py <IntrinsicsHaydn.td> <BuiltinsHaydn.td> \\
      <llvm/test/CodeGen/Haydn>
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

# PublicEnabled natives must publish Semantics. Empty leftover set is
# closed; this pin forbids a new hole.
EMPTY_SEMANTICS_LEFTOVER = frozenset()

INTRIN_FLOOR = 700


def parse_public_empty_semantics(text: str) -> list[str]:
    pat = re.compile(r"def\s+([A-Za-z0-9_]+)\s*:\s*(HaydnBuiltin|HaydnPairBuiltin)\b")
    recs = [(m.start(), m.group(1)) for m in pat.finditer(text)]
    empty: list[str] = []
    for i, (pos, name) in enumerate(recs):
        end = recs[i + 1][0] if i + 1 < len(recs) else len(text)
        body = text[pos:end]
        if re.search(r"let\s+PublicEnabled\s*=\s*0\b", body):
            continue
        sem = re.search(r'let\s+Semantics\s*=\s*"([^"]*)"', body)
        if not sem or not sem.group(1).strip():
            empty.append(name)
    return empty


def parse_int_haydn_names(text: str) -> list[str]:
    return re.findall(r"def\s+(int_haydn_[A-Za-z0-9_]+)\b", text)


def collect_test_blob(root: Path) -> str:
    parts: list[str] = []
    for p in root.rglob("*"):
        if p.suffix not in {".ll", ".mir", ".c", ".s", ".txt", ".td"}:
            continue
        try:
            parts.append(p.read_text(errors="ignore"))
        except OSError:
            continue
    return "\n".join(parts)


def mentioned(name: str, blob: str) -> bool:
    stem = name[len("int_haydn_") :]
    return (
        name in blob
        or ("llvm.haydn." + stem) in blob
        or ("llvm.haydn." + stem.replace("_", ".")) in blob
        or stem in blob
    )


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(
            "usage: tdsp13-declared-vs-tested.py "
            "IntrinsicsHaydn.td BuiltinsHaydn.td test/CodeGen/Haydn",
            file=sys.stderr,
        )
        return 2
    itd = Path(argv[1]).read_text()
    btd = Path(argv[2]).read_text()
    testdir = Path(argv[3])

    names = parse_int_haydn_names(itd)
    unique = sorted(set(names))
    if len(unique) < INTRIN_FLOOR:
        print(
            f"tdsp13 FAILED: parsed {len(unique)} int_haydn_* names "
            f"(floor {INTRIN_FLOOR})",
            file=sys.stderr,
        )
        return 1

    empty = parse_public_empty_semantics(btd)
    empty_set = set(empty)
    extra = sorted(empty_set - EMPTY_SEMANTICS_LEFTOVER)
    missing_pin = sorted(EMPTY_SEMANTICS_LEFTOVER - empty_set)
    if extra:
        print(
            "tdsp13 FAILED: new PublicEnabled empty Semantics:",
            ", ".join(extra),
            file=sys.stderr,
        )
        return 1
    if missing_pin:
        print(
            "tdsp13 FAILED: leftover Semantics pin is stale (now filled):",
            ", ".join(missing_pin),
            file=sys.stderr,
        )
        return 1

    blob = collect_test_blob(testdir)
    untested = [n for n in unique if not mentioned(n, blob)]
    print(
        f"tdsp13 OK: declared={len(unique)} untested={len(untested)} "
        f"empty_semantics_leftover={len(empty)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
