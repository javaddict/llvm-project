"""Shared Format E family descriptor, golden pins, and path helpers.

E96 is the only admitted family. Generators take --family (default e96)
and fail closed on any other name.

`python3 family_core.py --check` verifies every nine-file authority pin
and refuses an unpinned, derived, or unpublished input.

`--pin-check` verifies the compiler nine-file ledger and the catalog
six-file product pin without a golden directory. Catalog provenance is
six files by design; unused/derived members stay on the compiler pin.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Optional, Sequence, Tuple

DEFAULT_FAMILY = "e96"
# Authored catalog overlay (P19). Golden dir comes only from env / --json.
AUTHORED_OVERLAY_PATH = Path(__file__).with_name("authored_catalog_overlay.json")

# Manifest pin for the companion XLSX (geometry authority pair).
PINNED_XLSX_SHA256 = (
    "2a2b43cb394a16cf89173538f2a673235fdb6e4ed04cb6e4e75e72520cf7ffdb"
)
# Golden v2_2 (supersedes v2_1 2026-08-28): +AR_CBR type 101 (3b opcode)
# on LOADSTORE0 entry0 in BOTH packet entries (E2 and E3; LOAD1 keeps
# AR/RI6/RR), carrying 7 new circular-buffer UA load/store instrs
# (PLTWWUA_CB_POST, PLQHWUA_CB_POST, D_LTWUA_CB_POST, D_LQHWUA_CB_POST,
# D_STWUA_CB_POST, D_SQHWUA_CB_POST, WBARWUA_CB; opcodes 0x01-0x07 plus
# NOP 0x00). Operand fields ar_sel[2b]/cbr_sel[1b]/dest1(rtd)[4b]/
# dest2(rs)[4b]; E2 bits [37:36]/[35]/[23:20]/[27:24], E3 shifted to
# [36:35]/[34]/[20:17]/[24:21] (E3 payload uses 89/90b). instruction_
# type_index.json and instruction_type_operands.json gained the 7 rows
# (+AR_CBR operand shapes). Zero removals, zero opcode changes on the
# v2_1 surface; bit geometry identical elsewhere. Read-port repair
# re-applied 2026-08-28 (haydn_encoding.py --fix-read-ports --write: the
# same 6 FMUL*32S rows regained rtd in DR_Read_Port; the v2_2 delivery
# had dropped the v2_1 repair). Index pin is the repaired file.
PINNED_JSON_SHA256 = (
    "c436793cc8d3295088dda2271e68e5b53074eeb4bce3341d443ebac3e828dcba"
)
PINNED_INDEX_SHA256 = (
    "3f306463d108c8240b6f8afa876e3fa4ca06b91ee7f38efe1fb4eba631c3433b"
)
PINNED_CANONICAL_SHA256 = (
    "741f5b4141990dc27dc217d2b0c0d7ab57240e08ef31c34bc036f11bbda1938e"
)
PINNED_CONSTRAINTS_SHA256 = (
    "e0d7f7f0e7ce06622f4ae90dc9366caf16da48993c7f803c02d460473fd9b56a"
)
PINNED_REFMAN_SHA256 = (
    "550dac0c82c160397c510bd403116056e046a43d8df41cd34d81ab678cd9b49b"
)
PINNED_OPERANDS_INFO_SHA256 = (
    "e4b61bf5b5be2634b1665474bf4906db0df017a939289a49d40e82bc2121fb12"
)
PINNED_TYPE_OPERANDS_SHA256 = (
    "434544ef336fe703ff69c6c59316c3e89f3350e1d4cae6fd6790929d01e7bd20"
)
# instruction_to_entry.xlsx ZIP bytes embed openpyxl timestamps. Pin the
# extracted cell values, not the file bytes.
PINNED_ENTRY_XLSX_CELLS_SHA256 = (
    "ba6d65066b812083925ec5b68dae6dee26323ceb8aaab0040db0ff083f5d3098"
)

SSML_NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
GOLDEN_INPUTS_PIN_REL = Path(__file__).with_name("GOLDEN_INPUTS.sha256")
# In-tree catalog pin: six digest lines, no comments. Unused/derived
# nine-file members stay on GOLDEN_INPUTS_PIN_REL.
IN_TREE_CATALOG_PIN_REL = (
    Path("simulator")
    / "bundlesim"
    / "isa"
    / "database"
    / "generated"
    / "GOLDEN_INPUTS.sha256"
)
ENTRY_XLSX_PIN_NAME = "instruction_to_entry.xlsx#cells"
_UNPUBLISHED_NAME = re.compile(
    r"(top[-_]?pad|underfill|absence[-_]?encode|competitive[-_]?resource)",
    re.I,
)


@dataclass(frozen=True)
class AuthorityFile:
    """One row of the nine-file golden authority set.

    role:
      consumed — Format E generators read this file; pin required
      unused   — nine-file member not read here; still pin-checked so a
                 new consumer cannot appear unpinned
      derived  — not a hardware-fact oracle; consuming it is a hard error
    """

    filename: str
    sha256: Optional[str]
    role: str
    cell_sha256: Optional[str] = None


# instruction_to_entry.xlsx is regenerated from instruction_type_index.json
# (openpyxl timestamps). Authority is the pinned index, not the xlsx bytes.
AUTHORITY_FILES: Tuple[AuthorityFile, ...] = (
    AuthorityFile(
        "format_e_bit_layout_v2_2.xlsx", PINNED_XLSX_SHA256, "consumed"
    ),
    AuthorityFile(
        "format_e_bit_layout_v2_2.json", PINNED_JSON_SHA256, "consumed"
    ),
    AuthorityFile(
        "format_e_canonical_vectors_v1.json",
        PINNED_CANONICAL_SHA256,
        "consumed",
    ),
    AuthorityFile(
        "instruction_type_index.json", PINNED_INDEX_SHA256, "consumed"
    ),
    AuthorityFile(
        "VLIW_Engine_Compiler_Constraints.md",
        PINNED_CONSTRAINTS_SHA256,
        "consumed",
    ),
    AuthorityFile(
        "VLIW_Engine_Reference_Manual.docx", PINNED_REFMAN_SHA256, "unused"
    ),
    AuthorityFile("operands_info.md", PINNED_OPERANDS_INFO_SHA256, "unused"),
    AuthorityFile(
        "instruction_type_operands.json",
        PINNED_TYPE_OPERANDS_SHA256,
        "unused",
    ),
    AuthorityFile(
        "instruction_to_entry.xlsx",
        None,
        "derived",
        PINNED_ENTRY_XLSX_CELLS_SHA256,
    ),
)
AUTHORITY_BY_NAME: Dict[str, AuthorityFile] = {
    rec.filename: rec for rec in AUTHORITY_FILES
}

# Catalog generate_catalog.py hashes only these six files as provenance.
# Unused/derived nine-file members stay on the compiler pin.
# operands_info.md is a retired catalog basename.
CATALOG_PIN_FILES: Tuple[str, ...] = (
    "format_e_bit_layout_v2_2.xlsx",
    "format_e_bit_layout_v2_2.json",
    "format_e_canonical_vectors_v1.json",
    "VLIW_Engine_Compiler_Constraints.md",
    "VLIW_Engine_Reference_Manual.docx",
    "instruction_type_index.json",
)
# Product ARTIFACT.json hashes this pin FILE, not only inner digests.
# Comment or whitespace rewrites are catalog content drift. Keep six digest lines
# (two spaces) plus a trailing newline; no header comments.
# Compiler-only rows and commentary live on GOLDEN_INPUTS_PIN_REL.
CATALOG_PIN_FILE_SHA256 = (
    "2af5e0ac5f1ee9dae3b69a46df81c88b91fd78cdd914b0b6667bce2b6036c778"
)
RETIRED_CATALOG_BASENAMES = frozenset(
    {
        "operands_info.md",
        "slot0_alu_instruction_list.json",
        "slot0_load_and_store_unit_instruction_list.json",
        "slot1_alu_instruction_list.json",
        "slot1_load_unit_instruction_list.json",
        "slot1_mac_instruction_list.json",
        "slot2_alu_instruction_list.json",
        "slot2_mac_instruction_list.json",
    }
)
COMPILER_ONLY_PIN_NAMES = frozenset(
    {
        "operands_info.md",
        "instruction_type_operands.json",
        ENTRY_XLSX_PIN_NAME,
        "instruction_to_entry.xlsx",
    }
)
# Non-authority files that may sit next to the nine-file set. A new
# .json/.xlsx/.md/.docx in the golden dir is an unpinned authority input.
GOLDEN_DIR_TOOLING = frozenset({"generate_instruction_to_entry.py"})
AUTHORITY_SHAPED_SUFFIXES = (".json", ".xlsx", ".md", ".docx")


@dataclass(frozen=True)
class BundleFamily:
    id: int
    cli_name: str
    display: str
    json_filename: str
    xlsx_filename: str
    index_filename: str
    canonical_filename: str
    constraints_filename: str
    pinned_json_sha256: str
    pinned_xlsx_sha256: str
    pinned_index_sha256: str
    pinned_canonical_sha256: str
    records_inc: str
    setdesc_ledger_inc: str
    members_td_inc: str
    logical_defs_td_inc: str
    member_opcodes_inc: str
    mnemonic_roundtrip_s: str
    sched_records_inc: str
    memory_cycles_inc: str


# FamilyID 0 matches HaydnFamilyE96 and C++ BundleFamily::E96.
E96 = BundleFamily(
    id=0,
    cli_name="e96",
    display="E96",
    json_filename="format_e_bit_layout_v2_2.json",
    xlsx_filename="format_e_bit_layout_v2_2.xlsx",
    index_filename="instruction_type_index.json",
    canonical_filename="format_e_canonical_vectors_v1.json",
    constraints_filename="VLIW_Engine_Compiler_Constraints.md",
    pinned_json_sha256=PINNED_JSON_SHA256,
    pinned_xlsx_sha256=PINNED_XLSX_SHA256,
    pinned_index_sha256=PINNED_INDEX_SHA256,
    pinned_canonical_sha256=PINNED_CANONICAL_SHA256,
    records_inc="HaydnGenFormatERecords.inc",
    setdesc_ledger_inc="HaydnGenFormatESetDescLedger.inc",
    members_td_inc="HaydnFormatsE96Members.td.inc",
    logical_defs_td_inc="HaydnInstrInfoGolden.td.inc",
    member_opcodes_inc="HaydnGenFormatEMemberOpcodes.inc",
    mnemonic_roundtrip_s="format-e-mnemonic-roundtrip.s",
    sched_records_inc="HaydnGenSchedRecords.inc",
    memory_cycles_inc="HaydnGenMemoryCycles.inc",
)

FAMILIES: Dict[str, BundleFamily] = {
    E96.cli_name: E96,
}


def get_family(name: str) -> BundleFamily:
    family = FAMILIES.get(name)
    if family is None:
        admitted = ", ".join(sorted(FAMILIES))
        raise SystemExit(
            f"error: unknown family {name!r}; admitted: {admitted}"
        )
    return family


RECORDS_GENERATOR = "llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py"
SCHED_GENERATOR = "llvm/lib/Target/Haydn/FormatE/generate_sched_records.py"
SCHEMA_VERSION = "format-e-records/1"
UMBRELLA_CHECK = "llvm/lib/Target/Haydn/FormatE/check_generated.sh"


def authority_pin_banner_lines(prefix: str = "//") -> list:
    """Stamp the compiler nine-file pin (8 byte + 1 cell digest).

    Catalog generate_catalog.py stays six-file; unused/derived rows live here.
    Derived instruction_to_entry.xlsx is named with #cells, never ZIP bytes.
    """
    lines = [
        f"{prefix} Compiler nine-file pin (8 byte + 1 cell). "
        "Catalog generate_catalog.py stays six-file."
    ]
    for rec in AUTHORITY_FILES:
        if rec.role == "derived":
            name = ENTRY_XLSX_PIN_NAME
            digest = rec.cell_sha256
        else:
            name = rec.filename
            digest = rec.sha256
        if not digest:
            raise SystemExit(f"error: authority pin missing digest for {name}")
        lines.append(f"{prefix} PIN: {digest}  {name}")
    return lines


def generated_banner(
    *,
    generator: str,
    family: BundleFamily,
    json_sha: str = "",
    xlsx_sha: str = "",
    prefix: str = "//",
    authority_pins: bool = False,
) -> list:
    """P19 generated-file header. prefix is '//' or '#'."""
    check = f"python3 {generator} --check --family {family.cli_name}"
    lines = [
        f"{prefix} DO NOT EDIT.",
        f"{prefix} Generator: {generator}",
        f"{prefix} Schema: {SCHEMA_VERSION}",
        f"{prefix} Family: {family.cli_name} (id={family.id})",
        f"{prefix} Check: {check}",
        f"{prefix} Umbrella: {UMBRELLA_CHECK}",
    ]
    if json_sha:
        lines.append(f"{prefix} JSON-SHA256: {json_sha}")
    if xlsx_sha:
        lines.append(f"{prefix} XLSX-SHA256: {xlsx_sha}")
    if AUTHORED_OVERLAY_PATH.is_file():
        lines.append(f"{prefix} Overlay: {AUTHORED_OVERLAY_PATH.name}")
        lines.append(f"{prefix} Overlay-SHA256: {sha256_file(AUTHORED_OVERLAY_PATH)}")
    if authority_pins:
        lines.extend(authority_pin_banner_lines(prefix))
    return lines


def add_family_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--family",
        default=DEFAULT_FAMILY,
        metavar="NAME",
        help=f"Bundle family (default: {DEFAULT_FAMILY})",
    )


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def xlsx_cell_digest(path: Path) -> str:
    """SHA-256 of extracted cell values, ignoring ZIP/core timestamps."""
    with zipfile.ZipFile(path) as zf:
        shared: list[str] = []
        names = zf.namelist()
        if "xl/sharedStrings.xml" in names:
            root = ET.fromstring(zf.read("xl/sharedStrings.xml"))
            for si in root.findall("m:si", SSML_NS):
                texts = [t.text or "" for t in si.findall(".//m:t", SSML_NS)]
                shared.append("".join(texts))
        cells: list[str] = []
        sheets = sorted(
            n for n in names if n.startswith("xl/worksheets/") and n.endswith(".xml")
        )
        for name in sheets:
            root = ET.fromstring(zf.read(name))
            for cell in root.findall(".//m:c", SSML_NS):
                ref = cell.get("r") or ""
                kind = cell.get("t")
                value_el = cell.find("m:v", SSML_NS)
                if value_el is not None and value_el.text is not None:
                    if kind == "s":
                        val = shared[int(value_el.text)]
                    else:
                        val = value_el.text
                else:
                    inline = cell.find("m:is", SSML_NS)
                    if inline is None:
                        continue
                    val = "".join(
                        (t.text or "") for t in inline.findall(".//m:t", SSML_NS)
                    )
                cells.append(f"{name}\t{ref}\t{val}")
    return hashlib.sha256("\n".join(cells).encode("utf-8")).hexdigest()


def resolve_golden_dir(family: BundleFamily) -> Path:
    """Pinned golden dir from HAYDN_GOLDEN_DIR or BUNDLESIM_GOLDEN_DIR.

    No host-path fallback. Qualification fails closed if neither env is set
    or the named directory does not contain the family's JSON.
    """
    json_name = family.json_filename
    for key in ("HAYDN_GOLDEN_DIR", "BUNDLESIM_GOLDEN_DIR"):
        raw = os.environ.get(key)
        if not raw:
            continue
        p = Path(raw)
        if (p / json_name).is_file():
            return p
        nested = p / "golden"
        if (nested / json_name).is_file():
            return nested
        raise SystemExit(
            f"error: {key}={raw} does not contain {json_name} "
            "(no host-path fallback)"
        )
    raise SystemExit(
        "error: set HAYDN_GOLDEN_DIR or BUNDLESIM_GOLDEN_DIR to the pinned "
        "golden directory (no host-path fallback)"
    )


_DEF_RE = re.compile(r"^defm?\s+\S+", re.M)
_INCLUDE_RE = re.compile(r'^include\s+"([^"]+)"', re.M)
_FAMILY_RE = re.compile(r'HaydnBundleFamily<\s*"([^"]+)"\s*,\s*(\d+)\s*>')
_RESIDUAL_OCCUPANCY_DEF_RE = re.compile(r"^defm?\s+(\S+_S[012])\b", re.M)
_FORMAT_E_MEMBER_DEF_RE = re.compile(r"^defm?\s+(\S+_E[23]_\S*)", re.M)
_BANNED_MATCHER_FILES = frozenset(
    {
        "HaydnFormatE.td",
        "HaydnFamilies.td",
        "HaydnInstrInfoManual.td",
        "HaydnFormatsE96Members.td.inc",
        "HaydnFormatsALU32.td",
        "HaydnFormatsALU64.td",
        "HaydnFormatsLS.td",
        "HaydnFormatsMAC.td",
        "HaydnFormatsE96.td",
        "HaydnCompositeFormats.td",
    }
)
# Matcher-reachable generated includes: public logicals + published
# itineraries + the M18 mca ItinRW bridge (schedule-surface projection
# of the same itineraries; no Inst defs). Member Inst defs stay on the
# product root (HaydnFormatE.td).
_MATCHER_ALLOWED_GENERATED = frozenset(
    {
        "HaydnInstrInfoGolden.td.inc",
        "HaydnGenSchedRecords.inc",
        "HaydnGenSchedMcaBridge.td.inc",
    }
)
_MATCHER_BANNED_SUBSTRINGS = (
    "HaydnFormatE",
    "HaydnFormats",
    "HaydnInstrInfoManual",
    "HaydnFamilies",
)
_INSTRINFO_DEF_RE = re.compile(r"^def\s+(\S+)\s*:\s*InstrInfo\b", re.M)
_TARGET_DEF_RE = re.compile(r"^def\s+(\S+)\s*:\s*Target\b", re.M)
_BUNDLE_FAMILY_ENUM_RE = re.compile(
    r"enum class BundleFamily\s*:\s*uint8_t\s*\{([^}]*)\}", re.S
)
_FAMILY_COLUMN_RE = re.compile(r"^\s*\{\s*\d+\s*,\s*\d+\s*,\s*(\d+)\s*,", re.M)
# Overlay-owned leftovers that stay in HaydnInstrInfo.td as matcher shells.
# Dual-dest MAC ties and LS WITH lane stores; no hypothesized Inst bits.
HAND_RESIDUAL_LOGICALS = (
    "X2MULA32",
    "X2MULS32",
    "X4FF2MULA16S",
    "X4FF2MULS16S",
    "X4MULA16",
    "X4MULA16S",
    "X4MULS16",
    "X4MULS16S",
    "D_SW_L_WITH_IMM",
    "D_SW_H_WITH_IMM",
)


def _authority_name(item: object) -> str:
    if isinstance(item, Path):
        return item.name
    return Path(str(item)).name


def refuse_unpublished_choice(name: str) -> None:
    """Unpublished pad / underfill / competitive-resource choices are not authority."""
    if _UNPUBLISHED_NAME.search(Path(str(name)).name):
        raise SystemExit(
            f"error: unpublished encoding choice {name!r} is not authority"
        )


def verify_authority_inputs(golden: Path, consumed: Iterable[object]) -> None:
    """Fail closed if a consumed golden file is unpinned, derived, or drifted.

    Every pinned nine-file member in *golden* is hash-checked. Derived
    ``instruction_to_entry.xlsx`` is refused as a hardware-fact input and
    pinned by extracted cell values.
    """
    consumed_names = {_authority_name(item) for item in consumed}
    for name in sorted(consumed_names):
        refuse_unpublished_choice(name)
        rec = AUTHORITY_BY_NAME.get(name)
        if rec is None:
            admitted = ", ".join(
                a.filename for a in AUTHORITY_FILES if a.sha256 or a.cell_sha256
            )
            raise SystemExit(
                f"error: unpinned authority input {name!r} "
                f"(admitted: {admitted})"
            )
        if rec.role == "derived" or rec.sha256 is None:
            raise SystemExit(
                f"error: {name} is derived from instruction_type_index.json "
                "and is not a hardware-fact authority"
            )
        if rec.role == "unused":
            raise SystemExit(
                f"error: {name} is an unused nine-file member, "
                "not a generator input"
            )
    for rec in AUTHORITY_FILES:
        path = golden / rec.filename
        if rec.role == "derived":
            if not rec.cell_sha256:
                raise SystemExit(
                    f"error: derived authority {rec.filename} missing cell pin"
                )
            if not path.is_file():
                raise SystemExit(
                    f"error: golden authority {rec.filename} not found: {path}"
                )
            got = xlsx_cell_digest(path)
            if got != rec.cell_sha256:
                raise SystemExit(
                    f"error: {rec.filename} cell sha256 {got} != pinned "
                    f"{rec.cell_sha256}"
                )
            continue
        if rec.sha256 is None:
            continue
        if not path.is_file():
            raise SystemExit(
                f"error: golden authority {rec.filename} not found: {path}"
            )
        got = sha256_file(path)
        if got != rec.sha256:
            raise SystemExit(
                f"error: {rec.filename} sha256 {got} != pinned {rec.sha256}"
            )
    verify_golden_dir_no_unpinned(golden)


def _check_matcher_root_text(matcher_text: str) -> None:
    """Fail closed unless the matcher file includes HaydnGeneric.td only."""
    includes = _INCLUDE_RE.findall(matcher_text)
    if includes != ["HaydnGeneric.td"]:
        raise SystemExit(
            f"error: matcher root includes {includes}, "
            "expected HaydnGeneric.td only"
        )
    if any(name in matcher_text for name in _MATCHER_BANNED_SUBSTRINGS):
        raise SystemExit(
            "error: matcher root must not pull Format E members, "
            "family geometry, or Manual.td"
        )


def _matcher_include_closure(haydn_dir: Path, root: Path) -> list:
    """Authored include closure of the matcher root. Skip llvm/Target paths."""
    seen: list = []
    queued = [root]
    found = set()
    while queued:
        path = queued.pop()
        if path in found:
            continue
        found.add(path)
        if not path.is_file():
            raise SystemExit(f"error: matcher include missing: {path.name}")
        seen.append(path)
        text = path.read_text(encoding="utf-8")
        for inc in _INCLUDE_RE.findall(text):
            name = Path(inc).name
            if name in _BANNED_MATCHER_FILES or name.startswith("HaydnFormats"):
                raise SystemExit(
                    f"error: matcher-reachable {path.name} includes {name}; "
                    "matcher root must not pull Format E members, "
                    "family geometry, or Manual.td"
                )
            if inc.startswith("llvm/"):
                continue
            child = haydn_dir / name
            queued.append(child)
    return seen


def check_residual_hand_logicals(td_dir: Path) -> None:
    """Manual.td stays empty; named leftovers are HaydnInst shells."""
    manual = td_dir / "HaydnInstrInfoManual.td"
    if re.search(r"^defm?\s+\S+", manual.read_text(encoding="utf-8"), re.M):
        raise SystemExit(f"error: {manual.name} must remain a 0-def tombstone")
    info = td_dir / "HaydnInstrInfo.td"
    text = info.read_text(encoding="utf-8")
    if re.search(r'^include\s+"HaydnInstrInfoManual\.td"', text, re.M):
        raise SystemExit("error: HaydnInstrInfo.td must not include Manual.td")
    for raw in text.splitlines():
        if "HaydnInstrInfoManual" in raw and "formerly" not in raw:
            raise SystemExit(
                "error: HaydnInstrInfo.td still treats Manual.td as a def home: "
                + raw.strip()
            )
    def_spans = list(re.finditer(r"^def\s+([A-Za-z0-9_]+)\b", text, re.M))
    found = {m.group(1) for m in def_spans}
    missing = [n for n in HAND_RESIDUAL_LOGICALS if n not in found]
    if missing:
        raise SystemExit(
            "error: residual hand logicals missing from HaydnInstrInfo.td: "
            + ", ".join(missing)
        )
    for i, m in enumerate(def_spans):
        name = m.group(1)
        if name not in HAND_RESIDUAL_LOGICALS:
            continue
        end = def_spans[i + 1].start() if i + 1 < len(def_spans) else len(text)
        body = text[m.start() : end]
        if (
            re.search(r"\blet\s+Inst\{", body)
            or "FmtLaneStore" in body
            or not re.search(r":\s*HaydnInst\s*<", body)
        ):
            raise SystemExit(
                f"error: {name} must be a HaydnInst shell without Inst bits"
            )


def _check_family_handle_header(text: str) -> None:
    """Family-handle C++ API stays E96-only; tables remain the FormatE* names."""
    match = _BUNDLE_FAMILY_ENUM_RE.search(text)
    if not match:
        raise SystemExit("error: BundleFamily enum missing from family-handle header")
    enumerators = re.findall(
        r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(\d+)", match.group(1)
    )
    if enumerators != [("E96", "0")]:
        raise SystemExit(
            "error: family-handle tables must stay E96-only, found "
            f"{enumerators}"
        )
    if "kAdmittedFamily = BundleFamily::E96" not in text:
        raise SystemExit("error: kAdmittedFamily must be BundleFamily::E96")
    if "getFamilyRecords" not in text:
        raise SystemExit("error: getFamilyRecords family-handle API missing")
    if "Family != BundleFamily::E96" not in text:
        raise SystemExit(
            "error: getFamilyRecords must refuse every family besides E96"
        )


def _table_family_column(text: str, table: str, label: str) -> None:
    marker = f"{table}[] = {{"
    start = text.find(marker)
    if start < 0:
        raise SystemExit(f"error: {label} missing {table} table")
    open_at = text.find("{", start + len(marker) - 1)
    close_at = text.find("\n};", open_at)
    if open_at < 0 or close_at < 0:
        raise SystemExit(f"error: {label} {table} table is not closed")
    families = _FAMILY_COLUMN_RE.findall(text[open_at:close_at])
    if not families:
        raise SystemExit(f"error: {label} {table} has no family-column rows")
    bad = sorted({value for value in families if value != "0"})
    if bad:
        raise SystemExit(
            f"error: {label} {table} family column must stay 0 (E96), "
            f"found {bad}"
        )


def _check_records_family_column(text: str) -> None:
    if "static constexpr uint8_t FormatEFamilyId = 0u;" not in text:
        raise SystemExit("error: FormatEFamilyId must be 0 (E96)")
    if "static constexpr unsigned FormatEAdmittedFamilyCount = 1u;" not in text:
        raise SystemExit("error: FormatEAdmittedFamilyCount must be 1")
    if "uint8_t Family;" not in text:
        raise SystemExit("error: FormatEMemberRec must carry numeric Family")
    _table_family_column(text, "FormatEMembers", "HaydnGenFormatERecords.inc")


def _check_ledger_family_column(text: str) -> None:
    if "uint8_t Family;" not in text:
        raise SystemExit(
            "error: FormatESetDescLedgerRec must carry numeric Family"
        )
    _table_family_column(
        text, "FormatESetDescLedger", "HaydnGenFormatESetDescLedger.inc"
    )
    # Inverted firewall: the ledger is placement provenance only. Runtime
    # compatibility machinery (semantic-compat IDs, interned permutation
    # tables, frontier solvers) is forbidden — direct setDesc identity is
    # enforced at generation time by check_setdesc_identity.
    for banned in (
        "SemanticCompatibilityID",
        "OperandConstraintID",
        "ResourceClassID",
        "TimingClassID",
        "FormatESemanticCompatRec",
        "PlacementAlternative",
        "solveBundleFrontier",
    ):
        if banned in text:
            raise SystemExit(
                f"error: HaydnGenFormatESetDescLedger.inc must not contain "
                f"{banned} (runtime compatibility machinery is banned; the "
                "identity check owns direct-setDesc legality)"
            )


def _check_family_sched_handle(text: str) -> None:
    if "GeneratedFamilyE2EntryCapacity = 2;" not in text:
        raise SystemExit("error: family-handle E2 entry capacity missing")
    if "GeneratedFamilyE3EntryCapacity = 3;" not in text:
        raise SystemExit("error: family-handle E3 entry capacity missing")
    if "GeneratedFamilySharedUnits[]" not in text:
        raise SystemExit("error: family-handle SharedUnits table missing")


def check_family_handle_records(haydn_dir: Path) -> None:
    """Family-handle records stay inert: E96 only, no second family tables."""
    header = haydn_dir / "HaydnFormatERecords.h"
    records = haydn_dir / "HaydnGenFormatERecords.inc"
    ledger = haydn_dir / "HaydnGenFormatESetDescLedger.inc"
    sched = haydn_dir / "HaydnGenMemoryCycles.inc"
    for path in (header, records, ledger, sched):
        if not path.is_file():
            raise SystemExit(f"error: family-handle file missing: {path.name}")
    _check_family_handle_header(header.read_text(encoding="utf-8"))
    _check_records_family_column(records.read_text(encoding="utf-8"))
    _check_ledger_family_column(ledger.read_text(encoding="utf-8"))
    _check_family_sched_handle(sched.read_text(encoding="utf-8"))
    print("OK family-handle records inert")


def check_cutover_surfaces(haydn_dir: Path) -> None:
    """Keep matcher/Manual/family surfaces collapsed; they are not authority.

    Matcher stays HaydnGeneric.td only. Manual.td stays a 0-def tombstone.
    Only HaydnFamilyE96 FamilyID=0 is admitted (MF0 stays inert).
    Matcher-reachable files may hold public logicals, never occupancy-suffix
    or generated Format E member defs. Family-handle records stay inert
    (E96 FamilyID=0 only; getFamilyRecords refuses any other family).
    """
    manual = haydn_dir / "HaydnInstrInfoManual.td"
    defs = _DEF_RE.findall(manual.read_text(encoding="utf-8"))
    if defs:
        raise SystemExit(
            f"error: {manual.name} must remain a 0-def tombstone, found {defs}"
        )
    matcher = haydn_dir / "HaydnAsmMatcher.td"
    _check_matcher_root_text(matcher.read_text(encoding="utf-8"))
    closure = _matcher_include_closure(haydn_dir, matcher)
    residual = []
    member_defs = []
    extra_generated = []
    instrinfo_defs = []
    target_defs = []
    for path in closure:
        text = path.read_text(encoding="utf-8")
        residual.extend(f"{path.name}:{n}" for n in _RESIDUAL_OCCUPANCY_DEF_RE.findall(text))
        member_defs.extend(
            f"{path.name}:{n}" for n in _FORMAT_E_MEMBER_DEF_RE.findall(text)
        )
        instrinfo_defs.extend(
            f"{path.name}:{n}" for n in _INSTRINFO_DEF_RE.findall(text)
        )
        target_defs.extend(f"{path.name}:{n}" for n in _TARGET_DEF_RE.findall(text))
        if path.name.endswith(".inc") and path.name not in _MATCHER_ALLOWED_GENERATED:
            extra_generated.append(path.name)
    if residual:
        raise SystemExit(
            "error: matcher root still admits residual occupancy-suffix defs "
            f"{residual}"
        )
    if member_defs:
        raise SystemExit(
            "error: matcher root still admits Format E member defs "
            f"{member_defs}"
        )
    if extra_generated:
        raise SystemExit(
            "error: matcher root pulls generated members "
            f"{extra_generated}; matcher-facing generated includes are "
            + ", ".join(sorted(_MATCHER_ALLOWED_GENERATED))
        )
    if instrinfo_defs != ["HaydnGeneric.td:HaydnInstrInfo"]:
        raise SystemExit(
            "error: matcher root must expose HaydnInstrInfo only, "
            f"found {instrinfo_defs}"
        )
    if target_defs != ["HaydnGeneric.td:Haydn"]:
        raise SystemExit(
            "error: matcher root must expose one Target (Haydn), "
            f"found {target_defs}"
        )
    generic = (haydn_dir / "HaydnGeneric.td").read_text(encoding="utf-8")
    if 'include "HaydnInstrInfoGolden.td.inc"' not in generic:
        raise SystemExit(
            "error: HaydnGeneric.td must include matcher-facing logicals "
            "(HaydnInstrInfoGolden.td.inc)"
        )
    if "HaydnFormatsE96Members" in generic or 'include "HaydnFormatE.td"' in generic:
        raise SystemExit(
            "error: HaydnGeneric.td must not pull Format E members"
        )
    td_residual = []
    authored_and_generated = list(haydn_dir.glob("*.td")) + list(
        haydn_dir.glob("*.td.inc")
    )
    for path in sorted(authored_and_generated):
        td_residual.extend(
            f"{path.name}:{n}"
            for n in _RESIDUAL_OCCUPANCY_DEF_RE.findall(
                path.read_text(encoding="utf-8")
            )
        )
    if td_residual:
        raise SystemExit(
            "error: occupancy-suffix defs remain outside matcher collapse "
            f"{td_residual}"
        )
    check_residual_hand_logicals(haydn_dir)
    format_e = (haydn_dir / "HaydnFormatE.td").read_text(encoding="utf-8")
    if 'include "HaydnFormatsE96Members.td.inc"' not in format_e:
        raise SystemExit(
            "error: HaydnFormatE.td must include generated E96 members"
        )
    if "HaydnInstrInfoManual" in format_e:
        raise SystemExit("error: HaydnFormatE.td must not include Manual.td")
    if 'include "HaydnFormatsE96.td"' in format_e:
        raise SystemExit(
            "error: HaydnFormatE.td must not include HaydnFormatsE96.td"
        )
    families = (haydn_dir / "HaydnFamilies.td").read_text(encoding="utf-8")
    found = _FAMILY_RE.findall(families)
    if found != [("E96", "0")]:
        raise SystemExit(
            f"error: only HaydnFamilyE96 FamilyID=0 is admitted, found {found}"
        )
    if re.search(r"\bMF0\b", families) or "HaydnFamilyMF0" in families:
        raise SystemExit("error: MF0 family must stay inert")
    if re.search(r"\bbits<", families) or re.search(r"\bInst\s*=", families):
        raise SystemExit(
            "error: HaydnFamilies.td must not carry encoding bits "
            "(geometry comes from the nine-file golden set)"
        )
    haydn_td = (haydn_dir / "Haydn.td").read_text(encoding="utf-8")
    if 'include "HaydnFamilies.td"' not in haydn_td:
        raise SystemExit("error: Haydn.td must include HaydnFamilies.td")
    if "HaydnInstrInfoManual" in haydn_td:
        raise SystemExit("error: Haydn.td must not include Manual.td")
    if 'include "HaydnFormatsE96.td"' in haydn_td:
        raise SystemExit("error: Haydn.td must not include HaydnFormatsE96.td")
    formats_e96 = haydn_dir / "HaydnFormatsE96.td"
    e96_defs = _DEF_RE.findall(formats_e96.read_text(encoding="utf-8"))
    if e96_defs:
        raise SystemExit(
            "error: HaydnFormatsE96.td must remain a 0-def tombstone, "
            f"found {e96_defs}"
        )
    banned_includes = []
    for path in sorted(haydn_dir.glob("*.td")):
        if path.name in {"HaydnInstrInfoManual.td", "HaydnFormatsE96.td"}:
            continue
        text = path.read_text(encoding="utf-8")
        includes = _INCLUDE_RE.findall(text)
        if "HaydnInstrInfoManual.td" in includes:
            banned_includes.append(f"{path.name}:HaydnInstrInfoManual.td")
        if "HaydnFormatsE96.td" in includes:
            banned_includes.append(f"{path.name}:HaydnFormatsE96.td")
    if banned_includes:
        raise SystemExit(
            "error: retired td include remains in "
            f"{banned_includes}; matcher/product roots must not pull it"
        )
    sched = (haydn_dir / "HaydnSchedule.td").read_text(encoding="utf-8")
    if "CompleteModel = 1" in sched:
        raise SystemExit(
            "error: HaydnSchedule.td must keep CompleteModel=0 "
            "(per-op resource admission stays closed)"
        )
    if "CompleteModel = 0" not in sched:
        raise SystemExit("error: HaydnSchedule.td CompleteModel=0 missing")
    check_family_handle_records(haydn_dir)


def verify_golden_dir_no_unpinned(golden: Path) -> None:
    """Fail closed if the golden dir holds an unpinned authority-shaped file.

    The nine-file set is the only admitted authority. Tooling next to it
    stays on GOLDEN_DIR_TOOLING. Derived xlsx is already in AUTHORITY_FILES
    (cell-pinned, not ZIP-byte-pinned).
    """
    pinned_names = {rec.filename for rec in AUTHORITY_FILES}
    if not golden.is_dir():
        raise SystemExit(f"error: golden directory not found: {golden}")
    for path in sorted(golden.iterdir()):
        if not path.is_file():
            continue
        name = path.name
        refuse_unpublished_choice(name)
        if name in GOLDEN_DIR_TOOLING or name in pinned_names:
            continue
        if name.endswith(AUTHORITY_SHAPED_SUFFIXES):
            raise SystemExit(
                f"error: unpinned authority input {name!r} "
                f"(admitted: {', '.join(sorted(pinned_names))})"
            )


def golden_inputs_pin_path() -> Path:
    env = os.environ.get("HAYDN_GOLDEN_INPUTS_PIN")
    if env:
        path = Path(env)
        if not path.is_file():
            raise SystemExit(f"error: HAYDN_GOLDEN_INPUTS_PIN={env} is not a file")
        return path
    if GOLDEN_INPUTS_PIN_REL.is_file():
        return GOLDEN_INPUTS_PIN_REL
    raise SystemExit(
        "error: GOLDEN_INPUTS.sha256 pin not found (set HAYDN_GOLDEN_INPUTS_PIN "
        f"or keep {GOLDEN_INPUTS_PIN_REL})"
    )


def parse_golden_inputs_pin(path: Path) -> Dict[str, str]:
    files: Dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        parts = text.split()
        if len(parts) < 2:
            raise SystemExit(f"error: malformed GOLDEN_INPUTS pin line: {line!r}")
        digest, name = parts[0], parts[-1].lstrip("*")
        files[Path(name).name] = digest
    return files


def expected_golden_inputs_pin() -> Dict[str, str]:
    expected: Dict[str, str] = {}
    for rec in AUTHORITY_FILES:
        if rec.role == "derived":
            if not rec.cell_sha256:
                raise SystemExit(
                    f"error: derived authority {rec.filename} missing cell pin"
                )
            expected[ENTRY_XLSX_PIN_NAME] = rec.cell_sha256
        elif rec.sha256:
            expected[rec.filename] = rec.sha256
    return expected


def verify_golden_inputs_pin(pin_path: Path) -> None:
    files = parse_golden_inputs_pin(pin_path)
    expected = expected_golden_inputs_pin()
    got, want = set(files), set(expected)
    if got != want:
        raise SystemExit(
            "error: GOLDEN_INPUTS.sha256 pin mismatch "
            f"missing={sorted(want - got)} extra={sorted(got - want)}"
        )
    for name, digest in expected.items():
        if files[name] != digest:
            raise SystemExit(
                f"error: GOLDEN_INPUTS.sha256 {name} {files[name]} != pinned {digest}"
            )


def expected_catalog_golden_inputs_pin() -> Dict[str, str]:
    expected: Dict[str, str] = {}
    for rec in AUTHORITY_FILES:
        if rec.filename not in CATALOG_PIN_FILES:
            continue
        if not rec.sha256:
            raise SystemExit(
                f"error: catalog authority {rec.filename} missing byte pin"
            )
        expected[rec.filename] = rec.sha256
    if set(expected) != set(CATALOG_PIN_FILES):
        raise SystemExit(
            "error: catalog pin set drifted from AUTHORITY_FILES "
            f"got={sorted(expected)} want={list(CATALOG_PIN_FILES)}"
        )
    return expected


def _monorepo_root() -> Path:
    # llvm/lib/Target/Haydn/FormatE/family_core.py -> repo root
    return Path(__file__).resolve().parents[5]


def in_tree_catalog_golden_inputs_pin() -> Path:
    return _monorepo_root() / IN_TREE_CATALOG_PIN_REL


def catalog_golden_inputs_pin_paths() -> Tuple[Path, ...]:
    """Every catalog pin that must match the six-file product FILE hash."""
    found: list = []
    seen = set()

    def add(path: Path, *, required: bool) -> None:
        if not path.is_file():
            if required:
                raise SystemExit(
                    f"error: catalog GOLDEN_INPUTS.sha256 is not a file: {path}"
                )
            return
        key = path.resolve()
        if key in seen:
            return
        seen.add(key)
        found.append(path)

    env = os.environ.get("HAYDN_CATALOG_GOLDEN_INPUTS_PIN")
    if env:
        add(Path(env), required=True)
    add(in_tree_catalog_golden_inputs_pin(), required=True)
    root = os.environ.get("BUNDLESIM_ROOT")
    if root:
        add(
            Path(root)
            / "bundlesim"
            / "isa"
            / "database"
            / "generated"
            / "GOLDEN_INPUTS.sha256",
            required=True,
        )
    if not found:
        raise SystemExit(
            "error: catalog GOLDEN_INPUTS.sha256 pin not found "
            "(six-file product pin; unused/derived stay on the compiler pin)"
        )
    return tuple(found)


def catalog_golden_inputs_pin_path() -> Path:
    return catalog_golden_inputs_pin_paths()[0]


def catalog_pin_file_text() -> str:
    """Exact product catalog pin bytes: six digest lines, no comments."""
    expected = expected_catalog_golden_inputs_pin()
    lines = [f"{expected[name]}  {name}" for name in CATALOG_PIN_FILES]
    return "\n".join(lines) + "\n"


def verify_catalog_golden_inputs_pin(
    pin_path: Path, *, check_file_sha256: bool = False
) -> None:
    """Fail closed if a catalog pin copies unused/derived/retired rows."""
    files = parse_golden_inputs_pin(pin_path)
    got = set(files)
    retired = sorted(
        n
        for n in got
        if n in RETIRED_CATALOG_BASENAMES or n in COMPILER_ONLY_PIN_NAMES
    )
    if retired:
        raise SystemExit(
            "error: catalog GOLDEN_INPUTS.sha256 lists retired/unused rows "
            f"{retired}; catalog pin is six-file, unused/derived stay on the compiler pin"
        )
    expected = expected_catalog_golden_inputs_pin()
    want = set(expected)
    if got != want:
        raise SystemExit(
            "error: catalog GOLDEN_INPUTS.sha256 pin mismatch "
            f"missing={sorted(want - got)} extra={sorted(got - want)}"
        )
    for name, digest in expected.items():
        if files[name] != digest:
            raise SystemExit(
                f"error: catalog GOLDEN_INPUTS.sha256 {name} "
                f"{files[name]} != pinned {digest}"
            )
    if check_file_sha256:
        got = sha256_file(pin_path)
        if got != CATALOG_PIN_FILE_SHA256:
            raise SystemExit(
                "error: catalog GOLDEN_INPUTS.sha256 file sha256 "
                f"{got} != product pin {CATALOG_PIN_FILE_SHA256} "
                "(comment/whitespace rewrites are content drift; "
                "keep six digest lines only)"
            )


def prove_catalog_pin_refuses_retired() -> None:
    try:
        verify_catalog_golden_inputs_pin(golden_inputs_pin_path())
    except SystemExit as exc:
        msg = str(exc)
        if "retired/unused rows" in msg:
            print("OK catalog pin refuses retired/unused rows")
            return
        raise SystemExit(
            f"error: catalog retired-row probe failed unexpectedly: {msg}"
        ) from exc
    raise SystemExit(
        "error: nine-file compiler pin was accepted as a catalog pin"
    )


def _write_temp_pin(rows: Dict[str, str]) -> Path:
    handle = tempfile.NamedTemporaryFile(
        "w", suffix=".sha256", delete=False, encoding="utf-8"
    )
    with handle:
        for name, digest in rows.items():
            handle.write(f"{digest}  {name}\n")
    return Path(handle.name)


def prove_catalog_six_file_pin_ok() -> None:
    expected = expected_catalog_golden_inputs_pin()
    path = _write_temp_pin({name: expected[name] for name in CATALOG_PIN_FILES})
    try:
        verify_catalog_golden_inputs_pin(path)
    finally:
        path.unlink(missing_ok=True)
    print("OK catalog pin six-file")


def prove_catalog_pin_file_sha256() -> None:
    got = hashlib.sha256(catalog_pin_file_text().encode("utf-8")).hexdigest()
    if got != CATALOG_PIN_FILE_SHA256:
        raise SystemExit(
            "error: reconstructed catalog GOLDEN_INPUTS.sha256 "
            f"{got} != product pin {CATALOG_PIN_FILE_SHA256}"
        )
    print("OK catalog pin file sha256")


def prove_catalog_pin_comment_rewrite_is_drift() -> None:
    text = "# note\n" + catalog_pin_file_text()
    got = hashlib.sha256(text.encode("utf-8")).hexdigest()
    if got == CATALOG_PIN_FILE_SHA256:
        raise SystemExit(
            "error: commented catalog pin still matched product file pin"
        )
    handle = tempfile.NamedTemporaryFile(
        "w", suffix=".sha256", delete=False, encoding="utf-8"
    )
    with handle:
        handle.write(text)
    path = Path(handle.name)
    try:
        verify_catalog_golden_inputs_pin(path)
        try:
            verify_catalog_golden_inputs_pin(path, check_file_sha256=True)
        except SystemExit as exc:
            msg = str(exc)
            if "file sha256" in msg and "content drift" in msg:
                print("OK catalog pin comment rewrite is content drift")
                return
            raise SystemExit(
                "error: commented catalog pin probe failed unexpectedly: "
                f"{msg}"
            ) from exc
        raise SystemExit(
            "error: commented catalog pin file sha256 did not fail closed"
        )
    finally:
        path.unlink(missing_ok=True)


def prove_catalog_seventh_input_fails() -> None:
    """Unused/derived nine-file members are not a seventh catalog input."""
    expected = expected_catalog_golden_inputs_pin()
    rows = {name: expected[name] for name in CATALOG_PIN_FILES}
    rows["operands_info.md"] = PINNED_OPERANDS_INFO_SHA256
    path = _write_temp_pin(rows)
    try:
        verify_catalog_golden_inputs_pin(path)
    except SystemExit as exc:
        msg = str(exc)
        if "retired/unused rows" in msg and "operands_info.md" in msg:
            print("OK catalog pin refuses seventh input")
            return
        raise SystemExit(
            f"error: seventh-catalog-input probe failed unexpectedly: {msg}"
        ) from exc
    finally:
        path.unlink(missing_ok=True)
    raise SystemExit("error: seventh catalog input did not fail closed")


def prove_catalog_invented_seventh_fails() -> None:
    """An unpublished catalog basename is not a seventh provenance row."""
    expected = expected_catalog_golden_inputs_pin()
    rows = {name: expected[name] for name in CATALOG_PIN_FILES}
    rows["invented_catalog.json"] = "0" * 64
    path = _write_temp_pin(rows)
    try:
        verify_catalog_golden_inputs_pin(path)
    except SystemExit as exc:
        msg = str(exc)
        if "pin mismatch" in msg and "invented_catalog.json" in msg:
            print("OK catalog pin refuses invented seventh input")
            return
        raise SystemExit(
            f"error: invented-seventh-catalog probe failed unexpectedly: {msg}"
        ) from exc
    finally:
        path.unlink(missing_ok=True)
    raise SystemExit("error: invented seventh catalog input did not fail closed")


def check_pin_ledgers() -> None:
    """Compiler nine-file + catalog six-file pins; no golden directory."""
    verify_golden_inputs_pin(golden_inputs_pin_path())
    prove_owned_generated_authority_pins(haydn_target_dir())
    prove_catalog_pin_refuses_retired()
    prove_catalog_six_file_pin_ok()
    prove_incomplete_compiler_pin_fails()
    prove_xlsx_zip_bytes_not_authority_pin()
    prove_second_family_enum_fails()
    prove_second_family_records_fail()
    prove_catalog_pin_file_sha256()
    prove_catalog_pin_comment_rewrite_is_drift()
    prove_catalog_seventh_input_fails()
    prove_catalog_invented_seventh_fails()
    prove_unknown_family_fails()
    for catalog_pin in catalog_golden_inputs_pin_paths():
        verify_catalog_golden_inputs_pin(catalog_pin, check_file_sha256=True)
    print("OK catalog pin on-disk six-file")
    print("OK compiler pin nine-file")


def prove_incomplete_compiler_pin_fails() -> None:
    """Six-file catalog ledger is not a complete nine-file compiler pin."""
    expected = expected_catalog_golden_inputs_pin()
    path = _write_temp_pin({name: expected[name] for name in CATALOG_PIN_FILES})
    try:
        verify_golden_inputs_pin(path)
    except SystemExit as exc:
        msg = str(exc)
        if (
            "pin mismatch" in msg
            and "operands_info.md" in msg
            and "instruction_type_operands.json" in msg
            and ENTRY_XLSX_PIN_NAME in msg
        ):
            print("OK incomplete compiler pin fail-closed")
            return
        raise SystemExit(
            f"error: incomplete-compiler-pin probe failed unexpectedly: {msg}"
        ) from exc
    finally:
        path.unlink(missing_ok=True)
    raise SystemExit("error: six-file compiler pin did not fail closed")


def prove_second_family_enum_fails() -> None:
    """A second BundleFamily enumerator is not an inert family handle."""
    try:
        _check_family_handle_header(
            "enum class BundleFamily : uint8_t {\n"
            "  E96 = 0,\n"
            "  MF0 = 1,\n"
            "};\n"
            "inline constexpr BundleFamily kAdmittedFamily = BundleFamily::E96;\n"
            "inline FamilyRecords getFamilyRecords(BundleFamily Family) {\n"
            "  if (Family != BundleFamily::E96)\n"
            '    llvm_unreachable("Haydn: no admitted bundle-format family besides E96");\n'
            "}\n"
        )
    except SystemExit as exc:
        msg = str(exc)
        if "E96-only" in msg and "MF0" in msg:
            print("OK second-family enum fail-closed")
            return
        raise SystemExit(
            f"error: second-family-enum probe failed unexpectedly: {msg}"
        ) from exc
    raise SystemExit("error: second family enum did not fail closed")


def prove_second_family_records_fail() -> None:
    """AdmittedFamilyCount>1 is not an inert E96 table."""
    try:
        _check_records_family_column(
            "static constexpr uint8_t FormatEFamilyId = 0u;\n"
            "static constexpr unsigned FormatEAdmittedFamilyCount = 2u;\n"
            "struct FormatEMemberRec { uint8_t Family; };\n"
            "static constexpr FormatEMemberRec FormatEMembers[] = {\n"
            '  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "X", "T", "X"},\n'
            "};\n"
        )
    except SystemExit as exc:
        msg = str(exc)
        if "FormatEAdmittedFamilyCount must be 1" in msg:
            print("OK second-family records fail-closed")
            return
        raise SystemExit(
            f"error: second-family-records probe failed unexpectedly: {msg}"
        ) from exc
    raise SystemExit("error: second family records did not fail closed")


def prove_matcher_root_member_include_fails() -> None:
    """A matcher include of Format E members is not a collapsed root."""
    try:
        _check_matcher_root_text(
            'include "HaydnGeneric.td"\ninclude "HaydnFormatE.td"\n'
        )
    except SystemExit as exc:
        msg = str(exc)
        if "matcher root includes" in msg and "HaydnFormatE.td" in msg:
            print("OK matcher-root member include fail-closed")
            return
        raise SystemExit(
            f"error: matcher member-include probe failed unexpectedly: {msg}"
        ) from exc
    raise SystemExit("error: matcher root member include did not fail closed")


def prove_unknown_family_fails() -> None:
    """Only e96 is admitted; a second family name is not a generator instance."""
    try:
        get_family("mf0")
    except SystemExit as exc:
        msg = str(exc)
        if "unknown family" in msg and "mf0" in msg:
            print("OK unknown family fail-closed")
            return
        raise SystemExit(
            f"error: unknown-family probe failed unexpectedly: {msg}"
        ) from exc
    raise SystemExit("error: unknown family did not fail closed")


def prove_unpinned_golden_dir_file_fails() -> None:
    """A new authority-shaped file next to the nine-file set is unpinned."""
    with tempfile.TemporaryDirectory() as tmp:
        extra = Path(tmp) / "invented_authority.json"
        extra.write_text("{}\n", encoding="utf-8")
        try:
            verify_golden_dir_no_unpinned(Path(tmp))
        except SystemExit as exc:
            msg = str(exc)
            if "unpinned authority input" in msg and "invented_authority.json" in msg:
                print("OK unpinned golden-dir file fail-closed")
                return
            raise SystemExit(
                f"error: unpinned-golden-dir probe failed unexpectedly: {msg}"
            ) from exc
    raise SystemExit("error: unpinned golden-dir file did not fail closed")


def prove_xlsx_zip_bytes_not_authority_pin() -> None:
    """Derived xlsx must be cell-pinned; ZIP-byte filename is not admitted."""
    expected = expected_golden_inputs_pin()
    rows = {
        name: digest
        for name, digest in expected.items()
        if name != ENTRY_XLSX_PIN_NAME
    }
    rows["instruction_to_entry.xlsx"] = "0" * 64
    path = _write_temp_pin(rows)
    try:
        verify_golden_inputs_pin(path)
    except SystemExit as exc:
        msg = str(exc)
        if "pin mismatch" in msg and "instruction_to_entry.xlsx" in msg:
            print("OK xlsx ZIP-byte pin rejected")
            return
        raise SystemExit(
            f"error: xlsx-byte-pin probe failed unexpectedly: {msg}"
        ) from exc
    finally:
        path.unlink(missing_ok=True)
    raise SystemExit("error: xlsx ZIP-byte pin was accepted")


def prove_unpinned_consumed_fails(golden: Path) -> None:
    try:
        verify_authority_inputs(golden, ["invented_authority.json"])
    except SystemExit as exc:
        msg = str(exc)
        if "unpinned authority input" in msg and "invented_authority.json" in msg:
            print("OK unpinned authority input fail-closed")
            return
        raise SystemExit(
            f"error: unpinned probe failed unexpectedly: {msg}"
        ) from exc
    raise SystemExit("error: unpinned authority input did not fail closed")


def prove_derived_xlsx_not_authority(golden: Path) -> None:
    try:
        verify_authority_inputs(golden, ["instruction_to_entry.xlsx"])
    except SystemExit as exc:
        if "not a hardware-fact authority" in str(exc):
            print("OK derived instruction_to_entry.xlsx refused")
            return
        raise SystemExit(
            f"error: derived-xlsx probe failed unexpectedly: {exc}"
        ) from exc
    raise SystemExit("error: derived xlsx consume did not fail closed")


def prove_unused_authority_not_consumed(golden: Path) -> None:
    unused = [rec.filename for rec in AUTHORITY_FILES if rec.role == "unused"]
    if not unused:
        raise SystemExit("error: no unused nine-file members to probe")
    for name in unused:
        try:
            verify_authority_inputs(golden, [name])
        except SystemExit as exc:
            msg = str(exc)
            if "unused nine-file member" in msg and name in msg:
                continue
            raise SystemExit(
                f"error: unused-authority probe failed unexpectedly: {msg}"
            ) from exc
        raise SystemExit(
            f"error: unused authority consume did not fail closed: {name}"
        )
    print("OK unused authority input refused")


_ZIP_BYTE_ENTRY_PIN_RE = re.compile(
    r"PIN:\s+[0-9a-fA-F]{64}\s+instruction_to_entry\.xlsx\s*$", re.M
)


def prove_text_has_authority_pins(text: str, label: str) -> None:
    """Fail closed unless *text* stamps every nine-file pin (cell, not ZIP)."""
    if ENTRY_XLSX_PIN_NAME not in text:
        raise SystemExit(
            f"error: {label} missing cell pin "
            "(xlsx ZIP bytes are not authority)"
        )
    if _ZIP_BYTE_ENTRY_PIN_RE.search(text):
        raise SystemExit(
            f"error: {label} pins instruction_to_entry.xlsx ZIP bytes"
        )
    expected = expected_golden_inputs_pin()
    for name, digest in expected.items():
        if digest not in text or name not in text:
            raise SystemExit(f"error: {label} missing nine-file pin {name}")


def haydn_target_dir() -> Path:
    return Path(__file__).resolve().parents[1]


def prove_owned_generated_authority_pins(haydn_dir: Path) -> None:
    """Member and logical generated files stamp the compiler nine-file pin."""
    owned = (
        haydn_dir / "HaydnFormatsE96Members.td.inc",
        haydn_dir / "HaydnInstrInfoGolden.td.inc",
    )
    for path in owned:
        if not path.is_file():
            raise SystemExit(f"error: owned generated file missing: {path}")
        prove_text_has_authority_pins(
            path.read_text(encoding="utf-8"), path.name
        )
    print("OK owned generated nine-file pin stamp")


def prove_unpublished_choice_fails(golden: Path) -> None:
    try:
        verify_authority_inputs(golden, ["top_pad_encode.json"])
    except SystemExit as exc:
        msg = str(exc)
        if "unpublished encoding choice" in msg and "top_pad_encode.json" in msg:
            print("OK unpublished encoding choice fail-closed")
            return
        raise SystemExit(
            f"error: unpublished probe failed unexpectedly: {msg}"
        ) from exc
    raise SystemExit("error: unpublished encoding choice did not fail closed")


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    add_family_argument(ap)
    ap.add_argument(
        "--check",
        action="store_true",
        help="Verify nine-file authority pins and cutover surfaces (no write)",
    )
    ap.add_argument(
        "--pin-check",
        action="store_true",
        help="Verify compiler nine-file and catalog six-file pins (no golden dir)",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Target Haydn directory (default: llvm/lib/Target/Haydn)",
    )
    args = ap.parse_args(argv)
    if not args.check and not args.pin_check:
        ap.error("--check or --pin-check is required")
    if args.pin_check and not args.check:
        try:
            check_pin_ledgers()
        except SystemExit as exc:
            msg = str(exc)
            if msg:
                print(msg, file=sys.stderr)
            return 2 if msg else 0
        return 0
    family = get_family(args.family)
    golden = resolve_golden_dir(family)
    consumed = [rec.filename for rec in AUTHORITY_FILES if rec.role == "consumed"]
    try:
        verify_authority_inputs(golden, consumed)
        check_cutover_surfaces(args.out_dir)
        print("OK matcher-root collapse")
        print("OK Manual.td tombstone")
        print("OK residual hand logicals")
        print("OK FormatsE96 tombstone")
        prove_matcher_root_member_include_fails()
        prove_second_family_enum_fails()
        prove_second_family_records_fail()
        prove_unknown_family_fails()
        prove_unpinned_consumed_fails(golden)
        prove_derived_xlsx_not_authority(golden)
        prove_unused_authority_not_consumed(golden)
        prove_unpublished_choice_fails(golden)
        prove_unpinned_golden_dir_file_fails()
        check_pin_ledgers()
    except SystemExit as exc:
        msg = str(exc)
        if msg:
            print(msg, file=sys.stderr)
        return 2 if msg else 0
    pinned = sum(1 for rec in AUTHORITY_FILES if rec.sha256)
    derived = sum(1 for rec in AUTHORITY_FILES if rec.role == "derived")
    unused = sum(1 for rec in AUTHORITY_FILES if rec.role == "unused")
    consumed_n = sum(1 for rec in AUTHORITY_FILES if rec.role == "consumed")
    print(
        f"OK authority pins files={len(AUTHORITY_FILES)} "
        f"pinned={pinned} consumed={consumed_n} unused={unused} "
        f"derived={derived} cell_pinned={derived}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
