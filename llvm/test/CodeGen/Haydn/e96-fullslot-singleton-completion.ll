; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -stop-after=haydn-verify-bundles \
; RUN:   < %s | FileCheck %s
; REQUIRES: haydn-registered-target
;
; Role: late singleton commit is full-slot architectural NOP pad
; (AllEntriesReal / BUNDLE 0, 0), not unqualified StubE2Singleton (BUNDLE 0, 2).
; After Finalize FieldSlot→MemberId, the real child is ADD32_E*_E*_*, not _S*.

; CHECK-LABEL: name:{{ +}}fullslot_singleton
; CHECK: BUNDLE 0, 0
; CHECK: ADD32_E{{[23]}}_E{{[0-2]}}_
; CHECK-NOT: BUNDLE 0, 2
define i32 @fullslot_singleton(i32 %a, i32 %b) nounwind {
  %r = add i32 %a, %b
  ret i32 %r
}
