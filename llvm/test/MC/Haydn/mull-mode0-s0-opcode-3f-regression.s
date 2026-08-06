# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:     llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s
# REQUIRES: haydn-registered-target

# Role: object — MULL family (mull/mulssh/mulsuh/muluuh) Format E encode + disasm.

# REGRESSION: MULL family must encode as Format E parcels and round-trip to the
# correct mnemonic. Historical bug: these were unencodable FmtALU64 parcels with
# no Mode-0 map entry. Product path is Format E; each variant occupies one
# 12-byte Format E parcel and must not decode as <unknown>.
#
# Tied-destructive form: asm writes rs2 == rd (e.g. mull r0, r1, r0). The
# encoder packs the tied source; the decoder reconstructs it.

# CHECK-LABEL: <test_mull_mode0>:
# CHECK: {{.*}}0: 47 01 08 01 00 00 00 00 00 00 00 00  	{ 		mull	r0, r1, r0; 	nop }
# CHECK: c: 47 09 08 01 00 00 00 00 00 00 00 00  	{ 		mulssh	r0, r1, r0; 	nop }
# CHECK: {{.*}}18: 47 11 08 01 00 00 00 00 00 00 00 00  	{ 		mulsuh	r0, r1, r0; 	nop }
# CHECK: {{.*}}24: 47 19 08 01 00 00 00 00 00 00 00 00  	{ 		muluuh	r0, r1, r0; 	nop }
# COM: CHECK: 20: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 30 80
# COM: CHECK: mulsuh{{.*}}r0, r1, r0
# COM: CHECK: 30: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 40 80
# COM: CHECK: muluuh{{.*}}r0, r1, r0
# CHECK-NOT: <?>
# CHECK-NOT: <unknown>

.text
.globl test_mull_mode0
test_mull_mode0:
  mull r0, r1, r0
  mulssh r0, r1, r0
  mulsuh r0, r1, r0
  muluuh r0, r1, r0
