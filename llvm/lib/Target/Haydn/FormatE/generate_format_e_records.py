#!/usr/bin/env python3
"""Generate Format E tables + live TableGen members from golden JSON (once).

Emits HaydnGenFormatERecords.inc, HaydnGenFormatESetDescLedger.inc,
HaydnFormatsE96Members.td.inc (LIVE Inst{}), HaydnGenFormatEMemberOpcodes.inc,
and the MC mnemonic round-trip harness (test/MC/Haydn/format-e-mnemonic-roundtrip.s).
Product encode/decode: BUNDLE_E96 framing + tblgen on these members.

Usage:
  generate_format_e_records.py [--json PATH] [--xlsx PATH]
                               [--canonical-vectors PATH] [--xlsx-hash HEX]
                               [--out-dir DIR] [--check]
                               [--emit-mnemonic-roundtrip]

--check regenerates into memory and diffs against the committed files, then
fail-closes on XLSX↔JSON layout parity, td-vs-golden imm width/signedness,
and canonical-vector ledger round-trip.
It does not write, and it does not drive llvm-mc (ledger may_drive_llvm_mc_encode
is false). Peer: BundleSim generate_catalog.py --check
(bundlesim/isa/database/generate_catalog.py:483-485).
The mnemonic harness is included in that diff (same regenerate-and-compare).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

# Default golden location (plans tree). Overridable via --json / HAYDN_GOLDEN_DIR.
DEFAULT_GOLDEN_DIR = Path("/ssd2/mhyang/haydn-plans/Database/golden")
DEFAULT_JSON = DEFAULT_GOLDEN_DIR / "format_e_bit_layout_v2.json"
DEFAULT_XLSX = DEFAULT_GOLDEN_DIR / "format_e_bit_layout_v2.xlsx"
DEFAULT_CANONICAL = DEFAULT_GOLDEN_DIR / "format_e_canonical_vectors_v1.json"
# Manifest pin for the companion XLSX (geometry authority pair).
PINNED_XLSX_SHA256 = (
    "9b3c06612cec47fa026bd79cff5632cb970abdfe1e161075444f7d02432574af"
)
# Repaired golden: delivery b0b477e5… + haydn_encoding.py --fix-operand-mapping
# (76 mapping rows re-derived from instruction_type_index.json Syntax; bit
# geometry untouched). The delivery pin is retired — regenerating from it
# reintroduces the operand-mapping defect. The canonical-vector ledger embeds
# this hash in its oracle block, so it must be regenerated against the
# repaired golden on the plans machine before --check can pass again.
PINNED_JSON_SHA256 = (
    "8465132c2fb91e44a335d8a63577c637428d93106ed7a4d657d80ac70fdfa7f9"
)
PINNED_INDEX_SHA256 = (
    "e77908e9f09a6d649491389f8dabe06a896db22b230bedfe915d553e8801103b"
)
PINNED_CANONICAL_SHA256 = (
    "000cd92682adb7b88a727182318fa58bd0989547895ae36585c5cbd00f220c0d"
)
SSML_NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}

# Catalog snapshot pins from the current JSON hash (manifest §5).
PIN_UNIQUE_NON_NOP = 683
PIN_E2_NON_NOP = 677
PIN_E3_NON_NOP = 673
PIN_BOTH_NON_NOP = 667
PIN_E2_ONLY = 10
PIN_E3_ONLY = 6
PIN_TYPE_LAYOUTS = 126
PIN_E2_UNIT_PAIRS = 9
PIN_E3_LEGAL_TUPLES = 42
PIN_E3_ILLEGAL_TUPLES = 22

BIT_RE = re.compile(r"bit\[(\d+)(?::(\d+))?\]")
FIELD_RE = re.compile(
    r"^(?P<role>[A-Za-z0-9_]+)\((?P<aliases>[^)]*)\):\s*bit\[(?P<hi>\d+)(?::(?P<lo>\d+))?\]\s*\((?P<width>\d+)b\)$"
)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_bits(text: str) -> Optional[Tuple[int, int, int]]:
    """Return (hi, lo, width) from a golden bit[…] description, or None."""
    m = BIT_RE.search(text or "")
    if not m:
        return None
    hi = int(m.group(1))
    lo = int(m.group(2)) if m.group(2) is not None else hi
    if hi < lo:
        hi, lo = lo, hi
    return hi, lo, hi - lo + 1


def parse_opcode_width(text: str) -> Optional[int]:
    bits = parse_bits(text or "")
    return bits[2] if bits else None


def normalize_name(raw: str) -> str:
    return (raw or "").strip()


def parse_hex_opcode(raw: str) -> int:
    s = (raw or "").strip().lower()
    if not s:
        raise ValueError("empty opcode")
    if s.startswith("0x"):
        return int(s, 16)
    return int(s, 0)


def c_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def sanitize_ident(s: str) -> str:
    """TableGen / C identifier fragment from a golden token."""
    out = []
    for ch in s:
        if ch.isalnum() or ch == "_":
            out.append(ch)
        elif ch in "<>":
            out.append("TBD")
        else:
            out.append("_")
    ident = "".join(out)
    while "__" in ident:
        ident = ident.replace("__", "_")
    return ident.strip("_") or "X"


@dataclass(frozen=True)
class BitRange:
    hi: int
    lo: int

    @property
    def width(self) -> int:
        return self.hi - self.lo + 1


@dataclass
class OperandField:
    role: str
    aliases: Tuple[str, ...]
    bits: BitRange
    active_alias: str = ""  # per-instruction concrete alias when known


@dataclass
class TypeLayout:
    layout_id: int
    mode: str  # E2 / E3
    entry_num: int  # 0 or 1 (header entry_num)
    entry_idx: int  # 0..2
    unit: str
    unit_map: int
    type_name: str
    type_code: int
    type_code_width: int
    type_code_bits: BitRange
    opcode_bits: BitRange
    operand_fields: List[OperandField]
    reserved_text: str
    entry_bits: BitRange
    map_bits: BitRange


@dataclass
class MemberRecord:
    member_id: int
    logical: str
    is_nop: bool
    layout_id: int
    mode: str
    entry_num: int
    entry_idx: int
    unit: str
    unit_map: int
    type_name: str
    type_code: int
    opcode: int
    opcode_width: int
    # Future TableGen member spell; inert until activation includes it.
    member_symbol: str
    operand_active: Tuple[Tuple[str, str], ...]  # (role, active_alias)


@dataclass
class Catalog:
    bundle_bits: int
    payload_budget_bits: int
    payload_lsb: int
    layouts: List[TypeLayout] = field(default_factory=list)
    members: List[MemberRecord] = field(default_factory=list)
    # logical -> list of member_ids (stable order)
    alternatives: Dict[str, List[int]] = field(default_factory=dict)
    # inverse key -> member_id
    inverse: Dict[Tuple[str, int, str, str, int], int] = field(default_factory=dict)


def mode_name(entry_num_key: str) -> Tuple[str, int]:
    if entry_num_key == "entry_num_0":
        return "E2", 0
    if entry_num_key == "entry_num_1":
        return "E3", 1
    raise ValueError(f"unknown entry_num key {entry_num_key}")


def entry_index(entry_key: str) -> int:
    if not entry_key.startswith("entry"):
        raise ValueError(entry_key)
    return int(entry_key[len("entry") :])


def parse_operand_fields(raw_fields: Sequence[str]) -> List[OperandField]:
    out: List[OperandField] = []
    for item in raw_fields:
        m = FIELD_RE.match(item.strip())
        if not m:
            # Tolerate free-form reserved notes if ever mixed in.
            br = parse_bits(item)
            if br is None:
                raise ValueError(f"unparseable operand field: {item!r}")
            out.append(
                OperandField(
                    role="field",
                    aliases=tuple(),
                    bits=BitRange(br[0], br[1]),
                )
            )
            continue
        hi = int(m.group("hi"))
        lo = int(m.group("lo")) if m.group("lo") is not None else hi
        if hi < lo:
            hi, lo = lo, hi
        aliases = tuple(
            a.strip() for a in m.group("aliases").split(",") if a.strip()
        )
        width = int(m.group("width"))
        if width != hi - lo + 1:
            raise ValueError(
                f"width mismatch in {item!r}: declared {width} computed {hi-lo+1}"
            )
        out.append(
            OperandField(role=m.group("role"), aliases=aliases, bits=BitRange(hi, lo))
        )
    return out


def type_code_width_from_bin(bin_str: str) -> Tuple[int, int]:
    s = (bin_str or "").strip()
    if not s:
        raise ValueError("empty type_code_bin")
    value = int(s, 2)
    return value, len(s)


def build_catalog(data: Dict[str, Any]) -> Catalog:
    if data.get("format") != "E":
        raise SystemExit(f"expected format E, got {data.get('format')!r}")
    bundle_bits = int(data["bundle_bits"])
    cat = Catalog(
        bundle_bits=bundle_bits,
        payload_budget_bits=int(data["payload_budget_bits"]),
        payload_lsb=int(data["payload_lsb"]),
    )

    layout_id = 0
    member_id = 0
    inverse: Dict[Tuple[str, int, str, str, int], int] = {}
    alts: Dict[str, List[int]] = defaultdict(list)

    for entry_num_key in ("entry_num_0", "entry_num_1"):
        mode, entry_num = mode_name(entry_num_key)
        mode_obj = data[entry_num_key]
        for entry_key, entry_obj in sorted(mode_obj.items()):
            if not entry_key.startswith("entry"):
                continue
            if not isinstance(entry_obj, dict):
                continue
            eidx = entry_index(entry_key)
            entry_bits_t = parse_bits(entry_obj.get("_slot", ""))
            map_bits_t = parse_bits(entry_obj.get("mapping", ""))
            if entry_bits_t is None or map_bits_t is None:
                raise SystemExit(
                    f"missing entry/map geometry for {mode}/{entry_key}"
                )
            entry_bits = BitRange(entry_bits_t[0], entry_bits_t[1])
            map_bits = BitRange(map_bits_t[0], map_bits_t[1])

            for unit_name, unit_obj in entry_obj.items():
                if unit_name.startswith("_") or unit_name == "mapping":
                    continue
                if not isinstance(unit_obj, dict) or "types" not in unit_obj:
                    continue
                map_val = int(str(unit_obj["mapping_value"]).strip(), 2)
                tcode_bits_t = parse_bits(unit_obj.get("type_code", ""))
                if tcode_bits_t is None:
                    raise SystemExit(
                        f"missing type_code bits {mode}/{entry_key}/{unit_name}"
                    )
                tcode_bits = BitRange(tcode_bits_t[0], tcode_bits_t[1])
                types = unit_obj["types"]
                for type_name, type_obj in types.items():
                    tcode, twidth = type_code_width_from_bin(
                        type_obj["type_code_bin"]
                    )
                    if twidth != tcode_bits.width:
                        raise SystemExit(
                            f"type_code_bin width {twidth} != unit type_code "
                            f"{tcode_bits.width} at {mode}/{entry_key}/"
                            f"{unit_name}/{type_name}"
                        )
                    opc_bits_t = parse_bits(type_obj.get("opcode", ""))
                    if opc_bits_t is None:
                        raise SystemExit(
                            f"missing opcode bits {mode}/{entry_key}/{unit_name}/{type_name}"
                        )
                    opc_bits = BitRange(opc_bits_t[0], opc_bits_t[1])
                    operands = parse_operand_fields(type_obj.get("operand_fields", []))
                    layout = TypeLayout(
                        layout_id=layout_id,
                        mode=mode,
                        entry_num=entry_num,
                        entry_idx=eidx,
                        unit=unit_name,
                        unit_map=map_val,
                        type_name=type_name,
                        type_code=tcode,
                        type_code_width=twidth,
                        type_code_bits=tcode_bits,
                        opcode_bits=opc_bits,
                        operand_fields=operands,
                        reserved_text=str(type_obj.get("reserved", "")),
                        entry_bits=entry_bits,
                        map_bits=map_bits,
                    )
                    cat.layouts.append(layout)

                    seen_opc: Dict[int, str] = {}
                    for mapping in type_obj.get("mapping", []):
                        logical = normalize_name(mapping.get("instruction", ""))
                        if not logical:
                            raise SystemExit(
                                f"empty instruction name in {mode}/{entry_key}/{unit_name}/{type_name}"
                            )
                        opc = parse_hex_opcode(mapping.get("opcode", ""))
                        if opc.bit_length() > opc_bits.width and opc != 0:
                            # allow 0x0 with short forms; still check max
                            pass
                        if opc >= (1 << opc_bits.width):
                            raise SystemExit(
                                f"opcode {opc:#x} exceeds width {opc_bits.width} "
                                f"for {logical} at {mode}/{entry_key}/{unit_name}/{type_name}"
                            )
                        if opc in seen_opc and seen_opc[opc] != logical:
                            raise SystemExit(
                                f"duplicate opcode {opc:#x} in domain "
                                f"{mode}/{entry_key}/{unit_name}/{type_name}: "
                                f"{seen_opc[opc]} vs {logical}"
                            )
                        seen_opc[opc] = logical

                        active: List[Tuple[str, str]] = []
                        # Precompute mapping for dual-dest recovery (src1 empty).
                        map_norm = {
                            str(k): normalize_name(str(v)) if v is not None else ""
                            for k, v in mapping.items()
                        }
                        dual_dest_mac = (
                            map_norm.get("dest1", "") == "rtd1"
                            and map_norm.get("dest2", "") == "rtd2"
                            and map_norm.get("src2", "") == "rsd2"
                            and not map_norm.get("src1", "")
                        )
                        for of in operands:
                            # mapping rows carry role keys with selected alias text
                            raw = mapping.get(of.role, "")
                            active_alias = normalize_name(str(raw)) if raw is not None else ""
                            # Dual-dest MAC RRR: golden leaves src1 empty but dual-source
                            # product needs rsd1 on the wire (4 DR print matches catalog).
                            if (
                                not active_alias
                                and dual_dest_mac
                                and of.role.lower() == "src1"
                                and of.aliases
                            ):
                                for a in of.aliases:
                                    if str(a).strip().lower() == "rsd1":
                                        active_alias = normalize_name(str(a))
                                        break
                            active.append((of.role, active_alias))

                        symbol = (
                            f"{sanitize_ident(logical)}_{mode}_E{eidx}_"
                            f"{sanitize_ident(unit_name)}_{sanitize_ident(type_name)}"
                        )
                        # Disambiguate rare same-type multi-opc collisions on symbol
                        # by appending opcode when needed later; uniqueness checked below.
                        rec = MemberRecord(
                            member_id=member_id,
                            logical=logical,
                            is_nop=(logical == "NOP"),
                            layout_id=layout_id,
                            mode=mode,
                            entry_num=entry_num,
                            entry_idx=eidx,
                            unit=unit_name,
                            unit_map=map_val,
                            type_name=type_name,
                            type_code=tcode,
                            opcode=opc,
                            opcode_width=opc_bits.width,
                            member_symbol=symbol,
                            operand_active=tuple(active),
                        )
                        inv_key = (mode, eidx, unit_name, type_name, opc)
                        if inv_key in inverse:
                            raise SystemExit(f"inverse collision at {inv_key}")
                        inverse[inv_key] = member_id
                        if not rec.is_nop:
                            alts[logical].append(member_id)
                        cat.members.append(rec)
                        member_id += 1

                    layout_id += 1

    # Ensure member symbols unique (append opc if collision).
    seen_sym: Dict[str, int] = {}
    for rec in cat.members:
        base = rec.member_symbol
        if base in seen_sym:
            rec.member_symbol = f"{base}_{rec.opcode:X}"
        seen_sym[rec.member_symbol] = rec.member_id

    cat.inverse = inverse
    cat.alternatives = {k: v for k, v in sorted(alts.items())}
    validate_catalog(cat)
    return cat


def validate_catalog(cat: Catalog) -> None:
    if len(cat.layouts) != PIN_TYPE_LAYOUTS:
        raise SystemExit(
            f"type layout count {len(cat.layouts)} != pin {PIN_TYPE_LAYOUTS}"
        )
    non_nop = {m.logical for m in cat.members if not m.is_nop}
    e2 = {m.logical for m in cat.members if not m.is_nop and m.mode == "E2"}
    e3 = {m.logical for m in cat.members if not m.is_nop and m.mode == "E3"}
    both = e2 & e3
    if len(non_nop) != PIN_UNIQUE_NON_NOP:
        raise SystemExit(f"unique non-NOP {len(non_nop)} != {PIN_UNIQUE_NON_NOP}")
    if len(e2) != PIN_E2_NON_NOP:
        raise SystemExit(f"E2 non-NOP {len(e2)} != {PIN_E2_NON_NOP}")
    if len(e3) != PIN_E3_NON_NOP:
        raise SystemExit(f"E3 non-NOP {len(e3)} != {PIN_E3_NON_NOP}")
    if len(both) != PIN_BOTH_NON_NOP:
        raise SystemExit(f"both-mode non-NOP {len(both)} != {PIN_BOTH_NON_NOP}")
    if len(e2 - e3) != PIN_E2_ONLY:
        raise SystemExit(f"E2-only {len(e2-e3)} != {PIN_E2_ONLY}")
    if len(e3 - e2) != PIN_E3_ONLY:
        raise SystemExit(f"E3-only {len(e3-e2)} != {PIN_E3_ONLY}")

    # Every non-NOP alternative must invert to itself.
    for logical, mids in cat.alternatives.items():
        if not mids:
            raise SystemExit(f"empty alternatives for {logical}")
        for mid in mids:
            rec = cat.members[mid]
            key = (rec.mode, rec.entry_idx, rec.unit, rec.type_name, rec.opcode)
            if cat.inverse.get(key) != mid:
                raise SystemExit(f"inverse miss for {logical} key={key}")

    # Alt multiplicity pin (manifest §5).
    mult = Counter(len(v) for v in cat.alternatives.values())
    expected_mult = {1: 1, 2: 62, 3: 15, 4: 7, 5: 412, 7: 186}
    if dict(mult) != expected_mult:
        raise SystemExit(f"alt multiplicity {dict(mult)} != {expected_mult}")


def e2_unit_pairs(cat: Catalog) -> List[Tuple[str, str]]:
    e0 = sorted(
        {
            (m.unit_map, m.unit)
            for m in cat.members
            if m.mode == "E2" and m.entry_idx == 0 and not m.is_nop
        }
    )
    e1 = sorted(
        {
            (m.unit_map, m.unit)
            for m in cat.members
            if m.mode == "E2" and m.entry_idx == 1 and not m.is_nop
        }
    )
    pairs = [(a[1], b[1]) for a in e0 for b in e1]
    if len(pairs) != PIN_E2_UNIT_PAIRS:
        raise SystemExit(f"E2 pairs {len(pairs)} != {PIN_E2_UNIT_PAIRS}")
    return pairs


def e3_unit_tuples(cat: Catalog) -> Tuple[List[Tuple[str, str, str]], List[Tuple[str, str, str]]]:
    def units(eidx: int) -> List[str]:
        # Preserve mapping_value order 00,01,10,11
        items = {
            m.unit_map: m.unit
            for m in cat.members
            if m.mode == "E3" and m.entry_idx == eidx
        }
        return [items[k] for k in sorted(items)]

    e0, e1, e2 = units(0), units(1), units(2)
    legal: List[Tuple[str, str, str]] = []
    illegal: List[Tuple[str, str, str]] = []
    for a in e0:
        for b in e1:
            for c in e2:
                t = (a, b, c)
                if len(set(t)) == 3:
                    legal.append(t)
                else:
                    illegal.append(t)
    if len(legal) != PIN_E3_LEGAL_TUPLES or len(illegal) != PIN_E3_ILLEGAL_TUPLES:
        raise SystemExit(
            f"E3 tuples legal/illegal {len(legal)}/{len(illegal)} != "
            f"{PIN_E3_LEGAL_TUPLES}/{PIN_E3_ILLEGAL_TUPLES}"
        )
    return legal, illegal


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------


def mode_only_name_sets(cat: Catalog) -> Tuple[List[str], List[str]]:
    """Golden E2-only / E3-only logical-name SETS, sorted (W44 / P18(c)).

    The count pins (PIN_E2_ONLY / PIN_E3_ONLY) remain the catalog-shape
    invariant; these sets are the admission truth consumers derive from —
    generators emit SETS, not COUNTS (AIE pipeline study, ch4 port table:
    counts cannot catch identity drift, only cardinality drift).
    """
    e2 = {m.logical for m in cat.members if not m.is_nop and m.mode == "E2"}
    e3 = {m.logical for m in cat.members if not m.is_nop and m.mode == "E3"}
    e2_only = sorted(e2 - e3)
    e3_only = sorted(e3 - e2)
    if len(e2_only) != PIN_E2_ONLY or len(e3_only) != PIN_E3_ONLY:
        raise SystemExit(
            f"E2/E3-only sets {len(e2_only)}/{len(e3_only)} != pins "
            f"{PIN_E2_ONLY}/{PIN_E3_ONLY}"
        )
    return e2_only, e3_only


def emit_records_inc(
    cat: Catalog, json_sha: str, xlsx_sha: str
) -> str:
    e2_pairs = e2_unit_pairs(cat)
    e3_legal, e3_illegal = e3_unit_tuples(cat)
    e2_only, e3_only = mode_only_name_sets(cat)
    lines: List[str] = []
    lines.append("//===-- HaydnGenFormatERecords.inc - inert Format E records -*- C++ -*-===//")
    lines.append("//")
    lines.append("// Auto-generated by FormatE/generate_format_e_records.py")
    lines.append("// DO NOT EDIT. Regenerate with that script (supports --check).")
    lines.append("//")
    lines.append("// Inert: no product selector, encoder, decoder, or scheduler may")
    lines.append("// consume these tables until a later activation patch wires them.")
    lines.append("//===----------------------------------------------------------------------===//")
    lines.append("")
    lines.append("#ifdef GET_FORMAT_E_GOLDEN_PINS")
    lines.append("#undef GET_FORMAT_E_GOLDEN_PINS")
    lines.append(f'static constexpr const char FormatEJSONSHA256[] = "{json_sha}";')
    lines.append(f'static constexpr const char FormatEXLSXSHA256[] = "{xlsx_sha}";')
    lines.append(f"static constexpr unsigned FormatEBundleBits = {cat.bundle_bits}u;")
    lines.append(
        f"static constexpr unsigned FormatEPayloadBudgetBits = {cat.payload_budget_bits}u;"
    )
    lines.append(f"static constexpr unsigned FormatEPayloadLsb = {cat.payload_lsb}u;")
    lines.append(
        "static constexpr unsigned FormatEEncodedBytes ="
        " (FormatEBundleBits + 7u) / 8u;"
    )
    lines.append(f"static constexpr unsigned FormatETypeLayoutCount = {len(cat.layouts)}u;")
    lines.append(f"static constexpr unsigned FormatEMemberCount = {len(cat.members)}u;")
    lines.append(
        f"static constexpr unsigned FormatENonNopLogicalCount = {len(cat.alternatives)}u;"
    )
    lines.append(f"static constexpr unsigned FormatEUniqueNonNopNames = {PIN_UNIQUE_NON_NOP}u;")
    lines.append(f"static constexpr unsigned FormatEE2NonNopNames = {PIN_E2_NON_NOP}u;")
    lines.append(f"static constexpr unsigned FormatEE3NonNopNames = {PIN_E3_NON_NOP}u;")
    lines.append(f"static constexpr unsigned FormatEBothModeNonNopNames = {PIN_BOTH_NON_NOP}u;")
    lines.append(f"static constexpr unsigned FormatEE2OnlyNames = {PIN_E2_ONLY}u;")
    lines.append(f"static constexpr unsigned FormatEE3OnlyNames = {PIN_E3_ONLY}u;")
    lines.append(f"static constexpr unsigned FormatEE2UnitPairCount = {PIN_E2_UNIT_PAIRS}u;")
    lines.append(
        f"static constexpr unsigned FormatEE3LegalTupleCount = {PIN_E3_LEGAL_TUPLES}u;"
    )
    lines.append(
        f"static constexpr unsigned FormatEE3IllegalTupleCount = {PIN_E3_ILLEGAL_TUPLES}u;"
    )
    lines.append("// Format indicator and header reserved are fixed by golden geometry.")
    lines.append("static constexpr unsigned FormatEIndicator = 0x7u; // bits[2:0]")
    lines.append("static constexpr unsigned FormatEHeaderReserved = 0x0u; // bits[5:4]")
    lines.append("#endif // GET_FORMAT_E_GOLDEN_PINS")
    lines.append("")

    # Mode-only admission SETS (W44 / P18(c)): sorted logical names whose
    # golden catalog rows exist in exactly one Mode. Admission consumers
    # (residualAltCompatibleFormatMask, isFormatEE2Only/E3OnlyOpcodeName)
    # binary-search these sets — never a hand-transcribed name switch that
    # silently falls back to ProductFormatMask on drift.
    lines.append("#ifdef GET_FORMAT_E_MODE_ONLY_NAMES")
    lines.append("#undef GET_FORMAT_E_MODE_ONLY_NAMES")
    lines.append(f"static constexpr const char *const FormatEE2OnlyNameSet[] = {{")
    for n in e2_only:
        lines.append(f'  "{c_escape(n)}",')
    lines.append("};")
    lines.append(f"static constexpr const char *const FormatEE3OnlyNameSet[] = {{")
    for n in e3_only:
        lines.append(f'  "{c_escape(n)}",')
    lines.append("};")
    lines.append(
        "static_assert(sizeof(FormatEE2OnlyNameSet) /"
        f" sizeof(FormatEE2OnlyNameSet[0]) == {PIN_E2_ONLY}u,"
        ' "E2-only set pin");'
    )
    lines.append(
        "static_assert(sizeof(FormatEE3OnlyNameSet) /"
        f" sizeof(FormatEE3OnlyNameSet[0]) == {PIN_E3_ONLY}u,"
        ' "E3-only set pin");'
    )
    lines.append("#endif // GET_FORMAT_E_MODE_ONLY_NAMES")
    lines.append("")

    # Enumerations
    lines.append("#ifdef GET_FORMAT_E_ENUMS")
    lines.append("#undef GET_FORMAT_E_ENUMS")
    lines.append("enum class FormatEMode : uint8_t { E2 = 0, E3 = 1 };")
    lines.append("enum class FormatEUnit : uint8_t {")
    unit_names = sorted({m.unit for m in cat.members})
    for i, u in enumerate(unit_names):
        comma = "," if i + 1 < len(unit_names) else ""
        lines.append(f"  {sanitize_ident(u)} = {i}{comma}")
    lines.append("};")
    lines.append(
        f"static constexpr unsigned FormatEUnitCount = {len(unit_names)}u;"
    )
    lines.append("static constexpr const char *const FormatEUnitNames[] = {")
    for u in unit_names:
        lines.append(f'  "{c_escape(u)}",')
    lines.append("};")
    lines.append("#endif // GET_FORMAT_E_ENUMS")
    lines.append("")

    unit_index = {u: i for i, u in enumerate(unit_names)}

    # Type layouts
    lines.append("#ifdef GET_FORMAT_E_TYPE_LAYOUTS")
    lines.append("#undef GET_FORMAT_E_TYPE_LAYOUTS")
    lines.append("struct FormatETypeLayoutRec {")
    lines.append("  uint16_t LayoutId;")
    lines.append("  uint8_t Mode;          // 0=E2, 1=E3")
    lines.append("  uint8_t EntryNum;      // header entry_num")
    lines.append("  uint8_t EntryIdx;")
    lines.append("  uint8_t Unit;")
    lines.append("  uint8_t UnitMap;")
    lines.append("  uint8_t TypeCode;")
    lines.append("  uint8_t TypeCodeWidth;")
    lines.append("  uint8_t OpcodeHi;")
    lines.append("  uint8_t OpcodeLo;")
    lines.append("  uint8_t EntryHi;")
    lines.append("  uint8_t EntryLo;")
    lines.append("  uint8_t MapHi;")
    lines.append("  uint8_t MapLo;")
    lines.append("  const char *TypeName;")
    lines.append("  const char *UnitName;")
    lines.append("};")
    lines.append("static constexpr FormatETypeLayoutRec FormatETypeLayouts[] = {")
    for lay in cat.layouts:
        lines.append(
            "  {"
            f"{lay.layout_id}, "
            f"{0 if lay.mode == 'E2' else 1}, "
            f"{lay.entry_num}, "
            f"{lay.entry_idx}, "
            f"{unit_index[lay.unit]}, "
            f"{lay.unit_map}, "
            f"{lay.type_code}, "
            f"{lay.type_code_width}, "
            f"{lay.opcode_bits.hi}, "
            f"{lay.opcode_bits.lo}, "
            f"{lay.entry_bits.hi}, "
            f"{lay.entry_bits.lo}, "
            f"{lay.map_bits.hi}, "
            f"{lay.map_bits.lo}, "
            f'"{c_escape(lay.type_name)}", '
            f'"{c_escape(lay.unit)}"'
            "},"
        )
    lines.append("};")
    lines.append(
        "static_assert(sizeof(FormatETypeLayouts) / sizeof(FormatETypeLayouts[0]) =="
        " FormatETypeLayoutCount, \"layout pin\");"
    )
    lines.append("#endif // GET_FORMAT_E_TYPE_LAYOUTS")
    lines.append("")

    # Members
    lines.append("#ifdef GET_FORMAT_E_MEMBERS")
    lines.append("#undef GET_FORMAT_E_MEMBERS")
    lines.append("struct FormatEMemberRec {")
    lines.append("  uint16_t MemberId;")
    lines.append("  uint16_t LayoutId;")
    lines.append("  uint8_t Mode;")
    lines.append("  uint8_t EntryIdx;")
    lines.append("  uint8_t Unit;")
    lines.append("  uint8_t UnitMap;")
    lines.append("  uint8_t TypeCode;")
    lines.append("  uint8_t OpcodeWidth;")
    lines.append("  uint16_t Opcode;")
    lines.append("  uint8_t IsNop;")
    lines.append("  const char *Logical;")
    lines.append("  const char *TypeName;")
    lines.append("  const char *MemberSymbol;")
    lines.append("};")
    lines.append("static constexpr FormatEMemberRec FormatEMembers[] = {")
    for rec in cat.members:
        lines.append(
            "  {"
            f"{rec.member_id}, "
            f"{rec.layout_id}, "
            f"{0 if rec.mode == 'E2' else 1}, "
            f"{rec.entry_idx}, "
            f"{unit_index[rec.unit]}, "
            f"{rec.unit_map}, "
            f"{rec.type_code}, "
            f"{rec.opcode_width}, "
            f"{rec.opcode}, "
            f"{1 if rec.is_nop else 0}, "
            f'"{c_escape(rec.logical)}", '
            f'"{c_escape(rec.type_name)}", '
            f'"{c_escape(rec.member_symbol)}"'
            "},"
        )
    lines.append("};")
    lines.append(
        "static_assert(sizeof(FormatEMembers) / sizeof(FormatEMembers[0]) =="
        " FormatEMemberCount, \"member pin\");"
    )
    lines.append("#endif // GET_FORMAT_E_MEMBERS")
    lines.append("")

    # Alternatives: flatten logical names + offset table
    lines.append("#ifdef GET_FORMAT_E_ALTERNATIVES")
    lines.append("#undef GET_FORMAT_E_ALTERNATIVES")
    lines.append("struct FormatEAltSpan {")
    lines.append("  const char *Logical;")
    lines.append("  uint16_t Begin;")  # index into FormatEAltMemberIds
    lines.append("  uint16_t Count;")
    lines.append("};")
    flat: List[int] = []
    spans: List[Tuple[str, int, int]] = []
    for logical, mids in cat.alternatives.items():
        begin = len(flat)
        flat.extend(mids)
        spans.append((logical, begin, len(mids)))
    lines.append("static constexpr uint16_t FormatEAltMemberIds[] = {")
    if flat:
        row: List[str] = []
        for i, mid in enumerate(flat):
            row.append(str(mid))
            if len(row) == 16 or i + 1 == len(flat):
                lines.append("  " + ", ".join(row) + ",")
                row = []
    else:
        lines.append("  0")
    lines.append("};")
    lines.append(
        f"static constexpr unsigned FormatEAltMemberIdCount = {len(flat)}u;"
    )
    lines.append("static constexpr FormatEAltSpan FormatEAltSpans[] = {")
    for logical, begin, count in spans:
        lines.append(
            f'  {{"{c_escape(logical)}", {begin}, {count}}},'
        )
    lines.append("};")
    lines.append(
        "static_assert(sizeof(FormatEAltSpans) / sizeof(FormatEAltSpans[0]) =="
        " FormatENonNopLogicalCount, \"alt span pin\");"
    )
    lines.append("#endif // GET_FORMAT_E_ALTERNATIVES")
    lines.append("")

    # Inverse table: sorted by (mode, entry, unit, type, opcode)
    lines.append("#ifdef GET_FORMAT_E_INVERSE")
    lines.append("#undef GET_FORMAT_E_INVERSE")
    lines.append("struct FormatEInverseRec {")
    lines.append("  uint8_t Mode;")
    lines.append("  uint8_t EntryIdx;")
    lines.append("  uint8_t Unit;")
    lines.append("  uint8_t TypeCode;")
    lines.append("  uint16_t Opcode;")
    lines.append("  uint16_t MemberId;")
    lines.append("  const char *TypeName;")
    lines.append("  const char *Logical;")
    lines.append("};")
    inv_rows = []
    for rec in cat.members:
        inv_rows.append(rec)
    inv_rows.sort(
        key=lambda r: (
            0 if r.mode == "E2" else 1,
            r.entry_idx,
            unit_index[r.unit],
            r.type_code,
            r.opcode,
            r.member_id,
        )
    )
    lines.append("static constexpr FormatEInverseRec FormatEInverse[] = {")
    for rec in inv_rows:
        lines.append(
            "  {"
            f"{0 if rec.mode == 'E2' else 1}, "
            f"{rec.entry_idx}, "
            f"{unit_index[rec.unit]}, "
            f"{rec.type_code}, "
            f"{rec.opcode}, "
            f"{rec.member_id}, "
            f'"{c_escape(rec.type_name)}", '
            f'"{c_escape(rec.logical)}"'
            "},"
        )
    lines.append("};")
    lines.append(
        "static_assert(sizeof(FormatEInverse) / sizeof(FormatEInverse[0]) =="
        " FormatEMemberCount, \"inverse pin\");"
    )
    lines.append("#endif // GET_FORMAT_E_INVERSE")
    lines.append("")

    # Unit injectivity helper tables
    lines.append("#ifdef GET_FORMAT_E_UNIT_INJECTIVITY")
    lines.append("#undef GET_FORMAT_E_UNIT_INJECTIVITY")
    lines.append("struct FormatEUnitPair { uint8_t U0; uint8_t U1; };")
    lines.append("struct FormatEUnitTriple { uint8_t U0; uint8_t U1; uint8_t U2; };")
    lines.append("static constexpr FormatEUnitPair FormatEE2UnitPairs[] = {")
    for a, b in e2_pairs:
        lines.append(f"  {{{unit_index[a]}, {unit_index[b]}}},")
    lines.append("};")
    lines.append("static constexpr FormatEUnitTriple FormatEE3LegalTuples[] = {")
    for a, b, c in e3_legal:
        lines.append(f"  {{{unit_index[a]}, {unit_index[b]}, {unit_index[c]}}},")
    lines.append("};")
    lines.append("static constexpr FormatEUnitTriple FormatEE3IllegalTuples[] = {")
    for a, b, c in e3_illegal:
        lines.append(f"  {{{unit_index[a]}, {unit_index[b]}, {unit_index[c]}}},")
    lines.append("};")
    lines.append("#endif // GET_FORMAT_E_UNIT_INJECTIVITY")
    lines.append("")

    return "\n".join(lines) + "\n"


def operand_signature(rec: MemberRecord, layouts: Dict[int, TypeLayout]) -> str:
    lay = layouts[rec.layout_id]
    parts = []
    for of, (_role, active) in zip(lay.operand_fields, rec.operand_active):
        alias = active if active else "|".join(of.aliases)
        parts.append(f"{of.role}:{alias}:{of.bits.width}")
    return ";".join(parts)


def emit_setdesc_ledger_inc(cat: Catalog) -> str:
    """Start the setDesc compatibility ledger.

    Full MCInstrDesc equality (regclasses, ties, implicits, flags, sched
    cycles) requires live member opcodes and is completed when members are
    activated. This stage records the golden-side structural signature each
    logical/member pair must satisfy: operand role vector and field widths,
    plus placement identity. Ambiguous role vectors within one logical are
    flagged for later descriptor work.
    """
    layouts = {l.layout_id: l for l in cat.layouts}
    lines: List[str] = []
    lines.append("//===-- HaydnGenFormatESetDescLedger.inc - setDesc ledger -*- C++ -*-===//")
    lines.append("//")
    lines.append("// Auto-generated by FormatE/generate_format_e_records.py")
    lines.append("// DO NOT EDIT.")
    lines.append("//")
    lines.append("// Stage-1 setDesc compatibility ledger (inert). Each row is one")
    lines.append("// logical→member placement with the golden operand-role signature.")
    lines.append("// Generation fails closed on inverse misses; cross-member signature")
    lines.append("// divergence within a logical is recorded for later MCInstrDesc gating.")
    lines.append("//===----------------------------------------------------------------------===//")
    lines.append("")
    lines.append("#ifdef GET_FORMAT_E_SETDESC_LEDGER")
    lines.append("#undef GET_FORMAT_E_SETDESC_LEDGER")
    lines.append("struct FormatESetDescLedgerRec {")
    lines.append("  uint16_t MemberId;")
    lines.append("  uint16_t LayoutId;")
    lines.append("  uint8_t Mode;")
    lines.append("  uint8_t EntryIdx;")
    lines.append("  uint8_t Unit;")
    lines.append("  uint8_t OperandCount;")
    lines.append("  uint8_t SignatureGroup;")  # per-logical group id
    lines.append("  const char *Logical;")
    lines.append("  const char *MemberSymbol;")
    lines.append("  const char *OperandSignature;")
    lines.append("};")

    # Group signatures per logical
    sig_groups: Dict[str, Dict[str, int]] = {}
    rows: List[Tuple[MemberRecord, str, int, int]] = []
    unit_names = sorted({m.unit for m in cat.members})
    unit_index = {u: i for i, u in enumerate(unit_names)}

    for logical, mids in cat.alternatives.items():
        gmap: Dict[str, int] = {}
        for mid in mids:
            rec = cat.members[mid]
            sig = operand_signature(rec, layouts)
            if sig not in gmap:
                gmap[sig] = len(gmap)
            lay = layouts[rec.layout_id]
            rows.append((rec, sig, gmap[sig], len(lay.operand_fields)))
        sig_groups[logical] = gmap

    multi = sum(1 for g in sig_groups.values() if len(g) > 1)
    lines.append(
        f"// Logicals with >1 golden operand-role signature across placements: {multi}"
    )
    lines.append(
        f"static constexpr unsigned FormatESetDescMultiSignatureLogicals = {multi}u;"
    )
    lines.append(
        f"static constexpr unsigned FormatESetDescLedgerCount = {len(rows)}u;"
    )
    lines.append("static constexpr FormatESetDescLedgerRec FormatESetDescLedger[] = {")
    for rec, sig, gid, opc in rows:
        lines.append(
            "  {"
            f"{rec.member_id}, "
            f"{rec.layout_id}, "
            f"{0 if rec.mode == 'E2' else 1}, "
            f"{rec.entry_idx}, "
            f"{unit_index[rec.unit]}, "
            f"{opc}, "
            f"{gid}, "
            f'"{c_escape(rec.logical)}", '
            f'"{c_escape(rec.member_symbol)}", '
            f'"{c_escape(sig)}"'
            "},"
        )
    lines.append("};")
    lines.append("#endif // GET_FORMAT_E_SETDESC_LEDGER")
    lines.append("")
    return "\n".join(lines) + "\n"



def entry_base_class(mode: str, entry_idx: int) -> str:
    if mode == "E2":
        return f"HaydnEntryE2E{entry_idx}"
    return f"HaydnEntryE3E{entry_idx}"


def entry_inst_field(mode: str, entry_idx: int) -> str:
    if mode == "E2":
        return "e0" if entry_idx == 0 else "e1"
    return ("e0", "e1", "e2")[entry_idx]


def entry_bit_width(mode: str, entry_idx: int) -> int:
    if mode == "E2":
        return 45 if entry_idx == 0 else 41
    return 31 if entry_idx < 2 else 27


def abs_to_rel(abs_hi: int, abs_lo: int, entry_lo: int):
    return abs_hi - entry_lo, abs_lo - entry_lo


def classify_alias(alias: str, role: str) -> str:
    a = (alias or role or "").lower().strip()
    r = (role or "").lower()
    if r in ("cbr_sel", "hwlr_sel", "ar_sel") or a in (
        "cbr_sel", "hwlr_sel", "ar_sel",
    ):
        return "IMM"
    if a.startswith(("imm", "uimm", "simm", "off")):
        return "IMM"
    if "imm" in r and not any(x in a for x in ("rt", "rs", "rd", "rtd", "rsd")):
        return "IMM"
    if a.startswith("ar") or a in ("ar0", "ar1"):
        return "REG_AR"
    if a.startswith(("rtd", "rsd", "dr")) or a in (
        "rtd1", "rtd2", "rsd1", "rsd2",
    ):
        return "REG_DR"
    if re.match(r"r\d+$", a) or a in (
        "rt", "rs", "rd", "rs1", "rs2", "rs3", "ra", "rt1", "rt2",
    ):
        return "REG_GPR"
    if "imm" in r or r.endswith("_sel"):
        return "IMM"
    if "rtd" in r or "rsd" in r:
        return "REG_DR"
    return "REG_GPR"


# BundleSim catalog + ALU32.td authority for signed vs ZEXT immediates.
# Golden JSON only says "imm20"/"imm12"; signedness lives in semantic behavior.
SIMM20_LOGICALS = frozenset({
    "ADDI32", "ADDI32S", "SUBI32", "SUBI32S", "JAL",
})
# Logic RI20: ZEXT/bit-pattern (catalog bitpattern_mask), not signed.
UIMM20_LOGICALS = frozenset({
    "ANDI32", "ORI32", "XORI32",
})
SIMM12_LOGICALS = frozenset({
    # Cond branches: signed PC-relative imm12.
    "BEQ", "BNE", "BGE", "BGEU", "BGEZ", "BLT", "BLTZ", "BLTU",
    "BEQZ", "BNEZ",
    # JALR: signed rs-relative byte offset (Shift=0); shared RI12 field.
    "JALR",
})


def is_ls_ri6_scaled_imm(logical: str) -> bool:
    """LS/AGU RI6 element-index immediates are simm6 (per catalog)."""
    u = (logical or "").strip().upper()
    if not u:
        return False
    # PRE/POST/WITH/BREV imm forms: S_LW_PRE_IMM, D_LDW_WITH_IMM, …
    return any(
        tag in u
        for tag in (
            "_PRE_IMM",
            "_POST_IMM",
            "_WITH_IMM",
            "_BREV_IMM",
            "_CB_IMM",
        )
    )


# Golden `uimmN`/`simmN` annotations overlay td operand class only when the
# generated field width already matches. Width-only `immN` and unmatched
# widths stay as generated (open annotation residual).
_GOLDEN_IMM_ANN: Dict[str, Tuple[str, int]] = {}


def field_operand_td(
    of: "OperandField",
    idx: int,
    active_alias: str = "",
    logical: str = "",
):
    alias = active_alias or (of.aliases[0] if of.aliases else of.role)
    kind = classify_alias(alias, of.role)
    w = of.bits.width
    name = sanitize_ident(f"{of.role}_{idx}")
    logu = (logical or "").strip().upper()
    gold = _GOLDEN_IMM_ANN.get(logu)
    if kind == "IMM" and gold and gold[1] == w and gold[0] in ("uimm", "simm"):
        return name, f"{gold[0]}{gold[1]}:${name}", str(w)
    if kind == "REG_DR":
        return name, f"DR64:${name}", str(w)
    if kind == "REG_AR":
        return name, f"AR:${name}", str(w)
    if kind == "REG_GPR":
        return name, f"GPR32:${name}", str(w)
    pcrel = PCREL_OPERAND_TD.get(logu)
    if kind == "IMM" and pcrel:
        return name, f"{pcrel}:${name}", str(w)
    wide_abs = WIDE_ABS_OPERAND_TD.get(logu)
    if kind == "IMM" and wide_abs:
        return name, f"{wide_abs}:${name}", str(w)
    if w == 1:
        ty = "uimm1"
    elif w == 2:
        ty = "uimm2"
    elif w == 6 and is_ls_ri6_scaled_imm(logu):
        # Element index / post-inc stride: signed 6 (not golden alias "uimm6").
        ty = "simm6"
    elif w == 8 and (
        of.role.lower() == "imm"
        or "simm" in of.role.lower()
        or any("simm" in a.lower() or a.lower() == "imm8" for a in of.aliases)
    ):
        ty = "simm8"
    elif w <= 8:
        ty = f"uimm{w}" if w in (4, 5, 6, 7, 8) else "uimm8"
    elif w <= 12:
        # Branches: signed PC-relative imm12 (catalog signed_mask).
        if logu in SIMM12_LOGICALS or "simm" in of.role.lower() or any(
            "simm" in a.lower() for a in of.aliases
        ):
            ty = "simm12"
        else:
            ty = "uimm12"
    elif w <= 16:
        ty = "uimm16"
    else:
        # Full 32-bit immediates (MOVEI_H/L I32 type): residual uses
        # simm32_movei. Mapping these to uimm20 truncated encode/print to
        # 20 bits (objdump showed 0x12345678 → 284280) and BundleSim golden
        # checks failed on movei_h high half.
        if w >= 32 or logu in ("MOVEI_H", "MOVEI_L"):
            ty = "simm32_movei"
        # RI20+: ADDI/SUBI/JAL are simm20; AND/OR/XOR are uimm20 (ZEXT).
        elif logu in SIMM20_LOGICALS or "simm" in of.role.lower() or any(
            "simm" in a.lower() for a in of.aliases
        ):
            ty = "simm20"
        elif logu in UIMM20_LOGICALS:
            ty = "uimm20"
        else:
            # Default unsigned for unknown wide immediates (LUI-style).
            ty = "uimm20"
    return name, f"{ty}:${name}", str(w)


def emit_entry_bits_assign(lines, rec, lay, bit_names, entry_w, entry_lo, field_name: str):
    segs = []
    mh, ml = abs_to_rel(lay.map_bits.hi, lay.map_bits.lo, entry_lo)
    segs.append((ml, mh, "const", rec.unit_map))
    type_lo_abs = lay.map_bits.hi + 1
    type_hi_abs = type_lo_abs + lay.type_code_width - 1
    th, tl = abs_to_rel(type_hi_abs, type_lo_abs, entry_lo)
    segs.append((tl, th, "const", rec.type_code))
    oh, ol = abs_to_rel(lay.opcode_bits.hi, lay.opcode_bits.lo, entry_lo)
    segs.append((ol, oh, "const", rec.opcode))
    for item in bit_names:
        name, rh, rl, w, is_var = item
        if is_var:
            segs.append((rl, rh, "var", name))
        else:
            segs.append((rl, rh, "const", 0))
    segs.sort(key=lambda s: s[0])
    filled = []
    cursor = 0
    for lo, hi, kind, payload in segs:
        if lo < cursor:
            raise SystemExit(f"overlap {rec.member_symbol} [{hi}:{lo}]")
        if lo > cursor:
            filled.append((cursor, lo - 1, "const", 0))
        filled.append((lo, hi, kind, payload))
        cursor = hi + 1
    if cursor < entry_w:
        filled.append((cursor, entry_w - 1, "const", 0))
    elif cursor > entry_w:
        raise SystemExit(f"overflow {rec.member_symbol}")
    parts = []
    ci = 0
    for lo, hi, kind, payload in reversed(filled):
        w = hi - lo + 1
        if kind == "const":
            cname = f"c{ci}"
            ci += 1
            lines.append(f"  bits<{w}> {cname} = {payload:#x};")
            parts.append(cname)
        else:
            parts.append(payload)
    lines.append(f"  // {field_name} entry window {entry_w}b (MSB-first concat)")
    lines.append(f"  let {field_name} = {{" + ", ".join(parts) + "};")


# Published R5 itinerary classes, keyed by the member's generated unit.
# ALU0→Slot0_ALU, ALU1→Slot1_ALU, ALU2→Slot2_ALU, LOADSTORE0→Slot0_LS,
# LOAD1→Slot1_LD, MAC0→Slot1_MAC, MAC1→Slot2_MAC. Dual-unit classes
# (Slot012_ALU / Slot12_MAC) stay on logical opcodes in HaydnInstrFormats.td.
UNIT_ITINERARY = {
    "ALU0": "Slot0_ALU",
    "ALU1": "Slot1_ALU",
    "ALU2": "Slot2_ALU",
    "LOADSTORE0": "Slot0_LS",
    "LOAD1": "Slot1_LD",
    "MAC0": "Slot1_MAC",
    "MAC1": "Slot2_MAC",
}

# SIN_COS / ARCTAN: Constraints Data_Latency = uimm4+2. Conservative dest
# bound is the published Slot*_ALU_SinCosLat class (OperandCycles 17).
SINCOS_LOGICALS = frozenset({"SIN_COS", "ARCTAN"})
SINCOS_ITINERARY = {
    "ALU1": "Slot1_ALU_SinCosLat",
    "ALU2": "Slot2_ALU_SinCosLat",
}

BRANCH_LOGICALS = frozenset(
    {
        "BEQ",
        "BNE",
        "BGE",
        "BGEU",
        "BLT",
        "BLTU",
        "BEQZ",
        "BNEZ",
        "BGEZ",
        "BLTZ",
    }
)
CALL_LOGICALS = frozenset({"JAL"})
INDIRECT_CALL_LOGICALS = frozenset({"JALR"})
# Catalog role `reg` (alias rt) is an SSA dest for these logicals. Golden
# I8/I12/SFR_OUT rows name dest=rt; bit-layout still emits role `reg`.
# Do not include branch `reg` (rs), CSRW, or MOVEGPR2SFR (SFR writers).
DEST_REG_LOGICALS = frozenset(
    {
        "LUI",
        "ZERO_GPR",
        "ZERO_DR",
        "CSRR",
        "MOVESFR2GPR",
    }
)
# GE96-03: compact and `_W` share byte PC+imm. Generated members must use
# the WIDE PCRel operand class so reloc-bearing `_W_S0` can cut over
# without a CHECK-only mnemonic rewrite. AsmString stays the golden name
# (`jal`); llc after cutover matches objdump.
PCREL_OPERAND_TD = {
    "JAL": "brtarget_wide_i20",
    "JALR": "calltarget_wide_ri12",
    "BEQZ": "brtarget_wide_i12",
    "BNEZ": "brtarget_wide_i12",
    "BGEZ": "brtarget_wide_i12",
    "BLTZ": "brtarget_wide_i12",
    "BEQ": "brtarget_wide_ri12",
    "BNE": "brtarget_wide_ri12",
    "BGE": "brtarget_wide_ri12",
    "BGEU": "brtarget_wide_ri12",
    "BLT": "brtarget_wide_ri12",
    "BLTU": "brtarget_wide_ri12",
}
# GE96: compact and `_W` share the Format E RI20 field. Generated members
# use the WIDE absolute class so reloc-bearing `ADDI32_W`/`ORI32_W` can
# cut over. AsmString stays the golden name (`addi32`/`ori32`).
# SET_HWLOOP Off1/Off2 stay uimm6/uimm12: getExprFixupKind maps OpNo 1/2
# to HWLoopOff1/Off2. Do not put hwloop_off* on members (Shift=2 would
# fight the composite field).
WIDE_ABS_OPERAND_TD = {
    "ADDI32": "simm20_wide_abs",
    "ORI32": "uimm20_wide_abs",
    "ANDI32": "uimm20_wide_abs",
    "XORI32": "uimm20_wide_abs",
}
LS_UNITS = frozenset({"LOADSTORE0", "LOAD1"})
# Catalog WITH_IMM logical → user-facing matcher/print mnemonic. Inverse of
# peelLogicalOpcodeName for the RI6 signed-offset forms. Word/dword WITH_REG
# uses the documented 3-GPR user names. Byte/half `ld8_reg`/`ld16_reg` stay
# catalog: those user spellings are simm16 FieldSlots, not 3-GPR members.
LS_USER_MNEMONIC = {
    "S_LW_WITH_IMM": "ld32",
    "S_LW_WITH_REG": "ld32_reg",
    "S_SW_WITH_IMM": "st32",
    "S_SW_WITH_REG": "st32_reg",
    "D_LDW_WITH_IMM": "ld64",
    "D_LDW_WITH_REG": "ld64_reg",
    "D_SDW_WITH_IMM": "st64",
    "D_SDW_WITH_REG": "st64_reg",
    "S_LBS_WITH_IMM": "ld8",
    "S_LBU_WITH_IMM": "ldu8",
    "S_SB_WITH_IMM": "st8",
    "S_LHWS_WITH_IMM": "ld16",
    "S_LHWU_WITH_IMM": "ldu16",
    "S_SHW_WITH_IMM": "st16",
}
# Golden type names that are CSR / WFI / hwloop / SFR / AR / circular-buffer.
SIDE_EFFECT_TYPES = frozenset(
    {"SFR", "HINT", "HWLRIII", "HWLRIIR", "HWLRRRR", "AR", "CBRI", "CBRR"}
)

BLANKET_HAS_SIDE_EFFECTS_LET = (
    "let isCodeGenOnly = 0, isAsmParserOnly = 0, hasSideEffects = 1 in"
)
MEMBER_E23_DEF_RE = re.compile(
    r"^def\s+([A-Za-z0-9_]+_E[23]_[A-Za-z0-9_]+)\s*:"
)


@dataclass(frozen=True)
class MemberEmitFlags:
    """Per-member itinerary + MCID flags emitted on the wrapping `let`."""

    itinerary: str
    may_load: int
    may_store: int
    is_branch: int
    is_terminator: int
    is_call: int
    is_indirect_branch: int
    has_side_effects: int

    def let_line(self) -> str:
        parts = [
            f"Itinerary = {self.itinerary}",
            f"mayLoad = {self.may_load}",
            f"mayStore = {self.may_store}",
            f"isBranch = {self.is_branch}",
            f"isTerminator = {self.is_terminator}",
        ]
        if self.is_call:
            parts.append("isCall = 1")
        if self.is_indirect_branch:
            parts.append("isIndirectBranch = 1")
        parts.extend(
            [
                f"hasSideEffects = {self.has_side_effects}",
                "isCodeGenOnly = 0",
                "isAsmParserOnly = 0",
                'AsmVariantName = "e96member"',
            ]
        )
        return "let " + ", ".join(parts) + " in {"


def _logical_key(logical: str) -> str:
    return logical.strip().upper()


def _is_load_logical(key: str) -> bool:
    return key.startswith(("D_L", "S_L", "PLD"))


def _is_store_logical(key: str) -> bool:
    return key.startswith(("D_S", "S_S", "WBAR"))


def load_td_tied_logicals(td_dir: Path) -> set:
    """Logicals whose LLVM def models a tie (per-def `let Constraints`).

    The member Desc must mirror the LOGICAL's operand shape — setDesc keeps
    the MI operands — so a golden accumulator tie is only emittable when the
    logical actually carries the tied input. Some conditional-move logicals
    (MOVT64/MOVF64/MOVT32/MOVF32, MOVEI_*) read their destination per golden
    Behavior but are modeled UNTIED two-operand defs in TD; those diverge
    (see the CB ledger) and their members must stay at logical arity.
    Comments are skipped; a Constraints match is attributed to the nearest
    preceding `def NAME`."""
    tied = set()
    for fn in ("HaydnInstrInfo.td", "HaydnInstrInfoAuto.td"):
        path = td_dir / fn
        if not path.is_file():
            raise SystemExit(f"error: TD file for tie scan missing: {path}")
        text = path.read_text(encoding="utf-8")
        # Two spellings carry a tie: a body/backward `let Constraints = ...`
        # inside (or after) the def, and the PREFIX group form
        # `let Constraints = "..." in { ... def A; def B; ... }` where the
        # constraint precedes every def it governs (X2MULA32's dual-dest
        # family). Brace depth is tracked so a group closes exactly where
        # its `{` closes; def BODIES contribute braces too.
        depth = 0
        region_stack: List[int] = []
        current = None
        for raw in text.splitlines():
            ln = raw.split("//", 1)[0]
            stripped = ln.strip()
            m = re.match(r"def\s+([A-Za-z0-9_]+)", stripped)
            if m:
                current = m.group(1)
                if region_stack:
                    tied.add(_logical_key(current))
            if "Constraints" in ln and "=" in ln:
                if re.search(r"\bin\s*\{", ln):
                    region_stack.append(depth)
                elif current:
                    tied.add(_logical_key(current))
            for ch in ln:
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    while region_stack and region_stack[-1] >= depth:
                        region_stack.pop()
    return tied


def load_accumulator_ties(index_path: Path) -> Dict[str, Tuple[str, ...]]:
    """Golden accumulator law: an instruction whose *_Write_Port alias also
    appears in the same bank's *_Read_Port reads its own destination — the
    Behavior column spells it out (`rtd = rtd + ...`). LLVM models that as a
    tied accumulator INPUT operand on the logical (Constraints "$rd=$rd_in"),
    and a member Desc that lacks it desynchronizes from the MI it setDescs
    onto (machine verifier: "Explicit def tied to explicit use without tie
    constraint" + "Extra explicit operand", CB-152c). Returns
    logical-key -> tied aliases in Write_Port order. Purely derived — no
    hand list; X2MULA32 yields ('rtd1', 'rtd2'), F2MULAA32R_HHLL ('rtd',).
    LS POST/PRE/BREV base writeback is the OTHER tie family and stays with
    ls_has_tied_base_writeback (synthetic tied OUT, not a tied IN)."""
    idx = json.loads(index_path.read_text(encoding="utf-8"))
    ties: Dict[str, Tuple[str, ...]] = {}
    for type_recs in idx.values():
        for rec in type_recs:
            name = rec.get("Instruction")
            if not name:
                continue
            tied: List[str] = []
            for bank in ("GPR", "DR", "AR", "SFR"):
                writes = rec.get(f"{bank}_Write_Port") or []
                reads = set(rec.get(f"{bank}_Read_Port") or [])
                tied.extend(a for a in writes if a in reads)
            if not tied:
                continue
            key = _logical_key(name)
            if key in ties and ties[key] != tuple(tied):
                raise SystemExit(
                    f"error: conflicting accumulator ties for {key}: "
                    f"{ties[key]} vs {tuple(tied)}"
                )
            ties[key] = tuple(tied)
    return ties


def ls_has_tied_base_writeback(logical: str) -> bool:
    """POST/PRE/BREV update the encoded dest2 base (FieldSlot `$rs = $rs_wb`)."""
    key = _logical_key(logical)
    return any(tag in key for tag in ("_POST_", "_PRE_", "_BREV_"))


def ls_dest_is_ssa_def(logical: str, role: str) -> bool:
    """Whether an encoded LS dest* wire is an LLVM SSA def.

    Load dest1 is the loaded value. Encoded dest2 is always the base use
    (WITH and POST/PRE/BREV). Store dest* are stored value / base uses.
    POST/PRE/BREV writeback is a synthetic tied out (`dest2_wb`), not this
    wire — see ls_has_tied_base_writeback.
    """
    key = _logical_key(logical)
    if not role.startswith("dest"):
        return False
    if _is_store_logical(key) or not _is_load_logical(key):
        return False
    return role == "dest1"


def member_gpr_is_ssa_def(logical: str, role: str, is_ls: bool) -> bool:
    """Whether a generated member GPR/DR wire is an LLVM SSA def.

    ALU/MAC dest* are defs. LS dest* follow ls_dest_is_ssa_def. Catalog
    role `reg` (alias rt) is a def for JALR (link) and DEST_REG_LOGICALS
    (LUI/ZERO_GPR/ZERO_DR/CSRR/MOVESFR2GPR). Do not treat branch `reg`
    (rs), CSRW, or MOVEGPR2SFR as a def.
    """
    if role.startswith("dest"):
        return (not is_ls) or ls_dest_is_ssa_def(logical, role)
    if role != "reg":
        return False
    key = _logical_key(logical)
    return key in INDIRECT_CALL_LOGICALS or key in DEST_REG_LOGICALS


MAC_ACCFIRST_ITINERARY = {
    "MAC0": "Slot1_MAC_AccFirst",
    "MAC1": "Slot2_MAC_AccFirst",
}


def classify_member_flags(
    rec: MemberRecord, accum_ties: Optional[Dict[str, Tuple[str, ...]]] = None
) -> MemberEmitFlags:
    """Map unit/type/logical onto a published itinerary and closed flags.

    Itinerary comes from the already-published R5 class for `rec.unit`
    (SIN_COS/ARCTAN override via logical name). Load/store/branch flags
    come from golden unit + logical; hasSideEffects=1 only for CSR / WFI /
    hwloop / SFR / AR / CB types, control-transfer, or unknown.
    """
    key = _logical_key(rec.logical)
    if key in SINCOS_LOGICALS:
        itinerary = SINCOS_ITINERARY.get(rec.unit)
        if itinerary is None:
            raise SystemExit(
                f"error: {rec.member_symbol}: {key} unit {rec.unit} has no "
                "published SinCosLat itinerary"
            )
    elif (
        accum_ties is not None
        and len(accum_ties.get(key, ())) == 1
        and rec.unit in MAC_ACCFIRST_ITINERARY
    ):
        # Single-tie MAC member of a FmtALU64Acc logical: keep the
        # accumulator-read-late OperandCycles the logical's
        # Slot12_MAC_AccFirst carries, restricted to the committed unit —
        # the tied acc operand this member now declares would otherwise be
        # read at the wb-shape early cycle and stall golden RecMII=1
        # acc->acc chains. Dual-tie (FmtMAC2Dest) logicals publish the wb
        # shape themselves and stay on the unit default.
        itinerary = MAC_ACCFIRST_ITINERARY[rec.unit]
    else:
        itinerary = UNIT_ITINERARY.get(rec.unit)
        if itinerary is None:
            raise SystemExit(
                f"error: {rec.member_symbol}: unit {rec.unit} has no "
                "published itinerary class"
            )

    is_load = rec.unit in LS_UNITS and _is_load_logical(key)
    is_store = rec.unit in LS_UNITS and _is_store_logical(key)
    if is_load and is_store:
        raise SystemExit(
            f"error: {rec.member_symbol}: logical {key} classified as "
            "both load and store"
        )

    is_branch = key in BRANCH_LOGICALS
    is_indirect = key in INDIRECT_CALL_LOGICALS
    is_call = key in CALL_LOGICALS or is_indirect
    is_terminator = is_branch or is_indirect

    side = False
    if rec.type_name in SIDE_EFFECT_TYPES:
        side = True
    elif key in ("CSRR", "CSRW", "ZERO_SFR"):
        side = True
    elif key.startswith("WFI") or key.startswith("SET_HWLOOP"):
        side = True
    elif key in ("MOVEGPR2SFR", "MOVESFR2GPR"):
        side = True
    elif is_branch or is_call or is_indirect:
        # Match residual `_S*` CFG format classes (HaydnFormatsALU32.td
        # HaydnFU_ALU32_S0_RI12/I12_ONE/I20 hasSideEffects=1).
        side = True
    elif rec.unit in LS_UNITS and not is_load and not is_store:
        side = True

    return MemberEmitFlags(
        itinerary=itinerary,
        may_load=1 if is_load else 0,
        may_store=1 if is_store else 0,
        is_branch=1 if is_branch else 0,
        is_terminator=1 if is_terminator else 0,
        is_call=1 if is_call else 0,
        is_indirect_branch=1 if is_indirect else 0,
        has_side_effects=1 if side else 0,
    )


def check_emitted_member_itineraries(text: str) -> None:
    """Every E2/E3 member def must sit under a per-def Itinerary= let."""
    if BLANKET_HAS_SIDE_EFFECTS_LET in text:
        raise SystemExit(
            "error: blanket hasSideEffects=1 wrapper still present in members td"
        )
    lines = text.splitlines()
    missing: List[str] = []
    seen = 0
    for i, line in enumerate(lines):
        m = MEMBER_E23_DEF_RE.match(line)
        if not m:
            continue
        seen += 1
        window = "\n".join(lines[max(0, i - 4) : i])
        if "Itinerary =" not in window:
            missing.append(m.group(1))
        elif 'AsmVariantName = "e96member"' not in window:
            missing.append(m.group(1) + "(AsmVariantName)")
    if seen == 0:
        raise SystemExit("error: no E2/E3 member defs in emitted members td")
    if missing:
        sample = ", ".join(missing[:8])
        raise SystemExit(
            f"error: {len(missing)} members lack Itinerary (e.g. {sample})"
        )
    if "mayLoad = 1" not in text:
        raise SystemExit("error: no mayLoad=1 member emitted")
    if "mayStore = 1" not in text:
        raise SystemExit("error: no mayStore=1 member emitted")
    if "isBranch = 1" not in text:
        raise SystemExit("error: no isBranch=1 member emitted")
    if "Slot1_ALU_SinCosLat" not in text or "Slot2_ALU_SinCosLat" not in text:
        raise SystemExit("error: SIN_COS/ARCTAN SinCosLat itinerary missing")
    if "def ADD32_E2_E0_ALU0_RR : HaydnEntryE2E0<(outs GPR32:$dest_0)," not in text:
        raise SystemExit("error: ADD32 member dest must be an SSA out")
    if "def X2MUL32_E2_E0_MAC0_RRR : HaydnEntryE2E0<(outs DR64:$dest1_0, DR64:$dest2_3)," not in text:
        raise SystemExit("error: dual-dest MAC member dest1/dest2 must be SSA outs")
    if "def S_SW_WITH_IMM_E2_E0_LOADSTORE0_RI6 : HaydnEntryE2E0<(outs)," not in text:
        raise SystemExit(
            "error: S_SW_WITH_IMM dest1/dest2 are wire uses, not SSA outs"
        )
    if (
        'def S_LW_WITH_IMM_E2_E0_LOADSTORE0_RI6 : HaydnEntryE2E0<(outs GPR32:$dest1_0), '
        '(ins GPR32:$dest2_1, simm6:$imm_2), "ld32\\t$dest1_0, $dest2_1, $imm_2"'
        not in text
    ):
        raise SystemExit(
            "error: S_LW_WITH_IMM dest1 must be SSA out and AsmString ld32"
        )
    if 'def MUL64_LL_E2_E0_MAC0_RR : HaydnEntryE2E0<(outs DR64:$dest_0), (ins DR64:$src1_1, DR64:$src2_2), "mul64.ll\\t$dest_0, $src1_1, $src2_2"' not in text:
        raise SystemExit(
            "error: MAC member AsmString must use dotted user mnemonic mul64.ll"
        )
    if (
        'def JAL_E2_E0_ALU0_I20 : HaydnEntryE2E0<(outs GPR32:$dest_0), '
        '(ins brtarget_wide_i20:$imm_1), "jal\\t$dest_0, $imm_1"'
        not in text
    ):
        raise SystemExit(
            "error: JAL member imm must be brtarget_wide_i20 (WIDE PCRel)"
        )
    if (
        'def BEQZ_E2_E0_ALU0_I12 : HaydnEntryE2E0<(outs), '
        '(ins GPR32:$reg_0, brtarget_wide_i12:$imm_1), "beqz\\t$reg_0, $imm_1"'
        not in text
    ):
        raise SystemExit(
            "error: BEQZ member imm must be brtarget_wide_i12 (WIDE PCRel)"
        )
    if (
        'def JALR_E2_E0_ALU0_RI12 : HaydnEntryE2E0<(outs GPR32:$reg_0), '
        '(ins GPR32:$src_1, calltarget_wide_ri12:$imm_2), '
        '"jalr\\t$reg_0, $src_1, $imm_2"'
        not in text
    ):
        raise SystemExit(
            "error: JALR link (catalog role reg/rt) must be an SSA out"
        )
    if (
        'def LUI_E2_E0_ALU0_I12 : HaydnEntryE2E0<(outs GPR32:$reg_0), '
        '(ins uimm12:$imm_1), "lui\\t$reg_0, $imm_1"'
        not in text
    ):
        raise SystemExit(
            "error: LUI catalog role reg/rt must be an SSA out"
        )
    if (
        'def ZERO_GPR_E2_E0_ALU0_I8 : HaydnEntryE2E0<(outs GPR32:$reg_0), '
        '(ins), "zero_gpr\\t$reg_0"'
        not in text
    ):
        raise SystemExit(
            "error: ZERO_GPR catalog role reg/rt must be an SSA out"
        )
    if (
        'def ADDI32_E2_E0_ALU0_RI20 : HaydnEntryE2E0<(outs GPR32:$dest_0), '
        '(ins GPR32:$src_1, simm20_wide_abs:$imm_2), "addi32\\t$dest_0, $src_1, $imm_2"'
        not in text
    ):
        raise SystemExit(
            "error: ADDI32 member imm must be simm20_wide_abs (WIDE LO20)"
        )
    if (
        'def ORI32_E2_E0_ALU0_RI20 : HaydnEntryE2E0<(outs GPR32:$dest_0), '
        '(ins GPR32:$src_1, uimm20_wide_abs:$imm_2), "ori32\\t$dest_0, $src_1, $imm_2"'
        not in text
    ):
        raise SystemExit(
            "error: ORI32 member imm must be uimm20_wide_abs (WIDE LO20)"
        )
    if (
        'def SET_HWLOOP_F2_E2_E0_ALU0_HWLRIIR : HaydnEntryE2E0<(outs), '
        '(ins uimm1:$hwlr_sel_0, uimm6:$imm1_1, uimm12:$imm2_2, GPR32:$src_3), '
        '"set_hwloop_f2\\t$hwlr_sel_0, $imm1_1, $imm2_2, $src_3"'
        not in text
    ):
        raise SystemExit(
            "error: SET_HWLOOP_F2 Off1/Off2 stay uimm6/uimm12 "
            "(getExprFixupKind maps HWLoopOff by OpNo)"
        )
    if (
        "def S_LW_POST_IMM_E2_E0_LOADSTORE0_RI6 : HaydnEntryE2E0<"
        "(outs GPR32:$dest1_0, GPR32:$dest2_wb), "
        "(ins GPR32:$dest2_1, simm6:$imm_2), "
        '"s_lw_post_imm\\t$dest1_0, $dest2_1, $imm_2"'
        not in text
    ):
        raise SystemExit(
            "error: S_LW_POST_IMM dest2 must be a tied base use "
            "(outs dest1, dest2_wb) (ins dest2, imm)"
        )
    if (
        "def S_SW_POST_IMM_E2_E0_LOADSTORE0_RI6 : HaydnEntryE2E0<"
        "(outs GPR32:$dest2_wb), "
        "(ins GPR32:$dest1_0, GPR32:$dest2_1, simm6:$imm_2), "
        '"s_sw_post_imm\\t$dest1_0, $dest2_1, $imm_2"'
        not in text
    ):
        raise SystemExit(
            "error: S_SW_POST_IMM dest2 must be a tied base use "
            "(outs dest2_wb) (ins dest1, dest2, imm)"
        )
    if (
        "def S_LW_POST_REG_E2_E0_LOADSTORE0_RR : HaydnEntryE2E0<"
        "(outs GPR32:$dest1_0, GPR32:$dest2_wb), "
        "(ins GPR32:$dest2_1, GPR32:$src_2), "
        '"s_lw_post_reg\\t$dest1_0, $dest2_1, $src_2"'
        not in text
    ):
        raise SystemExit(
            "error: S_LW_POST_REG dest2 must be a tied base use "
            "(outs dest1, dest2_wb) (ins dest2, src)"
        )
    pins = (
        (
            "D_LW_POST_IMM_E2_E0_LOADSTORE0_RI6",
            (
                "Itinerary = Slot0_LS",
                "mayLoad = 1",
                "mayStore = 0",
                'Constraints = "$dest2_1 = $dest2_wb"',
            ),
        ),
        (
            "S_SW_POST_IMM_E2_E0_LOADSTORE0_RI6",
            (
                "Itinerary = Slot0_LS",
                "mayStore = 1",
                "mayLoad = 0",
                'Constraints = "$dest2_1 = $dest2_wb"',
            ),
        ),
        (
            "BEQ_E2_E0_ALU0_RI12",
            ("Itinerary = Slot0_ALU", "isBranch = 1", "isTerminator = 1"),
        ),
        (
            "SIN_COS_E3_E1_ALU1_RI4",
            ("Itinerary = Slot1_ALU_SinCosLat",),
        ),
    )
    found_pins = set()
    for i, line in enumerate(lines):
        m = MEMBER_E23_DEF_RE.match(line)
        if not m:
            continue
        for name, needles in pins:
            if m.group(1) != name:
                continue
            found_pins.add(name)
            window = "\n".join(lines[max(0, i - 4) : i])
            for needle in needles:
                if needle not in window:
                    raise SystemExit(
                        f"error: {name} preceding let missing {needle!r}"
                    )
    missing_pins = [name for name, _ in pins if name not in found_pins]
    if missing_pins:
        raise SystemExit(f"error: pinned members not emitted: {missing_pins}")


def emit_members_td_inc(
    cat: Catalog, accum_ties: Optional[Dict[str, Tuple[str, ...]]] = None
) -> str:
    """LIVE TableGen format-member Inst defs — included by HaydnFormatsE96.td.

    Each member is wrapped in a per-def `let Itinerary=..., mayLoad=..., ...`
    like AIE generated members (AIE2PSGenInstrInfo.td:12-13 default flags,
    :550 mayLoad on LDA). No file-wide hasSideEffects=1.
    """
    layouts = {l.layout_id: l for l in cat.layouts}

    # Canonical operand-alias order per logical = the majority signature's
    # alias sequence across its placements. Encode binds logical MC operands
    # to member Desc operands bag-by-class in Desc order, so a member whose
    # SAME-CLASS aliases permute against the canonical order would encode
    # swapped sources (self-consistent with the decoder, wrong against the
    # golden mapping — only the simulator would see it). Such members get
    # their (ins)/asm emitted in canonical alias order; bit placement below
    # is by field name and does not move. Known limit: a family consistently
    # permuted against its LOGICAL's operand order has a self-consistent
    # majority and is invisible here (the AR-ua residual, CB-151).
    def alias_class(alias: str) -> str:
        a = alias.lower()
        if a.startswith(("rtd", "rsd")):
            return "DR"
        if a.startswith("ar"):
            return "AR"
        if a.startswith(("rs", "rt", "rd")):
            return "GPR"
        return "IMM"

    def member_alias_seq(rec: "MemberRecord") -> List[str]:
        lay = layouts[rec.layout_id]
        active = {role: (alias or "").strip() for role, alias in rec.operand_active}
        return [active[of.role] for of in lay.operand_fields if active.get(of.role)]

    canon_alias_order: Dict[str, List[str]] = {}
    for logical, mids in cat.alternatives.items():
        seqs = Counter(tuple(member_alias_seq(cat.members[mid])) for mid in mids)
        canon_alias_order[logical] = list(seqs.most_common(1)[0][0])

    def same_class_permuted(aliases: List[str], canon: List[str]) -> bool:
        if sorted(aliases) != sorted(canon) or aliases == canon:
            return False
        for cls in ("DR", "GPR", "AR", "IMM"):
            a = [x for x in aliases if alias_class(x) == cls]
            c = [x for x in canon if alias_class(x) == cls]
            if a != c:
                return True
        return False

    canonicalized: List[str] = []

    accum_tied_members: List[str] = []
    lines: List[str] = []
    lines.append("//===-- HaydnFormatsE96Members.td.inc - LIVE E96 members -*-===//")
    lines.append("// Auto-generated by FormatE/generate_format_e_records.py")
    lines.append("// DO NOT EDIT.")
    lines.append("")
    count = 0
    for rec in cat.members:
        if rec.is_nop:
            continue
        lay = layouts[rec.layout_id]
        base = entry_base_class(rec.mode, rec.entry_idx)
        inst_f = entry_inst_field(rec.mode, rec.entry_idx)
        entry_lo = lay.entry_bits.lo
        entry_w = entry_bit_width(rec.mode, rec.entry_idx)
        op_frags: List[str] = []
        bit_names: List[Tuple[str, int, int, int, bool]] = []
        active = {role: (alias or "").strip() for role, alias in rec.operand_active}
        for i, of in enumerate(lay.operand_fields):
            rh, rl = abs_to_rel(of.bits.hi, of.bits.lo, entry_lo)
            if rl < 0 or rh >= entry_w:
                raise SystemExit(f"field OOB {rec.member_symbol} {of.role}")
            alias = active.get(of.role, "")
            if not alias:
                bit_names.append(("", rh, rl, of.bits.width, False))
                continue
            name, frag, _bw = field_operand_td(of, i, alias, rec.logical)
            op_frags.append(frag)
            bit_names.append((name, rh, rl, of.bits.width, True))

        # CSRW: catalog print order IMM, GPR
        if rec.logical.upper() == "CSRW":
            vars_idx = [i for i, x in enumerate(bit_names) if x[4]]
            imm_i = [i for i in vars_idx if "imm" in bit_names[i][0].lower()]
            reg_i = [i for i in vars_idx if i not in set(imm_i)]
            if imm_i and reg_i:
                order = imm_i + reg_i
                op_frags = []
                asm_names = []
                for i in order:
                    n, _rh, _rl, w, _v = bit_names[i]
                    asm_names.append(n)
                    if "imm" in n.lower():
                        op_frags.append(f"uimm8:${n}" if w == 8 else f"i32imm:${n}")
                    else:
                        op_frags.append(f"GPR32:${n}")
                asm_ops = ", ".join(f"${n}" for n in asm_names)
            else:
                asm_ops = ", ".join(f"${n}" for n, _, _, _, v in bit_names if v)
        else:
            # Dual-dest MAC RRR: print catalog/ISA order
            # rtd1, rtd2, rsd1, rsd2 (not wire field order dest1,src1,src2,dest2).
            names_active = [(n, i) for i, (n, _, _, _, v) in enumerate(bit_names) if v]
            by = {n.split("_")[0]: (n, i) for n, i in names_active}
            dual_order = ["dest1", "dest2", "src1", "src2"]
            if all(k in by for k in dual_order) and len(names_active) == 4:
                op_frags = []
                asm_names = []
                for role in dual_order:
                    n, i = by[role]
                    asm_names.append(n)
                    # Recover class from existing frag list by role index in bit_names
                    # Field was classified as DR for these roles.
                    op_frags.append(f"DR64:${n}")
                asm_ops = ", ".join(f"${n}" for n in asm_names)
            else:
                aliases = member_alias_seq(rec)
                canon = canon_alias_order.get(rec.logical, aliases)
                active_names = [n for n, _, _, _, v in bit_names if v]
                if same_class_permuted(aliases, canon):
                    # Emit (ins)/asm in canonical alias order so the
                    # bag-by-class binding lands each logical operand on its
                    # golden field. Bit placement is by name and unchanged.
                    by_alias = dict(zip(aliases, zip(active_names, op_frags)))
                    ordered = [by_alias[a] for a in canon]
                    op_frags = [f for _, f in ordered]
                    asm_ops = ", ".join(f"${n}" for n, _ in ordered)
                    canonicalized.append(rec.member_symbol)
                else:
                    asm_ops = ", ".join(f"${n}" for n in active_names)

        asm = assembler_mnemonic(rec.logical, rec.unit)
        if asm_ops:
            asm = asm + "\\t" + asm_ops

        # Dest roles: ALU/MAC dest* are SSA defs. LS layouts reuse dest1/dest2
        # for data and base: load dest1 is a def, encoded dest2 is the base
        # use, store dest* are uses. Catalog role `reg` (rt) is a def for
        # JALR link and DEST_REG_LOGICALS. POST/PRE/BREV add a synthetic
        # tied GPR writeback (`dest2_wb`) so NumDefs/NumOperands match
        # FieldSlot `$rs = $rs_wb`.
        is_ls = rec.unit in LS_UNITS
        out_frags: List[str] = []
        in_frags: List[str] = []
        for frag in op_frags:
            dollar = frag.find("$")
            name = frag[dollar + 1 :] if dollar >= 0 else ""
            role = name.split("_")[0].lower() if name else ""
            if member_gpr_is_ssa_def(rec.logical, role, is_ls):
                out_frags.append(frag)
            else:
                in_frags.append(frag)
        constraint_parts: List[str] = []
        # Accumulator law (CB-152c): the logical carries tied accumulator
        # INPUT operands (Constraints "$rd = $rd_in"); the member Desc must
        # mirror them or setDesc leaves the MI with more explicit operands
        # than the Desc declares and unmarked ties. Synthetic acc ins are
        # PREPENDED in dest order (the logical lists rd_in/ra before rs*),
        # carry no encoded bits (tied: the dest field is the wire), and stay
        # out of the AsmString exactly like the logical's rd_in.
        accum = (accum_ties or {}).get(_logical_key(rec.logical))
        # LS units are excluded from the accumulator path wholesale: their
        # golden ties are base/AR writebacks, not accumulators — POST/PRE/
        # BREV already mirror via the synthetic dest2_wb OUT above, and the
        # CB / *WUA_POST (AR-ua) tie families are the CB-151 encode-residual
        # domain whose member shapes need the owner's redesign, not a
        # bolted-on tie (their tied aliases are member INS or unencoded
        # ar[ar_sel], so this path could not express them anyway).
        if accum and not is_ls:
            acc_ins: List[str] = []
            for alias in accum:
                matches = []
                for frag in out_frags:
                    fname = frag[frag.find("$") + 1 :]
                    frole = fname.split("_")[0].lower()
                    if active.get(frole, "") == alias:
                        matches.append(frag)
                if len(matches) != 1:
                    raise SystemExit(
                        f"error: {rec.member_symbol} accumulator alias "
                        f"{alias!r} matched {len(matches)} dest operands"
                    )
                dfrag = matches[0]
                dname = dfrag[dfrag.find("$") + 1 :]
                dcls = dfrag.split(":")[0]
                aname = f"{dname}_acc"
                if any(
                    f.endswith("$" + aname) for f in out_frags + in_frags
                ):
                    raise SystemExit(
                        f"error: {rec.member_symbol} {aname} name collides"
                    )
                acc_ins.append(f"{dcls}:${aname}")
                constraint_parts.append(f"${dname} = ${aname}")
            in_frags = acc_ins + in_frags
            accum_tied_members.append(rec.member_symbol)
        constraints: Optional[str] = None
        if is_ls and ls_has_tied_base_writeback(rec.logical):
            dest2_ins = []
            for frag in in_frags:
                dollar = frag.find("$")
                name = frag[dollar + 1 :] if dollar >= 0 else ""
                if name.split("_")[0].lower() == "dest2":
                    dest2_ins.append(frag)
            if len(dest2_ins) != 1:
                raise SystemExit(
                    f"error: {rec.member_symbol} POST/PRE/BREV needs exactly "
                    f"one dest2 use, got {len(dest2_ins)}"
                )
            dest2_frag = dest2_ins[0]
            dest2_name = dest2_frag[dest2_frag.find("$") + 1 :]
            dest2_cls = dest2_frag.split(":")[0]
            wb_name = "dest2_wb"
            if any(
                frag.endswith("$" + wb_name) for frag in out_frags + in_frags
            ):
                raise SystemExit(
                    f"error: {rec.member_symbol} dest2_wb name collides"
                )
            out_frags.append(f"{dest2_cls}:${wb_name}")
            constraint_parts.append(f"${dest2_name} = ${wb_name}")
        if constraint_parts:
            constraints = ", ".join(constraint_parts)
        outs = ", ".join(out_frags)
        ins = ", ".join(in_frags)
        outs_dag = f"(outs {outs})" if outs else "(outs)"
        ins_dag = f"(ins {ins})" if ins else "(ins)"

        flags = classify_member_flags(rec, accum_ties)
        let = flags.let_line()
        if constraints:
            if not let.endswith(" in {"):
                raise SystemExit(
                    f"error: {rec.member_symbol} let_line missing ' in {{'"
                )
            let = let[:-5] + f', Constraints = "{constraints}" in {{'
        lines.append(let)
        lines.append(
            f'def {rec.member_symbol} : {base}<'
            f'{outs_dag}, {ins_dag}, "{asm}", []> {{'
        )
        for name, rh, rl, w, is_var in bit_names:
            if is_var:
                lines.append(f"  bits<{w}> {name};")
        emit_entry_bits_assign(lines, rec, lay, bit_names, entry_w, entry_lo, inst_f)
        lines.append("}")
        lines.append("}")
        lines.append("")
        count += 1
    # Fail-closed pin: the canonicalized set is measured, not assumed. A DB
    # regen that changes it must be re-audited against the encode bag binding
    # before this pin moves.
    if canonicalized != ["X4SEL16_E3_E1_ALU1_RRR"]:
        raise SystemExit(
            "error: canonicalized member set changed: "
            f"{canonicalized} != ['X4SEL16_E3_E1_ALU1_RRR'] — re-audit "
            "the encode bag binding before repinning"
        )
    # Accumulator-tie pins: measured, not assumed. The tie set derives from
    # golden Write∩Read ports; a DB regen that changes the member count must
    # be re-audited (operand order vs the logical Constraints) before the
    # count moves. Representative shapes are pinned as text below.
    if accum_ties is not None:
        if "F2MULAA32R_HHLL_E3_E2_MAC1_RR" not in accum_tied_members:
            raise SystemExit(
                "error: F2MULAA32R_HHLL_E3_E2_MAC1_RR did not receive its "
                "accumulator tie"
            )
        text_so_far = "\n".join(lines)
        pin_f2 = (
            "def F2MULAA32R_HHLL_E3_E2_MAC1_RR : HaydnEntryE3E2<"
            "(outs DR64:$dest_0), "
            "(ins DR64:$dest_0_acc, DR64:$src1_1, DR64:$src2_2), "
            '"f2mulaa32r.hhll\\t$dest_0, $src1_1, $src2_2"'
        )
        if pin_f2 not in text_so_far:
            raise SystemExit(
                "error: F2MULAA32R member must carry the tied accumulator "
                "input first in (ins), unprinted (mirrors logical rd_in)"
            )
        if 'Constraints = "$dest_0 = $dest_0_acc"' not in text_so_far:
            raise SystemExit(
                "error: F2MULAA32R member missing accumulator Constraints"
            )
        pin_x2 = (
            "def X2MULA32_E2_E0_MAC0_RRR : HaydnEntryE2E0<"
            "(outs DR64:$dest1_0, DR64:$dest2_3), "
            "(ins DR64:$dest1_0_acc, DR64:$dest2_3_acc, "
            "DR64:$src1_1, DR64:$src2_2)"
        )
        if pin_x2 not in text_so_far:
            raise SystemExit(
                "error: X2MULA32 member must carry BOTH tied accumulator "
                "inputs in dest order before the sources"
            )
        if (
            'Constraints = "$dest1_0 = $dest1_0_acc, $dest2_3 = $dest2_3_acc"'
            not in text_so_far
        ):
            raise SystemExit(
                "error: X2MULA32 member missing dual accumulator Constraints"
            )
    lines.append(
        "// Members emitted in canonical alias order (same-class permutation "
        f"vs majority signature): {len(canonicalized)}"
    )
    lines.append(f"// Live non-NOP Format E members: {count}")
    lines.append("")
    text = "\n".join(lines) + "\n"
    check_emitted_member_itineraries(text)
    return text


LOGICAL_MATERIALIZE_RE = re.compile(
    r"def\s*:\s*LogicalMaterialize<\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*\[([^\]]+)\]\s*>",
    re.MULTILINE,
)
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def parse_logical_materialize(path: Path) -> List[Tuple[str, str]]:
    """Parse residual `_S*` member → logical pairs from LogicalMaterialize.

    AIE peer: CodeGenFormat.cpp:146-163 emits AlternateInsts[] plus
    getAlternateInstsOpcode switch (logical → members). This is the inverse
    overlay for Haydn residual slot members still defined in TableGen.
    """
    text = path.read_text(encoding="utf-8")
    pairs: List[Tuple[str, str]] = []
    for m in LOGICAL_MATERIALIZE_RE.finditer(text):
        logical = m.group(1)
        for member in IDENT_RE.findall(m.group(2)):
            if member == logical:
                continue
            pairs.append((member, logical))
    # Empty is the R13 end state: every residual FieldSlot retired, so
    # member→logical comes only from generated Format E members.
    return pairs


TD_DEF_RE = re.compile(r"^def\s+([A-Za-z_][A-Za-z0-9_]*)\s*:", re.MULTILINE)


def collect_td_def_names(out_dir: Path) -> set:
    """TableGen `def NAME :` identifiers in the Haydn target dir."""
    names = set()
    for path in sorted(out_dir.glob("*.td")) + sorted(out_dir.glob("*.td.inc")):
        names.update(TD_DEF_RE.findall(path.read_text(encoding="utf-8")))
    if not names:
        raise SystemExit(f"no TableGen defs in {out_dir}")
    return names


def collect_member_to_logical(cat: Catalog, td_path: Path) -> Dict[str, str]:
    """Dense member-opcode-name → logical-opcode-name map.

    Sources: LogicalMaterialize residual `_S*` plus Format E member_symbol.
    Format E members whose catalog logical is not a TableGen opcode are
    omitted (fail-closed: lookup returns 0, never the member itself).
    Fail-closed on a member claiming two logicals.
    """
    mapping: Dict[str, str] = {}
    known = collect_td_def_names(td_path.parent)

    def add(member: str, logical: str) -> None:
        if not member or not logical or member == logical:
            return
        if not IDENT_RE.fullmatch(member) or not IDENT_RE.fullmatch(logical):
            raise SystemExit(f"non-ident member→logical {member!r} → {logical!r}")
        # Retired FieldSlots must not emit Haydn:: cases.
        if member not in known:
            return
        prev = mapping.get(member)
        if prev is not None and prev != logical:
            raise SystemExit(
                f"member→logical conflict {member}: {prev} vs {logical}"
            )
        mapping[member] = logical

    if not td_path.is_file():
        raise SystemExit(f"LogicalMaterialize file not found: {td_path}")
    mat_pairs = parse_logical_materialize(td_path)
    for member, logical in mat_pairs:
        add(member, logical)

    known_logicals = known | {logical for _, logical in mat_pairs}
    # Catalog names that are not themselves TableGen opcodes. Map members onto
    # the TableGen-real logical TII already switches on (no string peel).
    td_logical_aliases = {
        "SET_HWLOOP_F2": "SET_HWLOOP_F2_W",
        "WFITBDTBDTBD": "WFI",
    }
    skipped = 0
    for rec in cat.members:
        if rec.is_nop:
            continue
        logical = sanitize_ident(rec.logical)
        logical = td_logical_aliases.get(logical, logical)
        if logical not in known_logicals:
            skipped += 1
            continue
        add(rec.member_symbol, logical)

    if not mapping:
        raise SystemExit("empty member→logical map")
    print(
        f"member→logical pairs={len(mapping)} "
        f"format_e_skipped_no_logical_opcode={skipped}"
    )
    return mapping


def emit_member_opcodes_inc(cat: Catalog, member_to_logical: Dict[str, str]) -> str:
    lines: List[str] = []
    lines.append("//===-- HaydnGenFormatEMemberOpcodes.inc -*- C++ -*-===//")
    lines.append("// Auto-generated. DO NOT EDIT.")
    lines.append("#ifdef GET_FORMAT_E_MEMBER_OPCODES")
    lines.append("#undef GET_FORMAT_E_MEMBER_OPCODES")
    lines.append(f"static constexpr unsigned FormatEMemberOpcodeCount = {len(cat.members)}u;")
    lines.append("static constexpr unsigned FormatEMemberOpcodes[] = {")
    for rec in cat.members:
        if rec.is_nop:
            lines.append(f"  Haydn::NOP, // {rec.member_symbol}")
        else:
            lines.append(f"  Haydn::{rec.member_symbol},")
    lines.append("};")
    lines.append(
        "static_assert(sizeof(FormatEMemberOpcodes)/sizeof(FormatEMemberOpcodes[0])"
        " == FormatEMemberOpcodeCount, \"member opcode pin\");"
    )
    lines.append("#endif")
    lines.append("")

    # AIE peer: inverse of AIEMCFormats::getAlternateInstsOpcode
    # (AIEMCFormats.h:376-379; CodeGenFormat.cpp:155-163 generated switch).
    # Hexagon packet children keep the architectural opcode (HexagonInstrInfo.cpp:390-397
    # bundle walk); Haydn residual `_S*` / Format E members need this overlay.
    by_logical: Dict[str, List[str]] = defaultdict(list)
    for member, logical in member_to_logical.items():
        by_logical[logical].append(member)
    for members in by_logical.values():
        members.sort()

    lines.append("#ifdef GET_FORMAT_E_MEMBER_TO_LOGICAL")
    lines.append("#undef GET_FORMAT_E_MEMBER_TO_LOGICAL")
    lines.append(
        f"static constexpr unsigned FormatEMemberToLogicalCount = "
        f"{len(member_to_logical)}u;"
    )
    lines.append(
        "static_assert(FormatEMemberToLogicalCount > 0u, \"member to logical pin\");"
    )
    lines.append(
        "unsigned llvm::haydn::format_e::lookupGeneratedMemberToLogical("
        "unsigned Opcode) {"
    )
    lines.append("  switch (Opcode) {")
    lines.append("  default:")
    lines.append("    return 0u;")
    for logical in sorted(by_logical):
        for member in by_logical[logical]:
            lines.append(f"  case Haydn::{member}:")
        lines.append(f"    return Haydn::{logical};")
    lines.append("  }")
    lines.append("}")
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# MC mnemonic round-trip harness (one vector per product logical)
# Peer: llvm/test/MC/Hexagon/v67_all.s (one mnemonic × assemble+objdump).
# Operand print order matches emit_members_td_inc AsmString, not hypothesized
# HaydnInstrInfoAuto.td (those stay isCodeGenOnly / auto-hypothesized-unencodable.s).
# ---------------------------------------------------------------------------

BRANCH_TARGET_LOGICALS = frozenset({
    "BEQ", "BNE", "BGE", "BGEU", "BGEZ", "BLT", "BLTZ", "BLTU",
    "BEQZ", "BNEZ", "JAL", "JALR",
})


def logical_print_mnemonic(logical: str) -> str:
    """Objdump / coverage token: golden logical, lowercased (WFI<TBD> → wfi)."""
    name = (logical or "").strip()
    if name.upper().startswith("WFI"):
        return "wfi"
    return name.lower()


def assembler_mnemonic(logical: str, unit: str) -> str:
    """Matcher spelling. MAC lane suffixes use documented dotted aliases
    (HaydnFormatsMAC.td / llvm/test/MC/Haydn/mac-instructions.s). LS WITH
    forms print the user mnemonic (ld32 / st32), inverse of
    peelLogicalOpcodeName."""
    name = (logical or "").strip()
    if name.upper().startswith("WFI"):
        return "wfi"
    if name == "PLDWWUA_POST":
        return "pldwwua"
    user = LS_USER_MNEMONIC.get(name.upper())
    if user:
        return user
    mnem = name.lower()
    if (unit or "").startswith("MAC") and "_" in mnem:
        base, rest = mnem.split("_", 1)
        return base + "." + rest.replace("_", ".")
    return mnem


# Documented matcher packets that differ from Format E member AsmString
# operand count/order. Still the same logical mnemonic (or a known _w alias
# that objdump prints as the logical). No hypothesized Auto.td encodings.
_SPECIAL_PACKETS = {
    "SET_HWLOOP": "{ set_hwloop_w 0, 16, 32, 4; nop; nop }",
    "SET_HWLOOP_F2": "{ set_hwloop_f2_w 0, 16, 32, r1; nop; nop }",
    "SET_HWLOOP_REG": "{ set_hwloop_reg_w 0, r1, r2, r3; nop; nop }",
    "D_LQHWUA_POST": "{ d_lqhwua_post d0, 0, r1, r2, 0; nop; nop }",
    "D_LTWUA_POST": "{ d_ltwua_post d0, 0, r1, r2, 0; nop; nop }",
    "D_SQHWUA_POST": "{ d_sqhwua_post d0, 0, r1, r2, 0; nop; nop }",
    "D_STWUA_POST": "{ d_stwua_post d0, 0, r1, r2, 0; nop; nop }",
    "PLDWWUA_POST": "{ pldwwua 0, r1; nop; nop }",
    "WBARWUA": "{ wbarwua 0, r1, 0; nop; nop }",
    "MULL": "{ mull r1, r2, r1; nop; nop }",
}

# Product logicals whose matcher/placement cannot form a Format E parcel.
# Do not invent encoding. Coverage still pins the name via # MNEM:.
_UNENCODABLE_LOGICALS = frozenset({
    "WFI<TBD>",  # serialize-only WFI_S0 refuses a complete parcel
})


def mnemonic_roundtrip_path(out_dir: Path) -> Path:
    """llvm/test/MC/Haydn/format-e-mnemonic-roundtrip.s from Target/Haydn out-dir."""
    return out_dir.parents[2] / "test" / "MC" / "Haydn" / "format-e-mnemonic-roundtrip.s"


def _member_print_ops(rec: MemberRecord, lay: TypeLayout) -> List[Tuple[str, str, int]]:
    """Return print-order (role, classify_alias kind, width) for one member.

    Mirrors emit_members_td_inc: skip empty aliases; CSRW IMM then GPR;
    dual-dest MAC dest1, dest2, src1, src2.
    """
    active = {role: (alias or "").strip() for role, alias in rec.operand_active}
    ops: List[Tuple[str, str, int, str]] = []
    for i, of in enumerate(lay.operand_fields):
        alias = active.get(of.role, "")
        if not alias:
            continue
        name, _frag, _bw = field_operand_td(of, i, alias, rec.logical)
        kind = classify_alias(alias, of.role)
        ops.append((of.role, kind, of.bits.width, name))

    if rec.logical.upper() == "CSRW":
        imm = [o for o in ops if "imm" in o[0].lower() or o[1] == "IMM"]
        reg = [o for o in ops if o not in imm]
        if imm and reg:
            ops = imm + reg
        return [(r, k, w) for r, k, w, _n in ops]

    names_active = [(n, i) for i, (_r, _k, _w, n) in enumerate(ops)]
    by = {n.split("_")[0]: i for n, i in names_active}
    dual_order = ["dest1", "dest2", "src1", "src2"]
    if all(k in by for k in dual_order) and len(names_active) == 4:
        ops = [ops[by[k]] for k in dual_order]
    return [(r, k, w) for r, k, w, _n in ops]


def _fill_asm_token(
    kind: str,
    width: int,
    role: str,
    logical: str,
    is_last: bool,
    gpr_i: List[int],
    dr_i: List[int],
    first_dr: List[str],
) -> str:
    gprs = ("r1", "r2", "r3", "r4", "r5")
    drs = ("d0", "d1", "d2", "d3", "d4")
    if kind == "REG_DR":
        tok = drs[dr_i[0] % len(drs)]
        dr_i[0] += 1
        if first_dr[0] is None:
            first_dr[0] = tok
        return tok
    if kind == "REG_AR":
        return "ar0"
    if kind == "REG_GPR":
        tok = gprs[gpr_i[0] % len(gprs)]
        gpr_i[0] += 1
        return tok
    r = (role or "").lower()
    logu = (logical or "").strip().upper()
    if r in ("cbr_sel", "hwlr_sel", "ar_sel") or r.endswith("_sel"):
        return "0"
    if is_last and logu in BRANCH_TARGET_LOGICALS:
        # Offset 0: `.` is rejected by the matcher; a file-wide label
        # overflows simm12 and mis-decodes. Immediate 0 round-trips.
        return "0"
    if width <= 1:
        return "0"
    return "1"


def asm_packet_for_logical(cat: Catalog, logical: str) -> Optional[str]:
    """One complete `{ insn; nop; nop }` packet, or None if unencodable."""
    if logical in _UNENCODABLE_LOGICALS:
        return None
    if logical in _SPECIAL_PACKETS:
        return _SPECIAL_PACKETS[logical]
    mids = cat.alternatives[logical]
    rec = cat.members[mids[0]]
    lay = cat.layouts[rec.layout_id]
    ops = _member_print_ops(rec, lay)
    gpr_i = [0]
    dr_i = [0]
    first_dr: List[Optional[str]] = [None]
    # In-place DR+IMM: reuse dest for the first source DR.
    inplace_dr = (
        sum(1 for _r, k, _w in ops if k == "REG_DR") == 2
        and any(k == "IMM" for _r, k, _w in ops)
    )
    toks: List[str] = []
    for i, (role, kind, width) in enumerate(ops):
        if kind == "REG_DR" and inplace_dr and first_dr[0] is not None:
            toks.append(first_dr[0])
            continue
        toks.append(
            _fill_asm_token(
                kind,
                width,
                role,
                logical,
                i + 1 == len(ops),
                gpr_i,
                dr_i,
                first_dr,
            )
        )
    mnem = assembler_mnemonic(logical, rec.unit)
    if toks:
        insn = mnem + " " + ", ".join(toks)
    else:
        insn = mnem
    return "{ " + insn + "; nop; nop }"


def emit_mnemonic_roundtrip_s(cat: Catalog) -> str:
    """Committed MC harness: one packet per unique non-NOP logical."""
    logicals = list(cat.alternatives.keys())
    if len(logicals) != PIN_UNIQUE_NON_NOP:
        raise SystemExit(
            f"mnemonic harness logicals {len(logicals)} != {PIN_UNIQUE_NON_NOP}"
        )
    unenc = [l for l in logicals if l in _UNENCODABLE_LOGICALS]
    encodable_n = PIN_UNIQUE_NON_NOP - len(unenc)
    lines: List[str] = []
    lines.append(
        "# RUN: %python %S/../../../utils/haydn/check_mc_mnemonic_coverage.py"
    )
    lines.append(
        "# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | "
        "FileCheck %s --check-prefix=ENC"
    )
    lines.append(
        "# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && "
        "llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | "
        "FileCheck %s --check-prefix=DIS"
    )
    lines.append("# REQUIRES: haydn-registered-target")
    lines.append("#")
    lines.append(
        "# Auto-generated by FormatE/generate_format_e_records.py. DO NOT EDIT."
    )
    lines.append(
        "# One Format E packet per product non-NOP logical from the golden"
    )
    lines.append(
        "# member table. Packet form: { insn; nop; nop }. Bare nop is covered"
    )
    lines.append(
        "# in nop-format-e-not-all-zero.s. Hypothesized Auto.td encodings are"
    )
    lines.append(
        "# isCodeGenOnly and live in auto-hypothesized-unencodable.s, not here."
    )
    lines.append("#")
    lines.append(
        "# Role: object — assemble each product mnemonic, require a 12-byte"
    )
    lines.append(
        "# non-all-zero parcel, and require objdump to print the logical name."
    )
    lines.append(
        "# Peer: llvm/test/MC/Hexagon/v67_all.s (mnemonic × assemble+objdump)."
    )
    lines.append(
        f"# ENC-COUNT-{encodable_n}: encoding: ["
    )
    lines.append(
        "# ENC-NOT: encoding: "
        "[0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]"
    )
    lines.append("")
    lines.append(".text")
    lines.append("")
    for logical in logicals:
        unit = ""
        mids = cat.alternatives.get(logical) or []
        if mids:
            unit = cat.members[mids[0]].unit
        cov_m = logical_print_mnemonic(logical)
        print_m = assembler_mnemonic(logical, unit)
        label = "rt_" + re.sub(r"[^A-Za-z0-9_]", "_", print_m)
        packet = asm_packet_for_logical(cat, logical)
        lines.append(f"# MNEM: {cov_m}")
        if packet is None:
            lines.append(
                f"# UNENCODABLE: {logical} ({cov_m}) — no invented encoding"
            )
            lines.append("")
            continue
        lines.append(f"{label}:")
        lines.append(packet)
        lines.append(f"# DIS-LABEL: <{label}>:")
        lines.append("# DIS: {{[ \\t]}}" + print_m + "{{[ \\t,;}]}}")
        lines.append("")
    if unenc:
        lines.append("# UNENCODABLE product logicals (real matcher/placement gap):")
        for logical in unenc:
            lines.append(f"#   {logical} -> {logical_print_mnemonic(logical)}")
        lines.append("")
    return "\n".join(lines) + "\n"


def emit_composite_scaffold_fragment() -> str:
    """Text block merged into HaydnCompositeFormats.td (manual section)."""
    return ""  # CompositeFormats is updated separately as a stable hand edit.


def write_if_changed(path: Path, content: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return True


# ---------------------------------------------------------------------------
# Golden discovery + XLSX parse + canonical-vector round-trip (T-TII2)
# ---------------------------------------------------------------------------


def resolve_golden_dir() -> Path:
    """HAYDN_GOLDEN_DIR / BUNDLESIM_GOLDEN_DIR, else the host plans-tree default."""
    for key in ("HAYDN_GOLDEN_DIR", "BUNDLESIM_GOLDEN_DIR"):
        raw = os.environ.get(key)
        if not raw:
            continue
        p = Path(raw)
        if (p / "format_e_bit_layout_v2.json").is_file():
            return p
        nested = p / "golden"
        if (nested / "format_e_bit_layout_v2.json").is_file():
            return nested
    if DEFAULT_GOLDEN_DIR.is_dir():
        return DEFAULT_GOLDEN_DIR
    # Discovery, not trust: content is pinned by hash, so falling back to the
    # user database copy cannot change what generation accepts.
    home_db = Path.home() / "haydn"
    if (home_db / "format_e_bit_layout_v2.json").is_file():
        return home_db
    return DEFAULT_GOLDEN_DIR


def deposit_bits(bits: int, val: int, lo: int, width: int) -> int:
    mask = (1 << width) - 1
    return (bits & ~(mask << lo)) | ((val & mask) << lo)


def extract_bits(bits: int, lo: int, width: int) -> int:
    return (bits >> lo) & ((1 << width) - 1)


def le_hex_to_bits(hex_str: str) -> int:
    raw = bytes.fromhex(hex_str)
    bits = 0
    for i, byte in enumerate(raw):
        bits |= byte << (8 * i)
    return bits


def bits_to_le_hex(bits: int, nbytes: int) -> str:
    return bytes((bits >> (8 * i)) & 0xFF for i in range(nbytes)).hex()


def pack_member_bits(rec: MemberRecord, lay: TypeLayout) -> int:
    """Pack header + map + type_code + opcode; operands stay zero (placement RT)."""
    bits = 0x7  # format_indicator[2:0]
    if rec.mode == "E3":
        bits |= 1 << 3
    bits = deposit_bits(bits, rec.unit_map, lay.map_bits.lo, lay.map_bits.width)
    bits = deposit_bits(
        bits, rec.type_code, lay.type_code_bits.lo, lay.type_code_bits.width
    )
    bits = deposit_bits(
        bits, rec.opcode, lay.opcode_bits.lo, lay.opcode_bits.width
    )
    return bits


def roundtrip_members(cat: Catalog) -> None:
    """Every generated member encodes then inverse-decodes to the same MemberId."""
    for rec in cat.members:
        lay = cat.layouts[rec.layout_id]
        bits = pack_member_bits(rec, lay)
        mode = "E3" if extract_bits(bits, 3, 1) else "E2"
        umap = extract_bits(bits, lay.map_bits.lo, lay.map_bits.width)
        tcode = extract_bits(
            bits, lay.type_code_bits.lo, lay.type_code_bits.width
        )
        opc = extract_bits(bits, lay.opcode_bits.lo, lay.opcode_bits.width)
        key = (mode, rec.entry_idx, rec.unit, rec.type_name, opc)
        mid = cat.inverse.get(key)
        if (
            mid != rec.member_id
            or umap != rec.unit_map
            or tcode != rec.type_code
            or mode != rec.mode
        ):
            raise SystemExit(
                f"member round-trip miss {rec.member_symbol} "
                f"mid={mid} want={rec.member_id} map={umap}/{rec.unit_map} "
                f"tcode={tcode}/{rec.type_code} mode={mode}/{rec.mode}"
            )
        hex12 = bits_to_le_hex(bits, 12)
        if le_hex_to_bits(hex12) != bits:
            raise SystemExit(
                f"member LE hex round-trip miss {rec.member_symbol}"
            )


def _xlsx_shared_strings(zf: zipfile.ZipFile) -> List[str]:
    root = ET.fromstring(zf.read("xl/sharedStrings.xml"))
    out: List[str] = []
    for si in root.findall("m:si", SSML_NS):
        out.append("".join(t.text or "" for t in si.findall(".//m:t", SSML_NS)))
    return out


def _xlsx_sheet_cells(
    zf: zipfile.ZipFile, sheet: str, strings: List[str]
) -> Dict[int, Dict[str, str]]:
    root = ET.fromstring(zf.read(sheet))
    rows: Dict[int, Dict[str, str]] = {}
    for cell in root.findall(".//m:c", SSML_NS):
        ref = cell.get("r")
        if not ref:
            continue
        m = re.match(r"([A-Z]+)(\d+)", ref)
        if not m:
            continue
        kind = cell.get("t")
        val_el = cell.find("m:v", SSML_NS)
        if val_el is None or val_el.text is None:
            val = ""
        elif kind == "s":
            val = strings[int(val_el.text)]
        else:
            val = val_el.text
        rows.setdefault(int(m.group(2)), {})[m.group(1)] = val
    return rows


def _xlsx_col_a(rows: Dict[int, Dict[str, str]]) -> List[str]:
    return [rows[r].get("A", "") for r in sorted(rows) if rows[r].get("A")]


def parse_xlsx_type_layouts(xlsx_path: Path) -> List[Tuple[str, int, str, str, str]]:
    """(mode, entry_idx, unit, type_code_bin, type_name) from hierarchy sheets."""
    entry_re = re.compile(r"entry(\d+)\s*\[", re.I)
    map_re = re.compile(r"map=([01]+)\s*→\s*([A-Z0-9]+)")
    type_re = re.compile(r"\[([01]+)\]\s+(\S+)")

    def parse(texts: List[str], mode: str) -> List[Tuple[str, int, str, str, str]]:
        entry: Optional[int] = None
        unit: Optional[str] = None
        out: List[Tuple[str, int, str, str, str]] = []
        for text in texts:
            t = text.strip()
            em = entry_re.search(t)
            if em:
                entry = int(em.group(1))
                unit = None
                continue
            mm = map_re.search(t)
            if mm:
                unit = mm.group(2)
                continue
            tm = type_re.search(t)
            if tm and entry is not None and unit:
                out.append((mode, entry, unit, tm.group(1), tm.group(2)))
        return out

    with zipfile.ZipFile(xlsx_path) as zf:
        strings = _xlsx_shared_strings(zf)
        e2_rows = _xlsx_sheet_cells(zf, "xl/worksheets/sheet2.xml", strings)
        e3_rows = _xlsx_sheet_cells(zf, "xl/worksheets/sheet3.xml", strings)
    return parse(_xlsx_col_a(e2_rows), "E2") + parse(_xlsx_col_a(e3_rows), "E3")


def parse_xlsx_overview_geometry(
    xlsx_path: Path,
) -> Tuple[Dict[str, str], List[Tuple[str, int, str, str]]]:
    """Overview sheet: field bits plus (mode, entry_idx, map_bin, unit)."""
    with zipfile.ZipFile(xlsx_path) as zf:
        strings = _xlsx_shared_strings(zf)
        rows = _xlsx_sheet_cells(zf, "xl/worksheets/sheet1.xml", strings)

    fields: Dict[str, str] = {}
    for r in sorted(rows):
        name = (rows[r].get("A") or "").strip()
        bits = (rows[r].get("B") or "").strip()
        if name and bits.startswith("bit["):
            fields[name] = bits.split()[0]

    units: List[Tuple[str, int, str, str]] = []
    mode = ""
    entry_idx = -1
    for r in sorted(rows):
        a = (rows[r].get("A") or "").strip()
        if a.startswith("§3"):
            mode = "E2"
            continue
        if a.startswith("§4"):
            mode = "E3"
            continue
        if a.startswith("entry") and mode:
            em = re.match(r"entry(\d+)", a)
            if em:
                entry_idx = int(em.group(1))
        fmap = (rows[r].get("F") or "").strip()
        unit = (rows[r].get("G") or "").strip()
        if mode and entry_idx >= 0 and re.fullmatch(r"[01]+", fmap) and unit:
            units.append((mode, entry_idx, fmap, unit))
    return fields, units


def json_type_layout_keys(
    data: Dict[str, Any],
) -> List[Tuple[str, int, str, str, str]]:
    keys: List[Tuple[str, int, str, str, str]] = []
    for enk, mode in (("entry_num_0", "E2"), ("entry_num_1", "E3")):
        for ek, ev in data[enk].items():
            if not isinstance(ev, dict) or not ek.startswith("entry"):
                continue
            eidx = int(ek[len("entry") :])
            for un, uo in ev.items():
                if not isinstance(uo, dict) or "types" not in uo:
                    continue
                for tn, to in uo["types"].items():
                    keys.append(
                        (mode, eidx, un, str(to["type_code_bin"]).strip(), tn)
                    )
    return keys


def json_unit_map_keys(
    data: Dict[str, Any],
) -> List[Tuple[str, int, str, str]]:
    keys: List[Tuple[str, int, str, str]] = []
    for enk, mode in (("entry_num_0", "E2"), ("entry_num_1", "E3")):
        for ek, ev in data[enk].items():
            if not isinstance(ev, dict) or not ek.startswith("entry"):
                continue
            eidx = int(ek[len("entry") :])
            for un, uo in ev.items():
                if not isinstance(uo, dict) or "mapping_value" not in uo:
                    continue
                keys.append((mode, eidx, str(uo["mapping_value"]).strip(), un))
    return keys


def check_xlsx_json_parity(xlsx_path: Path, data: Dict[str, Any]) -> None:
    """Parse the golden XLSX (primary layout) against the JSON pair. No invention."""
    fields, xlsx_units = parse_xlsx_overview_geometry(xlsx_path)
    if fields.get("Total bundle") != "bit[95:0]":
        raise SystemExit(f"XLSX Total bundle {fields.get('Total bundle')!r}")
    if fields.get("Header") != "bit[5:0]":
        raise SystemExit(f"XLSX Header {fields.get('Header')!r}")
    if fields.get("Payload") != "bit[95:6]":
        raise SystemExit(f"XLSX Payload {fields.get('Payload')!r}")
    if int(data["bundle_bits"]) != 96 or int(data["payload_lsb"]) != 6:
        raise SystemExit("JSON bundle/payload_lsb disagree with XLSX Overview")
    if int(data["payload_budget_bits"]) != 90:
        raise SystemExit("JSON payload_budget_bits != XLSX 90b")

    json_units = json_unit_map_keys(data)
    if sorted(xlsx_units) != sorted(json_units):
        raise SystemExit(
            f"XLSX/JSON unit-map mismatch xlsx={len(xlsx_units)} "
            f"json={len(json_units)}"
        )

    xlsx_types = parse_xlsx_type_layouts(xlsx_path)
    json_types = json_type_layout_keys(data)

    def type_key(
        t: Tuple[str, int, str, str, str],
    ) -> Tuple[str, int, str, int, str]:
        mode, eidx, unit, tcb, name = t
        return (mode, eidx, unit, int(tcb, 2), name)

    xset = set(map(type_key, xlsx_types))
    jset = set(map(type_key, json_types))
    if xset != jset:
        raise SystemExit(
            f"XLSX/JSON type-layout mismatch xlsx-only={len(xset - jset)} "
            f"json-only={len(jset - xset)}"
        )
    if len(xlsx_types) != PIN_TYPE_LAYOUTS or len(json_types) != PIN_TYPE_LAYOUTS:
        raise SystemExit(
            f"type layout count xlsx={len(xlsx_types)} json={len(json_types)} "
            f"pin={PIN_TYPE_LAYOUTS}"
        )
    for xt, jt in zip(
        sorted(xlsx_types, key=type_key), sorted(json_types, key=type_key)
    ):
        if len(xt[3]) != len(jt[3]):
            raise SystemExit(
                f"type_code_bin width mismatch {xt} vs {jt}"
            )
    print(
        f"OK XLSX↔JSON parity layouts={len(xlsx_types)} unit_maps={len(xlsx_units)}"
    )


def _walk_ledger_entries(obj: Any, path: str = "") -> Iterable[Tuple[str, Dict[str, Any]]]:
    if isinstance(obj, dict):
        if "id" in obj or "completion_state_id" in obj:
            yield path, obj
        for key, val in obj.items():
            child = f"{path}.{key}" if path else str(key)
            yield from _walk_ledger_entries(val, child)
    elif isinstance(obj, list):
        for i, val in enumerate(obj):
            yield from _walk_ledger_entries(val, f"{path}[{i}]")


def classify_header_bits(bits: int, nbytes: int) -> str:
    if nbytes != 12:
        return "malformed_framing"
    indicator = extract_bits(bits, 0, 3)
    reserved = extract_bits(bits, 4, 2)
    if indicator != 0x7:
        return "malformed_header"
    if reserved != 0:
        return "malformed_header_reserved"
    return "header_ok"


def check_canonical_vectors(path: Path, cat: Catalog) -> None:
    """Consume every ledger entry. Honor may_drive_* — never invent hex or MC."""
    sha = sha256_file(path)
    if sha != PINNED_CANONICAL_SHA256:
        raise SystemExit(
            f"canonical-vector sha256 {sha} != pinned {PINNED_CANONICAL_SHA256}"
        )
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("may_drive_llvm_mc_encode") is not False:
        raise SystemExit(
            "canonical ledger may_drive_llvm_mc_encode must stay false; "
            "importer --check does not drive llvm-mc"
        )
    if data.get("may_drive_lld_idle_pad") is not False:
        raise SystemExit("canonical ledger may_drive_lld_idle_pad must stay false")
    geom = data.get("geometry_contract") or {}
    if int(geom.get("bundle_bits", -1)) != cat.bundle_bits:
        raise SystemExit("canonical geometry_contract.bundle_bits != catalog")
    if int(geom.get("parcel_bytes", -1)) != (cat.bundle_bits + 7) // 8:
        raise SystemExit("canonical geometry_contract.parcel_bytes != catalog")
    if geom.get("format_indicator") != "0b111":
        raise SystemExit("canonical format_indicator != 0b111")
    byte_order = (geom.get("byte_order_for_published_hex") or {}).get("convention")
    if byte_order != "little_endian_bit0_in_byte0":
        raise SystemExit(f"unexpected canonical byte-order convention {byte_order!r}")

    oracle = ((data.get("authority") or {}).get("oracle_sha256")) or {}
    if oracle.get("format_e_bit_layout_v2.json") != PINNED_JSON_SHA256:
        raise SystemExit("canonical oracle JSON hash != pinned JSON")
    if oracle.get("format_e_bit_layout_v2.xlsx") != PINNED_XLSX_SHA256:
        raise SystemExit("canonical oracle XLSX hash != pinned XLSX")

    n = 0
    n_hex = 0
    n_null = 0
    for loc, entry in _walk_ledger_entries(data):
        n += 1
        ident = entry.get("id") or entry.get("completion_state_id") or loc
        status = str(entry.get("status") or "")
        hex12 = entry.get("wire_hex_le_12", None)
        wire = entry.get("wire_hex", None)
        kind = str(entry.get("kind") or "")

        if status in ("OPEN_BLOCKED", "RECIPE_ONLY", "STUB_ONLY"):
            if hex12 is not None:
                raise SystemExit(
                    f"{ident}: {status} published wire_hex_le_12={hex12!r}"
                )
            n_null += 1
            continue

        published = hex12 if hex12 is not None else wire
        if published is None:
            n_null += 1
            if status == "MALFORMED_DEFINED" and kind.startswith("malformed_framing"):
                # MAL_TRUNC_0 may use empty wire_hex rather than wire_hex_le_12.
                published = wire if wire is not None else ""
            elif status in ("MALFORMED_DEFINED", "ILLUSTRATION_ONLY"):
                raise SystemExit(f"{ident}: {status} missing published hex")
            else:
                continue

        if not isinstance(published, str):
            raise SystemExit(f"{ident}: published hex is not a string")
        if len(published) % 2 != 0:
            raise SystemExit(f"{ident}: odd-length hex {published!r}")
        raw = bytes.fromhex(published)
        nbytes = len(raw)
        bits = le_hex_to_bits(published) if published else 0
        if published and bits_to_le_hex(bits, nbytes) != published.lower():
            raise SystemExit(f"{ident}: LE hex round-trip miss")
        n_hex += 1
        cls = classify_header_bits(bits, nbytes)

        if status == "ILLUSTRATION_ONLY":
            if cls != "header_ok":
                raise SystemExit(f"{ident}: illustration header not ok ({cls})")
            mode = entry.get("mode")
            entry_num = extract_bits(bits, 3, 1)
            if mode == "E2" and entry_num != 0:
                raise SystemExit(f"{ident}: E2 illustration entry_num={entry_num}")
            if mode == "E3" and entry_num != 1:
                raise SystemExit(f"{ident}: E3 illustration entry_num={entry_num}")
            continue

        if status == "MALFORMED_DEFINED":
            if kind.startswith("malformed_framing"):
                if nbytes == 12:
                    raise SystemExit(
                        f"{ident}: framing malformed claimed 12-byte parcel"
                    )
                if cls != "malformed_framing":
                    raise SystemExit(f"{ident}: expected framing reject, got {cls}")
            elif kind == "malformed_header" or "INDICATOR" in str(ident) or ident == "MAL_ALL_ZERO_12B":
                if cls != "malformed_header":
                    raise SystemExit(f"{ident}: expected indicator reject, got {cls}")
            elif kind == "malformed_header_reserved":
                if cls != "malformed_header_reserved":
                    raise SystemExit(f"{ident}: expected reserved reject, got {cls}")
            else:
                raise SystemExit(f"{ident}: unhandled MALFORMED_DEFINED kind={kind!r}")
            continue

        raise SystemExit(f"{ident}: unhandled ledger status {status!r}")

    if n == 0:
        raise SystemExit("canonical ledger walked zero entries")
    print(
        f"OK canonical-vector ledger entries={n} hex={n_hex} null={n_null}"
    )


def diff_generated_targets(
    targets: Dict[Path, str], *, quiet: bool = False
) -> List[str]:
    failed: List[str] = []
    for path, content in targets.items():
        if not path.is_file():
            if not quiet:
                print(f"MISSING {path}", file=sys.stderr)
            failed.append(str(path))
            continue
        cur = path.read_text(encoding="utf-8")
        if cur != content:
            if not quiet:
                print(f"OUT_OF_DATE {path}", file=sys.stderr)
            failed.append(str(path))
        elif not quiet:
            print(f"OK {path}")
    return failed


IMM_ANN_RE = re.compile(r"^(uimm|simm|imm)(\d+)", re.IGNORECASE)

# Explicit golden uimm/simm whose generated member field width differs.
# Query-live via --check (empty = no unmatched uimm/simm). Golden `immN`
# is a width pin only — do not invent a td rewrite for those rows.
TD_GOLDEN_IMM_WIDTH_RESIDUAL: frozenset[str] = frozenset()


def collect_golden_imm_annotations(data: Any) -> Dict[str, Tuple[str, int]]:
    """Map golden instruction name → (kind, width) from catalog `imm` fields."""
    out: Dict[str, Tuple[str, int]] = {}
    def walk(obj: Any) -> None:
        if isinstance(obj, dict):
            inst = obj.get("instruction")
            imm = obj.get("imm")
            if isinstance(inst, str) and isinstance(imm, str):
                m = IMM_ANN_RE.match(imm.strip())
                if m:
                    key = inst.strip().upper()
                    parsed = (m.group(1).lower(), int(m.group(2)))
                    prev = out.get(key)
                    if prev and prev[1] != parsed[1]:
                        raise SystemExit(
                            f"golden imm width drift {key}: {prev} vs {parsed}"
                        )
                    if prev and prev[0] != parsed[0] and "imm" not in (
                        prev[0],
                        parsed[0],
                    ):
                        raise SystemExit(
                            f"golden imm signedness drift {key}: {prev} vs {parsed}"
                        )
                    out[key] = parsed
            for val in obj.values():
                walk(val)
        elif isinstance(obj, list):
            for val in obj:
                walk(val)
    walk(data)
    return out


def check_td_golden_imm_parity(cat: Catalog, data: Any) -> None:
    """Fail closed when generated member imm width/signedness drifts from golden.

    Golden `imm20` is a width pin (signedness lives in catalog semantics);
    explicit `uimm*`/`simm*` must match the generated td operand class.
    """
    golden = collect_golden_imm_annotations(data)
    layouts = {lay.layout_id: lay for lay in cat.layouts}
    compared = 0
    compared_keys: set[str] = set()
    for rec in cat.members:
        if rec.is_nop:
            continue
        key = rec.logical.strip().upper()
        if key not in golden:
            continue
        gkind, gwidth = golden[key]
        lay = layouts[rec.layout_id]
        active = {role: (alias or "").strip() for role, alias in rec.operand_active}
        for i, of in enumerate(lay.operand_fields):
            alias = active.get(of.role, "")
            if not alias:
                continue
            # cbr_sel / hwlr_sel are IMM class but not the golden `imm` field.
            if of.bits.width != gwidth:
                continue
            _name, frag, _bw = field_operand_td(of, i, alias, rec.logical)
            ty = frag.split(":", 1)[0]
            tm = IMM_ANN_RE.match(ty)
            if not tm:
                continue
            tkind, twidth = tm.group(1).lower(), int(tm.group(2))
            if twidth != gwidth:
                raise SystemExit(
                    f"td-vs-golden imm width {key} {rec.member_symbol}: "
                    f"td {ty} vs golden {gkind}{gwidth}"
                )
            if gkind in ("uimm", "simm") and tkind != gkind:
                raise SystemExit(
                    f"td-vs-golden imm signedness {key} {rec.member_symbol}: "
                    f"td {ty} vs golden {gkind}{gwidth}"
                )
            compared += 1
            compared_keys.add(key)
        # Width-only golden `immN` is not a signedness fact (catalog ZEXT/
        # signed_mask owns ANDI/ORI/XORI). Unmatched explicit uimm/simm
        # widths stay on the residual ledger — do not invent a td rewrite.
    if compared == 0:
        raise SystemExit("td-vs-golden imm parity compared zero fields")
    unmatched: Dict[str, Tuple[str, int]] = {}
    for rec in cat.members:
        if rec.is_nop:
            continue
        key = rec.logical.strip().upper()
        if key not in golden or key in compared_keys:
            continue
        gkind, gwidth = golden[key]
        if gkind not in ("uimm", "simm"):
            continue
        unmatched[key] = (gkind, gwidth)
    unexpected = sorted(set(unmatched) - TD_GOLDEN_IMM_WIDTH_RESIDUAL)
    vanished = sorted(TD_GOLDEN_IMM_WIDTH_RESIDUAL - set(unmatched))
    if unexpected:
        raise SystemExit(
            "td-vs-golden imm width residual grew: "
            + ", ".join(
                f"{k} golden {unmatched[k][0]}{unmatched[k][1]}"
                for k in unexpected
            )
        )
    if vanished:
        raise SystemExit(
            "td-vs-golden imm width residual closed (drop from ledger): "
            + ", ".join(vanished)
        )
    print(
        f"OK td-vs-golden imm parity compared={compared} "
        f"golden_logicals={len(golden)} "
        f"width_residual={len(unmatched)}"
    )


def prove_flipped_byte_fails(targets: Dict[Path, str]) -> None:
    """Acceptance: one flipped byte in a generated file is visible to --check."""
    first = next(iter(targets))
    if not first.is_file():
        raise SystemExit(f"stale-flip probe needs committed file {first}")
    with tempfile.TemporaryDirectory(prefix="haydn-fe-stale-") as tmp:
        tmp_dir = Path(tmp)
        mutated_targets: Dict[Path, str] = {}
        for path, content in targets.items():
            dest = tmp_dir / path.name
            dest.write_text(path.read_text(encoding="utf-8"), encoding="utf-8")
            mutated_targets[dest] = content
        victim = tmp_dir / first.name
        text = victim.read_text(encoding="utf-8")
        if not text:
            raise SystemExit("stale-flip probe: empty generated file")
        idx = min(len(text) // 2, len(text) - 1)
        repl = "X" if text[idx] != "X" else "Y"
        victim.write_text(text[:idx] + repl + text[idx + 1 :], encoding="utf-8")
        if not diff_generated_targets(mutated_targets, quiet=True):
            raise SystemExit(
                "stale-flip probe: flipped generated byte was not detected"
            )
    print(f"OK stale-flip detector ({first.name})")


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--xlsx", type=Path, default=None)
    ap.add_argument("--canonical-vectors", type=Path, default=None)
    ap.add_argument("--xlsx-hash", default=None)
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Target Haydn directory (default: llvm/lib/Target/Haydn)",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="Verify committed outputs, XLSX/JSON parity, canonical RT (no write)",
    )
    ap.add_argument(
        "--emit-mnemonic-roundtrip",
        action="store_true",
        help="Write test/MC/Haydn/format-e-mnemonic-roundtrip.s (also written "
        "on a normal generate; --check diffs it)",
    )
    args = ap.parse_args(argv)

    golden = resolve_golden_dir()
    json_path: Path = args.json or (golden / "format_e_bit_layout_v2.json")
    xlsx_path: Path = args.xlsx or (golden / "format_e_bit_layout_v2.xlsx")
    canonical_path: Path = args.canonical_vectors or (
        golden / "format_e_canonical_vectors_v1.json"
    )
    if not json_path.is_file():
        print(f"error: golden JSON not found: {json_path}", file=sys.stderr)
        return 2

    json_sha = sha256_file(json_path)
    if json_sha != PINNED_JSON_SHA256:
        print(
            f"error: JSON sha256 {json_sha} != pinned {PINNED_JSON_SHA256}",
            file=sys.stderr,
        )
        return 2

    if not xlsx_path.is_file():
        print(f"error: golden XLSX not found: {xlsx_path}", file=sys.stderr)
        return 2
    xlsx_sha = sha256_file(xlsx_path)
    if xlsx_sha != PINNED_XLSX_SHA256:
        print(
            f"error: XLSX sha256 {xlsx_sha} != pinned {PINNED_XLSX_SHA256}",
            file=sys.stderr,
        )
        return 2
    if args.xlsx_hash is not None and args.xlsx_hash != xlsx_sha:
        print(
            f"error: --xlsx-hash {args.xlsx_hash} != file sha256 {xlsx_sha}",
            file=sys.stderr,
        )
        return 2

    data = json.loads(json_path.read_text(encoding="utf-8"))
    cat = build_catalog(data)
    global _GOLDEN_IMM_ANN
    _GOLDEN_IMM_ANN = collect_golden_imm_annotations(data)

    out_dir: Path = args.out_dir
    member_to_logical = collect_member_to_logical(
        cat, out_dir / "HaydnMultiSlotPseudo.td"
    )

    records = emit_records_inc(cat, json_sha, xlsx_sha)
    ledger = emit_setdesc_ledger_inc(cat)
    index_path = golden / "instruction_type_index.json"
    if not index_path.is_file():
        print(f"error: golden index not found: {index_path}", file=sys.stderr)
        return 2
    index_sha = sha256_file(index_path)
    if index_sha != PINNED_INDEX_SHA256:
        print(
            f"error: index sha256 {index_sha} != pinned {PINNED_INDEX_SHA256}",
            file=sys.stderr,
        )
        return 2
    accum_ties = load_accumulator_ties(index_path)
    td_tied = load_td_tied_logicals(Path(__file__).resolve().parent.parent)
    divergent = sorted(k for k in accum_ties if k not in td_tied)
    accum_ties = {k: v for k, v in accum_ties.items() if k in td_tied}
    # Golden says these read their destination; the LLVM logical models no
    # tie, so members stay at logical arity and the gap is a ledger item
    # (conditional moves / partial-word inserts with unmodeled dest reads).
    # Measured, pinned: a regen that changes this set must be re-audited.
    divergent_non_ls = [
        k for k in divergent
        if not k.startswith(("D_", "S_", "PLD", "WBAR"))
    ]
    if len(divergent_non_ls) != 87:
        raise SystemExit(
            "error: golden-tied-but-TD-untied set changed "
            f"({len(divergent_non_ls)}): {divergent_non_ls} — re-audit "
            "member arity vs the logicals (CB ledger: unmodeled dest reads)"
        )
    members_td = emit_members_td_inc(cat, accum_ties)
    member_opcodes = emit_member_opcodes_inc(cat, member_to_logical)
    mnemonic_rt = emit_mnemonic_roundtrip_s(cat)
    mnemonic_rt_path = mnemonic_roundtrip_path(out_dir)

    targets = {
        out_dir / "HaydnGenFormatERecords.inc": records,
        out_dir / "HaydnGenFormatESetDescLedger.inc": ledger,
        out_dir / "HaydnFormatsE96Members.td.inc": members_td,
        out_dir / "HaydnGenFormatEMemberOpcodes.inc": member_opcodes,
        mnemonic_rt_path: mnemonic_rt,
    }

    if args.check:
        failed = bool(diff_generated_targets(targets))
        if failed:
            return 1
        try:
            prove_flipped_byte_fails(targets)
            check_xlsx_json_parity(xlsx_path, data)
            check_td_golden_imm_parity(cat, data)
            roundtrip_members(cat)
            print(
                f"OK member encode→decode round-trip members={len(cat.members)}"
            )
            if not canonical_path.is_file():
                raise SystemExit(
                    f"canonical-vector ledger not found: {canonical_path}"
                )
            check_canonical_vectors(canonical_path, cat)
        except SystemExit as exc:
            msg = str(exc)
            if msg and msg != "1":
                print(f"error: {msg}", file=sys.stderr)
            return 1
        print(
            f"check passed: layouts={len(cat.layouts)} members={len(cat.members)} "
            f"logicals={len(cat.alternatives)} member_to_logical={len(member_to_logical)}"
        )
        return 0

    for path, content in targets.items():
        changed = write_if_changed(path, content)
        print(f"{'WROTE' if changed else 'UNCHANGED'} {path}")

    print(
        f"generated layouts={len(cat.layouts)} members={len(cat.members)} "
        f"logicals={len(cat.alternatives)} member_to_logical={len(member_to_logical)} "
        f"json={json_sha[:12]}…"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
