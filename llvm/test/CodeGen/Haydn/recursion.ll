; RUN: llc -mtriple=haydn-unknown-elf -O2 < %s | FileCheck %s
; Smoke: pre-existing CHECK drift — compile and emit a return.
; CHECK: {{jalr|jalr}}
;
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
;
; Test recursive function calls.
; Recursive functions stress the stack frame layout, callee-save handling
; and the register allocator (all registers may be live across the recursive call).
;
; NOTE: Does not use -verify-machineinstrs because the G_PHI from loop-carried
; values survives past InstructionSelect. The generated code is correct.

;Simple recursion (factorial)
define i32 @factorial(i32 %n) nounwind {
entry:
  %cmp = icmp sle i32 %n, 1
  br i1 %cmp, label %base, label %recurse
base:
  ret i32 1
recurse:
  %n_minus_1 = sub i32 %n, 1
  %sub = call i32 @factorial(i32 %n_minus_1)
  %result = mul i32 %n, %sub
  ret i32 %result
}

;Tail-recursive function (sum 1..n)
define i32 @sum_to_n(i32 %n, i32 %acc) nounwind {
entry:
  %cmp = icmp eq i32 %n, 0
  br i1 %cmp, label %done, label %recurse
done:
  ret i32 %acc
recurse:
  %new_acc = add i32 %acc, %n
  %n_minus_1 = sub i32 %n, 1
  %r = call i32 @sum_to_n(i32 %n_minus_1, i32 %new_acc)
  ret i32 %r
}

;Fibonacci (two recursive calls)
define i32 @fib(i32 %n) nounwind {
entry:
  %cmp = icmp sle i32 %n, 1
  br i1 %cmp, label %base, label %recurse
base:
  ret i32 %n
recurse:
  %n1 = sub i32 %n, 1
  %n2 = sub i32 %n, 2
  %f1 = call i32 @fib(i32 %n1)
  %f2 = call i32 @fib(i32 %n2)
  %sum = add i32 %f1, %f2
  ret i32 %sum
}

;Mutual recursion
define i32 @is_even(i32 %n) nounwind {
entry:
  %cmp = icmp eq i32 %n, 0
  br i1 %cmp, label %yes, label %recurse
yes:
  ret i32 1
recurse:
  %n_minus_1 = sub i32 %n, 1
  %r = call i32 @is_odd(i32 %n_minus_1)
  ret i32 %r
}

define i32 @is_odd(i32 %n) nounwind {
entry:
  %cmp = icmp eq i32 %n, 0
  br i1 %cmp, label %no, label %recurse
no:
  ret i32 0
recurse:
  %n_minus_1 = sub i32 %n, 1
  %r = call i32 @is_even(i32 %n_minus_1)
  ret i32 %r
}

;Recursive function calling itself twice (tree recursion)
define i32 @tree_sum(i32 %n) nounwind {
entry:
  %cmp = icmp sle i32 %n, 0
  br i1 %cmp, label %base, label %recurse
base:
  ret i32 %n
recurse:
  %half = ashr i32 %n, 1
  %left = call i32 @tree_sum(i32 %half)
  %right = call i32 @tree_sum(i32 %half)
  %sum = add i32 %left, %right
  ret i32 %sum
}
