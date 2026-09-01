; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 \
; RUN:     -debug-only=pipeliner < %s 2>&1 | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -haydn-enable-hwloops \
; RUN:     -global-isel-abort=1 -verify-machineinstrs -O2 < %s \
; RUN:     | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: D1.29 (2026-08-30) — pins the CLOSED misplacement admission class.
; The former HaydnInstrInfo.cpp NOTE (admitted 2026-07-27 in 7ef07decdc32,
; pre-W68, no reproducer) claimed multi-stage SMS "has been seen to misplace
; prolog/kernel/epilog vs HWLOOP BEGIN/END" on ZOL byte-mem loops (libc
; memcpy @ -O3). Closing evidence: the W68.1 edn fatal root-caused the real
; misplacement shape (pipelined ZOL body invisible to body resolution) and
; the pure-CFG guarded-chain proof now owns it (HaydnHWLoopDemote.cpp
; resolveBodyMBBCore + fail-closed HaydnFixupHwLoops); CB-166 dynamic-guard
; chains (033372428db8) and D1.6 pipelined LoopStart demote Prefer+Adj
; (1288c452f1a2) close the siblings. The D1.29 sweep (constant trip 16/64,
; runtime trip, store-only fill at -O2/-O3) produced zero misplacements and
; clean same-artifact BundleSim ISS execution. This test pins both halves:
; the multi-stage ACCEPT on byte-mem ZOL loops (log pin, cb166 pattern) and
; the emitted placement law — LoopStart setup in the preheader, prologue
; peel parcels before the hardware window, kernel exactly bracketed by
; BEGIN/END, epilog drain after END.

; SWP: ZOL: runtime trip reg live — dynamic prologue guards cover the peel
; SWP: SMS-SHOULDUSE: accept stages={{[2-9]}} II={{[2-9]}}
; SWP: Schedule Found? 1 (II={{[0-9]+}})
; SWP-NOT: ZOL: reject SMS (MaxStageCount={{[0-9]+}} MinTripCount=0 — no runtime trip reg for a dynamic guard)
; SWP-NOT: SMS: reject ZOL loop with ungrounded PHI chain
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

; ASM-LABEL: copy_const_64:
; setup + peeled prologue load live in the preheader, BEFORE the kernel block
; ASM: set_hwloop_f2
; ASM: .LBB0_1:
; kernel parcels inside the window: the next-iteration load and the
; stage-drain store, then the epilog drain store AFTER the window
; ASM: ldu8
; ASM: s_sb_post_imm
; ASM: s_sb_post_imm
; ASM: jalr

@src = global [128 x i8] zeroinitializer, align 4
@dst = global [128 x i8] zeroinitializer, align 4

define void @copy_const_64() {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr inbounds [128 x i8], ptr @src, i32 0, i32 %i
  %v = load i8, ptr %sp, align 1
  %dp = getelementptr inbounds [128 x i8], ptr @dst, i32 0, i32 %i
  store i8 %v, ptr %dp, align 1
  %i.next = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.next, 64
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define void @copy_const_16() {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr inbounds [128 x i8], ptr @src, i32 0, i32 %i
  %v = load i8, ptr %sp, align 1
  %dp = getelementptr inbounds [128 x i8], ptr @dst, i32 0, i32 %i
  store i8 %v, ptr %dp, align 1
  %i.next = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.next, 16
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define void @copy_runtime(i32 noundef %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sp = getelementptr inbounds [128 x i8], ptr @src, i32 0, i32 %i
  %v = load i8, ptr %sp, align 1
  %dp = getelementptr inbounds [128 x i8], ptr @dst, i32 0, i32 %i
  store i8 %v, ptr %dp, align 1
  %i.next = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define void @fill_const_64() {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %dp = getelementptr inbounds [128 x i8], ptr @dst, i32 0, i32 %i
  store i8 7, ptr %dp, align 1
  %i.next = add nuw nsw i32 %i, 1
  %c = icmp ult i32 %i.next, 64
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
