#!/usr/bin/env python3
"""Parse llvm-lit aggregate summary without XFAIL false-negatives.

Historical agent/gate harnesses matched the substring ``Failed`` inside
``Expectedly Failed: N``, treating XFAIL as a real failure and forcing
``haydn_lit_ok=false`` / ``all_pass=false`` on green runs.

Contract (product gate):
  * Count only real ``Failed: N`` and ``XPASS`` / ``Unexpectedly Passed: N``.
  * ``Expectedly Failed`` (XFAIL) and ``Unsupported`` are OK and never fail.
  * Exit 0 iff Failed==0 and XPASS==0; exit 1 otherwise (or on parse failure).

Usage:
  parse_lit_summary.py path/to/lit.log
  parse_lit_summary.py -   # stdin
  parse_lit_summary.py --self-test
"""

from __future__ import annotations

import argparse
import re
import sys
from typing import Optional, Tuple


# Real failure line: starts a summary field named exactly "Failed".
# Must not match "Expectedly Failed".
_RE_FAILED = re.compile(
    r"(?m)^[ \t]*Failed[ \t]*:[ \t]*(\d+)\b"
)
# XPASS appears as "Unexpectedly Passed" (lit default) or "XPASS".
_RE_XPASS = re.compile(
    r"(?m)^[ \t]*(?:Unexpectedly Passed|XPASS)[ \t]*:[ \t]*(\d+)\b"
)
# Optional context counts (never treated as failure).
_RE_XFAIL = re.compile(
    r"(?m)^[ \t]*Expectedly Failed[ \t]*:[ \t]*(\d+)\b"
)
_RE_UNSUPPORTED = re.compile(
    r"(?m)^[ \t]*Unsupported[ \t]*:[ \t]*(\d+)\b"
)
_RE_PASSED = re.compile(
    r"(?m)^[ \t]*Passed[ \t]*:[ \t]*(\d+)\b"
)


def parse_lit_summary(text: str) -> Tuple[int, int, Optional[int], Optional[int], Optional[int]]:
    """Return (failed, xpass, xfail|None, unsupported|None, passed|None)."""
    failed_m = _RE_FAILED.search(text)
    xpass_m = _RE_XPASS.search(text)
    xfail_m = _RE_XFAIL.search(text)
    unsup_m = _RE_UNSUPPORTED.search(text)
    passed_m = _RE_PASSED.search(text)

    # When lit is fully green with only XFAILs, there is often no "Failed:" line
    # at all. Treat missing Failed/XPASS as zero only when the summary block is
    # otherwise present (Passed or Expectedly Failed or Unsupported).
    has_summary = bool(passed_m or xfail_m or unsup_m or failed_m or xpass_m)
    if not has_summary:
        raise ValueError("no lit summary counters found")

    failed = int(failed_m.group(1)) if failed_m else 0
    xpass = int(xpass_m.group(1)) if xpass_m else 0
    xfail = int(xfail_m.group(1)) if xfail_m else None
    unsup = int(unsup_m.group(1)) if unsup_m else None
    passed = int(passed_m.group(1)) if passed_m else None
    return failed, xpass, xfail, unsup, passed


def ok(failed: int, xpass: int) -> bool:
    return failed == 0 and xpass == 0


def _self_test() -> int:
    green_xfail = """
-- Testing: 706 tests, 48 workers --
Testing Time: 3.34s

Total Discovered Tests: 706
  Unsupported      :   8 (1.13%)
  Passed           : 694 (98.30%)
  Expectedly Failed:   4 (0.57%)
"""
    f, x, xf, u, p = parse_lit_summary(green_xfail)
    assert f == 0 and x == 0 and xf == 4 and u == 8 and p == 694, (f, x, xf, u, p)
    assert ok(f, x)

    real_fail = """
Total Discovered Tests: 10
  Passed           :   7
  Failed           :   2 (20.00%)
  Expectedly Failed:   1 (10.00%)
  XPASS            :   0
"""
    f, x, xf, u, p = parse_lit_summary(real_fail)
    assert f == 2 and x == 0 and xf == 1 and p == 7, (f, x, xf, u, p)
    assert not ok(f, x)

    xpass_case = """
  Passed           :  10
  Unexpectedly Passed:   1
  Expectedly Failed:   0
"""
    f, x, xf, u, p = parse_lit_summary(xpass_case)
    assert f == 0 and x == 1 and p == 10, (f, x, xf, u, p)
    assert not ok(f, x)

    # Substring trap: naive "Failed:" match would see "Expectedly Failed: 4".
    trap = "Expectedly Failed:   4\n"
    f, x, xf, u, p = parse_lit_summary(trap)
    assert f == 0 and x == 0 and xf == 4, (f, x, xf, u, p)
    assert ok(f, x)

    # Trailing-noise trap: prose mentioning Failed must not invent a counter.
    trailing = """
Total Discovered Tests: 3
  Passed           :   3
note: earlier Failed attempt was fixed
"""
    f, x, xf, u, p = parse_lit_summary(trailing)
    assert f == 0 and x == 0 and p == 3, (f, x, xf, u, p)
    assert ok(f, x)

    print("parse_lit_summary self-test OK")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "path",
        nargs="?",
        help="lit log path, or '-' for stdin",
    )
    ap.add_argument(
        "--self-test",
        "--self-check",
        action="store_true",
        dest="self_test",
        help="run built-in false-negative regression vectors",
    )
    ap.add_argument(
        "--json",
        action="store_true",
        help="emit one JSON object on stdout",
    )
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    if not args.path:
        ap.error("path required unless --self-test")

    if args.path == "-":
        text = sys.stdin.read()
    else:
        with open(args.path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()

    try:
        failed, xpass, xfail, unsup, passed = parse_lit_summary(text)
    except ValueError as exc:
        print(f"parse_lit_summary: {exc}", file=sys.stderr)
        return 2

    lit_ok = ok(failed, xpass)
    if args.json:
        import json

        print(
            json.dumps(
                {
                    "failed": failed,
                    "xpass": xpass,
                    "xfail": xfail,
                    "unsupported": unsup,
                    "passed": passed,
                    "ok": lit_ok,
                },
                sort_keys=True,
            )
        )
    else:
        parts = [f"Failed={failed}", f"XPASS={xpass}"]
        if xfail is not None:
            parts.append(f"XFAIL={xfail}")
        if unsup is not None:
            parts.append(f"Unsupported={unsup}")
        if passed is not None:
            parts.append(f"Passed={passed}")
        parts.append(f"ok={'true' if lit_ok else 'false'}")
        print(" ".join(parts))

    return 0 if lit_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
