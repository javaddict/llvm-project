# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o
# RUN: llvm-readelf -r %t.o | FileCheck %s --check-prefix=RELOC
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj -defsym=DEFINE_TARGET=1 %s -o %t.target.o
# RUN: ld.lld -m elf32haydn -e 0 --image-base=0x10 --section-start=.text=0x10 %t.target.o %t.o -o %t.elf
# RUN: llvm-objdump -d --no-show-raw-insn %t.elf | FileCheck %s --check-prefix=LINKED

# Role: object — W25 (encoding F15 residual): LO20/PC_LO20/LS_IMM FieldLsb
# must follow the committed Format E entry window, not the fixed E2-e0 table.
#
# REGRESSION TEST: entry-aware FieldLsb for the three residual kinds.
#
# Bug: after HI12 gained E3 entry-aware FieldLsb (e3-lui-hi12-fieldlsb.s),
# LO20/PC_LO20 (ALU RI20, E2-only type) and LS_IMM (LS RI6) still patched
# from the E2-e0-only table (LO20 FieldLsb=31 NBytes=8, LS_IMM FieldLsb=28
# NBytes=6 — both windows too small to reach the real non-e0 fields). A
# symbolic ADDI32 at E2 e1 ALU1 (imm @ parcel bits[84:65]) or a symbolic
# S_LW_WITH_IMM at E3 e0/e1/e2 or E2 e1 LOAD1 patched 20/6 bits into the
# WRONG entry of the parcel and left the executed imm zero — in BOTH MC
# applyFixup and lld relocate (they share resolveFieldLsb).
#
# Fix: resolveFieldLsb derives each site's window from the parcel header +
# golden map/type keys (same mechanism as HI12 / WIDE branch / JAL):
#   LO20/PC_LO20: E2 e0 ALU0 @31 (table default); E2 e1 ALU1 @65
#   LS_IMM: E2 e0 LOADSTORE0 @28 (default); E2 e1 LOAD1 @72;
#           E3 e0 LOADSTORE0 @25; E3 e1 LOAD1 @54; E3 e2 LOAD1 @85
# NBytes: LO20/PC_LO20 8→12, LS_IMM 6→12 (patchField bit-walks past bit 63).
#
# Test design: symbolic operands force the reloc path (constants would
# resolve in the encoder). A second real op in each bundle forces the
# RI20/RI6 member onto a non-e0 entry (placement pinned by the const twins
# `{ xor32 r0,r0,r0; addi32 … }` → addi32@e1, and the three s_lw bundle
# orders → e0/e1/e2). Cross-object link (the hwloop-offset-reloc-shift.s
# shape) drives the same rows through lld relocate/getImplicitAddend.
# The image is pinned at 0x10 so the LS_IMM R_ABS value (xslot VA = 0x10)
# fits the signed imm6 window: target object first → xslot=0x10(16),
# xdata=0x1c(28); the second main parcel sits at 0x34(52), so
# R_HAYDN_PC_LO20 (R_PC) field = lo20(28-52) = -24. If FieldLsb regresses
# to the E2-e0 table, LINKED prints immediates 0 (or lld fails the LS_IMM
# range check from the corrupted implicit addend, as it did pre-fix).

# RELOC: R_HAYDN_LO20{{.*}} xdata
# RELOC: R_HAYDN_PC_LO20{{.*}} xdata
# RELOC: R_HAYDN_LS_IMM{{.*}} xslot

# LINKED: addi32{{.*}}r1, r2, 28
# LINKED: addi32{{.*}}r3, r4, -24
# LINKED: ld32{{.*}}r5, r6, 16
# LINKED: ld32{{.*}}r7, r8, 16
# LINKED: ld32{{.*}}r9, r10, 16

.ifndef DEFINE_TARGET

.text
.globl _start
_start:
# E2: addi32 lands at e1 ALU1 → LO20 window @65 (was 31).
  { xor32 r0, r0, r0; addi32 r1, r2, %lo20(xdata) }
  { xor32 r0, r0, r0; addi32 r3, r4, %pc_lo20(xdata) }
# E3: s_lw_with_imm at e0 LOADSTORE0 (@25), e1 LOAD1 (@54), e2 LOAD1 (@85).
# NOP pad holds the textual entry without a third GPR write / dest WAW.
  { nop; nop; s_lw_with_imm r5, r6, xslot }
  { nop; s_lw_with_imm r7, r8, xslot; nop }
  { s_lw_with_imm r9, r10, xslot; nop; nop }

.else

# Target object links FIRST at the pinned base: xslot = 0x10 (16, one
# parcel before xdata = 0x1c). Both fit the reloc windows above.
.text
.globl xdata, xslot
xslot:
  { xor32 r0, r0, r0 }
xdata:
  { xor32 r0, r0, r0 }

.endif
