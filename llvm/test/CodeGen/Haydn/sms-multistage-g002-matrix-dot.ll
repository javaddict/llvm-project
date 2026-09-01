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
; Purpose: lock the CURRENT multistage-SMS behavior of the load-MAC-store
; dot-product kernel class across the full 2x2 flag matrix
; (-haydn-enable-hwloops on/off x -haydn-enable-multistage-sms on/off),
; BEFORE any next-wave code lands (W59 routing, W61 Off1, remark changes),
; so later changes cannot silently regress it.
;
; Invariant locked (per the 2026-08-22 G004 qualification discipline):
;   OFF      — no hwloop SET, no SWPS artifact, no acceptance remark.
;   HWON     — hardware loop arms (set_hwloop_f2) but NO SWPS annotation.
;   SMSONLY  — SMS alone never candidates a count-up ZOL loop (no SET, no
;              swps stamp, no acceptance remark).
;   DUAL     — SMS accepts the kernel with II parity (accepted II ==
;              measured-II == parcels-per-iter == searched-II), commits a
;              kernel-only stage schedule under the same hardware-loop SET,
;              and the AsmPrinter AchievedII stamp equals the scheduled II.
;
; The explicit flags make every arm default-independent (precedent:
; hwloop-multistage-combined-matrix.ll). II values are CAPTURED, not
; pinned — the lock is the parity relations and artifact presence, not one
; accidental schedule (noise rule).
;
; Kernel-shape notes (why this IR is exactly this): the accepting spine
; requires an EMPTY dedicated preheader (a store in the preheader folds
; entry into a two-successor block and breaks PF-CFG candidacy) and
; llvm.loop.itercount.range MD (F39: unproven runtime trip fails closed
; at PF-TRIP; MD floor >= peel depth is required for acceptance).

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

define void @dot_store(ptr readonly %a, ptr readonly %b, ptr %dst, i32 %n) {
; OFF-LABEL: dot_store:
; OFF-NOT:   set_hwloop
; OFF-NOT:   #<swps>
; OFF:       jalr
;
; HWON-LABEL: dot_store:
; HWON:       set_hwloop_f2 0,
; HWON-NOT:   #<swps>
; HWON:       jalr
;
; SMSONLY-LABEL: dot_store:
; SMSONLY-NOT:   set_hwloop
; SMSONLY-NOT:   #<swps>
; SMSONLY:       jalr
;
; DUAL-LABEL: dot_store:
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
  %pa = getelementptr inbounds i32, ptr %a, i32 %i
  %pb = getelementptr inbounds i32, ptr %b, i32 %i
  %pd = getelementptr inbounds i32, ptr %dst, i32 %i
  %va = load i32, ptr %pa, align 4
  %vb = load i32, ptr %pb, align 4
  %m = mul i32 %va, %vb
  %s.n = add i32 %s, %m
  store i32 %s.n, ptr %pd, align 4
  %i.n = add i32 %i, 1
  %c = icmp ult i32 %i.n, %n
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}

; Min-trip floor (F39): runtime trip needs a provable floor >= peel depth
; or the candidate fails closed at PF-TRIP.
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
