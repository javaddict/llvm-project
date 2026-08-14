#!/usr/bin/env python3
"""Pin that retired old-format TableGen shells stay deleted.

Product is Format E96 12-byte only. Live Haydn Target .td/.cpp/.h must not
reintroduce HaydnInst16, HaydnInstrInfoC includes, hypo_encoding /
encoding_manual citations, BUNDLE128, HaydnDClassOpcodes, HaydnMCFlags,
HaydnFlexLayout, retired EncodedWidth tags (EW_16Bit..), or BUNDLE_*BIT_TAG.

Tombstone comments that name the ban (deleted / forbidden / never-reintroduce
/ no MCFlags) are allowed. Hard-fail `include "HaydnInstrInfoC.td"` and
`class HaydnInst16` even in comments.

Usage:
  check_retired_format_shells.py
  check_retired_format_shells.py --llvm-src PATH
  check_retired_format_shells.py --self-test

Exit:
  0  no retired shells in Target sources
  1  pin failure
  2  usage / missing Target dir

Peer: check_mc_mnemonic_coverage.py (lit RUN python pin).
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import List, Tuple

SCAN_REL = Path("llvm/lib/Target/Haydn")
SCAN_SUFFIXES = {".td", ".cpp", ".h"}

HARD_PATTERNS: List[Tuple[re.Pattern[str], str]] = [
    (
        re.compile(r'include\s+"HaydnInstrInfoC(\.td)?"'),
        'include "HaydnInstrInfoC.td"',
    ),
    (re.compile(r"\bclass\s+HaydnInst16\b"), "class HaydnInst16"),
]

IDENT_RE = re.compile(
    r"\b(HaydnInst16|hypo_encoding|encoding_manual(?:_flex)?|BUNDLE128|"
    r"HaydnDClassOpcodes|HaydnMCFlags|HaydnFlexLayout|"
    r"EW_(?:16|32|48|64)Bit|BUNDLE_(?:16|32|48|64)BIT_TAG|"
    r"getEncodedWidth|getSlotMask)\b",
    re.IGNORECASE,
)

# Comments that mention a banned name only to record the ban.
TOMBSTONE_RE = re.compile(
    r"forbidden|never-reintroduce|tombstone|do not resurrect|"
    r"must not appear|do not re-?include|\bno MCFlags\b|"
    r"\bdeleted\b|\bretired\b|\bban(?:ned)?\b",
    re.IGNORECASE,
)


def monorepo_from_script() -> Path:
    # llvm/utils/haydn/check_retired_format_shells.py -> parents[3]
    return Path(__file__).resolve().parents[3]


def split_line_comment(line: str) -> Tuple[str, str]:
    """Split a source line into code and `//` comment (td/cpp/h)."""
    in_str = False
    i = 0
    while i < len(line) - 1:
        ch = line[i]
        if ch == '"' and not in_str:
            in_str = True
        elif ch == '"' and in_str:
            in_str = False
        elif not in_str and ch == "/" and line[i + 1] == "/":
            return line[:i], line[i:]
        i += 1
    return line, ""


def scan_file(path: Path) -> List[str]:
    hits: List[str] = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return [f"{path}: read error: {exc}"]
    for lineno, raw in enumerate(text.splitlines(), 1):
        code, comment = split_line_comment(raw)
        for pat, label in HARD_PATTERNS:
            if pat.search(raw):
                hits.append(f"{path}:{lineno}: {label}")
        for m in IDENT_RE.finditer(raw):
            token = m.group(0)
            span = m.span()
            in_code = span[0] < len(code)
            if in_code:
                hits.append(f"{path}:{lineno}: {token}")
                continue
            if TOMBSTONE_RE.search(comment):
                continue
            hits.append(f"{path}:{lineno}: {token}")
    # Dedup identical line+token (hard + ident overlap on class HaydnInst16).
    seen = set()
    uniq: List[str] = []
    for h in hits:
        if h in seen:
            continue
        seen.add(h)
        uniq.append(h)
    return uniq


def iter_target_files(llvm_src: Path) -> List[Path]:
    root = llvm_src / SCAN_REL
    files: List[Path] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in SCAN_SUFFIXES:
            continue
        files.append(path)
    return files


def check(llvm_src: Path) -> Tuple[int, dict]:
    report: dict = {"ok": False, "hits": [], "errors": [], "files": 0}
    root = llvm_src / SCAN_REL
    if not root.is_dir():
        report["errors"].append(f"missing {root}")
        return 2, report
    files = iter_target_files(llvm_src)
    report["files"] = len(files)
    hits: List[str] = []
    for path in files:
        hits.extend(scan_file(path))
    report["hits"] = hits
    report["ok"] = not hits and not report["errors"]
    return (0 if report["ok"] else 1), report


def _self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        td = root / "llvm/lib/Target/Haydn"
        td.mkdir(parents=True)

        (td / "clean.td").write_text(
            "// Bundle128 composites are deleted.\n"
            "// HaydnMCFlags placement writers are forbidden.\n"
            "// no MCFlags tombstone; do not resurrect HaydnDClassOpcodes.\n"
            'include "HaydnInstrInfo.td"\n'
            "class HaydnInst48<dag outs> : HaydnInst<6>;\n",
            encoding="utf-8",
        )
        rc, rep = check(root)
        assert rc == 0 and rep["ok"], rep

        (td / "bad_include.td").write_text(
            'include "HaydnInstrInfoC.td"\n', encoding="utf-8"
        )
        rc, rep = check(root)
        assert rc == 1 and any("HaydnInstrInfoC" in h for h in rep["hits"]), rep
        (td / "bad_include.td").unlink()

        (td / "bad_class.td").write_text(
            "class HaydnInst16<dag outs, dag ins, string asmstr, list<dag> pattern>\n"
            "    : HaydnInst<2>;\n",
            encoding="utf-8",
        )
        rc, rep = check(root)
        assert rc == 1 and any("HaydnInst16" in h for h in rep["hits"]), rep
        (td / "bad_class.td").unlink()

        (td / "bad_hypo.cpp").write_text(
            "// Database/hypo_encoding/encoding_manual.md\n",
            encoding="utf-8",
        )
        rc, rep = check(root)
        assert rc == 1 and any("hypo_encoding" in h for h in rep["hits"]), rep
        (td / "bad_hypo.cpp").unlink()

        (td / "bad_id.h").write_text(
            "void HaydnMCFlags();\n", encoding="utf-8"
        )
        rc, rep = check(root)
        assert rc == 1 and any("HaydnMCFlags" in h for h in rep["hits"]), rep
        (td / "bad_id.h").unlink()

        (td / "bad_width.h").write_text(
            "constexpr unsigned BUNDLE_16BIT_TAG = 0;\n"
            "inline EncodedWidth getEncodedWidth(uint64_t T);\n"
            "enum { EW_32Bit = 1 };\n",
            encoding="utf-8",
        )
        rc, rep = check(root)
        assert rc == 1 and any("BUNDLE_16BIT_TAG" in h for h in rep["hits"]), rep
        assert any("getEncodedWidth" in h for h in rep["hits"]), rep
        assert any("EW_32Bit" in h for h in rep["hits"]), rep
        (td / "bad_width.h").unlink()

        (td / "bad_flex.td").write_text(
            "// Encoding model (encoding_manual_flex.md §1.2):\n",
            encoding="utf-8",
        )
        rc, rep = check(root)
        assert rc == 1 and any("encoding_manual" in h.lower() for h in rep["hits"]), rep
        (td / "bad_flex.td").unlink()

        rc, rep = check(root)
        assert rc == 0 and rep["ok"], rep
    print("check_retired_format_shells self-test OK")
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--llvm-src",
        type=Path,
        default=None,
        help="monorepo root (default: derived from this script)",
    )
    ap.add_argument(
        "--self-test",
        "--self-check",
        action="store_true",
        dest="self_test",
        help="run synthetic retired-shell vectors",
    )
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    llvm_src = (args.llvm_src or monorepo_from_script()).resolve()
    if not (llvm_src / "llvm").is_dir():
        sys.stderr.write(
            f"check_retired_format_shells: not an llvm monorepo: {llvm_src}\n"
        )
        return 2

    rc, report = check(llvm_src)
    print(
        f"check_retired_format_shells: files={report['files']} "
        f"hits={len(report['hits'])} ok={report['ok']}"
    )
    for e in report["errors"]:
        print(f"  ERROR: {e}", file=sys.stderr)
    for h in report["hits"]:
        print(f"  HIT: {h}", file=sys.stderr)
    if report["ok"]:
        print("check_retired_format_shells: PASS")
    else:
        print("check_retired_format_shells: FAIL", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
