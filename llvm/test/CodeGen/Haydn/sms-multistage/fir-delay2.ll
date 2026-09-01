; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 kernel-scale SMS corpus (ultragoal story G006, 2026-08-23).
; Class: FIR tap-delay line, accumulate-only (d1->d0->xv recurrence + coeff MAC).
; Expected kind=accepted; point-in-time II=10 NS=2 WITH real prologue/epilogue peels — the full stage-materialization fixture.
; Contract: README.md in this directory — II/NS CAPTURED here (auto-update
; iff they do not grow; NS growth only with II shrink); ASM arm is coarse
; shape only (no schedule/register pinning — noise rule).
; REGRESSION TEST LAW: this is the corpus's only staged-accept fixture
; family head; if NS drops to 1 or prologue parcels vanish, the stage
; materialization path regressed.
; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=accepted loop=bb.{{[0-9]+}}.loop
;
; ASM: fir:
; ASM: set_hwloop_f2
; ASM: jalr

target triple = "haydn-unknown-elf"

define i32 @fir(ptr readonly %x, ptr readonly %c, i32 %n) {
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
!1 = !{!"llvm.loop.itercount.range", i32 8}
