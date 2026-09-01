; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=false -haydn-enable-multistage-sms=0 \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFF
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=0 \
; RUN:   < %s | FileCheck %s --check-prefix=HWON
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.sms.rmk | FileCheck %s --check-prefix=SMSONLY
; RUN: FileCheck %s --allow-empty --check-prefix=SMSONLY-RMK < %t.sms.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.dual.rmk | FileCheck %s --check-prefix=DUAL
; RUN: FileCheck %s --check-prefix=DUAL-RMK < %t.dual.rmk

; G002 SMS LOCKING TEST LAYER (ultragoal story G002, 2026-08-23).
;
; Purpose: lock the CURRENT multistage-SMS REFUSAL behavior of the
; variable-trip memcpy class (no trip proof) and the no-MD boundary
; kernels, across the full 2x2 flag matrix.
;
; Invariant locked (fail-closed discipline):
;   OFF      — no hwloop SET, no SWPS artifact, no acceptance remark.
;   HWON     — hardware loop arms (set_hwloop_f2); NO SWPS annotation.
;   SMSONLY  — no candidacy for count-up ZOL loops; no artifacts.
;   DUAL     — SMS refuses the kernels FAIL-CLOSED (exhaustion, never a
;              sequential fallback) while the hardware loop still arms and
;              the object still compiles: no accepted II=, no swps stamp,
;              no stage-mbb, no set_hwloop loss.
;
; The refusal axis is precisely the UNPROVEN TRIP (F39): the identical
; memcpy with llvm.loop.itercount.range MD accepts (see the g002
; dot/trip matrix files); without MD the candidate exhausts its II window
; and restores the hardware-loop-only object. This is the negative-arm
; discipline of the 2026-08-22 qualification: refusal must be observable
; and default-independent, not silent.

target triple = "haydn-unknown-elf"

; OFFRMK-NOT: accepted II=
; SMSONLY-RMK-NOT: accepted II=
; DUAL-RMK-NOT: accepted II=
; DUAL-RMK-NOT: sequential (preflight)
; DUAL-RMK: exhausted: no feasible II
; DUAL-RMK-SAME: no-seq-fallback
; DUAL-RMK: qualify-or-cut

define void @memcpy_var(ptr %dst, ptr readonly %src, i32 %n) {
; OFF-LABEL: memcpy_var:
; OFF-NOT:   set_hwloop
; OFF-NOT:   #<swps>
; OFF:       jalr
;
; HWON-LABEL: memcpy_var:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   #<swps>
; HWON:       jalr
;
; SMSONLY-LABEL: memcpy_var:
; SMSONLY-NOT:   set_hwloop
; SMSONLY-NOT:   #<swps>
; SMSONLY:       jalr
;
; DUAL-LABEL: memcpy_var:
; DUAL:       set_hwloop_f2 0,
; DUAL-NOT:   #<swps>
; DUAL-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DUAL:       jalr
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

; No-MD constant-trip boundary kernels (trip 2/3): without an itercount
; proof the II window never closes and the loops exhaust — refusal class
; identical to memcpy. (With honest matching MD these accept — captured
; in sms-multistage-g002-matrix-trip.ll.)
define i32 @boundary_nomd_trip2(ptr readonly %p) {
; OFF-LABEL: boundary_nomd_trip2:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: boundary_nomd_trip2:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   #<swps>
; HWON:       jalr
;
; DUAL-LABEL: boundary_nomd_trip2:
; DUAL:       set_hwloop_f2 0,
; DUAL-NOT:   #<swps>
; DUAL:       jalr
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
  %c = icmp ult i32 %i.n, 2
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.n
}

define i32 @boundary_nomd_trip3(ptr readonly %p) {
; OFF-LABEL: boundary_nomd_trip3:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: boundary_nomd_trip3:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   #<swps>
; HWON:       jalr
;
; DUAL-LABEL: boundary_nomd_trip3:
; DUAL:       set_hwloop_f2 0,
; DUAL-NOT:   #<swps>
; DUAL:       jalr
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
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s.n
}
