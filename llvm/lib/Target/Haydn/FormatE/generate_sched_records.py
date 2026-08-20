#!/usr/bin/env python3
"""Generate published Haydn itinerary TableGen from golden-admitted aggregates.

Reads the same Format E golden hashes as generate_format_e_records.py (fail-closed
mismatch) and parses VLIW_Engine_Compiler_Constraints.md for the seven Shared
Unit names plus Data_Latency 1/2 and SIN_COS/ARCTAN (uimm4+2).

Emits HaydnGenSchedRecords.inc: ProcessorItineraries InstrItinData rows for the
published live classes (unit mapping + latency 1/2 scaffolds + conservative
SIN_COS/ARCTAN dest bound) plus generated Format E entry capacities
(E2=2 / E3=3). Also emits HaydnGenMemoryCycles.inc: C++
getFirst/LastMemoryCycle lookup for the published Slot0_LS / Slot1_LD /
Slot01_LD / Slot2_LS latency-2 scaffold (AIE MemInstrItinData +
AIEMemoryCyclesEmitter peer). Does not import per-operation
port/latency/pipeline tables and does not set CompleteModel.
Emits generated Format E entry capacities (E2=2 / E3=3) so
HaydnSchedModel.IssueWidth binds the E3 ProductRows EntryCount.

Peer: AIE generated ProcessorItineraries + InstrItinData
(llvm-aie llvm/lib/Target/AIE/aie2p/AIE2PGenSchedule.td:4226;
llvm-aie llvm/include/llvm/Target/AIETarget.td:22-47 MemoryCycles /
MemInstrItinData; llvm-aie llvm/utils/TableGen/AIEMemoryCyclesEmitter.cpp:123-157;
llvm-aie llvm/lib/Target/AIE/AIE2InstrInfo.cpp:53 AIE2GenMemoryCycles.inc).

Usage:
  generate_sched_records.py [--family NAME] [--json PATH] [--xlsx PATH]
                            [--canonical-vectors PATH] [--constraints PATH]
                            [--out-dir DIR] [--check]
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

from family_core import (
    PINNED_CANONICAL_SHA256,
    PINNED_JSON_SHA256,
    PINNED_XLSX_SHA256,
    SCHED_GENERATOR,
    add_family_argument,
    generated_banner,
    get_family,
    resolve_golden_dir,
    sha256_file,
    verify_authority_inputs,
)

# Unreferenced 5-cycle AccLat classes of unknown provenance. Must not emit.
DEAD_ACC_LAT_CLASS_NAMES = (
    "Slot12_ALU_AccLat",
    "Slot2_ALU_AccLat",
)

# uimm4 is a 4-bit unsigned immediate (operand name in Constraints).
# Conservative dest bound = uimm4_max + 2 = 15 + 2. Not a new latency invent.
UIMM4_BITS = 4

# Published load/store itineraries that report a memory-access cycle.
# Matches the product First/LastMemoryCycle surface, including Slot2_LS
# (S2 memory forms, `HaydnFormatsLS.td` Slot2_LS rows): every Slot*_LS /
# Slot*_LD itinerary must publish a MemoryCycle pair, else post-RA
# MemoryEdges fatals on the missing row (W21 / scheduling F1; silent
# latency-1 fallback on a no-interlock machine is a silicon hazard).
# first=0 (issue), last=Data_Latency-1 from the latency-2 scaffold.
MEMORY_ITIN_NAMES = ("Slot0_LS", "Slot1_LD", "Slot01_LD", "Slot2_LS")


@dataclass(frozen=True)
class GoldenLatencySurface:
    """Aggregate Data_Latency facts parsed from Constraints.md. Not per-op."""

    units: Tuple[str, ...]
    latency_1: int
    latency_2: int
    sincos_conservative: int


@dataclass(frozen=True)
class PublishedItin:
    """One published aggregate itinerary row (compiler class → units + cycles)."""

    name: str
    units: Tuple[str, ...]
    operand_cycles: Tuple[int, ...]
    # AIE MemInstrItinData FirstMemCycle / LastMemCycle. None = non-memory.
    mem_first: Optional[int] = None
    mem_last: Optional[int] = None


def parse_constraints(path: Path) -> GoldenLatencySurface:
    """Parse Shared Unit names and Data_Latency 1/2 / uimm4+2. No port invent."""
    text = path.read_text(encoding="utf-8")
    unit_m = re.search(
        r"### Shared Unit\n((?:- [A-Za-z0-9_]+\n)+)",
        text,
    )
    if not unit_m:
        raise SystemExit("error: Constraints.md missing ### Shared Unit list")
    units = tuple(
        line[2:].strip()
        for line in unit_m.group(1).splitlines()
        if line.startswith("- ")
    )
    expected = (
        "LOADSTORE0",
        "LOAD1",
        "ALU0",
        "ALU1",
        "ALU2",
        "MAC0",
        "MAC1",
    )
    if units != expected:
        raise SystemExit(
            f"error: Constraints Shared Unit {units} != published {expected}"
        )

    if "`Data_Latency = 1`" not in text:
        raise SystemExit("error: Constraints.md missing Data_Latency = 1")
    if "`Data_Latency = 2`" not in text:
        raise SystemExit("error: Constraints.md missing Data_Latency = 2")
    if "`Data_Latency = (uimm4 + 2)`" not in text:
        raise SystemExit("error: Constraints.md missing Data_Latency = (uimm4 + 2)")
    if "SIN_COS" not in text or "ARCTAN" not in text:
        raise SystemExit("error: Constraints.md missing SIN_COS / ARCTAN")

    sincos = ((1 << UIMM4_BITS) - 1) + 2
    return GoldenLatencySurface(
        units=units,
        latency_1=1,
        latency_2=2,
        sincos_conservative=sincos,
    )


def published_itineraries(surf: GoldenLatencySurface) -> Tuple[PublishedItin, ...]:
    """Current golden-admitted aggregate itineraries. No new latency numbers.

    Slot12_MAC_AccFirst remains as the FmtALU64Acc Itinerary= alias; OperandCycles
    use only published 1/2. The two 5-cycle AccLat classes are omitted.
    """
    u = {name: name for name in surf.units}
    l1, l2, l17 = surf.latency_1, surf.latency_2, surf.sincos_conservative
    mac_wb = (l2, l1, l1, l2)
    mac_acc = (l2, l2, l1, l1)
    # first=0 issue cycle; last=LoadLatency-1. Same numbers the hand switch
    # used; not a new latency invent. AIE MemoryCycles First/Last overlay.
    mem_first = 0
    mem_last = l2 - 1
    return (
        PublishedItin("Slot0_ALU", (u["ALU0"],), (l1,)),
        PublishedItin(
            "Slot0_LS", (u["LOADSTORE0"],), (l2,), mem_first, mem_last
        ),
        PublishedItin("Slot12_ALU", (u["ALU1"], u["ALU2"]), (l1,)),
        PublishedItin("Slot12_ALU_SinCosLat", (u["ALU1"], u["ALU2"]), (l17,)),
        PublishedItin("Slot012_ALU", (u["ALU0"], u["ALU1"], u["ALU2"]), (l1,)),
        PublishedItin("Slot1_LD", (u["LOAD1"],), (l2,), mem_first, mem_last),
        PublishedItin(
            "Slot01_LD", (u["LOADSTORE0"], u["LOAD1"]), (l2,), mem_first, mem_last
        ),
        PublishedItin("Slot1_ALU", (u["ALU1"],), (l1,)),
        PublishedItin("Slot1_ALU_SinCosLat", (u["ALU1"],), (l17,)),
        PublishedItin("Slot12_MAC", (u["MAC0"], u["MAC1"]), mac_wb),
        PublishedItin("Slot12_MAC_AccFirst", (u["MAC0"], u["MAC1"]), mac_acc),
        PublishedItin("Slot1_MAC", (u["MAC0"],), mac_wb),
        PublishedItin("Slot2_ALU", (u["ALU2"],), (l1,)),
        PublishedItin("Slot2_LS", (u["ALU2"],), (l2,), mem_first, mem_last),
        PublishedItin("Slot2_ALU_SinCosLat", (u["ALU2"],), (l17,)),
        PublishedItin("Slot2_MAC", (u["MAC1"],), mac_wb),
        # Per-slot AccFirst (CB-152c): committed members of FmtALU64Acc
        # logicals pin one MAC unit but must keep the accumulator-read-late
        # OperandCycles, or acc->acc chains (golden RecMII=1) grow a
        # spurious stall NOP. Same published numbers, unit-restricted.
        PublishedItin("Slot1_MAC_AccFirst", (u["MAC0"],), mac_acc),
        PublishedItin("Slot2_MAC_AccFirst", (u["MAC1"],), mac_acc),
    )


def validate_published(
    rows: Sequence[PublishedItin], surf: GoldenLatencySurface
) -> None:
    allowed = {surf.latency_1, surf.latency_2, surf.sincos_conservative}
    unit_set = set(surf.units)
    names: List[str] = []
    for row in rows:
        names.append(row.name)
        if row.name in DEAD_ACC_LAT_CLASS_NAMES:
            raise SystemExit(f"error: refusing to emit dead class {row.name}")
        for unit in row.units:
            if unit not in unit_set:
                raise SystemExit(f"error: {row.name} unit {unit} not in Constraints")
        for cyc in row.operand_cycles:
            if cyc not in allowed:
                raise SystemExit(
                    f"error: {row.name} operand cycle {cyc} not in {sorted(allowed)}"
                )
        is_mem = row.name in MEMORY_ITIN_NAMES
        if is_mem:
            if row.mem_first is None or row.mem_last is None:
                raise SystemExit(f"error: {row.name} missing MemoryCycle pair")
            if row.mem_first != 0 or row.mem_last != surf.latency_2 - 1:
                raise SystemExit(
                    f"error: {row.name} MemoryCycle "
                    f"{row.mem_first},{row.mem_last} != 0,{surf.latency_2 - 1}"
                )
        elif row.mem_first is not None or row.mem_last is not None:
            raise SystemExit(
                f"error: {row.name} is not a published memory itinerary"
            )
    for dead in DEAD_ACC_LAT_CLASS_NAMES:
        if dead in names:
            raise SystemExit(f"error: dead class leaked into published rows: {dead}")
    for mem_name in MEMORY_ITIN_NAMES:
        if mem_name not in names:
            raise SystemExit(f"error: published memory class {mem_name} missing")


def format_itin_data(row: PublishedItin) -> str:
    if not row.units and not row.operand_cycles:
        return f"    InstrItinData<{row.name}, []>"
    units = ", ".join(row.units)
    cycles = ", ".join(str(c) for c in row.operand_cycles)
    return (
        f"    InstrItinData<{row.name}, "
        f"[InstrStage<1, [{units}]>], [{cycles}]>"
    )


def emit_sched_records_inc(
    rows: Sequence[PublishedItin], surf: GoldenLatencySurface
) -> str:
    fu = ", ".join(surf.units)
    body_lines = [format_itin_data(row) for row in rows]
    body = ",\n".join(body_lines)
    banner = "\n".join(generated_banner(
        generator=SCHED_GENERATOR, family=get_family("e96")))
    return (
        "//===-- HaydnGenSchedRecords.inc - published itineraries "
        "-*- tablegen -*-===//\n"
        "//\n"
        f"{banner}\n"
        "//\n"
        "// Published aggregate itineraries only: seven Shared Units, Data_Latency\n"
        "// 1/2 scaffolds, and SIN_COS/ARCTAN conservative dest bound (uimm4_max+2).\n"
        "// MemoryCycle first/last literals live in HaydnGenMemoryCycles.inc\n"
        "// (AIE MemInstrItinData / AIEMemoryCyclesEmitter peer), not here.\n"
        "// Per-operation port/latency/pipeline tables are not imported.\n"
        "// CompleteModel stays 0 in HaydnSchedule.td.\n"
        "// Generated Format E entry capacities (ProductRows EntryCount):\n"
        "// E2=2 / E3=3. HaydnSchedModel.IssueWidth binds E3. Not a\n"
        "// competitive invent and not a second 2/3 literal at sched sites.\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n"
        "\n"
        "// Generated ProductRows EntryCount. One fact for E2=2 / E3=3.\n"
        "defvar FormatEE2EntryCapacity = 2;\n"
        "defvar FormatEE3EntryCapacity = 3;\n"
        "\n"
        "// Itinerary= alias for FmtALU64Acc. OperandCycles use only 1/2.\n"
        "def Slot12_MAC_AccFirst : InstrItinClass;\n"
        "// Per-slot AccFirst for committed accumulator members (CB-152c).\n"
        "def Slot1_MAC_AccFirst : InstrItinClass;\n"
        "def Slot2_MAC_AccFirst : InstrItinClass;\n"
        "\n"
        f"def HaydnItineraries : ProcessorItineraries<\n"
        f"  [{fu}],\n"
        f"  [],\n"
        f"  [\n"
        f"{body}\n"
        f"  ]>;\n"
    )


def emit_memory_cycles_inc(rows: Sequence[PublishedItin]) -> str:
    """C++ First/LastMemoryCycle bodies. AIE AIEMemoryCyclesEmitter.cpp:123-157."""
    mem_rows = [r for r in rows if r.mem_first is not None]
    if not mem_rows:
        raise SystemExit("error: no published MemoryCycle rows")
    firsts = [int(r.mem_first) for r in mem_rows]
    lasts = [int(r.mem_last) for r in mem_rows]
    first_cases = "\n".join(
        f"  case Haydn::Sched::{r.name}: return {r.mem_first}; // {r.name}"
        for r in mem_rows
    )
    last_cases = "\n".join(
        f"  case Haydn::Sched::{r.name}: return {r.mem_last}; // {r.name}"
        for r in mem_rows
    )
    banner = "\n".join(generated_banner(
        generator=SCHED_GENERATOR, family=get_family("e96")))
    units = ",\n".join(f'    "{u}"' for u in (
        "LOADSTORE0", "LOAD1", "ALU0", "ALU1", "ALU2", "MAC0", "MAC1"))
    family_block = (
        "#ifdef GET_HAYDN_FAMILY_SCHED\n"
        "// Family-scoped slot/coissue law (E96). Shared Unit order matches\n"
        "// Constraints.md and HaydnItineraries FuncUnits. Entry capacities\n"
        "// are the same FormatEE2/E3 facts as HaydnGenSchedRecords.inc.\n"
        "// Included from llvm::haydn::format_e in HaydnFormatERecords.h.\n"
        "inline constexpr unsigned GeneratedFamilyE2EntryCapacity = 2;\n"
        "inline constexpr unsigned GeneratedFamilyE3EntryCapacity = 3;\n"
        "inline constexpr const char *GeneratedFamilySharedUnits[] = {\n"
        f"{units}\n"
        "};\n"
        "#undef GET_HAYDN_FAMILY_SCHED\n"
        "#endif\n"
    )
    return (
        "//===-- HaydnGenMemoryCycles.inc - MemoryCycle lookup "
        "-*- C++ -*-===//\n"
        "//\n"
        f"{banner}\n"
        "//\n"
        "// Overlay of AIE MemInstrItinData (AIETarget.td:22-47) and\n"
        "// AIEMemoryCyclesEmitter.cpp:123-157 / AIE2InstrInfo.cpp:53.\n"
        "// Published Slot0_LS / Slot1_LD / Slot01_LD / Slot2_LS. first=0\n"
        "// (issue), last=Data_Latency-1 from the latency-2 scaffold. Not a\n"
        "// per-op invent.\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n"
        "\n"
        "#ifndef GET_HAYDN_FAMILY_SCHED\n"
        "std::optional<int>\n"
        "HaydnInstrInfo::getFirstMemoryCycle(unsigned SchedClass) const {\n"
        "  switch (SchedClass) {\n"
        "  default: return {};\n"
        f"{first_cases}\n"
        "  }\n"
        "}\n"
        "\n"
        "int HaydnInstrInfo::getMinFirstMemoryCycle() const {\n"
        f"  return {min(firsts)};\n"
        "}\n"
        "\n"
        "int HaydnInstrInfo::getMaxFirstMemoryCycle() const {\n"
        f"  return {max(firsts)};\n"
        "}\n"
        "\n"
        "std::optional<int>\n"
        "HaydnInstrInfo::getLastMemoryCycle(unsigned SchedClass) const {\n"
        "  switch (SchedClass) {\n"
        "  default: return {};\n"
        f"{last_cases}\n"
        "  }\n"
        "}\n"
        "\n"
        "int HaydnInstrInfo::getMinLastMemoryCycle() const {\n"
        f"  return {min(lasts)};\n"
        "}\n"
        "\n"
        "int HaydnInstrInfo::getMaxLastMemoryCycle() const {\n"
        f"  return {max(lasts)};\n"
        "}\n"
        "#endif // GET_HAYDN_FAMILY_SCHED\n"
        "\n"
        f"{family_block}"
    )


def prove_no_dead_classes(content: str) -> None:
    for name in DEAD_ACC_LAT_CLASS_NAMES:
        if name in content:
            raise SystemExit(f"error: generated output contains dead class {name}")
    if "[5, 1, 1, 5]" in content:
        raise SystemExit("error: generated output contains 5-cycle AccLat data")


def prove_generated_entry_capacities(content: str) -> None:
    """One generated E2=2 / E3=3 fact. CompleteModel stays 0."""
    if "defvar FormatEE2EntryCapacity = 2;" not in content:
        raise SystemExit("error: generated E2 entry capacity missing")
    if "defvar FormatEE3EntryCapacity = 3;" not in content:
        raise SystemExit("error: generated E3 entry capacity missing")
    if "CompleteModel = 1" in content:
        raise SystemExit("error: generated records must not set CompleteModel=1")


def prove_family_sched_records(content: str) -> None:
    """Family-scoped slot law is generated, not a second handwritten table."""
    if "GeneratedFamilyE2EntryCapacity = 2;" not in content:
        raise SystemExit("error: family-scoped E2 capacity missing")
    if "GeneratedFamilyE3EntryCapacity = 3;" not in content:
        raise SystemExit("error: family-scoped E3 capacity missing")
    for unit in (
        "LOADSTORE0",
        "LOAD1",
        "ALU0",
        "ALU1",
        "ALU2",
        "MAC0",
        "MAC1",
    ):
        if f'"{unit}"' not in content:
            raise SystemExit(f"error: family-scoped unit {unit} missing")
    if "GET_HAYDN_FAMILY_SCHED" not in content:
        raise SystemExit("error: family-sched include guard missing")


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


def prove_flipped_byte_fails(targets: Dict[Path, str]) -> None:
    first = next(iter(targets))
    if not first.is_file():
        raise SystemExit(f"stale-flip probe needs committed file {first}")
    with tempfile.TemporaryDirectory(prefix="haydn-sched-stale-") as tmp:
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
    surf: GoldenLatencySurface, content: str, mem_content: str
) -> None:
    """P19: a changed Constraints fact must change the sched projection."""
    mut = replace(surf, sincos_conservative=surf.sincos_conservative + 1)
    mut_rows = published_itineraries(mut)
    mutated = emit_sched_records_inc(mut_rows, mut)
    if mutated == content:
        raise SystemExit(
            "P19 source-mutation: sincos flip reused committed sched records"
        )
    restored_rows = published_itineraries(surf)
    restored = emit_sched_records_inc(restored_rows, surf)
    second = emit_sched_records_inc(restored_rows, surf)
    if restored != content:
        raise SystemExit(
            "P19 source-mutation: restored surface did not re-emit committed sched"
        )
    if restored != second:
        raise SystemExit("P19 determinism: two sched regenerations differed")
    mem2 = emit_memory_cycles_inc(restored_rows)
    if mem2 != mem_content:
        raise SystemExit("P19 determinism: memory-cycle regeneration drifted")
    print("OK P19 sched source-mutation + determinism")


def verify_golden_hashes(
    golden: Path,
    json_path: Path,
    xlsx_path: Path,
    canonical_path: Path,
    constraints_path: Path,
) -> None:
    verify_authority_inputs(
        golden,
        [
            json_path.name,
            xlsx_path.name,
            canonical_path.name,
            constraints_path.name,
        ],
    )
    json_sha = sha256_file(json_path)
    if json_sha != PINNED_JSON_SHA256:
        raise SystemExit(
            f"error: JSON sha256 {json_sha} != pinned {PINNED_JSON_SHA256}"
        )
    xlsx_sha = sha256_file(xlsx_path)
    if xlsx_sha != PINNED_XLSX_SHA256:
        raise SystemExit(
            f"error: XLSX sha256 {xlsx_sha} != pinned {PINNED_XLSX_SHA256}"
        )
    canon_sha = sha256_file(canonical_path)
    if canon_sha != PINNED_CANONICAL_SHA256:
        raise SystemExit(
            f"error: canonical-vector sha256 {canon_sha} != pinned "
            f"{PINNED_CANONICAL_SHA256}"
        )


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    add_family_argument(ap)
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--xlsx", type=Path, default=None)
    ap.add_argument("--canonical-vectors", type=Path, default=None)
    ap.add_argument("--constraints", type=Path, default=None)
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Target Haydn directory (default: llvm/lib/Target/Haydn)",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="Verify committed output against regenerate (no write)",
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
    constraints_path: Path = args.constraints or (
        golden / family.constraints_filename
    )

    for path, label in (
        (json_path, "golden JSON"),
        (xlsx_path, "golden XLSX"),
        (canonical_path, "canonical vectors"),
        (constraints_path, "Constraints.md"),
    ):
        if not path.is_file():
            print(f"error: {label} not found: {path}", file=sys.stderr)
            return 2

    try:
        verify_golden_hashes(
            golden, json_path, xlsx_path, canonical_path, constraints_path
        )
        surf = parse_constraints(constraints_path)
        rows = published_itineraries(surf)
        validate_published(rows, surf)
        content = emit_sched_records_inc(rows, surf)
        mem_content = emit_memory_cycles_inc(rows)
        prove_no_dead_classes(content)
        prove_no_dead_classes(mem_content)
        prove_generated_entry_capacities(content)
        prove_family_sched_records(mem_content)
    except SystemExit as exc:
        msg = str(exc)
        if msg:
            print(msg, file=sys.stderr)
        return 2 if msg else 0

    out_path = args.out_dir / family.sched_records_inc
    mem_path = args.out_dir / family.memory_cycles_inc
    targets = {out_path: content, mem_path: mem_content}

    if args.check:
        failed = bool(diff_generated_targets(targets))
        if failed:
            return 1
        try:
            prove_flipped_byte_fails(targets)
            prove_source_mutation_not_silent(surf, content, mem_content)
        except SystemExit as exc:
            print(str(exc), file=sys.stderr)
            return 1
        mem_n = sum(1 for r in rows if r.mem_first is not None)
        print(
            "OK sched itineraries "
            f"rows={len(rows)} units={len(surf.units)} "
            f"sincos={surf.sincos_conservative} memcycles={mem_n} "
            "e2=2 e3=3 complete_model=0"
        )
        return 0

    args.out_dir.mkdir(parents=True, exist_ok=True)
    out_path.write_text(content, encoding="utf-8")
    mem_path.write_text(mem_content, encoding="utf-8")
    print(f"wrote {out_path}")
    print(f"wrote {mem_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
