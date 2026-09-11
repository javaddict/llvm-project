#!/usr/bin/env python3
"""D1.80 generated parity probe: G_INTRINSIC* dispatch vs td-Pat opcodes.

Peer: AIE2InstructionSelector.cpp select() — C++ ID switch first,
`default: return selectImpl`. Haydn generic 1:1 stays Pat-first.

Pins:
  1. select() skips selectImpl-first for G_INTRINSIC*
  2. G_INTRINSIC_W_SIDE_EFFECTS default is selectIntrinsic (C++ first)
  3. selectIntrinsic unhandled IDs default to selectImpl
  4. Live td-Pat result opcode == extractable C++ 1:1 opcode on overlap

Usage:
  d180-intrinsic-owner-parity.py --self-test
  d180-intrinsic-owner-parity.py --llvm-src PATH
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Set, Tuple

SELECTOR_REL = Path("llvm/lib/Target/Haydn/GISel/HaydnInstructionSelector.cpp")
TD_REL = Path("llvm/lib/Target/Haydn/HaydnIntrinsics.td")

RE_SELECT_FN = re.compile(
    r"bool\s+HaydnInstructionSelector::select\s*\(\s*MachineInstr\s*&\s*\w+\s*\)\s*\{"
)
RE_SELECT_INTR = re.compile(
    r"bool\s+HaydnInstructionSelector::selectIntrinsic\s*\(\s*MachineInstr\s*&\s*\w+\s*\)\s*\{"
)
RE_IS_INTRINSIC = re.compile(
    r"\bIsIntrinsic\b[\s\S]{0,400}?G_INTRINSIC\b[\s\S]{0,200}?G_INTRINSIC_W_SIDE_EFFECTS"
)
RE_SKIP_IMPL = re.compile(r"!IsIntrinsic\s*&&\s*selectImpl\s*\(")
RE_UNCOND_IMPL = re.compile(r"if\s*\(\s*selectImpl\s*\(")
RE_WS_DEFAULT_INTR = re.compile(
    r"G_INTRINSIC_W_SIDE_EFFECTS[\s\S]{0,800}?default\s*:\s*"
    r"(?://[^\n]*\n\s*)*return\s+selectIntrinsic\s*\("
)
RE_WS_DEFAULT_IMPL = re.compile(
    r"G_INTRINSIC_W_SIDE_EFFECTS[\s\S]{0,800}?default\s*:\s*"
    r"(?://[^\n]*\n\s*)*return\s+selectImpl\s*\("
)
RE_UNHANDLED_IMPL = re.compile(
    r'Unhandled G_INTRINSIC[\s\S]{0,300}?return\s+selectImpl\s*\('
)
RE_CPP_CASE = re.compile(
    r"\bcase\s+(?:Intrinsic::)?haydn_([A-Za-z0-9_]+)\s*:"
)
RE_PAT = re.compile(
    r"Pat<\s*\(int_haydn_([A-Za-z0-9_]+)\b[\s\S]*?,\s*\(([A-Za-z0-9_]+)"
)
RE_CPP_1TO1 = re.compile(
    r"case\s+haydn_([A-Za-z0-9_]+)\s*:\s*return\s+"
    r"select(?:Binary|Unary(?:R_GD)?|AccMAC|SimdMac2Dest|SimdMacAcc2Dest)\s*"
    r"\(\s*([A-Za-z0-9_]+)"
)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*?$", "", text, flags=re.M)
    return text


def brace_body(text: str, start: int) -> str:
    """Return the `{...}` body starting at start (index of '{')."""
    if start < 0 or start >= len(text) or text[start] != "{":
        raise ValueError("brace_body: start is not '{'")
    depth = 0
    i = start
    while i < len(text):
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
        i += 1
    raise ValueError("brace_body: unmatched '{'")


def function_body(text: str, sig: re.Pattern[str]) -> str:
    m = sig.search(text)
    if not m:
        raise ValueError(f"missing {sig.pattern}")
    brace = text.find("{", m.end() - 1)
    return brace_body(text, brace)


def extract_sets(td_text: str, cpp_text: str) -> Tuple[Set[str], Set[str], Dict[str, Set[str]], Dict[str, Set[str]]]:
    td_live = strip_comments(td_text)
    cpp_live = strip_comments(cpp_text)
    pats = set(re.findall(r"int_haydn_([A-Za-z0-9_]+)", td_live))
    cases = set(RE_CPP_CASE.findall(cpp_live))
    pat_op: Dict[str, Set[str]] = {}
    for name, opc in RE_PAT.findall(td_live):
        pat_op.setdefault(name, set()).add(opc)
    cpp_op: Dict[str, Set[str]] = {}
    for name, opc in RE_CPP_1TO1.findall(cpp_live):
        cpp_op.setdefault(name, set()).add(opc)
    return pats, cases, pat_op, cpp_op


def check_dispatch(cpp_text: str) -> List[str]:
    errors: List[str] = []
    try:
        select_fn = function_body(cpp_text, RE_SELECT_FN)
        select_intr = function_body(cpp_text, RE_SELECT_INTR)
    except ValueError as exc:
        return [str(exc)]

    if not RE_IS_INTRINSIC.search(select_fn):
        errors.append("select() missing IsIntrinsic from G_INTRINSIC / W_SIDE_EFFECTS")
    if not RE_SKIP_IMPL.search(select_fn):
        errors.append("select() missing !IsIntrinsic && selectImpl (AIE C++-first)")
    # First selectImpl in select() must be the skipped generic-1:1 call.
    first_impl = RE_UNCOND_IMPL.search(select_fn)
    skip_impl = RE_SKIP_IMPL.search(select_fn)
    if first_impl and (not skip_impl or first_impl.start() < skip_impl.start()):
        errors.append("select() still calls selectImpl unconditionally before C++ intrinsic switch")
    # Haydn C++ ownership lives in selectIntrinsic. W_SIDE_EFFECTS default
    # must be selectIntrinsic (C++ first); default selectImpl skips owned IDs
    # that are not listed in the outer switch (the D1.80 allowlist hole).
    if not RE_WS_DEFAULT_INTR.search(select_fn):
        errors.append(
            "G_INTRINSIC_W_SIDE_EFFECTS default is not selectIntrinsic (C++ first)"
        )
    if RE_WS_DEFAULT_IMPL.search(select_fn):
        errors.append(
            "G_INTRINSIC_W_SIDE_EFFECTS default still selectImpl-first (owned IDs skip C++)"
        )
    if not RE_UNHANDLED_IMPL.search(select_intr):
        errors.append("selectIntrinsic unhandled path is not selectImpl")
    return errors


def check_opcode_parity(
    overlap: Set[str],
    pat_op: Dict[str, Set[str]],
    cpp_op: Dict[str, Set[str]],
) -> List[str]:
    errors: List[str] = []
    for name in sorted(overlap):
        po = pat_op.get(name)
        co = cpp_op.get(name)
        if not po or not co:
            continue
        if po.isdisjoint(co):
            errors.append(f"opcode drift haydn_{name}: pat={sorted(po)} cpp={sorted(co)}")
    return errors


def check(llvm_src: Path) -> Tuple[int, dict]:
    report: dict = {
        "ok": False,
        "pats": 0,
        "cpp": 0,
        "overlap": 0,
        "pat_only": 0,
        "cpp_only": 0,
        "errors": [],
    }
    selector = llvm_src / SELECTOR_REL
    td = llvm_src / TD_REL
    if not selector.is_file():
        report["errors"].append(f"missing {selector}")
        return 1, report
    if not td.is_file():
        report["errors"].append(f"missing {td}")
        return 1, report
    cpp_text = selector.read_text(encoding="utf-8")
    td_text = td.read_text(encoding="utf-8")
    report["errors"].extend(check_dispatch(cpp_text))
    pats, cases, pat_op, cpp_op = extract_sets(td_text, cpp_text)
    overlap = pats & cases
    report["pats"] = len(pats)
    report["cpp"] = len(cases)
    report["overlap"] = len(overlap)
    report["pat_only"] = len(pats - cases)
    report["cpp_only"] = len(cases - pats)
    report["errors"].extend(check_opcode_parity(overlap, pat_op, cpp_op))
    report["ok"] = not report["errors"]
    return (0 if report["ok"] else 1), report


GOOD_SELECTOR = r"""
bool HaydnInstructionSelector::select(MachineInstr &I) {
  unsigned Opcode = I.getOpcode();
  const bool IsIntrinsic =
      Opcode == TargetOpcode::G_INTRINSIC ||
      Opcode == TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS;
  if (!IsIntrinsic && selectImpl(I, *CoverageInfo))
    return true;
  switch (Opcode) {
  case TargetOpcode::G_INTRINSIC:
    return selectIntrinsic(I);
  case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS: {
    switch (cast<GIntrinsic>(I).getIntrinsicID()) {
    default:
      return selectIntrinsic(I);
    case Intrinsic::vaend:
      I.eraseFromParent();
      return true;
    }
  }
  default:
    return false;
  }
}

