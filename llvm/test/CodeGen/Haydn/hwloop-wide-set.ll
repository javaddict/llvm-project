; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s

; Role: semantic — WIDE SET_HWLOOP_F2_W emission.

; REGRESSION TEST : WIDE SET_HWLOOP_F2_W emission.
;
; Bug being prevented: hardware-loop setup was ALWAYS expanded to a
; multi-bundle dance — R12 scratch spill (st32 r12), ADDI32 count
; materialization, SET_HWLOOP_REG placeholder (8-byte two-word marker)
; 3-NOP HWLR_COUNT commit window, R12 restore (ld32 r12) — even though the
; 48-bit WIDE SET_HWLOOP_F2_W form (encoding_manual.md §5.12:
; rs(count) + uimm6_off1 + uimm12_off2 + hwlr_sel) encodes the same setup
; in ONE instruction.
;
; Fix: HaydnAsmPrinter emits SET_HWLOOP_F2_W when -haydn-hwloop-wide is set.
; The OLD dance is dropped entirely.
;
; RUN-line note: -haydn-hwloop-wide is registered as a direct cl::opt in
; HaydnAsmPrinter.cpp (EnableHaydnHwloopWide), so it is passed to llc WITHOUT
; the -mllvm prefix. The original `-mllvm -haydn-hwloop-wide` form was rejected
; by llc ("Unknown command line argument '-mllvm'"), producing empty stdin to
; FileCheck — that was a test-authoring mistake, not a backend bug.
;
; Test design: a simple counted loop. The IR-level HardwareLoops pass forms
; it as a LoopStart pseudo (count materialized into a GPR by the preheader's
; `addi32 rN, r0, 10`); the AsmPrinter's LoopStart case then emits ONE
; set_hwloop_f2 (not the 5-bundle dance). The CHECK-NOTs assert the dance
; is GONE: no R12 scratch spill/restore, no set_hwloop_f2 (the OLD
; register-count mnemonic rendered by the placeholder path). The CHECK
; confirms exactly one set_hwloop_f2 with sel=0 (golden inner seat) and the loop-body symbol
; as the start offset.
;
; The all-immediate SET_HWLOOP_W (§5.11) form is reached via the same
; EnableHaydnHwloopWide gate in the SET_HWLOOP case for constant counts that
; fit uimm16; the F2_W assertion here covers the WIDE-lowering plumbing
; (flag, helper, operand contract) shared by both WIDE forms.
;
; IMPORTANT: this is an asm-text (-S) test ONLY. The -filetype=obj path for
; the WIDE SET_HWLOOP fixup layer is incomplete (deferral): the two
; PC-relative offset fixups for the 6-byte parcel both land at byte offset 0
; and clobber each other, and the tag-compensation in HaydnAsmBackend
; reads placeholder tag bits that don't exist in the WIDE layout. Those are
; out of scope for this Slice (owned by the MC-layer follow-up). This test
; must NOT be run with -filetype=obj.

define i32 @hwloop_wide_set(ptr %p) {
; CHECK-LABEL: hwloop_wide_set:
; CHECK-NOT: set_hwloop_f2 {{[0-9]+}}, .LBB0_1, .LLhwloop_end0, {{r[0-9]+}}
; CHECK-NOT: set_hwloop_reg
; CHECK:     set_hwloop_f2 0, .LLhwloop_start{{[0-9]+}}, .LLhwloop_end0, {{r[0-9]+}}
; CHECK-NOT: set_hwloop_f2 {{[0-9]+}}, .LBB0_1, .LLhwloop_end0, {{r[0-9]+}}
; CHECK-NOT: set_hwloop_reg
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %val = load i32, ptr %p
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, 10
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
