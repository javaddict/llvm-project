#!/usr/bin/env python3
"""Generate Format E tables + live TableGen members from golden JSON (once).

Emits HaydnGenFormatERecords.inc, HaydnGenFormatESetDescLedger.inc,
HaydnFormatsE96Members.td.inc (LIVE Inst{}), HaydnGenFormatEMemberOpcodes.inc.
Product encode/decode: BUNDLE_E96 framing + tblgen on these members.

Usage:
  generate_format_e_records.py [--json PATH] [--xlsx-hash HEX] [--out-dir DIR]
                               [--check]

--check regenerates into memory and diffs against the committed files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

# Default golden location: discovered, not trusted — content is pinned by
# PINNED_JSON_SHA256 below, so discovery order cannot change what generation
# accepts. Overridable via --json or HAYDN_FORMAT_E_JSON.
_DEFAULT_JSON_CANDIDATES = [
    Path(os.environ["HAYDN_FORMAT_E_JSON"])
    if os.environ.get("HAYDN_FORMAT_E_JSON")
    else None,
    Path("/ssd2/mhyang/haydn-plans/Database/golden/format_e_bit_layout_v2.json"),
    Path.home() / "haydn" / "format_e_bit_layout_v2.json",
]
DEFAULT_JSON = next(
    (p for p in _DEFAULT_JSON_CANDIDATES if p and p.exists()),
    _DEFAULT_JSON_CANDIDATES[1],
)
# Manifest pin for the companion XLSX (geometry authority pair). The XLSX is
# the delivery rendering; the mapping repair below did not regenerate it.
PINNED_XLSX_SHA256 = (
    "9b3c06612cec47fa026bd79cff5632cb970abdfe1e161075444f7d02432574af"
)
# Repaired golden: delivery b0b477e5… + haydn_encoding.py --fix-operand-mapping
# (76 mapping rows re-derived from instruction_type_index.json Syntax; bit
# geometry untouched). The delivery pin is retired — regenerating from it
# reintroduces the operand-mapping defect.
PINNED_JSON_SHA256 = (
    "8465132c2fb91e44a335d8a63577c637428d93106ed7a4d657d80ac70fdfa7f9"
)

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
                types = unit_obj["types"]
                for type_name, type_obj in types.items():
                    tcode, twidth = type_code_width_from_bin(
                        type_obj["type_code_bin"]
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


def emit_records_inc(
    cat: Catalog, json_sha: str, xlsx_sha: str
) -> str:
    e2_pairs = e2_unit_pairs(cat)
    e3_legal, e3_illegal = e3_unit_tuples(cat)
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
    if kind == "REG_DR":
        return name, f"DR64:${name}", str(w)
    if kind == "REG_AR":
        return name, f"AR:${name}", str(w)
    if kind == "REG_GPR":
        return name, f"GPR32:${name}", str(w)
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


def emit_members_td_inc(cat: Catalog) -> str:
    """LIVE TableGen format-member Inst defs — included by HaydnFormatsE96.td."""
    layouts = {l.layout_id: l for l in cat.layouts}

    # Canonical operand-alias order per logical = the majority signature's
    # alias sequence across its placements. fillFormatEMemberInst binds
    # logical MC operands to member Desc operands bag-by-class in Desc order,
    # so a member whose SAME-CLASS aliases permute against the canonical
    # order would encode swapped sources (self-consistent with the decoder,
    # wrong against the golden mapping — only the simulator would see it).
    # Such members get their (ins)/asm emitted in canonical alias order; bit
    # placement below is by field name and does not move.
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

        ins = ", ".join(op_frags) if op_frags else ""
        asm = rec.logical.lower()
        if asm_ops:
            asm = asm + "\\t" + asm_ops

        lines.append("let isCodeGenOnly = 0, isAsmParserOnly = 0, hasSideEffects = 1 in")
        lines.append(
            f'def {rec.member_symbol} : {base}<'
            f'(outs), (ins {ins}), "{asm}", []> {{'
        )
        for name, rh, rl, w, is_var in bit_names:
            if is_var:
                lines.append(f"  bits<{w}> {name};")
        emit_entry_bits_assign(lines, rec, lay, bit_names, entry_w, entry_lo, inst_f)
        lines.append("}")
        lines.append("")
        count += 1
    # Fail-closed pin: the canonicalized set is measured, not assumed. A DB
    # regen that changes it must be re-audited against fillFormatEMemberInst
    # bag binding before this pin moves.
    if canonicalized != ["X4SEL16_E3_E1_ALU1_RRR"]:
        raise SystemExit(
            "error: canonicalized member set changed: "
            f"{canonicalized} != ['X4SEL16_E3_E1_ALU1_RRR'] — re-audit "
            "fillFormatEMemberInst bag binding before repinning"
        )
    lines.append(
        "// Members emitted in canonical alias order (same-class permutation "
        f"vs majority signature): {len(canonicalized)}"
    )
    lines.append(f"// Live non-NOP Format E members: {count}")
    lines.append("")
    return "\n".join(lines) + "\n"


def emit_member_opcodes_inc(cat: Catalog) -> str:
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


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", type=Path, default=DEFAULT_JSON)
    ap.add_argument("--xlsx-hash", default=PINNED_XLSX_SHA256)
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Target Haydn directory (default: llvm/lib/Target/Haydn)",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="Verify committed outputs match regeneration (no write)",
    )
    args = ap.parse_args(argv)

    json_path: Path = args.json
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
    xlsx_sha = args.xlsx_hash
    if xlsx_sha != PINNED_XLSX_SHA256:
        print(
            f"error: XLSX sha256 pin {xlsx_sha} != pinned {PINNED_XLSX_SHA256}",
            file=sys.stderr,
        )
        return 2

    data = json.loads(json_path.read_text(encoding="utf-8"))
    cat = build_catalog(data)

    records = emit_records_inc(cat, json_sha, xlsx_sha)
    ledger = emit_setdesc_ledger_inc(cat)
    members_td = emit_members_td_inc(cat)
    member_opcodes = emit_member_opcodes_inc(cat)

    out_dir: Path = args.out_dir
    targets = {
        out_dir / "HaydnGenFormatERecords.inc": records,
        out_dir / "HaydnGenFormatESetDescLedger.inc": ledger,
        out_dir / "HaydnFormatsE96Members.td.inc": members_td,
        out_dir / "HaydnGenFormatEMemberOpcodes.inc": member_opcodes,
    }

    if args.check:
        failed = False
        for path, content in targets.items():
            if not path.is_file():
                print(f"MISSING {path}", file=sys.stderr)
                failed = True
                continue
            cur = path.read_text(encoding="utf-8")
            if cur != content:
                print(f"OUT_OF_DATE {path}", file=sys.stderr)
                failed = True
            else:
                print(f"OK {path}")
        if failed:
            return 1
        print(
            f"check passed: layouts={len(cat.layouts)} members={len(cat.members)} "
            f"logicals={len(cat.alternatives)}"
        )
        return 0

    for path, content in targets.items():
        changed = write_if_changed(path, content)
        print(f"{'WROTE' if changed else 'UNCHANGED'} {path}")

    print(
        f"generated layouts={len(cat.layouts)} members={len(cat.members)} "
        f"logicals={len(cat.alternatives)} json={json_sha[:12]}…"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
