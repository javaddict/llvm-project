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
Slot01_LD latency-2 scaffold (AIE MemInstrItinData +
AIEMemoryCyclesEmitter peer).
Emits generated Format E entry capacities (E2=2 / E3=3) so
HaydnSchedModel.IssueWidth binds the E3 ProductRows EntryCount.

M18 per-operation import: also emits HaydnGenPerOpResources.inc from golden
instruction_type_index.json — one HaydnAdmittedPerOpResourceRecord per
compiler-reachable logical that has a golden row (unit mask over the seven
shared units, per-bank read/write port counts, Data_Latency, required
alignment bytes). Compiler reachability is the generated-member families of
HaydnFormatsE96Members.td.inc plus hand defs in HaydnInstrInfo.td — no
opcode is inferred: a logical without a golden row is recorded in the
uncovered census, never synthesized. CompleteModel stays 0 (the census is
nonempty: machine-generic opcodes and Haydn pseudos/wide variants have no
unit), so competitive II/density claims stay closed exactly as GE96-04
requires; per-op lookups open only for the covered set.

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
import json
import re
import sys
import tempfile
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

from family_core import (
    PINNED_CANONICAL_SHA256,
    PINNED_INDEX_SHA256,
    PINNED_JSON_SHA256,
    PINNED_XLSX_SHA256,
    SCHED_GENERATOR,
    add_family_argument,
    check_cutover_surfaces,
    generated_banner,
    get_family,
    golden_inputs_pin_path,
    resolve_golden_dir,
    sha256_file,
    verify_authority_inputs,
    verify_golden_inputs_pin,
)

# Unreferenced 5-cycle AccLat classes of unknown provenance. Must not emit.
DEAD_ACC_LAT_CLASS_NAMES = (
    "Slot12_ALU_AccLat",
    "Slot2_ALU_AccLat",
    # 2026-08-21 itinerary re-map retirees: golden Available re-mapped every
    # former user (X2/X4 ALU shells -> Slot012_ALU/DspLat; MAC multiplies ->
    # Slot12_MAC; D_LQHWUA_POST -> Slot01_LD). Orphaned classes drop out of
    # the tblgen Sched enum, so no row may reference them.
    "Slot12_ALU",
    "Slot2_LS",
)

# uimm4 is a 4-bit unsigned immediate (operand name in Constraints).
# Conservative dest bound = uimm4_max + 2 = 15 + 2. Not a new latency invent.
UIMM4_BITS = 4

# Fixed Data_Latency = 2 DSP-unary / CSR-read surface (2026-08-21 latency
# P0/P1, audit_latencies.md): golden instruction_type_index Pipeline_Info
# pins Data_Latency=2 for LOG2/EXP2/RECIP/SQRT (Available ALU1|ALU2) and
# CSRR (Available ALU0|ALU1|ALU2). Same published latency-2 number the
# memory scaffold uses; not a new latency invent.
DSPLY2_LOGICALS = frozenset({"LOG2", "EXP2", "RECIP", "SQRT"})
CSR_LY2_LOGICALS = frozenset({"CSRR"})

