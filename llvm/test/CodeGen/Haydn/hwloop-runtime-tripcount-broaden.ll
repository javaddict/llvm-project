; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -stop-after=haydn-hwloops < %s | FileCheck %s

; Role: MIR — Runtime-trip shapes (Case 4/5) — shape CHECKs only (no MIR dumps).

; Runtime-trip shapes (Case 4/5) — shape CHECKs only (no MIR dumps).
;
; HiFi contract: trip is a preheader fact then ZOL. Pre-RA HardwareLoops
; converts the dominant count-up init=0 bump=1 form today. Other Cases are
; covered post-RA by hwloop-hifi-oracle-countdown.ll (O2 asm set_hwloop_f2).
;
; Do not pin ADDI32 vs ADDI32_W or physreg numbers.

; Case 4 canonical: init=0, bump=1, runtime limit (vec_add / vec_dot family).

define void @case4_countup_init0_bump1(ptr noalias %out, ptr noalias readonly %in,
                                       i32 %n) nounwind {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %p = phi ptr [ %in, %entry ], [ %p.next, %loop ]
  %v = load i32, ptr %p, align 4
  %p.next = getelementptr inbounds i32, ptr %p, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}

; Case 4: init=1, bump=1.
; CHECK-LABEL: name: case4_countup_init1_bump1
; CHECK: SET_HWLOOP
; CHECK: PseudoLoopEnd
define i32 @case4_countup_init1_bump1(i32 %n) nounwind {
entry:
  br label %loop
loop:
  %i = phi i32 [ 1, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %i.next = add i32 %i, 1
  %s.next = add i32 %s, %i
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %s
}

; Unroll×2 niter and Case 5 countdown with runtime limit: post-RA path
; (see hwloop-hifi-oracle-countdown.ll). Keep IR here as compile smoke only.
; CHECK-LABEL: name: case4_countup_init0_bump2
define void @case4_countup_init0_bump2(ptr noalias %out, ptr noalias readonly %in,
                                       i32 %n) nounwind {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %i.next = add i32 %i, 2
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret void
}

; CHECK-LABEL: name: case5_countdown_runtime_limit
define i32 @case5_countdown_runtime_limit(ptr noalias readonly %in, i32 %start,
                                          i32 %limit) nounwind {
entry:
  br label %loop
loop:
  %i = phi i32 [ %start, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %p = getelementptr inbounds i32, ptr %in, i32 %i
  %v = load i32, ptr %p, align 4
  %s.next = add i32 %s, %v
  %i.next = add i32 %i, -1
  %cmp = icmp eq i32 %i.next, %limit
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %s
}
