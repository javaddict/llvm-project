; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 kernel-scale SMS corpus (ultragoal story G006, 2026-08-23).
; Class: dot + 2-op dependent chain on the MAC result.
; Expected kind=accepted; point-in-time II=6 NS=1.
; Contract: README.md in this directory — II/NS CAPTURED here (auto-update
; iff they do not grow; NS growth only with II shrink); ASM arm is coarse
; shape only (no schedule/register pinning — noise rule).

; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=accepted loop=bb.{{[0-9]+}}.loop
;
; ASM: k:
; ASM: set_hwloop_f2
; ASM: jalr

target triple = "haydn-unknown-elf"

define void @k(ptr readonly %a, ptr readonly %b, ptr %dst, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s = phi i32 [0, %pre], [%s.n, %loop]
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %pd = getelementptr inbounds i32, ptr %dst, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m = mul i32 %va, %vb
  %t1 = add i32 %m, %va
  %s.n = add i32 %s, %t1
  store i32 %s.n, ptr %pd, align 4
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
