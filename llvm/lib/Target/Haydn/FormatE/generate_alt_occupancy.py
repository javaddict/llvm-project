#!/usr/bin/env python3
"""Emit HaydnGenAltOccupancy.inc from residual occupancy sources.

Occupancy Mask is residual SLOT0/1/2 legality, not Format E EntryIdx
(ADD64/X4CMUL16/LD32 would gain e0/e2). Fallback is 0: Format E members
fill residual indices via formatEMemberAtResidualIndex.

HaydnGenFormats.inc AlternateInsts is MultiSlot_Pseudo only after
LogicalMaterialize retirement, so it is not the row universe (one
ADD32_MSP row). Every emitted mask has exactly one of three sources:

1. RESIDUAL_OCCUPANCY_EXCEPTIONS: one explicit table; each entry cites
   either a golden instruction_type_index.json Instruction row (with the
   Available unit set recorded in the entry and re-verified at every
   generation) or a named golden fact (NOP: the admitted architectural
   idle parcel has no index row by design).
2. ITIN_CLASS_BASE: itinerary-class base mask, used only when the row's
   golden Available (directly or via SHELL_TO_GOLDEN) exactly equals the
   class unit set. X2*/X4* ALU rows are golden ALU0|ALU1|ALU2 so class
   base 0x7 is the golden answer; MAC-family X2*/X4* stay 0x6.
3. The generated HaydnGenFormats.inc AlternateInsts member mask
   (ADD32_MSP is also table-cited because it carries no golden row of
   its own name).

Fail-closed rule: a row on a unit-bearing itinerary (Slot012_ALU*,
Slot12_*, Slot0_*, Slot0_LS*, Slot01_LD, Slot1_) that is neither in the
table nor golden-backed with the class unit set raises — residual
occupancy is never derived from an instruction-name substring.

  python3 generate_alt_occupancy.py [--family e96] [--out PATH] [--check]
      [--haydn-dir DIR] [--formats HaydnGenFormats.inc]
      [--index instruction_type_index.json]

Peer: AIE AIEMCFormats.h getAlternateInstsOpcode is TableGen
MultiSlot only; Haydn overlays residual occupancy because EncodedBytes
is not a slot bit.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

from family_core import (
    PINNED_INDEX_SHA256,
    add_family_argument,
    generated_banner,
    get_family,
    golden_inputs_pin_path,
    resolve_golden_dir,
    sha256_file,
    verify_authority_inputs,
    verify_golden_inputs_pin,
)
from generate_sched_records import (
    SHELL_TO_GOLDEN,
    load_golden_index,
    parse_index_units,
)

GENERATOR = "llvm/lib/Target/Haydn/FormatE/generate_alt_occupancy.py"
MIN_OCCUPANCY_ROWS = 715
ALU_TRIPLE = frozenset({"ALU0", "ALU1", "ALU2"})
MAC_PAIR = frozenset({"MAC0", "MAC1"})
INSTR_NAME_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
MEMBER_RE = re.compile(r"_E[23]_")
SUFFIX_RE = re.compile(r"_S[012]$")
COMPILER_LS_RE = re.compile(r"^(?:LD|LDU|ST)\d+(?:_REG_M0S0LS|_POST)?$")

# Golden citation for one exception entry. A "golden" citation records the
# exact index key and the Available unit set the entry was admitted under;
# both are re-verified at every generation (fail-closed on absence or unit
# drift). A "named-fact" citation records a stated golden admission that no
# index row can carry (NOP is the idle parcel, not an instruction).
GoldenCite = Tuple[str, Tuple[str, ...]]  # (index key, Available units)
NamedCite = Tuple[str, str]  # (fact name, fact statement)
Citation = Tuple[str, GoldenCite | NamedCite]

# One explicit residual-row exception table. Entry: name -> (mask, citation).
# Sources (each verified against instruction_type_index.json 2026-08-30):
#  - golden <KEY> <units>: index row KEY has Available == <units>.
#  - named-fact: the cited golden admission; no index row exists.
RESIDUAL_OCCUPANCY_EXCEPTIONS: Dict[str, Tuple[int, Citation]] = {
    # --- named golden facts (no index row can carry these) ---
    # Architectural idle parcel: the complete generated NOP packet is the
    # admitted executable idle object; there is no instruction row.
    "NOP": (0x1, ("named-fact", ("idle-parcel", "admitted architectural idle parcel; no instruction_type_index row exists"))),
    # Golden placeholder row for an unpublished op; cited by explicit key
    # only here (SHELL_TO_GOLDEN must stay untouched).
    "WFI": (0x1, ("golden", ("WFI<TBD>", ("ALU0",)))),
    # --- DR-domain Slot012_ALU residual S1|S2 family (51 rows) ---
    # All have golden Available ALU0|ALU1|ALU2; residual occupancy keeps
    # S1|S2 (0x6) — Mask is not Format E EntryIdx, and golden Available
    # here is ALU availability, not residual slot legality.
    **{
        name: (0x6, ("golden", (name, ("ALU0", "ALU1", "ALU2"))))
        for name in (
            # Scalar DR/SFR ALU shells kept at residual S1|S2 after the
            # Slot012 rematch (formerly a named set).
            "ZERO_DR",
            "ZERO_SFR",
            "MOVEGPR2SFR",
            "MOVESFR2GPR",
            "NSA16_L",
            "NSA32_L",
            "NSAZ16_L",
            "NSAZ32_L",
            # *64 Slot012_ALU shells enumerated explicitly (formerly a
            # name-substring arm)
            "ABS64",
            "ABS64S",
            "ADD64",
            "ADD64S",
            "ADD64S_H",
            "ADD64S_L",
            "ADD64_H",
            "ADD64_L",
            "AND64",
            "MAX64",
            "MIN64",
            "MOVE64",
            "MOVF64",
            "MOVT64",
            "NEG64",
            "NEG64S",
            "NOT64",
            "NSA64",
            "NSAZ64",
            "OR64",
            "POPCOUNT64",
            "SEQ64",
            "SLE64",
            "SLL64",
            "SLLI64",
            "SLT64",
            "SRA64",
            "SRA64R",
            "SRAI64",
            "SRAI64R",
            "SRL64",
            "SRLI64",
            "SUB64",
            "SUB64S",
            "SUB64S_H",
            "SUB64S_L",
            "SUB64_H",
            "SUB64_L",
            "TRANSF64",
            "TRANSF64F2",
            "TRANSF64_H",
            "TRANSF64_L",
            "XOR64",
        )
    },
    # --- golden ALU-triple rows with SLOT0-pinned residual ---
    "SEXT32T64": (0x7, ("golden", ("SEXT32T64", ("ALU0", "ALU1", "ALU2")))),
    "SEXT_GPR32_TO_DR64": (
        0x2,
        (
            "golden",
            (
                "SEXT32T64",
                ("ALU0", "ALU1", "ALU2"),
            ),
        ),
    ),  # FieldSlot-retired GISel encode form: e1 peel to SEXT32T64.
    "ADD32_MSP": (0x7, ("golden", ("ADD32", ("ALU0", "ALU1", "ALU2")))),
    # --- residual slot-0 control/imm/branch/CSR shells ---
    # These itineraries are Slot012_ALU after the golden unit re-map, but
    # golden Available is ALU0-only (branches/JAL/SET_HWLOOP) or the op is
    # the CsrLat control shell; residual stays SLOT0.
    "CSRR": (0x1, ("golden", ("CSRR", ("ALU0", "ALU1", "ALU2")))),
    "CSRW": (0x1, ("golden", ("CSRW", ("ALU0", "ALU1", "ALU2")))),
    "CSRW_W": (0x1, ("golden", ("CSRW", ("ALU0", "ALU1", "ALU2")))),
    "LUI": (0x1, ("golden", ("LUI", ("ALU0", "ALU1", "ALU2")))),
    "MOVEI_H": (0x1, ("golden", ("MOVEI_H", ("ALU0", "ALU1", "ALU2")))),
    "MOVEI_L": (0x1, ("golden", ("MOVEI_L", ("ALU0", "ALU1", "ALU2")))),
    "ADDI32_W": (0x1, ("golden", ("ADDI32", ("ALU0", "ALU1", "ALU2")))),
    "ORI32_W": (0x1, ("golden", ("ORI32", ("ALU0", "ALU1", "ALU2")))),
    **{
        name: (0x1, ("golden", (name[:-2], ("ALU0",))))
        for name in (
            "BEQZ_W",
            "BEQ_W",
            "BGEU_W",
            "BGEZ_W",
            "BGE_W",
            "BLTU_W",
            "BLTZ_W",
            "BLT_W",
            "BNEZ_W",
            "BNE_W",
            "JALR_W",
            "JAL_W",
            "SET_HWLOOP_F2_W",
            "SET_HWLOOP_W",
        )
    },
    # --- compiler LS shells: explicit, itinerary is not sufficient ---
    # Catalog rows and compiler shells sit on the SAME itineraries with
    # different residual masks, so each shell is its own cited entry.
    # Width-by-width loads map to S_/D_ family golden rows (same mapping
    # SHELL_TO_GOLDEN admits); loads Available=(LOADSTORE0, LOAD1),
    # stores Available=(LOADSTORE0,).
    "LD8": (0x1, ("golden", ("S_LBS_WITH_IMM", ("LOADSTORE0", "LOAD1")))),
    "LDU8": (0x1, ("golden", ("S_LBU_WITH_IMM", ("LOADSTORE0", "LOAD1")))),
    "LD16": (0x1, ("golden", ("S_LHWS_WITH_IMM", ("LOADSTORE0", "LOAD1")))),
    "LDU16": (0x1, ("golden", ("S_LHWU_WITH_IMM", ("LOADSTORE0", "LOAD1")))),
    "LD32": (0x3, ("golden", ("S_LW_WITH_IMM", ("LOADSTORE0", "LOAD1")))),
    "LD64": (0x3, ("golden", ("D_LDW_WITH_IMM", ("LOADSTORE0", "LOAD1")))),
    "ST8": (0x1, ("golden", ("S_SB_WITH_IMM", ("LOADSTORE0",)))),
    "ST16": (0x1, ("golden", ("S_SHW_WITH_IMM", ("LOADSTORE0",)))),
    "ST32": (0x1, ("golden", ("S_SW_WITH_IMM", ("LOADSTORE0",)))),
    "ST64": (0x1, ("golden", ("D_SDW_WITH_IMM", ("LOADSTORE0",)))),
    "LD32_REG_M0S0LS": (
        0x1,
        ("golden", ("S_LW_WITH_REG", ("LOADSTORE0", "LOAD1"))),
    ),
    "LD64_REG_M0S0LS": (
        0x1,
        ("golden", ("D_LDW_WITH_REG", ("LOADSTORE0", "LOAD1"))),
    ),
    "ST32_REG_M0S0LS": (0x1, ("golden", ("S_SW_WITH_REG", ("LOADSTORE0",)))),
    "ST64_REG_M0S0LS": (0x1, ("golden", ("D_SDW_WITH_REG", ("LOADSTORE0",)))),
    "LD32_POST": (0x2, ("golden", ("S_LW_POST_IMM", ("LOADSTORE0", "LOAD1")))),
    "LD64_POST": (
        0x2,
        ("golden", ("D_LDW_POST_IMM", ("LOADSTORE0", "LOAD1"))),
    ),
    "ST32_POST": (0x2, ("golden", ("S_SW_POST_IMM", ("LOADSTORE0",)))),
    "ST64_POST": (0x2, ("golden", ("D_SDW_POST_IMM", ("LOADSTORE0",)))),
}

# Itinerary class base masks: prefix -> (expected golden Available unit
# set, mask). A row on the class takes the base mask ONLY when its golden
# Available equals the class unit set (per-row verification, fail-closed
# otherwise). Slot12_* classes expect MAC0|MAC1 or ALU1|ALU2; Slot012_ALU
# expects the ALU triple; Slot0_*/Slot0_LS*/Slot01_LD/Slot1_ as below.
ITIN_CLASS_BASE: Tuple[Tuple[str, Tuple[str, ...], int], ...] = (
    ("Slot012_ALU", ("ALU0", "ALU1", "ALU2"), 0x7),
    ("Slot12_MAC", ("MAC0", "MAC1"), 0x6),
    ("Slot12_ALU_SinCosLat", ("ALU1", "ALU2"), 0x6),
    ("Slot12_ALU_DspLat", ("ALU1", "ALU2"), 0x6),
    ("Slot12_ALU", ("ALU1", "ALU2"), 0x6),
    ("Slot0_ALU", ("ALU0",), 0x1),
    ("Slot0_LS_WbLat", ("LOADSTORE0",), 0x7),
    ("Slot0_LS", ("LOADSTORE0",), 0x7),
    ("Slot01_LD", ("LOADSTORE0", "LOAD1"), 0x7),
    ("Slot1_LD", ("LOADSTORE0", "LOAD1"), 0x2),
    ("Slot0_", ("ALU0",), 0x1),
    ("Slot1_", (), 0x2),
    ("Slot2_", (), 0x4),
)

ARRAY_RE = re.compile(
    r"// Haydn::(\w+) \((?:LogicalMaterialize|MultiSlot_Pseudo)[^)]*\)[^\n]*\n"
    r"\s*\{\s*([^}]+)\}",
    re.M,
)
CASE_RE = re.compile(
    r"case Haydn::(\w+):\s*\n\s*return &AlternateInsts\[(\d+)\];",
    re.M,
)
TOKEN_RE = re.compile(
    r"Itinerary\s*=\s*(Slot\w+|NoItinerary)(\s+in\b)?"
    r"|\b(class|def)\s+([A-Za-z_][A-Za-z0-9_]*)\b"
    r"|([{}])"
)


def strip_td_comments(text: str) -> str:
    """Strip // first so comments that mention *_S1/*_S2 are not block comments."""
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    # TSFlags{2-0} / Inst{31, 30} are not brace blocks.
    return re.sub(r"(\w+)\{[^}]*\}", r"\1", text)


