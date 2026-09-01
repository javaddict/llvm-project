# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_format_e_records.py --check
# RUN: FileCheck %s --check-prefix=LSB --input-file=%S/../../../lib/Target/Haydn/HaydnGenRelocFieldLsb.inc
# REQUIRES: haydn-registered-target
# REQUIRES: haydn-golden-canonical
#
# REGRESSION TEST: T-TII2 / R1 — importer --check is the A0 fail-closed gate
# for generated Format E records, XLSX↔JSON layout parity, the canonical-
# vector ledger round-trip, and reloc FieldLsbSites / ExtraPublishedLsb
# (HaydnGenRelocFieldLsb.inc). RelocKind / scale / ELF rows and Loc sniff
# stay in HaydnRelocLayout; this file only ratchets generated member LSBs.
#
# Bug: generate_format_e_records.py --check existed but was unwired (zero
# CMake consumers beyond this lit), the golden XLSX was hash-pinned never
# parsed, and format_e_canonical_vectors_v1.json had zero consumers. Hand
# edits to the generated files (including reloc FieldLsb sites) survived
# build+unit tests. HaydnGenRelocFieldLsb.inc was #included but untracked.
# Fix: --check diffs committed outputs (including HaydnGenRelocFieldLsb.inc),
# parses the XLSX (zip/xml, no invented golden), round-trips every ledger
# entry (null stays null; MALFORMED_DEFINED / ILLUSTRATION_ONLY hex
# encode→decode), pack/unpacks every generated member, and checks generated
# td imm width/signedness against golden instruction annotations. A one-byte
# flip of a generated file fails this RUN. The LSB FileCheck below proves
# the reloc inc is generator-stamped and carries ExtraPublishedLsb (HWLRIII
# Off1@13 / Off2@36) without leaking Loc sniff into the table.
# If this regresses: generated TD/INC silently drift from golden, reloc
# FieldLsb sites become a hand-authored parallel table again, or XLSX/JSON
# / canonical-vector mismatch is invisible again.
#
# Role: generator-check — A0 fail-closed parity gate (NOT an object/encode
# contract and NOT llvm-mc; ledger may_drive_llvm_mc_encode is false).
# Object encode→obj→disasm lives in sibling MC lits.
#
# LSB: Generator: llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py
# LSB: GET_HAYDN_RELOC_FIELD_LSB
# LSB: FieldLsbSites
# LSB: ExtraPublishedLsb
# LSB: {RelocKind::HWLoopOff1, 13},
# LSB: {RelocKind::HWLoopOff2, 36},
# LSB-NOT: resolveFieldLsb
