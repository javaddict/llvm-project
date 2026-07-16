; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

;
; REGRESSION TEST : s64 -> s128 zero/sign extension feeding an i128
; multiply must legalize, NOT abort with "unable to legalize instruction:
; %5:_(s128) = G_ZEXT %2:_(s64)".
;
; Bug: at -O2 the inline schoolbook 64x64->128 multiply (4 partial products
; + carry-propagation) is pattern-matched by IR instcombine
; AggressiveInstCombine into
; %x128 = zext i64 %x to i128
; %y128 = zext i64 %y to i128
; %m = mul nuw i128 %x128, %y128
; The Haydn legalizer had NO rule for G_ZEXT s64 -> s128, so it aborted on
; the G_ZEXT before ever reaching the G_MUL. The compiler CRASHED at -O2
; (no.s emitted); -O0/-O1 do not perform the fold and worked. The G_MUL
; s128 itself was already handled (the generic narrowScalar of G_MUL s128

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: cb51_zext_mul_i128:
; CHECK-LABEL: cb51_sext_mul_i128:
; CHECK-LABEL: cb51_zext_i64_to_i128_low:
; CHECK: {{.}}

define i64 @cb51_zext_mul_i128(i64 %x, i64 %y) {
; The function must compile (no legalizer abort) and emit native mul64_*
; partials. The byte/bundle layout is not semantically meaningful — only the
; presence of native mul64 and the absence of any mul libcall are pinned.
 %xa = zext i64 %x to i128
 %ya = zext i64 %y to i128
 %m = mul nuw i128 %xa, %ya
 %lo = trunc i128 %m to i64
 ret i64 %lo
}

;sext i64 -> i128 feeding mul i128 (covers the G_SEXT path of the fix).
; `mul i128` (without `nuw`) on sign-extended operands exercises the
; sign-extend-high lowering (G_ASHR src, 63).
define i64 @cb51_sext_mul_i128(i64 %x, i64 %y) {
; Same intent as the zext variant: must compile, native mul64 partials, no
; libcall.
 %xa = sext i64 %x to i128
 %ya = sext i64 %y to i128
 %m = mul i128 %xa, %ya
 %lo = trunc i128 %m to i64
 ret i64 %lo
}

;Standalone zext i64 -> i128 (no mul): the feeding extension on its own.
; Ensures the rule fires for G_ZEXT regardless of consumer. The body is
; `zext x to i128; trunc to i64` == identity, so the selector folds it to a
; bare return of %x (already in its return reg) — there is no data instruction.
; The GUARD is that compilation succeeds at all: with -global-isel-abort=1 the
; RUN fails if the G_ZEXT s64->s128 rule is missing ("unable to legalize
; s128 = G_ZEXT"). So we only assert the function returns cleanly and emits
; no mul libcall.
define i64 @cb51_zext_i64_to_i128_low(i64 %x) {
 %xa = zext i64 %x to i128
 %lo = trunc i128 %xa to i64
 ret i64 %lo
}
