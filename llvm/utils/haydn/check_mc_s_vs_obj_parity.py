#!/usr/bin/env python3
"""Byte-exact Format E .s-vs-obj parity over the generated mnemonic catalog.

T-MC6 / M8: R3 (check_mc_mnemonic_coverage.py) pins text presence of every
product non-NOP logical. This layer assembles the generated catalog
(test/MC/Haydn/format-e-mnemonic-roundtrip.s) and requires:

  * llvm-mc -show-encoding bytes == llvm-mc -filetype=obj .text bytes
  * each parcel length == generated FormatEEncodedBytes (from BundleBits,
    never a scattered product-width literal)
  * no all-zero parcel (invalid Format E)
  * llvm-objdump -d prints each encodable catalog mnemonic under its label

A mutation canary XOR-flips one object byte and requires this check to
FAIL — the CB-95 class (assembler text / -show-encoding OK, object bytes
wrong or idle).

Usage:
  check_mc_s_vs_obj_parity.py --self-test
  check_mc_s_vs_obj_parity.py
  check_mc_s_vs_obj_parity.py --llvm-src PATH --haydn-bin PATH

Exit:
  0  encoding == obj, generated parcel size, disasm names, mutation canary
  1  mismatch / all-zero / pin / canary failure
  2  usage / missing paths / missing tools

Peer: check_mc_mnemonic_coverage.py (name presence); Hexagon v67_all.s.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

RECORDS_REL = "llvm/lib/Target/Haydn/HaydnGenFormatERecords.inc"
CATALOG_REL = "llvm/test/MC/Haydn/format-e-mnemonic-roundtrip.s"
TRIPLE = "haydn-unknown-elf"

_RE_BUNDLE_BITS = re.compile(
    r"static constexpr unsigned FormatEBundleBits = (\d+)u;"
)
_RE_ENCODED_BYTES_EXPR = re.compile(
    r"static constexpr unsigned FormatEEncodedBytes\s*=\s*"
    r"\(FormatEBundleBits \+ 7u\) / 8u;"
)
_RE_ENCODED_BYTES_NUM = re.compile(
    r"static constexpr unsigned FormatEEncodedBytes = (\d+)u;"
)
_RE_NON_NOP = re.compile(
    r"static constexpr unsigned FormatENonNopLogicalCount = (\d+)u;"
)
_RE_ENCODING = re.compile(r"encoding:\s*\[([^\]]+)\]")
_RE_MNEM = re.compile(r"^# MNEM:\s+(\S+)\s*$")
_RE_PACKET = re.compile(r"^[^#\n]*\{")
_RE_DIS_TOK = re.compile(
    r"^# DIS: \{\{\[ \\t\]\}\}(.+?)\{\{\[ \\t,;\}\]\}\}$"
)


def monorepo_from_script() -> Path:
    # llvm/utils/haydn/check_mc_s_vs_obj_parity.py -> parents[3]
    return Path(__file__).resolve().parents[3]


def mnemonic_token_re(mnem: str) -> re.Pattern[str]:
    return re.compile(
        r"(?<![A-Za-z0-9_])" + re.escape(mnem) + r"(?![A-Za-z0-9_])"
    )


def parse_encoded_bytes(inc_text: str) -> int:
    """Query generated EncodedBytes from Format E records.

    Evaluates ``(FormatEBundleBits + 7) / 8`` when the generated file uses
    that expression; a numeric EncodedBytes pin must agree with the bits.
    """
    bm = _RE_BUNDLE_BITS.search(inc_text)
    if not bm:
        raise ValueError("missing FormatEBundleBits")
    bits = int(bm.group(1))
    derived = (bits + 7) // 8
    if derived <= 0:
        raise ValueError(f"FormatEBundleBits={bits} yields EncodedBytes={derived}")
    if _RE_ENCODED_BYTES_EXPR.search(inc_text):
        return derived
    nm = _RE_ENCODED_BYTES_NUM.search(inc_text)
    if nm:
        numbered = int(nm.group(1))
        if numbered != derived:
            raise ValueError(
                f"FormatEEncodedBytes={numbered} != (FormatEBundleBits+7)/8={derived}"
            )
        return numbered
    raise ValueError("missing FormatEEncodedBytes")


def parse_non_nop_count(inc_text: str) -> int:
    m = _RE_NON_NOP.search(inc_text)
    if not m:
        raise ValueError("missing FormatENonNopLogicalCount")
    return int(m.group(1))


def parse_encoding_blobs(mc_stdout: str) -> List[bytes]:
    blobs: List[bytes] = []
    for match in _RE_ENCODING.finditer(mc_stdout):
        parts = [p.strip() for p in match.group(1).split(",") if p.strip()]
        blobs.append(bytes(int(p, 16) for p in parts))
    return blobs


def catalog_mnemonics(catalog_text: str) -> Tuple[List[str], List[str]]:
    """Return (encodable, unencodable) print names from # MNEM: / # DIS:."""
    encodable: List[str] = []
    unencodable: List[str] = []
    lines = catalog_text.splitlines()
    idx = 0
    while idx < len(lines):
        mm = _RE_MNEM.match(lines[idx])
        if not mm:
            idx += 1
            continue
        name = mm.group(1)
        print_name = name
        j = idx + 1
        seen_unenc = False
        seen_packet = False
        while j < len(lines) and not _RE_MNEM.match(lines[j]):
            if lines[j].startswith("# UNENCODABLE:"):
                seen_unenc = True
                break
            if _RE_PACKET.match(lines[j]):
                seen_packet = True
            dm = _RE_DIS_TOK.match(lines[j])
            if dm:
                print_name = dm.group(1)
            j += 1
        if seen_unenc:
            unencodable.append(name)
        elif seen_packet:
            encodable.append(print_name)
        else:
            unencodable.append(name)
        idx = j
    return encodable, unencodable


