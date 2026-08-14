; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -O0 < %s | FileCheck %s

; Role: semantic — systematic: every succ-empty MBB must terminate (soft RET).

; systematic: every succ-empty MBB must terminate (soft RET).
; Whole-function `unreachable` used to emit empty.text and fall through.
; Mid-function `unreachable` was worse: PEI never visits non-return blocks
; so the empty LBB sat before.Lfunc_end and fell into the next function.
; HaydnEnsureTerminators (post-PEI) + FrameLowering belt close both.

define void @all_unreach() {
; CHECK-LABEL: all_unreach:
; CHECK: jalr{{(\.s[012])?}}{{.*}}r0{{.*}}lr{{.*}}0
; CHECK: .Lfunc_end{{[0-9]+}}:
  unreachable
}

define i32 @maybe_unreach(i1 %c) {
; CHECK-LABEL: maybe_unreach:
; CHECK: jalr{{(\.s[012])?}}{{.*}}r0{{.*}}lr{{.*}}0
; Unreachable arm must also RET — not an empty LBB before.Lfunc_end.
; CHECK: jalr{{(\.s[012])?}}{{.*}}r0{{.*}}lr{{.*}}0
; CHECK: .Lfunc_end{{[0-9]+}}:
  br i1 %c, label %a, label %b
a:
  ret i32 1
b:
  unreachable
}

define void @empty_ret() {
; CHECK-LABEL: empty_ret:
; CHECK: jalr{{(\.s[012])?}}{{.*}}r0{{.*}}lr{{.*}}0
  ret void
}
