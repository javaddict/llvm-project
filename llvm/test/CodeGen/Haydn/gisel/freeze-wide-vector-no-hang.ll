; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 < %s | FileCheck %s
;
; A wide vector must not survive its producer.
;
; G_FREEZE was `alwaysLegal()`, so a <16 x i32> was allowed to exist as a
; VALUE — and nothing else in the target can hold one. Every consumer narrowed
; it locally and something re-merged the pieces to feed the next, which is a
; loop the legalizer has no reason to leave: clang HUNG on gcc-c-torture
; pr28982a and pr28982b at -O2, reaching 356505 legalizations and register
; numbers past %300000 before the timeout.
;
; This function is the shape reduced out of those: load a wide vector, freeze
; it, index it with a variable. Without the fix llc does not terminate; with
; it, the whole function legalizes in 119 steps.
;
; The test is that it FINISHES and emits a function. There is no useful
; assertion about the instruction sequence here — the point is termination, and
; a hang is not a FileCheck failure but a timeout, so the CHECK below is
; deliberately minimal rather than a schedule pinned for its own sake.
;
; Chasing this at the consumers does not converge, which is worth knowing
; before trying: capping the custom vector→vector unmerge at a 128-bit source
; moved the loop one level down to <4 x s32>, and lowering that unmerge through
; a scalar bitcast instead of element extracts moved it to G_CONCAT_VECTORS
; re-forming the <16 x s32>.

define i32 @wide_freeze(ptr %p, i32 %i) {
; CHECK-LABEL: wide_freeze:
; CHECK: jalr
entry:
  %v = load <16 x i32>, ptr %p
  %f = freeze <16 x i32> %v
  %e = extractelement <16 x i32> %f, i32 %i
  ret i32 %e
}
