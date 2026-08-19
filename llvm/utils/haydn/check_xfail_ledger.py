#!/usr/bin/env python3
"""Verify live Haydn XFAIL directives match Inputs/XFAIL-OWNER-LEDGER.txt.

G-TEST-EVIDENCE law: every live XFAIL under CodeGen/Haydn + MC/Haydn must have
an owned ledger row (GE96 golden or live G-* goal). Unexplained XFAIL is a
product gate failure. Ledger rows that no longer have a live XFAIL are also
fail (stale inventory drift).

Usage:
  check_xfail_ledger.py                 # scan monorepo from this script
  check_xfail_ledger.py --llvm-src PATH
  check_xfail_ledger.py --inventory-pin # PIPE-20/DG0/M16 inventory only
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
ANCHORS_REL = "llvm/test/CodeGen/Haydn/Inputs/SOURCE-AUTHORITY-ANCHORS.txt"
FAULT_REL = "llvm/test/CodeGen/Haydn/Inputs/FAULT-INJECTION-SEATS.txt"
CORRUPTION_REL = "llvm/test/CodeGen/Haydn/Inputs/CORRUPTION-MATRIX.txt"
RUNTIME_DIR_REL = "llvm/lib/Target/Haydn/haydn-rt"
SCAN_SUFFIXES = {".ll", ".s", ".mir", ".test"}
# PIPE-20 / DG0 / M16 inventory-only pin (G-TEST-EVIDENCE). These are
# required phrases in SOURCE-AUTHORITY-ANCHORS.txt, not a product registry.
INVENTORY_REQUIRED = (
    "Phase-firewall inventory (PIPE-20",
    "ff42d9ad",
    "inventory only",
    "DecisionGuard product registry remains absent",
    "no G-DECISION-GUARD revive",
    "product_coverage_pin",
    "T-TI3..6 / M16 freeze",
    "do not invent a fuzz gate",
    "scale32/shift32",
    "parcel12",
    "__addsf3",
    "when PC is unchanged",
    "T8-DEBUG-EVIDENCE",
    "T8-EVID",
    "never qualified",
    "28700d57",
    "G_ANYEXT",
    "adjustsStack",
    "MaxParcels",
)
# FAULT / CORRUPTION carry the same AR0 leftover: PIPE-20 inventory only,
# no DecisionGuard product registry. They do not repeat every M16 phrase.
INVENTORY_SHARED_REQUIRED = (
    "PIPE-20",
    "inventory",
    "DecisionGuard product registry",
    "absent",
)
INVENTORY_FORBIDDEN = (
    "DecisionGuardRegistry",
)
INVENTORY_SHARED_FILES = (
    ANCHORS_REL,
    FAULT_REL,
    CORRUPTION_REL,
)
RUNTIME_FILES = (
    "SOFTFLOAT-CONTRACT.txt",
    "NATUREDSP-CANARY-PIN.txt",
    "PRODUCT-IDENTITY.txt",
)
RUNTIME_SNIPPETS = {
    "SOFTFLOAT-CONTRACT.txt": (
        "__addsf3",
        "__adddf3",
        "long double",
        "_Float16",
        "unavailable",
        "__SOFTFP__",
        "gcc-torture",
        "T-SF10",
        "yarpgen",
        ".bak",
        ".broken",
        "28700d57",
    ),
    "NATUREDSP-CANARY-PIN.txt": (
        "vec_add16x16_fast_hifi3",
        "vec_add32x32_fast_hifi3",
        "vec_scale32x32_fast_hifi3",
        "vec_shift32x32_fast_hifi3",
        "vec_dot16x16_fast_hifi3",
        "product_library_pin",
        "456",
        "T-DSP12",
        "T-DSP13",
    ),
    "PRODUCT-IDENTITY.txt": (
        "ARTIFACT.json",
        "haydn-rt/haydn.ld",
        "parcel12",
        "EM_HAYDN=259",
        "decode live bind",
        "do not invent a fuzz gate",
        "beyond_approved=456",
        "T-DSP12",
        "T-DSP13",
        "replacement e_machine",
        "same-PC",
        "install_product_ld",
        "ARTIFACT.product_ld",
        ".bak",
        ".broken",
        "user-printf.c",
        "memset-2.c",
        "builtin-bitops-1.c",
        "strlen-5.c",
        "va-arg-1.c",
        "va-arg-2.c",
        "CODE_IMAGE_REJECT",
        "direct control target is not an exact code record",
        "9e5c878a",
        "T-ABI4",
        "T-ABI6",
        "T-ABI11",
        "T-ABI12",
        "G-ECOSYSTEM-CONSUMERS",
        "G-LIBRARY-COVERAGE",
        "G-DEBUG-OBSERVABILITY",
        "G-TEST-EVIDENCE",
    ),
}


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


def check_runtime_pin(llvm_src: Path) -> Tuple[int, dict]:
    """Verify in-tree haydn-rt contract pins (M1/M10/M13/M15 leftover)."""
    runtime = llvm_src / RUNTIME_DIR_REL
    report: dict = {
        "ok": False,
        "runtime": str(runtime),
        "missing_files": [],
        "missing_snippets": [],
        "errors": [],
    }
    if not runtime.is_dir():
        report["errors"].append(f"missing haydn-rt dir {runtime}")
        report["missing_files"] = list(RUNTIME_FILES)
        return 1, report
    for name in RUNTIME_FILES:
        path = runtime / name
        if not path.is_file():
            report["missing_files"].append(name)
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        folded = text.casefold()
        for snippet in RUNTIME_SNIPPETS.get(name, ()):
            if snippet.casefold() not in folded:
                report["missing_snippets"].append(f"{name}:{snippet}")
    if report["missing_files"]:
        report["errors"].append("haydn-rt contract file missing")
    if report["missing_snippets"]:
        report["errors"].append("haydn-rt contract missing required phrases")
    ok = not report["missing_files"] and not report["missing_snippets"]
    report["ok"] = ok
    return (0 if ok else 1), report


def check_inventory_pin(llvm_src: Path) -> Tuple[int, dict]:
    """Verify AR0/PIPE-20/DG0 inventory-only pins. No DecisionGuard revive."""
    anchors = llvm_src / ANCHORS_REL
    report: dict = {
        "ok": False,
        "anchors": str(anchors),
        "missing_snippets": [],
        "forbidden_hits": [],
        "errors": [],
    }
    if not anchors.is_file():
        report["errors"].append(f"missing anchors {anchors}")
        return 1, report
    text = anchors.read_text(encoding="utf-8", errors="replace")
    folded = text.casefold()
    missing = [s for s in INVENTORY_REQUIRED if s.casefold() not in folded]
    forbidden = [s for s in INVENTORY_FORBIDDEN if s.casefold() in folded]
    report["missing_snippets"] = missing
    report["forbidden_hits"] = forbidden
    if missing:
        report["errors"].append("anchors missing PIPE-20/DG0/M16 inventory phrases")
    if forbidden:
        report["errors"].append("anchors contain forbidden DecisionGuard/test-contract revive")

    # AR0 leftover spans SOURCE-AUTHORITY + FAULT + CORRUPTION. Each file
    # must stay inventory-only; none may revive a DecisionGuard registry.
    for rel in INVENTORY_SHARED_FILES:
        path = llvm_src / rel
        if not path.is_file():
            report["errors"].append(f"missing inventory file {rel}")
            continue
        body = path.read_text(encoding="utf-8", errors="replace")
        body_folded = body.casefold()
        for snippet in INVENTORY_SHARED_REQUIRED:
            if snippet.casefold() not in body_folded:
                key = f"{Path(rel).name}:{snippet}"
                report["missing_snippets"].append(key)
                report["errors"].append(f"{Path(rel).name} missing AR0 inventory phrase")
        for snippet in INVENTORY_FORBIDDEN:
            if snippet.casefold() in body_folded:
                key = f"{Path(rel).name}:{snippet}"
                report["forbidden_hits"].append(key)
                report["errors"].append(
                    f"{Path(rel).name} contains forbidden DecisionGuard revive"
                )

    ok = not report["missing_snippets"] and not report["forbidden_hits"] and not report["errors"]
    report["ok"] = ok
    return (0 if ok else 1), report


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
        "inventory_ok": False,
        "runtime_ok": False,
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

    inv_rc, inv = check_inventory_pin(llvm_src)
    report["inventory_ok"] = bool(inv.get("ok"))
    report["inventory"] = {
        "missing_snippets": inv.get("missing_snippets", []),
        "forbidden_hits": inv.get("forbidden_hits", []),
    }
    for err in inv.get("errors", []):
        report["errors"].append(err)

    rt_rc, rt = check_runtime_pin(llvm_src)
    report["runtime_ok"] = bool(rt.get("ok"))
    report["runtime"] = {
        "missing_files": rt.get("missing_files", []),
        "missing_snippets": rt.get("missing_snippets", []),
    }
    for err in rt.get("errors", []):
        report["errors"].append(err)

    ok = (
        not missing
        and not stale
        and not unowned
        and not report["errors"]
        and total is not None
        and total == len(live_set)
        and total == len(ledger_paths)
        and report["inventory_ok"]
        and inv_rc == 0
        and report["runtime_ok"]
        and rt_rc == 0
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

        # Inventory pin rides the same check(): synthetic anchors must
        # carry PIPE-20 / DG0 / M16 leftover phrases and no revive tokens.
        shared = (
            "Phase-firewall inventory (PIPE-20; no issue-cycle/format identity crosses RA)\n"
            "Audited backend baseline: ff42d9ad\n"
            "this table is inventory only\n"
            "DecisionGuard product registry remains absent\n"
            "No G-DECISION-GUARD revive\n"
            "product_coverage_pin is the aggregate policy seat\n"
        )
        (inputs / "SOURCE-AUTHORITY-ANCHORS.txt").write_text(
            shared
            + "T-TI3..6 / M16 freeze\n"
            "do not invent a fuzz gate\n"
            "scale32/shift32 compile\n"
            "parcel12 preferred\n"
            "__addsf3 contract\n"
            "when PC is unchanged\n"
            "T8-DEBUG-EVIDENCE\n"
            "T8-EVID\n"
            "never qualified\n"
            "28700d57 is not an ancestor\n"
            "G_ANYEXT s96 legalizer\n"
            "adjustsStack is not enough\n"
            "MaxParcels idle pad\n"
            "G-ECOSYSTEM-CONSUMERS G-LIBRARY-COVERAGE "
            "G-DEBUG-OBSERVABILITY G-TEST-EVIDENCE\n",
            encoding="utf-8",
        )
        (inputs / "FAULT-INJECTION-SEATS.txt").write_text(shared, encoding="utf-8")
        (inputs / "CORRUPTION-MATRIX.txt").write_text(shared, encoding="utf-8")
        runtime = root / RUNTIME_DIR_REL
        runtime.mkdir(parents=True)
        (runtime / "SOFTFLOAT-CONTRACT.txt").write_text(
            "__addsf3 __adddf3 long double _Float16 unavailable __SOFTFP__ "
            "gcc-torture T-SF10 yarpgen .bak .broken 28700d57\n",
            encoding="utf-8",
        )
        (runtime / "NATUREDSP-CANARY-PIN.txt").write_text(
            "vec_add16x16_fast_hifi3 vec_add32x32_fast_hifi3 "
            "vec_scale32x32_fast_hifi3 vec_shift32x32_fast_hifi3 "
            "vec_dot16x16_fast_hifi3 product_library_pin 456 T-DSP12 T-DSP13\n",
            encoding="utf-8",
        )
        (runtime / "PRODUCT-IDENTITY.txt").write_text(
            "ARTIFACT.json haydn-rt/haydn.ld parcel12 EM_HAYDN=259 "
            "decode live bind do not invent a fuzz gate "
            "beyond_approved=456 T-DSP12 T-DSP13 "
            "replacement e_machine same-PC "
            "install_product_ld ARTIFACT.product_ld .bak .broken "
            "user-printf.c memset-2.c builtin-bitops-1.c strlen-5.c "
            "va-arg-1.c va-arg-2.c CODE_IMAGE_REJECT "
            "direct control target is not an exact code record 9e5c878a "
            "T-ABI4 T-ABI6 T-ABI11 T-ABI12 "
            "G-ECOSYSTEM-CONSUMERS G-LIBRARY-COVERAGE "
            "G-DEBUG-OBSERVABILITY G-TEST-EVIDENCE\n",
            encoding="utf-8",
        )

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

        # Inventory pin: DecisionGuard registry token is fail-closed.
        (inputs / "SOURCE-AUTHORITY-ANCHORS.txt").write_text(
            "Phase-firewall inventory (PIPE-20\n"
            "ff42d9ad\ninventory only\n"
            "DecisionGuard product registry remains absent\n"
            "no G-DECISION-GUARD revive\n"
            "product_coverage_pin\n"
            "T-TI3..6 / M16 freeze\n"
            "do not invent a fuzz gate\n"
            "scale32/shift32\n"
            "parcel12\n"
            "__addsf3\n"
            "when PC is unchanged\n"
            "T8-DEBUG-EVIDENCE\n"
            "T8-EVID\n"
            "never qualified\n"
            "28700d57\n"
            "G_ANYEXT\n"
            "adjustsStack\n"
            "MaxParcels\n"
            "DecisionGuardRegistry\n",
            encoding="utf-8",
        )
        (inputs / "FAULT-INJECTION-SEATS.txt").write_text(
            "PIPE-20 inventory DecisionGuard product registry remains absent\n",
            encoding="utf-8",
        )
        (inputs / "CORRUPTION-MATRIX.txt").write_text(
            "PIPE-20 inventory DecisionGuard product registry remains absent\n",
            encoding="utf-8",
        )
        ledger.write_text(
            "TOTAL: 2\n\n"
            "PATH: llvm/test/MC/Haydn/owned-a.s\n"
            "  OWNER:   GE96-03\n"
            "  STATUS:  unsupported / alignment-open\n\n"
            "PATH: llvm/test/MC/Haydn/owned-b.s\n"
            "  OWNER:   GE96-09\n"
            "  STATUS:  unsupported / alignment-open\n",
            encoding="utf-8",
        )
        rc, rep = check(root)
        assert rc == 1 and not rep["inventory_ok"], rep
        assert "DecisionGuardRegistry" in rep["inventory"]["forbidden_hits"], rep

        inv_rc, inv = check_inventory_pin(root)
        assert inv_rc == 1 and not inv["ok"], inv

        # Runtime pin: missing NatureDSP scale/shift canary is fail-closed.
        (runtime / "NATUREDSP-CANARY-PIN.txt").write_text(
            "vec_add16x16_fast_hifi3 vec_add32x32_fast_hifi3 "
            "vec_dot16x16_fast_hifi3 product_library_pin\n",
            encoding="utf-8",
        )
        rt_rc, rt = check_runtime_pin(root)
        assert rt_rc == 1 and not rt["ok"], rt
        assert any("vec_scale32" in s for s in rt["missing_snippets"]), rt

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
    ap.add_argument(
        "--inventory-pin",
        action="store_true",
        help="PIPE-20/DG0/M16 inventory-only pin (no DecisionGuard revive)",
    )
    ap.add_argument(
        "--runtime-pin",
        action="store_true",
        help="haydn-rt M1/M10/M13/M15 contract pin (no second library matrix)",
    )
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    llvm_src = (args.llvm_src or monorepo_from_script()).resolve()
    if not (llvm_src / "llvm").is_dir():
        sys.stderr.write(f"check_xfail_ledger: not an llvm monorepo: {llvm_src}\n")
        return 2

    if args.inventory_pin:
        rc, report = check_inventory_pin(llvm_src)
        if args.json:
            json.dump(report, sys.stdout, indent=2, sort_keys=True)
            sys.stdout.write("\n")
        else:
            print(
                f"check_xfail_ledger inventory-pin ok={report['ok']} "
                f"missing={report['missing_snippets']} "
                f"forbidden={report['forbidden_hits']}"
            )
            for e in report["errors"]:
                print(f"  ERROR: {e}", file=sys.stderr)
            if report["ok"]:
                print("check_xfail_ledger: PASS (PIPE-20/DG0 inventory-only)")
            else:
                print("check_xfail_ledger: FAIL inventory-pin", file=sys.stderr)
        return rc

    if args.runtime_pin:
        rc, report = check_runtime_pin(llvm_src)
        if args.json:
            json.dump(report, sys.stdout, indent=2, sort_keys=True)
            sys.stdout.write("\n")
        else:
            print(
                f"check_xfail_ledger runtime-pin ok={report['ok']} "
                f"missing_files={report['missing_files']} "
                f"missing={report['missing_snippets']}"
            )
            for e in report["errors"]:
                print(f"  ERROR: {e}", file=sys.stderr)
            if report["ok"]:
                print("check_xfail_ledger: PASS (haydn-rt contract)")
            else:
                print("check_xfail_ledger: FAIL runtime-pin", file=sys.stderr)
        return rc

    rc, report = check(llvm_src)
    if args.json:
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        live_n = len(report["live"])
        led_n = len(report["ledger_paths"])
        print(
            f"check_xfail_ledger: live={live_n} ledger_paths={led_n} "
            f"TOTAL={report['ledger_total']} inventory_ok={report.get('inventory_ok')} "
            f"runtime_ok={report.get('runtime_ok')} ok={report['ok']}"
        )
        inv = report.get("inventory") or {}
        for s in inv.get("missing_snippets", []):
            print(f"  INVENTORY_MISSING: {s}", file=sys.stderr)
        for s in inv.get("forbidden_hits", []):
            print(f"  INVENTORY_FORBIDDEN: {s}", file=sys.stderr)
        rt = report.get("runtime") or {}
        for s in rt.get("missing_files", []):
            print(f"  RUNTIME_MISSING_FILE: {s}", file=sys.stderr)
        for s in rt.get("missing_snippets", []):
            print(f"  RUNTIME_MISSING: {s}", file=sys.stderr)
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
