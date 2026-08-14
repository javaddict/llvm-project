; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s --check-prefix=O2
;
; REGRESSION TEST: T-SM2 / SM-H1 — SIN_COS / ARCTAN dest Data_Latency.
;
; Bug: golden VLIW_Engine_Compiler_Constraints.md §Pipeline Hazards documents
; Data_Latency = (uimm4 + 2) for SIN_COS and ARCTAN, with the result available
; at bundle t+(uimm4+2). Logical opcodes had SchedClass 0 and _S* members used
; latency-1 ALU classes, so LatencyStalls::defLatency inserted zero stalls and
; a consumer could read the dest in the next bundle (silent silicon hazard).
;
; Conservative model (HaydnSchedule.td Slot*_ALU_SinCosLat): uimm4 is 4-bit, so
; the closed bound is 15+2 = 17. Exact uimm4-dependent windows wait for GE96-04.
; InstrStage occupancy stays 1 cycle (ProductMaxInstrStageCycles=1); the
; uimm4+2 unit lock remains the named class-3 residual.
;
; Test design: uimm4=0 (true hardware window would be 2) still requires ≥16
; intervening bundles before the dest is read, because the itinerary is the
; max-17 bound. If the class regresses to latency 1, CHECK-COUNT-16 fails.
;
; O0 is the LatencyStalls net; O2 is the scheduler + auditor. Both must keep
; the consumer ≥16 bundles after the producer.

declare i64 @llvm.haydn.sin.cos(i32, i32)
declare i32 @llvm.haydn.arctan(i64, i32)

; O0-LABEL: sin_cos_then_use:
; O0:      sin_cos
; O0-COUNT-16: { nop
; O0:      {{add64|or64|xor64}}
; O2-LABEL: sin_cos_then_use:
; O2:      sin_cos
; O2-COUNT-16: { nop
; O2:      {{add64|or64|xor64}}
define i64 @sin_cos_then_use(i32 %phase) nounwind {
  %v = call i64 @llvm.haydn.sin.cos(i32 %phase, i32 0)
  %s = add i64 %v, %v
  ret i64 %s
}

; O0-LABEL: arctan_then_use:
; O0:      arctan
; O0-COUNT-16: { nop
; O0:      {{add32|addi32|or32|xor32}}
; O2-LABEL: arctan_then_use:
; O2:      arctan
; O2-COUNT-16: { nop
; O2:      {{add32|addi32|or32|xor32}}
define i32 @arctan_then_use(i64 %xy) nounwind {
  %v = call i32 @llvm.haydn.arctan(i64 %xy, i32 0)
  %s = add i32 %v, %v
  ret i32 %s
}