def mutate_encoder_bytes(obj: bytes) -> bytes:
    """Deliberate encoder break: XOR the first non-zero byte (else byte 0)."""
    if not obj:
        return b"\xff"
    out = bytearray(obj)
    for i, val in enumerate(out):
        if val:
            out[i] = val ^ 0xFF
            return bytes(out)
    out[0] = 0xFF
    return bytes(out)


def first_mismatch(left: bytes, right: bytes) -> Optional[Tuple[int, int, int]]:
    n = min(len(left), len(right))
    for i in range(n):
        if left[i] != right[i]:
            return i, left[i], right[i]
    if len(left) != len(right):
        return n, (left[n] if len(left) > n else -1), (
            right[n] if len(right) > n else -1
        )
    return None


def disasm_has_mnemonic(dis_text: str, mnem: str) -> bool:
    """Require the mnemonic inside the ``<rt_mnem>:`` objdump block when present."""
    label_id = re.sub(r"[^A-Za-z0-9_]", "_", mnem)
    label = re.search(r"<rt_" + re.escape(label_id) + r">:", dis_text)
    token = mnemonic_token_re(mnem)
    if label:
        rest = dis_text[label.end() :]
        nxt = re.search(r"<rt_[^>]+>:", rest)
        block = rest[: nxt.start()] if nxt else rest
        return token.search(block) is not None
    return token.search(dis_text) is not None