def skip_angles(text: str, i: int) -> int:
    if i >= len(text) or text[i] != "<":
        return i
    depth = 0
    while i < len(text):
        if text[i] == "<":
            depth += 1
        elif text[i] == ">":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def parse_td_itineraries(
    path: Path, *, collect_defs: bool
) -> Tuple[Dict[str, Optional[str]], Dict[str, Optional[str]], Dict[Tuple[str, str], str]]:
    """Return (defs, classes, super_of) itinerary maps from one TableGen file."""
    text = strip_td_comments(path.read_text(encoding="utf-8"))
    defs: Dict[str, Optional[str]] = {}
    classes: Dict[str, Optional[str]] = {}
    super_of: Dict[Tuple[str, str], str] = {}
    stack: Dict[int, Optional[str]] = {0: None}
    depth = 0
    current: Optional[str] = None
    current_kind: Optional[str] = None
    current_depth: Optional[int] = None
    pending_in: Optional[str] = None
    pending_block: Optional[str] = None
    for m in TOKEN_RE.finditer(text):
        itin, in_kw, kind, name, brace = (
            m.group(1),
            m.group(2),
            m.group(3),
            m.group(4),
            m.group(5),
        )
        if brace == "{":
            depth += 1
            if pending_block is not None:
                stack[depth] = pending_block
                pending_block = None
                pending_in = None
            elif depth not in stack:
                stack[depth] = stack.get(depth - 1)
            continue
        if brace == "}":
            stack.pop(depth, None)
            if current_depth is not None and depth == current_depth:
                current = None
                current_kind = None
                current_depth = None
            depth = max(depth - 1, 0)
            continue
        if itin:
            val: Optional[str] = None if itin == "NoItinerary" else itin
            if in_kw:
                pending_block = val
                pending_in = val
            else:
                if current is not None:
                    target = classes if current_kind == "class" else defs
                    target[current] = val
                stack[depth] = val
            continue
        if kind:
            inherit = pending_in
            if inherit is None:
                for d in range(depth, -1, -1):
                    if stack.get(d):
                        inherit = stack[d]
                        break
            if pending_in is not None and kind == "def":
                pending_in = None
                pending_block = None
            current = name
            current_kind = kind
            current_depth = depth
            if kind == "class":
                classes[name] = inherit
            elif collect_defs:
                defs[name] = inherit
            j = skip_angles(text, m.end())
            sm = re.match(r"\s*:\s*([A-Za-z_][A-Za-z0-9_]*)", text[j:])
            if sm:
                super_of[(kind, name)] = sm.group(1)
    return defs, classes, super_of


