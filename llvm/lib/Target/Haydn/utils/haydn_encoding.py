#!/usr/bin/env python3
"""Normalize the Haydn format E instruction encoding.

`format_e_bit_layout_v2_2.json` and `instruction_type_index.json` are the
pinned layout and instruction-index inputs. This script flattens the layout
into one downstream-agnostic table and checks the database against itself.
Live TableGen members come from FormatE/generate_format_e_records.py.
HaydnInstrInfoManual.td is a tombstone and is not an encoding authority.

  haydn_encoding.py --database DIR --emit table  -o encoding.json
  haydn_encoding.py --database DIR --emit report [--target-dir DIR]
  haydn_encoding.py --database DIR --check
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from itertools import permutations
from pathlib import Path

FORMAT_E_LAYOUT = "format_e_bit_layout_v2_2.json"
INSTRUCTION_INDEX = "instruction_type_index.json"

# Hardware units, in the order their FuncUnit defs are emitted.
UNITS = ("LOADSTORE0", "LOAD1", "ALU0", "ALU1", "ALU2", "MAC0", "MAC1")

# Single-bit fields are spelled bit[N]; ranges are bit[HI:LO].
BIT_SPAN = re.compile(r"bit\[(\d+)(?::(\d+))?\]")
# "dest(rt, rtd): bit[22:19] (4b)"
OPERAND_FIELD = re.compile(r"^\s*(\w+)\s*\(([^)]*)\)\s*:")


def spans(text: str) -> list[tuple[int, int]]:
    """Every bit span in `text`, as (msb, lsb) pairs."""
    result = []
    for match in BIT_SPAN.finditer(text or ""):
        msb = int(match.group(1))
        lsb = int(match.group(2)) if match.group(2) is not None else msb
        if msb < lsb:
            raise SystemExit(f"inverted bit span in {text!r}")
        result.append((msb, lsb))
    return result


def one_span(text: str, context: str) -> tuple[int, int]:
    found = spans(text)
    if len(found) != 1:
        raise SystemExit(f"{context}: expected one bit span in {text!r}")
    return found[0]


def parse_int(text: str, base: int, context: str) -> int:
    try:
        return int(str(text).strip(), base)
    except ValueError:
        raise SystemExit(f"{context}: cannot read {text!r} as base-{base}")


def load_pipeline(database: Path) -> dict[str, tuple[tuple[str, ...], object]]:
    """Per-instruction (available units, data latency) from the instruction index.

    Latency belongs to the itinerary class, so it has to come from here rather
    than the bit layout. A missing Data_Latency means the instruction writes no
    register -- stores, branches, hwloop setup -- and so has no result to wait
    for; it reads as one, the no-bubble case. SIN_COS and ARCTAN state theirs as
    uimm4 + 2, which no static class can express, so they are kept apart.
    """
    data = json.loads((database / INSTRUCTION_INDEX).read_text(encoding="utf-8"))
    out: dict[str, tuple[tuple[str, ...], object]] = {}
    for group, entries in data.items():
        for entry in entries:
            name = str(entry.get("Instruction", "")).strip()
            if not name or name == "WFI<TBD>":
                continue
            available = entry.get("Available")
            units = ((available,) if isinstance(available, str)
                     else tuple(available or ()))
            for unit in units:
                if unit not in UNITS:
                    raise SystemExit(f"{group}/{name}: unknown unit {unit!r}")
            latency = (entry.get("Pipeline_Info") or {}).get("Data_Latency")
            if isinstance(latency, str):
                latency = "var"
            elif latency is None:
                latency = 1
            previous = out.get(name)
            if previous is not None and previous != (units, latency):
                raise SystemExit(f"{name}: conflicting pipeline info")
            out[name] = (units, latency)
    if not out:
        raise SystemExit("instruction index is empty")
    return out


def itinerary_name(units: tuple[str, ...], latency: object) -> str:
    return "Unit_" + "".join(u.replace("LOADSTORE", "LS") for u in units) \
           + f"_L{latency}"


def header_fields(header: dict) -> dict:
    """The bundle header: which bits select the format and the entry count.

    Values are given inline ("bit[2:0] = 0b111"); entry_num carries its meaning
    in prose instead, so only its position is taken from here.
    """
    parsed: dict[str, dict] = {}
    for name in ("format_indicator", "entry_num", "reserved"):
        text = str(header.get(name, ""))
        if not text:
            raise SystemExit(f"bundle header is missing {name}")
        msb, lsb = one_span(text, f"header/{name}")
        field = {"msb": msb, "lsb": lsb}
        if name != "entry_num":
            # "bit[2:0] = 0b111" — the value follows the span it belongs to.
            literal = re.search(r"bit\[[^\]]*\]\s*=\s*(0b[01]+|0x[0-9a-fA-F]+|\d+)",
                                text)
            if literal is None:
                raise SystemExit(f"header/{name}: no value given")
            field["value"] = int(literal.group(1), 0)
            if field["value"] >= 1 << (msb - lsb + 1):
                raise SystemExit(f"header/{name}: value does not fit its field")
        parsed[name] = field
    return parsed


def load_placements(database: Path) -> tuple[dict, list[dict]]:
    """Flatten the layout into one record per (instruction, entry, unit, type)."""
    data = json.loads((database / FORMAT_E_LAYOUT).read_text(encoding="utf-8"))
    if data.get("format") != "E":
        raise SystemExit("bit layout is not format E")
    geometry = {
        "bundle_bits": int(data["bundle_bits"]),
        "payload_lsb": int(data["payload_lsb"]),
        "payload_budget_bits": int(data["payload_budget_bits"]),
        "header": header_fields(data.get("header") or {}),
    }

    placements: list[dict] = []
    for group_key, entry_num in (("entry_num_0", 0), ("entry_num_1", 1)):
        group = data[group_key]
        entry_count = sum(1 for k in group if k.startswith("entry"))
        for entry_key in sorted(k for k in group if k.startswith("entry")):
            entry = group[entry_key]
            index = int(entry_key[len("entry"):])
            context = f"{group_key}/{entry_key}"
            entry_msb, entry_lsb = one_span(str(entry.get("_slot")),
                                            f"{context}/_slot")
            map_msb, map_lsb = one_span(str(entry.get("mapping")),
                                        f"{context}/mapping")

            for unit, body in entry.items():
                if not isinstance(body, dict) or "mapping_value" not in body:
                    continue
                unit_context = f"{context}/{unit}"
                mapping_value = parse_int(body["mapping_value"], 2, unit_context)
                type_msb, type_lsb = one_span(str(body.get("type_code")),
                                              f"{unit_context}/type_code")

                for type_name, type_body in (body.get("types") or {}).items():
                    type_context = f"{unit_context}/{type_name}"
                    type_value = parse_int(type_body["type_code_bin"], 2,
                                           type_context)
                    op_msb, op_lsb = one_span(str(type_body.get("opcode")),
                                              f"{type_context}/opcode")

                    operands = []
                    for text in (type_body.get("operand_fields") or []):
                        name_match = OPERAND_FIELD.match(text)
                        if name_match is None:
                            raise SystemExit(
                                f"{type_context}: cannot name field {text!r}")
                        field_msb, field_lsb = one_span(text, type_context)
                        operands.append({
                            "field": name_match.group(1),
                            "aliases": [a.strip() for a in
                                        name_match.group(2).split(",")
                                        if a.strip()],
                            "msb": field_msb,
                            "lsb": field_lsb,
                        })

                    reserved = [{"msb": m, "lsb": l}
                                for m, l in spans(type_body.get("reserved") or "")]

                    shape = {
                        "entry_count": entry_count,
                        "header_entry_num": entry_num,
                        "entry_index": index,
                        "entry_msb": entry_msb,
                        "entry_lsb": entry_lsb,
                        "unit": unit,
                        "type": type_name,
                        "mapping": {"msb": map_msb, "lsb": map_lsb,
                                    "value": mapping_value},
                        "type_code": {"msb": type_msb, "lsb": type_lsb,
                                      "value": type_value},
                        "opcode_field": {"msb": op_msb, "lsb": op_lsb},
                        "operands": operands,
                        "reserved": reserved,
                    }
                    verify_shape(shape, type_context)

                    seen_opcodes: dict[int, str] = {}
                    for row in (type_body.get("mapping") or []):
                        name = str(row.get("instruction", "")).strip()
                        if not name:
                            raise SystemExit(f"{type_context}: unnamed mapping row")
                        opcode = parse_int(row.get("opcode", ""), 16,
                                           f"{type_context}/{name}")
                        if opcode in seen_opcodes:
                            raise SystemExit(
                                f"{type_context}: opcode {opcode:#x} used by both"
                                f" {seen_opcodes[opcode]} and {name}")
                        seen_opcodes[opcode] = name
                        if opcode >= 1 << (op_msb - op_lsb + 1):
                            raise SystemExit(
                                f"{type_context}/{name}: opcode does not fit")

                        # Blank means this instruction leaves the field unused.
                        uses = {}
                        for operand in operands:
                            spelled = str(row.get(operand["field"], "")).strip()
                            uses[operand["field"]] = spelled or None
                            if spelled and spelled not in operand["aliases"]:
                                raise SystemExit(
                                    f"{type_context}/{name}: operand {spelled!r}"
                                    f" is not one of {operand['aliases']}")

                        placements.append({
                            "instruction": name,
                            "opcode": opcode,
                            "operand_use": uses,
                            **shape,
                        })
    return geometry, placements


def verify_shape(shape: dict, context: str) -> None:
    """Every bit of the entry window is claimed exactly once."""
    claimed: dict[int, str] = {}

    def claim(name: str, msb: int, lsb: int) -> None:
        if msb > shape["entry_msb"] or lsb < shape["entry_lsb"]:
            raise SystemExit(f"{context}: {name} falls outside the entry window")
        for bit in range(lsb, msb + 1):
            if bit in claimed:
                raise SystemExit(
                    f"{context}: bit {bit} claimed by both {claimed[bit]} and"
                    f" {name}")
            claimed[bit] = name

    claim("mapping", shape["mapping"]["msb"], shape["mapping"]["lsb"])
    claim("type_code", shape["type_code"]["msb"], shape["type_code"]["lsb"])
    claim("opcode", shape["opcode_field"]["msb"], shape["opcode_field"]["lsb"])
    for operand in shape["operands"]:
        claim(operand["field"], operand["msb"], operand["lsb"])
    for reserved in shape["reserved"]:
        claim("reserved", reserved["msb"], reserved["lsb"])

    window = set(range(shape["entry_lsb"], shape["entry_msb"] + 1))
    if set(claimed) != window:
        missing = sorted(window - set(claimed))
        raise SystemExit(f"{context}: entry window has unclaimed bits {missing}")


# An operand's register file is decided by how the instruction spells it, and
# every alias in the database is one of these shapes. Selector and immediate
# fields take their width from the field itself.
GPR_ALIASES = {"rt", "rs", "rs1", "rs2", "rs3"}
DR_ALIASES = {"rtd", "rsd", "rsd1", "rsd2", "rtd1", "rtd2"}


# Instruction properties the generic CodeGen layer reads off the MCInstrDesc.
# materializeMultiOpcodeInstrs commits a logical to its member before AsmPrinter
# runs, so it is the *member's* Desc that answers, and a member without these
# is not merely imprecise -- see load_instruction_flags.
#
# hasSideEffects is deliberately NOT copied: TableGen infers it when a def
# leaves it unset, and writing the resolved value onto every member would
# freeze that inference rather than reproduce it.
PROPERTY_FLAGS = ("isBranch", "isTerminator", "isCall", "isBarrier",
                  "isIndirectBranch", "isReturn", "isNotDuplicable",
                  # mayLoad/mayStore are inferred by TableGen FROM THE PATTERN,
                  # and a member has no pattern, so leaving them off does not
                  # make the member imprecise -- it makes it claim the
                  # instruction touches no memory. MachineVerifier says so:
                  # "Missing mayStore flag" on every store member, 1012 times
                  # across the CodeGen suite. Same argument as the seven above:
                  # the member's Desc is the one the generic layer reads.
                  "mayLoad", "mayStore")


def load_instruction_flags(path: Path) -> dict[str, dict]:
    """Per-logical instruction properties, read from TableGen's own output.

    Format E turns one logical into up to seven members, and each member needs
    the properties of the logical it expands. Getting them wrong is silent:
    with no isTerminator, MachineBasicBlock::terminators() comes back empty,
    AsmPrinter::isBlockOnlyReachableByFallthrough decides a real branch target
    is fallthrough-only, the label is emitted as a `// %bb.1:` comment while
    the branch still references .LBB0_1, and the assembler then rejects its own
    compiler's output with "Undefined temporary symbol".

    These are LLVM-side facts, not database facts, and they are not derivable
    from the database's Behavior text. Behavior would give the ten branches and
    JAL/JALR, but it says nothing about the twelve comparison instructions that
    carry Defs = [SFR] (SEQ64, X2SEQ32, X4SLE16, ...), nor about the caller
    clobber list on the call forms. So read them from the logical, which is
    what TableGen has already resolved -- including through `let ... in` blocks
    that a text scan of the .td would have to re-implement:

        llvm-tblgen --dump-json -I llvm/lib/Target/Haydn -I llvm/include \\
            llvm/lib/Target/Haydn/Haydn.td -o haydn-records.json
    """
    records = json.loads(path.read_text(encoding="utf-8"))
    flags: dict[str, dict] = {}
    for name, record in records.items():
        if not isinstance(record, dict) or "Namespace" not in record:
            continue
        entry: dict = {}
        for flag in PROPERTY_FLAGS:
            if record.get(flag):
                entry[flag] = 1
        registers = [r["def"] if isinstance(r, dict) else str(r)
                     for r in (record.get("Defs") or [])]
        if registers:
            entry["Defs"] = registers
        if entry:
            flags[name] = entry
    if not flags:
        raise SystemExit(f"{path}: no instruction records carried properties;"
                         " is this the output of llvm-tblgen --dump-json?")
    return flags


def load_operand_classes(path: Path):
    """Per-logical operand classes worth inheriting, from the same tblgen JSON.

    The generator synthesizes generic `simmN` / `uimmN` for every immediate.
    That loses whatever the logical's purpose-built operand class carried --
    and what it carries is the `EncoderMethod`, which is where BOTH the fixup
    kind and the immediate scaling live. A member with a generic class falls
    through to getMachineOpValue and then getExprFixupKind, which has no case
    for e.g. SET_HWLOOP, so a hardware-loop setup emitted two R_HAYDN_32
    pointing at an instruction. See FORMAT-E-SWITCH-PLAN.md 5.10; it is the
    same gap as 6.9's property flags, on the operand axis.

    ONLY classes with BOTH an explicit width and an EncoderMethod are taken.
    The width lets the member's field be checked against the class, so a
    mismatched inherit cannot silently mis-encode. Excluding the width-agnostic
    Operand<OtherVT> classes (brtarget, calltarget) is deliberate: their
    encoders dispatch on the OPCODE, branches already reach the right fixup
    kind through getExprFixupKind, and rerouting a working path is not worth
    the risk.
    """
    records = json.loads(path.read_text(encoding="utf-8"))
    widths: dict[str, tuple[str, int]] = {}
    for name, record in records.items():
        if not isinstance(record, dict) or not record.get("EncoderMethod"):
            continue
        match = re.search(r"is[US]Int<(\d+)>", json.dumps(record))
        if match:
            widths[name] = (name, int(match.group(1)))

    out: dict[str, dict[str, tuple[str, int]]] = {}
    raw: dict[str, dict[str, str]] = {}
    for name, record in records.items():
        if not isinstance(record, dict) or "InOperandList" not in record:
            continue
        per: dict[str, tuple[str, int]] = {}
        per_raw: dict[str, str] = {}
        for arg in record["InOperandList"].get("args", []):
            cls = arg[0].get("def") if isinstance(arg[0], dict) else str(arg[0])
            if not arg[1]:
                continue
            per_raw[str(arg[1])] = cls
            if cls in widths:
                per[str(arg[1])] = widths[cls]
        if per:
            out[name] = per
        if per_raw:
            raw[name] = per_raw
    return out, raw


def inherited_operand_class(classes: dict, logical: str, alias: str,
                            width: int) -> str | None:
    r"""The logical's class for \p alias, when it is safe to inherit.

    Matched by NAME rather than position: the member and the logical disagree
    on arity for 280 of the 684 logicals (tied writebacks, database reshapes),
    so a positional match would be wrong far more often than it is right. The
    member's alias carries the logical's operand name as a suffix --
    `uimm6_offset1` for `offset1` -- which is unambiguous where it matches at
    all. The width must agree; otherwise the class is left alone.
    """
    per = classes.get(logical)
    if not per:
        return None
    for name, (cls, class_width) in per.items():
        if alias == name or alias.endswith("_" + name):
            return cls if class_width == width else None
    return None


# The width-agnostic branch/call operand classes, mapped to a width-matched
# format E class that carries the same encoder.
#
# These cannot go through inherited_operand_class: they are Operand<OtherVT>
# with no width to check a field against. Branch/call scale, signedness, and
# PC-base equations are unpublished; this map only selects the already-declared
# width-matched class for the member field. Do not treat a historical
# divide-by-two encoder as product law.
BRANCH_CLASS_FOR_WIDTH = {
    ("brtarget", 12): "brtarget_e12",
    ("calltarget", 20): "calltarget_e20",
}


def branch_operand_class(classes_raw: dict, logical: str, width: int) -> str | None:
    """The format E branch/call class for this member's immediate, if any.

    Matched by UNIQUENESS, not by name: the logical calls a branch offset
    `offset` or `target` while the database calls the member's field `imm12`,
    so there is no name to match on. A branch logical carries exactly one
    brtarget / calltarget operand, so "the logical has exactly one of these and
    the member's field is the right width" identifies it without ambiguity.
    Anything else is left alone.
    """
    per = classes_raw.get(logical)
    if not per:
        return None
    targets = [cls for cls in per.values() if cls in ("brtarget", "calltarget")]
    if len(targets) != 1:
        return None
    return BRANCH_CLASS_FOR_WIDTH.get((targets[0], width))


def operand_type(alias: str, width: int, context: str,
                 signed: bool | None = None) -> str:
    if alias in GPR_ALIASES:
        expect, kind = 4, "GPR32"
    elif alias in DR_ALIASES:
        expect, kind = 4, "DR64"
    elif alias.endswith("_sel"):
        return f"uimm{width}"
    elif alias.startswith("uimm") or alias.startswith("imm"):
        # Signedness is the Behavior's statement, not the field name's --
        # see load_immediate_signedness.
        if signed is None:
            raise SystemExit(f"{context}: no signedness for immediate {alias!r}")
        # A 32-bit field spans the whole value, so simmN and uimmN cover the
        # same bit patterns and the printer's sign extension is the identity:
        # `movei_h d0, -1` assembles and disassembles back to `-1`. The only
        # thing the choice changes there is which spellings the parser takes,
        # and negative literals are what existing code writes. Below 32 bits
        # the difference IS observable in the printed text -- `lui r3, 4095`
        # came back as `lui r3, -1` -- and that is the defect this reader
        # exists to stop.
        if width >= 32:
            return f"simm{width}"
        return f"{'s' if signed else 'u'}imm{width}"
    else:
        raise SystemExit(f"{context}: unknown operand alias {alias!r}")
    if width != expect:
        raise SystemExit(f"{context}: {alias} needs {expect} bits, field has {width}")
    return kind


def td_bits(value: int, width: int, context: str) -> str:
    """A TableGen bit-range assignment must match the range width exactly, so
    constants are padded rather than written in their shortest form."""
    if value < 0 or value >= 1 << width:
        raise SystemExit(f"{context}: {value} does not fit {width} bits")
    return "0b" + format(value, f"0{width}b")


def td_identifier(name: str) -> str:
    """TableGen record names admit only identifier characters."""
    cleaned = re.sub(r"[^0-9A-Za-z_]", "_", name)
    return cleaned.strip("_")


def verify_decodable(placements: list[dict]) -> None:
    """Within one entry position, (mapping, type_code, opcode) must name exactly
    one instruction, or the disassembler cannot tell two encodings apart."""
    by_type: dict[tuple, dict[int, str]] = {}
    by_encoding: dict[tuple, dict[tuple, str]] = {}
    for p in placements:
        unit_key = (p["entry_count"], p["entry_index"], p["unit"])
        seen_types = by_type.setdefault(unit_key, {})
        code = p["type_code"]["value"]
        if seen_types.setdefault(code, p["type"]) != p["type"]:
            raise SystemExit(
                f"{unit_key}: type code {code:#x} is used by both"
                f" {seen_types[code]} and {p['type']}")

        position = (p["entry_count"], p["entry_index"])
        seen = by_encoding.setdefault(position, {})
        key = (p["mapping"]["value"], code, p["opcode"])
        if seen.setdefault(key, p["instruction"]) != p["instruction"]:
            raise SystemExit(
                f"{position}: encoding {key} decodes to both"
                f" {seen[key]} and {p['instruction']}")


def placement_index(placements: list[dict]) -> dict[tuple[int, int, str], int]:
    """Dense enumeration of the (entry position, unit) pairs the encoding admits.

    This is the member index inside an instruction's alternatives, so `1 << index`
    stays what it has always been: a mask of the positions an instruction is
    legal in. A flat per-instruction ordinal would not survive that -- it carries
    no position, so getLegalSlots could not be built from it.

    NOP is excluded from the uniqueness requirement below: it exists under every
    instruction type as the all-zero entry encoding, so it alone has several
    placements per pair. An empty entry is not materialized through alternatives.
    """
    pairs = sorted({(p["entry_count"], p["entry_index"], p["unit"])
                    for p in placements})
    if len(pairs) > 64:
        raise SystemExit(
            f"{len(pairs)} placement pairs — a 1<<index legality mask no longer"
            " fits 64 bits, so the mask type has to widen before this grows")

    seen: dict[tuple, str] = {}
    for p in placements:
        if p["instruction"] == "NOP":
            continue
        key = (p["instruction"], p["entry_count"], p["entry_index"], p["unit"])
        if seen.setdefault(key, p["type"]) != p["type"]:
            raise SystemExit(
                f"{p['instruction']} has two placements at the same position and"
                f" unit ({seen[key]} and {p['type']}), so the pair cannot index"
                " its alternatives")
    return {pair: index for index, pair in enumerate(pairs)}


def canonical_members(placements: list[dict]) -> list[dict]:
    """One member per (instruction, entry position, unit).

    Only NOP needs this. It exists under every instruction type as the encoding
    of an empty entry -- opcode zero, no operands, differing from its siblings
    only in the type code -- so the database lists it once per type. The form
    with type code zero is the canonical one: the entry payload is then all
    zeros above the unit mapping, which is what an unoccupied entry is. Emitting
    the others would only produce duplicate record names for one slot.
    """
    kept: dict[tuple, dict] = {}
    for p in placements:
        key = (p["instruction"], p["entry_count"], p["entry_index"], p["unit"])
        previous = kept.get(key)
        if previous is None:
            kept[key] = p
            continue
        if p["instruction"] != "NOP":
            raise SystemExit(
                f"{p['instruction']} has two placements at the same position and"
                f" unit ({previous['type']} and {p['type']}); only NOP may")
        if p["opcode"] != 0 or any(p["operand_use"].values()):
            raise SystemExit(
                f"NOP under {p['type']} is not the empty encoding"
                f" (opcode {p['opcode']:#x})")
        if p["type_code"]["value"] < previous["type_code"]["value"]:
            kept[key] = p
    for key, chosen in kept.items():
        if key[0] == "NOP" and chosen["type_code"]["value"] != 0:
            raise SystemExit(
                f"NOP at {key[1]}-entry entry{key[2]}/{key[3]} has no type-code"
                " zero form, so it has no canonical empty encoding")
    return list(kept.values())


def emit_schedule(pipeline: dict, placements: list[dict]) -> list[str]:
    """Units as FuncUnits, and one itinerary class per (unit set, latency).

    A class states which resources can serve an instruction, which is exactly
    what Available says, so the slot-shaped Slot0_ALU / Slot012_ALU classes have
    a direct replacement rather than a translation. Logicals take the full set;
    a member is pinned to one unit and takes the singleton.
    """
    out = [
        "//===--- Units and itineraries "
        + "-" * 44 + "===//",
        "// Each unit is a FuncUnit and each itinerary class names the units that",
        "// can serve it, with the data latency the ISA gives. Latencies of 'var'",
        "// are SIN_COS and ARCTAN, whose latency is uimm4 + 2 and cannot be a",
        "// static class; they are given the stage but no latency so the value has",
        "// to come from the operand.",
        "",
    ]
    for unit in UNITS:
        out.append(f"def U_{unit} : FuncUnit;")
    out.append("")

    # NOP is not an ISA instruction -- it is the encoding of an empty entry, so
    # instruction_type_index.json does not carry it -- but the entry it sits in
    # still belongs to some unit, and it can sit in any of them. Without a class
    # of its own it would be the one logical left holding a slot class.
    classes: dict[str, tuple[tuple[str, ...], object]] = {
        itinerary_name(UNITS, 1): (UNITS, 1),
    }
    for name, (units, latency) in pipeline.items():
        classes[itinerary_name(units, latency)] = (units, latency)
    for p in placements:
        info = pipeline.get(p["instruction"])
        if info is None:
            continue
        classes[itinerary_name((p["unit"],), info[1])] = ((p["unit"],), info[1])

    for name in sorted(classes):
        out.append(f"def {name} : InstrItinClass;")
    out.append("")
    # defvars rather than a ProcessorItineraries of their own: an itinerary
    # class only reaches the generated tables through the processor model that
    # is actually selected, so these have to be concatenated into the live one.
    out.append("defvar HaydnFormatEUnits = ["
               + ", ".join(f"U_{u}" for u in UNITS) + "];")
    out.append("")
    out.append("defvar HaydnFormatEItinData = [")
    rows = []
    for name in sorted(classes):
        units, latency = classes[name]
        stage = "[" + ", ".join(f"U_{u}" for u in units) + "]"
        cycles = "[]" if latency == "var" else f"[{latency}]"
        rows.append(f"    InstrItinData<{name}, [InstrStage<1, {stage}>], {cycles}>")
    out.append(",\n".join(rows))
    out += ["];", ""]
    return out


def roundtrip(geometry: dict, placements: list[dict]) -> str:
    """Encode every placement and decode it back, independently of LLVM.

    The point is an oracle that does not come from the thing being tested. Once
    the toolchain emits format E its test expectations have to be regenerated
    from its own output, which proves nothing on its own; this says whether the
    database can encode and decode each placement unambiguously, so a
    disagreement afterwards is attributable.

    Encoding a placement means writing the unit mapping, the type code, the
    opcode and a value per operand field into the entry window. Decoding means
    reading the three selectors back, finding the one placement they name, and
    recovering the operand values. Both directions use nothing but the table.
    """
    # Decode index: (entry_count, entry_index) -> (mapping, type_code, opcode).
    by_key: dict[tuple, list[dict]] = {}
    for p in placements:
        key = (p["entry_count"], p["entry_index"], p["mapping"]["value"],
               p["type_code"]["value"], p["opcode"])
        by_key.setdefault(key, []).append(p)

    # A malformed table is reported, not raised: a verifier that stops at the
    # first anomaly cannot say how widespread one is.
    def put(word: int, msb: int, lsb: int, value: int, base: int,
            note: list[str], what: str) -> int:
        width = msb - lsb + 1
        if value >= 1 << width:
            note.append(f"{what} value {value} does not fit {width} bits")
            return word
        return word | (value << (lsb - base))

    def get(word: int, msb: int, lsb: int, base: int) -> int:
        return (word >> (lsb - base)) & ((1 << (msb - lsb + 1)) - 1)

    checked = 0
    failures: list[str] = []
    for p in placements:
        base = p["entry_lsb"]
        where = (f"{p['instruction']} @ {p['entry_count']}e{p['entry_index']}"
                 f"/{p['unit']}/{p['type']}")
        note: list[str] = []
        word = 0
        word = put(word, p["mapping"]["msb"], p["mapping"]["lsb"],
                   p["mapping"]["value"], base, note, "mapping")
        word = put(word, p["type_code"]["msb"], p["type_code"]["lsb"],
                   p["type_code"]["value"], base, note, "type_code")
        word = put(word, p["opcode_field"]["msb"], p["opcode_field"]["lsb"],
                   p["opcode"], base, note, "opcode")
        # A distinct value per field, wide enough to catch a swapped pair and
        # small enough to fit the narrowest field.
        expect: dict[str, int] = {}
        for index, operand in enumerate(p["operands"]):
            if p["operand_use"].get(operand["field"]) is None:
                continue
            width = operand["msb"] - operand["lsb"] + 1
            if width <= 0:
                note.append(f"{operand['field']} spans no bits"
                            f" ([{operand['msb']}:{operand['lsb']}])")
                continue
            value = (index * 5 + 3) % (1 << width)
            expect[operand["field"]] = value
            word = put(word, operand["msb"], operand["lsb"], value, base, note,
                       operand["field"])

        failures += [f"{where}: {n}" for n in note]
        key = (p["entry_count"], p["entry_index"],
               get(word, p["mapping"]["msb"], p["mapping"]["lsb"], base),
               get(word, p["type_code"]["msb"], p["type_code"]["lsb"], base),
               get(word, p["opcode_field"]["msb"], p["opcode_field"]["lsb"], base))
        found = by_key.get(key, [])
        names = {q["instruction"] for q in found}
        if names != {p["instruction"]}:
            failures.append(
                f"{p['instruction']} @ {p['entry_count']}e{p['entry_index']}"
                f"/{p['unit']}/{p['type']} decodes to {sorted(names) or 'nothing'}")
            continue
        for operand in p["operands"]:
            field = operand["field"]
            if field not in expect:
                continue
            got = get(word, operand["msb"], operand["lsb"], base)
            if got != expect[field]:
                failures.append(
                    f"{p['instruction']}: {field} encoded {expect[field]},"
                    f" decoded {got}")
        if word >= 1 << (p["entry_msb"] - base + 1):
            failures.append(f"{p['instruction']}: entry word overflows its window")
        checked += 1

    lines = [f"round-trip over {checked} placements"]
    if failures:
        lines.append(f"  FAILURES: {len(failures)}")
        lines += [f"    {f}" for f in failures[:20]]
    else:
        lines.append("  every placement encodes and decodes back to itself,"
                     " operands included")
    # The count travels with the text so main() can EXIT on it. It used not
    # to: this printed "FAILURES: n" and the process still returned 0, so
    # anything using it as a gate through $? saw a pass. Same shape as the
    # vacuous-CHECK-NOT gate reading zero files (plan 6.15) -- a check that
    # cannot fail is not a check.
    return "\n".join(lines) + "\n", len(failures)


def emit_schedule_file(pipeline: dict, placements: list[dict]) -> str:
    """The unit scheduling model on its own.

    It defines FuncUnits and itinerary classes and no instruction records, so it
    can be included while Bundle128 is still live: nothing here claims an
    alternates index, which is what stops the encoding half from coexisting.
    Retargeting a logical from its Slot* class to the Unit_* one is then a
    per-family step rather than part of the switch.
    """
    header = [
        "//===- HaydnFormatESchedule.td - generated, do not edit ----*- tablegen -*-===//",
        "//",
        "// Generated by utils/haydn_encoding.py from instruction_type_index.json.",
        "//",
        "// Included while Bundle128 is live. The classes below are unreferenced",
        "// until a logical is retargeted onto them; defining them changes nothing",
        "// on its own.",
        "//===----------------------------------------------------------------------===//",
        "",
    ]
    return "\n".join(header + emit_schedule(pipeline, placements))


def load_syntax_order(database: Path) -> dict[str, list[str]]:
    """Operand names in the order each instruction is written.

    `format_e_bit_layout_v2_2.json` lists an entry's fields in bit order, which
    says nothing about how the instruction reads. The Syntax in
    `instruction_type_index.json` does, and it is what the assembler and the
    printer have to agree with, so the emitted operand list follows it.
    """
    data = json.loads((database / INSTRUCTION_INDEX).read_text(encoding="utf-8"))
    order: dict[str, list[str]] = {}
    for entries in data.values():
        for entry in entries:
            name = str(entry.get("Instruction", "")).strip()
            if not name or name == "WFI<TBD>":
                continue
            syntax = str(entry.get("Syntax", "")).strip().strip("`").strip()
            head, _, tail = syntax.partition(" ")
            operands = [o.strip() for o in tail.split(",") if o.strip()]
            previous = order.get(name)
            if previous is not None and previous != operands:
                raise SystemExit(f"{name}: conflicting Syntax across type groups")
            order[name] = operands
    return order


def load_immediate_signedness(database: Path) -> dict[str, dict[str, str]]:
    r"""Per instruction, whether each immediate its Syntax names is signed.

    This used to be read off the field's NAME -- `uimm...` unsigned, anything
    else signed -- which is a spelling convention standing in for a semantic
    fact. It got `ADDI32 rt, rs, imm20` right by luck and `LUI rt, imm12`
    wrong: the database says `rt = {imm12, 20'b0}`, a bit concatenation, so
    the member declared `simm12` while the logical declared `uimm12`. Nothing
    failed. `llvm-mc` printed `lui r3, 4095` (the parser kept what was
    written) and `llvm-objdump` printed `lui r3, -1` for the same twelve
    bytes, because the decoder sign-extended a field that is a pattern rather
    than a number. `--emit roundtrip` cannot see it: the BITS agree, and the
    disagreement is between the parser and the decoder.

    The Behavior states it, so read it there:

    * `SEXT32(imm20)` / `$signed(imm8)`      -> signed
    * `ZEXT32(imm20)`                        -> unsigned
    * the field inside a `{...}` concatenation -> unsigned; it is a bit
      pattern being spliced, and a negative spelling of it is a lie
    * a shift amount (`rs << uimm5`)         -> unsigned
    * an address or arithmetic offset (`rs + (imm6 << 3)`) -> signed

    Anything the Behavior does not place is an ERROR rather than a default.
    Defaulting is what produced the LUI defect, and a wrong default here is
    invisible until someone reads disassembly.
    """
    data = json.loads((database / INSTRUCTION_INDEX).read_text(encoding="utf-8"))
    out: dict[str, dict[str, str]] = {}
    for entries in data.values():
        for entry in entries:
            name = str(entry.get("Instruction", "")).strip()
            if not name or name == "WFI<TBD>":
                continue
            syntax = str(entry.get("Syntax", ""))
            behavior = str(entry.get("Behavior", ""))
            per: dict[str, str] = {}
            for field in sorted(set(re.findall(r"\b(u?imm\d*)\b", syntax))):
                f = re.escape(field)
                if field.startswith("uimm"):
                    per[field] = "u"
                elif (re.search(r"SEXT[\d>\-]*\s*\(\s*" + f + r"\s*\)", behavior)
                      or re.search(r"\$signed\([^)]*\b" + f + r"\b", behavior)):
                    per[field] = "s"
                elif re.search(r"ZEXT[\d>\-]*\s*\(\s*" + f + r"\s*\)", behavior):
                    per[field] = "u"
                elif any(re.search(r"\b" + f + r"\b", group)
                         for group in re.findall(r"\{([^{}]*)\}", behavior)):
                    per[field] = "u"
                elif re.search(r"(<<|>>>?)\s*" + f + r"\b", behavior):
                    per[field] = "u"
                elif re.search(r"[+\-]\s*\(?\s*" + f + r"\b", behavior):
                    per[field] = "s"
                else:
                    raise SystemExit(
                        f"{name}: Behavior does not say whether {field} is "
                        f"signed, and guessing is what this reader exists to "
                        f"stop.\n  Syntax:   {syntax.strip()}\n"
                        f"  Behavior: {behavior.strip()}")
            previous = out.get(name)
            if previous is not None and previous != per:
                raise SystemExit(
                    f"{name}: conflicting immediate signedness across type "
                    f"groups: {previous} vs {per}")
            out[name] = per
    return out


def immediate_is_signed(signedness: dict, logical: str, alias: str,
                        context: str) -> bool:
    """Whether \\p alias on \\p logical is signed, per the database.

    The member's alias is the Syntax's operand name, sometimes with a suffix
    (`uimm6_offset1` for `offset1`), so match exactly first and by prefix
    after -- the same rule `inherited_operand_class` uses.
    """
    per = signedness.get(logical)
    if per:
        if alias in per:
            return per[alias] == "s"
        for name, kind in per.items():
            if alias.startswith(name):
                return kind == "s"
    raise SystemExit(f"{context}: the database states no signedness for "
                     f"immediate {alias!r} on {logical}")


READ_PORTS = ("GPR_Read_Port", "DR_Read_Port", "AR_Read_Port", "SFR_Read_Port")
WRITE_PORTS = ("GPR_Write_Port", "DR_Write_Port", "AR_Write_Port", "SFR_Write_Port")


def load_operand_roles(database: Path) -> dict[str, tuple[set[str], set[str]]]:
    r"""Per instruction, the operands it READS and the ones it WRITES.

    This is what decides whether a member operand is a def, and the database
    states it directly: every instruction carries `GPR/DR/AR/SFR_Read_Port`
    and `_Write_Port` over the same operand names its Syntax uses.

    It replaces asking whether the bit-layout field the operand landed in is
    called `dest`, which is a LAYOUT fact and not a semantic one. The two come
    apart because an entry's field set depends on its (entry, unit):

        2-entry entry0/ALU0   {"dest": "rsd1", "src": "rsd2"}
        3-entry entry0/ALU2   {"dest": "   ", "src1": "rsd1", "src2": "rsd2"}

    Both rows are `X2SEQ32 rsd1, rsd2`, whose only write is `SFR_Write_Port`
    -- it has no register destination at all, and the ALU2 row says so by
    leaving `dest` blank. The narrower row has nowhere to put a second source,
    so `rsd1` sits in the field named `dest` and the old rule called it an out.
    That made the SAME instruction a def on one unit and not on another, and it
    failed both ways: it invented a destination for the SFR compares and lost
    the real one for LUI, ZERO_GPR, CSRR and MOVESFR2GPR on six placements of
    seven. See FORMAT-E-SWITCH-PLAN.md 5.11.

    A write reaches a register FILE through a selector (`ar[ar_sel]`) or names
    an implicit register (`SFR`); neither makes an operand a def, so both drop
    out. Anything else has to be a known register alias -- a re-delivered
    database that invents a shape must fail here rather than silently decide
    every operand of that instruction is a source.
    """
    data = json.loads((database / INSTRUCTION_INDEX).read_text(encoding="utf-8"))
    indirect = re.compile(r"\w+\[\w+\]")
    implicit = re.compile(r"[A-Z]+\d*")
    known = GPR_ALIASES | DR_ALIASES
    def ports(entry: dict, name: str, keys: tuple[str, ...]) -> set[str]:
        found: set[str] = set()
        for key in keys:
            for port in entry.get(key) or []:
                port = str(port).strip()
                if indirect.fullmatch(port) or implicit.fullmatch(port):
                    continue
                if port not in known:
                    raise SystemExit(
                        f"{name}: {key} names {port!r}, which is neither a"
                        " register alias, an implicit register nor a"
                        " selected file")
                found.add(port)
        return found

    roles: dict[str, tuple[set[str], set[str]]] = {}
    for entries in data.values():
        for entry in entries:
            name = str(entry.get("Instruction", "")).strip()
            if not name or name == "WFI<TBD>":
                continue
            roles[name] = (ports(entry, name, READ_PORTS),
                           ports(entry, name, WRITE_PORTS))
    return roles


def syntax_ordered_operands(placement: dict,
                            syntax: dict[str, list[str]]) -> list[dict]:
    """The placement's used fields, in the order the instruction is written.

    `operand_fields` lists the entry's fields in bit order, which is not the
    order the instruction reads. Bits stay keyed by field, so only the operand
    list and the asm string follow the Syntax; encoding is unaffected.
    verify_operand_sets cross-checks that the two database files name the same
    operand set for this instruction.

    Both the emitter and member_operand_shape order operands through here. They
    have to agree: `CSRW uimm8, rs` is written immediate-first while its bit
    layout puts `rs` first, so splitting into outs and ins before this sort
    yields an operand list the logical does not match.
    """
    used = [o for o in placement["operands"]
            if placement["operand_use"].get(o["field"])]
    written = syntax.get(placement["instruction"])
    if written is not None:
        rank = {name: i for i, name in enumerate(written)}
        used.sort(key=lambda o: rank[placement["operand_use"][o["field"]]])
    return used


def load_tie_positions(path: Path) -> dict[str, list[int]]:
    r"""Where each logical puts the IN of a tied writeback, among its ins.

    The database describes a written-back register as ONE operand; CodeGen
    presents it as two, an out and a tied in. Where that second one lands is
    therefore an LLVM-side fact that the database cannot state, and the
    logicals do not agree with each other about it:

        D_SDW_POST_IMM rtd, rs, imm6   ins ($rtd, $rs, $imm)   tie 2nd
        PLDWWUA_POST   ar_sel, rs      ins ($rs, $ar_sel)      tie 1st

    Both are legal -- the AsmString names operands, so either order prints the
    same -- but the encoder reads the MCInst positionally, so the member has
    to make the same choice its logical did. Read as an index rather than by
    name: the two sides name the same register differently (`rd`/`rd_in`
    against the database's `rtd`), which is exactly what silently defeated the
    previous tie restoration.
    """
    records = json.loads(path.read_text(encoding="utf-8"))
    clause = re.compile(r"\s*\$(\w+)\s*=\s*\$(\w+)\s*")
    positions: dict[str, list[int]] = {}
    for name, record in records.items():
        if not isinstance(record, dict):
            continue
        text = str(record.get("Constraints") or "").strip()
        if not text:
            continue
        ins = [str(arg[1])
               for arg in (record.get("InOperandList") or {}).get("args", [])]
        found = []
        for part in text.split(","):
            match = clause.fullmatch(part)
            if match is None:
                continue
            # `Constraints` does not say which side is the in: the logicals
            # write both `"$rs = $rs_wb"` (in first) and `"$rd = $rd_in"` (in
            # second). The in is whichever name the InOperandList carries.
            for side in (match.group(1), match.group(2)):
                if side in ins:
                    found.append(ins.index(side))
                    break
        if found:
            positions[name] = sorted(found)
    return positions


def member_operand_shape(placement: dict,
                         roles: dict[str, tuple[set[str], set[str]]],
                         syntax: dict[str, list[str]],
                         tie_positions: dict[str, list[int]] | None = None,
                         ) -> tuple[list[str], list[str], list[str]]:
    """The member's operand aliases in MCInst order, plus its Constraints.

    Shared with emit_tablegen so check_operand_agreement cannot drift from
    what the generator actually emits. The previous check MODELLED the
    emitter -- it added one for a tied writeback whenever the logical carried
    a `Constraints` -- while the emitter only restored that tie when the
    logical happened to name it the way the database does. Where the names did
    not line up the emitter silently skipped the tie and the check credited it
    anyway, so 823 placements over 164 logicals were reported as agreeing
    while their member really was one operand short. `F2MULAA32RS_HHLL` is the
    shape: the logical ties `$rd = $rd_in`, the database calls the same
    register `rtd`, nothing matched, and every MAC accumulate encoded its
    accumulator as its first source.

    The tie is now read off the database too -- an operand the instruction
    both reads and writes is one register CodeGen presents twice -- so it no
    longer depends on the two sides agreeing about a name.
    """
    used = syntax_ordered_operands(placement, syntax)
    reads, writes = roles.get(placement["instruction"], (None, None))
    aliases = {placement["operand_use"][o["field"]] for o in used}

    def written(operand: dict, alias: str) -> bool:
        if writes is None:
            return operand["field"].startswith("dest")
        return alias in writes

    # Read AND written is one register that CodeGen can present twice, as an
    # out and a tied in.
    tied_names = [alias for operand in used
                  if (alias := placement["operand_use"][operand["field"]])
                  and written(operand, alias) and reads is not None
                  and alias in reads and f"{alias}_wb" not in aliases]

    # Whether there IS a tie, and where its in sits, are CodeGen-side
    # presentation choices that only the logical can state -- the database
    # says no more than that the register is read and written. So the member
    # follows the logical: the two have to match operand for operand, or the
    # encoder reads past the end of the MCInst and llc aborts inside
    # MCOperand::operator[]. Where the logical then contradicts the database
    # -- 87 accumulating MAC logicals declared no tie at all, so their
    # accumulator input was not pinned to the register the hardware reads --
    # that is reported on its own axis rather than papered over here.
    #
    # A tie comes in two shapes and only one of them adds an operand:
    #
    #   SPLIT    the database has ONE operand and CodeGen presents two, an out
    #            and a tied in. Restoring it grows the member by one.
    #   IN-PLACE the logical ties an out and an in it ALREADY has, as
    #            SLLI64's `$rtd = $rsd` does. Nothing is added; the member just
    #            has to say so.
    #
    # Missing the in-place shape is not cosmetic. MachineInstr carries the
    # logical's tie flags, and after materializeMultiOpcodeInstrs the member's
    # Desc is what MachineVerifier checks them against: "Explicit def tied to
    # explicit use without tie constraint", 194 times.
    wanted = (tie_positions or {}).get(placement["instruction"], [])
    in_place = not tied_names and wanted

    outs: list[str] = []
    ins: list[str] = []
    for operand in used:
        alias = placement["operand_use"][operand["field"]]
        if written(operand, alias):
            outs.append(f"{alias}_wb" if alias in tied_names else alias)
        if alias in tied_names or not written(operand, alias):
            # Everything else follows the Syntax, with the write-only operands
            # taken out.
            ins.append(alias)

    constraints: list[str] = []
    if tied_names:
        ins = [name for name in ins if name not in set(tied_names)]
        for index, name in zip(wanted, tied_names):
            ins.insert(index, name)
        constraints = [f"${name} = ${name}_wb" for name in tied_names]
    elif in_place:
        # Pair the logical's tied ins with the member's outs in order. Only
        # positions are used: the two sides name the same register differently,
        # which is what defeated matching by name in the first place.
        for position, index in enumerate(wanted):
            if position < len(outs) and index < len(ins):
                constraints.append(f"${ins[index]} = ${outs[position]}")
    return outs, ins, constraints


def verify_operand_sets(placements: list[dict],
                        syntax: dict[str, list[str]]) -> None:
    """Every operand the Syntax names must have a field in the bit layout.

    The two database files can disagree without any other check noticing. Every
    bit of an entry window is still claimed, because a mapping row that leaves a
    field blank simply encodes it as zero, and the encode/decode round-trip only
    ever sees what the encoder already believed. So an instruction whose Syntax
    takes four registers can be emitted with three, the fourth silently pinned
    to zero, and nothing downstream objects.
    """
    missing: dict[str, tuple[list[str], list[str]]] = {}
    for p in placements:
        written = syntax.get(p["instruction"])
        if written is None:
            continue
        spelled = sorted(p["operand_use"][o["field"]] for o in p["operands"]
                         if p["operand_use"].get(o["field"]))
        if sorted(written) != spelled:
            missing.setdefault(p["instruction"], (sorted(written), spelled))
    if not missing:
        return
    lines = [f"{len(missing)} instruction(s) whose Syntax and bit layout name"
             " different operands:"]
    for name, (written, spelled) in sorted(missing.items()):
        absent = sorted(set(written) - set(spelled))
        lines.append(f"  {name:16} Syntax {','.join(written):28}"
                     f" layout {','.join(spelled):22}"
                     f" missing {','.join(absent) or '-'}")
    lines.append("")
    lines.append("Refusing to emit: the encoding would pin the missing operand")
    lines.append("to zero, which compiles and is wrong. Fix the mapping rows in")
    lines.append("format_e_bit_layout_v2_2.json, or correct the Syntax if the")
    lines.append("operand really is not encoded.")
    raise SystemExit("\n".join(lines))


def check_operand_agreement(placements: list[dict], flags_path: Path,
                            roles: dict[str, tuple[set[str], set[str]]],
                            syntax: dict[str, list[str]],
                            tie_positions: dict[str, list[int]]) -> str:
    """Report every member whose operand list disagrees with its logical's.

    This is the § 5.11 hazard made measurable. After
    materializeMultiOpcodeInstrs the MEMBER's MCInstrDesc drives the encoder
    while the MCInst still carries the operands CodeGen built from the
    LOGICAL, so a disagreement makes the encoder read every later operand one
    position early. It is silent: no diagnostic, a plausible encoding, and a
    missing or wrong relocation. LUI emitted no relocation at all; the whole
    pre/post-increment load-store family encoded a register as its offset.

    Two axes, because count alone missed most of it:

    * **arity** -- the member declares a different NUMBER of operands, so
      everything after the difference is read from the wrong position.
    * **defs** -- the counts agree but the member disagrees about how many of
      them are outs. `MCInstrDesc::NumDefs` is where the operand numbering
      starts, and a member that calls a source a def describes an instruction
      that writes a register it does not write. This is the axis that caught
      the SFR compares inventing a destination and LUI losing its real one.

    Both are asked of member_operand_shape rather than modelled here; the
    previous version modelled the emitter and got 823 placements wrong.

    Nothing else can see this. --emit roundtrip never looks at the logical,
    --check only validates the database against itself, and the encoder and
    decoder agree with each other because both read the member. It is a .td
    fact on both sides, so it costs nothing to check.

    The count is a standing hazard, not a to-do list: it should only shrink.
    """
    records = json.loads(flags_path.read_text(encoding="utf-8"))

    # A logical operand is a register iff its class is a RegisterClass; a
    # member's is iff the database alias names a register file. Both sides are
    # closed sets, so this needs no per-target table.
    register_classes = {name for name, record in records.items()
                        if isinstance(record, dict)
                        and "RegisterClass" in (record.get("!superclasses") or [])}
    registers = GPR_ALIASES | DR_ALIASES

    def logical_kinds(record: dict) -> list[str]:
        kinds = []
        for key in ("OutOperandList", "InOperandList"):
            for arg in (record.get(key) or {}).get("args", []):
                cls = arg[0]
                if isinstance(cls, dict):
                    cls = cls.get("def") or str(cls)
                kinds.append("reg" if str(cls) in register_classes else "imm")
        return kinds

    arity: dict[str, int] = {}
    defs: dict[str, int] = {}
    kinds: dict[str, int] = {}
    ties: dict[str, int] = {}
    # `ties` is judged independently of the three position axes, so a
    # placement can land in two buckets; the headline counts each once.
    flagged: set[tuple[str, int, int, str]] = set()
    # Only the placements that become members: canonical_members is what
    # --emit td writes, and counting the rest reports defects in defs that do
    # not exist.
    for placement in canonical_members(placements):
        logical = placement["instruction"]
        record = records.get(logical)
        if not isinstance(record, dict) or "InOperandList" not in record:
            continue
        want_outs = len(record.get("OutOperandList", {}).get("args", []))
        wanted = want_outs + len(record["InOperandList"]["args"])
        key = (logical, placement["entry_count"], placement["entry_index"],
               placement["unit"])
        outs, ins, _ = member_operand_shape(placement, roles, syntax,
                                           tie_positions)

        # Independent of the position axes below: the database says this
        # register is both read and written, and the logical does not present
        # it that way. The member follows the logical so the encoder stays in
        # bounds (see member_operand_shape), which leaves the MODEL wrong --
        # an accumulator whose input is not pinned to the register the
        # hardware reads, so regalloc is free to put the addend elsewhere.
        reads, writes = roles.get(logical, (None, None))
        if reads is not None:
            used = syntax_ordered_operands(placement, syntax)
            database_ties = sum(
                1 for operand in used
                if (alias := placement["operand_use"][operand["field"]])
                and alias in reads and alias in writes)
            if database_ties != len(tie_positions.get(logical, [])):
                ties[logical] = ties.get(logical, 0) + 1
                flagged.add(key)

        if len(outs) + len(ins) != wanted:
            arity[logical] = arity.get(logical, 0) + 1
            flagged.add(key)
        elif len(outs) != want_outs:
            defs[logical] = defs.get(logical, 0) + 1
            flagged.add(key)
        else:
            have = ["reg" if n.removesuffix("_wb") in registers else "imm"
                    for n in outs + ins]
            if have != logical_kinds(record):
                kinds[logical] = kinds.get(logical, 0) + 1
                flagged.add(key)

    if not arity and not defs and not kinds and not ties:
        return "operand agreement: every member matches its logical\n", 0

    lines = [f"operand agreement:"
             f" {len(set(arity) | set(defs) | set(kinds) | set(ties))} logicals,"
             f" {len(flagged)} member placements disagree with their logical",
             "",
             "  each one is a place the encoder reads the wrong operand,"
             " silently (plan 5.11)",
             ""]
    for title, counted in (("arity", arity), ("defs", defs),
                           ("kinds", kinds), ("ties", ties)):
        if not counted:
            continue
        lines.append(f"  {title}: {len(counted)} logicals,"
                     f" {sum(counted.values())} placements")
        for logical, count in sorted(counted.items()):
            lines.append(f"    {logical:32} {count} placements")
        lines.append("")
    # Blocking axes only. Plan 5.4: "do not regenerate while 5.11's first three
    # axes are non-zero" -- so arity, defs and kinds fail the process. `ties` is
    # a register-allocation defect rather than an encoding one and is held by
    # 5.2, so it reports without failing; that is a deliberate difference, not
    # an oversight, and it is why this returns a count of the first three
    # rather than of everything it printed.
    blocking = sum(arity.values()) + sum(defs.values()) + sum(kinds.values())
    return "\n".join(lines).rstrip("\n") + "\n", blocking


def emit_reloc_geometry(placements: list[dict]) -> str:
    """The bundle-bit position of every relocatable immediate, per placement.

    A relocation names a BYTE, but format E entry windows do not start on byte
    boundaries and the field's position inside an entry depends on the
    placement, so `byte * 8 + FieldLsb` is wrong twice over. See
    FORMAT-E-SWITCH-PLAN.md 5.8 -- getting it wrong silently mis-links, and in
    the one loud case it overwrites the bundle header.

    The key is (FieldSize, entry count, entry index, mapping value, type code).
    Every part is available where a relocation is applied: FieldSize comes from
    the relocation's own RelocFieldInfo, the entry count is header bit 3, the
    entry index follows from the relocation's offset within its bundle, and the
    mapping and type code are read out of the entry itself -- the type code's
    own position depends on (entry, mapping), which is what the second table
    below is for. That is what lets MC and lld share one answer -- neither of
    them knows the member.

    **The type code was not in the key and had to be.** An earlier revision
    keyed on the first four parts and said in this docstring that the fifth
    "would" be needed for the narrow fields it had to drop. It was needed for
    more than that: `SET_HWLOOP_F2` carries a 6-bit and a 12-bit offset in one
    entry, another instruction has a 6-bit immediate at the same
    (entry, mapping), and the four-part key answered with the wrong one. See
    FORMAT-E-SWITCH-PLAN.md 5.14 -- off1 was patched 13 bits away from its
    field, into off2's, and past the end into the reserved bit.

    Emitted rather than hand-written because this file already knows every
    field's position: it is what places them.
    """
    # Every immediate the layout names, not only the one spelled `imm`.
    # SET_HWLOOP_F2's two offsets are `imm1` and `imm2`, and skipping them is
    # how they came to be patched by a hand-written Bundle128 FieldLsb.
    imm_fields = {"imm", "imm1", "imm2", "imm3"}
    seen: dict[tuple, set] = {}
    type_pos: dict[tuple, set] = {}
    for p in placements:
        tc = p["type_code"]
        type_pos.setdefault(
            (p["entry_count"], p["entry_index"], p["mapping"]["value"]),
            set()).add((tc["lsb"], tc["msb"] - tc["lsb"] + 1))
        for operand in p["operands"]:
            if operand["field"] not in imm_fields:
                continue
            width = operand["msb"] - operand["lsb"] + 1
            key = (width, p["entry_count"], p["entry_index"],
                   p["mapping"]["value"], tc["value"])
            seen.setdefault(key, set()).add(operand["lsb"])

    for key, positions in sorted(type_pos.items()):
        if len(positions) != 1:
            raise SystemExit(
                f"reloc-geometry: type code sits at {sorted(positions)} for "
                f"entry {key} -- the consumer reads it from one place")

    # A key whose immediate still sits at more than one position cannot be
    # resolved from the image at all. Those are DROPPED rather than guessed, so
    # a relocation that ever needs one misses the table and the consumer
    # reports an error. Silently patching the wrong bits is the failure mode
    # this whole table exists to prevent -- and 5.14 is what that failure looks
    # like when it happens. With the type code in the key nothing is dropped
    # today; the branch stays because a re-delivered layout could reintroduce a
    # collision, and it must fail loudly rather than pick.
    rows = {k: next(iter(v)) for k, v in seen.items() if len(v) == 1}
    dropped = sorted(k for k, v in seen.items() if len(v) != 1)

    out = [
        "//===- HaydnRelocGeometry.inc - generated, do not edit -----*- C++ -*-===//",
        "//",
        "// Generated by utils/haydn_encoding.py --emit reloc-geometry.",
        "//",
        "// Bundle-bit LSB of a relocatable immediate, keyed by",
        "// (FieldSize, entry count, entry index, mapping value, type code).",
        "// See FORMAT-E-SWITCH-PLAN.md 5.8 and 5.14.",
        "//",
        "// The type code's own position depends on (entry, mapping), so the",
        "// second table locates it: read the mapping, look up where the type",
        "// code is, read that, then match a row.",
        "//===----------------------------------------------------------------===//",
        "",
        "// EntryCount, EntryIndex, Mapping, TypeLsb, TypeWidth",
        "HAYDN_RELOC_TYPE_POS_LIST(",
    ]
    tbody = []
    for (ecount, eindex, mapping), positions in sorted(type_pos.items()):
        lsb, width = next(iter(positions))
        tbody.append(f"  HAYDN_RELOC_TYPE_POS({ecount}, {eindex}, {mapping}, "
                     f"{lsb:2}, {width})")
    out.append("\n".join(tbody))
    out += [
        ")",
        "",
        "// FieldSize, EntryCount, EntryIndex, Mapping, TypeCode, BundleLsb",
        "HAYDN_RELOC_GEOM_ROW_LIST(",
    ]
    if dropped:
        out[-1:-1] = [
            "// Ambiguous, deliberately absent (see emit_reloc_geometry): "
            + ", ".join(f"w{w}/e{c}.{i}/m{m}/t{t}" for w, c, i, m, t in dropped),
        ]
    body = []
    for (width, ecount, eindex, mapping, tcode), lsb in sorted(rows.items()):
        body.append(f"  HAYDN_RELOC_GEOM_ROW({width:2}, {ecount}, {eindex}, "
                    f"{mapping}, {tcode:2}, {lsb:2})")
    out.append("\n".join(body))
    out.append(")")
    out.append("")
    return "\n".join(out)


# The three instructions lld's long-branch veneer is built from. Every one of
# them takes R0, which is register number 0, so every register field in the
# thunk is zero and the immediate is the only thing that varies at link time.
THUNK_INSTRUCTIONS = ("LUI", "ADDI32", "JALR")


def emit_thunk_encoding(geometry: dict, placements: list[dict]) -> str:
    """Bundle words for lld's long-branch veneer.

    `HaydnThunks.cpp` used to carry these as hand-written 64-bit constants
    with a 16-byte little-endian bundle writer -- a third parcel-size
    oracle after `HaydnBundlePlan.h` and `writeNopData`, and one invisible to
    a grep for either, because it lives in lld. It emitted Bundle128 forever
    after the switch: the veneer disassembled as <unknown> and any branch that
    needed one jumped into it.

    Generated instead, for the same reason § 5.8's relocation geometry is:
    this file already knows every field's position because it is what places
    them. The veneer is three BUNDLE_E2 words, each with the instruction at
    entry 0 on ALU0 and a NOP at entry 1 on ALU1 -- the one shape all three
    instructions share, since ADDI32 has no 3-entry placement at all.
    """
    def pick(name: str, entry_index: int, unit: str) -> dict:
        for p in placements:
            if (p["instruction"], p["entry_count"], p["entry_index"],
                    p["unit"]) == (name, 2, entry_index, unit):
                return p
        raise SystemExit(
            f"thunk encoding: {name} has no 2-entry placement at entry"
            f" {entry_index} on {unit}; the veneer shape must be re-chosen"
            f" rather than guessed")

    header = geometry["header"]
    nop = pick("NOP", 1, "ALU1")

    rows = []
    for name in THUNK_INSTRUCTIONS:
        insn = pick(name, 0, "ALU0")
        word = 0
        # Header: format indicator, entry count (0 => 2 entries), reserved.
        word |= header["format_indicator"]["value"] << header["format_indicator"]["lsb"]
        word |= header["reserved"]["value"] << header["reserved"]["lsb"]
        # Both entries. Every operand is left zero: the registers are R0 and
        # the immediate is what lld patches in.
        for p in (insn, nop):
            for part in ("mapping", "type_code"):
                word |= p[part]["value"] << p[part]["lsb"]
            word |= p["opcode"] << p["opcode_field"]["lsb"]

        imm = [o for o in insn["operands"]
               if insn["operand_use"].get(o["field"]) and
               str(insn["operand_use"][o["field"]]).startswith("imm")]
        if len(imm) != 1:
            raise SystemExit(f"thunk encoding: {name} has {len(imm)} immediate"
                             f" operands, expected exactly one")
        lsb, msb = imm[0]["lsb"], imm[0]["msb"]
        rows.append((name, word, lsb, msb - lsb + 1))

    bits = geometry["bundle_bits"]
    out = [
        "//===- HaydnThunkEncoding.inc - generated, do not edit -----*- C++ -*-===//",
        "//",
        "// Generated by utils/haydn_encoding.py --emit thunk-encoding.",
        "//",
        "// lld's long-branch veneer, as format E bundle words. Each row is one",
        f"// BUNDLE_E2 ({bits // 8} bytes): the instruction at entry 0 on ALU0, a NOP",
        "// at entry 1 on ALU1, every register field zero because the veneer uses",
        "// R0 throughout, and the immediate left clear for lld to patch.",
        "// See FORMAT-E-SWITCH-PLAN.md 5.8.",
        "//===----------------------------------------------------------------===//",
        "",
        f"// Name, Lo64, Hi32, ImmLsb (bundle bit), ImmWidth. Bundle is {bits} bits.",
        "HAYDN_THUNK_INSN_LIST(",
    ]
    for name, word, lsb, width in rows:
        out.append(f"  HAYDN_THUNK_INSN({name:6}, 0x{word & ((1 << 64) - 1):016x}ull,"
                   f" 0x{word >> 64:08x}u, {lsb:2}, {width:2})")
    out.append(")")
    out.append("")
    return "\n".join(out)


def emit_tablegen(geometry: dict, placements: list[dict],
                  pipeline: dict, syntax: dict[str, list[str]],
                  flags: dict[str, dict],
                  op_classes: dict, op_classes_raw: dict,
                  roles: dict[str, tuple[set[str], set[str]]],
                  tie_positions: dict[str, list[int]],
                  signedness: dict[str, dict[str, str]],
                  part: str = "members") -> str:
    """The encoding half. The scheduling half is its own file: it can be
    included while Bundle128 is live and this cannot, so emitting both here
    would define the itinerary classes twice once the switch happens.

    ``part`` splits the output the way Bundle128 was split. The bundle
    composites go in their own file because -gen-asm-matcher rejects a def
    whose AsmString opens with an operand, and a composite has no mnemonic of
    its own. Haydn.td includes both files; HaydnAsmMatcher.td includes only
    the members."""
    verify_decodable(placements)
    verify_operand_sets(placements, syntax)
    indices = placement_index(placements)
    placements = canonical_members(placements)
    positions: dict[tuple[int, int], tuple[int, int]] = {}
    for p in placements:
        positions[(p["entry_count"], p["entry_index"])] = \
            (p["entry_msb"], p["entry_lsb"])

    out: list[str] = [
        "//===- HaydnFormatEEncoding.td - generated, do not edit ----*- tablegen -*-===//",
        "//",
        "// Generated by utils/haydn_encoding.py from format_e_bit_layout_v2_2.json.",
        "//",
        "// One record per (instruction, entry position, unit). Bit positions are",
        "// relative to the entry window; the bundle composite places each window at",
        "// its absolute offset inside the 96-bit bundle:",
        "//",
    ]
    for (count, index) in sorted(positions):
        msb, lsb = positions[(count, index)]
        out.append(f"//   {count}-entry entry{index}   bundle bit[{msb}:{lsb}]"
                   f"   {msb - lsb + 1} bits")
    out += [
        "//",
        f"// Bundle is {geometry['bundle_bits']} bits: header bit"
        f"[{geometry['payload_lsb'] - 1}:0], payload bit"
        f"[{geometry['bundle_bits'] - 1}:{geometry['payload_lsb']}].",
        "//",
        "// Itinerary classes come from HaydnFormatESchedule.td, which is already",
        "// included; this file carries the encoding only.",
        "//",
        "// NOT included in the build. The MC layer still encodes Bundle128, whose",
        "// slot windows are wider than any entry window here, so these records",
        "// cannot coexist with it. They are emitted so the encoding can be reviewed",
        "// and diffed before the MC layer moves.",
        "//===----------------------------------------------------------------------===//",
        "",
    ]

    out += [
        "// Placement index: a dense enumeration of the (entry position, unit)",
        "// pairs the encoding admits. This is the member index in an",
        "// instruction's alternatives, so 1<<PlacementIndex is the mask of",
        "// positions the instruction is legal in -- the same meaning the slot",
        "// index carried for Bundle128, generalized from 3 slots to these pairs.",
        "//",
    ]
    for pair, index in sorted(indices.items(), key=lambda kv: kv[1]):
        count, entry, unit = pair
        out.append(f"//   {index:>2}  {count}-entry entry{entry}  {unit}")
    out += [f"//", f"// {len(indices)} pairs; a 1<<index mask needs"
            f" {max(32, 1 << (len(indices) - 1).bit_length())} bits.", ""]

    out += ["// Entry windows are operand types, one per position: an entry is only",
            "// substitutable for another at the same position, because the positions",
            "// differ in width and in which units they can name.",
            "let Namespace = \"Haydn\" in {"]
    for (count, index) in sorted(positions):
        msb, lsb = positions[(count, index)]
        name = f"p{count}{index}_entry"
        out.append(f"  def {name} : InstSlot<\"P{count}{index}\","
                   f" {msb - lsb + 1}> {{ let FieldToFind = \"e{index}\"; }}")
    out += ["}", ""]

    for (count, index) in sorted(positions):
        msb, lsb = positions[(count, index)]
        width = msb - lsb + 1
        # Size is a byte count, so it cannot express a 45- or 27-bit entry.
        # Round it up and zero-pad Inst to match: InstructionEncoding rejects
        # an Inst narrower than Size * 8. The padding sits above the window,
        # so every field keeps the bit position the layout gives it, and the
        # composite still takes only the low `width` bits through the slot.
        size_bytes = (width + 7) // 8
        pad = size_bytes * 8 - width
        inst = (("{" + ", ".join(["0"] * pad) + f", e{index}}}")
                if pad else f"e{index}")
        out += [
            f"// {count}-entry bundle, entry{index}: bundle bit[{msb}:{lsb}].",
            f"class HaydnEntryP{count}{index}<dag outs, dag ins, string asm>",
            f"    : HaydnFormatInst<outs, ins, asm, []> {{",
            f"  let Slot = p{count}{index}_entry;",
            f"  let DecoderNamespace = \"P{count}{index}\";",
            f"  let Size = {size_bytes};",
            "  int PlacementIndex = -1;  // set per unit by each member below",
            f"  bits<{width}> e{index};   // the window p{count}{index}_entry finds",
            f"  bits<{size_bytes * 8}> Inst = {inst};",
            "}",
            "",
        ]

    # The bundle composites. Each names its entries low position first so the
    # concatenation below reads MSB-first, matching how Inst is written.
    header = geometry["header"]
    bundle_bits = geometry["bundle_bits"]
    comp: list[str] = [
        "//===- HaydnFormatEComposites.td - generated, do not edit --*- tablegen -*-===//",
        "//",
        "// Generated by utils/haydn_encoding.py from format_e_bit_layout_v2_2.json.",
        "//",
        "// The bundle composites, kept apart from the members they hold because",
        "// -gen-asm-matcher rejects a def whose AsmString opens with an operand and",
        "// a composite has no mnemonic. Include after HaydnFormatEEncoding.td, which",
        "// defines the entry slots these name.",
        "//===----------------------------------------------------------------------===//",
        "",
    ]
    for count in sorted({c for (c, _) in positions}):
        entries = sorted(i for (c, i) in positions if c == count)
        spans_used = [positions[(count, i)] for i in entries]
        top = max(msb for msb, _ in spans_used)
        operands = ", ".join(f"p{count}{i}_entry:$e{i}" for i in entries)
        # Bundle text names the high entry first and is right-aligned on
        # entry0, separated by `; ` so the separator cannot be confused with
        # the commas between an instruction's own operands. This is the
        # Bundle128 spelling (`$s2; $s1; $s0`) and what the assembler,
        # llvm-objdump and BundleSim's dump parser already agree on. The braces
        # come from the printer, as they did for Bundle128.
        asm = "; ".join(f"$e{i}" for i in reversed(entries))
        comp += [
            f"// {count}-entry format E bundle. Header selects the format and the",
            f"// entry count; entries fill the payload from bit"
            f"[{geometry['payload_lsb']}] up.",
            f"def BUNDLE_E{count} : HaydnFormatInst<(outs), (ins {operands}),",
            f"    \"{asm}\", []> {{",
            "  let isComposite = true;",
            f"  let Size = {bundle_bits // 8};",
            f"  let DecoderNamespace = \"FormatE{count}\";",
        ]
        for i in entries:
            msb, lsb = positions[(count, i)]
            comp.append(f"  bits<{msb - lsb + 1}> e{i};")
        comp.append(f"  bits<{bundle_bits}> Inst;")
        rows: list[tuple[int, int, str, str]] = []
        if top + 1 < bundle_bits:
            rows.append((bundle_bits - 1, top + 1,
                         td_bits(0, bundle_bits - 1 - top, "bundle/unused"),
                         "unused"))
        for i in entries:
            msb, lsb = positions[(count, i)]
            rows.append((msb, lsb, f"e{i}", f"entry{i}"))
        entry_num = header["entry_num"]
        rows.append((entry_num["msb"], entry_num["lsb"],
                     td_bits(1 if count == 3 else 0,
                             entry_num["msb"] - entry_num["lsb"] + 1,
                             "header/entry_num"),
                     f"{count} entries"))
        for name in ("reserved", "format_indicator"):
            field = header[name]
            rows.append((field["msb"], field["lsb"],
                         td_bits(field["value"],
                                 field["msb"] - field["lsb"] + 1, name),
                         name))
        for msb, lsb, value, label in sorted(rows, key=lambda r: -r[0]):
            span = f"{msb}-{lsb}" if msb != lsb else f"{msb}"
            comp.append(f"  let Inst{{{span}}} = {value};  // {label}")
        comp += ["}", ""]

    for p in sorted(placements, key=lambda x: (x["instruction"], x["entry_count"],
                                               x["entry_index"], x["unit"])):
        name = (f"{td_identifier(p['instruction'])}"
                f"_P{p['entry_count']}{p['entry_index']}_{p['unit']}")
        base = p["entry_lsb"]
        context = f"{p['instruction']}@{p['entry_count']}e{p['entry_index']}/{p['unit']}"

        used = syntax_ordered_operands(p, syntax)
        spelling = {o["field"]: p["operand_use"][o["field"]] for o in used}

        unused = [o for o in p["operands"]
                  if not p["operand_use"].get(o["field"])]
        bit_lines, decls = [], []
        classes: dict[str, str] = {}
        for operand in used + unused:
            alias = p["operand_use"].get(operand["field"])
            width = operand["msb"] - operand["lsb"] + 1
            if alias is None:
                # Unused by this instruction: the field reads as zero.
                bit_lines.append(
                    (operand["msb"] - base, operand["lsb"] - base, "0", operand["field"]))
                continue
            kind = (branch_operand_class(op_classes_raw, p["instruction"],
                                         width)
                    or inherited_operand_class(op_classes, p["instruction"],
                                               alias, width))
            if kind is None:
                signed = None
                if (not alias.endswith("_sel")
                        and alias.startswith(("imm", "uimm"))):
                    signed = immediate_is_signed(signedness, p["instruction"],
                                                 alias, context)
                kind = operand_type(alias, width, context, signed)
            decls.append(f"  bits<{width}> {alias};")
            classes[alias] = kind
            bit_lines.append(
                (operand["msb"] - base, operand["lsb"] - base, alias, operand["field"]))

        # Which of these are defs, and which is a tied writeback, is the
        # database's statement and not this entry's field naming -- see
        # member_operand_shape. The bits<> above stay bound to the alias, which
        # after a tie is the name of the tied IN, so the field still resolves.
        out_names, in_names, constraints = member_operand_shape(
            p, roles, syntax, tie_positions)

        def declare(operand_name: str) -> str:
            base_name = (operand_name[:-len("_wb")]
                         if operand_name.endswith("_wb") else operand_name)
            return f"{classes[base_name]}:${operand_name}"

        outs = [declare(n) for n in out_names]
        ins = [declare(n) for n in in_names]

        asm_operands = ", ".join(f"${spelling[o['field']]}" for o in used)
        asm = p["instruction"].lower() + (f"\t{asm_operands}" if asm_operands else "")

        out.append(f"def {name} : HaydnEntryP{p['entry_count']}{p['entry_index']}<")
        out.append(f"    (outs {', '.join(outs)}), (ins {', '.join(ins)}),")

        out.append(f"    \"{asm}\"> {{")
        if constraints:
            out.append(f"  let Constraints = \"{', '.join(constraints)}\";")
        index = indices[(p["entry_count"], p["entry_index"], p["unit"])]
        out.append(f"  let PlacementIndex = {index};  // {p['unit']}"
                   f" @ {p['entry_count']}-entry entry{p['entry_index']}")
        info = pipeline.get(p["instruction"])
        if info is not None:
            out.append("  let Itinerary = "
                       f"{itinerary_name((p['unit'],), info[1])};")
        # Properties of the logical this member expands. The member's Desc is
        # the one the generic CodeGen layer reads, so they have to be here.
        for flag, value in (flags.get(p["instruction"]) or {}).items():
            if flag == "Defs":
                out.append(f"  let Defs = [{', '.join(value)}];")
            else:
                out.append(f"  let {flag} = {value};")
        out += decls
        def constant(msb: int, lsb: int, value: int, label: str):
            width = msb - lsb + 1
            return (msb - base, lsb - base,
                    td_bits(value, width, f"{context}/{label}"), label)

        rows = [
            constant(p["mapping"]["msb"], p["mapping"]["lsb"],
                     p["mapping"]["value"], f"mapping={p['unit']}"),
            constant(p["type_code"]["msb"], p["type_code"]["lsb"],
                     p["type_code"]["value"], f"type={p['type']}"),
            constant(p["opcode_field"]["msb"], p["opcode_field"]["lsb"],
                     p["opcode"], "opcode"),
        ]
        rows += [constant(r["msb"], r["lsb"], 0, "reserved")
                 for r in p["reserved"]]
        for msb, lsb, value, field in bit_lines:
            rows.append((msb, lsb,
                         value if value != "0"
                         else td_bits(0, msb - lsb + 1, f"{context}/{field}"),
                         field))
        window = f"e{p['entry_index']}"
        for msb, lsb, value, field in sorted(rows, key=lambda t: -t[0]):
            span = f"{msb}-{lsb}" if msb != lsb else f"{msb}"
            out.append(f"  let {window}{{{span}}} = {value};  // {field}")
        out.append("}")
        out.append("")

    return "\n".join(comp if part == "composites" else out)


def current_td_instructions(target_dir: Path) -> set[str]:
    """Instruction names the target currently defines in TableGen."""
    definition = re.compile(r"^def\s+([A-Za-z_][A-Za-z0-9_]*)\s*:")
    names: set[str] = set()
    for path in sorted(target_dir.glob("*.td")):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = definition.match(line)
            if match is not None:
                names.add(match.group(1))
    return names


def report(geometry: dict, placements: list[dict], target_dir: Path | None) -> str:
    lines: list[str] = []
    instructions = sorted({p["instruction"] for p in placements})
    by_shape: dict[tuple, int] = {}
    for p in placements:
        key = (p["entry_count"], p["entry_index"], p["unit"], p["type"])
        by_shape[key] = by_shape.get(key, 0) + 1

    lines.append("Haydn format E encoding — normalized from the ISA database")
    lines.append("=" * 66)
    lines.append(f"bundle                : {geometry['bundle_bits']} bits"
                 f" (payload bit[{geometry['bundle_bits'] - 1}:"
                 f"{geometry['payload_lsb']}],"
                 f" budget {geometry['payload_budget_bits']}b)")
    lines.append(f"(entry, unit, type)   : {len(by_shape)}")
    lines.append(f"placements            : {len(placements)}")
    lines.append(f"distinct instructions : {len(instructions)}")
    lines.append("")

    widths: dict[tuple[int, int], int] = {}
    for p in placements:
        widths[(p["entry_count"], p["entry_index"])] = \
            p["entry_msb"] - p["entry_lsb"] + 1
    lines.append("entry windows (bits):")
    for (count, index) in sorted(widths):
        lines.append(f"  {count}-entry  entry{index}  {widths[(count, index)]:>3}")
    lines.append("")

    spread: dict[str, int] = {}
    for name in instructions:
        spread[name] = sum(1 for p in placements if p["instruction"] == name)
    buckets: dict[int, int] = {}
    for count in spread.values():
        buckets[count] = buckets.get(count, 0) + 1
    lines.append("placements per instruction:")
    for count in sorted(buckets):
        lines.append(f"  {count:>2} placement(s) : {buckets[count]:>4} instruction(s)")
    lines.append("")

    if target_dir is not None:
        defined = current_td_instructions(target_dir)
        real = {name.upper() for name in instructions}
        lines.append("against the current TableGen definitions")
        lines.append("-" * 66)
        lines.append(f"  `def` lines in *.td            : {len(defined)}")
        lines.append(f"  in the database, no `def` line : "
                     f"{len(real - {d.upper() for d in defined})}")
        lines.append("")
        lines.append("  Read these as orders of magnitude, not a diff:")
        lines.append("")
        lines.append("  - Generated members live in HaydnFormatsE96Members.td.inc.")
        lines.append("    Occupancy-suffix names are not product identity.")
        lines.append("  - Instructions produced by a multiclass have no literal")
        lines.append("    `def NAME :` line, so the database-side shortfall is an")
        lines.append("    upper bound, not a list of gaps.")
        lines.append("  - Authored logicals live in HaydnInstrInfo.td.")
        lines.append("    HaydnInstrInfoManual.td is a 0-def tombstone.")
        lines.append("    Placement bits come only from the pinned layout JSON.")
    return "\n".join(lines) + "\n"


# A written register is read too when the Behavior names it on the right of
# its own assignment. The lookahead keeps `rtd` from matching `rtd1`; the
# operands carry Q-format suffixes (`rtdQ1.63`), so a plain \b would miss it.
def _behavior_reads(behavior: str, name: str) -> bool:
    pattern = re.compile(rf"\b{re.escape(name)}(?![0-9_])")
    for statement in behavior.split(";"):
        left, assign, right = statement.partition("=")
        if assign and pattern.search(left) and pattern.search(right):
            return True
    return False


def read_port_defects(database: Path) -> list[tuple[str, str, str]]:
    """Instructions whose Behavior reads a register their Read_Port omits.

    A third statement the database makes about each instruction, after the
    Syntax and the ports: the Behavior text. `FMULA32S_HH` reads

        rtd = SATQ1.63(rtdQ1.63 + SATQ1.63(rsd1[63:32]Q1.31 * ...))

    and its Description says "written back to rtd", while its `DR_Read_Port`
    lists only `rsd1, rsd2`. The ports are what the generator believes, so the
    accumulator is not modelled as an input and nothing ties it to the register
    the hardware actually reads.

    Same class as § 5.3's 76 mapping rows: a disagreement *between* two
    statements the database makes about itself, invisible to every gate that
    checks the database against the encoder rather than against itself.
    Returns (instruction, port key, operand).
    """
    data = json.loads((database / INSTRUCTION_INDEX).read_text(encoding="utf-8"))
    files = (("GPR", GPR_ALIASES), ("DR", DR_ALIASES))
    found: list[tuple[str, str, str]] = []
    for entries in data.values():
        for entry in entries:
            name = str(entry.get("Instruction", "")).strip()
            behavior = str(entry.get("Behavior") or "")
            if not name or not behavior:
                continue
            for prefix, aliases in files:
                written = [str(o).strip()
                           for o in (entry.get(f"{prefix}_Write_Port") or [])]
                read = {str(o).strip()
                        for o in (entry.get(f"{prefix}_Read_Port") or [])}
                for operand in written:
                    if (operand in aliases and operand not in read
                            and _behavior_reads(behavior, operand)):
                        found.append((name, f"{prefix}_Read_Port", operand))
    return found


def verify_read_ports(database: Path) -> None:
    defects = read_port_defects(database)
    if not defects:
        return
    lines = [f"{len(defects)} instruction(s) read a register their Read_Port"
             " does not name:", ""]
    for name, key, operand in defects:
        lines.append(f"  {name:20} {key} is missing {operand}")
    lines += ["",
              "The Behavior is explicit and the repair is forced. Run:",
              "  haydn_encoding.py --database <dir> --fix-read-ports --write"]
    raise SystemExit("\n".join(lines))


def fix_read_ports(database: Path, write: bool) -> str:
    """Add the operands the Behavior reads to the Read_Port that omits them.

    Forced, not chosen: the Behavior assigns the register from an expression
    containing itself, so it is read, and the register file follows from the
    alias. Nothing else about the row changes.

    Each port is one line of the file, so the edit is made at byte level and
    every other byte -- including the CRLF endings -- is left alone. Re-running
    is a no-op.
    """
    path = database / INSTRUCTION_INDEX
    raw = path.read_bytes()
    defects = read_port_defects(database)
    if not defects:
        return "read ports: every register the Behavior reads is named\n"

    lines = raw.split(b"\n")
    # Walk the file once, tracking which instruction each line belongs to, so
    # a port line is only rewritten for the row that actually names it.
    wanted: dict[tuple[str, str], list[str]] = {}
    for name, key, operand in defects:
        wanted.setdefault((name, key), []).append(operand)

    current = None
    edited = 0
    for index, line in enumerate(lines):
        if b'"Instruction":' in line:
            current = line.split(b'"Instruction": "')[1].split(b'"')[0].decode()
            continue
        if current is None:
            continue
        for (name, key), operands in wanted.items():
            if name != current or f'"{key}":'.encode() not in line:
                continue
            body = line.decode("utf-8")
            # The file is CRLF. Split the terminator off and put it back, or
            # the repaired rows silently become the only LF lines in it.
            eol = "\r" if body.endswith("\r") else ""
            content = body[:-len(eol)] if eol else body
            head, _, tail = content.partition(":")
            existing = json.loads(tail.strip().rstrip(",") or "null") or []
            merged = existing + [o for o in operands if o not in existing]
            rendered = ", ".join(f'"{o}"' for o in merged)
            suffix = "," if content.rstrip().endswith(",") else ""
            lines[index] = f"{head}: [{rendered}]{suffix}{eol}".encode("utf-8")
            edited += 1
    if edited != len(wanted):
        raise SystemExit(f"{len(wanted)} port line(s) to repair but {edited}"
                         " were placed; cannot write")

    report = (f"{len(defects)} read port(s) repaired over {len(wanted)} row(s)"
              f"{'' if write else ' — dry run, pass --write to apply'}\n")
    for name, key, operand in defects:
        report += f"  {name:20} {key} += {operand}\n"
    if write:
        path.write_bytes(b"\n".join(lines))
        report += ("\nRe-pin the database: GOLDEN_INPUTS.sha256 still names "
                   "the uncorrected file.\n")
    return report


def fix_operand_mapping(database: Path, write: bool) -> str:
    """Repair mapping rows that name fewer operands than the Syntax does.

    `verify_operand_sets` reports this class of defect but cannot fix it, and
    the bit layout is **not** version-controlled by either repo -- it is a
    delivered artefact living beside them. So a correction made on one host does
    not travel, and a fresh checkout pairs a corrected `.td` with an
    uncorrected database. See the `--check` failure that names the affected
    instructions.

    The repair is forced rather than chosen. Every field declares the operand
    aliases it may hold, so assigning each operand the Syntax names to a
    distinct admissible field is a bipartite matching, and where that matching
    is unique there is exactly one legal row. Rows whose matching is not unique
    are reported and nothing is written -- guessing here would pin an operand
    to the wrong register field, which is precisely the failure being repaired.

    Note the alias lists differ **per (entry, unit, type)**: the same
    instruction can need a different assignment at different placements, so
    this cannot be done as a global search and replace.
    """
    path = database / FORMAT_E_LAYOUT
    raw = path.read_bytes()
    data = json.loads(raw.decode("utf-8"))
    syntax = load_syntax_order(database)

    # One record per mapping row, in the order the rows appear in the file.
    records: list[dict] = []
    for group_key in ("entry_num_0", "entry_num_1"):
        for entry_key, entry in data[group_key].items():
            if not entry_key.startswith("entry"):
                continue
            for unit, body in entry.items():
                if not isinstance(body, dict) or "mapping_value" not in body:
                    continue
                for type_name, type_body in (body.get("types") or {}).items():
                    fields = []
                    for text in (type_body.get("operand_fields") or []):
                        match = OPERAND_FIELD.match(text)
                        if match is None:
                            raise SystemExit(f"cannot name field {text!r}")
                        fields.append((match.group(1),
                                       [a.strip() for a in
                                        match.group(2).split(",") if a.strip()]))
                    for row in (type_body.get("mapping") or []):
                        records.append({
                            "context": f"{group_key}/{entry_key}/{unit}/{type_name}",
                            "name": str(row.get("instruction", "")).strip(),
                            "fields": fields,
                            "row": row,
                        })

    # Each row occupies one line, so the edit can be made at byte level and
    # leave every other byte -- including the CRLF endings -- untouched.
    lines = raw.split(b"\n")
    numbered = [i for i, line in enumerate(lines) if b'"instruction"' in line]
    if len(numbered) != len(records):
        raise SystemExit(f"{len(records)} mapping rows but {len(numbered)} lines"
                         " naming an instruction; cannot place the edits")
    for record, index in zip(records, numbered):
        spelled = lines[index].split(b'"instruction": "')[1].split(b'"')[0]
        if spelled.strip().decode() != record["name"]:
            raise SystemExit(f"line {index + 1} is {spelled!r}, expected"
                             f" {record['name']!r}; row order does not match")
        record["line"] = index

    repairs: list[tuple[dict, dict[str, str]]] = []
    ambiguous: list[str] = []
    for record in records:
        written = syntax.get(record["name"])
        if written is None:
            continue
        present = sorted(v for v in (str(record["row"].get(f, "")).strip()
                                     for f, _ in record["fields"]) if v)
        if sorted(written) == present:
            continue

        solutions = []
        for combo in permutations(range(len(record["fields"])), len(written)):
            if all(written[k] in record["fields"][combo[k]][1]
                   for k in range(len(written))):
                solutions.append(combo)
                if len(solutions) > 1:
                    break
        if len(solutions) != 1:
            ambiguous.append(
                f"  {record['context']}/{record['name']}:"
                f" {len(solutions)} matchings for {','.join(written)} over "
                + " ".join(f"{f}({'|'.join(a)})" for f, a in record["fields"]))
            continue
        repairs.append((record, {record["fields"][field][0]: written[k]
                                 for k, field in enumerate(solutions[0])}))

    if ambiguous:
        raise SystemExit("\n".join(
            [f"{len(ambiguous)} row(s) have no unique operand assignment:"]
            + ambiguous
            + ["", "Refusing to write: the layout has to be corrected by hand."]))
    if not repairs:
        return "every mapping row already names the operands its Syntax does"

    edits = 0
    for record, assignment in repairs:
        line = lines[record["line"]]
        for field, _ in record["fields"]:
            old = str(record["row"].get(field, ""))
            new = assignment.get(field, "")
            if old == new:
                continue
            was = f'"{field}": "{old}"'.encode()
            now = f'"{field}": "{new}"'.encode()
            if line.count(was) != 1:
                raise SystemExit(f"{record['context']}/{record['name']}:"
                                 f" {was!r} is not unique on its line")
            line = line.replace(was, now)
            edits += 1
        lines[record["line"]] = line

    summary = (f"{len(repairs)} mapping row(s) repaired, {edits} field(s)"
               f" rewritten, each assignment forced by the alias declarations")
    if not write:
        return summary + "\n(dry run: pass --write to apply)"
    path.write_bytes(b"\n".join(lines))
    return summary + f"\nwrote {path}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, required=True,
                        help="directory holding the read-only ISA database JSON")
    parser.add_argument("--emit",
                        choices=("table", "report", "td", "composites",
                                 "schedule", "roundtrip", "reloc-geometry",
                                 "thunk-encoding", "operand-agreement"))
    parser.add_argument("--output", "-o", type=Path)
    parser.add_argument("--target-dir", type=Path,
                        help="Haydn target directory, for the .td comparison")
    parser.add_argument("--flags-from", type=Path,
                        help="llvm-tblgen --dump-json output, for the"
                             " instruction properties members inherit from"
                             " their logical (required by --emit td)")
    parser.add_argument("--check", action="store_true",
                        help="validate the database and emit nothing")
    parser.add_argument("--fix-operand-mapping", action="store_true",
                        help="repair mapping rows the Syntax cross-check"
                             " rejects, where the assignment is forced")
    parser.add_argument("--fix-read-ports", action="store_true",
                        help="add the operands the Behavior reads to the"
                             " Read_Port that omits them")
    parser.add_argument("--write", action="store_true",
                        help="with --fix-operand-mapping or --fix-read-ports,"
                             " edit the database in place instead of reporting")
    args = parser.parse_args()

    if args.fix_operand_mapping:
        print(fix_operand_mapping(args.database, args.write))
        return 0
    if args.fix_read_ports:
        print(fix_read_ports(args.database, args.write))
        return 0

    _format_e = Path(__file__).resolve().parent.parent / "FormatE"
    if str(_format_e) not in sys.path:
        sys.path.insert(0, str(_format_e))
    from family_core import verify_authority_inputs

    verify_authority_inputs(
        args.database,
        [FORMAT_E_LAYOUT, INSTRUCTION_INDEX],
    )

    # 0 unless a CHECK mode says otherwise. Generation modes either produce
    # their file or raise; only roundtrip and operand-agreement have a verdict.
    status = 0
    geometry, placements = load_placements(args.database)
    verify_decodable(placements)
    verify_operand_sets(placements, load_syntax_order(args.database))
    verify_read_ports(args.database)
    if args.check or args.emit is None:
        print(f"format E: {len(placements)} placements over "
              f"{len({(p['entry_count'], p['entry_index'], p['unit'], p['type']) for p in placements})}"
              f" (entry, unit, type) shapes — layout is self-consistent")
        return 0

    if args.emit == "roundtrip":
        text, status = roundtrip(geometry, placements)
    elif args.emit == "schedule":
        text = emit_schedule_file(load_pipeline(args.database),
                                  canonical_members(placements))
    elif args.emit in ("td", "composites"):
        # Composites carry no instruction properties; members must (§ 6.9).
        if args.emit == "td" and args.flags_from is None:
            raise SystemExit(
                "--emit td needs --flags-from: every member has to carry the\n"
                "properties of the logical it expands, and emitting them as\n"
                "plain defs is silently wrong rather than merely incomplete —\n"
                "a branch member without isTerminator makes AsmPrinter drop\n"
                "the target label and the compiler's own output stops\n"
                "assembling. Produce the input with:\n"
                "  llvm-tblgen --dump-json -I llvm/lib/Target/Haydn"
                " -I llvm/include \\\n"
                "      llvm/lib/Target/Haydn/Haydn.td -o haydn-records.json")
        text = emit_tablegen(geometry, placements,
                             load_pipeline(args.database),
                             load_syntax_order(args.database),
                             load_instruction_flags(args.flags_from)
                             if args.flags_from else {},
                             *(load_operand_classes(args.flags_from)
                               if args.flags_from else ({}, {})),
                             load_operand_roles(args.database),
                             load_tie_positions(args.flags_from)
                             if args.flags_from else {},
                             load_immediate_signedness(args.database),
                             part="composites" if args.emit == "composites"
                             else "members")
    elif args.emit == "operand-agreement":
        if args.flags_from is None:
            raise SystemExit("--emit operand-agreement needs --flags-from:"
                             " the logicals' operand lists come from tblgen")
        text, status = check_operand_agreement(
            placements, args.flags_from,
            load_operand_roles(args.database),
            load_syntax_order(args.database),
            load_tie_positions(args.flags_from))
    elif args.emit == "reloc-geometry":
        text = emit_reloc_geometry(placements)
    elif args.emit == "thunk-encoding":
        text = emit_thunk_encoding(geometry, placements)
    elif args.emit == "table":
        text = json.dumps({"geometry": geometry, "placements": placements},
                          indent=1, sort_keys=True) + "\n"
    else:
        text = report(geometry, placements, args.target_dir)

    if args.output is None:
        print(text, end="")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    # Clamp: `status` is a COUNT, and an exit status is a byte — 256 findings
    # would exit 0 and read as a pass.
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())