def parity_errors(
    enc: bytes,
    obj: bytes,
    encoded_bytes: int,
    enc_blobs: Sequence[bytes],
    dis_text: str,
    mnems: Sequence[str],
) -> List[str]:
    errors: List[str] = []
    if encoded_bytes <= 0:
        errors.append(f"EncodedBytes must be positive, got {encoded_bytes}")
        return errors
    for i, blob in enumerate(enc_blobs):
        if len(blob) != encoded_bytes:
            errors.append(
                f"encoding blob {i} len {len(blob)} != EncodedBytes={encoded_bytes}"
            )
        if blob and not any(blob):
            errors.append(f"encoding blob {i} is all-zero (invalid Format E)")
    if len(enc) != len(obj):
        errors.append(f"encoding len {len(enc)} != obj .text len {len(obj)}")
    if len(obj) % encoded_bytes != 0:
        errors.append(
            f"obj .text len {len(obj)} is not a multiple of EncodedBytes={encoded_bytes}"
        )
    if not obj:
        errors.append("empty .text")
    n_parcels = len(obj) // encoded_bytes if encoded_bytes and obj else 0
    for i in range(n_parcels):
        parcel = obj[i * encoded_bytes : (i + 1) * encoded_bytes]
        if not any(parcel):
            errors.append(f"obj parcel {i} is all-zero (invalid Format E)")
    if enc != obj:
        mm = first_mismatch(enc, obj)
        if mm:
            off, a, b = mm
            errors.append(
                f"encoding vs obj mismatch at byte {off} ({a:#04x} vs {b:#04x})"
            )
        else:
            errors.append("encoding vs obj mismatch")
    if mnems and n_parcels and n_parcels != len(mnems):
        errors.append(
            f"obj parcels {n_parcels} != encodable catalog mnemonics {len(mnems)}"
        )
    if mnems and enc_blobs and len(enc_blobs) != len(mnems):
        errors.append(
            f"encoding blobs {len(enc_blobs)} != encodable catalog mnemonics {len(mnems)}"
        )
    for mnem in mnems:
        if not disasm_has_mnemonic(dis_text, mnem):
            errors.append(f"DIS missing {mnem}")
    return errors


def find_tool(name: str, haydn_bin: Optional[Path]) -> Optional[Path]:
    candidates: List[Path] = []
    if haydn_bin:
        candidates.append(Path(haydn_bin) / name)
    env_bin = os.environ.get("HAYDN_BIN")
    if env_bin:
        candidates.append(Path(env_bin) / name)
    which = shutil.which(name)
    if which:
        candidates.append(Path(which))
    seen = set()
    for cand in candidates:
        try:
            resolved = cand.resolve()
        except OSError:
            continue
        if resolved in seen:
            continue
        seen.add(resolved)
        if resolved.is_file() and os.access(resolved, os.X_OK):
            return resolved
    return None


def run_cmd(argv: Sequence[str], timeout: int = 120) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(argv),
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def extract_text_bytes(objcopy: Path, obj_path: Path, out_bin: Path) -> bytes:
    proc = run_cmd(
        [str(objcopy), "-O", "binary", "-j", ".text", str(obj_path), str(out_bin)]
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"llvm-objcopy failed ({proc.returncode}): {proc.stderr.strip()}"
        )
    return out_bin.read_bytes()


