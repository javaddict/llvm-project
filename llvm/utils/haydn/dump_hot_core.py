#!/usr/bin/env python3
"""Dump hot-core metrics from a Haydn .s (and optional SMS/PP debug log).

KPI (plan §0):
  - ii: bundles between .LLhwloop_startN and .LLhwloop_endN
  - ops: non-nop instructions in those bundles
  - fill: ops / ii  (max 3.0)
  - shell: ≤2 ops, or ≤4 ops with no MAC-like mnemonic

Usage:
  dump_hot_core.py foo.s
  dump_hot_core.py foo.s --sms-log foo.sms --pp-log foo.pp --json
  dump_hot_core.py foo.s --hottest   # single hottest non-shell core only
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

BUNDLE_RE = re.compile(r"^\s*\{\s*(.*?)\s*\}\s*$")
START_RE = re.compile(r"^\.LLhwloop_start(\d+):")
END_RE = re.compile(r"^\.LLhwloop_end(\d+):")
MAC_HINT = re.compile(
    r"(mac|mul|mula|f2mul|ff2mul|f2mula|x2add|x2sub)", re.I
)


def parse_bundle_ops(body: str) -> list[str]:
    ops = []
    for part in body.split(";"):
        p = part.strip()
        if not p or p == "nop":
            continue
        ops.append(p)
    return ops


def extract_hwloops(text: str) -> list[dict]:
    lines = text.splitlines()
    loops = []
    i = 0
    while i < len(lines):
        m = START_RE.match(lines[i].strip())
        if not m:
            i += 1
            continue
        idx = m.group(1)
        j = i + 1
        bundles = 0
        ops: list[str] = []
        body_preview: list[str] = []
        while j < len(lines):
            if END_RE.match(lines[j].strip()) and lines[j].strip().startswith(
                f".LLhwloop_end{idx}:"
            ):
                break
            raw = re.sub(r"//.*$", "", lines[j]).rstrip()
            bm = BUNDLE_RE.match(raw)
            if bm:
                bundles += 1
                bops = parse_bundle_ops(bm.group(1))
                ops.extend(bops)
                if len(body_preview) < 8:
                    body_preview.append(bm.group(1)[:120])
            j += 1
        nops = len(ops)
        fill = (nops / bundles) if bundles else 0.0
        has_mac = any(MAC_HINT.search(o) for o in ops)
        shell = (nops <= 2) or (nops <= 4 and not has_mac)
        loops.append(
            {
                "id": int(idx),
                "ii": bundles,
                "ops": nops,
                "fill": round(fill, 4),
                "shell": shell,
                "has_mac": has_mac,
                "body_preview": body_preview,
            }
        )
        i = j + 1
    return loops


def count_set_hwloop(text: str) -> int:
    return sum(1 for ln in text.splitlines() if "set_hwloop" in ln)


def parse_sms_log(path: Path | None) -> list[dict]:
    if not path or not path.exists():
        return []
    text = path.read_text(errors="replace")
    found = []
    # Pair nearby Res MII / Found lines loosely
    res_mii = None
    for ln in text.splitlines():
        m = re.search(r"Return Res MII:(\d+)", ln)
        if m:
            res_mii = int(m.group(1))
        m = re.search(r"Schedule Found\?\s*(\d+)\s*\(II=(\d+)\)", ln)
        if m:
            found.append(
                {
                    "found": int(m.group(1)) == 1,
                    "ii": int(m.group(2)),
                    "res_mii": res_mii,
                }
            )
    return found


def parse_pp_log(path: Path | None) -> list[dict]:
    if not path or not path.exists():
        return []
    text = path.read_text(errors="replace")
    out = []
    for ln in text.splitlines():
        m = re.search(
            r"HaydnPostPipeliner: Success II=(\d+) NStages=(\d+)", ln
        )
        if m:
            out.append({"success": True, "ii": int(m.group(1)), "nstages": int(m.group(2))})
        if "skip (fail closed)" in ln or "no schedule found" in ln:
            out.append({"success": False, "line": ln.strip()[:120]})
    return out


def hottest_non_shell(loops: list[dict]) -> dict | None:
    real = [L for L in loops if not L["shell"]]
    pool = real if real else loops
    if not pool:
        return None
    # Prefer MAC bodies, then max ops, then max ii
    return max(
        pool,
        key=lambda L: (L["has_mac"], L["ops"], L["ii"]),
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("s_file", type=Path, help="Haydn assembly .s")
    ap.add_argument("--sms-log", type=Path, default=None)
    ap.add_argument("--pp-log", type=Path, default=None)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--hottest", action="store_true")
    args = ap.parse_args()

    if not args.s_file.exists():
        print(f"missing {args.s_file}", file=sys.stderr)
        return 2

    text = args.s_file.read_text(errors="replace")
    loops = extract_hwloops(text)
    hot = hottest_non_shell(loops)
    report = {
        "file": str(args.s_file),
        "hwloop_count": count_set_hwloop(text),
        "loops": loops,
        "hottest": hot,
        "sms": parse_sms_log(args.sms_log),
        "pp": parse_pp_log(args.pp_log),
    }

    if args.json:
        print(json.dumps(report, indent=2))
        return 0

    print(f"file={args.s_file} set_hwloop={report['hwloop_count']} n_bodies={len(loops)}")
    for L in loops:
        tag = "SHELL" if L["shell"] else ("MAC" if L["has_mac"] else "body")
        print(
            f"  L{L['id']}: II={L['ii']} ops={L['ops']} fill={L['fill']:.3f} [{tag}]"
        )
        for line in L["body_preview"][:4]:
            print(f"    {{ {line} }}")
    if hot:
        print(
            f"hottest: L{hot['id']} II={hot['ii']} ops={hot['ops']} "
            f"fill={hot['fill']:.3f} shell={hot['shell']}"
        )
    if report["sms"]:
        for s in report["sms"]:
            print(f"  sms: Found={s['found']} II={s.get('ii')} ResMII={s.get('res_mii')}")
    if report["pp"]:
        for p in report["pp"][:6]:
            print(f"  pp: {p}")
    if args.hottest and hot is None:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
