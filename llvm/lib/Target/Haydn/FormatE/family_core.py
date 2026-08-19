"""Shared Format E family descriptor, golden pins, and path helpers.

E96 is the only admitted family. Generators take --family (default e96)
and fail closed on any other name.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Dict

DEFAULT_FAMILY = "e96"
# Authored catalog overlay (P19). Golden dir comes only from env / --json.
AUTHORED_OVERLAY_PATH = Path(__file__).with_name("authored_catalog_overlay.json")

# Manifest pin for the companion XLSX (geometry authority pair).
PINNED_XLSX_SHA256 = (
    "dd8491b7c182d006ad7d05c8cd46f64c02f439ae41bad0416f7139703d07b76f"
)
# Golden v2_1 (supersedes v2 2026-08-18): +120 MAC RR 32X16 instrs, +4 LS
# D_SW_F64RS rows, RRR operand-field canonicalization; zero removals, zero
# opcode changes, bit geometry identical to v2. Read-port repair applied
# 2026-08-18 (haydn_encoding.py --fix-read-ports --write: 6 FMUL*32S rows in
# instruction_type_index.json gained rtd; layout JSON untouched).
PINNED_JSON_SHA256 = (
    "2609877075156dd9749e1e8dd0b45ff1ef326dae2e1c2a9c10fbd9cd1c1c8f6a"
)
PINNED_INDEX_SHA256 = (
    "7a13453ad934d6be9a303b51fcaeb6e908d97015b3e75db7d76fa13cb7a6dede"
)
PINNED_CANONICAL_SHA256 = (
    "741f5b4141990dc27dc217d2b0c0d7ab57240e08ef31c34bc036f11bbda1938e"
)


@dataclass(frozen=True)
class BundleFamily:
    id: int
    cli_name: str
    display: str
    json_filename: str
    xlsx_filename: str
    index_filename: str
    canonical_filename: str
    constraints_filename: str
    pinned_json_sha256: str
    pinned_xlsx_sha256: str
    pinned_index_sha256: str
    pinned_canonical_sha256: str
    records_inc: str
    setdesc_ledger_inc: str
    members_td_inc: str
    logical_defs_td_inc: str
    member_opcodes_inc: str
    mnemonic_roundtrip_s: str
    sched_records_inc: str
    memory_cycles_inc: str


# FamilyID 0 matches HaydnFamilyE96 and C++ BundleFamily::E96.
E96 = BundleFamily(
    id=0,
    cli_name="e96",
    display="E96",
    json_filename="format_e_bit_layout_v2_1.json",
    xlsx_filename="format_e_bit_layout_v2_1.xlsx",
    index_filename="instruction_type_index.json",
    canonical_filename="format_e_canonical_vectors_v1.json",
    constraints_filename="VLIW_Engine_Compiler_Constraints.md",
    pinned_json_sha256=PINNED_JSON_SHA256,
    pinned_xlsx_sha256=PINNED_XLSX_SHA256,
    pinned_index_sha256=PINNED_INDEX_SHA256,
    pinned_canonical_sha256=PINNED_CANONICAL_SHA256,
    records_inc="HaydnGenFormatERecords.inc",
    setdesc_ledger_inc="HaydnGenFormatESetDescLedger.inc",
    members_td_inc="HaydnFormatsE96Members.td.inc",
    logical_defs_td_inc="HaydnInstrInfoGolden.td.inc",
    member_opcodes_inc="HaydnGenFormatEMemberOpcodes.inc",
    mnemonic_roundtrip_s="format-e-mnemonic-roundtrip.s",
    sched_records_inc="HaydnGenSchedRecords.inc",
    memory_cycles_inc="HaydnGenMemoryCycles.inc",
)

FAMILIES: Dict[str, BundleFamily] = {
    E96.cli_name: E96,
}


def get_family(name: str) -> BundleFamily:
    family = FAMILIES.get(name)
    if family is None:
        admitted = ", ".join(sorted(FAMILIES))
        raise SystemExit(
            f"error: unknown family {name!r}; admitted: {admitted}"
        )
    return family


RECORDS_GENERATOR = "llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py"
SCHED_GENERATOR = "llvm/lib/Target/Haydn/FormatE/generate_sched_records.py"
SCHEMA_VERSION = "format-e-records/1"
UMBRELLA_CHECK = "llvm/lib/Target/Haydn/FormatE/check_generated.sh"


def generated_banner(
    *,
    generator: str,
    family: BundleFamily,
    json_sha: str = "",
    xlsx_sha: str = "",
    prefix: str = "//",
) -> list:
    """P19 generated-file header. prefix is '//' or '#'."""
    check = f"python3 {generator} --check --family {family.cli_name}"
    lines = [
        f"{prefix} DO NOT EDIT.",
        f"{prefix} Generator: {generator}",
        f"{prefix} Schema: {SCHEMA_VERSION}",
        f"{prefix} Family: {family.cli_name} (id={family.id})",
        f"{prefix} Check: {check}",
        f"{prefix} Umbrella: {UMBRELLA_CHECK}",
    ]
    if json_sha:
        lines.append(f"{prefix} JSON-SHA256: {json_sha}")
    if xlsx_sha:
        lines.append(f"{prefix} XLSX-SHA256: {xlsx_sha}")
    if AUTHORED_OVERLAY_PATH.is_file():
        lines.append(f"{prefix} Overlay: {AUTHORED_OVERLAY_PATH.name}")
        lines.append(f"{prefix} Overlay-SHA256: {sha256_file(AUTHORED_OVERLAY_PATH)}")
    return lines


def add_family_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--family",
        default=DEFAULT_FAMILY,
        metavar="NAME",
        help=f"Bundle family (default: {DEFAULT_FAMILY})",
    )


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def resolve_golden_dir(family: BundleFamily) -> Path:
    """Pinned golden dir from HAYDN_GOLDEN_DIR or BUNDLESIM_GOLDEN_DIR.

    No host-path fallback. Qualification fails closed if neither env is set
    or the named directory does not contain the family's JSON.
    """
    json_name = family.json_filename
    for key in ("HAYDN_GOLDEN_DIR", "BUNDLESIM_GOLDEN_DIR"):
        raw = os.environ.get(key)
        if not raw:
            continue
        p = Path(raw)
        if (p / json_name).is_file():
            return p
        nested = p / "golden"
        if (nested / json_name).is_file():
            return nested
        raise SystemExit(
            f"error: {key}={raw} does not contain {json_name} "
            "(no host-path fallback)"
        )
    raise SystemExit(
        "error: set HAYDN_GOLDEN_DIR or BUNDLESIM_GOLDEN_DIR to the pinned "
        "golden directory (no host-path fallback)"
    )