def _self_test() -> int:
    """Synthetic vectors; parcel width is derived (not a product-width literal)."""
    inc = (
        "static constexpr unsigned FormatEBundleBits = 80u;\n"
        "static constexpr unsigned FormatEEncodedBytes ="
        " (FormatEBundleBits + 7u) / 8u;\n"
        "static constexpr unsigned FormatENonNopLogicalCount = 2u;\n"
    )
    eb = parse_encoded_bytes(inc)
    assert eb == (80 + 7) // 8, eb
    assert parse_non_nop_count(inc) == 2

    parcel_a = bytes(range(1, eb + 1))
    parcel_b = bytes(range(2, eb + 2))
    good = parcel_a + parcel_b
    blobs = [parcel_a, parcel_b]
    dis = (
        "00000000 <rt_add32>:\n"
        "       { nop; add32 r1, r2, r3 }\n"
        "0000000a <rt_sub32>:\n"
        "       { nop; sub32 r1, r2, r3 }\n"
    )
    mnems = ["add32", "sub32"]
    errs = parity_errors(good, good, eb, blobs, dis, mnems)
    assert not errs, errs

    zeros = bytes(eb * 2)
    errs = parity_errors(good, zeros, eb, blobs, dis, mnems)
    assert any("all-zero" in e or "mismatch" in e for e in errs), errs

    broken = mutate_encoder_bytes(good)
    assert broken != good
    errs = parity_errors(good, broken, eb, blobs, dis, mnems)
    assert any("mismatch" in e for e in errs), errs

    short = good[:-1]
    errs = parity_errors(good, short, eb, blobs, dis, mnems)
    assert any("not a multiple" in e or "len" in e for e in errs), errs

    errs = parity_errors(good, good, eb, blobs, dis.replace("add32", "or32"), mnems)
    assert any("DIS missing add32" in e for e in errs), errs

    cat = (
        "# MNEM: add32\n"
        "rt_add32:\n"
        "{ add32 r1, r2, r3; nop; nop }\n"
        "# MNEM: wfi\n"
        "# UNENCODABLE: WFI<TBD> (wfi)\n"
        "# MNEM: sub32\n"
        "rt_sub32:\n"
        "{ sub32 r1, r2, r3; nop; nop }\n"
    )
    enc_n, unenc = catalog_mnemonics(cat)
    assert enc_n == ["add32", "sub32"] and unenc == ["wfi"], (enc_n, unenc)

    enc_line = (
        "\t{ add32 r1, r2, r3; nop; nop } "
        "// encoding: [" + ",".join(f"{b:#04x}" for b in parcel_a) + "]\n"
    )
    parsed = parse_encoding_blobs(enc_line)
    assert parsed == [parcel_a], parsed

    print("check_mc_s_vs_obj_parity self-test OK")
    return 0