def resolve_itinerary(
    name: str,
    defs: Dict[str, Optional[str]],
    classes: Dict[str, Optional[str]],
    super_of: Dict[Tuple[str, str], str],
) -> Optional[str]:
    itin = defs.get(name)
    if itin:
        return itin
    cls = super_of.get(("def", name))
    seen: set = set()
    while cls and cls not in seen:
        seen.add(cls)
        if classes.get(cls):
            return classes[cls]
        cls = super_of.get(("class", cls))
    return itin


def load_logical_itineraries(haydn_dir: Path) -> Dict[str, str]:
    """Compiler-reachable logical name → Slot* itinerary class."""
    class_files = (
        "HaydnInstrFormats.td",
        "HaydnInstrFormatsC.td",
    )
    def_files = (
        "HaydnInstrInfo.td",
        "HaydnInstrInfoGolden.td.inc",
        "HaydnMultiSlotPseudo.td",
    )
    defs: Dict[str, Optional[str]] = {}
    classes: Dict[str, Optional[str]] = {}
    super_of: Dict[Tuple[str, str], str] = {}
    for fn in class_files:
        path = haydn_dir / fn
        if not path.is_file():
            raise SystemExit(f"error: itinerary class file missing: {path}")
        _d, c, s = parse_td_itineraries(path, collect_defs=False)
        classes.update(c)
        super_of.update(s)
    for fn in def_files:
        path = haydn_dir / fn
        if not path.is_file():
            raise SystemExit(f"error: occupancy def file missing: {path}")
        d, c, s = parse_td_itineraries(path, collect_defs=True)
        defs.update(d)
        classes.update(c)
        super_of.update(s)
    out: Dict[str, str] = {}
    for name in defs:
        if not INSTR_NAME_RE.match(name):
            continue
        if MEMBER_RE.search(name) or SUFFIX_RE.search(name):
            continue
        itin = resolve_itinerary(name, defs, classes, super_of)
        if not itin:
            continue
        out[name] = itin
    return out