# Published load/store itineraries that report a memory-access cycle.
# Matches the product First/LastMemoryCycle surface: every Slot*_LS /
# Slot*_LD itinerary must publish a MemoryCycle pair, else post-RA
# MemoryEdges fatals on the missing row (W21 / scheduling F1; silent
# latency-1 fallback on a no-interlock machine is a silicon hazard).
# first=0 (issue), last=Data_Latency-1 from the latency-2 scaffold.
# 2026-08-21 itinerary re-map: Slot2_LS retired — golden assigns every
# former S2 memory row to LOADSTORE0/LOAD1 units (Slot0_LS/Slot01_LD), so
# no instruction books it and the class left the Sched enum.
# 2026-08-21 latency P3: Slot0_LS_WbLat joins the published memory set
# (store-with-writeback family). Its OperandCycles drop to 1 (golden
# Data_Latency=1 for the writeback register) but the MemoryCycle pair
# stays (0, Data_Latency-1): the memory access itself keeps the
# conservative store→load spacing — the register latency and the memory
# edge are different facts and only the former is golden-1.
MEMORY_ITIN_NAMES = ("Slot0_LS", "Slot1_LD", "Slot01_LD", "Slot0_LS_WbLat")


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
    2026-08-21 latency P3: golden per-op Data_Latency=1 families get their
    own rows — store-with-writeback (Slot0_LS_WbLat) and non-accumulating
    multiplies (Slot12_MAC_MulLat + per-slot member rows). Assignment is
    golden-derived row-by-row in generate_format_e_records.py (the
    instruction_type_index Data_Latency admission mechanism); this table
    only publishes the numbers.
    """
    u = {name: name for name in surf.units}
    l1, l2, l17 = surf.latency_1, surf.latency_2, surf.sincos_conservative
    mac_wb = (l2, l1, l1, l2)
    mac_acc = (l2, l2, l1, l1)
    # Fresh-dest multiply: golden Data_Latency=1 (result next bundle) with
    # plain early-read sources — the wb/acc asymmetric shapes do not apply
    # because no operand is an accumulator tie.
    mac_mul_lat1 = (l1, l1, l1, l1)
    # first=0 issue cycle; last=LoadLatency-1. Same numbers the hand switch
    # used; not a new latency invent. AIE MemoryCycles First/Last overlay.
    mem_first = 0
    mem_last = l2 - 1
    return (
        PublishedItin("Slot0_ALU", (u["ALU0"],), (l1,)),
        PublishedItin(
            "Slot0_LS", (u["LOADSTORE0"],), (l2,), mem_first, mem_last
        ),
        # Store-with-writeback: the rs writeback register is golden
        # Data_Latency=1 (available next bundle). The MemoryCycle pair
        # stays (0, Data_Latency-1) so store→load memory edges keep the
        # conservative spacing — register latency and the memory edge are
        # separate facts and only the former is golden-1 (2026-08-21
        # latency P3, audit_latency.md mismatch #3).
        PublishedItin(
            "Slot0_LS_WbLat", (u["LOADSTORE0"],), (l1,), mem_first, mem_last
        ),
        PublishedItin("Slot12_ALU_SinCosLat", (u["ALU1"], u["ALU2"]), (l17,)),
        # Fixed Data_Latency=2 DSP unary (LOG2/EXP2/RECIP/SQRT) on their
        # golden ALU1|ALU2 menu, and per-slot member rows mirroring the
        # SinCosLat pattern (members pin one unit; latency stays 2).
        PublishedItin("Slot12_ALU_DspLat", (u["ALU1"], u["ALU2"]), (l2,)),
        PublishedItin("Slot1_ALU_DspLat", (u["ALU1"],), (l2,)),
        PublishedItin("Slot2_ALU_DspLat", (u["ALU2"],), (l2,)),
        # Fixed Data_Latency=2 CSR read (CSRR) on its golden ALU0|ALU1|ALU2
        # menu + per-slot member rows (CSRW's SFR-domain dest has no HR dest
        # window; documented P2 residual, not covered here).
        PublishedItin("Slot012_ALU_CsrLat", (u["ALU0"], u["ALU1"], u["ALU2"]), (l2,)),
        PublishedItin("Slot0_ALU_CsrLat", (u["ALU0"],), (l2,)),
        PublishedItin("Slot1_ALU_CsrLat", (u["ALU1"],), (l2,)),
        PublishedItin("Slot2_ALU_CsrLat", (u["ALU2"],), (l2,)),
        PublishedItin("Slot012_ALU", (u["ALU0"], u["ALU1"], u["ALU2"]), (l1,)),
        PublishedItin("Slot1_LD", (u["LOAD1"],), (l2,), mem_first, mem_last),
        PublishedItin(
            "Slot01_LD", (u["LOADSTORE0"], u["LOAD1"]), (l2,), mem_first, mem_last
        ),
        PublishedItin("Slot1_ALU", (u["ALU1"],), (l1,)),
        PublishedItin("Slot1_ALU_SinCosLat", (u["ALU1"],), (l17,)),
        PublishedItin("Slot12_MAC", (u["MAC0"], u["MAC1"]), mac_wb),
        PublishedItin("Slot12_MAC_AccFirst", (u["MAC0"], u["MAC1"]), mac_acc),
        # Non-accumulating multiplies, golden Data_Latency=1 (fresh dest
        # next bundle): logical menu + per-slot member rows mirroring the
        # AccFirst split. Members with the wb shape would stall every
        # mul→use chain one extra cycle and inflate RecMII (2026-08-21
        # latency P3, audit_latency.md mismatch #4 — the lat-1 set is
        # derived per-row from golden, never a family list).
        PublishedItin(
            "Slot12_MAC_MulLat", (u["MAC0"], u["MAC1"]), mac_mul_lat1
        ),
        PublishedItin("Slot1_MAC_MulLat", (u["MAC0"],), mac_mul_lat1),
        PublishedItin("Slot2_MAC_MulLat", (u["MAC1"],), mac_mul_lat1),
        PublishedItin("Slot1_MAC", (u["MAC0"],), mac_wb),
        PublishedItin("Slot2_ALU", (u["ALU2"],), (l1,)),
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


# ---------------------------------------------------------------------------
# M18 per-operation resource import (golden instruction_type_index.json)
# ---------------------------------------------------------------------------

# Golden unit name → HAYDN_ADMITTED_UNIT_* bit in HaydnPortModel.h. The bit
# order is the HaydnSchedule.td ProcessorItineraries FuncUnit order and
# HaydnExecUnit; one typed mapping, no per-site literals.
UNIT_BIT: Dict[str, int] = {
    "LOADSTORE0": 0,
    "LOAD1": 1,
    "ALU0": 2,
    "ALU1": 3,
    "ALU2": 4,
    "MAC0": 5,
    "MAC1": 6,
}

# Bank port fields of one golden row, in HaydnAdmittedPerOpResourceRecord
# field order.
PORT_FIELDS: Tuple[Tuple[str, str], ...] = (
    ("GPRReadPorts", "GPR_Read_Port"),
    ("GPRWritePorts", "GPR_Write_Port"),
    ("DRReadPorts", "DR_Read_Port"),
    ("DRWritePorts", "DR_Write_Port"),
    ("ARReadPorts", "AR_Read_Port"),
    ("ARWritePorts", "AR_Write_Port"),
    ("SFRReadPorts", "SFR_Read_Port"),
    ("SFRWritePorts", "SFR_Write_Port"),
)

# Required_Alignment text → byte alignment. Golden prose forms only; a new
# prose shape fails closed instead of guessing a number.
ALIGNMENT_BYTES: Dict[str, int] = {
    "1-byte.": 1,
    "the value in the rs register should be aligned 2-byte.": 2,
    "the value in the rs register should be aligned 4-byte.": 4,
    "the value in the rs register should be aligned 8-byte.": 8,
    "the value in the rs1 register should be aligned 2-byte.": 2,
    "the value in the rs1 register should be aligned 4-byte.": 4,
    "the value in the rs1 register should be aligned 8-byte.": 8,
    "the value in rs register should be aligned 2-byte.": 2,
    "the value in rs1 register should be aligned 2-byte.": 2,
    "the value rs + (imm6 << 1) should be aligned 2-byte.": 2,
    "the value rs + (imm6 << 2) should be aligned 4-byte.": 4,
    "the value rs + (imm6 << 3) should be aligned 8-byte.": 8,
    "the value rs1 + rs2 should be aligned 2-byte.": 2,
    "the value rs1 + rs2 should be aligned 4-byte.": 4,
    "the value rs1 + rs2 should be aligned 8-byte.": 8,
    "the value (rs1 + rs2) should be aligned 4-byte.": 4,
    "the value (rs1 + rs2) should be aligned 8-byte.": 8,
    "the value REVERSE32(rs) should be aligned 4-byte.": 4,
    "the value REVERSE32(rs) should be aligned 8-byte.": 8,
    "the value REVERSE32(rs1) should be aligned 4-byte.": 4,
    "the value REVERSE32(rs1) should be aligned 8-byte.": 8,
}

# OperandCycles room mirrored from HaydnPortModel.h
# HAYDN_ADMITTED_OPERAND_CYCLE_ROOM. One fact; the C++ static_assert in the
# emitted table pins the C++ side to the generator side.
ADMITTED_OPERAND_CYCLE_ROOM = 8


@dataclass(frozen=True)
class PerOpResourceRow:
    """One golden-admitted per-logical resource record."""

    name: str
    unit_mask: int
    ports: Tuple[int, ...]
    # Scalar max Data_Latency; None = golden silent (stores/branches/hints).
    latency: Optional[int]
    # SIN_COS/ARCTAN (uimm4+2): dest bound is the published conservative 17.
    latency_conservative: Optional[int]
    # OperandCycles payload: [dest latency] when a latency is known.
    operand_cycles: Tuple[int, ...]
    alignment: int
    # Golden format key (provenance comment only).
    fmt: str


def parse_index_units(raw: object) -> Tuple[str, ...]:
    """Golden Available is a list or a bare string; both → unit tuple."""
    if raw is None:
        return ()
    if isinstance(raw, str):
        return (raw,)
    if isinstance(raw, list):
        return tuple(raw)
    raise SystemExit(f"error: golden Available shape {type(raw).__name__}")


def parse_index_latency(
    raw: object, sincos_bound: int
) -> Tuple[Optional[int], Optional[int]]:
    """Data_Latency → (scalar, conservative). Both None when golden silent.

    '(uimm4 + 2)' is operand-dependent; the published conservative dest
    bound (uimm4_max+2) is the schedulable number, recorded as the
    conservative field so consumers never mistake it for a golden scalar.
    """
    if raw is None:
        return (None, None)
    if isinstance(raw, int):
        return (raw, None)
    if raw == "(uimm4 + 2)":
        return (None, sincos_bound)
    raise SystemExit(f"error: golden Data_Latency shape {raw!r}")


def parse_index_alignment(raw: object) -> int:
    if raw is None:
        return 0
    if not isinstance(raw, str):
        raise SystemExit(f"error: golden Required_Alignment shape {raw!r}")
    text = raw.strip()
    if text in ("N/A", ""):
        return 0
    got = ALIGNMENT_BYTES.get(text)
    if got is None:
        raise SystemExit(
            f"error: unmapped golden Required_Alignment prose {text!r}; "
            "map it in ALIGNMENT_BYTES or fail closed"
        )
    return got


def load_golden_index(index_path: Path) -> Dict[str, dict]:
    """instruction_type_index.json rows keyed by uppercase instruction name."""
    data = json.loads(index_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise SystemExit("error: instruction_type_index.json is not an object")
    by_name: Dict[str, Tuple[str, dict]] = {}
    for fmt, recs in data.items():
        if not isinstance(recs, list):
            raise SystemExit(f"error: index format {fmt} is not a list")
        for rec in recs:
            name = rec.get("Instruction")
            if not isinstance(name, str) or not name:
                raise SystemExit("error: index row without Instruction")
            key = name.upper()
            if key in by_name:
                raise SystemExit(f"error: duplicate index row {name}")
            by_name[key] = (fmt, rec)
    if not by_name:
        raise SystemExit("error: instruction_type_index.json is empty")
    return by_name


def compiler_reachable_logicals(haydn_dir: Path) -> Tuple[set, set]:
    """Logical-name universe the compiler can select or parse.

    Two source-tree anchors (the same ones the product build compiles):
    generated member families of HaydnFormatsE96Members.td.inc and hand
    defs in the authored instruction files (HaydnInstrInfo.td plus the
    Format E logical shells). Returns (member_families, hand_defs); a name
    is compiler-reachable when it appears in either. Not an opcode enum
    copy: build artifacts are unreadable from the source-tree generator.
    """
    members_path = haydn_dir / "HaydnFormatsE96Members.td.inc"
    if not members_path.is_file():
        raise SystemExit(
            f"error: member families anchor missing: {members_path}"
        )
    member_text = members_path.read_text(encoding="utf-8")
    member_fams = set(re.findall(r"\b([A-Z][A-Z0-9_]*)_E[23]_E?\d_", member_text))
    hand: set = set()
    for fn in ("HaydnInstrInfo.td", "HaydnMultiSlotPseudo.td"):
        path = haydn_dir / fn
        if not path.is_file():
            raise SystemExit(f"error: hand-def anchor missing: {path}")
        text = path.read_text(encoding="utf-8")
        # strip comments first: doc comments name retired opcodes too
        text = re.sub(r"//.*", "", text)
        hand.update(re.findall(r"^def\s+([A-Za-z][A-Za-z0-9_]*)", text, re.M))
    return member_fams, hand


# M18 name map: codegen shells whose public spelling differs from the golden
# instruction_type_index.json Instruction name. Each entry maps a
# compiler-reachable logical to the golden row that owns its semantics — the
# row is imported under the codegen name (units/ports/latency from golden);
# no row is synthesized. Width-by-width LS shells map to their S_/D_ family
# (the selector emits LD32/ST32 etc. directly with imm 0 — they are the
# S_LW_WITH_IMM/S_SW_WITH_IMM instructions under compiler spellings); _W wide
# shells drop the suffix (the same golden op in the wide assembler surface);
# shifts use golden mnemonics SLL/SRL/SRA. NOP/WFI/WFITBDTBDTBD stay
# uncovered by design: NOP is the architectural idle (no unit/latency fact),
# WFI<TBD> is the golden placeholder for an unpublished op.
SHELL_TO_GOLDEN = {
    "LD8": "S_LBS_WITH_IMM",
    "LDU8": "S_LBU_WITH_IMM",
    "LD16": "S_LHWS_WITH_IMM",
    "LDU16": "S_LHWU_WITH_IMM",
    "LD32": "S_LW_WITH_IMM",
    "LD64": "D_LDW_WITH_IMM",
    "ST8": "S_SB_WITH_IMM",
    "ST16": "S_SHW_WITH_IMM",
    "ST32": "S_SW_WITH_IMM",
    "ST64": "D_SDW_WITH_IMM",
    "LD32_POST": "S_LW_POST_IMM",
    "LD64_POST": "D_LDW_POST_IMM",
    "ST32_POST": "S_SW_POST_IMM",
    "ST64_POST": "D_SDW_POST_IMM",
    "LD32_REG_M0S0LS": "S_LW_WITH_REG",
    "LD64_REG_M0S0LS": "D_LDW_WITH_REG",
    "ST32_REG_M0S0LS": "S_SW_WITH_REG",
    "ST64_REG_M0S0LS": "D_SDW_WITH_REG",
    "SHL32": "SLL32",
    "LSR32": "SRL32",
    "ASR32": "SRA32",
    "PLDWWUA": "PLDWWUA_POST",
    "SEXT_GPR32_TO_DR64": "SEXT32T64",
    "ADDI32_W": "ADDI32",
    "BEQZ_W": "BEQZ",
    "BEQ_W": "BEQ",
    "BGEU_W": "BGEU",
    "BGEZ_W": "BGEZ",
    "BGE_W": "BGE",
    "BLTU_W": "BLTU",
    "BLTZ_W": "BLTZ",
    "BLT_W": "BLT",
    "BNEZ_W": "BNEZ",
    "BNE_W": "BNE",
    "CSRW_W": "CSRW",
    "JALR_W": "JALR",
    "JAL_W": "JAL",
    "ORI32_W": "ORI32",
    "SET_HWLOOP_F2_W": "SET_HWLOOP_F2",
    "SET_HWLOOP_REG_W": "SET_HWLOOP_REG",
    "SET_HWLOOP_W": "SET_HWLOOP",
}


def build_per_op_rows(
    index: Dict[str, dict], member_fams: set, hand: set, surf: GoldenLatencySurface
) -> Tuple[List[PerOpResourceRow], List[str]]:
    """Rows for every compiler-reachable logical with a golden record.

    Fail-closed census: reachable names without a golden row (directly or
    through SHELL_TO_GOLDEN) are returned as the uncovered list (never
    synthesized). WFI<TBD> is the golden placeholder for an unpublished op;
    its WFITBDTBDTBD member family maps to no record. NOP is the
    architectural idle parcel — no unit/latency fact exists to import.
    """
    def golden_row_for(name: str):
        if name in index:
            return index[name]
        mapped = SHELL_TO_GOLDEN.get(name)
        if mapped is not None and mapped in index:
            return index[mapped]
        return None

    reachable = {n for n in (member_fams | hand) if golden_row_for(n) is not None}
    rows: List[PerOpResourceRow] = []
    for name in sorted(reachable):
        fmt, rec = golden_row_for(name)
        units = parse_index_units(rec.get("Available"))
        mask = 0
        for unit in units:
            bit = UNIT_BIT.get(unit)
            if bit is None:
                raise SystemExit(
                    f"error: {name} golden unit {unit!r} has no admitted bit"
                )
            mask |= 1 << bit
        if mask == 0:
            raise SystemExit(f"error: {name} golden Available is empty")
        ports = []
        for field, golden_key in PORT_FIELDS:
            raw = rec.get(golden_key)
            if raw is None:
                ports.append(0)
                continue
            if not isinstance(raw, list):
                raise SystemExit(
                    f"error: {name} golden {golden_key} shape "
                    f"{type(raw).__name__}"
                )
            ports.append(len(raw))
        lat, lat_cons = parse_index_latency(
            (rec.get("Pipeline_Info") or {}).get("Data_Latency"),
            surf.sincos_conservative,
        )
        # OperandCycles payload mirrors the published itinerary shape for
        # the dest: one entry carrying the scalar (or conservative) bound.
        if lat is not None:
            operand_cycles: Tuple[int, ...] = (lat,)
        elif lat_cons is not None:
            operand_cycles = (lat_cons,)
        else:
            operand_cycles = ()
        rows.append(
            PerOpResourceRow(
                name=name,
                unit_mask=mask,
                ports=tuple(ports),
                latency=lat,
                latency_conservative=lat_cons,
                operand_cycles=operand_cycles,
                alignment=parse_index_alignment(rec.get("Required_Alignment")),
                fmt=fmt,
            )
        )
    covered = {r.name for r in rows}
    uncovered = sorted(n for n in (member_fams | hand) if n not in covered)
    return rows, uncovered


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
    """Itineraries + M18 llvm-mca bridge (ProcResource/SchedWriteRes/ItinRW).

    llvm-mca requires a SchedRW model (MCSchedModel::SchedClassTable); a
    ProcessorItineraries-only model is rejected ("instruction itineraries
    are currently unsupported"). The LLVM-native bridge for itinerary
    targets is ItinRW (TargetSchedule.td:477; ARM ARMScheduleA9.td:2284):
    itinerary class → SchedWrite → SchedWriteRes on ProcResources. The
    resources are the same seven units the itinerary InstrStages reserve
    (ProcResource<1> each; dual-unit menus become resource groups), and
    SchedWriteRes Latency is the class's max published OperandCycle — the
    same golden numbers, one generated projection, no hand literals.
    """
    fu = ", ".join(surf.units)
    body_lines = [format_itin_data(row) for row in rows]
    body = ",\n".join(body_lines)
    banner = "\n".join(generated_banner(
        generator=SCHED_GENERATOR, family=get_family("e96")))
    # ---- M18 llvm-mca bridge (ItinRW), generated from the same rows ----
    # One ProcResource per shared unit (single-unit itineraries point at
    # the unit directly) plus one resource GROUP per distinct multi-unit
    # menu so ItinRW can map dual/triple classes without inventing an
    # issue-arbitration policy: mca's DefaultResourceStrategy picks the
    # ready unit, exactly like the HR's injective placement.
    res_of = {u: f"HaydnRes{u}" for u in surf.units}
    unit_tag = {
        "LOADSTORE0": "LS",
        "LOAD1": "L1",
        "ALU0": "A0",
        "ALU1": "A1",
        "ALU2": "A2",
        "MAC0": "M0",
        "MAC1": "M1",
    }
    group_of: Dict[Tuple[str, ...], str] = {}
    group_defs: List[str] = []
    for row in rows:
        units = tuple(row.units)
        if len(units) < 2 or units in group_of:
            continue
        gname = "HaydnUnitGrp" + "".join(unit_tag[u] for u in units)
        group_of[units] = gname
        members = ", ".join(res_of[u] for u in units)
        group_defs.append(f"def {gname} : ProcResGroup<[{members}]>;")
    # SchedWriteRes per itinerary class (named defs, ARM A9 form): the
    # SchedWriteRes class carries the SchedModel field, so it works under
    # `let SchedModel = ... in` where anonymous `def : WriteRes` does not.
    # Latency = max OperandCycle of the class (golden dest bound; the
    # load/store conservative 2).
    write_res: List[str] = []
    itin_rw_classes: List[str] = []
    for row in rows:
        wname = f"HW_{row.name}"
        resource = (
            group_of[tuple(row.units)]
            if len(row.units) >= 2
            else res_of[row.units[0]]
        )
        lat = max(row.operand_cycles) if row.operand_cycles else 1
        write_res.append(
            f"def {wname} : SchedWriteRes<[{resource}]> {{ let Latency = {lat}; }}"
        )
        itin_rw_classes.append(row.name)
    # Chunk the ItinRW class lists to keep lines readable.
    itin_chunks: List[str] = []
    chunk: List[str] = []
    for name in itin_rw_classes:
        chunk.append(name)
        if len(chunk) == 6:
            itin_chunks.append(chunk)
            chunk = []
    if chunk:
        itin_chunks.append(chunk)
    itin_rw_lines = []
    for chunk_names in itin_chunks:
        writes = ", ".join(f"HW_{n}" for n in chunk_names)
        classes = ",\n  ".join(chunk_names)
        itin_rw_lines.append(
            f"def : ItinRW<[{writes}],\n  [{classes}]>;"
        )
    bridge = (
        "//===-- HaydnGenSchedMcaBridge.td.inc - llvm-mca ItinRW bridge -*-===//\n"
        "//\n"
        f"{banner}\n"
        "//\n"
        "// ---- M18 llvm-mca bridge (generated; ARM A9 ItinRW peer) ----\n"
        "// llvm-mca needs a SchedRW model; ItinRW maps each published\n"
        "// itinerary class to a SchedWrite whose WriteRes carries the same\n"
        "// golden latency and reserves the same units (groups for menus).\n"
        "// This is the mca/latency projection of the itineraries in\n"
        "// HaydnGenSchedRecords.inc — not a second scheduling policy and\n"
        "// not a completeness claim (the HaydnSchedModel polarity is\n"
        "// untouched). Separate file because `let SchedModel =` in\n"
        "// HaydnSchedule.td must follow the HaydnSchedModel def.\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n"
        "\n"
        "let SchedModel = HaydnSchedModel in {\n\n"
        + "\n".join(
            f"def {res_of[u]} : ProcResource<1>;" for u in surf.units
        )
        + "\n\n"
        + "\n".join(group_defs)
        + "\n\n"
        + "}\n\n"
        "let SchedModel = HaydnSchedModel in {\n\n"
        + "\n".join(write_res)
        + "\n\n"
        + "\n".join(itin_rw_lines)
        + "\n\n} // SchedModel = HaydnSchedModel\n"
    )
    content = (
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
        "// Fixed Data_Latency=2 classes (2026-08-21 latency P0/P1): logical\n"
        "// menus + per-slot member rows, SinCosLat pattern with latency 2.\n"
        "def Slot12_ALU_DspLat : InstrItinClass;\n"
        "def Slot1_ALU_DspLat : InstrItinClass;\n"
        "def Slot2_ALU_DspLat : InstrItinClass;\n"
        "def Slot012_ALU_CsrLat : InstrItinClass;\n"
        "def Slot0_ALU_CsrLat : InstrItinClass;\n"
        "def Slot1_ALU_CsrLat : InstrItinClass;\n"
        "def Slot2_ALU_CsrLat : InstrItinClass;\n"
        "// Golden Data_Latency=1 classes (2026-08-21 latency P3): store\n"
        "// writeback register + fresh-dest multiplies. Same ownership split\n"
        "// as DspLat/CsrLat — class defs and InstrItinData rows live here.\n"
        "def Slot0_LS_WbLat : InstrItinClass;\n"
        "def Slot12_MAC_MulLat : InstrItinClass;\n"
        "def Slot1_MAC_MulLat : InstrItinClass;\n"
        "def Slot2_MAC_MulLat : InstrItinClass;\n"
        "\n"
        f"def HaydnItineraries : ProcessorItineraries<\n"
        f"  [{fu}],\n"
        f"  [],\n"
        f"  [\n"
        f"{body}\n"
        f"  ]>;\n"
    )
    return content, bridge


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
        "// Published Slot0_LS / Slot1_LD / Slot01_LD. first=0\n"
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


def emit_per_op_resources_inc(
    rows: Sequence[PerOpResourceRow],
    uncovered: Sequence[str],
    surf: GoldenLatencySurface,
) -> str:
    """C++ HaydnAdmittedPerOpResourceRecord table + uncovered census.

    Consumers include this from HaydnPortModel.h under
    GET_HAYDN_PER_OP_RESOURCES. Lookup stays per-name (switch on opcode
    enum) so admission is exactly the covered set; the census of
    compiler-reachable names without golden rows is a pin, not a claim.
    """
    banner = "\n".join(generated_banner(
        generator=SCHED_GENERATOR, family=get_family("e96")))
    table_entries = []
    for r in rows:
        # Positional init in HaydnAdmittedPerOpResourceRecord declaration
        # order (C++17 build; designated initializers need C++20):
        # Opcode, 8 port fields, UnitMask, DataLatency, OperandCycleCount,
        # OperandCycles, PipelineOccupancy, RequiredAlignment.
        cyc = ", ".join(str(c) for c in r.operand_cycles)
        cyc_arr = "{" + (f"{cyc}" if cyc else "") + "}"
        counts = ", ".join(str(c) for c in r.ports)
        lat_lit = "0" if r.latency is None else str(r.latency)
        table_entries.append(
            f"    {{ // {r.name} ({r.fmt}) units=0x{r.unit_mask:02X}\n"
            f"        Haydn::{r.name}, {counts}, 0x{r.unit_mask:02X}u,\n"
            f"        {lat_lit}u, {len(r.operand_cycles)}u, {cyc_arr},\n"
            f"        1u, {r.alignment}u,\n"
            f"    }},"
        )
    switch_cases = []
    for idx, r in enumerate(rows):
        switch_cases.append(
            f"  case Haydn::{r.name}:\n    return &Table[{idx}];"
        )
    census_lines = "\n".join(f"    \"{n}\"," for n in uncovered)
    return (
        "//===-- HaydnGenPerOpResources.inc - per-op resource import -*- C++ -*-===//\n"
        "//\n"
        f"{banner}\n"
        "//\n"
        "// M18 import from golden instruction_type_index.json: one\n"
        "// HaydnAdmittedPerOpResourceRecord per compiler-reachable logical\n"
        "// with a golden row. UnitMask bits are the seven shared units\n"
        "// (LOADSTORE0, LOAD1, ALU0, ALU1, ALU2, MAC0, MAC1); port counts\n"
        "// are per-bank golden port lists; DataLatency is golden\n"
        "// Pipeline_Info (0 = golden silent:\n"
        "// stores/branches/hints publish no latency); conservative bound\n"
        "// (uimm4+2 dest) rides OperandCycles like the published SinCosLat\n"
        "// class. PipelineOccupancy=1 is the same product InstrStage<1,...>\n"
        "// law the itineraries publish. CompleteModel stays 0: the census\n"
        "// below is the explicit uncovered set (GE96-04).\n"
        "//\n"
        "//===----------------------------------------------------------------------===//\n"
        "\n"
        "#ifdef GET_HAYDN_PER_OP_RESOURCES\n"
        "#undef GET_HAYDN_PER_OP_RESOURCES\n"
        "\n"
        "static_assert(HAYDN_ADMITTED_OPERAND_CYCLE_ROOM ==\n"
        f"                  {ADMITTED_OPERAND_CYCLE_ROOM}u,\n"
        "              \"generator/C++ OperandCycles room drift\");\n"
        "\n"
        "static constexpr HaydnAdmittedPerOpResourceRecord Table[] = {\n"
        + "\n".join(table_entries)
        + "\n};\n"
        "\n"
        "static constexpr unsigned HaydnAdmittedPerOpRecordCount =\n"
        f"    sizeof(Table) / sizeof(Table[0]);\n"
        "static_assert(HaydnAdmittedPerOpRecordCount > 0u,\n"
        "              \"empty per-op import is not an import\");\n"
        "\n"
        "inline const HaydnAdmittedPerOpResourceRecord *\n"
        "haydnGetAdmittedPerOpResourceRecord(unsigned Opcode) {\n"
        "  switch (Opcode) {\n"
        "  default:\n"
        "    return nullptr;\n"
        + "\n".join(switch_cases)
        + "\n  }\n"
        "}\n"
        "\n"
        "// Compiler-reachable logicals WITHOUT a golden row (fail-closed\n"
        "// census; never synthesized). CompleteModel=0 evidence.\n"
        "static constexpr const char *HaydnPerOpUncoveredNames[] = {\n"
        f"{census_lines}\n"
        "};\n"
        "static constexpr unsigned HaydnPerOpUncoveredCount =\n"
        "    sizeof(HaydnPerOpUncoveredNames) /\n"
        "    sizeof(HaydnPerOpUncoveredNames[0]);\n"
        "\n"
        "#endif // GET_HAYDN_PER_OP_RESOURCES\n"
    )


def prove_per_op_import(
    content: str, rows: Sequence[PerOpResourceRow]
) -> None:
    """The import must carry representative rows and pin the census."""
    if "GET_HAYDN_PER_OP_RESOURCES" not in content:
        raise SystemExit("error: per-op import guard missing")
    if "haydnGetAdmittedPerOpResourceRecord" not in content:
        raise SystemExit("error: per-op lookup missing")
    if "case Haydn::ADD32:" not in content:
        raise SystemExit("error: ADD32 per-op case missing")
    if "case Haydn::CSRR:" not in content:
        raise SystemExit("error: CSRR per-op case missing")
    if "HaydnPerOpUncoveredNames" not in content:
        raise SystemExit("error: uncovered census missing")
    if "CompleteModel = 1" in content or "CompleteModel=1" in content:
        raise SystemExit("error: per-op import must not set CompleteModel=1")
    names = [r.name for r in rows]
    if len(names) != len(set(names)):
        dupes = sorted({n for n in names if names.count(n) > 1})
        raise SystemExit(f"error: duplicate per-op rows {dupes}")


def prove_mca_bridge(bridge: str, rows: Sequence[PublishedItin]) -> None:
    """The ItinRW bridge covers every published class with one WriteRes."""
    if "let SchedModel = HaydnSchedModel in {" not in bridge:
        raise SystemExit("error: bridge missing SchedModel binding")
    if "ProcResource<1>;" not in bridge:
        raise SystemExit("error: bridge missing unit ProcResources")
    for row in rows:
        if f"HW_{row.name}" not in bridge:
            raise SystemExit(
                f"error: bridge missing SchedWriteRes for {row.name}"
            )
        lat = max(row.operand_cycles) if row.operand_cycles else 1
        # Every class WriteRes must carry its published max latency.
        if f"let Latency = {lat}; }}" not in bridge:
            raise SystemExit(
                f"error: bridge missing WriteRes latency {lat} for {row.name}"
            )
    itin_count = bridge.count("def : ItinRW<")
    if itin_count == 0:
        raise SystemExit("error: bridge has no ItinRW rows")
    covered = sum(
        1 for row in rows if f"[{row.name}]" in bridge or f"\n  [{row.name}]" in bridge or f"[{row.name}," in bridge or f",\n  {row.name}" in bridge or f" {row.name}," in bridge or f" {row.name}\n" in bridge
    )
    if covered != len(rows):
        missing = [r.name for r in rows if f"{r.name}" not in bridge]
        raise SystemExit(
            f"error: bridge ItinRW covers {covered}/{len(rows)} classes "
            f"(missing {missing[:6]})"
        )
    if "CompleteModel" in bridge:
        raise SystemExit("error: bridge must not touch CompleteModel")


def prove_no_dead_classes(content: str) -> None:
    for name in DEAD_ACC_LAT_CLASS_NAMES:
        # Word-boundary match: "Slot12_ALU" must not fire on the live
        # "Slot12_ALU_DspLat" / "Slot12_ALU_SinCosLat" names.
        if re.search(rf"\b{re.escape(name)}\b(?![\w])", content):
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
    mutated, _mut_bridge = emit_sched_records_inc(mut_rows, mut)
    if mutated == content:
        raise SystemExit(
            "P19 source-mutation: sincos flip reused committed sched records"
        )
    restored_rows = published_itineraries(surf)
    restored, _bridge = emit_sched_records_inc(restored_rows, surf)
    second, _bridge2 = emit_sched_records_inc(restored_rows, surf)
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
    ap.add_argument("--index", type=Path, default=None)
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
    index_path: Path = args.index or (golden / family.index_filename)

    for path, label in (
        (json_path, "golden JSON"),
        (xlsx_path, "golden XLSX"),
        (canonical_path, "canonical vectors"),
        (constraints_path, "Constraints.md"),
        (index_path, "instruction type index"),
    ):
        if not path.is_file():
            print(f"error: {label} not found: {path}", file=sys.stderr)
            return 2

    try:
        verify_golden_hashes(
            golden, json_path, xlsx_path, canonical_path, constraints_path
        )
        verify_authority_inputs(golden, [index_path.name])
        index_sha = sha256_file(index_path)
        if index_sha != PINNED_INDEX_SHA256:
            raise SystemExit(
                f"error: index sha256 {index_sha} != pinned "
                f"{PINNED_INDEX_SHA256}"
            )
        verify_golden_inputs_pin(golden_inputs_pin_path())
        check_cutover_surfaces(args.out_dir)
        surf = parse_constraints(constraints_path)
        rows = published_itineraries(surf)
        validate_published(rows, surf)
        content, bridge_content = emit_sched_records_inc(rows, surf)
        mem_content = emit_memory_cycles_inc(rows)
        prove_no_dead_classes(content)
        prove_no_dead_classes(mem_content)
        prove_generated_entry_capacities(content)
        prove_family_sched_records(mem_content)
        prove_mca_bridge(bridge_content, rows)
        index = load_golden_index(index_path)
        member_fams, hand = compiler_reachable_logicals(args.out_dir)
        per_op_rows, uncovered = build_per_op_rows(
            index, member_fams, hand, surf
        )
        per_op_content = emit_per_op_resources_inc(
            per_op_rows, uncovered, surf
        )
        prove_per_op_import(per_op_content, per_op_rows)
        prove_no_dead_classes(per_op_content)
    except SystemExit as exc:
        msg = str(exc)
        if msg:
            print(msg, file=sys.stderr)
        return 2 if msg else 0

    out_path = args.out_dir / family.sched_records_inc
    mem_path = args.out_dir / family.memory_cycles_inc
    per_op_path = args.out_dir / "HaydnGenPerOpResources.inc"
    bridge_path = args.out_dir / "HaydnGenSchedMcaBridge.td.inc"
    targets = {
        out_path: content,
        mem_path: mem_content,
        per_op_path: per_op_content,
        bridge_path: bridge_content,
    }

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
        print(
            "OK per-op resources "
            f"covered={len(per_op_rows)} uncovered={len(uncovered)} "
            "complete_model=0"
        )
        return 0

    args.out_dir.mkdir(parents=True, exist_ok=True)
    out_path.write_text(content, encoding="utf-8")
    mem_path.write_text(mem_content, encoding="utf-8")
    per_op_path.write_text(per_op_content, encoding="utf-8")
    bridge_path.write_text(bridge_content, encoding="utf-8")
    print(f"wrote {out_path}")
    print(f"wrote {mem_path}")
    print(f"wrote {per_op_path}")
    print(f"wrote {bridge_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
