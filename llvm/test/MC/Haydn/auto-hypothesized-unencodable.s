# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: FileCheck --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfoManual.td %s \
# RUN:   --check-prefix=TD
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: hypothesized Auto.td encodings (FmtALU64 ADD64S Inst{})
# must not bare-encode to zeros. Auto.td was renamed then tombstoned.
#
# Bug: Auto.td logicals carried live hypothesized Inst{} in the matcher and
# decoder. Runtime bare-encode ban protected CodeGen, but llvm-mc could still
# select the Auto logical and emit the hypothesized (or all-zero) word.
#
# Fix: non-catalog Auto.td stays isCodeGenOnly=1. Catalog logicals
# (ADD64S) are matcher-visible with unused DecoderNamespace so hypothesized
# Inst{} is not in a live decode trie. Bundle encode is Format E members,
# one 12-byte parcel, not all-zero.
#
# Test design: braced add64s + nop pad. TD FileCheck pins the outer
# quarantine wrap. CHECK-NOT all-zero encoding.

# TD: Former hand-maintained hypothesized encodings
# TD: generate_format_e_records.py does not emit this file
# TD-NOT: Auto-generated from spec JSON
# CHECK: add64s{{.*}}encoding: [0x{{[1-9a-f][0-9a-f]|[0-9a-f][1-9a-f]}},{{.*}}]
# CHECK-NOT: encoding: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]

{ add64s d0, d1, d2; nop; nop }