def parse_members(body: str) -> list[str]:
    parts = [p.strip() for p in body.split(",")]
    if len(parts) != 3:
        raise SystemExit(f"expected 3 members, got {parts!r}")
    return parts


def mask_of_members(members: list[str]) -> int:
    bits = 0
    for i, m in enumerate(members):
        if m != "0":
            bits |= 1 << i
    return bits


def parse_alternate_insts(formats: Path) -> Dict[str, int]:
    """Optional MultiSlot_Pseudo rows from HaydnGenFormats.inc."""
    text = formats.read_text(encoding="utf-8")
    start = text.find("static std::vector<unsigned int> const AlternateInsts[]")
    if start < 0:
        raise SystemExit("AlternateInsts table not found")
    func = text.find("HaydnMCFormats::getAlternateInstsOpcode", start)
    if func < 0:
        raise SystemExit("getAlternateInstsOpcode not found")
    table = text[start:func]
    switch = text[func : text.find("#endif // GET_ALTERNATE_INST_OPCODE_FUNC", func)]
    rows: list[tuple[str, list[str]]] = []
    for m in ARRAY_RE.finditer(table):
        rows.append((m.group(1), parse_members(m.group(2))))
    cases = [(m.group(1), int(m.group(2))) for m in CASE_RE.finditer(switch)]
    if not rows or not cases:
        return {}
    if len(rows) != len(cases):
        # LogicalMaterialize retirement leaves MultiSlot_Pseudo only.
        # Do not treat a 1-row table as the occupancy universe.
        return {name: mask_of_members(members) for name, members in rows}
    for name, idx in cases:
        if idx >= len(rows):
            raise SystemExit(f"{name} index {idx} out of range")
        if rows[idx][0] != name:
            raise SystemExit(f"{name} comment is {rows[idx][0]}")
    return {name: mask_of_members(members) for name, members in rows}