bool HaydnInstructionSelector::selectIntrinsic(MachineInstr &I) {
  switch (cast<GIntrinsic>(I).getIntrinsicID()) {
  case haydn_add32s: return selectBinary(ADD32S, GPR32RegClass);
  case haydn_slli32: return false;
  }
  LLVM_DEBUG(dbgs() << "Unhandled G_INTRINSIC: " << IntrID << "\n");
  return selectImpl(I, *CoverageInfo);
}
"""

BAD_SELECTOR_WSE_IMPL = r"""
bool HaydnInstructionSelector::select(MachineInstr &I) {
  unsigned Opcode = I.getOpcode();
  const bool IsIntrinsic =
      Opcode == TargetOpcode::G_INTRINSIC ||
      Opcode == TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS;
  if (!IsIntrinsic && selectImpl(I, *CoverageInfo))
    return true;
  switch (Opcode) {
  case TargetOpcode::G_INTRINSIC:
    return selectIntrinsic(I);
  case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS: {
    switch (cast<GIntrinsic>(I).getIntrinsicID()) {
    default:
      return selectImpl(I, *CoverageInfo);
    }
  }
  default:
    return false;
  }
}

bool HaydnInstructionSelector::selectIntrinsic(MachineInstr &I) {
  switch (cast<GIntrinsic>(I).getIntrinsicID()) {
  case haydn_add32s: return selectBinary(ADD32S, GPR32RegClass);
  }
  LLVM_DEBUG(dbgs() << "Unhandled G_INTRINSIC: " << IntrID << "\n");
  return selectImpl(I, *CoverageInfo);
}
"""

BAD_SELECTOR_FIRST = r"""
bool HaydnInstructionSelector::select(MachineInstr &I) {
  if (selectImpl(I, *CoverageInfo))
    return true;
  unsigned Opcode = I.getOpcode();
  switch (Opcode) {
  case TargetOpcode::G_INTRINSIC:
    return selectIntrinsic(I);
  case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS: {
    switch (cast<GIntrinsic>(I).getIntrinsicID()) {
    default:
      return false;
    }
  }
  default:
    return false;
  }
}

