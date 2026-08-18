#!/usr/bin/env python3
"""Pin the in-tree Haydn product linker script (T-MC10 / M12).

G-RUNTIME-TOOLCHAIN: the live BundleSim/sysroot linker script is versioned in
the monorepo at haydn-rt/haydn.ld. This checker:

  * greps HAYDN-LD-IDENT (date + Format E 12-byte parcel)
  * requires product geometry tokens copied from the live script
  * hashes the ident-stripped body
  * when a sysroot/BSP copy is present, requires that body to match
    (ident comment is in-tree only until BundleSim consumes the same file)

Usage:
  check_product_ld.py
  check_product_ld.py --llvm-src PATH
  check_product_ld.py --sysroot PATH
  check_product_ld.py --require-sysroot
  check_product_ld.py --self-test

Exit:
  0  ident + geometry OK (and sysroot body match when required/present)
  1  pin failure
  2  usage / missing in-tree script
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path
from typing import List, Optional, Tuple

IDENT_RE = re.compile(
    r"HAYDN-LD-IDENT:\s+\d{4}-\d{2}-\d{2}\s+Format E 12-byte parcel"
)
IDENT_LINE_RE = re.compile(r"HAYDN-LD-IDENT:")
PRODUCT_REL = Path("haydn-rt/haydn.ld")
SYSROOT_LD_RELS = (Path("lib/bundlesim.ld"), Path("lib/haydn.ld"))

# Live product tokens — do not invent a memory map; these are copied from
# BundleSim bundlesim/bsp/bundlesim.ld / sysroot lib/bundlesim.ld.
REQUIRED_TOKENS = (
    "OUTPUT_ARCH(haydn)",
    "ENTRY(_start)",
    "PROVIDE(BUNDLESIM_TEXT_BASE = 0x00010000)",
    "PROVIDE(BUNDLESIM_INSTR_ALIGN = 2)",
    "PROVIDE(BUNDLESIM_RECORD_BYTES = 12)",
    "PROVIDE(BUNDLESIM_ABI_ALIGN = 16)",
)


def monorepo_from_script() -> Path:
    # llvm/utils/haydn/check_product_ld.py -> monorepo is parents[3]
    return Path(__file__).resolve().parents[3]


def canonical_body(text: str) -> str:
    """Ident-stripped body so in-tree versioning can differ by the ident line."""
    lines: List[str] = []
    for line in text.splitlines():
        if IDENT_LINE_RE.search(line):
            continue
        lines.append(line.rstrip())
    while lines and not lines[0]:
        lines.pop(0)
    while lines and not lines[-1]:
        lines.pop()
    return "\n".join(lines) + "\n"


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def discover_sysroot(explicit: Optional[Path], tools_dir: Optional[Path]) -> Optional[Path]:
    candidates: List[Path] = []
    if explicit:
        candidates.append(explicit)
    for env in ("HAYDN_SYSROOT", "BUNDLESIM_SYSROOT"):
        val = os.environ.get(env)
        if val:
            candidates.append(Path(val))
    for env in ("HAYDN_BIN", "BUNDLESIM_HAYDN_TOOLCHAIN_BIN"):
        val = os.environ.get(env)
        if val:
            candidates.append(Path(val).resolve().parent / "sysroot" / "haydn-unknown-elf")
    if tools_dir:
        candidates.append(Path(tools_dir).resolve().parent / "sysroot" / "haydn-unknown-elf")
    llc = shutil.which("llc")
    if llc:
        candidates.append(Path(llc).resolve().parent.parent / "sysroot" / "haydn-unknown-elf")
    default = Path("/ssd2/mhyang/haydn-build/sysroot/haydn-unknown-elf")
    if default.is_dir():
        candidates.append(default)
    seen = set()
    for cand in candidates:
        try:
            resolved = cand.resolve()
        except OSError:
            continue
        if resolved in seen:
            continue
        seen.add(resolved)
        if any((resolved / rel).is_file() for rel in SYSROOT_LD_RELS):
            return resolved
    return None


def check_in_tree(path: Path) -> Tuple[List[str], str]:
    errors: List[str] = []
    text = path.read_text(encoding="utf-8", errors="replace")
    if not IDENT_RE.search(text):
        errors.append("missing HAYDN-LD-IDENT date + Format E 12-byte parcel")
    for tok in REQUIRED_TOKENS:
        if tok not in text:
            errors.append(f"missing product token: {tok}")
    body = canonical_body(text)
    if "BUNDLESIM_RECORD_BYTES = 12" not in body:
        errors.append("canonical body lost Format E 12-byte parcel PROVIDE")
    return errors, sha256_text(body)


def check_sysroot_hygiene(sysroot: Path) -> List[str]:
    """Refuse stale .bak / .broken* debris next to the product ld."""
    found: List[str] = []
    lib = sysroot / "lib"
    roots = [lib] if lib.is_dir() else [sysroot]
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            name = path.name
            if name.endswith((".bak", ".broken", ".broken-t5")) or ".bak" in name or ".broken" in name:
                try:
                    found.append(str(path.relative_to(sysroot)))
                except ValueError:
                    found.append(str(path))
    if found:
        return ["sysroot hygiene debris: " + ",".join(sorted(found)[:8])]
    return []


def check_sysroot_match(in_tree: Path, sysroot_ld: Path) -> List[str]:
    errors: List[str] = []
    in_body = canonical_body(in_tree.read_text(encoding="utf-8", errors="replace"))
    sys_body = canonical_body(sysroot_ld.read_text(encoding="utf-8", errors="replace"))
    if in_body != sys_body:
        errors.append(
            "sysroot/install linker script body != in-tree product "
            f"(in-tree={in_tree} sysroot={sysroot_ld} "
            f"in_sha={sha256_text(in_body)} sys_sha={sha256_text(sys_body)})"
        )
    return errors


def check_artifact_product_ld(sysroot: Path) -> List[str]:
    """Fail-closed: live ARTIFACT.product_ld must be the in-tree bind."""
    stamp = sysroot / "ARTIFACT.json"
    if not stamp.is_file():
        return []
    try:
        record = json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"ARTIFACT.json unreadable: {exc}"]
    if not isinstance(record, dict):
        return ["ARTIFACT.json is not an object"]
    block = record.get("product_ld")
    if not isinstance(block, dict) or not block:
        return [
            "ARTIFACT.product_ld missing (null is fail-closed; "
            "bind with record_haydn_artifact_set.py --install-product-ld)"
        ]
    errors: List[str] = []
    if (block.get("authority") or "") != "haydn-rt/haydn.ld":
        errors.append("ARTIFACT.product_ld.authority")
    if block.get("complete") is not True or block.get("body_match") is not True:
        errors.append("ARTIFACT.product_ld incomplete or body mismatch")
    return errors


def _self_test() -> int:
    ident = "/* HAYDN-LD-IDENT: 2026-08-14 Format E 12-byte parcel */\n"
    body = (
        "OUTPUT_ARCH(haydn)\n"
        "ENTRY(_start)\n"
        "PROVIDE(BUNDLESIM_TEXT_BASE = 0x00010000);\n"
        "PROVIDE(BUNDLESIM_INSTR_ALIGN = 2);\n"
        "PROVIDE(BUNDLESIM_RECORD_BYTES = 12);\n"
        "PROVIDE(BUNDLESIM_ABI_ALIGN = 16);\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        product = tmp_path / "haydn-rt" / "haydn.ld"
        product.parent.mkdir()
        product.write_text(ident + body, encoding="utf-8")
        errs, digest = check_in_tree(product)
        assert not errs and len(digest) == 64, (errs, digest)

        sysroot = tmp_path / "sysroot" / "haydn-unknown-elf" / "lib"
        sysroot.mkdir(parents=True)
        sys_ld = sysroot / "bundlesim.ld"
        sys_ld.write_text(body, encoding="utf-8")
        (sysroot / "haydn.ld").write_text(body, encoding="utf-8")
        match_errs = check_sysroot_match(product, sys_ld)
        assert not match_errs, match_errs
        match_errs = check_sysroot_match(product, sysroot / "haydn.ld")
        assert not match_errs, match_errs

        sys_ld.write_text(body.replace("0x00010000", "0x00020000"), encoding="utf-8")
        match_errs = check_sysroot_match(product, sys_ld)
        assert match_errs, "expected sysroot mismatch"

        product.write_text(body, encoding="utf-8")
        errs, _ = check_in_tree(product)
        assert any("HAYDN-LD-IDENT" in e for e in errs), errs

        debris = sysroot / "stale.ld.bak"
        debris.write_text("x", encoding="utf-8")
        hy = check_sysroot_hygiene(sysroot.parent)
        assert hy and "hygiene" in hy[0], hy
        debris.unlink()
        assert not check_sysroot_hygiene(sysroot.parent)

        art = sysroot.parent / "ARTIFACT.json"
        art.write_text(json.dumps({"product_ld": None}), encoding="utf-8")
        art_errs = check_artifact_product_ld(sysroot.parent)
        assert art_errs and "missing" in art_errs[0], art_errs
        assert "install-product-ld" in art_errs[0], art_errs
        art.write_text(
            json.dumps(
                {
                    "product_ld": {
                        "authority": "haydn-rt/haydn.ld",
                        "complete": True,
                        "body_match": True,
                    }
                }
            ),
            encoding="utf-8",
        )
        assert not check_artifact_product_ld(sysroot.parent)

    print("check_product_ld self-test OK")
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--llvm-src",
        type=Path,
        default=None,
        help="monorepo root (default: derived from this script)",
    )
    ap.add_argument("--sysroot", type=Path, default=None, help="haydn-unknown-elf sysroot")
    ap.add_argument("--tools-dir", type=Path, default=None, help="toolchain bin dir")
    ap.add_argument(
        "--require-sysroot",
        action="store_true",
        help="fail if sysroot lib/bundlesim.ld or lib/haydn.ld is not found",
    )
    ap.add_argument(
        "--self-test",
        "--self-check",
        action="store_true",
        dest="self_test",
        help="run synthetic ident / sysroot-match vectors",
    )
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    llvm_src = (args.llvm_src or monorepo_from_script()).resolve()
    if not (llvm_src / "llvm").is_dir():
        sys.stderr.write(f"check_product_ld: not an llvm monorepo: {llvm_src}\n")
        return 2

    product = llvm_src / PRODUCT_REL
    if not product.is_file():
        sys.stderr.write(f"check_product_ld: missing in-tree product script: {product}\n")
        return 2

    errors, digest = check_in_tree(product)
    print(f"check_product_ld: in-tree={product} body_sha256={digest}")

    sysroot = discover_sysroot(args.sysroot, args.tools_dir)
    if sysroot is None:
        msg = "sysroot lib/bundlesim.ld or lib/haydn.ld not found"
        if args.require_sysroot:
            errors.append(msg)
        else:
            print(f"check_product_ld: INFO {msg}")
    else:
        found = False
        for rel in SYSROOT_LD_RELS:
            sys_ld = sysroot / rel
            if not sys_ld.is_file():
                errors.append(f"sysroot product ld missing: {rel}")
                continue
            found = True
            print(f"check_product_ld: sysroot_ld={sys_ld}")
            errors.extend(check_sysroot_match(product, sys_ld))
        if not found:
            errors.append("sysroot product ld missing (bundlesim.ld/haydn.ld)")
        errors.extend(check_sysroot_hygiene(sysroot))
        errors.extend(check_artifact_product_ld(sysroot))

    if errors:
        for e in errors:
            print(f"  FAIL: {e}", file=sys.stderr)
        print("check_product_ld: FAIL", file=sys.stderr)
        return 1

    print("check_product_ld: PASS (ident + product body pin)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
