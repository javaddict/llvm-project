; RUN: llc -mtriple=haydn-unknown-elf -O2 -global-isel-abort=1 < %s | FileCheck %s
;
; REGRESSION TEST: Align(non-power-of-2) assert in
; HaydnTargetLowering::allowsMisalignedMemoryAccesses via ConstantHoisting.
;
; Bug: yarpgen seed14 driver.c aborted clang (exit 134) with
;   llvm/Support/Alignment.h:70: Assertion `isPowerOf2_64(Value)' in
;   Align::Align(uint64_t), reached as ConstantHoistingPass::collectConstantCandidates
;   → HaydnTTIImpl::getIntImmCostInst(Store)
;   → TargetLoweringBase::allowsMemoryAccessForAlignment
;   → HaydnTargetLowering::allowsMisalignedMemoryAccesses.
; The hook built `Align(SizeBits / 8)` from the store byte width. Widths that
; are not a power of two (i96=12B, i80=10B, i24=3B) constructed Align(12),
; Align(10), Align(3) — the Align ctor asserts isPowerOf2_64. The hook is only
; reached when the store's IR alignment is BELOW the ABI alignment of the type
; (allowsMemoryAccessForAlignment early-returns true otherwise), hence every
; offending store here carries align 1 or 2.
;
; Fix/design: natural alignment for a non-power-of-two width is the largest
; power of two dividing the byte width (12→4, 10→2, 3→1) — the strictest
; alignment power-of-two tiling imposes (12B = 3×LD32 needs ≥4). Power-of-two
; widths reproduce the old byte count exactly, so all prior results are
; unchanged. The one rule: never construct Align from a raw byte count.
;
; If the bug returns, llc aborts (exit 134 / SIGABRT) inside Constant Hoisting
; on the first function below — any non-pow2-width constant store with
; sub-ABI alignment crashes -O2 clang on yarpgen seed14-class inputs.

define void @store_i96_align1(ptr %p) {
; CHECK-LABEL: store_i96_align1:
  store i96 123456789012345678901234567, ptr %p, align 1
  ret void
}

define void @store_i96_align2(ptr %p) {
; CHECK-LABEL: store_i96_align2:
  store i96 -98765432109876543210987654321, ptr %p, align 2
  ret void
}

define void @store_i80_align1(ptr %p) {
; CHECK-LABEL: store_i80_align1:
  store i80 1234567890123456789012345, ptr %p, align 1
  ret void
}

define void @store_i24_align1(ptr %p) {
; CHECK-LABEL: store_i24_align1:
  store i24 1234567, ptr %p, align 1
  ret void
}

; Power-of-two widths keep their exact byte alignment: this v4i16 (64-bit)
; store at align 2 must still be REJECTED as misaligned (rejecting
; `load <4 x i16> align 2` is the coremark matrix_add_const contract), so the
; hook's misaligned path must still run for it — it just must not crash.
define void @store_v4i16_align2(ptr %p) {
; CHECK-LABEL: store_v4i16_align2:
  store <4 x i16> <i16 1, i16 2, i16 3, i16 4>, ptr %p, align 2
  ret void
}
