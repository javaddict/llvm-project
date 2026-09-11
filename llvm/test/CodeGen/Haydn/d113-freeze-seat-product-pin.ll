; D1.13 full-pipeline freeze-seat pin. Freeze identity is bound by the
; addPreEmitPass2 registration argument (createHaydnVerifyBundlesPass(true)),
; never by instance counting: the LLVM_DEBUG seat trace must show EXACTLY
; two seat=invariant lines (addPreSched2, addPreEmitPass) and then exactly
; one seat=freeze line (addPreEmitPass2), with no further invariant seat
; after the freeze. GR1.7 deleted the addPostBBSections Verify seat. The
; census is deliberately over-pinned: ANY future VerifyBundles seat
; addition, removal, or move fails this test loudly.
;
; REQUIRES: asserts
; REQUIRES: haydn-registered-target
;
; RUN: llc -mtriple=haydn-unknown-elf -O2 -debug-only=haydn-verify-bundles \
; RUN:   -o /dev/null %s 2>&1 | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O0 -debug-only=haydn-verify-bundles \
; RUN:   -o /dev/null %s 2>&1 | FileCheck %s

; CHECK-COUNT-2: HaydnVerifyBundles seat=invariant on seat_pin
; CHECK: HaydnVerifyBundles seat=freeze on seat_pin
; CHECK-NOT: seat=invariant

define void @seat_pin() {
  ret void
}