def golden_units_for(
    name: str, index: Dict[str, tuple]
) -> Tuple[str, ...]:
    key = SHELL_TO_GOLDEN.get(name, name).upper()
    rec = index.get(key)
    if rec is None:
        return ()
    _fmt, row = rec
    return parse_index_units(row.get("Available"))


def verify_exception_citation(
    name: str, mask: int, citation: Citation, index: Dict[str, tuple]
) -> None:
    """Fail closed if a golden-citing entry no longer resolves or drifted.

    Each "golden" citation records the index key and the Available unit
    set that admitted the entry. A missing key or a changed unit set is a
    golden-input change that must be re-admitted by hand, not silently
    absorbed. "named-fact" citations carry no index row (NOP idle parcel).
    """
    kind = citation[0]
    if kind == "named-fact":
        return
    key, expected = citation[1]
    rec = index.get(key.upper())
    if rec is None:
        raise SystemExit(
            f"error: occupancy exception {name} cites golden row "
            f"{key!r} which is absent from instruction_type_index.json"
        )
    got = parse_index_units(rec[1].get("Available"))
    if tuple(got) != tuple(expected):
        raise SystemExit(
            f"error: occupancy exception {name} cites golden row {key!r} "
            f"Available {list(got)} but was admitted under {list(expected)}; "
            "re-admit the entry against the new golden fact"
        )
    _ = mask


