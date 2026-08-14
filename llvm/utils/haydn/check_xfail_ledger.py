#!/usr/bin/env python3
"""Verify live Haydn XFAIL directives match Inputs/XFAIL-OWNER-LEDGER.txt.

G-TEST-EVIDENCE law: every live XFAIL under CodeGen/Haydn + MC/Haydn must have
an owned ledger row (GE96 golden or live G-* goal). Unexplained XFAIL is a
product gate failure. Ledger rows that no longer have a live XFAIL are also
fail (stale inventory drift).

Usage:
  check_xfail_ledger.py                 # scan monorepo from this script
  check_xfail_ledger.py --llvm-src PATH
  check_xfail_ledger.py --self-test
  check_xfail_ledger.py --json

Exit:
  0  ledger matches live XFAIL set (TOTAL and PATH set)
  1  drift / unexplained XFAIL / parse failure
  2  usage / missing paths
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# Lit XFAIL directive (not prose containing "XFAIL").
_RE_XFAIL = re.compile(r"(?m)^[ \t]*[#;]+[ \t]*XFAIL:[ \t]*")
# Ledger fields.
_RE_TOTAL = re.compile(r"(?m)^TOTAL:\s*(\d+)\s*$")
_RE_PATH = re.compile(r"(?m)^PATH:\s*(\S+)\s*$")
_RE_OWNER = re.compile(r"(?m)^[ \t]*OWNER:\s*(.+?)\s*$")
_RE_STATUS = re.compile(r"(?m)^[ \t]*STATUS:\s*(.+?)\s*$")

SUITES = (
    "llvm/test/CodeGen/Haydn",
    "llvm/test/MC/Haydn",
)
LEDGER_REL = "llvm/test/CodeGen/Haydn/Inputs/XFAIL-OWNER-LEDGER.txt"
SCAN_SUFFIXES = {".ll", ".s", ".mir", ".test"}


def monorepo_from_script() -> Path:
    # llvm/utils/haydn/check_xfail_ledger.py -> monorepo root is parents[3]
    return Path(__file__).resolve().parents[3]


def find_live_xfails(llvm_src: Path) -> Dict[str, Path]:
    """Return map of repo-relative path -> absolute path for live XFAIL files."""
    found: Dict[str, Path] = {}
    for suite in SUITES:
        root = llvm_src / suite
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file() or path.suffix not in SCAN_SUFFIXES:
                continue
            # Skip Inputs/ and non-test scaffolding.
            rel_parts = path.relative_to(llvm_src).parts
            if "Inputs" in rel_parts:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            if _RE_XFAIL.search(text):
                rel = path.relative_to(llvm_src).as_posix()
                found[rel] = path
    return found


def parse_ledger(ledger_path: Path) -> Tuple[Optional[int], List[str], Dict[str, dict]]:
    text = ledger_path.read_text(encoding="utf-8", errors="replace")
    total_m = _RE_TOTAL.search(text)
    total = int(total_m.group(1)) if total_m else None
    paths: List[str] = []
    meta: Dict[str, dict] = {}
    # Split on PATH: blocks.
    blocks = re.split(r"(?m)^(?=PATH:\s*)", text)
    for block in blocks:
        if not block.lstrip().startswith("PATH:"):
            continue
        pm = _RE_PATH.search(block)
        if not pm:
            continue
        p = pm.group(1).strip()
        paths.append(p)
        om = _RE_OWNER.search(block)
        sm = _RE_STATUS.search(block)
        meta[p] = {
            "owner": om.group(1).strip() if om else "",
            "status": sm.group(1).strip() if sm else "",
        }
    return total, paths, meta


def check(llvm_src: Path) -> Tuple[int, dict]:
    ledger = llvm_src / LEDGER_REL
    report: dict = {
        "llvm_src": str(llvm_src),
        "ledger": str(ledger),
        "ok": False,
        "live": [],
        "ledger_paths": [],
        "ledger_total": None,
        "missing_in_ledger": [],
        "stale_in_ledger": [],
        "total_mismatch": False,
        "unowned": [],
        "errors": [],
    }
    if not ledger.is_file():
        report["errors"].append(f"missing ledger {ledger}")
        return 1, report

    live = find_live_xfails(llvm_src)
    report["live"] = sorted(live.keys())
    total, ledger_paths, meta = parse_ledger(ledger)
    report["ledger_total"] = total
    report["ledger_paths"] = list(ledger_paths)

    live_set: Set[str] = set(live.keys())
    ledger_set: Set[str] = set(ledger_paths)

    missing = sorted(live_set - ledger_set)
    stale = sorted(ledger_set - live_set)
    report["missing_in_ledger"] = missing
    report["stale_in_ledger"] = stale

    if total is None:
        report["errors"].append("ledger missing TOTAL: N line")
    elif total != len(ledger_paths):
        report["total_mismatch"] = True
        report["errors"].append(
            f"ledger TOTAL={total} != PATH count {len(ledger_paths)}"
        )
    elif total != len(live_set):
        report["total_mismatch"] = True
        report["errors"].append(
            f"ledger TOTAL={total} != live XFAIL count {len(live_set)}"
        )

    # Every ledger row needs a non-empty OWNER (GE96-* or live G-*).
    unowned: List[str] = []
    for p in ledger_paths:
        owner = meta.get(p, {}).get("owner", "")
        if not owner or owner.lower() in {"unknown", "none", "tbd", "?"}:
            unowned.append(p)
    report["unowned"] = unowned

    ok = (
        not missing
        and not stale
        and not unowned
        and not report["errors"]
        and total is not None
        and total == len(live_set)
        and total == len(ledger_paths)
    )
    report["ok"] = ok
    return (0 if ok else 1), report


def _self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        code = root / "llvm/test/CodeGen/Haydn"
        mc = root / "llvm/test/MC/Haydn"
        inputs = code / "Inputs"
        inputs.mkdir(parents=True)
        mc.mkdir(parents=True)

        live_a = mc / "owned-a.s"
        live_a.write_text("# XFAIL: *\n# XFAIL-OWNER: GE96-03\n", encoding="utf-8")
        live_b = mc / "owned-b.s"
        live_b.write_text("# XFAIL: *\n# XFAIL-OWNER: GE96-09\n", encoding="utf-8")
        # Prose must not count as XFAIL.
        (code / "history.ll").write_text(
            "; Previously XFAIL removed; now PASS.\n", encoding="utf-8"
        )

        ledger = inputs / "XFAIL-OWNER-LEDGER.txt"
        ledger.write_text(
            "Haydn lit XFAIL owner ledger\n"
            "TOTAL: 2\n\n"
            "PATH: llvm/test/MC/Haydn/owned-a.s\n"
            "  OWNER:   GE96-03 (branch scale)\n"
            "  STATUS:  unsupported / alignment-open\n\n"
            "PATH: llvm/test/MC/Haydn/owned-b.s\n"
            "  OWNER:   GE96-09 (ar_sel)\n"
            "  STATUS:  unsupported / alignment-open\n",
            encoding="utf-8",
        )

        rc, rep = check(root)
        assert rc == 0 and rep["ok"], rep

        # Unexplained live XFAIL.
        (mc / "orphan.s").write_text("# XFAIL: *\n", encoding="utf-8")
        rc, rep = check(root)
        assert rc == 1 and "llvm/test/MC/Haydn/orphan.s" in rep["missing_in_ledger"], rep

        (mc / "orphan.s").unlink()
        # Stale ledger PATH.
        ledger.write_text(
            "TOTAL: 3\n\n"
            "PATH: llvm/test/MC/Haydn/owned-a.s\n"
            "  OWNER:   GE96-03\n"
            "  STATUS:  unsupported / alignment-open\n\n"
            "PATH: llvm/test/MC/Haydn/owned-b.s\n"
            "  OWNER:   GE96-09\n"
            "  STATUS:  unsupported / alignment-open\n\n"
            "PATH: llvm/test/MC/Haydn/gone.s\n"
            "  OWNER:   GE96-03\n"
            "  STATUS:  unsupported / alignment-open\n",
            encoding="utf-8",
        )
        rc, rep = check(root)
        assert rc == 1 and "llvm/test/MC/Haydn/gone.s" in rep["stale_in_ledger"], rep

    print("check_xfail_ledger self-test OK")
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
        help="run synthetic ledger drift vectors",
    )
    ap.add_argument("--json", action="store_true", help="emit JSON report")
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    llvm_src = (args.llvm_src or monorepo_from_script()).resolve()
    if not (llvm_src / "llvm").is_dir():
        sys.stderr.write(f"check_xfail_ledger: not an llvm monorepo: {llvm_src}\n")
        return 2

    rc, report = check(llvm_src)
    if args.json:
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        live_n = len(report["live"])
        led_n = len(report["ledger_paths"])
        print(
            f"check_xfail_ledger: live={live_n} ledger_paths={led_n} "
            f"TOTAL={report['ledger_total']} ok={report['ok']}"
        )
        for p in report["missing_in_ledger"]:
            print(f"  MISSING_IN_LEDGER: {p}", file=sys.stderr)
        for p in report["stale_in_ledger"]:
            print(f"  STALE_IN_LEDGER: {p}", file=sys.stderr)
        for p in report["unowned"]:
            print(f"  UNOWNED: {p}", file=sys.stderr)
        for e in report["errors"]:
            print(f"  ERROR: {e}", file=sys.stderr)
        if report["ok"]:
            print("check_xfail_ledger: PASS (unexplained XFAIL=0)")
        else:
            print("check_xfail_ledger: FAIL", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
