# REQUIRES: haydn-registered-target
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
# RUN:   FileCheck %s

# REGRESSION TEST : Bundle128 ADDI32 imm20 must decode/print as signed.
#
# LLD long-call thunks emit LUI+ADDI32 with the MIPS-style HI12/LO20 split:
# HI12 = (VA + 0x80000) >> 20
# LO20 = VA - (HI12 << 20) // signed 20-bit; ADDI32 sign-extends
# When bit 19 of the residual is set, LO20 is negative (e.g. VA=0xDF9B0 →
# HI=1, LO=-132688, field bits 0xDF9B0). Without DecoderMethod on `simm20`
# tblgen zero-extends and objdump prints 915888. BundleSim's ELF frontend
# parses llvm-objdump text and catalog-rejects |imm| > 524287 as
# ILLEGAL_INSTRUCTION at pc=entry (bundles=0). Mirror of (simm16)
# (simm6) / (calltarget_s0).

    .text
    .globl cb119_addi32_simm20
    .type cb119_addi32_simm20,@function
cb119_addi32_simm20:
    # LO20 residual for a far absolute address (seed6 thunk shape).
    { addi32 r12, r12, -132688; nop; nop }
    # simm20 min / max / small negative / small positive.
    { addi32 r1, r0, -524288; nop; nop }
    { addi32 r2, r0, 524287; nop; nop }
    { addi32 r3, r0, -1; nop; nop }
    { addi32 r4, r0, 1; nop; nop }
    { jalr r0, lr, 0; nop; nop }
    .size cb119_addi32_simm20, .-cb119_addi32_simm20

# CHECK-LABEL: <cb119_addi32_simm20>:
# CHECK: addi32{{.*}}r12,{{.*}}r12,{{.*}}-132688
# CHECK: addi32{{.*}}r1,{{.*}}r0,{{.*}}-524288
# CHECK: addi32{{.*}}r2,{{.*}}r0,{{.*}}524287
# CHECK: addi32{{.*}}r3,{{.*}}r0,{{.*}}-1
# CHECK: addi32{{.*}}r4,{{.*}}r0,{{.*}}1
# CHECK-NOT: 915888
# CHECK-NOT: 524288
# CHECK-NOT: 1048575
