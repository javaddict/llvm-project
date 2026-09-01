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
; Purpose: lock the CURRENT multistage-SMS behavior of the FIR tap-delay
; kernel class (delay-line recurrence d1->d0->xv plus a coefficient MAC
; chain, accumulate only — no store) across the full 2x2 flag matrix.
;
; Invariant locked (2026-08-22 G004 qualification discipline):
;   OFF      — no hwloop SET, no SWPS artifact, no acceptance remark.
;   HWON     — hardware loop arms, NO SWPS annotation.
;   SMSONLY  — SMS alone never candidates this count-up ZOL loop.
;   DUAL     — SMS accepts with II parity AND real multi-stage geometry:
;              stages=2, nonzero stage-mbb prolog/epilog peels,
;              peel-order=modulo-cycle, epilogue-preseed=kernel-steady.
;              The AsmPrinter AchievedII stamp equals the scheduled II.
;
; This is the ONLY G002 kernel class that exercises the full
; prologue/kernel/epilogue materialization path (dot is kernel-only
; stages=1), so peel geometry locks here.
;
; Shape notes: empty dedicated preheader (PF-CFG); itercount.range floor
; (F39); 3-mul body so LinearLength >= II window closes (2-mul variants
; exhaust — captured as the refusal classes in the g002-refusal file).
; II values are CAPTURED, not pinned (noise rule).

target triple = "haydn-unknown-elf"

; OFFRMK-NOT: accepted II=
; OFFRMK-NOT: MultiStageStageMBB
; SMSONLY-RMK-NOT: accepted II=
; SMSONLY-RMK-NOT: stage-mbb
;
; DUAL-RMK: accepted II=[[II:[0-9]+]]
; DUAL-RMK-SAME: measured-II=[[II]]
; DUAL-RMK-SAME: no-seq-fallback
; DUAL-RMK: qualify parcels-per-iter=[[II]]
; DUAL-RMK-SAME: searched-II=[[II]]
; Commit-path echo of the same parity (realized parcel recount).
; DUAL-RMK: swps measured-II=[[II]]
; DUAL-RMK-SAME: searched-II=[[II]]
; DUAL-RMK: stage-mbb prolog={{[1-9][0-9]*}}
; DUAL-RMK-SAME: epilog={{[1-9][0-9]*}} stages={{[2-9]|[1-9][0-9]+}}
; DUAL-RMK-SAME: peel-order=modulo-cycle

define i32 @fir_delay2(ptr readonly %x, ptr readonly %c, i32 %n) {
; OFF-LABEL: fir_delay2:
; OFF-NOT:   set_hwloop
; OFF-NOT:   #<swps>
; OFF:       jalr
;
; HWON-LABEL: fir_delay2:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   #<swps>
; HWON:       jalr
;
; SMSONLY-LABEL: fir_delay2:
; SMSONLY-NOT:   set_hwloop
; SMSONLY-NOT:   #<swps>
; SMSONLY:       jalr
;
; DUAL-LABEL: fir_delay2:
; DUAL:       set_hwloop_f2 0,
; DUAL:       #<swps> II=[[ASMII:[0-9]+]]
; DUAL:       #<swps> AchievedII=[[ASMII]]
; DUAL-NOT:   csrw{{.*}} 0x2{{[0-5]}}
; DUAL:       jalr
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

; Min-trip floor (F39): runtime trip needs a provable floor >= peel depth
; (stages=2 -> peel depth 1) or the candidate fails closed at PF-TRIP.
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
