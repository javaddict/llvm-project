#!/usr/bin/env python3
"""Write and validate the Haydn CI gate verdict JSON.

Air-gapped forks have no GitHub Actions. The nightly / per-commit runner
(`run_ci_gate.sh`) records one machine-readable verdict after wrapping the
existing BundleSim and llvm/utils/haydn helpers. This module owns the schema;
it does not reimplement lit parsing.

Lit aggregates are obtained by calling `parse_lit_summary.py` (Failed / XPASS
only). Expectedly Failed is never a real failure.

Usage:
  write_ci_verdict.py --out PATH [phase flags...] [--haydn-lit-log PATH]
  write_ci_verdict.py --fixture testdata/ci-verdict-lit-green.log --out PATH
  write_ci_verdict.py --dry-run PATH
  write_ci_verdict.py --self-test

Exit:
  0  JSON written / schema valid / --require-overall matched / self-test OK
  1  overall FAIL (real run) or --require-overall mismatch
  2  usage / parse / schema error
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Mapping, Optional


SCHEMA_NAME = "haydn-ci-verdict-v1"
OVERALL_PASS = "PASS"
OVERALL_FAIL = "FAIL"
STALE_FULL_GATE_COMMIT = "28700d57"
REBIND_ANCESTOR = "9e5c878a"
# CI overall is a phase-gate label. It is never semantic QUALIFY.
# CoreMark/Dhrystone TARGET_BUILD_FAILED (status -15) is a consumer-C
# residual, not a QUALIFY bit.

BOOL_FIELDS = (
    "build_ok",
    "haydn_lit_ok",
    "units_ok",
    "xfail_ledger_ok",
    "sysroot_ok",
    "bundlesim_regression_ok",
    "coremark_ok",
    "dhrystone_ok",
)
INT_FIELDS = ("haydn_lit_failed", "haydn_lit_xpass")
REQUIRED_FIELDS = (
    "schema",
    "timestamp",
    "head_sha",
    *BOOL_FIELDS,
    *INT_FIELDS,
    "overall",
    "log_paths",
)

_UTILS = Path(__file__).resolve().parent
_TESTDATA = _UTILS / "testdata"


def _load_parse_lit_summary():
    """Import sibling parse_lit_summary.py without mutating its CLI."""
    path = _UTILS / "parse_lit_summary.py"
    spec = importlib.util.spec_from_file_location("haydn_parse_lit_summary", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def parse_bool(text: str) -> bool:
    key = text.strip().lower()
    if key in ("1", "true", "yes", "on"):
        return True
    if key in ("0", "false", "no", "off"):
        return False
    raise argparse.ArgumentTypeError(f"not a boolean: {text!r}")


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def counts_from_lit_log(path: Path) -> tuple[int, int, bool]:
    """Return (failed, xpass, lit_ok) via parse_lit_summary. Do not reimplement."""
    pls = _load_parse_lit_summary()
    text = path.read_text(encoding="utf-8", errors="replace")
    failed, xpass, _xfail, _unsup, _passed = pls.parse_lit_summary(text)
    return int(failed), int(xpass), bool(pls.ok(failed, xpass))


def compute_overall(doc: Mapping[str, Any]) -> str:
    if any(not bool(doc[name]) for name in BOOL_FIELDS):
        return OVERALL_FAIL
    if int(doc["haydn_lit_failed"]) != 0 or int(doc["haydn_lit_xpass"]) != 0:
        return OVERALL_FAIL
    return OVERALL_PASS


def build_verdict(
    *,
    timestamp: str,
    head_sha: str,
    haydn_lit_failed: int,
    haydn_lit_xpass: int,
    haydn_lit_ok: bool,
    log_paths: Mapping[str, str],
    **ok_flags: bool,
) -> Dict[str, Any]:
    doc: Dict[str, Any] = {
        "schema": SCHEMA_NAME,
        "timestamp": timestamp,
        "head_sha": head_sha,
        "haydn_lit_failed": int(haydn_lit_failed),
        "haydn_lit_xpass": int(haydn_lit_xpass),
        "haydn_lit_ok": bool(haydn_lit_ok),
        "semantic_qualify": False,
        "rebind_ancestor": REBIND_ANCESTOR,
        "log_paths": dict(log_paths),
    }
    for name in BOOL_FIELDS:
        if name == "haydn_lit_ok":
            continue
        doc[name] = bool(ok_flags[name])
    doc["overall"] = compute_overall(doc)
    return doc


def validate_verdict(doc: Any) -> List[str]:
    """Return a list of schema / consistency errors (empty iff valid)."""
    errors: List[str] = []
    if not isinstance(doc, dict):
        return ["verdict is not a JSON object"]
    for name in REQUIRED_FIELDS:
        if name not in doc:
            errors.append(f"missing field {name}")
    if errors:
        return errors
    if doc.get("schema") != SCHEMA_NAME:
        errors.append(f"schema must be {SCHEMA_NAME!r}, got {doc.get('schema')!r}")
    if not isinstance(doc.get("timestamp"), str) or not doc["timestamp"]:
        errors.append("timestamp must be a non-empty string")
    if not isinstance(doc.get("head_sha"), str) or not doc["head_sha"]:
        errors.append("head_sha must be a non-empty string")
    elif str(doc.get("head_sha") or "").lower().startswith(STALE_FULL_GATE_COMMIT):
        errors.append(
            f"head_sha {STALE_FULL_GATE_COMMIT} is not an ancestor; "
            f"rebind to {REBIND_ANCESTOR}"
        )
    for name in BOOL_FIELDS:
        if not isinstance(doc.get(name), bool):
            errors.append(f"{name} must be a JSON boolean")
    for name in INT_FIELDS:
        val = doc.get(name)
        if type(val) is not int or isinstance(val, bool) or val < 0:
            errors.append(f"{name} must be a non-negative integer")
    overall = doc.get("overall")
    if overall not in (OVERALL_PASS, OVERALL_FAIL):
        errors.append(f"overall must be {OVERALL_PASS!r} or {OVERALL_FAIL!r}")
    if overall == "QUALIFY":
        errors.append("overall QUALIFY is forbidden (CI verdict is not semantic QUALIFY)")
    if "semantic_qualify" in doc and doc.get("semantic_qualify") is not False:
        errors.append("semantic_qualify must be false (compile/CI is not QUALIFY)")
    log_paths = doc.get("log_paths")
    if not isinstance(log_paths, dict):
        errors.append("log_paths must be an object")
    else:
        for key, val in log_paths.items():
            if not isinstance(key, str) or not isinstance(val, str):
                errors.append("log_paths entries must be string:string")
                break
    if errors:
        return errors
    lit_ok_expected = doc["haydn_lit_failed"] == 0 and doc["haydn_lit_xpass"] == 0
    if doc["haydn_lit_ok"] != lit_ok_expected:
        errors.append(
            "haydn_lit_ok must be true iff haydn_lit_failed==0 and haydn_lit_xpass==0"
        )
    expected = compute_overall(doc)
    if doc["overall"] != expected:
        errors.append(f"overall {doc['overall']!r} inconsistent with phases (want {expected})")
    return errors


def write_verdict(path: Path, doc: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def fixture_verdict(lit_log: Path, head_sha: str, timestamp: str) -> Dict[str, Any]:
    """Build a verdict from a fake lit log; other phases assumed PASS."""
    failed, xpass, lit_ok = counts_from_lit_log(lit_log)
    return build_verdict(
        timestamp=timestamp,
        head_sha=head_sha,
        haydn_lit_failed=failed,
        haydn_lit_xpass=xpass,
        haydn_lit_ok=lit_ok,
        log_paths={"haydn_lit": str(lit_log)},
        build_ok=True,
        units_ok=True,
        xfail_ledger_ok=True,
        sysroot_ok=True,
        bundlesim_regression_ok=True,
        coremark_ok=True,
        dhrystone_ok=True,
    )


def _self_test() -> int:
    green = _TESTDATA / "ci-verdict-lit-green.log"
    fail = _TESTDATA / "ci-verdict-lit-fail.log"
    if not green.is_file() or not fail.is_file():
        print(f"write_ci_verdict: missing testdata under {_TESTDATA}", file=sys.stderr)
        return 2

    gdoc = fixture_verdict(green, "deadbeef", "2026-08-13T00:00:00Z")
    gerr = validate_verdict(gdoc)
    assert not gerr, gerr
    assert gdoc["haydn_lit_failed"] == 0 and gdoc["haydn_lit_xpass"] == 0
    assert gdoc["haydn_lit_ok"] is True
    assert gdoc["overall"] == OVERALL_PASS
    assert gdoc["semantic_qualify"] is False
    # Expectedly Failed in the green log must not become haydn_lit_failed.
    assert gdoc["haydn_lit_failed"] == 0
    lie_q = dict(gdoc)
    lie_q["semantic_qualify"] = True
    assert validate_verdict(lie_q)
    lie_label = dict(gdoc)
    lie_label["overall"] = "QUALIFY"
    assert validate_verdict(lie_label)

    fdoc = fixture_verdict(fail, "deadbeef", "2026-08-13T00:00:00Z")
    ferr = validate_verdict(fdoc)
    assert not ferr, ferr
    assert fdoc["haydn_lit_failed"] == 1 and fdoc["haydn_lit_xpass"] == 0
    assert fdoc["haydn_lit_ok"] is False
    assert fdoc["overall"] == OVERALL_FAIL

    # Consistency: lying haydn_lit_ok is a schema error.
    lie = dict(gdoc)
    lie["haydn_lit_failed"] = 1
    assert validate_verdict(lie)

    stale = fixture_verdict(
        green, "28700d57366a35a7d04e8adfbdf782743ec847e0", "2026-08-13T00:00:00Z"
    )
    assert validate_verdict(stale)
    rebound = fixture_verdict(
        green, "9e5c878a03921a31f76b1c251954cd14d9fcfd72", "2026-08-13T00:00:00Z"
    )
    rerr = validate_verdict(rebound)
    assert not rerr, rerr
    assert rebound["rebind_ancestor"] == REBIND_ANCESTOR

    print("write_ci_verdict self-test OK")
    return 0


def _parse_log_paths(raw: Optional[str]) -> Dict[str, str]:
    if not raw:
        return {}
    data = json.loads(raw)
    if not isinstance(data, dict):
        raise ValueError("log_paths JSON must be an object")
    out: Dict[str, str] = {}
    for key, val in data.items():
        out[str(key)] = str(val)
    return out


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, help="write verdict JSON here")
    ap.add_argument("--fixture", type=Path, help="fake lit log; other phases assumed PASS")
    ap.add_argument(
        "--dry-run",
        type=Path,
        metavar="PATH",
        help="validate an existing verdict JSON (schema only; no product gate)",
    )
    ap.add_argument("--self-test", "--self-check", action="store_true", dest="self_test")
    ap.add_argument("--head-sha", default="unknown")
    ap.add_argument("--timestamp", default=None, help="UTC ISO-8601; default now")
    ap.add_argument("--haydn-lit-log", type=Path, help="real llvm-lit log to parse")
    ap.add_argument("--haydn-lit-failed", type=int, default=None)
    ap.add_argument("--haydn-lit-xpass", type=int, default=None)
    ap.add_argument("--log-paths-json", default=None, help="JSON object of named log paths")
    ap.add_argument(
        "--require-overall",
        choices=(OVERALL_PASS, OVERALL_FAIL),
        help="exit 1 if written overall does not match (fixture unit gate)",
    )
    for name in BOOL_FIELDS:
        if name == "haydn_lit_ok":
            continue
        ap.add_argument(f"--{name.replace('_', '-')}", type=parse_bool, default=None)
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    if args.dry_run is not None:
        try:
            doc = load_json(args.dry_run)
        except (OSError, json.JSONDecodeError) as exc:
            print(f"write_ci_verdict: cannot read {args.dry_run}: {exc}", file=sys.stderr)
            return 2
        errors = validate_verdict(doc)
        if errors:
            for err in errors:
                print(f"write_ci_verdict: {err}", file=sys.stderr)
            return 2
        print(f"write_ci_verdict: schema OK overall={doc['overall']}")
        return 0

    if args.fixture is not None:
        if not args.fixture.is_file():
            print(f"write_ci_verdict: missing fixture {args.fixture}", file=sys.stderr)
            return 2
        try:
            doc = fixture_verdict(
                args.fixture,
                args.head_sha,
                args.timestamp or utc_timestamp(),
            )
        except (ValueError, OSError) as exc:
            print(f"write_ci_verdict: fixture parse failed: {exc}", file=sys.stderr)
            return 2
        errors = validate_verdict(doc)
        if errors:
            for err in errors:
                print(f"write_ci_verdict: {err}", file=sys.stderr)
            return 2
        if args.out is not None:
            write_verdict(args.out, doc)
        print(json.dumps({"overall": doc["overall"], "haydn_lit_ok": doc["haydn_lit_ok"]},
                         sort_keys=True))
        if args.require_overall and doc["overall"] != args.require_overall:
            print(
                f"write_ci_verdict: overall {doc['overall']} != {args.require_overall}",
                file=sys.stderr,
            )
            return 1
        return 0

    # Real (or reconstructed) verdict from phase flags.
    failed = args.haydn_lit_failed
    xpass = args.haydn_lit_xpass
    if args.haydn_lit_log is not None:
        if not args.haydn_lit_log.is_file():
            print(f"write_ci_verdict: missing lit log {args.haydn_lit_log}", file=sys.stderr)
            return 2
        try:
            failed, xpass, lit_ok = counts_from_lit_log(args.haydn_lit_log)
        except (ValueError, OSError) as exc:
            print(f"write_ci_verdict: lit log parse failed: {exc}", file=sys.stderr)
            return 2
    else:
        failed = 0 if failed is None else failed
        xpass = 0 if xpass is None else xpass
        lit_ok = failed == 0 and xpass == 0

    # Fail-closed: omitted phase flags are false (not a silent green).
    ok_flags = {}
    for name in BOOL_FIELDS:
        if name == "haydn_lit_ok":
            continue
        raw = getattr(args, name)
        ok_flags[name] = False if raw is None else bool(raw)

    try:
        log_paths = _parse_log_paths(args.log_paths_json)
    except (ValueError, json.JSONDecodeError) as exc:
        print(f"write_ci_verdict: bad --log-paths-json: {exc}", file=sys.stderr)
        return 2
    if args.haydn_lit_log is not None:
        log_paths.setdefault("haydn_lit", str(args.haydn_lit_log))

    doc = build_verdict(
        timestamp=args.timestamp or utc_timestamp(),
        head_sha=args.head_sha,
        haydn_lit_failed=failed,
        haydn_lit_xpass=xpass,
        haydn_lit_ok=lit_ok,
        log_paths=log_paths,
        **ok_flags,
    )
    errors = validate_verdict(doc)
    if errors:
        for err in errors:
            print(f"write_ci_verdict: {err}", file=sys.stderr)
        return 2
    if args.out is None:
        print(json.dumps(doc, indent=2, sort_keys=True))
    else:
        write_verdict(args.out, doc)
        print(f"write_ci_verdict: wrote {args.out} overall={doc['overall']}")
    if args.require_overall and doc["overall"] != args.require_overall:
        return 1
    return 0 if doc["overall"] == OVERALL_PASS else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
