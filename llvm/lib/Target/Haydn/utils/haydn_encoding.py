#!/usr/bin/env python3
"""Normalize the Haydn format E instruction encoding.

`format_e_bit_layout_v2.json` is the ISA database's bit layout for the 96-bit
format E bundle. It describes, for every entry position of every bundle form,
every hardware unit that entry can name, and every instruction type that unit
can issue, the exact bit placement of each field and the constant values that
select it. This script turns that into one flat, downstream-agnostic table.

Why this exists: the encodings currently in `HaydnInstrInfoAuto.td` are
hypothesized -- the file says so in its own header -- and were never derived
from the database. Format E supersedes them wholesale rather than adjusting
them, because the entry windows (31/31/27 bits for three entries, 45/41 for
two) are far narrower than the retired Bundle128 slot windows (48/40/40), so no
existing per-slot encoding fits.

Step 1 of the migration: emit and validate the table, and report the distance
from the current .td. Nothing consumes the output yet.

  haydn_encoding.py --database DIR --emit table  -o encoding.json
  haydn_encoding.py --database DIR --emit report [--target-dir DIR]
  haydn_encoding.py --database DIR --check
"""

from __future__ import annotations

import argparse
import json
import re
from itertools import permutations
from pathlib import Path

FORMAT_E_LAYOUT = "format_e_bit_layout_v2.json"
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
                  "isIndirectBranch", "isReturn", "isNotDuplicable")


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


def operand_type(alias: str, width: int, context: str) -> str:
    if alias in GPR_ALIASES:
        expect, kind = 4, "GPR32"
    elif alias in DR_ALIASES:
        expect, kind = 4, "DR64"
    elif alias.endswith("_sel"):
        return f"uimm{width}"
    elif alias.startswith("uimm"):
        return f"uimm{width}"
    elif alias.startswith("imm"):
        return f"simm{width}"
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
    return "\n".join(lines) + "\n"


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

    `format_e_bit_layout_v2.json` lists an entry's fields in bit order, which
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
    lines.append("format_e_bit_layout_v2.json, or correct the Syntax if the")
    lines.append("operand really is not encoded.")
    raise SystemExit("\n".join(lines))


def emit_tablegen(geometry: dict, placements: list[dict],
                  pipeline: dict, syntax: dict[str, list[str]],
                  flags: dict[str, dict], part: str = "members") -> str:
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
        "// Generated by utils/haydn_encoding.py from format_e_bit_layout_v2.json.",
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
        "// Generated by utils/haydn_encoding.py from format_e_bit_layout_v2.json.",
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

        # `operand_fields` lists the entry's fields in bit order, which is not
        # the order the instruction is written in. Bits stay keyed by field, so
        # only the operand list and the asm string follow the Syntax; encoding
        # is unaffected. syntax_order cross-checks that the two database files
        # name the same operand set for this instruction.
        used = [o for o in p["operands"] if p["operand_use"].get(o["field"])]
        spelling = {o["field"]: p["operand_use"][o["field"]] for o in used}
        written = syntax.get(p["instruction"])
        if written is not None:
            rank = {name: i for i, name in enumerate(written)}
            used.sort(key=lambda o: rank[spelling[o["field"]]])

        unused = [o for o in p["operands"]
                  if not p["operand_use"].get(o["field"])]
        outs, ins, bit_lines, decls = [], [], [], []
        for operand in used + unused:
            alias = p["operand_use"].get(operand["field"])
            width = operand["msb"] - operand["lsb"] + 1
            if alias is None:
                # Unused by this instruction: the field reads as zero.
                bit_lines.append(
                    (operand["msb"] - base, operand["lsb"] - base, "0", operand["field"]))
                continue
            kind = operand_type(alias, width, context)
            decls.append(f"  bits<{width}> {alias};")
            target = outs if operand["field"].startswith("dest") else ins
            target.append(f"{kind}:${alias}")
            bit_lines.append(
                (operand["msb"] - base, operand["lsb"] - base, alias, operand["field"]))

        asm_operands = ", ".join(f"${spelling[o['field']]}" for o in used)
        asm = p["instruction"].lower() + (f"\t{asm_operands}" if asm_operands else "")

        out.append(f"def {name} : HaydnEntryP{p['entry_count']}{p['entry_index']}<")
        out.append(f"    (outs {', '.join(outs)}), (ins {', '.join(ins)}),")

        out.append(f"    \"{asm}\"> {{")
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
        slot_member = re.compile(r"_S[012]$")
        members = {d for d in defined if slot_member.search(d)}
        rest = defined - members
        lines.append("against the current TableGen definitions")
        lines.append("-" * 66)
        lines.append(f"  `def` lines in *.td            : {len(defined)}")
        lines.append(f"    of which _S0/_S1/_S2 members : {len(members)}")
        lines.append(f"    remaining                    : {len(rest)}")
        lines.append(f"  in the database, no `def` line : "
                     f"{len(real - {d.upper() for d in defined})}")
        lines.append("")
        lines.append("  Read these as orders of magnitude, not a diff:")
        lines.append("")
        lines.append("  - The _S0/_S1/_S2 members are the slot model's")
        lines.append("    scaffolding. Format E replaces them with (entry, unit)")
        lines.append("    placements, so they are retired rather than missing.")
        lines.append("  - Instructions produced by a multiclass have no literal")
        lines.append("    `def NAME :` line, so the database-side shortfall is an")
        lines.append("    upper bound, not a list of gaps.")
        lines.append("  - Above all, HaydnInstrInfoAuto.td says in its own header")
        lines.append("    that its binary encodings are hypothesized. A matching")
        lines.append("    name says nothing about the bits agreeing, so every one")
        lines.append("    of the placements above has to be re-derived from the")
        lines.append("    database regardless of what the name comparison shows.")
    return "\n".join(lines) + "\n"


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, required=True,
                        help="directory holding the read-only ISA database JSON")
    parser.add_argument("--emit",
                        choices=("table", "report", "td", "composites",
                                 "schedule", "roundtrip"))
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
    parser.add_argument("--write", action="store_true",
                        help="with --fix-operand-mapping, edit the database"
                             " in place instead of reporting")
    args = parser.parse_args()

    if args.fix_operand_mapping:
        print(fix_operand_mapping(args.database, args.write))
        return

    geometry, placements = load_placements(args.database)
    verify_decodable(placements)
    verify_operand_sets(placements, load_syntax_order(args.database))
    if args.check or args.emit is None:
        print(f"format E: {len(placements)} placements over "
              f"{len({(p['entry_count'], p['entry_index'], p['unit'], p['type']) for p in placements})}"
              f" (entry, unit, type) shapes — layout is self-consistent")
        return

    if args.emit == "roundtrip":
        text = roundtrip(geometry, placements)
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
                             part="composites" if args.emit == "composites"
                             else "members")
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


if __name__ == "__main__":
    main()
