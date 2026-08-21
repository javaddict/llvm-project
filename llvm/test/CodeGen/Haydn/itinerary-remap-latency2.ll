; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   < %s | FileCheck %s
;
; REGRESSION TEST: itinerary re-map + latency-2 classes (2026-08-21,
; audit_itinerary.md + latency P0).
;
; Bug class 1 (stale-too-narrow / wrong-family): logical shells ADD64/
; X2ADD32/... carried Slot12_ALU (ALU1|ALU2 only) and X2MUL32/X4MUL16/
; X2CMUL32_F2 carried ALU classes while golden Available says
; ALU0|ALU1|ALU2 / MAC0|MAC1. The post-RA hazard recognizer booked wrong
; unit bits: slot-0 starvation for the ALU block; zero MAC reservation
; plus false ALU conflicts for MAC-family multiplies.
;
; Bug class 2 (latency P0, silent-wrong-code class): golden
; instruction_type_index Pipeline_Info pins Data_Latency=2 for
; LOG2/EXP2/RECIP/SQRT; the itinerary modeled 1. Haydn has no interlock,
; so a consumer in the next parcel would read stale data. Fix:
; Slot12_ALU_DspLat (OperandCycles [2]) on the logicals + generated
; per-slot member rows (same shape as the SIN_COS SinCosLat precedent).
;
; Test design: chain two DSP-unary ops. The CHECK pins the CONTRACT —
; producer and consumer land in DIFFERENT parcels with an all-NOP filler
; parcel between them (the architectural-NOP cycle the latency-2 model
; must insert). Before the fix, sqrt/log2 packed into adjacent parcels
; with no separation. If the itinerary regresses to latency 1, the
; filler parcel disappears and CHECK-NEXT fails.

define i32 @lat2_sqrt_chain(i32 %x) {
entry:
  %a = call i32 @llvm.haydn.sqrt(i32 %x)
  %b = call i32 @llvm.haydn.log2(i32 %a)
  ret i32 %b
}

define i32 @lat2_recip_exp2(i32 %x) {
entry:
  %a = call i32 @llvm.haydn.recip(i32 %x)
  %b = call i32 @llvm.haydn.exp2(i32 %a)
  ret i32 %b
}

; CHECK-LABEL: lat2_sqrt_chain:
; CHECK: { 	nop; 	nop; 	sqrt
; CHECK-NEXT: {{^}}	{ 	nop; 	nop }
; CHECK-NEXT: { 	nop; 	nop; 	log2

; CHECK-LABEL: lat2_recip_exp2:
; CHECK: { 	nop; 	nop; 	recip
; CHECK: { 	nop; 	nop; 	exp2

declare i32 @llvm.haydn.sqrt(i32)
declare i32 @llvm.haydn.log2(i32)
declare i32 @llvm.haydn.recip(i32)
declare i32 @llvm.haydn.exp2(i32)
