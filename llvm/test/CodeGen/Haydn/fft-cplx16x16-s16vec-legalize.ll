; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 %s -o - | FileCheck %s
;
; REGRESSION TEST: <4 x s16> bitwise ops (G_OR/G_AND/G_XOR) must legalize.
;
; Bug: HaydnLegalizerInfo.cpp's bitwise rule declared
; legalFor({S32, S64, V2I32})
; but OMITTED V4I16, even though every sibling SIMD rule (G_ADD/G_SUB/G_MUL
; G_SHL/G_LSHR/G_ASHR, G_PHI, G_SELECT) lists V4I16. The cplx16/real16 FFT
; kernels (fft_cplx16x16_hifi3 etc.) lower 4x16 SIMD on DR64 lanes and emit
; G_OR/G_AND/G_XOR <4 x s16>, which aborted with:
; LLVM ERROR: unable to legalize instruction: G_OR <4 x s16>
; Five kernels were blocked: fft_cplx16x16, ifft_cplx16x16, fft_real16x16
; ifft_real16x16, and the 5th in the fft cplx16/real16 family.
;
; Fix : add V4I16 to the G_AND/G_OR/G_XOR legalFor set. A <4 x s16>
; lane is a 64-bit DR64 value, and a packed-s16 bitwise IS the 64-bit DR64
; bitwise (AND64/OR64/XOR64) — identical to how <2 x s32> already lowers.
; The selector (HaydnInstructionSelector.cpp G_AND/G_OR/G_XOR cases) keys
; purely on DstTy.getSizeInBits==64 -> DR64RegClass, so it already handles
; v4i16; only the legalizer rule was missing.
;
; Test design: each function does one <4 x s16> bitwise op. Before the fix
; every function aborts the legalizer with "unable to legalize G_{OR,AND,XOR}
; <4 x s16>". After the fix each selects to the matching 64-bit DR64 op.

;===----------------------------------------------------------------------===
; <4 x s16> bitwise OR -> OR64 (DR64)
;===----------------------------------------------------------------------===
define <4 x i16> @s16vec_or(<4 x i16> %a, <4 x i16> %b) nounwind {
; CHECK-LABEL: s16vec_or:
; CHECK: or64
  %r = or <4 x i16> %a, %b
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===
; <4 x s16> bitwise AND -> AND64 (DR64)
;===----------------------------------------------------------------------===
define <4 x i16> @s16vec_and(<4 x i16> %a, <4 x i16> %b) nounwind {
; CHECK-LABEL: s16vec_and:
; CHECK: and64
  %r = and <4 x i16> %a, %b
  ret <4 x i16> %r
}

;===----------------------------------------------------------------------===
; <4 x s16> bitwise XOR -> XOR64 (DR64)
;===----------------------------------------------------------------------===
define <4 x i16> @s16vec_xor(<4 x i16> %a, <4 x i16> %b) nounwind {
; CHECK-LABEL: s16vec_xor:
; CHECK: xor64
  %r = xor <4 x i16> %a, %b
  ret <4 x i16> %r
}
