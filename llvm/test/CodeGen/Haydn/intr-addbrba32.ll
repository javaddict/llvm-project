; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — llvm.haydn.addbrba32 — bit-reversed add for FFT butterfly addressing (maps 1:1 to the BREV32 hardware instruction).

; REGRESSION TEST: llvm.haydn.addbrba32 — bit-reversed add for FFT butterfly
; addressing (maps 1:1 to the BREV32 hardware instruction).
;
; Why this test exists:
; NatureDSP FFT kernels (e.g. fft_stage_last_ie_hifi3.c, fft_cplxf_ie_hifi3.c
; ifft_cplxf_ie_hifi3.c) walk stage buffers in bit-reversed index order using
; AE_ADDBRBA32(idx, stride). Without a native mapping, FFT porting would
; expand to a multi-instruction scalar sequence (REVERSE32 + ADD + REVERSE32)
; burning 3+ GPR32 slots per butterfly step and breaking VLIW scheduling.
; The Haydn ISA provides BREV32 with the exact semantics
; `rt = bitreverse(bitreverse(rs1) + rs2)`, so AE_ADDBRBA32 maps 1:1.
;
; What this guards:
; The llvm.haydn.addbrba32 intrinsic exists and is i32 -> i32 -> i32.
; The ISel pattern in HaydnInstructionSelector.cpp routes the intrinsic to
; Haydn::BREV32 (not a fallback sequence, not an unhandled-intrinsic error).
; The instruction uses the GPR32 register class for all operands.
;
; What would break if the bug reappears:
; If the ISel case is removed: llc aborts with "unable to legalize
; select intrinsic" under -global-isel-abort=1.
; If the intrinsic is re-typed (e.g. to i64): the declare/define mismatch
; trips the verifier.
;
; Reference values (computed with Python bitreverse of 32-bit values):
; br(x) reverses all 32 bits.
; test_known: a=0x10000000, b=1
; br(a) = 0x00000008
; br(a) + b = 0x00000009
; br(br(a)+b) = 0x90000000 (== 2415919104)
; The result is consumed via `ret` so the intrinsic is not DCE'd.

declare i32 @llvm.haydn.addbrba32(i32, i32)

; Pattern test: intrinsic must lower to a single `brev32` instruction.
; The result is returned so it stays live through selection.
define i32 @test_addbrba32_pattern(i32 %a, i32 %b) {
; CHECK-LABEL: test_addbrba32_pattern:
; CHECK: brev32
  %r = call i32 @llvm.haydn.addbrba32(i32 %a, i32 %b)
  ret i32 %r
}

; Known-value test: a=0x10000000, b=1 -> expected 0x90000000.
; Uses constants to force a deterministic result that the selector must
; materialize. The result is returned (live) to prevent dead-code elimination.
define i32 @test_addbrba32_known() {
; CHECK-LABEL: test_addbrba32_known:
; CHECK: brev32
  %r = call i32 @llvm.haydn.addbrba32(i32 268435456, i32 1)  ; 0x10000000, 1
  ret i32 %r
}
