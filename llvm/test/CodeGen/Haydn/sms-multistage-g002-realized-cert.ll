; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.rmk | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
; RUN: FileCheck %s --check-prefix=ACC < %t.rmk
;
; G002 SMS LOCKING TEST LAYER (ultragoal story G002, 2026-08-23).
;
; Purpose: REALIZED-PARCEL CERTIFICATE PIN on the multi-stage stage-peel
; kernel class (FIR tap-delay), extending the sms-multistage-measured-ii
; discipline from the kernel-only form to the prolog/kernel/epilog form.
;
; REGRESSION TEST LAW (contract):
;   The accepted-II remark must equal the REALIZED parcel stream, not the
;   planned one. Every acceptance must print the closed certificate chain
;     accepted II=N  +  measured-II=N  (SAME line, one remark)
;     qualify parcels-per-iter=N searched-II=N   (one remark)
;     swps measured-II=N searched-II=N            (commit echo)
;   with the SAME N in all three — plus, for the staged form, the
;   stage-mbb line's measured-II=N and an AsmPrinter AchievedII=N stamp
;   equal to the scheduled II. If the realized recount ever disagrees
;   (parcel lost/gained in peel materialization), the engine must REJECT
;   (exhausted:), never accept with a lying II — and this test fails the
;   moment any of those lines diverges or disappears.
;
; Negative-arm discipline: the refusal kernel (unproven runtime trip) must
; produce NO accepted-II line at all — CHECK-NOT below — while its
; hardware loop still arms (refusal is SMS-scoped, never object-wide).
;
; Kernel choice: the FIR delay-line body is the deepest materialization
; path G002 locks (stages=2, real prolog/epilog peels — the dot form is
; kernel-only and cannot exercise peel-time parcel drift).

target triple = "haydn-unknown-elf"

; RMK-NOT: sequential (preflight)
; RMK: no-seq-fallback
; RMK: resource-bias=slot-windows
;
; ACC: accepted II=[[II:[0-9]+]]
; ACC-SAME: measured-II=[[II]]
; ACC-SAME: no-seq-fallback
; ACC: qualify parcels-per-iter=[[II]]
; ACC-SAME: searched-II=[[II]]
; ACC: swps measured-II=[[II]]
; ACC-SAME: searched-II=[[II]]
; ACC: stage-mbb prolog={{[1-9][0-9]*}}
; ACC-SAME: epilog={{[1-9][0-9]*}} stages={{[2-9]|[1-9][0-9]+}}
; ACC-SAME: measured-II=[[II]]
; ACC-NOT: sequential
;
; Negative arm: exactly one acceptance exists (the FIR kernel); the
; memcpy refusal contributes none. Count enforced by the single ACC
; chain above plus the explicit NOT on a second acceptance after it.
; ACC-NOT: accepted II=

; ASM-LABEL: fir_delay2:
; ASM: set_hwloop_f2 0,
; ASM: #<swps> II=[[II:[0-9]+]] cycles per pipeline stage (SMS schedule)
; ASM: #<swps> stages={{[2-9]|[1-9][0-9]+}}
; ASM: #<swps> AchievedII=[[II]] (kernel parcels)
; ASM: jalr
; ASM-LABEL: memcpy_var:
; ASM: set_hwloop_f2 0,
; ASM-NOT: #<swps>
; ASM: jalr

define i32 @fir_delay2(ptr readonly %x, ptr readonly %c, i32 %n) {
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

; Refusal kernel: identical shape family, unproven runtime trip, NO MD —
; must NOT accept (F39 fail-closed) while the hardware loop still arms.
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

; Min-trip floor for the accepting kernel only (F39).
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
