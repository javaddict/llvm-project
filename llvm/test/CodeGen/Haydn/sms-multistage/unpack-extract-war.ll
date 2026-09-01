; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 kernel-scale SMS corpus (ultragoal story G006, 2026-08-23).
; Class: unpack/extract WAR (crossbank analog): load, extract halves, recombine swapped, store in place.
; Expected kind=declined seat=ii-exhaustion; point-in-time II=6 NS=2 reported — the store->next-iter-load LCD pins the refusal.
; Contract: README.md in this directory — II/NS CAPTURED here (auto-update
; iff they do not grow; NS growth only with II shrink); ASM arm is coarse
; shape only (no schedule/register pinning — noise rule).

; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=declined seat=ii-exhaustion loop=bb.{{[0-9]+}}.loop
;
; ASM: warx:
; ASM: set_hwloop_f2
; ASM-NOT: #<swps>
; ASM: jalr

target triple = "haydn-unknown-elf"

define void @warx(ptr %io, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %pi = getelementptr inbounds i32, ptr %io, i32 %i
  %v = load i32, ptr %pi, align 4
  %lo = and i32 %v, 65535
  %hi = lshr i32 %v, 16
  %sw = shl i32 %lo, 16
  %nv = or i32 %sw, %hi
  store i32 %nv, ptr %pi, align 4
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