def itin_class_base(name: str, itin: str, index: Dict[str, tuple]) -> int:
    """Class base mask with per-row golden Available verification.

    The class base is admitted only when the row's golden Available
    exactly equals the class unit set; anything else is an exception-row
    question, never a guess. Slot1_/Slot2_ classes carry no unit
    expectation (pure slot classes) and verify no golden row.
    """
    for prefix, expected_units, mask in ITIN_CLASS_BASE:
        if itin.startswith(prefix):
            units = golden_units_for(name, index)
            if tuple(units) != tuple(expected_units):
                exc = RESIDUAL_OCCUPANCY_EXCEPTIONS.get(name)
                if exc is not None:
                    return exc[0]
                if expected_units:
                    raise SystemExit(
                        f"error: {name} itinerary {itin} has no golden "
                        f"Available {list(expected_units)} row (residual "
                        "occupancy is not name-derivable)"
                    )
                raise SystemExit(
                    f"error: {name} itinerary {itin} carries golden "
                    f"Available {list(units)} but the class admits no unit "
                    "set (residual occupancy is not name-derivable)"
                )
            return mask
    raise SystemExit(f"error: unmapped itinerary {itin} for {name}")


def residual_mask(name: str, itin: str, index: Dict[str, tuple]) -> int:
    """Residual SLOT occupancy: exception table first, else class base."""
    exc = RESIDUAL_OCCUPANCY_EXCEPTIONS.get(name)
    if exc is not None:
        mask, citation = exc
        verify_exception_citation(name, mask, citation, index)
        return mask
    return itin_class_base(name, itin, index)


def collect_rows(
    haydn_dir: Path,
    index: Dict[str, tuple],
    formats: Optional[Path],
) -> List[Tuple[str, int]]:
    itins = load_logical_itineraries(haydn_dir)
    masks: Dict[str, int] = {}
    for name, itin in itins.items():
        masks[name] = residual_mask(name, itin, index)
    if formats is not None and formats.is_file():
        for name, alt_mask in parse_alternate_insts(formats).items():
            if name not in masks:
                exc = RESIDUAL_OCCUPANCY_EXCEPTIONS.get(name)
                if exc is not None:
                    mask, citation = exc
                    verify_exception_citation(name, mask, citation, index)
                    masks[name] = mask
                else:
                    # AlternateInsts member mask (ADD32_MSP is table-cited;
                    # any future MultiSlot row carries its own members).
                    masks[name] = alt_mask
    if len(masks) < MIN_OCCUPANCY_ROWS:
        raise SystemExit(
            f"error: occupancy parse drift: {len(masks)} rows "
            f"(expected >={MIN_OCCUPANCY_ROWS}; AlternateInsts-only is not "
            "the residual universe)"
        )
    msp = sorted(n for n in masks if n.endswith("_MSP"))
    rest = sorted(n for n in masks if not n.endswith("_MSP"))
    return [(name, masks[name]) for name in msp + rest]


def emit_occupancy_inc(
    rows: Sequence[Tuple[str, int]],
    family,
    index_sha: str,
) -> str:
    banner = generated_banner(generator=GENERATOR, family=family)
    lines = [
        "//===-- HaydnGenAltOccupancy.inc -*- C++ -*-===//",
        "//",
        *banner,
        f"// Index-SHA256: {index_sha}",
        "//",
        "// Residual SLOT0/1/2 occupancy for compiler-reachable logicals.",
        "// Mask bit i is residual occupancy class i. Fallback is 0;",
        "// Format E members fill residual indices.",
        "// Do not derive Mask from Format E EntryIdx.",
        "// X2*/X4* ALU masks follow golden Available ALU0|ALU1|ALU2 (0x7);",
        "// MAC-family X2*/X4* stay MAC0|MAC1 (0x6).",
        "// SEXT32T64 residual is 0x7 (golden ALU0|ALU1|ALU2); other *64",
        "// Slot012_ALU shells keep residual S1|S2 (0x6).",
        "//",
        "//===----------------------------------------------------------------------===//",
        "",
        "#ifdef GET_HAYDN_ALT_OCCUPANCY",
        "#undef GET_HAYDN_ALT_OCCUPANCY",
        "",
        "struct HaydnAltOccupancyRow {",
        "  unsigned LogicalOpc;",
        "  uint8_t Mask;",
        "  unsigned Fallback[3];",
        "};",
        "",
        "static const HaydnAltOccupancyRow HaydnAltOccupancy[] = {",
    ]
    for name, mask in rows:
        lines.append(
            f"    {{ Haydn::{name}, {mask:#x}, {{ 0, 0, 0 }} }},"
        )
    lines.append("};")
    lines.append("")
    lines.append("static int haydnAltOccupancyIndex(unsigned Opcode) {")
    lines.append("  switch (Opcode) {")
    lines.append("  default:")
    lines.append("    return -1;")
    for i, (name, _) in enumerate(rows):
        lines.append(f"  case Haydn::{name}:")
        lines.append(f"    return {i};")
    lines.append("  }")
    lines.append("}")
    lines.append("#endif // GET_HAYDN_ALT_OCCUPANCY")
    lines.append("")
    return "\n".join(lines)


