; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 kernel-scale SMS corpus (ultragoal story G006, 2026-08-23).
; Class: large-II: 8 serial MACs (one instruction class dominating > 7 units).
; Expected kind=accepted; point-in-time II=11 NS=2 with prologue=11 — the deep over-lapped lifetime fixture.
; Contract: README.md in this directory — II/NS CAPTURED here (auto-update
; iff they do not grow; NS growth only with II shrink); ASM arm is coarse
; shape only (no schedule/register pinning — noise rule).

; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=accepted loop=bb.{{[0-9]+}}.loop
;
; ASM: bigii:
; ASM: set_hwloop_f2
; ASM: jalr

target triple = "haydn-unknown-elf"

define i32 @bigii(ptr readonly %p, ptr readonly %q, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s = phi i32 [0, %pre], [%s.n, %loop]
  %ge = getelementptr inbounds i32, ptr %p, i32 %i
  %gq = getelementptr inbounds i32, ptr %q, i32 %i
  %v = load i32, ptr %ge, align 4
  %w = load i32, ptr %gq, align 4
  %m1 = mul i32 %v, %w
  %m2 = mul i32 %m1, %v
  %m3 = mul i32 %m2, %w
  %m4 = mul i32 %m3, %v
  %m5 = mul i32 %m4, %w
  %m6 = mul i32 %m5, %v
  %m7 = mul i32 %m6, %w
  %m8 = mul i32 %m7, %v
  %s.n = add i32 %s, %m8
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  %r = phi i32 [0, %entry], [%s.n, %loop]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
