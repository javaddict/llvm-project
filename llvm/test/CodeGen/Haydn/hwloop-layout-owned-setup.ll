; RUN: llc -mtriple=haydn-unknown-elf -mattr=+hwloop -O2 < %s | FileCheck %s
;
; P2 reform: default layout-owned setup places only t−3 pads after SET_HWLOOP.
; Preheader useful work (e.g. address setup) must not reverse-order through
; Fixup shorten. A simple countdown loop must still form a hardware loop.

define dso_local void @count_store(ptr nocapture writeonly %p, i32 %n) {
; CHECK-LABEL: count_store:
; CHECK: set_hwloop
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
