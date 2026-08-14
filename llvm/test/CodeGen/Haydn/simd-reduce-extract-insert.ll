; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — SIMD vector extract/insert element legalization.

; REGRESSION TEST: SIMD vector extract/insert element legalization.
;
; Bug: G_EXTRACT_VECTOR_ELT and G_INSERT_VECTOR_ELT were declared as
; customFor in HaydnLegalizerInfo.cpp but legalizeCustom only handled
; G_TRUNC. If these opcodes reached legalization, the legalizer would return
; false (unhandled), causing a crash with -global-isel-abort=1.
;
; Fix: Added handlers in legalizeCustom that delegate to the legalizer
; helper's lowerExtractInsertVectorElt, which handles both constant and
; variable indices via scalarization or stack-based lowering.
;
; If the legalization regresses, llc will crash with "unable to legalize"
; at -global-isel-abort=1.

;===----------------------------------------------------------------------===;
; G_EXTRACT_VECTOR_ELT with constant index on v2i32
;===----------------------------------------------------------------------===;

define i32 @extract_elt_v2i32_idx0(<2 x i32> %v) nounwind {
; CHECK-LABEL: extract_elt_v2i32_idx0:
  %e = extractelement <2 x i32> %v, i32 0
  ret i32 %e
}

define i32 @extract_elt_v2i32_idx1(<2 x i32> %v) nounwind {
; CHECK-LABEL: extract_elt_v2i32_idx1:
; Native move32_dr_h (1 op) replaces 5-op stack spill.
; CHECK: move32_dr_h
  %e = extractelement <2 x i32> %v, i32 1
  ret i32 %e
}

;===----------------------------------------------------------------------===;
; G_EXTRACT_VECTOR_ELT with constant index on v4i16
;===----------------------------------------------------------------------===;

define i16 @extract_elt_v4i16_idx0(<4 x i16> %v) nounwind {
; CHECK-LABEL: extract_elt_v4i16_idx0:
  %e = extractelement <4 x i16> %v, i32 0
  ret i16 %e
}

define i16 @extract_elt_v4i16_idx3(<4 x i16> %v) nounwind {
; CHECK-LABEL: extract_elt_v4i16_idx3:
; High half-word of high GPR: srli32 (or legacy or32 pack path).
; CHECK: {{srli32|or32}}
  %e = extractelement <4 x i16> %v, i32 3
  ret i16 %e
}

;===----------------------------------------------------------------------===;
; G_INSERT_VECTOR_ELT with constant index on v2i32
;===----------------------------------------------------------------------===;

define <2 x i32> @insert_elt_v2i32(<2 x i32> %v, i32 %val) nounwind {
; CHECK-LABEL: insert_elt_v2i32:
; G-ABI-VEC: arg is DR; insert lane 1 via stack st32/ld64 (or move32 into high
; half). Both are correct legalizations of G_INSERT_VECTOR_ELT.
; CHECK-DAG: {{st32|move32}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = insertelement <2 x i32> %v, i32 %val, i32 1
  ret <2 x i32> %r
}
