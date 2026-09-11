; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -O2 < %s | FileCheck %s

; Role: semantic — HiFi oracle contract (NatureDSP / Cadence loopnez): preheader: materialize trip once.

; HiFi oracle contract (NatureDSP / Cadence loopnez):
; preheader: materialize trip once
; loopnez aN, Lend
; body: no soft SEQ/BEQ back-edge
;
; Haydn competitive form after post-RA HardwareLoops (Role B):
; set_hwloop_f2 sel, start, end, tripReg
; body without beqz latch
;
; Canonical C shapes that must convert (not monkey-patch special cases):
; Case 2 count-down: for (i=N; i!=0; --i) → trip = IV @ entry, step=-1, lim=0
; Case 1 count-up: for (i=0; i<N; ++i) → trip = N
; Guarded entry must still convert after createPreheaderForLoop + Dom* resolve.
;
; Regression locked by P9: program-point Val(R@Use) + Dom*(Header)∪L for
; spilled step/limit; never accept stale physreg constants across slots.

; Count-down-to-zero (dominant FIR / peel residual). HiFi: loopnez with N.

define i32 @countdown_to_zero(ptr %p, i32 %n) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i = phi i32 [ %n, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %addr = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %addr, align 4
  %acc.next = add i32 %acc, %v
  %i.next = add i32 %i, -1
  %cmp = icmp ne i32 %i.next, 0
  br i1 %cmp, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %r
}

; Count-up runtime trip (vec_add / vec_dot family). HiFi: loopnez with N.
; CHECK-LABEL: countup_runtime:
; CHECK: set_hwloop_f2
define i32 @countup_runtime(ptr readonly %a, i32 %n) nounwind {
entry:
  %c0 = icmp sgt i32 %n, 0
  br i1 %c0, label %loop, label %exit

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr inbounds i32, ptr %a, i32 %i
  %v = load i32, ptr %p, align 4
  %sum.next = add i32 %sum, %v
  %i.next = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  ret i32 %r
}

; Compile-time trip (HiFi loop with known count → SET_HWLOOP imm).
; CHECK-LABEL: countup_imm16:
; CHECK: set_hwloop
define i32 @countup_imm16(ptr %p) nounwind {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %addr = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %addr, align 4
  %acc.next = add i32 %acc, %v
  %i.next = add i32 %i, 1
  %cmp = icmp eq i32 %i.next, 16
  br i1 %cmp, label %exit, label %loop

exit:
  ret i32 %acc.next
}

; Register pressure: many live values so post-RA may spill step=-1 / limit=0
; and reuse physregs (divide-class residual). GR2.1 Kind-A restamp: this
; high-pressure body now SOFTWARE-pipelines (II=2, guarded peel with folded
; spills) instead of forming a ZOL — the ZOL-form pins for the two
; low-pressure peers above are unchanged. -verify-machineinstrs must stay
; silent: stack-counter LatchScr may be prologue-killed CSR R14; overlay
; skips SET-site ST32 of that undefined value (pure tail DEF / SMS-guard).
; CHECK-LABEL: countdown_high_pressure:
; CHECK: // =>This Inner Loop Header: Depth=1
; CHECK-NOT: set_hwloop
define i32 @countdown_high_pressure(ptr %p, i32 %n,
                                    i32 %a0, i32 %a1, i32 %a2, i32 %a3,
                                    i32 %a4, i32 %a5, i32 %a6, i32 %a7) nounwind {
entry:
  %cmp0 = icmp sgt i32 %n, 0
  br i1 %cmp0, label %loop, label %exit

loop:
  %i = phi i32 [ %n, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %addr = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %addr, align 4
  ; Keep many arithmetic live across the latch so RA spills constants.
  %t0 = add i32 %v, %a0
  %t1 = add i32 %t0, %a1
  %t2 = add i32 %t1, %a2
  %t3 = add i32 %t2, %a3
  %t4 = add i32 %t3, %a4
  %t5 = add i32 %t4, %a5
  %t6 = add i32 %t5, %a6
  %t7 = add i32 %t6, %a7
  %acc.next = add i32 %acc, %t7
  %i.next = add i32 %i, -1
  %cmp = icmp ne i32 %i.next, 0
  br i1 %cmp, label %loop, label %exit

exit:
  %r = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %r
}
