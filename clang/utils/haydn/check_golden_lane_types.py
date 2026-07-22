#!/usr/bin/env python3
"""Assert BuiltinsHaydn.td / IntrinsicsHaydn.td match golden lane semantics.

Golden rule (instruction_type_index.json Behavior/Description):
  - X2* with parallel 32-bit lanes  → clang _ExtVector<2,int> / LLVM v2i32
  - X4* with parallel 16-bit lanes  → clang _ExtVector<4,short> / LLVM v4i16
  - Reduce (dot/hadd/energy/hmax…): vector srcs, scalar/i64 result
  - Pair frexp MAC: i64 accs + vector srcs → [i64,i64]
  - Cross-lane / selected-lane ops (FMUL*_HS*, packsr, FIR): may stay bag i64

Exit 0 if no hard mismatches among mnemonics that are clearly full-lane SIMD.
"""
from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(os.environ.get("LLVM_HEAD", Path(__file__).resolve().parents[3]))
GOLDEN = Path(
    os.environ.get(
        "HAYDN_GOLDEN_DB",
        "/ssd2/mhyang/haydn-plans/Database/golden/instruction_type_index.json",
    )
)
BUILTINS = ROOT / "clang/include/clang/Basic/BuiltinsHaydn.td"
INTRINS = ROOT / "llvm/include/llvm/IR/IntrinsicsHaydn.td"


def load_golden():
    with open(GOLDEN) as f:
        d = json.load(f)
    out = {}
    for items in d.values():
        if not isinstance(items, list):
            continue
        for it in items:
            out[it["Instruction"]] = it
    return out


def parse_builtins(text):
    blocks = re.split(r"\ndef ", text)
    res = {}
    for b in blocks:
        m = re.match(r'(\w+)\s*:\s*Haydn\w*Builtin<"([^"]+)">', b)
        if not m:
            continue
        name, proto = m.group(1), m.group(2)
        mn = re.search(r'let Mnemonic = "([^"]+)"', b)
        mn = mn.group(1) if mn else name.upper()
        res[mn] = {"name": name, "proto": proto}
    return res


def parse_intrinsics(text):
    res = {}
    for m in re.finditer(r"def (int_haydn_(\w+))\s*:\s*([^;]+);", text, re.S):
        res[m.group(2)] = re.sub(r"\s+", " ", m.group(3).strip())
    return res


def is_full_lane_x2(beh: str) -> bool:
    return bool(
        re.search(
            r"rtd\[63:32\].*rsd.*\[63:32\].*rtd\[31:00\].*rsd.*\[31:00\]|"
            r"two parallel 32-bit|"
            r"performs two parallel",
            beh,
            re.I | re.S,
        )
    )


def is_full_lane_x4(beh: str) -> bool:
    return bool(
        re.search(
            r"rtd\[63:48\].*rtd\[47:32\].*rtd\[31:16\].*rtd\[15:00\]|"
            r"four parallel|"
            r"quad 16-bit|"
            r"four packed 16-bit",
            beh,
            re.I | re.S,
        )
    )


def is_reduce_name(name: str) -> bool:
    return bool(re.search(r"hadd|hmax|hmin|dot|energy", name, re.I))


def main():
    golden = load_golden()
    builtins = parse_builtins(BUILTINS.read_text())
    intrins = parse_intrinsics(INTRINS.read_text())
    hard = []
    soft = []
    checked = 0
    for mn, it in sorted(golden.items()):
        if not re.match(r"X[24]", mn):
            continue
        beh = (it.get("Behavior") or "") + "\n" + (it.get("Description") or "")
        b = builtins.get(mn)
        if not b:
            soft.append(f"no builtin for golden {mn}")
            continue
        proto = b["proto"]
        name = b["name"]
        if name.endswith("_pair"):
            name = name[: -len("_pair")]
        checked += 1
        full_x2 = mn.startswith("X2") and is_full_lane_x2(beh)
        full_x4 = mn.startswith("X4") and is_full_lane_x4(beh)
        reduce = is_reduce_name(mn)
        has_v2 = "_ExtVector<2, int>" in proto
        has_v4 = "_ExtVector<4, short>" in proto
        # hard: clear full-lane same-width SIMD must be ExtVector
        if full_x2 and not reduce and not has_v2:
            hard.append(
                f"{mn}: full X2 lanes but Prototype lacks ExtVector2: {proto}"
            )
        if full_x4 and not reduce and not has_v4:
            hard.append(
                f"{mn}: full X4 lanes but Prototype lacks ExtVector4: {proto}"
            )
        if reduce and mn.startswith("X2"):
            if "hadd" in mn.lower() or "dot" in mn.lower() or "hmax" in mn.lower() or "hmin" in mn.lower():
                if not has_v2:
                    hard.append(
                        f"{mn}: reduce should take ExtVector2 src: {proto}"
                    )
        if reduce and mn.startswith("X4"):
            if any(k in mn.lower() for k in ("energy", "dot", "hadd", "hmax", "hmin")):
                if not has_v4:
                    hard.append(
                        f"{mn}: reduce should take ExtVector4 src: {proto}"
                    )
        # intrinsic check when present (resolve 2-dest helper class names)
        intr = intrins.get(name)
        if intr and full_x2 and not reduce:
            ok = (
                "llvm_v2i32" in intr
                or "v2i32" in intr
                or "simd_mac_2dest_intrinsic" in intr  # v2i32 sources
                or "simd_maca_2dest_intrinsic" in intr
            )
            if "v4i16" in intr:
                ok = False
            if not ok:
                hard.append(f"{mn}: intrinsic not v2i32: {intr[:100]}")
        if intr and full_x4 and not reduce:
            ok = (
                "llvm_v4i16" in intr
                or "v4i16" in intr
                or "simd_mac_2dest_v4i16" in intr
                or "simd_maca_2dest_v4i16" in intr
            )
            if not ok:
                hard.append(f"{mn}: intrinsic not v4i16: {intr[:100]}")

    print(f"checked {checked} X2/X4 golden mnemonics with builtins")
    print(f"hard mismatches: {len(hard)}")
    for h in hard[:50]:
        print("  HARD:", h)
    if len(hard) > 50:
        print(f"  ... +{len(hard) - 50} more")
    print(f"soft notes: {len(soft)} (missing builtins)")
    return 1 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
