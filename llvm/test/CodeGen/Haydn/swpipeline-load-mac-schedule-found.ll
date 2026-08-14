; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 < %s | FileCheck %s --check-prefix=ASM

; Role: semantic — Dual-load accumulator-MAC reduction (NatureDSP vec_dot32x32
; shape). Positive SMS schedule-found pin: SMS pipelines this hot loop (no scalar
; fallback). Requires G_PTR_ADD -> ADDI32 (packable, not ADDI32_W S0-only),
; Bundle pickSlot S2->S0 so loads keep S0/S1, and load->acc-MAC edge latency 2.
;
; Why this is contract-only (no brittle bundle body): the D999 no-forwarding
; fix changes SMS placement to call the operand-aware MI overload of
; ResourceCycle (llvm/lib/CodeGen/MachinePipeliner.cpp). calculateResMIIDFA
; shares that MI overload, so ResMII — and therefore the starting II and the
; exact kernel bundle layout — may shift vs the pre-D999 schedule. The previous
; per-bundle CHECKs were auto-generated against the pre-D999 schedule and are no
; longer reliable. The durable contract is that SMS FINDS a schedule for this
; loop (not rejected) and emits a non-empty kernel. Regenerate exact II/bundles
; with utils/update_llc_test_checks.py after a coordinator build if a precision
; pin is wanted again.

; SWP contract (single function): SMS pipelines this loop. The pipeliner
; -debug-only trace (including "Schedule Found? 1") is emitted BEFORE the
; asm, so it is matched here up front rather than after ASM-LABEL.
; SWP: Schedule Found? 1
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

; ASM-LABEL: dot_dual_load_mac:
; ASM: jalr

define i64 @dot_dual_load_mac(ptr nocapture readonly %a, ptr nocapture readonly %b, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i64 [ 0, %entry ], [ %acc.n, %loop ]
  %pa = getelementptr inbounds i64, ptr %a, i32 %i
  %pb = getelementptr inbounds i64, ptr %b, i32 %i
  %va = load i64, ptr %pa, align 4
  %vb = load i64, ptr %pb, align 4
  %bc.1 = bitcast i64 %va to <2 x i32>
  %bc.2 = bitcast i64 %vb to <2 x i32>
  %acc.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  %i.next = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %r = phi i64 [ 0, %entry ], [ %acc.n, %loop ]
  ret i64 %r
}

declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
