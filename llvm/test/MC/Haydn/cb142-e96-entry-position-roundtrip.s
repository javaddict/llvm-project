# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target
#
# REGRESSION (CB-142 / #10, Format E native): bundle TEXT names the entry.
# High entry first, right-aligned on e0:
#   `{ a; b; c }` = e2,e1,e0     `{ a; b }` = e1,e0     `{ a }` = e0
# Printer and parser share that order so `llc -S | llvm-mc` can reproduce
# encode-order e0..eN. Encoding is untouched (operand dag stays $e0..$eN).
#
# BundleSim text order is updated in its own tree — this lit only covers
# LLVM print/parse round-trip.

.text

# CHECK-LABEL: <f_cb142_three_entry_high_first>:
# Three real ops: text high-first maps to e2,e1,e0 and must re-print the same.
# CHECK: { add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }
f_cb142_three_entry_high_first:
  { add64 d0, d1, d2; add64 d3, d4, d5; add32 r0, r1, r2 }

# CHECK-LABEL: <f_cb142_two_entry_high_first>:
# Encoder may promote E2→E3 with a high-entry nop pad; reals stay high-first.
# CHECK: { {{(nop; )?}}add64 d0, d1, d2; add32 r0, r1, r2 }
f_cb142_two_entry_high_first:
  { add64 d0, d1, d2; add32 r0, r1, r2 }

# CHECK-LABEL: <f_cb142_nop_holds_position>:
# Explicit nop is a position holder: real op is last text entry → e0.
# CHECK: { nop; add32 r0, r1, r2 }
f_cb142_nop_holds_position:
  { nop; add32 r0, r1, r2 }

# CHECK-LABEL: <f_cb142_single_entry>:
# Single-entry carries no reverse: lands e0 (high-entry nop pad on print).
# CHECK: { nop; lui r1, 1 }
# CHECK: { nop; lui r1, 1 }
f_cb142_single_entry:
  { lui r1, 1 }
  { nop; lui r1, 1 }

# CHECK-LABEL: <f_cb142_legacy_post_names>:
# st32_post / st64_post must assemble (d463: DecoderNamespace not isCodeGenOnly)
# and disassemble under the canonical DB mnemonic.
# CHECK: { {{.*}}s_sw_post_imm{{.*}}
# CHECK: { {{.*}}d_sdw_post_imm{{.*}}
f_cb142_legacy_post_names:
  { st32_post r6, r7, 4 }
  { st64_post d0, r7, 8 }