bool HaydnInstructionSelector::selectIntrinsic(MachineInstr &I) {
  LLVM_DEBUG(dbgs() << "Unhandled G_INTRINSIC: " << IntrID << "\n");
  return false;
}
"""

GOOD_TD = r"""
def : Pat<(int_haydn_add32s GPR32:$a, GPR32:$b), (ADD32S GPR32:$a, GPR32:$b)>;
def : Pat<(int_haydn_brev32 GPR32:$a, GPR32:$b), (BREV32 GPR32:$a, GPR32:$b)>;
"""

DRIFT_TD = r"""
def : Pat<(int_haydn_add32s GPR32:$a, GPR32:$b), (ADD32 GPR32:$a, GPR32:$b)>;
"""


def _write_tree(root: Path, selector: str, td: str) -> None:
    sel = root / SELECTOR_REL
    td_path = root / TD_REL
    sel.parent.mkdir(parents=True, exist_ok=True)
    td_path.parent.mkdir(parents=True, exist_ok=True)
    sel.write_text(selector, encoding="utf-8")
    td_path.write_text(td, encoding="utf-8")


def _self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        _write_tree(root, GOOD_SELECTOR, GOOD_TD)
        rc, rep = check(root)
        assert rc == 0 and rep["ok"], rep
        assert rep["overlap"] == 1 and rep["pat_only"] == 1, rep

        _write_tree(root, BAD_SELECTOR_FIRST, GOOD_TD)
        rc, rep = check(root)
        assert rc == 1 and not rep["ok"], rep
        joined = " ".join(rep["errors"])
        assert "selectImpl unconditionally" in joined or "IsIntrinsic" in joined, rep

        _write_tree(root, BAD_SELECTOR_WSE_IMPL, GOOD_TD)
        rc, rep = check(root)
        assert rc == 1 and not rep["ok"], rep
        joined = " ".join(rep["errors"])
        assert "selectImpl-first" in joined or "selectIntrinsic" in joined, rep

        _write_tree(root, GOOD_SELECTOR, DRIFT_TD)
        rc, rep = check(root)
        assert rc == 1, rep
        assert any("opcode drift" in e for e in rep["errors"]), rep
    print("d180-intrinsic-owner-parity self-test OK")
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--llvm-src", type=Path, default=None)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return _self_test()
    if args.llvm_src is None:
        sys.stderr.write("d180-intrinsic-owner-parity: --llvm-src is required\n")
        return 2
    llvm_src = args.llvm_src.resolve()
    rc, report = check(llvm_src)
    sys.stdout.write(
        "D1.80 dispatch: C++-first\n"
        f"pats={report['pats']} cpp={report['cpp']} overlap={report['overlap']} "
        f"pat-only={report['pat_only']} cpp-only={report['cpp_only']}\n"
        f"opcode-parity: {'ok' if not any('opcode drift' in e for e in report['errors']) else 'FAIL'}\n"
    )
    for err in report["errors"]:
        sys.stderr.write(f"d180-intrinsic-owner-parity: {err}\n")
    if not report["ok"]:
        return rc
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
