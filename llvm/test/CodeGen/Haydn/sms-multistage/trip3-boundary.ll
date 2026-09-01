; RUN: llc -mtriple=haydn-unknown-elf -haydn-sms-containment-max=1 -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 kernel-scale SMS corpus (ultragoal story G006, 2026-08-23).
; Class: trip-count boundary: constant trip=3 with HONEST matching itercount.range MD.
; Expected kind=accepted; point-in-time II=6 NS=2 — the MD floor (not the trip constant) is the acceptance axis (F39 peel-depth law).
; Contract: README.md in this directory — II/NS CAPTURED here (auto-update
; iff they do not grow; NS growth only with II shrink); ASM arm is coarse
; shape only (no schedule/register pinning — noise rule).

; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=accepted loop=bb.{{[0-9]+}}.loop
;
; ASM: boundary:
; ASM: set_hwloop_f2
; ASM: jalr

target triple = "haydn-unknown-elf"

define i32 @boundary(ptr readonly %p) {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.n, %loop]
  %s = phi i32 [0, %entry], [%s.n, %loop]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %v = load i32, ptr %ge, align 4
  %t0 = add i32 %v, 1
  %t1 = mul i32 %t0, 3
  %t2 = add i32 %t1, %v
  %t3 = xor i32 %t2, %s
  %s.n = add i32 %s, %t3
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, 3
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret i32 %s.n
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 3}
