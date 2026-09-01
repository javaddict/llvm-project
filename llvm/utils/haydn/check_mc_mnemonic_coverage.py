#!/usr/bin/env python3
"""Pin 100% MC coverage of product Format E non-NOP logical mnemonics.

Extracts unique non-NOP logical names from HaydnGenFormatERecords.inc
FormatEAltSpans (PIN_UNIQUE_NON_NOP = 814) and fails if any name is absent
from llvm/test/MC/Haydn/. Hypothesized HaydnInstrInfoManual.td encodings are
isCodeGenOnly and are not this pin.

Usage:
  check_mc_mnemonic_coverage.py
  check_mc_mnemonic_coverage.py --llvm-src PATH
  check_mc_mnemonic_coverage.py --self-test

Exit:
  0  every product mnemonic appears in MC tests (814/814)
  1  missing mnemonics or pin mismatch
  2  usage / missing paths

Peer: check_xfail_ledger.py (lit RUN python pin); Hexagon v67_all.s coverage
shape; BundleSim generate_catalog.py --check regenerate-and-compare.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Set, Tuple

PIN_UNIQUE_NON_NOP = 814
RECORDS_REL = "llvm/lib/Target/Haydn/HaydnGenFormatERecords.inc"
MC_REL = "llvm/test/MC/Haydn"
SCAN_SUFFIXES = {".s"}

_RE_COUNT = re.compile(
    r"static constexpr unsigned FormatENonNopLogicalCount = (\d+)u;"
)
_RE_SPAN_BLOCK = re.compile(
    r"static constexpr FormatEAltSpan FormatEAltSpans\[\] = \{(.+?)^\};",
    re.MULTILINE | re.DOTALL,
)
_RE_SPAN = re.compile(r'\{\s*"([^"]+)"\s*,\s*\d+\s*,\s*\d+\s*\}')


def monorepo_from_script() -> Path:
    # llvm/utils/haydn/check_mc_mnemonic_coverage.py -> parents[3]
    return Path(__file__).resolve().parents[3]


def logical_asm_mnemonic(logical: str) -> str:
    """User-facing assembler mnemonic for a golden logical name."""
    name = (logical or "").strip()
    if name.upper().startswith("WFI"):
        return "wfi"
    return name.lower()


def extract_logicals(inc_path: Path) -> Tuple[int, List[str]]:
    text = inc_path.read_text(encoding="utf-8")
    cm = _RE_COUNT.search(text)
    if not cm:
        raise ValueError(f"missing FormatENonNopLogicalCount in {inc_path}")
    count = int(cm.group(1))
    bm = _RE_SPAN_BLOCK.search(text)
    if not bm:
        raise ValueError(f"missing FormatEAltSpans in {inc_path}")
    names = _RE_SPAN.findall(bm.group(1))
    return count, names


def mnemonic_token_re(mnem: str) -> re.Pattern[str]:
    return re.compile(
        r"(?<![A-Za-z0-9_])" + re.escape(mnem) + r"(?![A-Za-z0-9_])"
    )


def collect_mc_text(mc_root: Path) -> str:
    chunks: List[str] = []
    for path in sorted(mc_root.rglob("*")):
        if not path.is_file() or path.suffix not in SCAN_SUFFIXES:
            continue
        if "Inputs" in path.parts:
            continue
        chunks.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(chunks)


def check(llvm_src: Path) -> Tuple[int, dict]:
    inc = llvm_src / RECORDS_REL
    mc_root = llvm_src / MC_REL
    report: dict = {
        "ok": False,
        "pin": PIN_UNIQUE_NON_NOP,
        "declared": None,
        "logicals": 0,
        "covered": 0,
        "missing": [],
        "errors": [],
    }
    if not inc.is_file():
        report["errors"].append(f"missing {inc}")
        return 1, report
    if not mc_root.is_dir():
        report["errors"].append(f"missing {mc_root}")
        return 1, report
    try:
        declared, names = extract_logicals(inc)
    except ValueError as exc:
        report["errors"].append(str(exc))
        return 1, report
    report["declared"] = declared
    report["logicals"] = len(names)
    if declared != PIN_UNIQUE_NON_NOP:
        report["errors"].append(
            f"FormatENonNopLogicalCount={declared} != pin {PIN_UNIQUE_NON_NOP}"
        )
    if len(names) != PIN_UNIQUE_NON_NOP:
        report["errors"].append(
            f"FormatEAltSpans count {len(names)} != pin {PIN_UNIQUE_NON_NOP}"
        )
    if len(names) != declared:
        report["errors"].append(
            f"FormatEAltSpans count {len(names)} != declared {declared}"
        )

    corpus = collect_mc_text(mc_root)
    missing: List[str] = []
    for logical in names:
        mnem = logical_asm_mnemonic(logical)
        if not mnemonic_token_re(mnem).search(corpus):
            missing.append(f"{logical} ({mnem})")
    report["missing"] = missing
    report["covered"] = len(names) - len(missing)
    ok = not missing and not report["errors"] and len(names) == PIN_UNIQUE_NON_NOP
    report["ok"] = ok
    return (0 if ok else 1), report


def _self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        inc_dir = root / "llvm/lib/Target/Haydn"
        mc = root / "llvm/test/MC/Haydn"
        inc_dir.mkdir(parents=True)
        mc.mkdir(parents=True)
        inc = inc_dir / "HaydnGenFormatERecords.inc"
        inc.write_text(
            "static constexpr unsigned FormatENonNopLogicalCount = 2u;\n"
            "static constexpr FormatEAltSpan FormatEAltSpans[] = {\n"
            '  {"ADD32", 0, 7},\n'
            '  {"D_LDW_POST_REG", 7, 4},\n'
            "};\n",
            encoding="utf-8",
        )
        (mc / "sample.s").write_text("{ add32 r1, r2, r3; nop; nop }\n", encoding="utf-8")
        rc, rep = check(root)
        assert rc == 1 and rep["covered"] == 1, rep
        (mc / "d.s").write_text(
            "{ d_ldw_post_reg d0, r1, r2; nop; nop }\n", encoding="utf-8"
        )
        # Local pin is 683; self-test uses 2 so expect pin mismatch + full names.
        # Rewrite pin via monkeypatching would hide the real check; instead
        # verify extract + token search in isolation.
        declared, names = extract_logicals(inc)
        assert declared == 2 and names == ["ADD32", "D_LDW_POST_REG"], names
        corpus = collect_mc_text(mc)
        assert mnemonic_token_re("add32").search(corpus)
        assert mnemonic_token_re("d_ldw_post_reg").search(corpus)
        assert not mnemonic_token_re("abs32").search(corpus)
    print("check_mc_mnemonic_coverage self-test OK")
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
        help="run synthetic coverage vectors",
    )
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    llvm_src = (args.llvm_src or monorepo_from_script()).resolve()
    if not (llvm_src / "llvm").is_dir():
        sys.stderr.write(
            f"check_mc_mnemonic_coverage: not an llvm monorepo: {llvm_src}\n"
        )
        return 2

    rc, report = check(llvm_src)
    print(
        f"check_mc_mnemonic_coverage: "
        f"{report['covered']}/{report['logicals']} "
        f"pin={report['pin']} declared={report['declared']} ok={report['ok']}"
    )
    for e in report["errors"]:
        print(f"  ERROR: {e}", file=sys.stderr)
    for m in report["missing"]:
        print(f"  MISSING: {m}", file=sys.stderr)
    if report["ok"]:
        print(
            f"check_mc_mnemonic_coverage: PASS "
            f"({PIN_UNIQUE_NON_NOP}/{PIN_UNIQUE_NON_NOP})"
        )
    else:
        print("check_mc_mnemonic_coverage: FAIL", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
