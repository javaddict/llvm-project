#!/usr/bin/env python3
"""Mechanical Clang↔LLVM arity/name parity for advertised Haydn builtins.

Every HaydnBuiltin / HaydnPairBuiltin NAME must match int_haydn_<NAME>
argument count. Catches the fmulas32s/fmulsa32s 5-arg Clang vs ternary
LLVM class of defect. HaydnAeBuiltin rows are compatibility composites
and are skipped (explicit software composition, not 1:1 catalog).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path


def split_args(inner: str) -> list[str]:
    inner = inner.strip()
    if not inner or inner == "void":
        return []
    args: list[str] = []
    depth = 0
    buf: list[str] = []
    for ch in inner:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(buf).strip())
            buf = []
            continue
        buf.append(ch)
    if buf:
        args.append("".join(buf).strip())
    return [a for a in args if a]


def parse_builtin_arities(text: str) -> dict[str, int]:
    recs = list(
        re.finditer(
            r'def\s+([A-Za-z0-9_]+)\s*:\s*(HaydnBuiltin|HaydnPairBuiltin)\s*<\s*"([^"]*)"',
            text,
        )
    )
    out: dict[str, int] = {}
    for m in recs:
        name, _kind, proto = m.group(1), m.group(2), m.group(3)
        lp = proto.find("(")
        rp = proto.rfind(")")
        if lp < 0 or rp < lp:
            continue
        out[name] = len(split_args(proto[lp + 1 : rp]))
    return out


def llvm_param_count(body: str) -> int | None:
    body = re.sub(r"\s+", " ", body)
    if "haydn_unary_intrinsic" in body:
        return 1
    if "haydn_binary_intrinsic" in body:
        return 2
    if "haydn_ternary_intrinsic" in body:
        return 3
    m = re.search(r"Haydn_Intrinsic\s*<[^,]+,\s*\[(.*?)\],\s*\[(.*?)\]", body)
    if not m:
        return None
    params = m.group(2).strip()
    if not params:
        return 0
    return len(split_args(params))


def parse_intrinsic_arities(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for m in re.finditer(r"def\s+(int_haydn_[A-Za-z0-9_]+)\s*:\s*(.*?);", text, re.S):
        full, body = m.group(1), m.group(2)
        n = llvm_param_count(body)
        if n is None:
            continue
        out[full[len("int_haydn_") :]] = n
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(
            "usage: s2-clang-llvm-signature-parity.py "
            "<BuiltinsHaydn.td> <IntrinsicsHaydn.td>",
            file=sys.stderr,
        )
        return 2
    builtins = Path(argv[1]).read_text()
    intrinsics = Path(argv[2]).read_text()
    b = parse_builtin_arities(builtins)
    i = parse_intrinsic_arities(intrinsics)
    errs: list[str] = []
    compared = 0
    for name, nargs in sorted(b.items()):
        # Pair/composite Clang names (…_pair, CodeGen recipes) are not 1:1
        # LLVM identities. Compare only names that exist on both sides.
        if name not in i:
            continue
        compared += 1
        if i[name] != nargs:
            errs.append(
                f"{name}: Clang arity {nargs} vs LLVM arity {i[name]}"
            )
    for key in ("fmulas32s_hhll", "fmulas32s_hllh", "fmulsa32s_hhll", "fmulsa32s_hllh"):
        if key not in b or key not in i:
            errs.append(f"{key}: missing Clang or LLVM identity")
        elif b[key] != 3 or i[key] != 3:
            errs.append(
                f"{key}: expected ternary Clang and LLVM, got {b[key]}/{i[key]}"
            )
    if compared < 200:
        errs.append(f"compared only {compared} overlapping names; parser drift")
    if errs:
        print("S2 Clang↔LLVM signature parity failed:")
        for e in errs:
            print("  ", e)
        return 1
    print(f"ok: {compared} overlapping HaydnBuiltin names match LLVM arity")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
