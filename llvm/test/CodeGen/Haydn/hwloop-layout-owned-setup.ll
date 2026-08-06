; RUN: llc -mtriple=haydn-unknown-elf -haydn-enable-hwloops -mattr=+hwloop -O2 < %s | FileCheck %s

; Role: semantic — default layout-owned setup places Following >= InterveningCycles (=3) size-bearing Full parcels after SET_HWLOOP (SetupIssueDistance=4).

; Default layout-owned setup places Following >= InterveningCycles (=3)
; size-bearing Full parcels after SET_HWLOOP (SetupIssueDistance=4).
; Preheader useful work (e.g. address setup) must not reverse-order through
; Fixup shorten. A simple countdown loop must still form a hardware loop.
; Following floor is three size-bearing parcels; useful work may sit in the
; setup window and still count toward Following.

define dso_local void @count_store(ptr nocapture writeonly %p, i32 %n) {
; CHECK-LABEL: count_store:
; CHECK: set_hwloop{{(_f2)?}}{{(_w)?}}
; Following >= InterveningCycles (=3): at least three size-bearing Full
; parcels after SET before the body start label. Useful preheader work may
; occupy one of those parcels; deficit pads fill the remainder.
; CHECK-NEXT: {
; CHECK-NEXT: {
; CHECK-NEXT: {
; CHECK: LLhwloop_start
; Inclusive END remains product law.
; CHECK: LLhwloop_end
; Soft back-edge must not remain once the hardware loop forms.
; CHECK-NOT: beqz_w
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
