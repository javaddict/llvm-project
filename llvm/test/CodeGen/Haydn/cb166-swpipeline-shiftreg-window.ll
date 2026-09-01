; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 -debug-only=pipeliner < %s 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SWP
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -O2 < %s | FileCheck %s --check-prefix=ASM
; REQUIRES: asserts

; Role: CB-166 (2026-08-27) — sliding-window FIR loops must reach SMS.
;
; The init-era hasShiftRegisterPhiChain blanket reject declined EVERY loop
; whose latch PHIs form a delay line (J1=J2, J2=J3 window registers) — the
; core shape of every streaming FIR kernel — so no modulo overlap ever
; formed and loads never rode with the MAC bodies. The precise law
; (hasUngroundedPhiChain) rejects only chains that cycle or leave the loop
; block; a grounded delay line is expandable by the classic
; ModuloScheduleExpander (phi-referencing-another-phi walk in
; updateInstruction) and true PHI cycles are already rejected by generic
; hasPHICycle.
;
; Contract under test: SMS ANALYZES and FINDS a schedule for this loop
; (not blanket-rejected), and the machine verifier stays clean through the
; classic expander (prologue/kernel/epilog PHI rewrite).

; SWP: Schedule Found? 1
; SWP-NOT: SMS: reject loop with ungrounded PHI chain
; SWP-NOT: Unable to analyzeLoop, can NOT pipeline Loop

; ASM-LABEL: fir_shiftreg_window:
; ASM: jalr

define i64 @fir_shiftreg_window(ptr nocapture readonly %x, ptr nocapture readonly %h, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i64 [ 0, %entry ], [ %acc.n, %loop ]
  ; J2/J1 delay line: J1 = old J2, J2 = new load. The latch PHIs form the
  ; shift register the old gate rejected.
  %j2 = phi i64 [ 0, %entry ], [ %v2, %loop ]
  %j1 = phi i64 [ 0, %entry ], [ %j2, %loop ]
  %pa = getelementptr inbounds i64, ptr %x, i32 %i
  %v2 = load i64, ptr %pa, align 4
  %bc.1 = bitcast i64 %j1 to <2 x i32>
  %bc.2 = bitcast i64 %v2 to <2 x i32>
  %acc.n = tail call i64 @llvm.haydn.f2mulaa32rs.hhll(i64 %acc, <2 x i32> %bc.1, <2 x i32> %bc.2)
  %i.next = add nuw nsw i32 %i, 1
  %done = icmp eq i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %r = phi i64 [ 0, %entry ], [ %acc.n, %loop ]
  ret i64 %r
}

declare i64 @llvm.haydn.f2mulaa32rs.hhll(i64, <2 x i32>, <2 x i32>)
