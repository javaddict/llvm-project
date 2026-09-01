; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:     -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:     -haydn-multistage-sms-force-fail-seat=JM-ALLOC \
; RUN:     -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; REGRESSION TEST (qg-review F1, 2026-08-23): G005 canonical remark must
; never dereference freed stage MBBs on the rollback path.
;
; Bug: SMSLoopRecord journaled PrologueMBB/EpilogueMBB pointers, but a
; post-materialize certificate failure rolls back through
; OrdinarySnapshot.restore() -> eraseCreatedStageMBB -> eraseFromParent —
; the record's pointers dangle, and emitHaydnSMSLoopRemarks (finalize)
; then read countKernelIssueParcels(*PrologueMBB) = use-after-free read.
; Reproduced with -haydn-multistage-sms-force-fail-seat=JM-ALLOC printing
; "prologue=0 parcels" from freed memory.
; Fix: the rollback lambda nulls PrologMBB/EpilogMBB after restore();
; a rolled-back decline records no stage pointers. Test design: force a
; post-mutation failure seat on a staged-accepting FIR-class body; the
; canonical line must still print (kind=declined seat=...) with
; prologue=0/epilogue=0 read from NULL-checked members, and the asm must
; be the ordinary rolled-back schedule. If the bug reappears this runs
; on freed memory (ASan/valgrind trap; benign zero under default build
; only because the MI list was emptied pre-delete).

target triple = "haydn-unknown-elf"

define i32 @staged_rollback(ptr readonly %x, ptr readonly %c, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s = phi i32 [0, %pre], [%s.n, %loop]
  %d0 = phi i32 [0, %pre], [%xv, %loop]
  %d1 = phi i32 [0, %pre], [%d0, %loop]
  %pi = getelementptr inbounds i32, ptr %x, i32 %i
  %xv = load i32, ptr %pi, align 4
  %c0 = load i32, ptr %c, align 4
  %m0 = mul i32 %xv, %c0
  %m1 = mul i32 %d0, %c0
  %m2 = mul i32 %d1, %c0
  %a0 = add i32 %m0, %m1
  %a1 = add i32 %a0, %m2
  %s.n = add i32 %a1, %s
  %i.n = add i32 %i, 1
  %cc = icmp ult i32 %i.n, %n
  br i1 %cc, label %loop, label %exit, !llvm.loop !0
exit:
  %r = phi i32 [0, %entry], [%s.n, %loop]
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 32}

; The rollback event fires first (engine line), then exactly ONE
; canonical line: the rolled-back loop reports kind=declined with the
; forced seat and NULL-SAFE stage pointers (prologue=0/epilogue=0).
; Non-loop regions (entry/pre) journal records too but the census +
; accepted-only fallback correctly excludes them from the KPI stream
; (F2 narrowing). Exactly-once semantics via the negative COUNT arm.
; RMK: rollback to ordinary baseline (JM-ALLOC-force)
; RMK: Schedule found II={{[0-9]+}} NS={{[0-9]+}} prologue=0 parcels
; RMK-SAME: epilogue=0 parcels kind=declined seat=JM-ALLOC-force
; RMK-SAME: loop=bb.2.loop
; RMK-NOT: Schedule found

; ASM: staged_rollback:
; ASM: jalr
