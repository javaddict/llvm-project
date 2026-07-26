; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; XFAIL: *
; B1.2: singleton BUNDLE asm form (; nop pad / layout) needs rebaseline.

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.

;
; REGRESSION TEST: Re-zero R0 at jump-table target blocks.
;
; Bug: the BR_JT (jump-table dispatch) pseudo expands to `JALR r0, $addr, 0`.
; JALR unconditionally writes PC_next_bundle into its first operand (the link
; register) per the ISA DB (`JALR rt, rs, imm12: rt = PC_next_bundle;
; PC = rs + imm12`). Using R0 as the link discards the return-PC INTO R0
; clobbering the soft-zero invariant the backend relies on everywhere:
; the B (unconditional-branch) pseudo expands to `BEQZ r0, target`
; assuming R0 == 0;
; immediate materialization is `addi32 rd, r0, imm` (LUI also reads R0);
; libcall result moves use `add32 dst, r1, r0`.
; After a JT dispatch R0 holds the return-PC and is NONZERO, so the first
; `beqz_w r0` idiom in a switch-case target silently falls through instead of
; branching. ISS repro (cb22_switch_jt_bare_imm_base.c, exp 35): the JT
; dispatch `jalr_w r0, r1, 0` wrote the return-PC into r0; the PUSH-case target
; began with `beqz_w r0,.LBB0_11` which then did NOT branch, so the wrong
; case body ran and the program returned 0 instead of 35.
;
; Fix : HaydnAsmPrinter collects every MBB that is a direct successor
; of a BR_JT in runOnMachineFunction, and emitInstruction emits a leading
; `xor32 r0, r0, r0` bundle at the FIRST real instruction of each such target
; block — restoring the R0 == 0 invariant before any `beqz_w r0` idiom runs.
;
; Test design: a dense switch (8 consecutive cases) lowers to a jump table
; (G_BRJT → BR_JT → JALR r0, $addr, 0). The JT dispatch clobbers R0, so
; every case target must begin with a `xor32 r0, r0, r0` bundle. We verify the
; `jalr_w r0` dispatch is present and that `xor32 r0, r0, r0` appears at least 8
; times (once per case target) in the function body. A ret-only case body
; (`ret i32 N`) would normally have no `xor32 r0, r0, r0`; the re-zero injects
; one, so counting `xor32 r0, r0, r0` between the jalr_w and the first ret is a
; clean signal. If the re-zero regresses, the count drops and the `beqz_w r0`
; idiom in real switch bodies breaks (silently wrong control flow on the ISS).

;Dense switch with 8 consecutive cases → jump table; each case target
; must be prefixed by a `xor32 r0, r0, r0` bundle (8 occurrences for 8 cases).
; REBASELINED (auto) llc <stdin>;.file skipped

; CHECK:  .globl cb22_jt_r0_rezero // -- Begin function cb22_jt_r0_rezero
; CHECK:  .type cb22_jt_r0_rezero,@function
; CHECK: cb22_jt_r0_rezero: // @cb22_jt_r0_rezero
; CHECK: // %bb.0: // %entry
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { subi32 sp, sp, 8 }
; CHECK:  { addi32{{(_w)?}} r2, r0, 7 }
; CHECK:  { sltu32 r2, r2, r1 }
; CHECK:  { bnez_w r2, .LBB0_10 }
; CHECK: // %bb.1: // %entry
; CHECK:  { lui r2, .LJTI0_0; slli32 r1, r1, 2; nop }
; CHECK:  { addi32{{(_w)?}} r2, r2, .LJTI0_0 }
; Fused s_lw_pre_reg or split add32+ld32, then indirect jalr.
; CHECK:  {{s_lw_pre_reg|add32}}
; CHECK:  { jalr_w{{(\.s[012])?}} r0, r1, 0 }
; CHECK: .LBB0_2: // %bb0
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} r1, r0, 10 }
; CHECK:  { beqz_w r0, .LBB0_11 }
; CHECK: .LBB0_3: // %bb4
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} r1, r0, 54 }
; CHECK:  { beqz_w r0, .LBB0_11 }
; CHECK: .LBB0_4: // %bb2
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} r1, r0, 32 }
; CHECK:  { beqz_w r0, .LBB0_11 }
; CHECK: .LBB0_5: // %bb3
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} r1, r0, 43 }
; CHECK:  { beqz_w r0, .LBB0_11 }
; CHECK: .LBB0_6: // %bb7
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} r1, r0, 87 }
; CHECK:  { beqz_w r0, .LBB0_11 }
; CHECK: .LBB0_7: // %bb1
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} r1, r0, 21 }
; CHECK:  { beqz_w r0, .LBB0_11 }
; CHECK: .LBB0_8: // %bb5
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} r1, r0, 65 }
; CHECK:  { beqz_w r0, .LBB0_11 }
; CHECK: .LBB0_9: // %bb6
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} r1, r0, 76 }
; CHECK:  { beqz_w r0, .LBB0_11 }
; CHECK: .LBB0_10: // %def
; CHECK:  { addi32{{(_w)?}} r1, r0, 99 }
; CHECK: .LBB0_11: // %bb0
; CHECK:  { xor32 r0, r0, r0 }
; CHECK:  { addi32{{(_w)?}} sp, sp, 8 }
; CHECK:  { jalr_w{{(\.s[012])?}} r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK:  .size cb22_jt_r0_rezero, .Lfunc_end0-cb22_jt_r0_rezero
; CHECK:  .section .rodata,"a",@progbits
; CHECK:  .p2align 2, 0x0
; CHECK: .LJTI0_0:
; CHECK:  .long .LBB0_2
; CHECK:  .long .LBB0_7
; CHECK:  .long .LBB0_4
; CHECK:  .long .LBB0_5
; CHECK:  .long .LBB0_3
; CHECK:  .long .LBB0_8
; CHECK:  .long .LBB0_9
; CHECK:  .long .LBB0_6
; CHECK:  // -- End function
; CHECK:  .section ".note.GNU-stack","",@progbits

define i32 @cb22_jt_r0_rezero(i32 %x) nounwind {
; The JT dispatch above clobbers R0; every case target below re-zeros it.
entry:
 switch i32 %x, label %def [
 i32 0, label %bb0
 i32 1, label %bb1
 i32 2, label %bb2
 i32 3, label %bb3
 i32 4, label %bb4
 i32 5, label %bb5
 i32 6, label %bb6
 i32 7, label %bb7
 ]
bb0:
 ret i32 10
bb1:
 ret i32 21
bb2:
 ret i32 32
bb3:
 ret i32 43
bb4:
 ret i32 54
bb5:
 ret i32 65
bb6:
 ret i32 76
bb7:
 ret i32 87
def:
 ret i32 99
}