def check(
    llvm_src: Path,
    haydn_bin: Optional[Path],
    catalog: Optional[Path],
) -> Tuple[int, Dict[str, object]]:
    report: Dict[str, object] = {
        "ok": False,
        "encoded_bytes": None,
        "parcels": 0,
        "encodable": 0,
        "unencodable": 0,
        "mutation_caught": False,
        "errors": [],
    }
    errors: List[str] = report["errors"]  # type: ignore[assignment]

    inc_path = llvm_src / RECORDS_REL
    cat_path = catalog or (llvm_src / CATALOG_REL)
    if not inc_path.is_file():
        errors.append(f"missing {inc_path}")
        return 1, report
    if not cat_path.is_file():
        errors.append(f"missing {cat_path}")
        return 1, report

    try:
        inc_text = inc_path.read_text(encoding="utf-8")
        encoded_bytes = parse_encoded_bytes(inc_text)
        declared = parse_non_nop_count(inc_text)
    except ValueError as exc:
        errors.append(str(exc))
        return 1, report
    report["encoded_bytes"] = encoded_bytes

    cat_text = cat_path.read_text(encoding="utf-8")
    mnems, unenc = catalog_mnemonics(cat_text)
    report["encodable"] = len(mnems)
    report["unencodable"] = len(unenc)
    if declared != len(mnems) + len(unenc):
        errors.append(
            f"catalog encodable+unencodable {len(mnems)+len(unenc)} "
            f"!= FormatENonNopLogicalCount {declared}"
        )

    llvm_mc = find_tool("llvm-mc", haydn_bin)
    llvm_objdump = find_tool("llvm-objdump", haydn_bin)
    llvm_objcopy = find_tool("llvm-objcopy", haydn_bin)
    missing = [
        n
        for n, p in (
            ("llvm-mc", llvm_mc),
            ("llvm-objdump", llvm_objdump),
            ("llvm-objcopy", llvm_objcopy),
        )
        if p is None
    ]
    if missing:
        errors.append("missing tools: " + ", ".join(missing))
        return 2, report

    enc_proc = run_cmd(
        [str(llvm_mc), f"--triple={TRIPLE}", "-show-encoding", str(cat_path)]
    )
    if enc_proc.returncode != 0:
        errors.append(
            f"llvm-mc -show-encoding failed ({enc_proc.returncode}): "
            f"{enc_proc.stderr.strip()[:500]}"
        )
        return 1, report
    blobs = parse_encoding_blobs(enc_proc.stdout)
    enc_bytes = b"".join(blobs)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        obj_path = tmp_path / "catalog.o"
        bin_path = tmp_path / "catalog.bin"
        obj_proc = run_cmd(
            [
                str(llvm_mc),
                f"--triple={TRIPLE}",
                "-filetype=obj",
                str(cat_path),
                "-o",
                str(obj_path),
            ]
        )
        if obj_proc.returncode != 0:
            errors.append(
                f"llvm-mc -filetype=obj failed ({obj_proc.returncode}): "
                f"{obj_proc.stderr.strip()[:500]}"
            )
            return 1, report
        try:
            obj_bytes = extract_text_bytes(llvm_objcopy, obj_path, bin_path)
        except RuntimeError as exc:
            errors.append(str(exc))
            return 1, report
        dis_proc = run_cmd(
            [
                str(llvm_objdump),
                "-d",
                "-z",
                "--no-show-raw-insn",
                f"--triple={TRIPLE}",
                str(obj_path),
            ]
        )
        if dis_proc.returncode != 0:
            errors.append(
                f"llvm-objdump -d failed ({dis_proc.returncode}): "
                f"{dis_proc.stderr.strip()[:500]}"
            )
            return 1, report
        dis_text = dis_proc.stdout

    report["parcels"] = (
        len(obj_bytes) // encoded_bytes if encoded_bytes and obj_bytes else 0
    )
    errors.extend(
        parity_errors(enc_bytes, obj_bytes, encoded_bytes, blobs, dis_text, mnems)
    )

    mutated = mutate_encoder_bytes(obj_bytes)
    mut_errs = parity_errors(
        enc_bytes, mutated, encoded_bytes, blobs, dis_text, mnems
    )
    mutation_caught = bool(mut_errs) and mutated != obj_bytes
    report["mutation_caught"] = mutation_caught
    if not mutation_caught:
        errors.append("mutation canary did not fail on XOR-flipped .text")

    ok = not errors
    report["ok"] = ok
    return (0 if ok else 1), report


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--llvm-src",
        type=Path,
        default=None,
        help="monorepo root (default: derived from this script)",
    )
    ap.add_argument(
        "--haydn-bin",
        type=Path,
        default=None,
        help="toolchain bin dir containing llvm-mc/objdump/objcopy",
    )
    ap.add_argument(
        "--catalog",
        type=Path,
        default=None,
        help="assembly catalog (default: format-e-mnemonic-roundtrip.s)",
    )
    ap.add_argument(
        "--self-test",
        "--self-check",
        action="store_true",
        dest="self_test",
        help="run synthetic parity / mutation vectors (no llvm-mc)",
    )
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()

    llvm_src = (args.llvm_src or monorepo_from_script()).resolve()
    if not (llvm_src / "llvm").is_dir():
        sys.stderr.write(
            f"check_mc_s_vs_obj_parity: not an llvm monorepo: {llvm_src}\n"
        )
        return 2

    haydn_bin = args.haydn_bin
    if haydn_bin is None and os.environ.get("HAYDN_BIN"):
        haydn_bin = Path(os.environ["HAYDN_BIN"])

    rc, report = check(llvm_src, haydn_bin, args.catalog)
    print(
        "check_mc_s_vs_obj_parity: "
        f"parcels={report['parcels']} encodable={report['encodable']} "
        f"unencodable={report['unencodable']} "
        f"EncodedBytes={report['encoded_bytes']} "
        f"mutation_caught={report['mutation_caught']} ok={report['ok']}"
    )
    for err in report["errors"]:  # type: ignore[union-attr]
        print(f"  ERROR: {err}", file=sys.stderr)
    if report["ok"]:
        print(
            "check_mc_s_vs_obj_parity: PASS "
            f"(encoding==obj, EncodedBytes={report['encoded_bytes']}, "
            f"{report['parcels']} parcels, mutation canary)"
        )
    else:
        print("check_mc_s_vs_obj_parity: FAIL", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
