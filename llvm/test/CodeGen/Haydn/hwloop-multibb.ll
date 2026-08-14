; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop < %s | FileCheck %s --check-prefix=DEFAULT
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops < %s | FileCheck %s --check-prefix=HWON

; Role: semantic — multi-BB KPI seat for Role-A OFF qualification.
; Multi-BB formation is FUTURE/KPI-gated greenfield (never Role-B physical
; rediscovery). Product default OFF; even under HWON, multi-BB side-effect
; control flow must stay soft (no set_hwloop). Single-BB Role A is covered
; by hwloop-rolea-*. This file only pins the multi-BB decline seat.

target triple = "haydn-unknown-elf"

; Multi-BB body with stores in both arms — resists if-conversion and must
; not arm SET under DEFAULT OFF or HWON.
define void @multibb_side_effect_stores(ptr %dst, ptr readonly %src, i32 %n) nounwind {
; DEFAULT-LABEL: multibb_side_effect_stores:
; DEFAULT-NOT:   set_hwloop
; DEFAULT:       jalr
;
; HWON-LABEL: multibb_side_effect_stores:
; HWON-NOT:   set_hwloop
; Soft multi-BB residual (any cond back-edge form remains):
; HWON:       {{blt|bge|bnez|beqz|bltu|bgeu}}
; HWON:       jalr
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %dp = phi ptr [ %dst, %entry ], [ %dp.next, %latch ]
  %v = load i32, ptr %sp
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else

then:
  store i32 0, ptr %dp
  br label %latch

else:
  store i32 %v, ptr %dp
  br label %latch

latch:
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; Two-way branch with arithmetic in each arm. If if-converted to single-BB,
; Role-A may legally arm SET under HWON; the KPI seat is only that multi-BB
; control never invents multi-BB formation. Pin: never more than one SET for
; this shape, and DEFAULT stays soft.
define i32 @multibb_arith_arms(ptr readonly %src, i32 %n, i32 %k) nounwind {
; DEFAULT-LABEL: multibb_arith_arms:
; DEFAULT-NOT:   set_hwloop
; DEFAULT:       jalr
;
; HWON-LABEL: multibb_arith_arms:
; If-converted single-BB may form Role A; multi-BB residual must not invent
; a second nested multi-BB SET path. At most one SET (or none if still multi-BB).
; HWON-NOT:   set_hwloop{{.*}}set_hwloop
; HWON:       jalr
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sp = phi ptr [ %src, %entry ], [ %sp.next, %latch ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %latch ]
  %v = load i32, ptr %sp
  %sign = icmp slt i32 %v, 0
  br i1 %sign, label %then, label %else

then:
  %neg = sub i32 0, %v
  %scaled_then = mul i32 %neg, %k
  br label %latch

else:
  %scaled_else = mul i32 %v, %k
  br label %latch

latch:
  %scaled = phi i32 [ %scaled_then, %then ], [ %scaled_else, %else ]
  %acc.next = add i32 %acc, %scaled
  %sp.next = getelementptr inbounds i32, ptr %sp, i32 1
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %acc.next
}
