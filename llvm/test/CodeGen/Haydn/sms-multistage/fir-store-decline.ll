; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 kernel-scale SMS corpus (ultragoal story G006, 2026-08-23).
; Class: FIR delay-line + per-iteration store — the canonical DECLINE fixture.
; Expected kind=declined seat=ii-exhaustion (fail-closed); point-in-time II=8 NS=1 reported (body-length semantics).
; Contract: README.md in this directory — II/NS CAPTURED here (auto-update
; iff they do not grow; NS growth only with II shrink); ASM arm is coarse
; shape only (no schedule/register pinning — noise rule).
; G002 finding institutionalized: FIR+store never accepts on this engine
; (every variant exhausts — delay-phis, offset loads, pointer-bump); the
; accepting FIR form is accumulate-only (fir-delay2.ll). This fixture pins
; the refusal: the canonical line MUST stay kind=declined with its seat —
; never accepted, never a sequential fallback — while the hardware loop
; still arms (set_hwloop_f2 in ASM).
; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=declined seat=ii-exhaustion loop=bb.{{[0-9]+}}.loop
;
; ASM: fir_store:
; ASM: set_hwloop_f2
; ASM-NOT: #<swps>
; ASM: jalr

target triple = "haydn-unknown-elf"

define i32 @fir_store(ptr readonly %x, ptr %y, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %s = phi i32 [0, %pre], [%s.n, %loop]
  %d0 = phi i32 [0, %pre], [%xv, %loop]
  %pi = getelementptr inbounds i32, ptr %x, i32 %i
  %xv = load i32, ptr %pi, align 4
  %t0 = add i32 %xv, 1
  %t1 = mul i32 %t0, 3
  %t2 = add i32 %t1, %xv
  %t3 = xor i32 %t2, %s
  %s.n = add i32 %t3, %d0
  store i32 %s.n, ptr %y, align 4
  %i.n = add i32 %i, 1
  %cc = icmp ult i32 %i.n, %n
  br i1 %cc, label %loop, label %exit, !llvm.loop !0
exit:
  %r = phi i32 [0, %entry], [%s.n, %loop]
  ret i32 %r
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
