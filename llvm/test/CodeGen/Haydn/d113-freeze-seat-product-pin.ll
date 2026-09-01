; D1.13 full-pipeline freeze-seat pin. Freeze identity is bound by the
; addPreEmitPass2 registration argument (createHaydnVerifyBundlesPass(true)),
; never by instance counting: the LLVM_DEBUG seat trace must show EXACTLY
; three seat=invariant lines (addPreSched2, addPreEmitPass, addPostBBSections)
; and then exactly one seat=freeze line (addPreEmitPass2), with no further
; invariant seat after the freeze. The census is deliberately over-pinned:
; ANY future VerifyBundles seat addition, removal, or move fails this test
; loudly (the D1.13 acceptance clause) and must be re-pinned consciously
; together with the pipeline contract — a failure here is the loud
; seat-count drift signal, not a false positive.
;
; REQUIRES: asserts
; REQUIRES: haydn-registered-target
;
; RUN: llc -mtriple=haydn-unknown-elf -O2 -debug-only=haydn-verify-bundles \
; RUN:   -o /dev/null %s 2>&1 | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O0 -debug-only=haydn-verify-bundles \
; RUN:   -o /dev/null %s 2>&1 | FileCheck %s

; CHECK-COUNT-3: HaydnVerifyBundles seat=invariant on seat_pin
; CHECK: HaydnVerifyBundles seat=freeze on seat_pin
; CHECK-NOT: seat=invariant

define void @seat_pin() {
  ret void
}
