# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
#
# REGRESSION TEST (L228): lld must DISPATCH WIDE branch/call relocations
# through getRelExpr / getImplicitAddend / inBranchRange / needsThunk.
#
# Format E: every bundle is 12 bytes. WIDE jal / beq still emit
# R_HAYDN_WIDE_CallSImm20 / R_HAYDN_WIDE_BranchSImm12.
#
# The reloc offsets are 0x4 and 0x10, NOT the bundle addresses. A relocation
# is anchored at its ENTRY's byte base within the bundle, and both of these
# land in entry 1 of a 3-entry bundle, whose base is +4 (entries start at
# bundle bits 6, 37 and 68 -> byte bases 0, 4, 8). So 0x0+4 and 0xc+4. See
# FORMAT-E-SWITCH-PLAN.md 5.8.
#
# The branch VALUES are derivable, which is what makes this a real check
# rather than a recording: callee is at 0x10018, jal is fixed up at 0x10004 so
# its offset is 0x14 = 20, and beq is fixed up at 0x10010 so its offset is 8.
#
# Those are BYTE offsets. The field holds half of each (§ 5.14 D1 puts branch
# offsets in 2-byte units) and the decoder shifts back, so the disassembly
# reads in bytes — which is the convention § 6.11 says BundleSim depends on.
# An earlier revision of this test asserted 10 and 4, the raw field values,
# because it was written before the branch operand classes carried their
# scale (§ 6.10) and simply recorded what the disassembler then printed.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# RELOCS-DAG:      0x4 R_HAYDN_WIDE_CallSImm20 callee 0x0
# RELOCS-DAG:      0x10 R_HAYDN_WIDE_BranchSImm12{{(_RI)?}} callee 0x0
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: 10000: {{.*}} jal	lr, 20
    jal lr, callee

    # CHECK: 1000c: {{.*}} beq	r1, r2, 8
    beq r1, r2, callee

.globl callee
callee:
    # CHECK-LABEL: <callee>:
    # CHECK: {{.*}} add32
    { add32 r1, r2, r3 }
    .size callee, .-callee

    .size _start, .-_start
