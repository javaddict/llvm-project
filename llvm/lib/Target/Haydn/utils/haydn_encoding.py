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
from pathlib import Path

FORMAT_E_LAYOUT = "format_e_bit_layout_v2.json"

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


def load_placements(database: Path) -> tuple[dict, list[dict]]:
    """Flatten the layout into one record per (instruction, entry, unit, type)."""
    data = json.loads((database / FORMAT_E_LAYOUT).read_text(encoding="utf-8"))
    if data.get("format") != "E":
        raise SystemExit("bit layout is not format E")
    geometry = {
        "bundle_bits": int(data["bundle_bits"]),
        "payload_lsb": int(data["payload_lsb"]),
        "payload_budget_bits": int(data["payload_budget_bits"]),
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


def emit_tablegen(geometry: dict, placements: list[dict]) -> str:
    verify_decodable(placements)
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
        "// NOT included in the build. The MC layer still encodes Bundle128, whose",
        "// slot windows are wider than any entry window here, so these records",
        "// cannot coexist with it. They are emitted so the encoding can be reviewed",
        "// and diffed before the MC layer moves.",
        "//===----------------------------------------------------------------------===//",
        "",
    ]

    for (count, index) in sorted(positions):
        msb, lsb = positions[(count, index)]
        out += [
            f"class HaydnEntryP{count}{index}<dag outs, dag ins, string asm>",
            f"    : HaydnFormatInst<outs, ins, asm, []> {{",
            f"  bits<{msb - lsb + 1}> Inst;",
            "}",
            "",
        ]

    for p in sorted(placements, key=lambda x: (x["instruction"], x["entry_count"],
                                               x["entry_index"], x["unit"])):
        name = (f"{td_identifier(p['instruction'])}"
                f"_P{p['entry_count']}{p['entry_index']}_{p['unit']}")
        base = p["entry_lsb"]
        context = f"{p['instruction']}@{p['entry_count']}e{p['entry_index']}/{p['unit']}"

        outs, ins, bit_lines, decls = [], [], [], []
        for operand in p["operands"]:
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

        asm_operands = ", ".join(
            f"${p['operand_use'][o['field']]}" for o in p["operands"]
            if p["operand_use"].get(o["field"]))
        asm = p["instruction"].lower() + (f"\t{asm_operands}" if asm_operands else "")

        out.append(f"def {name} : HaydnEntryP{p['entry_count']}{p['entry_index']}<")
        out.append(f"    (outs {', '.join(outs)}), (ins {', '.join(ins)}),")
        out.append(f"    \"{asm}\"> {{")
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
        for msb, lsb, value, field in sorted(rows, key=lambda t: -t[0]):
            span = f"{msb}-{lsb}" if msb != lsb else f"{msb}"
            out.append(f"  let Inst{{{span}}} = {value};  // {field}")
        out.append("}")
        out.append("")

    return "\n".join(out)


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, required=True,
                        help="directory holding the read-only ISA database JSON")
    parser.add_argument("--emit", choices=("table", "report", "td"))
    parser.add_argument("--output", "-o", type=Path)
    parser.add_argument("--target-dir", type=Path,
                        help="Haydn target directory, for the .td comparison")
    parser.add_argument("--check", action="store_true",
                        help="validate the database and emit nothing")
    args = parser.parse_args()

    geometry, placements = load_placements(args.database)
    verify_decodable(placements)
    if args.check or args.emit is None:
        print(f"format E: {len(placements)} placements over "
              f"{len({(p['entry_count'], p['entry_index'], p['unit'], p['type']) for p in placements})}"
              f" (entry, unit, type) shapes — layout is self-consistent")
        return

    if args.emit == "td":
        text = emit_tablegen(geometry, placements)
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
