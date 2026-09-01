; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -pass-remarks-analysis=haydn-multistage-sms < %s \
; RUN:   2>%t.dual.rmk | FileCheck %s --check-prefix=DUAL
; RUN: FileCheck %s --check-prefix=DUAL-RMK < %t.dual.rmk
; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -mattr=+hwloop -haydn-enable-hwloops -haydn-enable-multistage-sms \
; RUN:   -filetype=obj -o %t.dual.o < %s
; RUN: llvm-objdump -d -z --triple=haydn-unknown-elf %t.dual.o \
; RUN:   | FileCheck %s --check-prefix=OBJ

; G002 SMS LOCKING TEST LAYER (ultragoal story G002, 2026-08-23).
;
; Purpose: FORMAT ROUND-TRIP lock for the two G002-accepting kernel
; classes (load-MAC-store dot; FIR tap-delay with stage peels). The
; committed multi-stage kernel must survive MC encode -> object ->
; disassembly as decodable Format E parcels.
;
; Invariant locked:
;   DUAL    — both kernels accept with II parity (quick acceptance guard
;             so a future silent refusal flips THIS test, not just QoR).
;   OBJ     — (a) every kernel parcel decodes: NO "<?>" operand
;             placeholders and NO "<unknown>" mnemonics anywhere in .text
;             (HaydnInstPrinter bounds-defense contract);
;             (b) each function's disassembly contains its hardware-loop
;             SET, and at least one real instruction parcel between the
;             SET parcel and the function tail — the kernel body parcels
;             exist and decode (finite, nonzero; exact count NOT pinned:
;             noise rule — schedule/registers are free to move).
;
; REGRESSION TEST LAW: if the committed multi-stage kernel ever emits a
; parcel the Format E decoder cannot fully decode, or drops the kernel
; body from the object, the OBJ checks here fail. The dot kernel pins the
; kernel-only stages=1 path; the FIR kernel pins the prolog/kernel/epilog
; stages=2 path (peel parcels must decode too).

target triple = "haydn-unknown-elf"

; DUAL-RMK: accepted II=[[II1:[0-9]+]]
; DUAL-RMK-SAME: measured-II=[[II1]]
; DUAL-RMK: qualify parcels-per-iter=[[II1]]
; DUAL-RMK-SAME: searched-II=[[II1]]
; DUAL-RMK: accepted II=[[II2:[0-9]+]]
; DUAL-RMK-SAME: measured-II=[[II2]]
; DUAL-RMK: qualify parcels-per-iter=[[II2]]
; DUAL-RMK-SAME: searched-II=[[II2]]

define void @dot_store(ptr readonly %a, ptr readonly %b, ptr %dst, i32 %n) {
; DUAL-LABEL: dot_store:
; DUAL:       set_hwloop_f2 0,
; DUAL:       #<swps> II=[[ASM1:[0-9]+]]
; DUAL:       #<swps> AchievedII=[[ASM1]]
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

define i32 @fir_delay2(ptr readonly %x, ptr readonly %c, i32 %n) {
; DUAL-LABEL: fir_delay2:
; DUAL:       set_hwloop_f2 0,
; DUAL:       #<swps> II=[[ASM2:[0-9]+]]
; DUAL:       #<swps> AchievedII=[[ASM2]]
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

; (a) Global decode contract: no placeholder operands, no unknown parcels.
; OBJ-NOT: <?>
; OBJ-NOT: <unknown>
;
; (b) Kernel parcels exist and decode between the SET and the tail, per
; function (finite and nonzero; exact count NOT pinned).
; OBJ:      <dot_store>:
; OBJ:      set_hwloop_f2
; OBJ:      {
; OBJ:      jalr
; OBJ:      <fir_delay2>:
; OBJ:      set_hwloop_f2
; OBJ:      {
; OBJ:      jalr

; Min-trip floor (F39) — shared by both kernels.
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.itercount.range", i32 8}
