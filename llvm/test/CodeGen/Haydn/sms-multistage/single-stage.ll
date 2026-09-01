; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 kernel-scale SMS corpus (ultragoal story G006, 2026-08-23).
; Class: single-stage (NS=1) resource-conflict: dual independent accumulators, one MAC each.
; Expected kind=accepted; point-in-time II=4 NS=1 kernel-only — parallelism within one stage (contrast fir-delay2 for the NS=2 path).
; Contract: README.md in this directory — II/NS CAPTURED here (auto-update
; iff they do not grow; NS growth only with II shrink); ASM arm is coarse
; shape only (no schedule/register pinning — noise rule).

; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=accepted loop=bb.{{[0-9]+}}.loop
;
; ASM: dualacc:
; ASM: set_hwloop_f2
; ASM: jalr

target triple = "haydn-unknown-elf"

define i32 @dualacc(ptr readonly %a, ptr readonly %b, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s1 = phi i32 [0, %pre], [%s1.n, %loop]
  %s2 = phi i32 [0, %pre], [%s2.n, %loop]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m1 = mul i32 %va, %vb
  %s1.n = add i32 %s1, %m1
  %m2 = mul i32 %vb, 3
  %s2.n = add i32 %s2, %m2
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  %r = phi i32 [0, %entry], [%s2.n, %loop]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
