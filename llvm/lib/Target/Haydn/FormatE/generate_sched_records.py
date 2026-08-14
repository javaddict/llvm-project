#!/usr/bin/env python3
"""Generate published Haydn itinerary TableGen from golden-admitted aggregates.

Reads the same Format E golden hashes as generate_format_e_records.py (fail-closed
mismatch) and parses VLIW_Engine_Compiler_Constraints.md for the seven Shared
Unit names plus Data_Latency 1/2 and SIN_COS/ARCTAN (uimm4+2).

Emits HaydnGenSchedRecords.inc: ProcessorItineraries InstrItinData rows for the
published live classes (unit mapping + latency 1/2 scaffolds + conservative
SIN_COS/ARCTAN dest bound). Also emits HaydnGenMemoryCycles.inc: C++
getFirst/LastMemoryCycle lookup for the published Slot0_LS / Slot1_LD /
Slot01_LD latency-2 scaffold (AIE MemInstrItinData + AIEMemoryCyclesEmitter
peer). Does not import per-operation port/latency/pipeline tables and does
not set CompleteModel.

Peer: AIE generated ProcessorItineraries + InstrItinData
(llvm-aie llvm/lib/Target/AIE/aie2p/AIE2PGenSchedule.td:4226;
llvm-aie llvm/include/llvm/Target/AIETarget.td:22-47 MemoryCycles /
MemInstrItinData; llvm-aie llvm/utils/TableGen/AIEMemoryCyclesEmitter.cpp:123-157;
llvm-aie llvm/lib/Target/AIE/AIE2InstrInfo.cpp:53 AIE2GenMemoryCycles.inc).

Usage:
  generate_sched_records.py [--json PATH] [--xlsx PATH]
                            [--canonical-vectors PATH] [--constraints PATH]
                            [--out-dir DIR] [--check]
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

DEFAULT_GOLDEN_DIR = Path("/ssd2/mhyang/haydn-plans/Database/golden")
DEFAULT_JSON = DEFAULT_GOLDEN_DIR / "format_e_bit_layout_v2.json"
DEFAULT_XLSX = DEFAULT_GOLDEN_DIR / "format_e_bit_layout_v2.xlsx"
DEFAULT_CANONICAL = DEFAULT_GOLDEN_DIR / "format_e_canonical_vectors_v1.json"
DEFAULT_CONSTRAINTS = DEFAULT_GOLDEN_DIR / "VLIW_Engine_Compiler_Constraints.md"

# Same pins as FormatE/generate_format_e_records.py — do not drift.
PINNED_XLSX_SHA256 = (
    "9b3c06612cec47fa026bd79cff5632cb970abdfe1e161075444f7d02432574af"
)
# Repaired golden (see generate_format_e_records.py) — sched outputs derive
# from Behavior/latency, so the mapping repair changes nothing here, but the
# two importers must agree on which golden is current.
PINNED_JSON_SHA256 = (
    "8465132c2fb91e44a335d8a63577c637428d93106ed7a4d657d80ac70fdfa7f9"
)
PINNED_CANONICAL_SHA256 = (
    "6d403139d2530efbcee741456be330ce63843482d7fd372a18eab94cdfb728f9"
)

# Unreferenced 5-cycle AccLat classes of unknown provenance. Must not emit.
DEAD_ACC_LAT_CLASS_NAMES = (
    "Slot12_ALU_AccLat",
    "Slot2_ALU_AccLat",
)

# uimm4 is a 4-bit unsigned immediate (operand name in Constraints).
# Conservative dest bound = uimm4_max + 2 = 15 + 2. Not a new latency invent.
UIMM4_BITS = 4

GENERATED_NAME = "HaydnGenSchedRecords.inc"
GENERATED_MEMORY_CYCLES_NAME = "HaydnGenMemoryCycles.inc"

# Published load/store itineraries that report a memory-access cycle.
# Matches the product First/LastMemoryCycle surface (not Slot2_LS).
# first=0 (issue), last=Data_Latency-1 from the latency-2 scaffold.
MEMORY_ITIN_NAMES = ("Slot0_LS", "Slot1_LD", "Slot01_LD")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


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
        PublishedItin("PSEUDO", (), ()),
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
        PublishedItin("Slot2_LS", (u["ALU2"],), (l2,)),
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
    return (
        "//===-- HaydnGenSchedRecords.inc - published itineraries "
        "-*- tablegen -*-===//\n"
        "//\n"
        "// Auto-generated by FormatE/generate_sched_records.py\n"
        "// DO NOT EDIT. Regenerate with that script (supports --check).\n"
        "//\n"
        "// Published aggregate itineraries only: seven Shared Units, Data_Latency\n"
        "// 1/2 scaffolds, and SIN_COS/ARCTAN conservative dest bound (uimm4_max+2).\n"
        "// MemoryCycle first/last literals live in HaydnGenMemoryCycles.inc\n"
        "// (AIE MemInstrItinData / AIEMemoryCyclesEmitter peer), not here.\n"
        "// Per-operation port/latency/pipeline tables are not imported.\n"
        "// CompleteModel stays 0 in HaydnSchedule.td.\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n"
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
    return (
        "//===-- HaydnGenMemoryCycles.inc - MemoryCycle lookup "
        "-*- C++ -*-===//\n"
        "//\n"
        "// Auto-generated by FormatE/generate_sched_records.py\n"
        "// DO NOT EDIT. Regenerate with that script (supports --check).\n"
        "//\n"
        "// Overlay of AIE MemInstrItinData (AIETarget.td:22-47) and\n"
        "// AIEMemoryCyclesEmitter.cpp:123-157 / AIE2InstrInfo.cpp:53.\n"
        "// Published Slot0_LS / Slot1_LD / Slot01_LD only. first=0 (issue),\n"
        "// last=Data_Latency-1 from the latency-2 scaffold. Not a per-op invent.\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n"
        "\n"
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
    )


def prove_no_dead_classes(content: str) -> None:
    for name in DEAD_ACC_LAT_CLASS_NAMES:
        if name in content:
            raise SystemExit(f"error: generated output contains dead class {name}")
    if "[5, 1, 1, 5]" in content:
        raise SystemExit("error: generated output contains 5-cycle AccLat data")


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


def verify_golden_hashes(json_path: Path, xlsx_path: Path, canonical_path: Path) -> None:
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

    golden = resolve_golden_dir()
    json_path: Path = args.json or (golden / "format_e_bit_layout_v2.json")
    xlsx_path: Path = args.xlsx or (golden / "format_e_bit_layout_v2.xlsx")
    canonical_path: Path = args.canonical_vectors or (
        golden / "format_e_canonical_vectors_v1.json"
    )
    constraints_path: Path = args.constraints or (
        golden / "VLIW_Engine_Compiler_Constraints.md"
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
        verify_golden_hashes(json_path, xlsx_path, canonical_path)
        surf = parse_constraints(constraints_path)
        rows = published_itineraries(surf)
        validate_published(rows, surf)
        content = emit_sched_records_inc(rows, surf)
        mem_content = emit_memory_cycles_inc(rows)
        prove_no_dead_classes(content)
        prove_no_dead_classes(mem_content)
    except SystemExit as exc:
        msg = str(exc)
        if msg:
            print(msg, file=sys.stderr)
        return 2 if msg else 0

    out_path = args.out_dir / GENERATED_NAME
    mem_path = args.out_dir / GENERATED_MEMORY_CYCLES_NAME
    targets = {out_path: content, mem_path: mem_content}

    if args.check:
        failed = bool(diff_generated_targets(targets))
        if failed:
            return 1
        try:
            prove_flipped_byte_fails(targets)
        except SystemExit as exc:
            print(str(exc), file=sys.stderr)
            return 1
        mem_n = sum(1 for r in rows if r.mem_first is not None)
        print(
            "OK sched itineraries "
            f"rows={len(rows)} units={len(surf.units)} "
            f"sincos={surf.sincos_conservative} memcycles={mem_n}"
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
