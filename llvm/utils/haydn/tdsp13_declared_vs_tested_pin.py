#!/usr/bin/env python3
"""T-DSP13 declared-vs-tested inventory pin (library residual).

NatureDSP product_library_pin is the only library matrix. This pin
inventories the IntrinsicsHaydn vs lit set-difference as residual. It
does not author a 746-name harness or QUALIFY. Empty-Semantics leftover is closed.

Usage:
  tdsp13_declared_vs_tested_pin.py
  tdsp13_declared_vs_tested_pin.py --llvm-src PATH
  tdsp13_declared_vs_tested_pin.py --self-test
  tdsp13_declared_vs_tested_pin.py --json

Exit:
  0  residual classified; optional name census reported
  1  missing residual classification / QUALIFY invent
  2  usage
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

_RE_INT_HAYDN = re.compile(r"def\s+(int_haydn_[A-Za-z0-9_]+)\b")
INTRIN_FLOOR = 700
ANCHORS_REL = "llvm/test/CodeGen/Haydn/Inputs/SOURCE-AUTHORITY-ANCHORS.txt"
FAULT_REL = "llvm/test/CodeGen/Haydn/Inputs/FAULT-INJECTION-SEATS.txt"
CORRUPTION_REL = "llvm/test/CodeGen/Haydn/Inputs/CORRUPTION-MATRIX.txt"
RUNTIME_DIR_REL = "llvm/lib/Target/Haydn/haydn-rt"
INTRINSICS_REL = "llvm/include/llvm/IR/IntrinsicsHaydn.td"
TESTDIR_REL = "llvm/test/CodeGen/Haydn"
BUILTINS_REL = "clang/include/clang/Basic/BuiltinsHaydn.td"
CLANG_PIN_REL = "clang/test/CodeGen/Haydn/tdsp13-declared-vs-tested.py"

# PublicEnabled natives must publish Semantics. Empty leftover set is
# closed; this pin forbids a new hole.
EMPTY_SEMANTICS_LEFTOVER = frozenset()

# LS pre/post-inc + saturating ALU64 families that T-DSP13 must name in
# CodeGen/Haydn (compat/tdsp13-ls-satalu64-pin.ll). Not a 746-name harness.
LS_SATALU64_PIN = (
    "d_ldw_pre_reg",
    "d_lhw_post_imm",
    "d_lhw_post_reg",
    "d_lhw_pre_imm",
    "d_lw_post_reg",
    "d_lw_pre_imm",
    "d_lw_pre_reg",
    "s_lbs_post_reg",
    "s_lbu_post_imm",
    "s_lbu_post_reg",
    "s_lbu_pre_imm",
    "s_lhws_post_imm",
    "s_lhws_post_reg",
    "s_lhws_pre_imm",
    "s_lhws_pre_reg",
    "s_lhwu_post_imm",
    "s_lhwu_post_reg",
    "s_lhwu_pre_imm",
    "add64s_h",
    "add64s_l",
    "sub64s_h",
    "sub64s_l",
)

INVENTORY_FILES = {
    ANCHORS_REL: (
        "T-DSP13",
        "declared-vs-tested",
        "inventory only",
        "746-name harness",
    ),
    FAULT_REL: ("T-DSP13", "declared-vs-tested", "746-name harness"),
    CORRUPTION_REL: ("T-DSP13", "declared-vs-tested", "746 harness"),
}
RUNTIME_FILES = {
    "NATUREDSP-CANARY-PIN.txt": (
        "T-DSP13",
        "declared-vs-tested",
        "746-name harness",
        "product_library_pin",
    ),
    "PRODUCT-IDENTITY.txt": ("T-DSP13", "declared-vs-tested", "746-name harness"),
}
FORBIDDEN = ("tdsp13_qualified", "second library matrix")


def monorepo_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def _missing_snippets(text: str, snippets: Tuple[str, ...]) -> List[str]:
    folded = text.casefold()
    return [s for s in snippets if s.casefold() not in folded]


def _collect_blob(root: Path) -> str:
    parts: List[str] = []
    for path in root.rglob("*"):
        if path.suffix not in {".ll", ".mir", ".c", ".s", ".txt", ".td"}:
            continue
        try:
            parts.append(path.read_text(encoding="utf-8", errors="ignore"))
        except OSError:
            continue
    return "\n".join(parts)


def _mentioned(name: str, blob: str) -> bool:
    stem = name[len("int_haydn_") :]
    return (
        name in blob
        or ("llvm.haydn." + stem) in blob
        or ("llvm.haydn." + stem.replace("_", ".")) in blob
        or stem in blob
    )


def parse_public_empty_semantics(text: str) -> List[str]:
    pat = re.compile(r"def\s+([A-Za-z0-9_]+)\s*:\s*(HaydnBuiltin|HaydnPairBuiltin)\b")
    recs = [(m.start(), m.group(1)) for m in pat.finditer(text)]
    empty: List[str] = []
    for i, (pos, name) in enumerate(recs):
        end = recs[i + 1][0] if i + 1 < len(recs) else len(text)
        body = text[pos:end]
        if re.search(r"let\s+PublicEnabled\s*=\s*0\b", body):
            continue
        sem = re.search(r'let\s+Semantics\s*=\s*"([^"]*)"', body)
        if not sem or not sem.group(1).strip():
            empty.append(name)
    return empty


def census_declared(
    llvm_src: Path,
) -> Tuple[Optional[int], Optional[int], List[str], List[str]]:
    """Optional name census. Untested count is inventory, never QUALIFY."""
    itd = llvm_src / INTRINSICS_REL
    testdir = llvm_src / TESTDIR_REL
    if not itd.is_file():
        return None, None, [], []
    names = sorted(set(_RE_INT_HAYDN.findall(itd.read_text(encoding="utf-8", errors="replace"))))
    notes: List[str] = []
    if len(names) < INTRIN_FLOOR:
        notes.append(f"declared_floor:{len(names)}<{INTRIN_FLOOR}")
    untested = None
    missing_ls: List[str] = []
    if testdir.is_dir():
        blob = _collect_blob(testdir)
        untested = sum(1 for name in names if not _mentioned(name, blob))
        declared = set(names)
        for stem in LS_SATALU64_PIN:
            full = "int_haydn_" + stem
            if full in declared and not _mentioned(full, blob):
                missing_ls.append(stem)
    return len(names), untested, notes, missing_ls


def check(llvm_src: Path) -> Tuple[int, Dict[str, object]]:
    report: Dict[str, object] = {
        "ok": False,
        "qualified": False,
        "semantic_qualify": False,
        "second_matrix": False,
        "missing_snippets": [],
        "forbidden_hits": [],
        "declared": None,
        "untested": None,
        "empty_semantics": None,
        "ls_satalu64_missing": [],
        "clang_pin": False,
        "errors": [],
        "info": [],
    }
    missing: List[str] = []
    forbidden: List[str] = []
    for rel, snippets in INVENTORY_FILES.items():
        path = llvm_src / rel
        if not path.is_file():
            report["errors"].append(f"missing {rel}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for snippet in _missing_snippets(text, snippets):
            missing.append(f"{Path(rel).name}:{snippet}")
        folded = text.casefold()
        for token in FORBIDDEN:
            if token in folded:
                forbidden.append(f"{Path(rel).name}:{token}")
    runtime = llvm_src / RUNTIME_DIR_REL
    if not runtime.is_dir():
        if (llvm_src / "llvm" / "lib" / "Target" / "Haydn").is_dir():
            report["errors"].append("missing haydn-rt")
    else:
        for name, snippets in RUNTIME_FILES.items():
            path = runtime / name
            if not path.is_file():
                report["errors"].append(f"missing haydn-rt/{name}")
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for snippet in _missing_snippets(text, snippets):
                missing.append(f"{name}:{snippet}")
            folded = text.casefold()
            for token in FORBIDDEN:
                if token in folded:
                    forbidden.append(f"{name}:{token}")
    report["missing_snippets"] = missing
    report["forbidden_hits"] = forbidden
    if missing:
        report["errors"].append("T-DSP13 residual unclassified")
    if forbidden:
        report["errors"].append("T-DSP13 QUALIFY/second-matrix invent")

    clang_pin = llvm_src / CLANG_PIN_REL
    if clang_pin.is_file():
        report["clang_pin"] = True
        pin_text = clang_pin.read_text(encoding="utf-8", errors="replace")
        if "746-name harness" not in pin_text:
            report["errors"].append("clang T-DSP13 pin dropped no-harness law")
        report["info"].append("clang T-DSP13 pin present (inventory; no 746-name harness)")

    declared, untested, notes, missing_ls = census_declared(llvm_src)
    report["declared"] = declared
    report["untested"] = untested
    report["ls_satalu64_missing"] = missing_ls
    report["info"].extend(notes)
    if declared is not None:
        report["info"].append(
            f"T-DSP13 census declared={declared} untested={untested} (inventory; not QUALIFY)"
        )
    if missing_ls:
        report["errors"].append(
            "T-DSP13 LS/sat-ALU64 pin unnamed: " + ", ".join(missing_ls)
        )

    builtins = llvm_src / BUILTINS_REL
    if builtins.is_file():
        empty = parse_public_empty_semantics(
            builtins.read_text(encoding="utf-8", errors="replace")
        )
        empty_set = set(empty)
        report["empty_semantics"] = len(empty)
        extra = sorted(empty_set - EMPTY_SEMANTICS_LEFTOVER)
        missing_pin = sorted(EMPTY_SEMANTICS_LEFTOVER - empty_set)
        if extra:
            report["errors"].append(
                "T-DSP13 new PublicEnabled empty Semantics: " + ", ".join(extra)
            )
        if missing_pin:
            report["errors"].append(
                "T-DSP13 leftover Semantics pin stale (now filled): "
                + ", ".join(missing_pin)
            )
        report["info"].append(
            f"T-DSP13 empty_semantics_leftover={len(empty)} (inventory; not QUALIFY)"
        )

    ok = not report["errors"]
    report["ok"] = ok
    return (0 if ok else 1), report


def _self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        inputs = root / "llvm/test/CodeGen/Haydn/Inputs"
        runtime = root / RUNTIME_DIR_REL
        inputs.mkdir(parents=True)
        runtime.mkdir(parents=True)
        shared = (
            "T-DSP13 declared-vs-tested inventory only; no 746-name harness\n"
            "product_library_pin is the only library matrix\n"
        )
        (inputs / "SOURCE-AUTHORITY-ANCHORS.txt").write_text(shared, encoding="utf-8")
        (inputs / "FAULT-INJECTION-SEATS.txt").write_text(shared, encoding="utf-8")
        (inputs / "CORRUPTION-MATRIX.txt").write_text(
            "T-DSP13 declared-vs-tested; 746 harness forbidden\n", encoding="utf-8"
        )
        (runtime / "NATUREDSP-CANARY-PIN.txt").write_text(shared, encoding="utf-8")
        (runtime / "PRODUCT-IDENTITY.txt").write_text(shared, encoding="utf-8")
        rc, rep = check(root)
        assert rc == 0 and rep["ok"], rep

        (inputs / "SOURCE-AUTHORITY-ANCHORS.txt").write_text(
            "T-DSP13 residual only\n", encoding="utf-8"
        )
        rc, rep = check(root)
        assert rc == 1 and not rep["ok"], rep
        assert any("declared-vs-tested" in s for s in rep["missing_snippets"]), rep

        (inputs / "SOURCE-AUTHORITY-ANCHORS.txt").write_text(
            shared + "tdsp13_qualified true\n", encoding="utf-8"
        )
        rc, rep = check(root)
        assert rc == 1 and any("QUALIFY" in e for e in rep["errors"]), rep
    print("tdsp13_declared_vs_tested_pin self-test OK")
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--llvm-src", type=Path, default=None)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return _self_test()
    llvm_src = (args.llvm_src or monorepo_from_script()).resolve()
    if not (llvm_src / "llvm").is_dir():
        sys.stderr.write(f"tdsp13 pin: not an llvm monorepo: {llvm_src}\n")
        return 2
    rc, report = check(llvm_src)
    if args.json:
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        for line in report["info"]:
            print(f"  INFO: {line}")
        for snippet in report["missing_snippets"]:
            print(f"  MISSING: {snippet}", file=sys.stderr)
        for hit in report["forbidden_hits"]:
            print(f"  FORBIDDEN: {hit}", file=sys.stderr)
        for err in report["errors"]:
            print(f"  ERROR: {err}", file=sys.stderr)
        if report["ok"]:
            print("tdsp13_declared_vs_tested_pin: PASS (inventory residual)")
        else:
            print("tdsp13_declared_vs_tested_pin: FAIL", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
