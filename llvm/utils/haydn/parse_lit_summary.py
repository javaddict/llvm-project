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
import subprocess
import sys
from pathlib import Path
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
# Full-gate / ARTIFACT identity. Historical 28700d57 is not an ancestor.
_RE_GIT_COMMIT = re.compile(
    r"""(?m)["']?git_commit["']?\s*[:=]\s*["']?([0-9a-fA-F]{7,40})"""
)
STALE_FULL_GATE_COMMIT = "28700d57"
# new_gerrit rebound: TD reshape / 0 `_S*` defs (T7-ALIGN 38bd4059 is not
# an ancestor of this lineage).
REBIND_ANCESTOR = "4b6677f8bf87d12f20e4d0b0ff5ec10124dddf2f"
REBIND_SHORT = "4b6677f8bf87"


def parse_git_commit(text: str) -> Optional[str]:
    """Return the first git_commit hex from a gate manifest / ARTIFACT log."""
    m = _RE_GIT_COMMIT.search(text)
    return m.group(1) if m else None


def is_stale_full_gate_commit(commit: Optional[str]) -> bool:
    """True when the commit is the 08-17 manifest that is not an ancestor."""
    if not commit:
        return False
    return commit.lower().startswith(STALE_FULL_GATE_COMMIT)


def git_commit_is_repo_resident(repo: Path, commit: Optional[str]) -> bool:
    """True when commit exists and is an ancestor of HEAD (not 28700d57)."""
    if not commit or is_stale_full_gate_commit(commit):
        return False
    try:
        inside = subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "--is-inside-work-tree"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
        if inside != "true":
            return False
        subprocess.check_output(
            ["git", "-C", str(repo), "cat-file", "-t", commit],
            stderr=subprocess.DEVNULL,
        )
        subprocess.check_call(
            ["git", "-C", str(repo), "merge-base", "--is-ancestor", commit, "HEAD"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return False
    return True


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

    # Coverage-pin style log: only Passed, no Failed line.
    coverage = """
-- Testing: 4 tests, 1 workers --
Testing Time: 0.40s
Total Discovered Tests: 4
  Passed           :   4
"""
    f, x, xf, u, p = parse_lit_summary(coverage)
    assert f == 0 and x == 0 and p == 4, (f, x, xf, u, p)
    assert ok(f, x)

    # Full-gate manifest rebound: 28700d57 is not an ancestor.
    stale_manifest = """
{
  "status": "PASS",
  "git_commit": "28700d57366a35a7d04e8adfbdf782743ec847e0",
  "haydn_lit_ok": "n/a"
}
  Passed           :   3
"""
    f, x, xf, u, p = parse_lit_summary(stale_manifest)
    assert f == 0 and x == 0 and p == 3, (f, x, xf, u, p)
    stale = parse_git_commit(stale_manifest)
    assert stale and stale.startswith(STALE_FULL_GATE_COMMIT), stale
    assert is_stale_full_gate_commit(stale)
    rebound = parse_git_commit(f"llvm_src.git_commit={REBIND_ANCESTOR}")
    assert rebound and rebound.startswith(REBIND_SHORT), rebound
    assert not is_stale_full_gate_commit(rebound)
    live_repo = Path("/ssd/mhyang/llvm/llvm-head")
    if (live_repo / ".git").exists() or git_commit_is_repo_resident(
        live_repo, rebound
    ):
        assert git_commit_is_repo_resident(live_repo, rebound)
        assert not git_commit_is_repo_resident(live_repo, stale)

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
    ap.add_argument(
        "--refuse-stale-commit",
        action="store_true",
        help="exit 1 when the log/manifest git_commit is 28700d57 (not an ancestor)",
    )
    ap.add_argument(
        "--require-ancestor",
        action="store_true",
        help="exit 1 when git_commit is missing from --llvm-src or is not an ancestor of HEAD",
    )
    ap.add_argument(
        "--llvm-src",
        type=Path,
        default=None,
        help="monorepo used by --require-ancestor (default: /ssd/mhyang/llvm/llvm-head)",
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

    git_commit = parse_git_commit(text)
    stale = is_stale_full_gate_commit(git_commit)
    repo = args.llvm_src or Path("/ssd/mhyang/llvm/llvm-head")
    resident = (
        git_commit_is_repo_resident(repo, git_commit) if git_commit else False
    )
    lit_ok = ok(failed, xpass)
    if args.refuse_stale_commit and stale:
        lit_ok = False
    if args.require_ancestor and git_commit and not resident:
        lit_ok = False
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
                    "git_commit": git_commit,
                    "stale_full_gate_commit": stale,
                    "repo_resident": resident,
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
        if git_commit:
            parts.append(f"git_commit={git_commit}")
            parts.append(f"stale={str(stale).lower()}")
            parts.append(f"resident={str(resident).lower()}")
        parts.append(f"ok={'true' if lit_ok else 'false'}")
        print(" ".join(parts))

    if args.refuse_stale_commit and stale:
        print(
            "parse_lit_summary: refuse 28700d57 (not an ancestor); rebound to an in-repo commit",
            file=sys.stderr,
        )
    if args.require_ancestor and git_commit and not resident:
        print(
            f"parse_lit_summary: refuse {git_commit} (not repo-resident); "
            "rebind to an in-repo ancestor",
            file=sys.stderr,
        )
    return 0 if lit_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
