; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — tail call handling.

; Test tail call handling.
; The Haydn backend currently does not optimize tail calls into jumps
; they are emitted as regular calls followed by return. This test verifies
; the behavior is correct (function produces the right result) even without
; tail-call optimization.
;
; If tail-call optimization is implemented in the future, the CHECK lines
; should be updated to verify the jump pattern instead of call+ret.

declare i32 @helper_i32(i32)
declare i64 @helper_i64(i64)

;Simple tail call
define i32 @tail_call_simple(i32 %a) nounwind {
; CHECK-LABEL: tail_call_simple:
; CHECK: jal{{(\.s[012])?}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = tail call i32 @helper_i32(i32 %a)
  ret i32 %r
}

;Tail call with different argument
define i32 @tail_call_modified(i32 %a) nounwind {
; CHECK-LABEL: tail_call_modified:
; CHECK: jal{{(\.s[012])?}}
  %b = add i32 %a, 1
  %r = tail call i32 @helper_i32(i32 %b)
  ret i32 %r
}

;Tail call with i64
define i64 @tail_call_i64(i64 %a) nounwind {
; CHECK-LABEL: tail_call_i64:
; CHECK: jal{{(\.s[012])?}}
  %r = tail call i64 @helper_i64(i64 %a)
  ret i64 %r
}

;Non-tail call for comparison (regular call + ret)
define i32 @regular_call(i32 %a) nounwind {
; CHECK-LABEL: regular_call:
; CHECK: jal{{(\.s[012])?}}
; CHECK: jalr{{(\.s[012])?}} r0, lr, 0
  %r = call i32 @helper_i32(i32 %a)
  ret i32 %r
}

;Tail call to function that uses the result
define i32 @tail_call_used(i32 %a) nounwind {
; CHECK-LABEL: tail_call_used:
; CHECK: jal{{(\.s[012])?}}
  %r = tail call i32 @helper_i32(i32 %a)
  ret i32 %r
}

;Mutual tail recursion pattern
define i32 @mutual_a(i32 %x) nounwind {
; CHECK-LABEL: mutual_a:
; CHECK: jal{{(\.s[012])?}}
entry:
  %cmp = icmp eq i32 %x, 0
  br i1 %cmp, label %done, label %recurse
done:
  ret i32 0
recurse:
  %x1 = sub i32 %x, 1
  %r = tail call i32 @helper_i32(i32 %x1)
  ret i32 %r
}
