; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:     -verify-machineinstrs < %s 2>&1 | FileCheck %s

; REGRESSION TEST: v8i8 lane-extract must materialise MOVE32_DR_L/H (Hard #2).
;
; Bug: the G_UNMERGE_VALUES selector handled s64->2xs32 (MOVE32_DR_L/H) and
; s64->4xs16 (MOV_DR64_TO_GPR + shift/mask) but NOT s64->8xs8 (v8i8 lane
; extract). The legalizer custom-lowers G_EXTRACT_VECTOR_ELT of v8i8 to a
; bitcast to s64 + G_UNMERGE_VALUES to 8 x s8 (HaydnLegalizerInfo.cpp). With
; no selector arm for that shape, the generic fallback emitted buildCopy for
; each byte lane -> a generic cross-bank COPY ($r = COPY $d) reached regalloc
; -> post-RA expansion called copyPhysReg on a DR64->GPR32 pair. copyPhysReg
; had no cross-bank case: it unconditionally built MOVE32 with DR64 sources,
; and MC fillFormatEMemberInst failed (MOVE32_E3_E0_ALU2_R expects 2 GPR
; operands, got 1 GPR + 2 DR) - silent wrong-code / MC abort (Hard #2).
; Manifests on simd-6 / pr70903 (8 fail-slots) at all opt levels.
;
; Fix (two layers, both required):
;   (1) Symptom gate (HaydnInstrInfo::copyPhysReg): a physical COPY is only
;       defined within one register bank. Cross-bank DR64<->GPR32 data motion
;       MUST go through MOVE32_DR_L/H or MOV_GPR_TO_DR64; copyPhysReg fails
;       closed with a pointed message naming the owning selector. This is a
;       correctness gate, NOT a monkey-patch - it converts silent MC wrong-
;       code into a loud, owning-layer-named error.
;   (2) Root cause (HaydnInstructionSelector G_UNMERGE_VALUES): a new
;       s64->8xs8 arm materialises every byte-lane extract via
;       MOVE32_DR_L (bytes 0..3) + MOVE32_DR_H (bytes 4..7) + shift/mask,
;       mirroring the s64->2xs32 model. No generic cross-bank COPY survives
;       to copyPhysReg.
;
; Test design: bitcast an i64 to <8 x i8>, extract every lane, and sum them.
; The legalizer lowers each extractelement to G_UNMERGE_VALUES s64->8xs8; the
; selector must emit MOVE32_DR_L/_H (one per DR64 half) for the two 32-bit
; lane words, then SRLI32+ANDI32 for each byte within. CHECK asserts that
; MOVE32_DR_L and MOVE32_DR_H both appear (cross-bank materialisation) and
; that NO bare cross-bank COPY reaches copyPhysReg (no fatal, clean compile).

; The pre-fix fatal must NOT appear.
; CHECK-NOT: LLVM ERROR
; CHECK-NOT: cross-bank COPY in copyPhysReg

define i32 @v8i8_lane_extract_sum(i64 %v) nounwind {
; CHECK-LABEL: v8i8_lane_extract_sum:
entry:
  ; Bitcast to <8 x i8> forces the legalizer's v8i8 -> s64 unmerge path.
  %vec = bitcast i64 %v to <8 x i8>
  ; Extract every lane. Each lane is a byte from the DR64-resident vector;
  ; the selector must materialise the extracts via MOVE32_DR_L/H.
  %e0 = extractelement <8 x i8> %vec, i32 0
  %e1 = extractelement <8 x i8> %vec, i32 1
  %e2 = extractelement <8 x i8> %vec, i32 2
  %e3 = extractelement <8 x i8> %vec, i32 3
  %e4 = extractelement <8 x i8> %vec, i32 4
  %e5 = extractelement <8 x i8> %vec, i32 5
  %e6 = extractelement <8 x i8> %vec, i32 6
  %e7 = extractelement <8 x i8> %vec, i32 7
  %z0 = zext i8 %e0 to i32
  %z1 = zext i8 %e1 to i32
  %z2 = zext i8 %e2 to i32
  %z3 = zext i8 %e3 to i32
  %z4 = zext i8 %e4 to i32
  %z5 = zext i8 %e5 to i32
  %z6 = zext i8 %e6 to i32
  %z7 = zext i8 %e7 to i32
  %s01 = add i32 %z0, %z1
  %s23 = add i32 %z2, %z3
  %s45 = add i32 %z4, %z5
  %s67 = add i32 %z6, %z7
  %s0123 = add i32 %s01, %s23
  %s4567 = add i32 %s45, %s67
  %sum = add i32 %s0123, %s4567
  ret i32 %sum
}

; The low 32 bits of %v are extracted via MOVE32_DR_L (bytes 0..3).
; CHECK: move32_dr_l
; The high 32 bits of %v are extracted via MOVE32_DR_H (bytes 4..7).
; CHECK: move32_dr_h