PRODUCT_LS_MASKS = {
    "LD32": 0x3,
    "LD64": 0x3,
    "ST32": 0x1,
    "ST64": 0x1,
}


def prove_sext32t64_residual_mask(
    rows: Sequence[Tuple[str, int]], index: Dict[str, tuple]
) -> None:
    """Fail closed if SEXT32T64 residual is not ALU0|ALU1|ALU2 (0x7)."""
    by = {name: mask for name, mask in rows}
    if "SEXT32T64" not in by:
        raise SystemExit("error: SEXT32T64 missing from occupancy rows")
    mask = by["SEXT32T64"]
    units = frozenset(golden_units_for("SEXT32T64", index))
    if not (ALU_TRIPLE <= units) or (units & MAC_PAIR):
        raise SystemExit(
            f"error: golden SEXT32T64 Available {sorted(units)} "
            "is not ALU0|ALU1|ALU2"
        )
    if mask != 0x7:
        raise SystemExit(
            f"error: SEXT32T64 residual occupancy {mask:#x} "
            "(want 0x7; Slot012_ALU/'64' must not drop ALU0)"
        )


def prove_product_ls_masks(rows: Sequence[Tuple[str, int]]) -> None:
    """Fail closed unless LD32/LD64=0x3, ST32/ST64=0x1, and no compiler LS 0x6.

    LS 0x6 restamps pipeline-*-*.ll and vliw-slot-stress; do not emit it.
    """
    by = {name: mask for name, mask in rows}
    bad: List[str] = []
    for name, want in PRODUCT_LS_MASKS.items():
        if name not in by:
            bad.append(f"{name} missing")
        elif by[name] != want:
            bad.append(f"{name} {by[name]:#x} (want {want:#x})")
    if bad:
        raise SystemExit(
            "error: product occupancy pins failed: " + "; ".join(bad)
        )
    ls6 = [
        f"{name} {mask:#x}"
        for name, mask in rows
        if COMPILER_LS_RE.match(name) and mask == 0x6
    ]
    if ls6:
        raise SystemExit(
            "error: compiler LS occupancy 0x6 restamps pipeline/vliw pins: "
            + "; ".join(ls6[:8])
        )


def prove_x2x4_available_masks(
    rows: Sequence[Tuple[str, int]], index: Dict[str, tuple]
) -> Tuple[int, int]:
    """Fail closed if X2*/X4* masks drift from golden Available unit sets.

    Pure rows-vs-golden ratchet: ALU triples (ALU0|ALU1|ALU2) must carry
    0x7, MAC pairs (MAC0|MAC1) must carry 0x6. Counts come from golden
    directly, not from any name-keyed patching.
    """
    alu_n = 0
    mac_n = 0
    bad: List[str] = []
    for name, mask in rows:
        if not name.startswith(("X2", "X4")):
            continue
        units = frozenset(golden_units_for(name, index))
        if not units:
            continue
        has_alu = ALU_TRIPLE <= units
        has_mac = bool(units & MAC_PAIR)
        if has_alu and not has_mac:
            alu_n += 1
            if mask != 0x7:
                bad.append(f"{name} ALU Available {sorted(units)} mask={mask:#x}")
        elif has_mac and not (units & ALU_TRIPLE):
            mac_n += 1
            if mask != 0x6:
                bad.append(f"{name} MAC Available {sorted(units)} mask={mask:#x}")
    if bad:
        raise SystemExit(
            "error: X2*/X4* golden Available occupancy ratchet failed: "
            + "; ".join(bad[:8])
        )
    if alu_n == 0:
        raise SystemExit("error: no X2*/X4* ALU Available triples for occupancy ratchet")
    return alu_n, mac_n


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
    with tempfile.TemporaryDirectory(prefix="haydn-occ-stale-") as tmp:
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


