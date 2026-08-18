; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -haydn-enable-hwloops -mattr=+hwloop -O2 < %s | FileCheck %s

; Role: semantic — layout-owned setup places Following >= InterveningCycles
; (2) size-bearing parcels after SET_HWLOOP (SetupIssueDistance = 3).
;
; Preheader useful work (e.g. address setup) must not reverse-order through
; Fixup shorten. A simple countdown loop must still form a hardware loop.
; Useful work may sit in the setup window and still count toward Following.

define dso_local void @count_store(ptr nocapture writeonly %p, i32 %n) {
; CHECK-LABEL: count_store:
; CHECK: set_hwloop
; Setup window: size-bearing parcels (nop fill and/or useful work) between
; SET and body start; product Following floor still enforced by Fixup.
; CHECK: { nop; nop
; CHECK: LLhwloop_start{{[0-9]*}}
; Inclusive END remains product law.
; CHECK: LLhwloop_end{{[0-9]*}}
; Soft back-edge must not remain once the hardware loop forms.
; CHECK-NOT: beqz
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %for.body.preheader, label %for.end

for.body.preheader:
  br label %for.body

for.body:
  %i = phi i32 [ %inc, %for.body ], [ 0, %for.body.preheader ]
  %q = getelementptr inbounds i32, ptr %p, i32 %i
  store i32 %i, ptr %q, align 4
  %inc = add nuw nsw i32 %i, 1
  %exitcond = icmp eq i32 %inc, %n
  br i1 %exitcond, label %for.end, label %for.body

for.end:
  ret void
}
