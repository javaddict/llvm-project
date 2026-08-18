#!/usr/bin/env python3
"""Classify Haydn LLDB step-inst PC deltas and RSP/DWARF register namespaces.

Format E product step is one 12-byte parcel. Same-PC re-stop and non-12
deltas are residual classes, never a qualified step.

Register namespaces are distinct: BundleSim RSP process-plugin indices
(PC at 16, AR, then DR at 21) versus HaydnRegisterInfo.td DWARF numbers
(D0=16, AR0=32). Collapsing RSP into DWARF is residual, never preferred.

This is the monorepo twin of BundleSim scripts/lldb_feature_matrix.sh,
k_reg_meta in lldb_rsp.c, ABISysV_haydn register kinds, and
EmulateInstructionHaydn (parcel +12; no member decode).

Usage:
  classify_lldb_step.py FROM TO
  classify_lldb_step.py --self-test
  classify_lldb_step.py --json FROM TO
  classify_lldb_step.py --reg-map
  classify_lldb_step.py --return-reg BYTES
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import Dict, List, Optional, Tuple

PARCEL_BYTES = 12
PREFERRED = "parcel12"
SAME_PC = "same-pc-residual"
NON12 = "non12-residual"

RSP_PC = 16
RSP_AR0 = 17
RSP_DR0 = 21
DWARF_D0 = 16
DWARF_AR0 = 32
DWARF_SFR = 36
DWARF_CSR = 37

SPLIT_OK = "split-ok"
COLLAPSED = "collapsed-residual"

# (primary, alt, rsp or None, dwarf or None)
# Twin of BundleSim k_reg_meta + compiler DWARF. SFR/CSR have DWARF and no RSP.
RegRow = Tuple[str, Optional[str], Optional[int], Optional[int]]

REG_MAP: List[RegRow] = (
    [("r%d" % i, None, i, i) for i in range(13)]
    + [
        ("sp", "r13", 13, 13),
        ("fp", "r14", 14, 14),
        ("lr", "r15", 15, 15),
        ("pc", None, RSP_PC, None),
        ("ar0", None, RSP_AR0, DWARF_AR0),
        ("ar1", None, RSP_AR0 + 1, DWARF_AR0 + 1),
        ("ar2", None, RSP_AR0 + 2, None),
        ("ar3", None, RSP_AR0 + 3, None),
    ]
    + [("dr%d" % i, "d%d" % i, RSP_DR0 + i, DWARF_D0 + i) for i in range(16)]
    + [
        ("sfr", None, None, DWARF_SFR),
        ("csr", None, None, DWARF_CSR),
        ("hwlr0_begin", None, 37, None),
        ("hwlr0_end", None, 38, None),
        ("hwlr0_count", None, 39, None),
        ("hwlr1_begin", None, 40, None),
        ("hwlr1_end", None, 41, None),
        ("hwlr1_count", None, 42, None),
        ("cbr0_begin", None, 43, None),
        ("cbr0_end", None, 44, None),
        ("cbr1_begin", None, 45, None),
        ("cbr1_end", None, 46, None),
    ]
)


def classify_step_delta(from_pc: int, to_pc: int) -> str:
    """Return preferred / residual class for a step-inst PC pair."""
    delta = int(to_pc) - int(from_pc)
    if delta == 0:
        return SAME_PC
    if abs(delta) == PARCEL_BYTES:
        return PREFERRED
    return NON12


def classify_report(from_pc: int, to_pc: int) -> Dict[str, object]:
    klass = classify_step_delta(from_pc, to_pc)
    return {
        "from_pc": int(from_pc),
        "to_pc": int(to_pc),
        "delta": int(to_pc) - int(from_pc),
        "class": klass,
        "preferred_class": PREFERRED,
        "parcel_bytes": PARCEL_BYTES,
        "qualified": False,
        "semantic_qualify": False,
        "same_pc_class": "residual",
        "non12_delta_class": "residual",
    }


def lookup_reg(name: str) -> Optional[RegRow]:
    key = name.lower()
    for row in REG_MAP:
        if row[0] == key or row[1] == key:
            return row
    return None


def dwarf_for_rsp(rsp: int) -> Optional[int]:
    for _name, _alt, row_rsp, dwarf in REG_MAP:
        if row_rsp == rsp:
            return dwarf
    return None


def classify_reg_namespaces(name: str, rsp: Optional[int],
                            dwarf: Optional[int]) -> str:
    """Preferred when advertised dwarf matches the compiler table, not RSP."""
    row = lookup_reg(name)
    if row is None:
        return COLLAPSED
    _prim, _alt, want_rsp, want_dwarf = row
    if rsp is not None and want_rsp is not None and int(rsp) != want_rsp:
        return COLLAPSED
    if want_dwarf is None:
        if dwarf is None:
            return SPLIT_OK
        return COLLAPSED
    if dwarf is None:
        return COLLAPSED
    if int(dwarf) != want_dwarf:
        return COLLAPSED
    if rsp is not None and int(rsp) == int(dwarf) and want_rsp != want_dwarf:
        return COLLAPSED
    return SPLIT_OK


def return_reg_for_bytes(num_bytes: int) -> str:
    """RetCC_Haydn: <=4 in r1, 8 in d0, else sret / unsupported."""
    if num_bytes <= 0:
        return "unsupported"
    if num_bytes <= 4:
        return "r1"
    if num_bytes == 8:
        return "d0"
    return "sret"


def generic_num_for_name(name: str) -> Optional[str]:
    key = name.lower()
    mapping = {
        "pc": "pc",
        "lr": "ra",
        "r15": "ra",
        "sp": "sp",
        "r13": "sp",
        "fp": "fp",
        "r14": "fp",
        "r1": "arg1",
        "r2": "arg2",
        "r3": "arg3",
        "r4": "arg4",
        "r5": "arg5",
        "r6": "arg6",
        "r7": "arg7",
    }
    return mapping.get(key)


def _self_test() -> int:
    assert classify_step_delta(0x10000, 0x1000C) == PREFERRED
    assert classify_step_delta(0x1000C, 0x10000) == PREFERRED
    assert classify_step_delta(0x10000, 0x10000) == SAME_PC
    assert classify_step_delta(0x10000, 0x10002) == NON12
    assert classify_step_delta(0x10000, 0x10018) == NON12
    assert classify_step_delta(0x10000, 0x10001) == NON12
    rep = classify_report(0x10000, 0x10000)
    assert rep["qualified"] is False
    assert rep["semantic_qualify"] is False
    assert rep["class"] == SAME_PC
    pref = classify_report(0x10000, 0x1000C)
    assert pref["class"] == PREFERRED and pref["semantic_qualify"] is False

    assert dwarf_for_rsp(RSP_PC) is None
    assert dwarf_for_rsp(RSP_DR0) == DWARF_D0
    assert dwarf_for_rsp(RSP_AR0) == DWARF_AR0
    assert dwarf_for_rsp(0) == 0
    assert classify_reg_namespaces("dr0", RSP_DR0, DWARF_D0) == SPLIT_OK
    assert classify_reg_namespaces("d0", RSP_DR0, DWARF_D0) == SPLIT_OK
    assert classify_reg_namespaces("dr0", RSP_DR0, RSP_DR0) == COLLAPSED
    assert classify_reg_namespaces("pc", RSP_PC, None) == SPLIT_OK
    assert classify_reg_namespaces("pc", RSP_PC, RSP_PC) == COLLAPSED
    assert classify_reg_namespaces("pc", RSP_PC, DWARF_D0) == COLLAPSED
    assert classify_reg_namespaces("ar0", RSP_AR0, DWARF_AR0) == SPLIT_OK
    assert classify_reg_namespaces("ar2", RSP_AR0 + 2, None) == SPLIT_OK
    assert classify_reg_namespaces("ar2", RSP_AR0 + 2, 34) == COLLAPSED
    assert classify_reg_namespaces("sfr", None, DWARF_SFR) == SPLIT_OK
    assert classify_reg_namespaces("csr", None, DWARF_CSR) == SPLIT_OK
    assert return_reg_for_bytes(4) == "r1"
    assert return_reg_for_bytes(1) == "r1"
    assert return_reg_for_bytes(8) == "d0"
    assert return_reg_for_bytes(16) == "sret"
    assert generic_num_for_name("r1") == "arg1"
    assert generic_num_for_name("r7") == "arg7"
    assert generic_num_for_name("r2") == "arg2"
    assert generic_num_for_name("dr0") is None
    # RSP 16 is PC; DWARF 16 is D0. The split is the product contract.
    assert RSP_PC == DWARF_D0
    assert RSP_DR0 != DWARF_D0
    # Twin of ABISysV_haydn::kStepNeverQualified / ClassifyStepReport.
    assert pref["qualified"] is False
    assert pref["semantic_qualify"] is False
    assert classify_reg_namespaces("r1", 1, 1) == SPLIT_OK
    assert classify_reg_namespaces("lr", 15, 15) == SPLIT_OK
    print("classify_lldb_step self-test OK")
    return 0


def _parse_pc(text: str) -> int:
    return int(text, 0)


def _dump_reg_map() -> Dict[str, object]:
    rows = []
    for name, alt, rsp, dwarf in REG_MAP:
        rows.append(
            {
                "name": name,
                "alt": alt,
                "rsp": rsp,
                "dwarf": dwarf,
                "class": classify_reg_namespaces(name, rsp, dwarf),
            }
        )
    return {
        "parcel_bytes": PARCEL_BYTES,
        "rsp_pc": RSP_PC,
        "rsp_dr0": RSP_DR0,
        "dwarf_d0": DWARF_D0,
        "dwarf_ar0": DWARF_AR0,
        "preferred_reg_class": SPLIT_OK,
        "registers": rows,
    }


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("from_pc", nargs="?", help="start PC (hex or decimal)")
    ap.add_argument("to_pc", nargs="?", help="stop PC (hex or decimal)")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--reg-map", action="store_true",
                    help="dump RSP/DWARF register namespace contract")
    ap.add_argument(
        "--return-reg",
        metavar="BYTES",
        type=int,
        default=None,
        help="print RetCC register class for a return of BYTES",
    )
    args = ap.parse_args(argv)
    if args.self_test:
        return _self_test()
    if args.return_reg is not None:
        print(return_reg_for_bytes(args.return_reg))
        return 0
    if args.reg_map:
        json.dump(_dump_reg_map(), sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
        return 0
    if args.from_pc is None or args.to_pc is None:
        ap.print_usage(sys.stderr)
        return 2
    rep = classify_report(_parse_pc(args.from_pc), _parse_pc(args.to_pc))
    if args.json:
        json.dump(rep, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        print(f"{rep['class']} delta={rep['delta']} qualified=false")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
