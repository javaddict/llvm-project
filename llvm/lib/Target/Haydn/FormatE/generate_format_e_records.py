#!/usr/bin/env python3
"""Generate Format E tables + live TableGen members from golden JSON (once).

Emits HaydnGenFormatERecords.inc, HaydnGenFormatESetDescLedger.inc,
HaydnFormatsE96Members.td.inc (LIVE Inst{}), HaydnGenFormatEMemberOpcodes.inc,
HaydnGenRelocFieldLsb.inc (FieldLsbSites + ExtraPublishedLsb),
and the MC mnemonic round-trip harness (test/MC/Haydn/format-e-mnemonic-roundtrip.s).
Product encode/decode: BUNDLE_E96 framing + tblgen on these members.

Usage:
  generate_format_e_records.py [--family NAME] [--json PATH] [--xlsx PATH]
                               [--canonical-vectors PATH] [--xlsx-hash HEX]
                               [--out-dir DIR] [--check]
                               [--emit-mnemonic-roundtrip]

--check regenerates into memory and diffs against the committed files, then
fail-closes on XLSX↔JSON layout parity, td-vs-golden imm width/signedness,
canonical-vector ledger round-trip, and reloc FieldLsb site drift.
It does not write, and it does not drive llvm-mc (ledger may_drive_llvm_mc_encode
is false). Peer: BundleSim generate_catalog.py --check
(bundlesim/isa/database/generate_catalog.py:483-485).
The mnemonic harness is included in that diff (same regenerate-and-compare).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Set, Tuple

from family_core import (
    AUTHORED_OVERLAY_PATH,
    PINNED_CANONICAL_SHA256,
    PINNED_INDEX_SHA256,
    PINNED_JSON_SHA256,
    PINNED_XLSX_SHA256,
    RECORDS_GENERATOR,
    SCHEMA_VERSION,
    add_family_argument,
    check_cutover_surfaces,
    check_residual_hand_logicals,
    generated_banner,
    get_family,
    golden_inputs_pin_path,
    prove_derived_xlsx_not_authority,
    prove_text_has_authority_pins,
    prove_unpublished_choice_fails,
    prove_unpinned_consumed_fails,
    prove_unused_authority_not_consumed,
    resolve_golden_dir,
    sha256_file,
    verify_authority_inputs,
    verify_golden_inputs_pin,
)

SSML_NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}

# Catalog snapshot pins from the current JSON hash (manifest §5).
# 2026-08-28 v2_2 rollover: +7 AR_CBR instrs (unique 807→814, E2 801→808,
# E3 797→804, both 791→798; type layouts 126→128 = the two new LOADSTORE0
# AR_CBR tables; alt multiplicity 2: 66→73).
PIN_UNIQUE_NON_NOP = 814
PIN_E2_NON_NOP = 808
PIN_E3_NON_NOP = 804
PIN_BOTH_NON_NOP = 798
PIN_E2_ONLY = 10
PIN_E3_ONLY = 6
PIN_TYPE_LAYOUTS = 128
PIN_E2_UNIT_PAIRS = 9
PIN_E3_LEGAL_TUPLES = 42
PIN_E3_ILLEGAL_TUPLES = 22

BIT_RE = re.compile(r"bit\[(\d+)(?::(\d+))?\]")
FIELD_RE = re.compile(
    r"^(?P<role>[A-Za-z0-9_]+)\((?P<aliases>[^)]*)\):\s*bit\[(?P<hi>\d+)(?::(?P<lo>\d+))?\]\s*\((?P<width>\d+)b\)$"
)


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
    expected_mult = {1: 1, 2: 73, 3: 15, 4: 7, 5: 532, 7: 186}
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


#//===---------------------------------------------------------------------===//
# Universal singleton coverage (PIPE-20 schema seat)
#//===---------------------------------------------------------------------===//
#
# Law: every compiler-reachable logical has generated singleton coverage —
# at least one mode M and entry E where the logical owns >=1 generated
# member AND the golden catalog hosts >=1 NOP member at the same (M, E)
# window. One real child plus generated NOP completion fills a complete
# admitted mode-M packet (E2 = 2 entries, E3 = 3), so RA always has a
# sequential fallback without any matching frontier. NOP itself is exempt
# from alt spans by construction (is_nop rows never enter cat.alternatives;
# PIN_UNIQUE_NON_NOP=814 pins that) — its coverage is the NOP-completion
# pin below, never a NOP row in alternatives.

# Window modes that host >=1 NOP member (measured at golden v2_2: every
# member-hosting (mode, entry) window hosts NOP; both modes admit the
# architectural idle packet).
def nop_completion_windows(cat: Catalog) -> Set[Tuple[str, int]]:
    return {(m.mode, m.entry_idx) for m in cat.members if m.is_nop}


def compute_singleton_coverage(
    cat: Catalog,
) -> Tuple[List[Tuple[str, int, int]], Set[str]]:
    """Per-logical singleton proof rows + the uncovered set.

    Returns ([("LOGICAL", ModeMask, FirstMemberId), …] sorted by logical,
    {uncovered logicals}). ModeMask bit0 = E2, bit1 = E3 — a bit is set
    when the logical has a member at some entry whose (mode, entry)
    window also hosts a NOP member (NOP completion in the same packet).
    FirstMemberId is the lowest covering MemberId. Fails closed when any
    non-NOP catalog logical lacks a covering mode (catches golden growth:
#   a new logical without NOP-completable placement is not admissible).
    """
    nop_windows = nop_completion_windows(cat)
    cover: Dict[str, Tuple[int, int]] = {}
    for rec in cat.members:
        if rec.is_nop:
            continue
        if (rec.mode, rec.entry_idx) not in nop_windows:
            continue
        bit = 1 if rec.mode == "E2" else 2
        prev = cover.get(rec.logical)
        if prev is None:
            cover[rec.logical] = (bit, rec.member_id)
        else:
            cover[rec.logical] = (prev[0] | bit, min(prev[1], rec.member_id))
    uncovered = set(cat.alternatives) - set(cover)
    if uncovered:
        raise SystemExit(
            "error: catalog logicals without NOP-completed singleton "
            f"coverage ({len(uncovered)}): {sorted(uncovered)} — golden "
            "growth must place every new logical in a NOP-hosting window"
        )
    rows = sorted(
        (logical, mask, first) for logical, (mask, first) in cover.items()
    )
    return rows, set()


def nop_completion_mode_mask(cat: Catalog) -> int:
    """Modes hosting NOP members (bit0 = E2, bit1 = E3)."""
    mask = 0
    for rec in cat.members:
        if rec.is_nop:
            mask |= 1 if rec.mode == "E2" else 2
    return mask


def emit_records_inc(
    cat: Catalog, json_sha: str, xlsx_sha: str, family
) -> str:
    e2_pairs = e2_unit_pairs(cat)
    e3_legal, e3_illegal = e3_unit_tuples(cat)
    e2_only, e3_only = mode_only_name_sets(cat)
    lines: List[str] = []
    lines.append("//===-- HaydnGenFormatERecords.inc - Format E records -*- C++ -*-===//")
    lines.append("//")
    lines.extend(generated_banner(
        generator=RECORDS_GENERATOR, family=family,
        json_sha=json_sha, xlsx_sha=xlsx_sha,
    ))
    lines.append("//")
    lines.append("// Numeric member identity is (family, row=MemberId, entry, unit, type).")
    lines.append("// Family tags in def names are tblgen uniqueness only — do not parse.")
    lines.append("//===----------------------------------------------------------------------===//")
    lines.append("")
    lines.append("#ifdef GET_FORMAT_E_GOLDEN_PINS")
    lines.append("#undef GET_FORMAT_E_GOLDEN_PINS")
    lines.append(f'static constexpr const char FormatEJSONSHA256[] = "{json_sha}";')
    lines.append(f'static constexpr const char FormatEXLSXSHA256[] = "{xlsx_sha}";')
    lines.append(
        f"static constexpr uint8_t FormatEFamilyId = {family.id}u; // {family.display}"
    )
    lines.append("static constexpr unsigned FormatEAdmittedFamilyCount = 1u;")
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
    lines.append("  uint8_t Family; // BundleFamily; E96=0")
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
            f"{family.id}, "
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

    # Universal singleton coverage (PIPE-20 schema seat): per-logical
    # NOP-completed mode proof. Inert generated data — no product pass
    # consumes it; HaydnFormatERecordsTest pins the rows and the empty
    # uncovered ratchet set. Same class as the mode-only name SETS
    # (W44 / P18(c)): generators emit sets/proofs, never counts alone.
    singleton_rows, _uncovered = compute_singleton_coverage(cat)
    nop_modes = nop_completion_mode_mask(cat)
    lines.append("#ifdef GET_FORMAT_E_SINGLETON_COVERAGE")
    lines.append("#undef GET_FORMAT_E_SINGLETON_COVERAGE")
    lines.append("struct FormatESingletonCoverageRec {")
    lines.append("  const char *Logical;")
    lines.append("  uint8_t ModeMask; // bit0=E2, bit1=E3 (NOP-completed)")
    lines.append("  uint16_t FirstMemberId; // lowest covering member")
    lines.append("};")
    lines.append(
        "static constexpr FormatESingletonCoverageRec"
        " FormatESingletonCoverage[] = {"
    )
    for logical, mask, first in singleton_rows:
        lines.append(
            f'  {{"{c_escape(logical)}", 0b{mask:02b}u, {first}u}},'
        )
    lines.append("};")
    lines.append(
        f"static constexpr unsigned FormatESingletonCoverageCount ="
        f" {len(singleton_rows)}u;"
    )
    lines.append(
        "static_assert(sizeof(FormatESingletonCoverage) /"
        " sizeof(FormatESingletonCoverage[0]) =="
        " FormatESingletonCoverageCount, \"singleton coverage pin\");"
    )
    lines.append(
        "static_assert(FormatESingletonCoverageCount =="
        " FormatENonNopLogicalCount, \"singleton coverage covers every"
        " non-NOP logical\");"
    )
    lines.append(
        f"static constexpr uint8_t FormatENopCompletionModes ="
        f" 0b{nop_modes:02b}u; // modes hosting architectural NOP members"
    )
    # Ratchet set: compiler-reachable logicals without catalog coverage.
    # Monotone shrink only (same semantics as EXPECTED_SINGLETON_UNCOVERED
    # in the census); EMPTY since installation — growth fails generation.
    # The names live in the generator pin (a zero-size array is not valid
    # C++); the generated constant is the count the unittest pins to 0.
    lines.append(
        f"static constexpr unsigned FormatESingletonUncoveredCount ="
        f" {len(EXPECTED_SINGLETON_UNCOVERED)}u;"
    )
    lines.append(
        "static_assert(FormatESingletonUncoveredCount == 0u,"
        " \"singleton uncovered ratchet: compiler-reachable logicals"
        " without coverage must stay pinned empty\");"
    )
    lines.append("#endif // GET_FORMAT_E_SINGLETON_COVERAGE")
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


# Reloc FieldLsbSites / ExtraPublishedLsb. Parcel-absolute imm LSBs from
# generated members. RelocKind / scale / ELF rows stay hand-authored in
# HaydnRelocLayout.cpp; Loc sniffing stays resolveFieldLsb.
RELOC_FIELD_LSB_INC = "HaydnGenRelocFieldLsb.inc"
# Same unit numbers as HaydnRelocLayout (ALU0=0 … LOADSTORE0=4).
_RELOC_UNIT_INDEX = {
    "ALU0": 0,
    "ALU1": 1,
    "ALU2": 2,
    "LOAD1": 3,
    "LOADSTORE0": 4,
}
_RELOC_UNIT_TOKEN = {
    0xFF: "kAnyUnit",
    0: "kALU0",
    1: "kALU1",
    2: "kALU2",
    3: "kLOAD1",
    4: "kLS0",
}
_RELOC_LS_UNITS = frozenset({"LOADSTORE0", "LOAD1"})
# Type+opcode → RelocKind, mirroring HaydnRelocLayout TypeFixupSpecs.
# RI20 also twins to PC_LO20 (same windows, PC-rel kind overlay).
_RELOC_TYPE_SPECS: Tuple[Tuple[str, int, int, str, int, bool], ...] = (
    ("I12", 1, 1, "HI12", 12, False),
    ("I12", 4, 7, "WIDE_BranchSImm12", 12, False),
    ("RI12", 1, 1, "JALRSImm12", 12, False),
    ("RI12", 2, 7, "WIDE_BranchSImm12_RI", 12, False),
    ("I20", 1, 1, "WIDE_CallSImm20", 20, False),
    ("RI20", 1, 7, "LO20", 20, False),
    ("RI6", 0, 255, "LS_IMM", 6, True),
    ("I8", 4, 5, "CSR_UImm8", 8, False),
)
_RELOC_KIND_ORDER = (
    "HI12",
    "LO20",
    "PC_LO20",
    "LS_IMM",
    "CSR_UImm8",
    "JALRSImm12",
    "WIDE_BranchSImm12",
    "WIDE_BranchSImm12_RI",
    "WIDE_CallSImm20",
    "HWLoopOff1",
    "HWLoopOff2",
)
_RELOC_KIND_COMMENT = {
    "HI12": "HI12 / LUI I12",
    "LO20": "LO20 / PC_LO20 — RI20 is E2-only",
    "LS_IMM": "LS_IMM RI6",
    "CSR_UImm8": "CSR I8",
    "JALRSImm12": "JALR RI12",
    "WIDE_BranchSImm12": "I12 / RI12 cond-branch",
    "WIDE_CallSImm20": "JAL I20",
    "HWLoopOff1": "SET_HWLOOP F2 (HWLRIIR); HWLRIII extras below",
}
# E2 e0 RelocFieldInfo defaults (hand table in HaydnRelocLayout.cpp).
_RELOC_TABLE_LSB = {
    "HI12": 32,
    "LO20": 31,
    "PC_LO20": 31,
    "LS_IMM": 28,
    "CSR_UImm8": 32,
    "JALRSImm12": 32,
    "WIDE_BranchSImm12": 32,
    "WIDE_BranchSImm12_RI": 32,
    "WIDE_CallSImm20": 31,
    "HWLoopOff1": 32,
    "HWLoopOff2": 38,
}
# RelocLayoutTest PublishedMemberFieldLsbAndFixupFields windows. Do not
# invent LSBs; fail generation if a committed test window drops or an
# unpublished window appears.
_RELOC_TEST_PUBLISHED = (
    ("JALRSImm12", 32),
    ("JALRSImm12", 23),
    ("JALRSImm12", 54),
    ("HI12", 32),
    ("HI12", 21),
    ("HI12", 23),
    ("HI12", 54),
    ("HI12", 81),
    ("HI12", 83),
    ("PC_LO20", 31),
    ("PC_LO20", 65),
    ("LO20", 65),
    ("LS_IMM", 28),
    ("LS_IMM", 72),
    ("LS_IMM", 25),
    ("LS_IMM", 54),
    ("LS_IMM", 85),
    ("WIDE_CallSImm20", 31),
    ("WIDE_CallSImm20", 17),
    ("WIDE_CallSImm20", 48),
    ("WIDE_BranchSImm12", 32),
    ("WIDE_BranchSImm12", 23),
    ("WIDE_BranchSImm12", 54),
    ("WIDE_BranchSImm12", 81),
    ("CSR_UImm8", 32),
    ("CSR_UImm8", 27),
    ("CSR_UImm8", 23),
    ("CSR_UImm8", 54),
    ("CSR_UImm8", 85),
    ("HWLoopOff1", 32),
    ("HWLoopOff1", 13),
    ("HWLoopOff2", 38),
    ("HWLoopOff2", 36),
)
_RELOC_TEST_UNPUBLISHED = (
    ("JALRSImm12", 81),
    ("JALRSImm12", 99),
    ("PC_LO20", 23),
    # E2 e1 has no I12/I8 member at golden v2_2 — no such window exists.
    ("HI12", 65),
    ("CSR_UImm8", 65),
)
# resolveFieldLsbForMember(kind, mode, entry, unit|None) → lsb
_RELOC_TEST_MEMBER = (
    ("HI12", 0, 0, None, 32),
    ("HI12", 1, 0, 2, 21),
    ("HI12", 1, 0, 0, 23),
    ("HI12", 1, 1, None, 54),
    ("HI12", 1, 2, 2, 83),
    ("HI12", 1, 2, 0, 81),
    ("HI12", 1, 0, 0, 23),
    ("HI12", 1, 1, 0, 54),
    ("HI12", 1, 1, 1, 54),
    ("LO20", 0, 0, 0, 31),
    ("LO20", 0, 1, 1, 65),
    ("PC_LO20", 0, 1, 1, 65),
    ("PC_LO20", 1, 0, 0, 31),
    ("JALRSImm12", 0, 0, 0, 32),
    ("JALRSImm12", 1, 0, 0, 23),
    ("JALRSImm12", 1, 1, 0, 54),
    ("JALRSImm12", 1, 2, 0, 32),
    ("WIDE_BranchSImm12", 1, 2, None, 81),
    ("CSR_UImm8", 0, 0, None, 32),
    ("CSR_UImm8", 1, 0, 2, 27),
    ("CSR_UImm8", 1, 0, 0, 23),
    ("CSR_UImm8", 1, 1, 0, 54),
    ("CSR_UImm8", 1, 1, 1, 54),
    ("CSR_UImm8", 1, 2, 2, 85),
    ("CSR_UImm8", 1, 2, 0, 85),
    ("CSR_UImm8", 1, 2, None, 85),
    # E2 e1: no I12/I8 member exists — both kinds fail closed to the E2
    # e0 table default (D1.24 structural law; no E2-e1 window is minted).
    ("HI12", 0, 1, 1, 32),
    ("CSR_UImm8", 0, 1, 1, 32),
)
# D1.17 sniff-pin cross-validation: (kind, mode, entry, unit) → the
# member-opcode set the C++ Loc-sniff pins must accept. Derived from the
# golden mapping tables; --check additionally asserts that members of
# OTHER units at the same (mode, entry) sharing (map, type) have opcodes
# DISJOINT from the kind range — the generated proof that the opc pin
# separates members (this is exactly what failed for D1.17: a BEQZ at
# E3 e1 ALU0 satisfied the LUI (map,type) predicate).
_RELOC_SNIFF_PIN = (
    # HI12: LUI opcode 1 at every generated site.
    ("HI12", 0, 0, 0, (1,)),
    ("HI12", 1, 0, 0, (1,)),
    ("HI12", 1, 0, 2, (1,)),
    ("HI12", 1, 1, 0, (1,)),
    ("HI12", 1, 1, 1, (1,)),
    ("HI12", 1, 2, 0, (1,)),
    ("HI12", 1, 2, 2, (1,)),
    # CSR_UImm8: CSRR 4 / CSRW 5 at every generated site.
    ("CSR_UImm8", 0, 0, 0, (4, 5)),
    ("CSR_UImm8", 1, 0, 0, (4, 5)),
    ("CSR_UImm8", 1, 0, 2, (4, 5)),
    ("CSR_UImm8", 1, 1, 0, (4, 5)),
    ("CSR_UImm8", 1, 1, 1, (4, 5)),
    ("CSR_UImm8", 1, 2, 0, (4, 5)),
    ("CSR_UImm8", 1, 2, 2, (4, 5)),
)
# Kinds whose non-default windows got D1.17 entry-qualified ELF twins.
# (kind, mode, entry, unit|None, qualified-enum-value) mirroring
# HaydnRelocLayout RelocKind 34..42. None unit == both units share it.
_RELOC_QUALIFIED_KINDS = (
    ("HI12", 1, 0, 2, "HI12_E3E0_ALU2", 34, 21),
    ("HI12", 1, 0, 0, "HI12_E3E0_ALU0", 35, 23),
    ("HI12", 1, 1, None, "HI12_E3E1", 36, 54),
    ("HI12", 1, 2, 2, "HI12_E3E2_ALU2", 37, 83),
    ("HI12", 1, 2, 0, "HI12_E3E2_ALU0", 38, 81),
    ("CSR_UImm8", 1, 0, 2, "CSR_UImm8_E3E0_ALU2", 39, 27),
    ("CSR_UImm8", 1, 0, 0, "CSR_UImm8_E3E0_ALU0", 40, 23),
    ("CSR_UImm8", 1, 1, None, "CSR_UImm8_E3E1", 41, 54),
    ("CSR_UImm8", 1, 2, None, "CSR_UImm8_E3E2", 42, 85),
)


def _reloc_imm_lsb(lay: TypeLayout, width: int, role: Optional[str] = None) -> int:
    hits: List[int] = []
    want = (role or "").lower()
    for of in lay.operand_fields:
        if of.bits.width != width:
            continue
        r = of.role.lower()
        aliases = ",".join(a.lower() for a in of.aliases)
        if want:
            if r == want or r.startswith(want):
                hits.append(of.bits.lo)
            continue
        if "imm" in r or "off" in r or "imm" in aliases or "off" in aliases:
            hits.append(of.bits.lo)
    if len(hits) != 1:
        raise SystemExit(
            f"reloc FieldLsb: {lay.mode} e{lay.entry_idx} {lay.unit} "
            f"{lay.type_name} width={width} role={role!r} hits={hits}"
        )
    return hits[0]


def _collect_reloc_field_lsb(
    cat: Catalog,
) -> Tuple[List[Tuple[str, int, int, int, int]], List[Tuple[str, int]]]:
    """Return (FieldLsbSites, ExtraPublishedLsb) from generated members.

    HWLRIIR is the unique (kind, mode, entry) site; HWLRIII LSBs that share
    that key go to ExtraPublishedLsb (E2 e0 Off1@13 / Off2@36). Unique LSB
    at a (kind, mode, entry) collapses to kAnyUnit=0xff so a unit-omitted
    resolveFieldLsbForMember query still hits the typed window.
    """
    layouts = {l.layout_id: l for l in cat.layouts}
    # (kind, mode, entry, unit) -> set of LSBs
    raw: Dict[Tuple[str, int, int, int], set] = defaultdict(set)

    def add_site(kind: str, rec: MemberRecord, lsb: int) -> None:
        unit = _RELOC_UNIT_INDEX.get(rec.unit)
        if unit is None:
            raise SystemExit(
                f"reloc FieldLsb: unknown unit {rec.unit!r} on {rec.member_symbol}"
            )
        mode = 0 if rec.mode == "E2" else 1
        raw[(kind, mode, rec.entry_idx, unit)].add(lsb)

    for rec in cat.members:
        if rec.is_nop:
            continue
        lay = layouts[rec.layout_id]
        if rec.type_name == "HWLRIIR":
            add_site("HWLoopOff1", rec, _reloc_imm_lsb(lay, 6, "imm1"))
            add_site("HWLoopOff2", rec, _reloc_imm_lsb(lay, 12, "imm2"))
            continue
        if rec.type_name == "HWLRIII":
            # Colliding extras are applied after HWLRIIR unique sites.
            continue
        for type_name, opc_lo, opc_hi, kind, width, require_ls in _RELOC_TYPE_SPECS:
            if rec.type_name != type_name:
                continue
            if rec.opcode < opc_lo or rec.opcode > opc_hi:
                continue
            if require_ls and rec.unit not in _RELOC_LS_UNITS:
                continue
            add_site(kind, rec, _reloc_imm_lsb(lay, width))
            if kind == "LO20":
                add_site("PC_LO20", rec, _reloc_imm_lsb(lay, width))

    # Unique site per (kind, mode, entry, unit). Multiple LSBs here are a
    # generator bug (HWLRIII collision is handled separately).
    unique: Dict[Tuple[str, int, int, int], int] = {}
    for key, lsbs in raw.items():
        if len(lsbs) != 1:
            raise SystemExit(f"reloc FieldLsb collision at {key}: {sorted(lsbs)}")
        unique[key] = next(iter(lsbs))

    extras: Dict[Tuple[str, int], None] = {}
    for rec in cat.members:
        if rec.is_nop or rec.type_name != "HWLRIII":
            continue
        lay = layouts[rec.layout_id]
        unit = _RELOC_UNIT_INDEX.get(rec.unit)
        if unit is None:
            raise SystemExit(
                f"reloc FieldLsb: unknown unit {rec.unit!r} on {rec.member_symbol}"
            )
        mode = 0 if rec.mode == "E2" else 1
        for kind, width, role in (
            ("HWLoopOff1", 6, "imm1"),
            ("HWLoopOff2", 12, "imm2"),
        ):
            lsb = _reloc_imm_lsb(lay, width, role)
            taken = [
                v
                for (k, m, e, _u), v in unique.items()
                if k == kind and m == mode and e == rec.entry_idx
            ]
            if taken and lsb not in taken:
                extras[(kind, lsb)] = None
            elif not taken:
                unique[(kind, mode, rec.entry_idx, unit)] = lsb

    # Collapse (kind, mode, entry) to kAnyUnit when every unit shares one LSB.
    collapsed: Dict[Tuple[str, int, int, int], int] = {}
    by_entry: Dict[Tuple[str, int, int], Dict[int, int]] = defaultdict(dict)
    for (kind, mode, entry, unit), lsb in unique.items():
        by_entry[(kind, mode, entry)][unit] = lsb
    for (kind, mode, entry), unit_lsbs in by_entry.items():
        lsb_vals = set(unit_lsbs.values())
        if len(lsb_vals) == 1:
            collapsed[(kind, mode, entry, 0xFF)] = next(iter(lsb_vals))
        else:
            for unit, lsb in unit_lsbs.items():
                collapsed[(kind, mode, entry, unit)] = lsb

    def site_key(row: Tuple[str, int, int, int, int]) -> Tuple:
        kind, mode, entry, unit, lsb = row
        kind_i = _RELOC_KIND_ORDER.index(kind)
        any_flag = 0 if unit == 0xFF else 1
        unit_desc = 0 if unit == 0xFF else -unit
        return (kind_i, mode, entry, any_flag, unit_desc, lsb)

    sites = [
        (kind, mode, entry, unit, lsb)
        for (kind, mode, entry, unit), lsb in collapsed.items()
    ]
    sites.sort(key=site_key)
    extra_rows = sorted(
        extras,
        key=lambda kv: (_RELOC_KIND_ORDER.index(kv[0]), kv[1]),
    )
    return sites, extra_rows


def _reloc_resolve_for_member(
    sites: Sequence[Tuple[str, int, int, int, int]],
    kind: str,
    mode: int,
    entry: int,
    unit: Optional[int],
) -> int:
    want = 0xFF if unit is None else unit
    wildcard: Optional[int] = None
    for k, m, e, u, lsb in sites:
        if k != kind or m != mode or e != entry:
            continue
        if want != 0xFF and u == want:
            return lsb
        if u == 0xFF:
            wildcard = lsb
    if wildcard is not None:
        return wildcard
    return _RELOC_TABLE_LSB[kind]


def _check_reloc_test_windows(
    sites: Sequence[Tuple[str, int, int, int, int]],
    extras: Sequence[Tuple[str, int]],
) -> None:
    published = {(k, lsb) for k, _m, _e, _u, lsb in sites}
    published.update(extras)
    for kind, lsb in _RELOC_TEST_PUBLISHED:
        table = _RELOC_TABLE_LSB[kind]
        if (kind, lsb) not in published and lsb != table:
            raise SystemExit(
                f"reloc FieldLsb: dropped RelocLayoutTest window {kind}@{lsb}"
            )
    for kind, lsb in _RELOC_TEST_UNPUBLISHED:
        table = _RELOC_TABLE_LSB[kind]
        if (kind, lsb) in published or lsb == table:
            raise SystemExit(
                f"reloc FieldLsb: unpublished RelocLayoutTest window {kind}@{lsb} "
                "became published"
            )
    for kind, mode, entry, unit, expect in _RELOC_TEST_MEMBER:
        got = _reloc_resolve_for_member(sites, kind, mode, entry, unit)
        if got != expect:
            raise SystemExit(
                f"reloc FieldLsb: resolveFieldLsbForMember({kind}, {mode}, "
                f"{entry}, {unit}) = {got} want {expect}"
            )
    # D1.17 qualified-kind ratchet: every (kind, mode, entry, unit) site
    # whose typed window differs from the base row must resolve to exactly
    # the minted qualified row's window, and no extra non-default site may
    # appear (a new golden admission fails here until a twin is minted).
    covered: Set[Tuple[str, int, int]] = set()
    for kind, mode, entry, unit, _name, _val, want_lsb in _RELOC_QUALIFIED_KINDS:
        got = _reloc_resolve_for_member(sites, kind, mode, entry, unit)
        if got != want_lsb or got == _RELOC_TABLE_LSB[kind]:
            raise SystemExit(
                f"reloc FieldLsb: qualified twin {kind}@({mode},{entry},"
                f"{unit}) resolves {got}, want the typed window {want_lsb} "
                "(non-default)"
            )
        covered.add((kind, mode, entry))
    for kind, mode, entry, unit, lsb in sites:
        if kind not in ("HI12", "CSR_UImm8"):
            continue
        if lsb == _RELOC_TABLE_LSB[kind] or (kind, mode, entry) in covered:
            continue
        raise SystemExit(
            f"reloc FieldLsb: non-default {kind}@({mode},{entry},{unit})"
            f"={lsb} has no entry-qualified ELF twin — mint one or drop "
            "the site"
        )


def _check_reloc_sniff_pins(cat: Catalog) -> None:
    """D1.17 generator ratchet: the C++ Loc-sniff opc pins separate members.

    For every (kind, mode, entry, unit) sniff site: the golden mapping at
    that site must host the pinned opcode(s), and every OTHER golden
    member at the same (mode, entry) sharing (map, type_code) — the
    predicate the pre-D1.17 sniff used alone — must have an opcode
    DISJOINT from the pin set. That is the generated proof the opc pin
    removes the D1.17 wrong-window ambiguity (BEQZ at E3 e1 ALU0 satisfied
    the LUI (map,type); ZERO_GPR at E3 e0 ALU0 satisfied the CSR
    (map,type)). ALU0 sites (and E2 e0) MUST have non-empty sharing —
    that golden fact is why the pins exist; if a future golden revision
    drops the sharing the pin becomes vacuous and this ratchet says so.
    """
    for kind, mode, entry, unit, pins in _RELOC_SNIFF_PIN:
        type_name = "I12" if kind == "HI12" else "I8"
        mode_s = "E3" if mode else "E2"
        site = None
        for rec in cat.members:
            if rec.is_nop or rec.mode != mode_s:
                continue
            if rec.entry_idx != entry or rec.type_name != type_name:
                continue
            if _RELOC_UNIT_INDEX.get(rec.unit) != unit:
                continue
            if rec.opcode in pins:
                site = rec
                break
        if site is None:
            raise SystemExit(
                f"reloc sniff pin: no {kind} member at ({mode},{entry},"
                f"{unit}) with opcode in {pins}"
            )
        sharing: Set[int] = set()
        for rec in cat.members:
            if rec.is_nop or rec.mode != mode_s:
                continue
            if rec.entry_idx != entry:
                continue
            if rec.unit_map != site.unit_map or rec.type_code != site.type_code:
                continue
            if rec.opcode in pins:
                continue
            sharing.add(rec.opcode)
        if sharing & set(pins):
            raise SystemExit(
                f"reloc sniff pin: {kind}@({mode},{entry},{unit}) pin set "
                f"{pins} collides with sharing opcodes {sorted(sharing)}"
            )
        # ALU0 I12/I8 sites (and E2 e0) host the branch/ZERO_* sharing in
        # golden v2_2 — the ambiguity the pin removes. Fail if it vanishes
        # so the pin's reason is re-audited rather than silently vacuous.
        if unit == 0 and not sharing:
            raise SystemExit(
                f"reloc sniff pin: {kind}@({mode},{entry},ALU0) expected "
                "non-empty (map,type) sharing (branches/ZERO_*); opc pin "
                "would be vacuous — re-audit golden"
            )


def emit_reloc_field_lsb_inc(cat: Catalog, json_sha: str, xlsx_sha: str, family) -> str:
    sites, extras = _collect_reloc_field_lsb(cat)
    _check_reloc_test_windows(sites, extras)
    _check_reloc_sniff_pins(cat)
    if not extras:
        raise SystemExit(
            "reloc FieldLsb: ExtraPublishedLsb empty (HWLRIII Off1/Off2 missing)"
        )
    lines: List[str] = []
    lines.append(
        "//===-- HaydnGenRelocFieldLsb.inc - reloc FieldLsb sites -*- C++ -*-===//"
    )
    lines.append("//")
    lines.extend(
        generated_banner(
            generator=RECORDS_GENERATOR,
            family=family,
            json_sha=json_sha,
            xlsx_sha=xlsx_sha,
        )
    )
    lines.append("//")
    lines.append("// Parcel-absolute FieldLsb from generated Format E members.")
    lines.append("// RelocKind / scale / ELF rows stay in HaydnRelocLayout.cpp.")
    lines.append("//===----------------------------------------------------------------------===//")
    lines.append("")
    lines.append("#ifdef GET_HAYDN_RELOC_FIELD_LSB")
    lines.append("#undef GET_HAYDN_RELOC_FIELD_LSB")
    lines.append("constexpr FieldLsbSite FieldLsbSites[] = {")
    last_kind = None
    for kind, mode, entry, unit, lsb in sites:
        comment = _RELOC_KIND_COMMENT.get(kind)
        if comment and kind != last_kind:
            lines.append(f"    // {comment}")
            last_kind = kind
        elif kind != last_kind:
            last_kind = kind
        tok = _RELOC_UNIT_TOKEN.get(unit)
        if tok is None:
            raise SystemExit(f"reloc FieldLsb: no token for unit {unit}")
        lines.append(
            f"    {{RelocKind::{kind}, {mode}, {entry}, {tok}, {lsb}}},"
        )
    lines.append("};")
    lines.append("")
    lines.append("constexpr ExtraLsb ExtraPublishedLsb[] = {")
    for kind, lsb in extras:
        lines.append(f"    {{RelocKind::{kind}, {lsb}}},")
    lines.append("};")
    lines.append("#endif // GET_HAYDN_RELOC_FIELD_LSB")
    lines.append("")
    return "\n".join(lines) + "\n"


#//===---------------------------------------------------------------------===//
# Direct-setDesc identity check (build-time, generator-side)
#//===---------------------------------------------------------------------===//
#
# Contract: every compiler-reachable logical opcode must be operand-shape
# IDENTICAL to every generated Format E member reachable by a late
# MI.setDesc — same operand count, same def count, same per-index kind,
# same tie index-pair set, same execution flags. No runtime permutation
# table, no SemanticCompatibilityID, no emitted records: the check fails
# generation closed and the census below ratchets monotonically toward
# empty as logical schemas are aligned.
#
# This block parses the LOGICAL TableGen side (class defaults, prefix
# lets, def headers) and compares against the member shape the generator
# itself constructs (layouts + operand_fields). Hand-assembly-only defs
# (isCodeGenOnly/isAsmParserOnly) are exempt: they never ride setDesc in
# the compiler lane.

TD_OP_RE = re.compile(r"([A-Za-z0-9_]+):\$([A-Za-z0-9_]+)")
TD_DEF_RE = re.compile(r"^def\s+([A-Za-z0-9_]+)\s*:")
# Two-line def header (`def NAME\n    : Parent<…>;`): the parse-time regex
# accepts an end-of-line terminator so the parent on the next line is seen.
# (The MULTILINE collector regex at collect_td_def_names already handles
# this shape via findall and must keep matching both spellings.)
TD_DEF_TAIL_RE = re.compile(r"^def\s+([A-Za-z0-9_]+)\s*$")
TD_CLASS_RE = re.compile(r"^class\s+([A-Za-z0-9_]+)\b")
TD_PARENT_RE = re.compile(r":\s*([A-Za-z0-9_]+)\s*<")
TD_LET_IN_RE = re.compile(r"\blet\b(.+)\bin\s*\{")
TD_LET_SEMI_RE = re.compile(r"\blet\s+([A-Za-z0-9_]+)\s*=\s*(.+?)\s*;")

# Ratchet: compiler-reachable logicals still divergent from their members.
# Monotone shrink only — a logical leaving the set requires re-pinning
# (smaller); any NEW name fails generation immediately.
#
# Current membership rationale: EMPTY since 2026-08-26 (CB-151 reshape).
# Every compiler-reachable logical is operand-shape identical to every
# generated member it setDescs onto. Departures, all 2026-08-26:
#   * SET_HWLOOP_REG — census parser now threads class defaults across
#     include-ordered texts and ends a header at a body-opening `{`, so
#     the HaydnPseudo class default isCodeGenOnly=1 reaches the def (the
#     exemption matches the real TableGen surface; product creator emits
#     SET_HWLOOP_F2_W directly).
#   * CSRR — 3-op FmtCSR decoder-parity shell shrank to the catalog 2-op
#     HaydnInst<0> isPseudo shape (FmtCSR deleted; Haydn32 trie was never
#     consulted for decode).
#   * D_L*UA_POST / D_S*UA_POST / WBARWUA — CB-151 reshape: logicals now
#     carry the golden member wire shape (loads (rtd, rs_wb; ar_sel, rs),
#     stores (rs_wb; ar_sel, rtd, rs), wbarwua (ar_sel, rs)); stride and
#     dir_sel fold at ISel (golden: rs = rs+8, direction in rs[2:1]).
EXPECTED_IDENTITY_DIVERGENT: frozenset = frozenset({})

#//===---------------------------------------------------------------------===//
# Universal singleton coverage census (compiler-reachable direction)
#//===---------------------------------------------------------------------===//
#
# Contract (PIPE-20): every compiler-reachable TableGen instruction def —
# Instruction-derived (lineage, not name), not isCodeGenOnly, not
# isAsmParserOnly — maps through the ONE compiler peel law to a non-empty
# catalog alt span, or sits in the pinned ratchet set below. A hand-added
# logical escaping ExpandPseudos with no catalog span fails generation
# here and the HaydnTests walk at build/test time — not as the post-RA
# "no generated member" fatal (HaydnBundleVerify.cpp).
#
# The TD-name → catalog-logical normalization is pinned HERE (one seat,
# extending td_logical_aliases in collect_member_to_logical). Each family
# is mirrored by a peel-parity pin in HaydnFormatERecordsTest
# (CompilerReachableLogicalsHaveSingletonCoverage) so this table and the
# compiler's peelLogicalOpcodeName cannot drift silently: an alias added
# to the C++ peel without this table fails this census closed, and vice
# versa. Any def the table cannot map fails the census — never a silent
# invented mapping.
SINGLETON_PEEL_ALIASES: Dict[str, str] = {
    # WIDE reloc spellings peel their trailing _W / _F2_W (StripWide).
    "SET_HWLOOP_F2_W": "SET_HWLOOP_F2",
    "WFITBDTBDTBD": "WFI<TBD>",
}
_SINGLETON_SUFFIX_PEELS: Tuple[Tuple[str, str], ...] = (
    # Order mirrors peelLogicalOpcodeName: _M0S0LS-family strips first
    # (looped), then _MSP, then PLDWWUA rename, then _W/_F2_W.
    ("_M0S0LS", ""),
    ("_M0S1LS", ""),
    ("_M0S2LS", ""),
    ("_M1S0LS", ""),
    ("_M1S1LS", ""),
    ("_M1S2LS", ""),
)
_SINGLETON_EXACT_PEELS: Dict[str, str] = {
    # User LS spellings → catalog member logicals (one peel law).
    "LD32": "S_LW_WITH_IMM",
    "LD32_REG": "S_LW_WITH_REG",
    "ST32": "S_SW_WITH_IMM",
    "ST32_REG": "S_SW_WITH_REG",
    "LD64": "D_LDW_WITH_IMM",
    "LD64_REG": "D_LDW_WITH_REG",
    "ST64": "D_SDW_WITH_IMM",
    "ST64_REG": "D_SDW_WITH_REG",
    "LD8": "S_LBS_WITH_IMM",
    "LD8_REG": "S_LBS_WITH_REG",
    "LDU8": "S_LBU_WITH_IMM",
    "LDU8_REG": "S_LBU_WITH_REG",
    "ST8": "S_SB_WITH_IMM",
    "ST8_REG": "S_SB_WITH_REG",
    "LD16": "S_LHWS_WITH_IMM",
    "LD16_REG": "S_LHWS_WITH_REG",
    "LDU16": "S_LHWU_WITH_IMM",
    "LDU16_REG": "S_LHWU_WITH_REG",
    "ST16": "S_SHW_WITH_IMM",
    "ST16_REG": "S_SHW_WITH_REG",
    "LD32_POST": "S_LW_POST_IMM",
    "ST32_POST": "S_SW_POST_IMM",
    "LD32_PRE": "S_LW_PRE_IMM",
    "ST32_PRE": "S_SW_PRE_IMM",
    "LD64_POST": "D_LDW_POST_IMM",
    "ST64_POST": "D_SDW_POST_IMM",
    "PLDWWUA": "PLDWWUA_POST",
    # DR64-bank move family folds onto the sext catalog logical.
    "SEXT_GPR32_TO_DR64": "SEXT32T64",
    "MOV_GPR_TO_DR64": "SEXT32T64",
    "MOVE_GPR_TO_DR64": "SEXT32T64",
    "ZEXT_GPR32_TO_DR64": "SEXT32T64",
    "RET": "JALR",
    "WFI": "WFI<TBD>",
}
# NOP is exempt from alt spans by construction (is_nop rows never enter
# cat.alternatives; PIN_UNIQUE_NON_NOP pins that boundary). Its coverage
# is the NOP-completion law itself: NOP members exist in both modes, so
# the idle parcel completes any singleton packet. The unittest pins that
# fact directly (FormatENopCompletionModes == 0b11).
SINGLETON_IDLE_PARCEL_EXEMPT: frozenset = frozenset({"NOP"})

# Ratchet: compiler-reachable logicals still without catalog singleton
# coverage. Monotone shrink only — a name leaving the set requires
# re-pinning (smaller); any NEW name fails generation immediately in BOTH
# emit and --check modes. EMPTY since installation (2026-08-31 GR2.2):
# the measured residue over the five authored TD files + golden defs is
# entirely alias spellings the one peel law maps.
EXPECTED_SINGLETON_UNCOVERED: frozenset = frozenset({})


def peel_logical_name(name: str) -> str:
    """Python mirror of the compiler's peelLogicalOpcodeName for the
    census residue only. Pinned families are exhaustive + fail-closed:
    any def whose peeled name has no catalog span fails the census (never
    silently maps). Each family carries a parity pin in
    HaydnFormatERecordsTest so the two seats stay one law."""
    base = SINGLETON_PEEL_ALIASES.get(name, name)
    if name in SINGLETON_PEEL_ALIASES:
        return base
    # _M<n>S<m>LS occupancy-class strips (looped like the 3-pass C++ peel).
    for _ in range(3):
        before = base
        for suf, _rep in _SINGLETON_SUFFIX_PEELS:
            if base.endswith(suf):
                base = base[: -len(suf)]
        if base == before:
            break
    if base.endswith("_MSP"):
        base = base[:-4]
    if base == "PLDWWUA":
        base = "PLDWWUA_POST"
    if base.endswith("_F2_W"):
        base = base[:-2]
    elif base.endswith("_W"):
        base = base[:-2]
    if base in _SINGLETON_EXACT_PEELS:
        return _SINGLETON_EXACT_PEELS[base]
    return base


def singleton_uncovered_census(
    cat: Catalog,
    logical_schemas: Dict[str, TDInstSchema],
) -> List[str]:
    """Sorted compiler-reachable def names with no catalog coverage.

    Compiler-reachable = Instruction-lineage def, not isCodeGenOnly, not
    isAsmParserOnly. NOP admits via the idle-parcel law; everything else
    must peel to a non-empty cat.alternatives span or join the census.
    """
    alts = set(cat.alternatives)
    uncovered: Set[str] = set()
    for name, schema in logical_schemas.items():
        if not schema.is_instruction:
            continue
        if schema.is_codegen_only or schema.is_asm_parser_only:
            continue
        if name in SINGLETON_IDLE_PARCEL_EXEMPT:
            continue
        if peel_logical_name(name) not in alts:
            uncovered.add(name)
    return sorted(uncovered)


@dataclass
class TDInstOp:
    cls: str
    name: str
    is_def: bool


@dataclass
class TDInstSchema:
    name: str
    ops: Tuple[TDInstOp, ...]
    ties: Tuple[Tuple[str, str], ...]
    itinerary: str
    may_load: int = 0
    may_store: int = 0
    is_branch: int = 0
    is_terminator: int = 0
    is_call: int = 0
    is_indirect_branch: int = 0
    is_commutable: int = 0
    has_side_effects: int = 0
    is_barrier: int = 0
    implicit_defs: Tuple[str, ...] = ()
    implicit_uses: Tuple[str, ...] = ()
    is_codegen_only: int = 0
    is_asm_parser_only: int = 0
    # Instruction-lineage (def's parent chain reaches the TableGen
    # `Instruction` root). Operand/ImmLeaf defs (simm*, uimm*, brtarget,
    # HaydnMem*) are NOT instructions; the singleton census excludes them
    # by lineage, never by name.
    is_instruction: bool = True


def _strip_td_line(raw: str) -> str:
    out: List[str] = []
    in_str = False
    i = 0
    while i < len(raw):
        ch = raw[i]
        if ch == '"':
            in_str = not in_str
            out.append(ch)
            i += 1
            continue
        if not in_str and ch == "/" and i + 1 < len(raw) and raw[i + 1] == "/":
            break
        out.append(ch)
        i += 1
    return "".join(out).rstrip()


def _split_td_props(blob: str) -> Dict[str, str]:
    props: Dict[str, str] = {}
    depth = 0
    token: List[str] = []
    in_str = False
    for ch in blob:
        if ch == '"':
            in_str = not in_str
            token.append(ch)
            continue
        if not in_str:
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth = max(0, depth - 1)
            elif ch == "," and depth == 0:
                piece = "".join(token).strip()
                token = []
                if "=" in piece:
                    k, v = piece.split("=", 1)
                    props[k.strip()] = v.strip()
                continue
        token.append(ch)
    piece = "".join(token).strip()
    if piece and "=" in piece:
        k, v = piece.split("=", 1)
        props[k.strip()] = v.strip()
    return props


def _parse_name_list(value: str) -> Tuple[str, ...]:
    inner = value.strip()
    if inner.startswith("[") and inner.endswith("]"):
        inner = inner[1:-1]
    names = [n.strip() for n in inner.split(",") if n.strip()]
    return tuple(names)


def _parse_ties(value: str) -> Tuple[Tuple[str, str], ...]:
    raw = value.strip().strip('"')
    if not raw:
        return ()
    out: List[Tuple[str, str]] = []
    for part in raw.split(","):
        if "=" not in part:
            continue
        a, b = part.split("=", 1)
        out.append((a.strip().lstrip("$"), b.strip().lstrip("$")))
    return tuple(out)


def _apply_td_props(base: Dict[str, Any], props: Dict[str, str]) -> Dict[str, Any]:
    cur = dict(base)
    for key, val in props.items():
        low = val.lower()
        if key == "Itinerary":
            cur["itinerary"] = val.strip()
        elif key == "mayLoad":
            cur["may_load"] = 0 if low in ("0", "false") else 1
        elif key == "mayStore":
            cur["may_store"] = 0 if low in ("0", "false") else 1
        elif key == "isBranch":
            cur["is_branch"] = 0 if low in ("0", "false") else 1
        elif key == "isTerminator":
            cur["is_terminator"] = 0 if low in ("0", "false") else 1
        elif key == "isCall":
            cur["is_call"] = 0 if low in ("0", "false") else 1
        elif key == "isIndirectBranch":
            cur["is_indirect_branch"] = 0 if low in ("0", "false") else 1
        elif key == "isCommutable":
            cur["is_commutable"] = 0 if low in ("0", "false") else 1
        elif key == "hasSideEffects":
            cur["has_side_effects"] = 0 if low in ("0", "false") else 1
        elif key == "isBarrier":
            cur["is_barrier"] = 0 if low in ("0", "false") else 1
        elif key == "isCodeGenOnly":
            cur["is_codegen_only"] = 0 if low in ("0", "false") else 1
        elif key == "isAsmParserOnly":
            cur["is_asm_parser_only"] = 0 if low in ("0", "false") else 1
        elif key == "Defs":
            cur["implicit_defs"] = _parse_name_list(val)
        elif key == "Uses":
            cur["implicit_uses"] = _parse_name_list(val)
        elif key == "Constraints":
            cur["ties"] = _parse_ties(val)
    return cur


def _extract_dags(header: str) -> Tuple[List[TDInstOp], List[TDInstOp]]:
    def grab(tag: str) -> str:
        token = f"({tag}"
        start = header.find(token)
        if start < 0:
            return ""
        depth = 0
        for i, ch in enumerate(header[start:]):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    return header[start : start + i + 1]
        return ""

    def ops(blob: str, is_def: bool) -> List[TDInstOp]:
        if not blob:
            return []
        found = TD_OP_RE.findall(blob)
        return [TDInstOp(cls=c, name=n, is_def=is_def) for c, n in found]

    return ops(grab("outs"), True), ops(grab("ins"), False)


def _empty_schema_props() -> Dict[str, Any]:
    return {
        "itinerary": "NoItinerary",
        "may_load": 0,
        "may_store": 0,
        "is_branch": 0,
        "is_terminator": 0,
        "is_call": 0,
        "is_indirect_branch": 0,
        "is_commutable": 0,
        "has_side_effects": 0,
        "is_barrier": 0,
        "implicit_defs": (),
        "implicit_uses": (),
        "ties": (),
        "is_codegen_only": 0,
        "is_asm_parser_only": 0,
    }


def parse_td_schemas(
    text: str,
    class_defaults: Optional[Dict[str, Dict[str, Any]]] = None,
    class_parents: Optional[Dict[str, Optional[str]]] = None,
) -> Tuple[Dict[str, Dict[str, Any]], Dict[str, TDInstSchema]]:
    """Parse class defaults and named instruction schemas from TD text.

    Handles prefix `let ... in { }` groups, single-def `let ...;` bodies,
    and class-default inheritance (one parent level, enough for the
    Haydn logical shells). class_defaults carries inherited class
    defaults across separately-parsed texts (caller compiles one
    include-ordered unit); None starts fresh. class_parents threads the
    class→parent chain the same way so Instruction-lineage (vs Operand/
    ImmLeaf defs) survives across per-file parse."""
    if class_defaults is None:
        class_defaults = {
            "Instruction": _empty_schema_props(),
            "HaydnInst": _empty_schema_props(),
        }
    else:
        class_defaults = dict(class_defaults)
        class_defaults.setdefault("Instruction", _empty_schema_props())
        class_defaults.setdefault("HaydnInst", _empty_schema_props())
    # class → parent-name chain for Instruction-lineage resolution. The
    # first parse (fresh None) seeds the root; a threaded map is mutated
    # in place so every text in the include-ordered unit sees the chain.
    if class_parents is None:
        class_parents = {"Instruction": None}
    else:
        class_parents.setdefault("Instruction", None)
    schemas: Dict[str, TDInstSchema] = {}
    lines = [_strip_td_line(ln) for ln in text.splitlines()]
    depth = 0
    let_stack: List[Tuple[int, Dict[str, str]]] = []
    pending_let: Dict[str, str] = {}
    let_buf: List[str] = []
    i = 0
    n = len(lines)

    def active_props() -> Dict[str, str]:
        merged: Dict[str, str] = {}
        for _, props in let_stack:
            merged.update(props)
        merged.update(pending_let)
        return merged

    def is_instruction_class(name: str) -> bool:
        # Lineage by class-parent chain rooted at `Instruction`. The
        # class_defaults map keys every class seen in include order; a
        # parent outside it (Operand, ImmLeaf roots in Target.td) is not
        # an instruction — fail-closed to False, never a name guess.
        seen: Set[str] = set()
        cur: Optional[str] = name
        while cur and cur not in seen:
            seen.add(cur)
            if cur == "Instruction":
                return True
            cur = class_parents.get(cur)
        return False

    def take_header(start: int, first: str) -> Tuple[str, int]:
        buf = [first]
        j = start
        while j < n:
            joined = "\n".join(buf)
            if "> {" in joined or re.search(r">\s*;", joined):
                return joined, j
            # A line ending in `{` opens the body: the header is done even
            # when the parent has no template args (`: Instruction {`) —
            # without this the scan swallows the NEXT class header (e.g.
            # HaydnPseudo into HaydnInst) and the swallowed class is never
            # registered.
            if j > start and lines[j].rstrip().endswith("{"):
                return joined, j
            j += 1
            if j < n:
                buf.append(lines[j])
        return "\n".join(buf), start

    while i < n:
        ln = lines[i]
        if let_buf or (ln.strip().startswith("let ") and "let " in ln):
            let_buf.append(ln)
            joined = " ".join(let_buf)
            if re.search(r"\bin\s*\{", joined):
                let_m = TD_LET_IN_RE.search(joined)
                props = _split_td_props(let_m.group(1) if let_m else joined)
                opens = joined.count("{") - joined.count("}")
                depth += opens
                let_stack.append((depth, props))
                let_buf = []
                i += 1
                continue
            if re.search(r"\bin\s*$", joined):
                head = re.sub(r"^let\s+", "", joined)
                head = re.sub(r"\bin\s*$", "", head)
                pending_let = _split_td_props(head)
                let_buf = []
                i += 1
                continue
            if "in" not in joined:
                i += 1
                continue
            let_buf = []
        class_m = TD_CLASS_RE.match(ln.strip())
        def_m = TD_DEF_RE.match(ln.strip())
        if def_m is None and i + 1 < n:
            # Two-line def header: `def NAME` alone, parent on the next
            # line. Only continue when the NEXT line actually opens a
            # parent clause; otherwise fall through (blank/comment tails
            # are not def headers).
            tail_m = TD_DEF_TAIL_RE.match(ln.strip())
            if tail_m and lines[i + 1].lstrip().startswith(":"):
                def_m = re.match(
                    r"^def\s+([A-Za-z0-9_]+)\s*:", ln.strip() + " :"
                )
        if class_m:
            header, end_i = take_header(i, ln)
            parent_m = TD_PARENT_RE.search(header)
            parent = parent_m.group(1) if parent_m else "Instruction"
            props = dict(class_defaults.get(parent, _empty_schema_props()))
            body_lines: List[str] = []
            j = end_i
            local_depth = header.count("{") - header.count("}")
            started = local_depth > 0
            while j + 1 < n and (not started or local_depth > 0):
                j += 1
                raw = lines[j]
                local_depth += raw.count("{") - raw.count("}")
                started = True
                body_lines.append(raw)
                if started and local_depth <= 0:
                    break
            body = "\n".join(body_lines)
            for sm in TD_LET_SEMI_RE.finditer(body):
                props = _apply_td_props(props, {sm.group(1): sm.group(2)})
            class_defaults[class_m.group(1)] = props
            class_parents[class_m.group(1)] = parent
            depth += header.count("{") - header.count("}")
            for raw in body_lines:
                depth += raw.count("{") - raw.count("}")
            while let_stack and let_stack[-1][0] > depth:
                let_stack.pop()
            i = j + 1 if body_lines else end_i + 1
            continue
        if def_m:
            header, end_i = take_header(i, ln)
            parent_m = TD_PARENT_RE.search(header)
            parent = parent_m.group(1) if parent_m else "HaydnInst"
            props = dict(class_defaults.get(parent, _empty_schema_props()))
            props = _apply_td_props(props, active_props())
            outs, ins = _extract_dags(header)
            body_lines = []
            j = end_i
            local_depth = header.count("{") - header.count("}")
            if local_depth > 0:
                while j + 1 < n and local_depth > 0:
                    j += 1
                    raw = lines[j]
                    local_depth += raw.count("{") - raw.count("}")
                    body_lines.append(raw)
                    if local_depth <= 0:
                        break
            for sm in TD_LET_SEMI_RE.finditer("\n".join(body_lines)):
                props = _apply_td_props(props, {sm.group(1): sm.group(2)})
            name = def_m.group(1)
            schemas[name] = TDInstSchema(
                name=name,
                ops=tuple(outs + ins),
                ties=tuple(props.get("ties") or ()),
                itinerary=str(props.get("itinerary") or "NoItinerary"),
                may_load=int(props.get("may_load") or 0),
                may_store=int(props.get("may_store") or 0),
                is_branch=int(props.get("is_branch") or 0),
                is_terminator=int(props.get("is_terminator") or 0),
                is_call=int(props.get("is_call") or 0),
                is_indirect_branch=int(props.get("is_indirect_branch") or 0),
                is_commutable=int(props.get("is_commutable") or 0),
                has_side_effects=int(props.get("has_side_effects") or 0),
                is_barrier=int(props.get("is_barrier") or 0),
                implicit_defs=tuple(props.get("implicit_defs") or ()),
                implicit_uses=tuple(props.get("implicit_uses") or ()),
                is_codegen_only=int(props.get("is_codegen_only") or 0),
                is_asm_parser_only=int(props.get("is_asm_parser_only") or 0),
                is_instruction=is_instruction_class(parent),
            )
            pending_let = {}
            depth += header.count("{") - header.count("}")
            for raw in body_lines:
                depth += raw.count("{") - raw.count("}")
            while let_stack and let_stack[-1][0] > depth:
                let_stack.pop()
            i = j + 1 if body_lines else end_i + 1
            continue
        depth += ln.count("{") - ln.count("}")
        while let_stack and let_stack[-1][0] > depth:
            let_stack.pop()
        i += 1
    return class_defaults, schemas


def load_logical_schemas(
    td_dir: Path, extra_texts: Sequence[str]
) -> Tuple[Dict[str, TDInstSchema], Dict[str, Optional[str]]]:
    texts: List[str] = []
    for fn in (
        "HaydnInstrFormats.td",
        "HaydnInstrFormatsC.td",
        "HaydnInstrInfo.td",
        "HaydnGISel.td",
        "HaydnPseudos.td",
    ):
        path = td_dir / fn
        if path.is_file():
            texts.append(path.read_text(encoding="utf-8"))
    texts.extend(extra_texts)
    schemas: Dict[str, TDInstSchema] = {}
    # TableGen compiles these files as one include-ordered unit; class
    # defaults (HaydnPseudo isCodeGenOnly=1 lives in HaydnInstrFormats.td,
    # HaydnPseudos.td defs inherit it) must survive across per-file parse.
    # Thread one class_defaults map through every text so census exemptions
    # see the same flags the real MCInstrDesc carries. class_parents rides
    # the same thread for Instruction-lineage resolution.
    classes: Dict[str, Dict[str, Any]] = {}
    parents: Dict[str, Optional[str]] = {"Instruction": None}
    for text in texts:
        classes, found = parse_td_schemas(text, classes, parents)
        schemas.update(found)
    return schemas, parents


def _td_op_is_reg(cls: str) -> bool:
    c = cls.strip().lower()
    return c in (
        "gpr32", "gpr", "dr64", "dr", "ar", "ar64", "sfr",
    )


def _td_tie_index_pairs(schema: TDInstSchema) -> List[Tuple[int, int]]:
    names = {op.name: i for i, op in enumerate(schema.ops)}
    pairs: List[Tuple[int, int]] = []
    for a, b in schema.ties:
        if a in names and b in names:
            pairs.append((names[a], names[b]))
    return sorted(pairs)


def check_setdesc_identity(
    cat: Catalog,
    member_schemas: Dict[str, TDInstSchema],
    logical_schemas: Dict[str, TDInstSchema],
    member_to_logical: Dict[str, str],
) -> List[str]:
    """Errors for compiler-reachable logical/member pairs that are not
    operand-shape identical. Members are read from the EMITTED member TD
    (members_td, parsed back with parse_td_schemas — the exact text
    TableGen consumes), so outs/ins/ties parity with the real Desc is
    guaranteed. Empty list = goal state."""
    errors: List[str] = []
    for member_symbol, logical in sorted(member_to_logical.items()):
        l_schema = logical_schemas.get(logical)
        m_schema = member_schemas.get(member_symbol)
        if l_schema is None or m_schema is None:
            # collect_member_to_logical skips non-opcode logicals
            # fail-closed; a missing member schema means the symbol is not
            # in the emitted members text (NOP rows) — nothing to compare.
            continue
        if l_schema.is_codegen_only or l_schema.is_asm_parser_only:
            continue
        l_ops = l_schema.ops
        m_ops = m_schema.ops
        if len(l_ops) != len(m_ops):
            errors.append(
                f"{logical} vs {member_symbol}: operand count "
                f"{len(l_ops)} vs {len(m_ops)}"
            )
            continue
        for idx, (lo, mo) in enumerate(zip(l_ops, m_ops)):
            lk = "reg" if _td_op_is_reg(lo.cls) else "imm"
            mk = "reg" if _td_op_is_reg(mo.cls) else "imm"
            if lk != mk or lo.is_def != mo.is_def:
                errors.append(
                    f"{logical} vs {member_symbol}: operand {idx} "
                    f"{lk}{'d' if lo.is_def else 'u'} vs "
                    f"{mk}{'d' if mo.is_def else 'u'}"
                )
                break
        else:
            if _td_tie_index_pairs(l_schema) != _td_tie_index_pairs(m_schema):
                errors.append(
                    f"{logical} vs {member_symbol}: tie index pairs "
                    f"{_td_tie_index_pairs(l_schema)} vs "
                    f"{_td_tie_index_pairs(m_schema)}"
                )
    return errors


def identity_divergent_census(
    cat: Catalog,
    member_schemas: Dict[str, TDInstSchema],
    logical_schemas: Dict[str, TDInstSchema],
    member_to_logical: Dict[str, str],
) -> List[str]:
    """Sorted logical names that have at least one non-identity placement."""
    divergent = set()
    for err in check_setdesc_identity(
        cat, member_schemas, logical_schemas, member_to_logical
    ):
        divergent.add(err.split(" vs ")[0])
    return sorted(divergent)


def operand_signature(rec: MemberRecord, layouts: Dict[int, TypeLayout]) -> str:
    lay = layouts[rec.layout_id]
    parts = []
    for of, (_role, active) in zip(lay.operand_fields, rec.operand_active):
        alias = active if active else "|".join(of.aliases)
        parts.append(f"{of.role}:{alias}:{of.bits.width}")
    return ";".join(parts)


def emit_setdesc_ledger_inc(cat: Catalog, family) -> str:
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
    lines.extend(generated_banner(generator=RECORDS_GENERATOR, family=family))
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
    lines.append("  uint8_t Family; // BundleFamily; E96=0")
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
            f"{family.id}, "
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
DSPLY2_LOGICALS = frozenset({"LOG2", "EXP2", "RECIP", "SQRT"})

# Fixed Data_Latency = 2 logicals (2026-08-21 latency P0/P1,
# golden instruction_type_index Pipeline_Info): the four DSP-unary
# LUT-interpolation ops and the CSR read. Members pin one unit; the
# per-slot DspLat/CsrLat classes keep OperandCycles [2] so a consumer in
# the next cycle reads stale data on this no-interlock machine.
DSPLY2_ITINERARY = {
    "ALU1": "Slot1_ALU_DspLat",
    "ALU2": "Slot2_ALU_DspLat",
}
CSR_LY2_ITINERARY = {
    "ALU0": "Slot0_ALU_CsrLat",
    "ALU1": "Slot1_ALU_CsrLat",
    "ALU2": "Slot2_ALU_CsrLat",
}

# Golden Data_Latency = 1 surfaces (2026-08-21 latency P3,
# gaps/audit_latency.md mismatch #3/#4 + the post-landing correction):
# store-with-writeback registers and fresh-dest (non-accumulating)
# multiplies. The member sets are DERIVED PER-ROW from golden
# instruction_type_index Pipeline_Info (load_store_writeback_logicals /
# load_mul_lat1_logicals) — never the audit's family list, which the
# correction falsified for the mul side (X4MUL16/X2FMUL32*/X2CMUL32X16*/
# F2MULZAA* are golden lat-2 and stay 2; tightening a golden-2 row would
# be aggressive-wrong silent code on this no-interlock machine).
# Itinerary rows: Slot0_LS_WbLat (LOADSTORE0, OperandCycles [1],
# MemoryCycle pair unchanged) and Slot12_MAC_MulLat + per-slot member
# rows (OperandCycles [1,1,1,1]) published by generate_sched_records.py.
STWB_ITINERARY = "Slot0_LS_WbLat"
MUL_LAT1_ITINERARY = {
    "MAC0": "Slot1_MAC_MulLat",
    "MAC1": "Slot2_MAC_MulLat",
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
# Compact and `_W` share byte PC+imm. Generated members use the WIDE
# PCRel operand class so reloc-bearing `_W` forms cut over. AsmString stays
# the golden name (`jal`); llc after cutover matches objdump.
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
# Compact and `_W` share the Format E RI20 field. Generated members
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
# catalog: those user spellings are not 3-GPR members.
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
# v2_2: AR_CBR joins — the CB writeback UA loads/stores advance CB state
# (rs base wrap + ar[ar_sel] update) beyond mayLoad/mayStore.
SIDE_EFFECT_TYPES = frozenset(
    {
        "SFR", "HINT", "HWLRIII", "HWLRIIR", "HWLRRRR",
        "AR", "CBRI", "CBRR", "AR_CBR",
    }
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
    # 2026-08-21 (gaps/audit_shapes.md): compare logicals write SFR as
    # their ONLY semantic output; members must declare the implicit def
    # (Defs = [SFR]) like the authored logicals (HaydnInstrInfo.td
    # SEQ64/X2SEQ32... `let Defs = [SFR]`), keeping hasSideEffects = 0 so
    # SMS does not serialize them as memory barriers.
    implicit_defs: Tuple[str, ...] = ()

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
        if self.implicit_defs:
            parts.append(
                "Defs = [" + ", ".join(self.implicit_defs) + "]"
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
    for fn in ("HaydnInstrInfo.td", "HaydnInstrInfoGolden.td.inc"):
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


def ls_has_tied_base_writeback(
    logical: str, gpr_ties: Optional[Tuple[str, ...]] = None
) -> bool:
    """LS base registers that this logical writes back (`$rs = $rs_wb`).

    Law (2026-08-21, golden GPR Write∩Read port tie, gaps/audit_shapes.md
    class (d')): an LS logical whose golden GPR_Write_Port alias is also in
    GPR_Read_Port updates its encoded dest2 base — the synthetic tied OUT
    `dest2_wb` must exist on every member. That single predicate covers
    all four spellings the name-tag form missed:
      - infix tags `_POST_`/`_PRE_`/`_BREV_` (70 sibling logicals)
      - suffix `_POST` (AR-ua families D_LQHWUA_POST etc. — the tag
        `_POST_` never matches a trailing `_POST`)
      - untagged CB families (D_LDW_CB_IMM/REG, D_SDW_CB_IMM/REG —
        circular-buffer base wrap has no POST/PRE/BREV token at all)
    `gpr_ties` is the golden-derived tie tuple from
    load_accumulator_ties (GPR bank only). When absent the historical
    name-tag check is kept as a fallback so probe/pin paths that call
    without the index still behave.
    """
    key = _logical_key(logical)
    if gpr_ties is not None:
        return any(a.startswith("rs") for a in gpr_ties)
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


def member_gpr_is_ssa_def(
    logical: str,
    role: str,
    is_ls: bool,
    dr_readonly: Optional[set] = None,
) -> bool:
    """Whether a generated member GPR/DR wire is an LLVM SSA def.

    ALU/MAC dest* are defs. LS dest* follow ls_dest_is_ssa_def. Catalog
    role `reg` (alias rt) is a def for JALR (link) and DEST_REG_LOGICALS
    (LUI/ZERO_GPR/ZERO_DR/CSRR/MOVESFR2GPR). Do not treat branch `reg`
    (rs), CSRW, or MOVEGPR2SFR as a def.

    2026-08-21 (gaps/audit_shapes.md "Scalar trio"): a logical whose
    golden DR_Write_Port is empty writes no DR at all — its dest-spelled
    layout roles (the R sheet spells rsd1 `dest`) are DR READS like the
    ALU2 src1/src2 spellings. Covers the nine SFR compares' ALU0/ALU1
    unary members; `dr_readonly` is the golden-derived law (see
    load_dr_readonly_logicals), no hand list.
    """
    if role.startswith("dest"):
        if dr_readonly is not None and _logical_key(logical) in dr_readonly:
            return False
        return (not is_ls) or ls_dest_is_ssa_def(logical, role)
    if role != "reg":
        return False
    key = _logical_key(logical)
    return key in INDIRECT_CALL_LOGICALS or key in DEST_REG_LOGICALS


MAC_ACCFIRST_ITINERARY = {
    "MAC0": "Slot1_MAC_AccFirst",
    "MAC1": "Slot2_MAC_AccFirst",
}


def load_dr_readonly_logicals(index_path: Path) -> set:
    """Golden SFR-compare law: a logical whose SFR_Write_Port is non-empty
    and DR_Write_Port is empty writes NO data register — its only semantic
    output is SFR, so every DR wire (including the R-sheet `dest`-spelled
    rsd1) is a READ (2026-08-21, gaps/audit_shapes.md "Scalar trio").
    Derived from instruction_type_index.json — no hand list. Census: the
    9 SFR compares (SEQ64/SLE64/SLT64, X2SEQ/SLE/LT32, X4SEQ/SLE/LT16)
    plus MOVEGPR2SFR / ZERO_SFR (no DR wires at all — membership is a
    pin, not a behavior change for them). Store/NSA/POPCOUNT families
    are also DR-read-only but spell their DR roles `src`/data uses
    already; they are excluded because they do not write SFR."""
    idx = json.loads(index_path.read_text(encoding="utf-8"))
    readonly: set = set()
    for type_recs in idx.values():
        for rec in type_recs:
            name = rec.get("Instruction")
            if not name:
                continue
            if (rec.get("SFR_Write_Port") or []) and not (
                rec.get("DR_Write_Port") or []
            ):
                readonly.add(_logical_key(name))
    return readonly


def load_sfr_writers(index_path: Path) -> set:
    """Golden SFR-writer law: logicals whose SFR_Write_Port is non-empty
    declare an implicit SFR def on every member (2026-08-21,
    gaps/audit_shapes.md). Derived from instruction_type_index.json — no
    hand list. Census: the 9 compares (SEQ64/SLE64/SLT64, X2SEQ/SLE/LT32,
    X4SEQ/SLE/LT16) plus MOVEGPR2SFR and ZERO_SFR (whose types SFR/I8
    already carry hasSideEffects=1; the implicit def is added anyway —
    an explicit def keeps the SFR single-writer bundle law checkable at
    member level for them too)."""
    idx = json.loads(index_path.read_text(encoding="utf-8"))
    writers: set = set()
    for type_recs in idx.values():
        for rec in type_recs:
            name = rec.get("Instruction")
            if name and (rec.get("SFR_Write_Port") or []):
                writers.add(_logical_key(name))
    return writers


def load_store_writeback_logicals(index_path: Path) -> set:
    """Golden store-writeback latency-1 law (2026-08-21 latency P3,
    gaps/audit_latency.md mismatch #3): a STORE-side logical whose golden
    GPR_Write_Port alias is also in GPR_Read_Port updates and reads its
    base pointer, and golden Pipeline_Info pins that writeback register
    at Data_Latency=1 (available next bundle — only the loaded value of
    the load siblings is latency 2). Derived from
    instruction_type_index.json — no hand list. Census: 38 logicals
    (the POST/PRE/BREV/CB D_S*/S_S* writeback stores incl. the two
    UA suffix families). Excludes WBARWUA (AR-domain writeback, no GPR
    port overlap — P4 residual, stays Slot0_LS) and every load
    (Data_Latency=2). Members of these logicals publish
    Slot0_LS_WbLat (OperandCycles [1], MemoryCycle pair unchanged)."""
    idx = json.loads(index_path.read_text(encoding="utf-8"))
    wb: set = set()
    for type_recs in idx.values():
        for rec in type_recs:
            name = rec.get("Instruction")
            if not name:
                continue
            key = _logical_key(name)
            if not _is_store_logical(key):
                continue
            writes = set(rec.get("GPR_Write_Port") or [])
            reads = set(rec.get("GPR_Read_Port") or [])
            lat = (rec.get("Pipeline_Info") or {}).get("Data_Latency")
            if writes & reads and lat == 1:
                wb.add(key)
    return wb


def load_mul_lat1_logicals(index_path: Path) -> set:
    """Golden fresh-dest multiply latency-1 law (2026-08-21 latency P3,
    gaps/audit_latency.md mismatch #4 + the post-landing correction): a
    MAC-unit logical with golden Data_Latency=1 AND no accumulator tie
    (no bank's Write_Port alias appears in its Read_Port — the
    load_accumulator_ties law) produces a fresh dest available next
    bundle. Derived per-row from instruction_type_index.json — no hand
    list; the audit's family list is falsified (X4MUL16/X2FMUL32*/
    X2CMUL32X16*/F2MULZAA* and every accumulator-tied row are golden
    lat-2 and must NOT be tightened). Census: 67 logicals. Members
    publish Slot12_MAC_MulLat / per-slot MulLat rows."""
    idx = json.loads(index_path.read_text(encoding="utf-8"))
    mul1: set = set()
    for type_recs in idx.values():
        for rec in type_recs:
            name = rec.get("Instruction")
            if not name:
                continue
            avail = rec.get("Available") or []
            if isinstance(avail, str):
                avail = [avail]
            if "MAC0" not in avail and "MAC1" not in avail:
                continue
            lat = (rec.get("Pipeline_Info") or {}).get("Data_Latency")
            if lat != 1:
                continue
            tied = False
            for bank in ("GPR", "DR", "AR", "SFR"):
                writes = set(rec.get(f"{bank}_Write_Port") or [])
                reads = set(rec.get(f"{bank}_Read_Port") or [])
                if writes & reads:
                    tied = True
                    break
            if not tied:
                mul1.add(_logical_key(name))
    return mul1


def classify_member_flags(
    rec: MemberRecord,
    accum_ties: Optional[Dict[str, Tuple[str, ...]]] = None,
    sfr_writers: Optional[set] = None,
    store_writeback: Optional[set] = None,
    mul_lat1: Optional[set] = None,
) -> MemberEmitFlags:
    """Map unit/type/logical onto a published itinerary and closed flags.

    Itinerary comes from the already-published R5 class for `rec.unit`
    (SIN_COS/ARCTAN override via logical name; golden Data_Latency=1
    store-writeback / fresh-dest multiply overrides via the golden-derived
    member sets). Load/store/branch flags come from golden unit + logical;
    hasSideEffects=1 only for CSR / WFI / hwloop / SFR / AR / CB types,
    control-transfer, or unknown.
    """
    key = _logical_key(rec.logical)
    if key in SINCOS_LOGICALS:
        itinerary = SINCOS_ITINERARY.get(rec.unit)
        if itinerary is None:
            raise SystemExit(
                f"error: {rec.member_symbol}: {key} unit {rec.unit} has no "
                "published SinCosLat itinerary"
            )
    elif key in DSPLY2_LOGICALS:
        itinerary = DSPLY2_ITINERARY.get(rec.unit)
        if itinerary is None:
            raise SystemExit(
                f"error: {rec.member_symbol}: {key} unit {rec.unit} has no "
                "published DspLat itinerary"
            )
    elif key == "CSRR":
        itinerary = CSR_LY2_ITINERARY.get(rec.unit)
        if itinerary is None:
            raise SystemExit(
                f"error: {rec.member_symbol}: {key} unit {rec.unit} has no "
                "published CsrLat itinerary"
            )
    elif (
        store_writeback is not None
        and key in store_writeback
        and rec.unit == "LOADSTORE0"
    ):
        # Golden Data_Latency=1 store-writeback register (latency P3):
        # the rs writeback is available next bundle; MemoryCycle pair
        # stays conservative on the WbLat class.
        itinerary = STWB_ITINERARY
    elif (
        mul_lat1 is not None
        and key in mul_lat1
        and rec.unit in MUL_LAT1_ITINERARY
    ):
        # Golden Data_Latency=1 fresh-dest multiply (latency P3): members
        # pin one MAC unit and keep the lat-1 OperandCycles shape.
        itinerary = MUL_LAT1_ITINERARY[rec.unit]
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
        # Match residual CFG format classes (HaydnFormatsALU32.td
        # ALU32 RI12/I12_ONE/I20 hasSideEffects=1).
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
        implicit_defs=("SFR",)
        if sfr_writers and key in sfr_writers
        else (),
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
    cat: Catalog,
    family,
    accum_ties: Optional[Dict[str, Tuple[str, ...]]] = None,
    sfr_writers: Optional[set] = None,
    dr_readonly: Optional[set] = None,
    store_writeback: Optional[set] = None,
    mul_lat1: Optional[set] = None,
) -> str:
    """LIVE TableGen format-member Inst defs — included by HaydnFormatE.td.

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
    lines.append(
        f"//===-- {family.members_td_inc} - LIVE {family.display} members -*-===//"
    )
    lines.extend(generated_banner(
        generator=RECORDS_GENERATOR, family=family, authority_pins=True))
    lines.append("// Included by HaydnFormatE.td.")
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
        # `$rs = $rs_wb`.
        is_ls = rec.unit in LS_UNITS
        out_frags: List[str] = []
        in_frags: List[str] = []
        for frag in op_frags:
            dollar = frag.find("$")
            name = frag[dollar + 1 :] if dollar >= 0 else ""
            role = name.split("_")[0].lower() if name else ""
            if member_gpr_is_ssa_def(
                rec.logical, role, is_ls, dr_readonly
            ):
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
        # 2026-08-21 (gaps/audit_shapes.md class (d')): the writeback law is
        # golden-derived (GPR Write∩Read port tie), so UA-suffix `_POST` and
        # untagged CB families now tie dest2 the same way the 70 infix-tag
        # POST/PRE/BREV siblings always have.
        gpr_tie = tuple(
            a
            for a in (accum_ties or {}).get(_logical_key(rec.logical), ())
            if a.startswith("rs")
        )
        if is_ls and ls_has_tied_base_writeback(rec.logical, gpr_tie):
            dest2_ins = []
            for frag in in_frags:
                dollar = frag.find("$")
                name = frag[dollar + 1 :] if dollar >= 0 else ""
                if name.split("_")[0].lower() == "dest2":
                    dest2_ins.append(frag)
            if len(dest2_ins) != 1:
                raise SystemExit(
                    f"error: {rec.member_symbol} base-writeback needs exactly "
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

        flags = classify_member_flags(
            rec, accum_ties, sfr_writers, store_writeback, mul_lat1
        )
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
    # 2026-08-18 v2_1: golden RRR operand_fields carry one canonical token
    # per position (dest(rtd), src3(rs), ...), so member alias sequences now
    # match canon_alias_order directly and the same-class permutation branch
    # no longer fires (X4SEL16_E3_E1_ALU1_RRR re-audited: row present at
    # opcode 0x01, operands bind by name, bit placement unchanged).
    if canonicalized != []:
        raise SystemExit(
            "error: canonicalized member set changed: "
            f"{canonicalized} != [] — re-audit "
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
    # 2026-08-21 base-writeback + SFR-def pins (gaps/audit_shapes.md
    # classes (d') and (b)-real+(d)): measured, not assumed. The UA-suffix
    # _POST and untagged CB families are pinned by member symbol; the
    # compare SFR def is pinned by text shape. A DB regen that changes
    # these sets must be re-audited (logical Constraints arity /
    # SFR_Write_Port coverage) before the pins move.
    if accum_ties is not None:
        text_so_far = "\n".join(lines)
        pin_wb = (
            "def D_LQHWUA_POST_E2_E0_LOADSTORE0_AR : HaydnEntryE2E0<"
            "(outs DR64:$dest1_1, GPR32:$dest2_wb), "
            '(ins uimm2:$ar_sel_0, GPR32:$dest2_2), "d_lqhwua_post'
        )
        if pin_wb not in text_so_far:
            raise SystemExit(
                "error: D_LQHWUA_POST member (AR-ua suffix family) must "
                "carry the tied dest2_wb OUT + Constraints like its 70 "
                "infix-tag POST/PRE/BREV siblings"
            )
        pin_cb = (
            "def D_LDW_CB_IMM_E2_E0_LOADSTORE0_CBRI : HaydnEntryE2E0<"
            "(outs DR64:$dest1_1, GPR32:$dest2_wb), "
            '(ins uimm1:$cbr_sel_0, GPR32:$dest2_2, simm8:$imm_3), '
            '"d_ldw_cb_imm'
        )
        if pin_cb not in text_so_far:
            raise SystemExit(
                "error: D_LDW_CB_IMM member (untagged CB family) must "
                "carry the tied dest2_wb OUT + Constraints (circular-"
                "buffer base wrap is a golden GPR Write∩Read tie)"
            )
        if "PLDWWUA_POST_E2_E0_LOADSTORE0_AR" not in text_so_far:
            raise SystemExit(
                "error: PLDWWUA_POST member missing from emission"
            )
    if sfr_writers is not None:
        text_so_far = "\n".join(lines)
        # 2026-08-21 pair rework: golden SEQ64 is "SEQ64 rsd1, rsd2" —
        # SFR-only 2-src on EVERY row incl. the dest/src-spelled R sheet.
        pin_sfr = (
            "def SEQ64_E2_E0_ALU0_R : HaydnEntryE2E0<(outs), "
            "(ins DR64:$dest_0, DR64:$src_1"
        )
        if "Defs = [SFR]" not in text_so_far:
            raise SystemExit(
                "error: compare members must declare implicit "
                "Defs = [SFR] (golden SFR_Write_Port; only semantic output)"
            )
        if pin_sfr not in text_so_far:
            raise SystemExit(
                "error: SEQ64 member missing from emission (or lost the "
                "SFR-only pair shape — golden Syntax 'SEQ64 rsd1, rsd2')"
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
    """Parse residual member → logical pairs from LogicalMaterialize.

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
    # Empty is the retirement end state: member→logical comes only from
    # generated Format E members.
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

    Sources: LogicalMaterialize residual plus Format E member_symbol.
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
        # Retired slot members must not emit Haydn:: cases.
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


def emit_member_opcodes_inc(
    cat: Catalog, member_to_logical: Dict[str, str], family
) -> str:
    lines: List[str] = []
    lines.append(f"//===-- {family.member_opcodes_inc} -*- C++ -*-===//")
    lines.extend(generated_banner(
        generator=RECORDS_GENERATOR, family=family))
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
    # bundle walk); Haydn residual / Format E members need this overlay.
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
# Operand print order matches emit_members_td_inc AsmString.
# HaydnInstrInfoManual.td is a 0-def tombstone, not an encoding authority.
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


# Documented matcher insns that differ from Format E member AsmString
# operand count/order. Still the same logical mnemonic (or a known _w alias
# that objdump prints as the logical). No hypothesized Auto.td encodings.
# Packed by pack_full_bundle_text from the first generated member's Mode/
# EntryIdx — not a fixed 3-slot `{ insn; nop; nop }` occupancy bag.
_SPECIAL_INSNS = {
    "SET_HWLOOP": "set_hwloop_w 0, 16, 32, 4",
    "SET_HWLOOP_F2": "set_hwloop_f2_w 0, 16, 32, r1",
    "SET_HWLOOP_REG": "set_hwloop_reg_w 0, r1, r2, r3",
    # CB-151 member wire shape: stride/dir are not encoded (golden
    # rs = rs+8, direction in rs[2:1]).
    "D_LQHWUA_POST": "d_lqhwua_post 0, d0, r1",
    "D_LTWUA_POST": "d_ltwua_post 0, d0, r1",
    "D_SQHWUA_POST": "d_sqhwua_post 0, d0, r1",
    "D_STWUA_POST": "d_stwua_post 0, d0, r1",
    "PLDWWUA_POST": "pldwwua 0, r1",
    "WBARWUA": "wbarwua 0, r1",
    "MULL": "mull r1, r2, r1",
}

# Product logicals whose matcher/placement cannot form a Format E parcel.
# Do not invent encoding. Coverage still pins the name via # MNEM:.
_UNENCODABLE_LOGICALS = frozenset({
    "WFI<TBD>",  # serialize-only WFI refuses a complete parcel
})


def dis_check_line(logical: str, print_m: str, cov_m: str) -> str:
    """Objdump token in the T-MC6 harness-readable DIS form.

    check_mc_s_vs_obj_parity.py reads
    ``# DIS: {{[ \\t]}}NAME{{[ \\t,;]}}`` (print mnemonic), not a FileCheck
    alternation. Objdump prints the logical/print mnemonic after MemberId
    packing (`set_hwloop`, not the matcher `_w` alias used to assemble).
    """
    tok = print_m or cov_m or logical_print_mnemonic(logical)
    return "# DIS: {{[ \\t]}}" + tok + "{{[ \\t,;]}}"


def mnemonic_roundtrip_path(out_dir: Path, basename: str) -> Path:
    """llvm/test/MC/Haydn/<basename> from Target/Haydn out-dir."""
    return out_dir.parents[2] / "test" / "MC" / "Haydn" / basename


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


def check_memberid_packet_packing(cat: Catalog) -> None:
    """Packets follow first-member Mode/EntryIdx; unused slots are NOP."""
    e2_only, e3_only = mode_only_name_sets(cat)
    e2_only_s, e3_only_s = set(e2_only), set(e3_only)
    for logical, mids in cat.alternatives.items():
        packet = asm_packet_for_logical(cat, logical)
        if packet is None:
            continue
        if not (packet.startswith("{ ") and packet.endswith(" }")):
            raise SystemExit(f"packet not braced: {logical} {packet}")
        parts = [p.strip() for p in packet[2:-2].split(";")]
        if not parts or any(p == "" for p in parts):
            raise SystemExit(f"empty slot in {logical}: {packet}")
        rec = cat.members[mids[0]]
        width = 3 if rec.mode == "E3" else 2
        if len(parts) != width:
            raise SystemExit(
                f"packet width {len(parts)} != {rec.mode} width {width} "
                f"for {logical} {packet}"
            )
        if logical in e2_only_s and width != 2:
            raise SystemExit(f"E2-only {logical} packed as {packet}")
        if logical in e3_only_s and width != 3:
            raise SystemExit(f"E3-only {logical} packed as {packet}")
        want_insn_at = width - 1 - rec.entry_idx
        if parts[want_insn_at] == "nop":
            raise SystemExit(
                f"insn not at entry {rec.entry_idx} for {logical}: {packet}"
            )
        for i, part in enumerate(parts):
            if i != want_insn_at and part != "nop":
                raise SystemExit(
                    f"non-NOP pad at slot {i} for {logical}: {packet}"
                )
    print("OK MemberId full-bundle packing")


def pack_full_bundle_text(rec: MemberRecord, insn: str) -> str:
    """Full-bundle TEXT from MemberId (mode, entry). High entry first.

    Unused entries are the architectural NOP. Not a singleton/underfill
    packet. E2 width 2 (`{ e1; e0 }`); E3 width 3 (`{ e2; e1; e0 }`).
    """
    width = 3 if rec.mode == "E3" else 2
    if rec.entry_idx < 0 or rec.entry_idx >= width:
        raise SystemExit(
            f"member {rec.member_symbol} entry {rec.entry_idx} "
            f"out of {rec.mode} width {width}"
        )
    slots = ["nop"] * width
    slots[rec.entry_idx] = insn
    return "{ " + "; ".join(reversed(slots)) + " }"


def asm_packet_for_logical(cat: Catalog, logical: str) -> Optional[str]:
    """One complete full-bundle packet, or None if unencodable."""
    if logical in _UNENCODABLE_LOGICALS:
        return None
    mids = cat.alternatives[logical]
    rec = cat.members[mids[0]]
    if logical in _SPECIAL_INSNS:
        return pack_full_bundle_text(rec, _SPECIAL_INSNS[logical])
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
    return pack_full_bundle_text(rec, insn)


def emit_mnemonic_roundtrip_s(cat: Catalog, family) -> str:
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
    lines.extend(generated_banner(
        generator=RECORDS_GENERATOR, family=family, prefix="#"))
    lines.append(
        "# One Format E packet per product non-NOP logical from the golden"
    )
    lines.append(
        "# member table. Packet form follows the first generated member's"
    )
    lines.append(
        "# Mode and EntryIdx (high-entry-first TEXT). Unused entries are"
    )
    lines.append(
        "# architectural NOP: E2 e0 is `{ nop; insn }`, E3 e0 is"
    )
    lines.append(
        "# `{ nop; nop; insn }`. Never a singleton/underfill packet."
    )
    lines.append(
        "# Bare nop is covered in nop-format-e-not-all-zero.s. Hypothesized"
    )
    lines.append(
        "# Auto.td encodings are"
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
        # T-MC6 parity harness uses # MNEM: as the objdump token. Emit the
        # matcher/print spelling so encoding==obj DIS matches the packet.
        # Keep the catalog logical as a coverage pin when it differs.
        lines.append(f"# MNEM: {print_m}")
        if print_m != cov_m:
            lines.append(f"# LOGICAL: {cov_m}")
        if packet is None:
            lines.append(
                f"# UNENCODABLE: {logical} ({cov_m}) — no invented encoding"
            )
            lines.append("")
            continue
        lines.append(f"{label}:")
        lines.append(packet)
        lines.append(f"# DIS-LABEL: <{label}>:")
        lines.append(dis_check_line(logical, print_m, cov_m))
        lines.append("")
    if unenc:
        lines.append("# UNENCODABLE product logicals (real matcher/placement gap):")
        for logical in unenc:
            lines.append(f"#   {logical} -> {logical_print_mnemonic(logical)}")
        lines.append("")
    return "\n".join(lines) + "\n"


TOMBSTONE_TD_FILES = frozenset({
    "HaydnInstrInfoManual.td",
    "HaydnFormatsE96.td",
})


def load_hand_def_logicals(td_dir: Path) -> set:
    """Names with a hand def anywhere in the target .td set (all *.td,
    excluding generated *.td.inc and matcher/product tombstones)."""
    names = set()
    for path in sorted(td_dir.glob("*.td")):
        text = path.read_text(encoding="utf-8")
        defs = re.findall(r"^def\s+([A-Za-z0-9_]+)", text, re.M)
        if path.name in TOMBSTONE_TD_FILES:
            if defs:
                raise SystemExit(
                    f"error: {path.name} must remain a 0-def tombstone, "
                    f"found {defs}"
                )
            continue
        names.update(_logical_key(n) for n in defs)
    return names


def load_authored_catalog_overlay(path: Path) -> Tuple[set, set]:
    """P19 authored overlay: golden identifier names owned by hand td."""
    if not path.is_file():
        raise SystemExit(f"error: authored overlay not found: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != SCHEMA_VERSION:
        raise SystemExit(
            f"error: overlay schema {data.get('schema_version')!r} "
            f"!= {SCHEMA_VERSION}"
        )
    if data.get("family") != "e96":
        raise SystemExit(f"error: overlay family {data.get('family')!r} != e96")
    authored_list = data.get("authored_logicals")
    unavail_list = data.get("unavailable_logicals")
    if not isinstance(authored_list, list) or not isinstance(unavail_list, list):
        raise SystemExit(
            "error: overlay authored_logicals/unavailable_logicals must be lists"
        )
    authored: set = set()
    for n in authored_list:
        if not isinstance(n, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", n):
            raise SystemExit(f"error: overlay authored name not an identifier: {n!r}")
        k = _logical_key(n)
        if k in authored:
            raise SystemExit(f"error: overlay duplicate authored_logicals {n}")
        authored.add(k)
    unavailable: set = set()
    for n in unavail_list:
        if not isinstance(n, str) or not n.strip():
            raise SystemExit("error: overlay unavailable name empty")
        if n in unavailable:
            raise SystemExit(f"error: overlay duplicate unavailable_logicals {n}")
        unavailable.add(n)
    return authored, unavailable


def check_catalog_ownership(
    cat: Catalog,
    hand_logicals: set,
    authored: set,
    unavailable: set,
    emitted: set,
) -> None:
    """Fail closed on missing owner or authored/generated collision."""
    ident: set = set()
    non_ident: set = set()
    for logical in cat.alternatives:
        if re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", logical):
            ident.add(_logical_key(logical))
        else:
            non_ident.add(logical)
    extra_overlay = sorted(authored - ident)
    if extra_overlay:
        raise SystemExit(
            "P19 overlay names not in golden catalog: " + ", ".join(extra_overlay)
        )
    missing_unavail = sorted(non_ident - unavailable)
    if missing_unavail:
        raise SystemExit(
            "P19 missing unavailable declaration: " + ", ".join(missing_unavail)
        )
    extra_unavail = sorted(unavailable - non_ident)
    if extra_unavail:
        raise SystemExit(
            "P19 unavailable_logicals not a golden non-identifier: "
            + ", ".join(extra_unavail)
        )
    hand_catalog = ident & hand_logicals
    undeclared = sorted(hand_catalog - authored)
    if undeclared:
        raise SystemExit(
            "P19 undeclared authored overlap (hand def, not in overlay): "
            + ", ".join(undeclared)
        )
    missing_hand = sorted(authored - hand_logicals)
    if missing_hand:
        raise SystemExit(
            "P19 overlay authored name has no hand def: " + ", ".join(missing_hand)
        )
    collision = sorted(authored & emitted)
    if collision:
        raise SystemExit(
            "P19 owner collision (overlay and generated): " + ", ".join(collision)
        )
    missing_owner = sorted(ident - authored - emitted)
    if missing_owner:
        raise SystemExit(
            "P19 missing owner (not overlay, not generated): "
            + ", ".join(missing_owner)
        )
    print(
        f"OK P19 ownership authored={len(authored)} generated={len(emitted)} "
        f"unavailable={len(unavailable)}"
    )


def prove_ownership_fail_closed(
    cat: Catalog,
    hand_logicals: set,
    authored: set,
    unavailable: set,
    emitted: set,
) -> None:
    """Negative probes: missing owner and overlay/generated collision."""
    if not authored or not emitted:
        raise SystemExit("P19 ownership probe needs non-empty overlay and generated")
    victim_emitted = next(iter(sorted(emitted)))
    try:
        check_catalog_ownership(
            cat, hand_logicals, authored, unavailable, emitted - {victim_emitted}
        )
    except SystemExit as exc:
        if "missing owner" not in str(exc):
            raise SystemExit(
                f"P19 missing-owner probe expected 'missing owner', got: {exc}"
            )
    else:
        raise SystemExit("P19 missing-owner probe did not fail")
    victim_overlay = next(iter(sorted(authored)))
    try:
        check_catalog_ownership(
            cat, hand_logicals, authored, unavailable, emitted | {victim_overlay}
        )
    except SystemExit as exc:
        if "collision" not in str(exc):
            raise SystemExit(
                f"P19 collision probe expected 'collision', got: {exc}"
            )
    else:
        raise SystemExit("P19 collision probe did not fail")
    print("OK P19 ownership fail-closed probes")


def emit_logical_defs_td_inc(
    cat: Catalog,
    family,
    hand_logicals: set,
    accum_ties: Dict[str, Tuple[str, ...]],
    behaviors: Dict[str, str],
    gpr_ports: Dict[str, Dict[str, list]],
    store_writeback: Optional[set] = None,
    mul_lat1: Optional[set] = None,
) -> Tuple[str, set, set]:
    """HaydnInst logical defs for golden logicals with no hand def.

    2026-08-18 (v2_1): the golden catalog grew past the hand-maintained
    logical layer (+120 MAC RR 32X16, +4 LS D_SW_F64RS). The MC matcher
    parses LOGICAL defs (members are e96member-variant, never in the public
    matcher), so a golden logical without a def is un-assemblable. This
    emitter closes that gap from golden truth only: operand classes from
    member alias classification, writeback ties from golden POST semantics
    (rs writeback), itinerary from the primary unit. Everything lands in a
    generated include. Authored overlay names stay in hand td; undeclared
    overlap or a missing owner fails closed (P19).
    """
    lines: List[str] = []
    lines.append(
        f"//===-- {family.logical_defs_td_inc} - generated logicals -*- tablegen -*-===//"
    )
    lines.append("//")
    lines.extend(generated_banner(
        generator=RECORDS_GENERATOR, family=family, authority_pins=True))
    lines.append("//")
    lines.append("// Logical (matcher-facing) defs for golden catalog names that have NO")
    lines.append("// hand def in HaydnInstrInfo.td. Encoding")
    lines.append("// lives in the Format E members (HaydnFormatsE96Members.td.inc); these")
    lines.append("// defs exist so the public AsmMatcher can parse the mnemonic. Scalar")
    lines.append("// codegen selection is NOT claimed (no Patterns); intrinsics/ISel wire")
    lines.append("// them separately.")
    lines.append("//===----------------------------------------------------------------------===//")
    lines.append("")
    emitted = 0
    emitted_tied: set = set()
    emitted_names: set = set()
    for logical in sorted(cat.alternatives):
        if _logical_key(logical) in hand_logicals:
            continue
        # Golden placeholder rows (e.g. WFI<TBD>) are not td identifiers;
        # their mnemonics parse via hand defs (ALU32 WFI).
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", logical):
            continue
        mids = cat.alternatives[logical]
        rec = cat.members[mids[0]]
        lay = cat.layouts[rec.layout_id]
        ops = _member_print_ops(rec, lay)
        u = logical.upper()
        # Base-writeback semantics from golden GPR ports (the base alias is
        # both read and written): covers POST/PRE AND BREV (`mem..[..] = ..;
        # rs1 = rs1 + rs2`), which the name check alone misses. The name
        # tags stay as a belt-and-suspenders fallback for missing rows.
        gpr_w = gpr_ports.get(u, {}).get("W", [])
        gpr_r = set(gpr_ports.get(u, {}).get("R", []))
        port_wb = bool(set(gpr_w) & gpr_r)
        is_post = port_wb or "_POST_" in u or "_PRE_" in u
        # LS family: unit LOADSTORE0, mayStore per golden (all new LS are stores).
        is_ls = rec.unit.startswith("LOADSTORE")
        beh = behaviors.get(u, "")
        # A statement `X = mem..[..]` is a load (mem on RHS); `mem..[..] = X`
        # is a store (mem on LHS). Register reads like r_temp = {rtd..} do
        # not touch mem even though the text contains "mem"-free equals.
        # Loads may wrap the mem read in a conversion (ZEXT8->32(mem8[..]),
        # SEXT16->32(...)); match '=' then any cast prefix then mem[..].
        may_load = 1 if re.search(r"=\s*[\w>\-]*\s*\(?\s*mem\w*\[", beh) else 0
        # Stores may bit-select the mem64 line (`mem64[..][15:00] = ...`,
        # WBARWUA_CB partial residual write) — allow one [..] index before
        # the closing '=' (2026-08-28: golden v2_2 AR_CBR WBARWUA_CB).
        may_store = (
            1
            if re.search(r"mem\w*\[[^]]*\](?:\[[^]]*\])?\s*=", beh)
            else 0
        )
        # Def AsmString: user-mnemonic LS aliases (ld32/st32/...) are OWNED
        # by the canonical defs in HaydnInstrInfo.td (LD32 etc.); emitting
        # them here would duplicate the mnemonic and fail the match closed.
        # These logicals parse under their literal golden name
        # (s_sw_with_imm ...), exactly like the hand defs they replace.
        mnem = assembler_mnemonic(logical, rec.unit)
        if u in LS_USER_MNEMONIC:
            mnem = re.sub(r"(?<=[a-z0-9])_(?=[a-z0-9]*$)", "_", logical.lower())
            mnem = logical.lower()
        # Golden accumulator law (load_accumulator_ties, CB-152c): a logical
        # whose written DR bank alias is also read ties dest to an accumulator
        # input operand on the logical def.
        acc_ties = accum_ties.get(_logical_key(logical), ())
        is_acc = len(acc_ties) == 1 and not is_ls
        out_frags: List[str] = []
        in_frags: List[str] = []
        asm_ops: List[str] = []
        tie = ""
        gpr_n = 0
        dr_n = 0
        for role, kind, width in ops:
            r = role.lower()
            if kind == "REG_GPR":
                gpr_n += 1
                if is_post and r in ("dest2", "src1") and "wb" not in r:
                    # base writeback operand: out wb + tied in
                    out_frags.append("GPR32:$rs_wb")
                    in_frags.append("GPR32:$rs")
                    asm_ops.append("$rs")
                    tie = "$rs = $rs_wb"
                    continue
                is_gpr_dest = r in ("dest", "dest1") and r not in ("dest2",)
                if is_gpr_dest and (may_load or kind == "REG_GPR" and r == "dest"):
                    # Result dest in GPR: loads (rt = mem..[..]) and ALU/MAC
                    # GPR-dest ops (MULL rt, rs1, rs2 — golden GPR_Write=[rt]).
                    out_frags.append("GPR32:$rt")
                    asm_ops.append("$rt")
                    continue
                if r == "dest1" and may_load:
                    out_frags.append("GPR32:$rt")
                    asm_ops.append("$rt")
                    continue
                else:
                    in_frags.append(f"GPR32:$rs{gpr_n}")
                    asm_ops.append(f"$rs{gpr_n}")
            elif kind == "REG_DR":
                dr_n += 1
                # MAC RR names its dest role `dest`; LS RI6/RR name theirs
                # `dest1` (dest2 = base). Both are the value dest when the
                # op produces one (loads, ALU/MAC); store data stays ins.
                is_value_dest = r in ("dest", "dest1")
                if is_value_dest and (not is_ls or may_load):
                    # Result dest: outs for ALU/MAC and for LS LOADS (the
                    # loaded value). LS STORE dest1 is a READ (stored data)
                    # and stays ins.
                    out_frags.append(f"DR64:$rd{dr_n}")
                    if is_acc:
                        # tied accumulator input: ins gains $rd_in, asm
                        # keeps the 3-op user spelling (peer X4CMULA16S_H).
                        in_frags.append(f"DR64:$rd{dr_n}_in")
                        tie = f"$rd{dr_n} = $rd{dr_n}_in"
                else:
                    in_frags.append(f"DR64:$rd{dr_n}")
                asm_ops.append(f"$rd{dr_n}")
            elif kind == "REG_AR":
                in_frags.append("AR64:$ar_sel")
                asm_ops.append("$ar_sel")
            else:  # IMM
                if is_ls_ri6_scaled_imm(logical) and width == 6:
                    in_frags.append("simm6:$scaled_imm")
                    asm_ops.append("$scaled_imm")
                else:
                    of = OperandField(
                        role=role,
                        aliases=(role,),
                        bits=BitRange(width - 1, 0),
                    )
                    _n, frag, _w = field_operand_td(of, 0, role, logical)
                    in_frags.append(frag)
                    asm_ops.append(frag.split(":", 1)[1])
        outs = "(outs " + ", ".join(out_frags) + ")" if out_frags else "(outs)"
        ins = "(ins " + ", ".join(in_frags) + ")"
        asm = mnem + ("\t" + ", ".join(asm_ops) if asm_ops else "")
        # Constraints.md:57 "unit assignment is not bound to a fixed slot" —
        # itinerary follows the UNION of the logical's member units (golden
        # placements), mirroring the Available set. MAC symmetric MAC0+MAC1;
        # dual-load LOADSTORE0+LOAD1 gets the Slot01_LD menu; accumulating
        # (tied) MACs keep acc-read-late AccFirst timing (CB-152c).
        # 2026-08-21 latency P3: golden Data_Latency=1 overrides from the
        # per-row golden sets — fresh-dest multiplies take the MulLat menu
        # (OperandCycles [1,1,1,1]) and store-with-writeback logicals take
        # Slot0_LS_WbLat (writeback register next-bundle, MemoryCycle pair
        # unchanged). Golden lat-2 rows keep the wb/AccFirst shapes.
        uset = {(cat.members[mid].unit) for mid in mids}
        if "MAC0" in uset or "MAC1" in uset:
            if is_acc:
                itin = "Slot12_MAC_AccFirst"
            elif _logical_key(logical) in mul_lat1:
                itin = "Slot12_MAC_MulLat"
            else:
                itin = "Slot12_MAC"
        elif uset == {"LOADSTORE0", "LOAD1"}:
            itin = "Slot01_LD"
        elif "LOAD1" in uset:
            itin = "Slot1_LD"
        elif "LOADSTORE0" in uset:
            itin = (
                STWB_ITINERARY
                if _logical_key(logical) in store_writeback
                else "Slot0_LS"
            )
        elif uset == {"ALU0"}:
            # Golden Available = ALU0 only (e.g. SET_HWLOOP_F2 HWLRIIR):
            # do not book ALU1/ALU2 the op cannot occupy (2026-08-21
            # itinerary re-map, audit_itinerary.md over-broad table).
            itin = "Slot0_ALU"
        else:
            itin = "Slot012_ALU"
        props = [f"isCodeGenOnly = 0", "DecoderNamespace = \"HaydnAutoNoDecode\"", "isAsmParserOnly = 0"]
        if may_load:
            props.append("mayLoad = 1")
        if may_store:
            props.append("mayStore = 1")
        if tie:
            props.append(f'Constraints = "{tie}"')
        lines.append(f"let {', '.join(props)} in {{")
        lines.append(f"def {logical} : HaydnInst<4, {outs},")
        lines.append(f"    {ins},")
        lines.append(f'    "{asm}", []> {{')
        lines.append(f"  let Itinerary = {itin};")
        lines.append("}")
        lines.append("}")
        lines.append("")
        if tie:
            emitted_tied.add(_logical_key(logical))
        emitted_names.add(_logical_key(logical))
        emitted += 1
    lines.append(f"// generated logical defs: {emitted}")
    lines.append("")
    return "\n".join(lines), emitted_tied, emitted_names


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
    # The v1 canonical-vector ledger is UNCHANGED under golden v2_2 (same
    # file sha as the v2_1 era) and carries no AR_CBR entries. Its embedded
    # provenance block therefore still names the v2_1 layout pair it was
    # authored against; pin that block verbatim (v2_1 digests) so any edit
    # to the ledger's authority stanza fails closed until re-audited — the
    # ledger is a derived parity check, never an oracle for new surface.
    CANONICAL_LEDGER_ORACLE = {
        "format_e_bit_layout_v2_1.json": (
            "2609877075156dd9749e1e8dd0b45ff1ef326dae2e1c2a9c10fbd9cd1c1c8f6a"
        ),
        "format_e_bit_layout_v2_1.xlsx": (
            "dd8491b7c182d006ad7d05c8cd46f64c02f439ae41bad0416f7139703d07b76f"
        ),
    }
    if oracle != CANONICAL_LEDGER_ORACLE:
        raise SystemExit(
            "canonical oracle provenance block changed "
            f"{oracle!r} — the v1 ledger is v2_1-authored; re-audit before "
            "moving this pin"
        )

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


# W53: authored + generated *logical* imm classes vs golden field (not members).
# ANDI/ORI/XORI32 are ZEXT imm20 → uimm20. SIN_COS/ARCTAN are uimm4.
# CB stride is SEXT8 → simm8. Extra uimm1 cbr_sel on CB is allowed.
W53_LOGICAL_IMM: Dict[str, Tuple[str, int]] = {
    "ANDI32": ("uimm", 20),
    "ORI32": ("uimm", 20),
    "XORI32": ("uimm", 20),
    "SIN_COS": ("uimm", 4),
    "ARCTAN": ("uimm", 4),
    "D_LDW_CB_IMM": ("simm", 8),
    "D_SDW_CB_IMM": ("simm", 8),
}

TD_DEF_RE_W53 = re.compile(r"^def\s+([A-Za-z0-9_]+)\s*:", re.MULTILINE)
TD_INS_RE = re.compile(r"\(ins\s+([^)]*)\)")
TD_IMM_CLASS_RE = re.compile(r"\b(uimm|simm|imm)(\d+)\b")


def collect_td_logical_imm_classes(text: str) -> Dict[str, List[Tuple[str, int]]]:
    """Map def name → imm operand classes found in its (ins) list."""
    out: Dict[str, List[Tuple[str, int]]] = {}
    matches = list(TD_DEF_RE_W53.finditer(text))
    for i, m in enumerate(matches):
        name = m.group(1)
        end = matches[i + 1].start() if i + 1 < len(matches) else min(len(text), m.end() + 800)
        chunk = text[m.start() : end]
        ins = TD_INS_RE.search(chunk)
        if not ins:
            continue
        imms = [
            (k.lower(), int(w)) for k, w in TD_IMM_CLASS_RE.findall(ins.group(1))
        ]
        if imms:
            out[name] = imms
    return out


def check_authored_logical_imm_parity(*td_texts: str) -> None:
    """Fail closed if W53 logical imm classes drift from golden."""
    found: Dict[str, List[Tuple[str, int]]] = {}
    for text in td_texts:
        found.update(collect_td_logical_imm_classes(text))
    missing = sorted(n for n in W53_LOGICAL_IMM if n not in found)
    if missing:
        raise SystemExit(
            "W53 logical imm defs missing from td: " + ", ".join(missing)
        )
    bad: List[str] = []
    for name, (kind, width) in W53_LOGICAL_IMM.items():
        imms = found[name]
        if (kind, width) not in imms:
            bad.append(f"{name} want {kind}{width} got {imms}")
    if bad:
        raise SystemExit("W53 logical imm mismatch: " + "; ".join(bad))
    print(f"OK W53 logical imm parity names={len(W53_LOGICAL_IMM)}")


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


def prove_source_mutation_not_silent(
    cat: Catalog, family, json_sha: str, xlsx_sha: str, records_path: Path
) -> None:
    """P19: a changed catalog fact must change the projection or fail closed.

    Silent reuse of the committed records.inc after an opcode flip is a
    readback/fallback defect. Restored catalog must re-emit committed bytes
    (determinism; second regeneration).
    """
    if not records_path.is_file():
        raise SystemExit(f"P19 source-mutation needs committed {records_path}")
    committed = records_path.read_text(encoding="utf-8")
    victim = next((rec for rec in cat.members if not rec.is_nop), None)
    if victim is None:
        raise SystemExit("P19 source-mutation: no non-NOP member")
    orig = victim.opcode
    victim.opcode = orig ^ 1
    try:
        mutated = emit_records_inc(cat, json_sha, xlsx_sha, family)
    finally:
        victim.opcode = orig
    if mutated == committed:
        raise SystemExit(
            "P19 source-mutation: opcode flip reused committed records"
        )
    restored = emit_records_inc(cat, json_sha, xlsx_sha, family)
    second = emit_records_inc(cat, json_sha, xlsx_sha, family)
    if restored != committed:
        raise SystemExit(
            "P19 source-mutation: restored catalog did not re-emit committed records"
        )
    if restored != second:
        raise SystemExit("P19 determinism: two regenerations differed")
    print("OK P19 source-mutation + determinism")


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    add_family_argument(ap)
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
        "--print-identity-census",
        action="store_true",
        help="Print the sorted divergent-logical census for re-pinning "
        "EXPECTED_IDENTITY_DIVERGENT (no write)",
    )
    ap.add_argument(
        "--emit-mnemonic-roundtrip",
        action="store_true",
        help="Write test/MC/Haydn/format-e-mnemonic-roundtrip.s (also written "
        "on a normal generate; --check diffs it)",
    )
    args = ap.parse_args(argv)
    family = get_family(args.family)

    if args.json:
        json_path = args.json
        golden = json_path.parent
    else:
        golden = resolve_golden_dir(family)
        json_path = golden / family.json_filename
    xlsx_path: Path = args.xlsx or (golden / family.xlsx_filename)
    canonical_path: Path = args.canonical_vectors or (
        golden / family.canonical_filename
    )
    if not json_path.is_file():
        print(f"error: golden JSON not found: {json_path}", file=sys.stderr)
        return 2

    try:
        verify_authority_inputs(
            golden,
            [
                json_path.name,
                xlsx_path.name,
                family.index_filename,
                family.canonical_filename,
                family.constraints_filename,
            ],
        )
        verify_golden_inputs_pin(golden_inputs_pin_path())
        check_cutover_surfaces(args.out_dir)
        check_residual_hand_logicals(args.out_dir)
        print("OK matcher-root collapse")
        print("OK Manual.td tombstone")
        print("OK residual hand logicals")
    except SystemExit as exc:
        msg = str(exc)
        if msg:
            print(msg, file=sys.stderr)
        return 2 if msg else 0

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

    records = emit_records_inc(cat, json_sha, xlsx_sha, family)
    reloc_lsb = emit_reloc_field_lsb_inc(cat, json_sha, xlsx_sha, family)
    ledger = emit_setdesc_ledger_inc(cat, family)
    index_path = golden / family.index_filename
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
    full_accum_ties = load_accumulator_ties(index_path)
    accum_ties = full_accum_ties
    # Emit the generated logical defs FIRST: their in-memory tie set is part
    # of "what the td models" for the divergence pin (a disk-stale .inc must
    # not fake divergence that this very run is about to fix).
    hand_logicals = load_hand_def_logicals(out_dir)
    behaviors = {}
    gpr_ports: Dict[str, Dict[str, list]] = {}
    for _rows in json.loads(index_path.read_text(encoding="utf-8")).values():
        if isinstance(_rows, list):
            for _r in _rows:
                if isinstance(_r, dict) and _r.get("Instruction"):
                    behaviors[_r["Instruction"].strip().upper()] = _r.get("Behavior") or ""
                    gpr_ports[_r["Instruction"].strip().upper()] = {
                        "W": _r.get("GPR_Write_Port") or [],
                        "R": _r.get("GPR_Read_Port") or [],
                    }
    logical_defs_td, emitted_tied, emitted_defs_keys = emit_logical_defs_td_inc(
        cat,
        family,
        hand_logicals,
        full_accum_ties,
        behaviors,
        gpr_ports,
        load_store_writeback_logicals(index_path),
        load_mul_lat1_logicals(index_path),
    )
    overlay_authored, overlay_unavail = load_authored_catalog_overlay(
        AUTHORED_OVERLAY_PATH
    )
    check_catalog_ownership(
        cat,
        hand_logicals,
        overlay_authored,
        overlay_unavail,
        emitted_defs_keys,
    )
    td_tied = load_td_tied_logicals(Path(__file__).resolve().parent.parent)
    # names already emitted by this run are governed by emitted_tied, not by
    # the stale on-disk .inc the scan just read
    # names emitted this run: their tie verdict comes from emitted_tied
    td_tied = (td_tied - emitted_defs_keys) | emitted_tied
    divergent = sorted(k for k in accum_ties if k not in td_tied)
    accum_ties = {k: v for k, v in accum_ties.items() if k in td_tied}
    # Golden says these read their destination; the LLVM logical models no
    # tie, so members stay at logical arity and the gap is a ledger item
    # (conditional moves / partial-word inserts with unmodeled dest reads).
    # Measured, pinned: a regen that changes this set must be re-audited.
    # History: 87 -> 159 (2026-08-18 v2_1 index Read_Port growth) -> 87
    # (generated defs tied the 72 32X16 accumulators) -> 46 (2026-08-19
    # S2b wave-1) -> 4 (2026-08-19 S2b wave-2: F2MULAS32R/RS, F2MULSA32R/
    # RS, FMULS16_HS/LS, FMULAA16/SS16 pairs, MULSA32/MULSS32, SMULA16
    # family, and X4CLAMP16 all migrated to generated tied defs) -> 0
    # (2026-08-28 M23 one-wave: MOVEI_H/L and MOVF64/MOVT64 gained the tied
    # $rd_old logical input; X2MOVF/T32 and X4MOVF/T16 were already tied).
    # The non-LS remainder must stay EMPTY: any growth is a new unmodeled
    # golden dest read — re-audit member arity vs the logicals before
    # touching this pin.
    divergent_non_ls = [
        k for k in divergent
        if not k.startswith(("D_", "S_", "PLD", "WBAR"))
    ]
    if divergent_non_ls:
        raise SystemExit(
            "error: golden-tied-but-TD-untied set must stay empty "
            f"({len(divergent_non_ls)}): {divergent_non_ls} — re-audit "
            "member arity vs the logicals (CB ledger: unmodeled dest reads; "
            "M23 closed the last four 2026-08-28)"
        )
    # 2026-08-21: golden SFR-writer set (compares + MOVEGPR2SFR/ZERO_SFR)
    # drives implicit Defs = [SFR] on members. Measured pin below.
    sfr_writers = load_sfr_writers(index_path)
    expected_sfr_writers = {
        "SEQ64", "SLE64", "SLT64",
        "X2SEQ32", "X2SLE32", "X2SLT32",
        "X4SEQ16", "X4SLE16", "X4SLT16",
        "MOVEGPR2SFR", "ZERO_SFR",
    }
    if sfr_writers != expected_sfr_writers:
        raise SystemExit(
            "error: golden SFR-writer set changed "
            f"({sorted(sfr_writers)}) — re-audit implicit Defs = [SFR] "
            "coverage (gaps/audit_shapes.md class (b)-real+(d))"
        )
    # 2026-08-21 scalar-trio pair rework: SFR-compare logicals (SFR
    # write + no DR write) turn dest-spelled DR wires into reads.
    dr_readonly = load_dr_readonly_logicals(index_path)
    expected_dr_readonly = expected_sfr_writers
    if dr_readonly != expected_dr_readonly:
        raise SystemExit(
            "error: golden DR read-only set changed "
            f"({sorted(dr_readonly)}) — re-audit member DR-wire "
            "direction (gaps/audit_shapes.md Scalar trio)"
        )
    # 2026-08-21 latency P3: golden Data_Latency=1 member sets, derived
    # per-row from instruction_type_index (never a family list — the
    # audit's mul list was falsified by the post-landing correction).
    # Measured pins: a regen that changes either census must be re-audited
    # against golden before the itineraries move.
    store_writeback = load_store_writeback_logicals(index_path)
    # v2_2: 38 -> 40 (D_STWUA_CB_POST, D_SQHWUA_CB_POST join — the CB
    # writeback UA stores carry the same golden GPR Write∩Read lat-1 tie).
    if len(store_writeback) != 40:
        raise SystemExit(
            "error: golden store-writeback lat-1 census changed "
            f"({len(store_writeback)}): {sorted(store_writeback)} — "
            "re-audit Slot0_LS_WbLat coverage (gaps/audit_latency.md #3)"
        )
    mul_lat1 = load_mul_lat1_logicals(index_path)
    if len(mul_lat1) != 67:
        raise SystemExit(
            "error: golden fresh-dest multiply lat-1 census changed "
            f"({len(mul_lat1)}): {sorted(mul_lat1)} — re-audit "
            "Slot12_MAC_MulLat coverage (gaps/audit_latency.md #4 "
            "correction: only per-row JSON lat-1 + no accumulator tie)"
        )
    members_td = emit_members_td_inc(
        cat,
        family,
        accum_ties,
        sfr_writers,
        dr_readonly,
        store_writeback,
        mul_lat1,
    )
    # Direct-setDesc identity law: every compiler-reachable logical must be
    # operand-shape identical to every generated member. Members are parsed
    # back from the emitted members_td (exact TableGen input). Ratchet: the
    # divergent census must only shrink; growth fails generation in BOTH
    # emit and --check modes.
    _m_classes, member_schemas = parse_td_schemas(members_td)
    logical_schemas, _logical_parents = load_logical_schemas(
        out_dir, [logical_defs_td] if logical_defs_td else []
    )
    census = identity_divergent_census(
        cat, member_schemas, logical_schemas, member_to_logical
    )
    census_set = set(census)
    if args.print_identity_census:
        for name in census:
            print(name)
        return 0
    if census_set - EXPECTED_IDENTITY_DIVERGENT:
        raise SystemExit(
            "error: new setDesc identity divergence (logicals grew the "
            f"census): {sorted(census_set - EXPECTED_IDENTITY_DIVERGENT)} "
            "— fix the logical TableGen schema before the member, or "
            "re-audit EXPECTED_IDENTITY_DIVERGENT"
        )
    fixed = EXPECTED_IDENTITY_DIVERGENT - census_set
    if fixed:
        print(
            "note: setDesc identity ratchet: logicals now aligned (re-pin "
            f"the census): {sorted(fixed)}"
        )
    # Universal singleton coverage census (PIPE-20, GR2.2): every
    # compiler-reachable logical must peel to a catalog alt span. Runs in
    # BOTH emit and --check modes — the --check arm is the ratchet's
    # regeneration gate; HaydnFormatERecordsTest is the always-on layer.
    singleton_census = singleton_uncovered_census(cat, logical_schemas)
    singleton_set = set(singleton_census)
    if singleton_set - EXPECTED_SINGLETON_UNCOVERED:
        raise SystemExit(
            "error: compiler-reachable logicals without singleton "
            f"coverage: {sorted(singleton_set - EXPECTED_SINGLETON_UNCOVERED)} "
            "— add the catalog span or extend SINGLETON_PEEL_ALIASES / "
            "peel_logical_name (and re-pin both seats + the unittest "
            "parity rows); never let an unpeelable logical reach post-RA"
        )
    singleton_fixed = EXPECTED_SINGLETON_UNCOVERED - singleton_set
    if singleton_fixed:
        print(
            "note: singleton coverage ratchet: logicals now covered "
            f"(re-pin the census): {sorted(singleton_fixed)}"
        )
    member_opcodes = emit_member_opcodes_inc(cat, member_to_logical, family)
    mnemonic_rt = emit_mnemonic_roundtrip_s(cat, family)
    mnemonic_rt_path = mnemonic_roundtrip_path(out_dir, family.mnemonic_roundtrip_s)

    targets = {
        out_dir / family.records_inc: records,
        out_dir / RELOC_FIELD_LSB_INC: reloc_lsb,
        out_dir / family.setdesc_ledger_inc: ledger,
        out_dir / family.members_td_inc: members_td,
        out_dir / family.logical_defs_td_inc: logical_defs_td,
        out_dir / family.member_opcodes_inc: member_opcodes,
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
            authored = (out_dir / "HaydnInstrInfo.td").read_text(encoding="utf-8")
            check_authored_logical_imm_parity(authored, logical_defs_td)
            roundtrip_members(cat)
            print(
                f"OK member encode→decode round-trip members={len(cat.members)}"
            )
            check_memberid_packet_packing(cat)
            prove_ownership_fail_closed(
                cat,
                hand_logicals,
                overlay_authored,
                overlay_unavail,
                emitted_defs_keys,
            )
            prove_source_mutation_not_silent(
                cat, family, json_sha, xlsx_sha, out_dir / family.records_inc
            )
            prove_unpinned_consumed_fails(golden)
            prove_derived_xlsx_not_authority(golden)
            prove_unused_authority_not_consumed(golden)
            prove_unpublished_choice_fails(golden)
            prove_text_has_authority_pins(members_td, family.members_td_inc)
            prove_text_has_authority_pins(
                logical_defs_td, family.logical_defs_td_inc
            )
            print("OK owned generated nine-file pin stamp")
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
