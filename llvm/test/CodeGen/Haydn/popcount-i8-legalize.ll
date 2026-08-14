; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s 2>&1 | FileCheck %s

; Role: semantic — G_CTPOP (and the G_CTTZ/G_CTLZ family) on an s8 operand must legalize without crashing.

; REGRESSION TEST: G_CTPOP (and the G_CTTZ/G_CTLZ family) on an s8 operand
; must legalize without crashing.
;
; Bug: the bitcount action declared `.lowerFor({S16,S32,S64}).minScalar(0,S16)`.
; For an s8 input the legalizer first widened ONLY the result (widenScalarDst)
; s8->s16, then — seeing TypeIdx 0 now legal — fell through to the generic
; lowerBitCount while the source was still s8. lowerBitCount builds every
; shift/mask intermediate in the SOURCE type (s8) and writes the final shift
; into the s16 destination, so MachineIRBuilder::validateShiftOp asserted
; `(Res == Op0) && "type mismatch"` and clang crashed (exit 134, no.s). This
; fired for __builtin_popcount((unsigned char)x) at O1/O2.
;
; Fix: lower s8 DIRECTLY (DstTy == SrcTy == s8, no result-only widening);
; lowerBitCount then sees matched types and the s8 intermediates widen to s32
; via Haydn's generic sub-word rule.
;
; This test must COMPILE (no assertion); if the bug regresses, llc aborts and
; FileCheck sees no output. We only assert the function assembles.

declare i8 @llvm.ctpop.i8(i8)
declare i8 @llvm.cttz.i8(i8, i1)
declare i8 @llvm.ctlz.i8(i8, i1)

define i8 @popcount_i8(i8 %x) {
; CHECK-LABEL: popcount_i8:
; CHECK:       jalr{{(\.s[012])?}}
  %c = call i8 @llvm.ctpop.i8(i8 %x)
  ret i8 %c
}

define i8 @cttz_i8(i8 %x) {
; CHECK-LABEL: cttz_i8:
; CHECK:       jalr{{(\.s[012])?}}
  %c = call i8 @llvm.cttz.i8(i8 %x, i1 false)
  ret i8 %c
}

define i8 @ctlz_i8(i8 %x) {
; CHECK-LABEL: ctlz_i8:
; CHECK:       jalr{{(\.s[012])?}}
  %c = call i8 @llvm.ctlz.i8(i8 %x, i1 false)
  ret i8 %c
}
