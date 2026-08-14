# RUN: %python %S/../../../lib/Target/Haydn/FormatE/generate_format_e_records.py --check
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: T-TII2 / R1 — importer --check is the A0 fail-closed gate
# for generated Format E records, XLSX↔JSON layout parity, and the canonical-
# vector ledger round-trip.
#
# Bug: generate_format_e_records.py --check existed but was unwired (zero
# CMake consumers beyond this lit), the golden XLSX was hash-pinned never
# parsed, and format_e_canonical_vectors_v1.json had zero consumers. Hand
# edits to the four generated files survived build+unit tests.
# Fix: --check diffs committed outputs, parses the XLSX (zip/xml, no invented
# golden), round-trips every ledger entry (null stays null; MALFORMED_DEFINED
# / ILLUSTRATION_ONLY hex encode→decode), and pack/unpacks every generated
# member. A one-byte flip of a generated file fails this RUN.
# If this regresses: generated TD/INC silently drift from golden, or XLSX/JSON
# / canonical-vector mismatch is invisible again.
#
# Role: generator-check — A0 fail-closed parity gate (NOT an object/encode
# contract and NOT llvm-mc; ledger may_drive_llvm_mc_encode is false).
# Object encode→obj→disasm lives in sibling MC lits.
