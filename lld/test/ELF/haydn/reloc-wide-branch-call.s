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
# The DISPLACEMENT is anchored at the BUNDLE, not at that byte. The database
# says `PC = PC + imm20` and, in the same Behavior, `rt = PC_next_bundle` —
# the PC unit is the bundle, and a machine with no per-entry PC cannot have a
# per-entry branch origin. If it did, the same branch to the same label would
# encode a different displacement depending on which entry the packer chose.
# The emitter folds the entry's byte base back into the addend so the two
# cancel; see the fixup translation in HaydnMCCodeEmitter.
#
# So the values are derivable, which is what makes this a real check rather
# than a recording: callee is at 0x10018, the jal's bundle is at 0x10000 so
# its offset is 0x18 = 24, and the beq's bundle is at 0x1000c so its offset
# is 12. Both land exactly on callee.
#
# Those are BYTE offsets. The field holds half of each (§ 5.14 D1 puts branch
# offsets in 2-byte units) and the decoder shifts back, so the disassembly
# reads in bytes — which is the convention § 6.11 says BundleSim depends on.
#
# This test has now recorded the tools twice instead of deriving from the ISA.
# It first asserted 10 and 4, the raw field values, before the branch operand
# classes carried their scale (§ 6.10). It then asserted 20 and 8, which is
# this bundle's addresses measured from the relocation byte — both of which
# resolve to 0x10014, four bytes short of callee and inside the previous
# bundle. Derive the answer from the Behavior; do not paste what objdump said.

# RELOCS:      Relocations [
# RELOCS-NEXT:   Section ({{.*}}) .rela.text {
# The addend is the entry's byte base, +4 for both. It is what cancels the
# same base in the relocation's own address so that lld's S + A - P comes out
# bundle-relative: callee + 4 - (bundle + 4).
# RELOCS-DAG:      0x4 R_HAYDN_WIDE_CallSImm20 callee 0x4
# RELOCS-DAG:      0x10 R_HAYDN_WIDE_BranchSImm12{{(_RI)?}} callee 0x4
# RELOCS:        }
# RELOCS-NEXT: ]

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # CHECK: 10000: {{.*}} jal	lr, 24
    jal lr, callee

    # CHECK: 1000c: {{.*}} beq	r1, r2, 12
    beq r1, r2, callee

.globl callee
callee:
    # CHECK-LABEL: <callee>:
    # CHECK: {{.*}} add32
    { add32 r1, r2, r3 }
    .size callee, .-callee

    .size _start, .-_start
