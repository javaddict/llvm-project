; REQUIRES: asserts
; REQUIRES: haydn-registered-target
; RUN: %python -c "import os,sys; p=sys.argv[1]; assert not os.path.exists(p), p+' must be deleted'" %S/../../../lib/Target/Haydn/HaydnLateConvergence.cpp
; RUN: %python -c "import os,sys; p=sys.argv[1]; assert not os.path.exists(p), p+' must be deleted'" %S/../../../lib/Target/Haydn/HaydnLateConvergence.h
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/Haydn.h --check-prefix=NOLC
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/CMakeLists.txt --check-prefix=NOCM
; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 \
; RUN:     -O2 -verify-machineinstrs -o /dev/null < %s
;
; GR1.7 L: HaydnLateConvergence.{h,cpp} are deleted. S2, inner
; PostMachineScheduler, and generic BranchRelaxation LAST are greppably
; absent. LBN keys are not moved.

; NOLC: createHaydnLongBranchNormalizePass
; NOLC-NOT: createHaydnLateConvergencePass
; NOLC-NOT: haydnSMS2Enabled
; NOLC-NOT: initializeHaydnLateConvergencePassPass
; NOCM: HaydnLongBranchNormalize.cpp
; NOCM-NOT: HaydnLateConvergence.cpp

define i32 @no_br_last(ptr nocapture readonly %a, i32 %n) {
entry:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %l1, label %exit
l1:
  %x = load i32, ptr %a, align 4
  %v1 = add i32 %x, 1
  %d1 = icmp sgt i32 %v1, 10
  br i1 %d1, label %l2, label %exit
l2:
  %v2 = mul i32 %v1, 3
  br label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %v1, %l1 ], [ %v2, %l2 ]
  ret i32 %r
}
