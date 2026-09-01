; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G006 kernel-scale SMS corpus (ultragoal story G006, 2026-08-23).
; Class: memcpy, variable trip with NO proof (refusal class).
; Expected kind=declined seat=ii-exhaustion (F39 fail-closed: unproven trip); point-in-time II=3 NS=1 reported (body length).
; Contract: README.md in this directory — II/NS CAPTURED here (auto-update
; iff they do not grow; NS growth only with II shrink); ASM arm is coarse
; shape only (no schedule/register pinning — noise rule).
; The refusal axis is precisely the unproven trip: the identical kernel
; WITH llvm.loop.itercount.range MD accepts (G002 evidence). Hardware
; loop still arms — refusal is SMS-scoped, never object-wide.
; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=declined seat=ii-exhaustion loop=bb.{{[0-9]+}}.loop
;
; ASM: memcpy_var:
; ASM: set_hwloop_f2
; ASM-NOT: #<swps>
; ASM: jalr

target triple = "haydn-unknown-elf"

define void @memcpy_var(ptr %dst, ptr readonly %src, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %exit
pre:
  br label %loop
loop:
  %i = phi i32 [0, %pre], [%i.n, %loop]
  %ps = getelementptr inbounds i32, ptr %src, i32 %i
  %pd = getelementptr inbounds i32, ptr %dst, i32 %i
  %v = load i32, ptr %ps, align 4
  store i32 %v, ptr %pd, align 4
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