def resolve_formats_inc(explicit: Optional[Path]) -> Optional[Path]:
    """HaydnGenFormats.inc from --formats, HAYDN_GEN_FORMATS, or HAYDN_BIN."""
    if explicit is not None:
        return explicit
    env = os.environ.get("HAYDN_GEN_FORMATS")
    if env:
        return Path(env)
    bindir = os.environ.get("HAYDN_BIN")
    if bindir:
        cand = (
            Path(bindir).resolve().parent
            / "lib"
            / "Target"
            / "Haydn"
            / "HaydnGenFormats.inc"
        )
        if cand.is_file():
            return cand
    return None


def prove_altinsts_not_universe(
    formats: Optional[Path], n_rows: int, *, required: bool
) -> None:
    """A 1-row AlternateInsts parse must not be able to satisfy --check."""
    if formats is None or not formats.is_file():
        if required:
            raise SystemExit(
                "error: occupancy --check requires --formats HaydnGenFormats.inc "
                "(or HAYDN_BIN / HAYDN_GEN_FORMATS); AlternateInsts-only is not "
                "the residual universe"
            )
        return
    alt = parse_alternate_insts(formats)
    if not alt:
        raise SystemExit(
            "error: occupancy --check could not parse AlternateInsts from "
            f"{formats}; AlternateInsts-only (1 MultiSlot_Pseudo row) cannot "
            "satisfy the occupancy floor"
        )
    if len(alt) >= MIN_OCCUPANCY_ROWS:
        return
    if n_rows <= len(alt):
        raise SystemExit(
            "error: occupancy universe collapsed to AlternateInsts "
            f"({len(alt)} rows)"
        )
    print(
        f"OK occupancy not AlternateInsts-only "
        f"(altinsts={len(alt)} rows={n_rows})"
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    add_family_argument(ap)
    haydn_default = Path(__file__).resolve().parents[1]
    ap.add_argument(
        "--haydn-dir",
        type=Path,
        default=haydn_default,
        help="Target Haydn directory (default: llvm/lib/Target/Haydn)",
    )
    ap.add_argument(
        "--formats",
        type=Path,
        default=None,
        help="HaydnGenFormats.inc (required for --check; MultiSlot merge)",
    )
    ap.add_argument("--index", type=Path, default=None)
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output path (default: <haydn-dir>/HaydnGenAltOccupancy.inc)",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="Verify committed output against regenerate (no write)",
    )
    args = ap.parse_args(argv)
    family = get_family(args.family)
    haydn_dir = args.haydn_dir
    out_path = args.out or (haydn_dir / "HaydnGenAltOccupancy.inc")

    if args.index:
        index_path = args.index
        golden = index_path.parent
    else:
        golden = resolve_golden_dir(family)
        index_path = golden / family.index_filename
    if not index_path.is_file():
        print(f"error: instruction type index not found: {index_path}", file=sys.stderr)
        return 2

    try:
        verify_authority_inputs(golden, [index_path.name])
        verify_golden_inputs_pin(golden_inputs_pin_path())
        index_sha = sha256_file(index_path)
        if index_sha != PINNED_INDEX_SHA256:
            raise SystemExit(
                f"error: index sha256 {index_sha} != pinned {PINNED_INDEX_SHA256}"
            )
        index = load_golden_index(index_path)
        formats = resolve_formats_inc(args.formats)
        rows = collect_rows(haydn_dir, index, formats)
        alu_n, mac_n = prove_x2x4_available_masks(rows, index)
        prove_sext32t64_residual_mask(rows, index)
        prove_product_ls_masks(rows)
        content = emit_occupancy_inc(rows, family, index_sha)
        prove_altinsts_not_universe(formats, len(rows), required=args.check)
    except SystemExit as exc:
        msg = str(exc)
        if msg:
            print(msg, file=sys.stderr)
        return 2 if msg else 0

    targets = {out_path: content}
    if args.check:
        failed = bool(diff_generated_targets(targets))
        if failed:
            return 1
        try:
            prove_flipped_byte_fails(targets)
        except SystemExit as exc:
            print(str(exc), file=sys.stderr)
            return 1
        print(
            "OK occupancy "
            f"rows={len(rows)} x2x4_alu={alu_n} x2x4_mac={mac_n}"
        )
        print(
            "OK product occupancy LD32=0x3 LD64=0x3 ST32=0x1 ST64=0x1 "
            "SEXT32T64=0x7"
        )
        return 0

    out_path.write_text(content, encoding="utf-8")
    print(
        f"wrote {out_path} ({len(rows)} rows, "
        f"x2x4_alu={alu_n} x2x4_mac={mac_n})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
