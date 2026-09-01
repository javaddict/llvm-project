# REQUIRES: haydn-registered-target
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=DUAL_CSRR=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=DUAL-CSRR %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=CSRW_CSRR=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=CSRW-CSRR %s
# RUN: not llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=DUAL_CSRR_LOCAL=1 %s \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=DUAL-LOCAL %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj --defsym=UNIQUE=1 %s \
# RUN:   -o %t.uniq.o
# RUN: llvm-readobj -r %t.uniq.o | FileCheck --check-prefix=UNIQUE %s

# REGRESSION TEST: D1.5 — dual CSR I8 at one parcel r_offset must refuse.
#
# Bug: two symbolic CSRR (or CSRW+CSRR) I8 members in one parcel both
# emit a CSR_UImm8-family fixup at offset 0. lld resolves one I8 window
# and patches it twice; the other field stays 0. Dual CSRR is golden-legal
# SFR 2R1W (hand-asm), so this is a named MC refusal, not a silent reloc
# pair.
#
# D1.17: unique non-default E3 CSR sites now emit ENTRY-QUALIFIED kinds
# (typed window; no sniff). The D1.5 wall still refuses duplicates by
# baseKindFor — two CSR fixups at two qualified entries of one parcel
# still trip it (that comparison must not regress, or { csrr@e1;
# csrr@e2 } would silently link wrong). LLVM ERROR is non-abort (plain
# `not`, not `--crash`). reloc-csr-uimm8.s is the unique-CSR pin.

# DUAL-CSRR: LLVM ERROR:{{.*}}dual CSR_UImm8
# DUAL-CSRR-SAME: {{.*}}r_offset
# DUAL-CSRR-NOT: R_HAYDN_CSR_UImm8

# CSRW-CSRR: LLVM ERROR:{{.*}}dual CSR_UImm8
# CSRW-CSRR-SAME: {{.*}}r_offset
# CSRW-CSRR-NOT: R_HAYDN_CSR_UImm8

# DUAL-LOCAL: LLVM ERROR:{{.*}}dual CSR_UImm8
# DUAL-LOCAL-SAME: {{.*}}r_offset
# DUAL-LOCAL-NOT: R_HAYDN_CSR_UImm8

.ifdef DUAL_CSRR
.text
  { csrr r1, g0; csrr r2, g1; nop }
.endif

.ifdef CSRW_CSRR
.text
  { csrw g0, r3; csrr r2, g1; nop }
.endif

.ifdef DUAL_CSRR_LOCAL
.text
loc0:
  { add32 r0, r0, r0 }
loc1:
  { add32 r1, r1, r1 }
  { csrr r2, loc0; csrr r3, loc1; nop }
.endif

.ifdef UNIQUE
.text
  { nop; nop; csrr r1, ext_csr }
  { nop; csrr r2, ext_csr; nop }
  { csrr r3, ext_csr; nop; nop }
# UNIQUE: R_HAYDN_CSR_UImm8 ext_csr
.endif
