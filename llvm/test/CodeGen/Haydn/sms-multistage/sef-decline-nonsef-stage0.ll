; RUN: llc -mtriple=haydn-unknown-elf -haydn-sms-containment-max=1 -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s 2>%t.rmk \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: FileCheck %s --check-prefix=RMK < %t.rmk
;
; G007 SEF peel corpus (SF10 completion, 2026-08-23).
; Class: NON-SEF boundary control — the trip-refused NS=3 schedule has a
; stage-0 that WRITES THE CARRIED ACCUMULATOR (the phi-feeding MULL sits
; at a cycle < II), so isSEFPeelableMI must refuse: an extra peeled
; instance of a phi def overwrites the carried value (def overlaps a
; live-in; also a live-out — the exit reads the accumulator).
;
; Expected TODAY and AFTER G007 identically: the NS=3 II is refused
; (trip 2 < 3, no SEF rescue), the search proceeds to the II where NS=2
; and accepts there — but with NO sef-peel field. The control pins that
; the peel NEVER fires on a non-SEF stage-0: the accept remark carries no
; sef-peel=1, and the loop is accepted (fail-closed rescue, not a
; decline — the engine still finds the deeper-II plan).
;
; REGRESSION TEST: if isSEFPeelableMI is loosened to ignore live-in/
; live-out def overlap (AIE's weaker rule), this fixture would accept at
; the LOWER II with sef-peel=1 — silently executing the accumulator MULL
; extra times and corrupting the reduction. The absent sef-peel field is
; the pin.
;
; RMK-NOT: sef-peel=1
; RMK: Schedule found II=[[II:[0-9]+]] NS=[[NS:[0-9]+]] prologue=[[P:[0-9]+]] parcels epilogue=[[E:[0-9]+]] parcels kind=accepted loop=bb.{{[0-9]+}}.loop
;
; ASM: nonssef:
; ASM: set_hwloop_f2
; ASM: jalr

target triple = "haydn-unknown-elf"

define i32 @nonssef(ptr readonly %p) {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.n, %loop]
  %s = phi i32 [1, %entry], [%s.n, %loop]
  %x = phi i32 [3, %entry], [%x.n, %loop]
  %v = load i32, ptr %p, align 4
  %u1 = add i32 %v, %v
  %u2 = sub i32 %u1, %v
  %u3 = xor i32 %u2, %v
  %u4 = and i32 %u3, %v
  %u5 = or i32 %u4, %v
  %u6 = add i32 %u5, %v
  %u7 = sub i32 %u6, %v
  %u8 = xor i32 %u7, %v
  %a = mul i32 %s, %x
  %b = mul i32 %a, %x
  %c = mul i32 %b, %x
  %s.n = add i32 %c, %u8
  %x.n = add i32 %x, 0
  %i.n = add i32 %i, 1
  %cc = icmp ult i32 %i.n, 2
  br i1 %cc, label %loop, label %exit, !llvm.loop !0
exit:
  ret i32 %s.n
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 2}
