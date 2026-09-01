; RUN: llc -mtriple=haydn-unknown-elf -haydn-sms-containment-max=1 -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=false -haydn-enable-multistage-sms=0 \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.off.rmk | FileCheck %s --check-prefix=OFF
; RUN: FileCheck %s --allow-empty --check-prefix=OFFRMK < %t.off.rmk
; RUN: llc -mtriple=haydn-unknown-elf -haydn-sms-containment-max=1 -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms=0 \
; RUN:   < %s | FileCheck %s --check-prefix=HWON
; RUN: llc -mtriple=haydn-unknown-elf -haydn-sms-containment-max=1 -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops=false -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.sms.rmk | FileCheck %s --check-prefix=SMSONLY
; RUN: FileCheck %s --allow-empty --check-prefix=SMSONLY-RMK < %t.sms.rmk
; RUN: llc -mtriple=haydn-unknown-elf -haydn-sms-containment-max=1 -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.dual.rmk | FileCheck %s --check-prefix=DUAL
; RUN: FileCheck %s --check-prefix=DUAL-RMK < %t.dual.rmk

; G002 SMS LOCKING TEST LAYER (ultragoal story G002, 2026-08-23).
;
; Purpose: lock the CURRENT multistage-SMS behavior at the SMALL-TRIP
; BOUNDARY (constant trip 2 and 3 with HONEST matching
; llvm.loop.itercount.range MD) across the full 2x2 flag matrix.
;
; Invariant locked:
;   OFF      — no hwloop SET, no SWPS artifact, no acceptance remark.
;   HWON     — hardware loop arms, NO SWPS annotation.
;   SMSONLY  — no candidacy (count-up ZOL without hwloop); no artifacts.
;   DUAL     — SMS ACCEPTS even at trip=2 (MD floor satisfies the F39
;              peel-depth requirement: stages=2 needs min-trip >= 2) with
;              full II parity AND real stage geometry (prolog/epilog
;              peels, modulo-cycle order). AchievedII == scheduled II.
;
; This pins the boundary of the 2026-08-22 qualification: the MD floor,
; not the trip constant, is the acceptance axis — the same loops without
; MD exhaust (negative arm in sms-multistage-g002-matrix-refusal.ll).
; II values are CAPTURED, not pinned (noise rule).

target triple = "haydn-unknown-elf"

; OFFRMK-NOT: accepted II=
; SMSONLY-RMK-NOT: accepted II=
;
; Two accepting kernels in this file: both must show the full parity
; chain each (FileCheck matches in order).
; DUAL-RMK: accepted II=[[II1:[0-9]+]]
; DUAL-RMK-SAME: measured-II=[[II1]]
; DUAL-RMK-SAME: no-seq-fallback
; DUAL-RMK: qualify parcels-per-iter=[[II1]]
; DUAL-RMK-SAME: searched-II=[[II1]]
; DUAL-RMK: accepted II=[[II2:[0-9]+]]
; DUAL-RMK-SAME: measured-II=[[II2]]
; DUAL-RMK-SAME: no-seq-fallback
; DUAL-RMK: qualify parcels-per-iter=[[II2]]
; DUAL-RMK-SAME: searched-II=[[II2]]
; DUAL-RMK: stage-mbb prolog={{[1-9][0-9]*}}
; DUAL-RMK-SAME: epilog={{[1-9][0-9]*}} stages={{[2-9]|[1-9][0-9]+}}

define i32 @boundary_trip2(ptr readonly %p) {
; OFF-LABEL: boundary_trip2:
; OFF-NOT:   set_hwloop
; OFF-NOT:   #<swps>
; OFF:       jalr
;
; HWON-LABEL: boundary_trip2:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   #<swps>
; HWON:       jalr
;
; SMSONLY-LABEL: boundary_trip2:
; SMSONLY-NOT:   set_hwloop
; SMSONLY-NOT:   #<swps>
; SMSONLY:       jalr
;
; DUAL-LABEL: boundary_trip2:
; DUAL:       set_hwloop_f2 0,
; DUAL:       #<swps> II=[[A2:[0-9]+]]
; DUAL:       #<swps> AchievedII=[[A2]]
; DUAL-NOT:   csrw{{.*}} 0x2{{[0-5]}}
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
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret i32 %s.n
}

define i32 @boundary_trip3(ptr readonly %p) {
; OFF-LABEL: boundary_trip3:
; OFF-NOT:   set_hwloop
; OFF:       jalr
;
; HWON-LABEL: boundary_trip3:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   #<swps>
; HWON:       jalr
;
; SMSONLY-LABEL: boundary_trip3:
; SMSONLY-NOT:   set_hwloop
; SMSONLY:       jalr
;
; DUAL-LABEL: boundary_trip3:
; DUAL:       set_hwloop_f2 0,
; DUAL:       #<swps> II=[[A3:[0-9]+]]
; DUAL:       #<swps> AchievedII=[[A3]]
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
  br i1 %c, label %loop, label %exit, !llvm.loop !2
exit:
  ret i32 %s.n
}

; Honest floors matching the constant trips (F39): trip=2 with stages=2
; is exactly the peel-depth boundary — min-trip >= NStages-1+1 holds.
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 2}
!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.itercount.range", i32 3}
