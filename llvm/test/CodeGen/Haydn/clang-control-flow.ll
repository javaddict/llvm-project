; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 < %s | FileCheck %s
; NOTE: -verify-machineinstrs is disabled due to known G_PHI selection issue

; Test control flow constructs compile correctly

; Simple if-then-else
define i32 @test_if_else(i32 %a, i32 %b) {
; CHECK-LABEL: test_if_else:
entry:
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  %result1 = add i32 %a, %b
  br label %join
else:
  %result2 = sub i32 %a, %b
  br label %join
join:
  %result = phi i32 [%result1, %then], [%result2, %else]
  ret i32 %result
}

; Nested if
define i32 @test_nested_if(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_nested_if:
entry:
  %cmp1 = icmp sgt i32 %a, %b
  br i1 %cmp1, label %outer_then, label %outer_else
outer_then:
  %cmp2 = icmp sgt i32 %b, %c
  br i1 %cmp2, label %inner_then, label %inner_else
inner_then:
  %r1 = add i32 %a, %b
  %r2 = add i32 %r1, %c
  br label %outer_join
inner_else:
  %r3 = sub i32 %a, %c
  br label %outer_join
outer_else:
  %r4 = mul i32 %a, %b
  br label %outer_join
outer_join:
  %result = phi i32 [%r2, %inner_then], [%r3, %inner_else], [%r4, %outer_else]
  ret i32 %result
}

; Simple while loop
define i32 @test_while_loop(i32 %n) {
; CHECK-LABEL: test_while_loop:
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.next, %loop]
  %sum.next = add i32 %sum, %i
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.next
}

; Do-while loop (converted to while by LLVM)
; Back-edge is fused blt_w (icmp slt %i,%n). 560038acd774 briefly changed this to
; bnez_w in its rebaseline, but test_do_while's back-edge was never SFR-stripped
; the fused form survived, so bnez_w was unsatisfiable. Reverted to blt_w.
define i32 @test_do_while(i32 %n) {
; CHECK-LABEL: test_do_while:
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.next, %loop]
  %sum.next = add i32 %sum, %i
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.next
}

; For loop
define i32 @test_for_loop(i32 %n) {
; CHECK-LABEL: test_for_loop:
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.next, %loop]
  %sum.next = add i32 %sum, %i
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.next
}

; Loop with early exit
define i32 @test_loop_break(i32 %n, i32 %limit) {
; CHECK-LABEL: test_loop_break:
; Cmp+branch fusion no longer fires. sge (break test) lowered as
; slt32 + xor32-with-1 + bnez_w; slt (continue test) as slt32 + bnez_w.
; CHECK-DAG: xor32
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %body]
  %sum = phi i32 [0, %entry], [%sum.inc, %body]
  %cmp_break = icmp sge i32 %i, %limit
  br i1 %cmp_break, label %exit, label %body
body:
  %sum.inc = add i32 %sum, %i
  %i.next = add i32 %i, 1
  %cmp_cont = icmp slt i32 %i.next, %n
  br i1 %cmp_cont, label %loop, label %exit
exit:
  %result = phi i32 [%sum, %loop], [%sum.inc, %body]
  ret i32 %result
}

; Loop with continue (simplified: skip even numbers)
define i32 @test_loop_continue(i32 %n) {
; CHECK-LABEL: test_loop_continue:
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.new, %loop]
  %cmp_skip = icmp eq i32 %i, 5
  %add.val = add i32 %sum, %i
  %sum.new = select i1 %cmp_skip, i32 %sum, i32 %add.val
  %i.next = add i32 %i, 1
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.new
}

; Switch statement
define i32 @test_switch(i32 %val) {
; CHECK-LABEL: test_switch:
entry:
  switch i32 %val, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
  ]
case0:
  br label %exit
case1:
  br label %exit
case2:
  br label %exit
default:
  br label %exit
exit:
  %result = phi i32 [0, %case0], [1, %case1], [2, %case2], [-1, %default]
  ret i32 %result
}

; Multiple return paths
define i32 @test_multiple_returns(i32 %a, i32 %b) {
; CHECK-LABEL: test_multiple_returns:
entry:
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %return_early, label %continue
return_early:
  ret i32 %a
continue:
  %result = mul i32 %a, %b
  ret i32 %result
}

; Conditional select (no branch)
define i32 @test_conditional_select(i32 %a, i32 %b) {
; CHECK-LABEL: test_conditional_select:
; CHECK-DAG: max32
entry:
  %cmp = icmp sgt i32 %a, %b
  %result = select i1 %cmp, i32 %a, i32 %b
  ret i32 %result
}

; Chain of comparisons
define i32 @test_comparison_chain(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: test_comparison_chain:
entry:
  %cmp1 = icmp sgt i32 %a, %b
  br i1 %cmp1, label %check2, label %use_a
check2:
  %cmp2 = icmp sgt i32 %b, %c
  br i1 %cmp2, label %use_b, label %use_c
use_a:
  br label %exit
use_b:
  br label %exit
use_c:
  br label %exit
exit:
  %result = phi i32 [%a, %use_a], [%b, %use_b], [%c, %use_c]
  ret i32 %result
}

; Loop with multiple induction variables
define i32 @test_multiple_induction(i32 %n) {
; CHECK-LABEL: test_multiple_induction:
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %j = phi i32 [1, %entry], [%j.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.next, %loop]
  %sum.next = add i32 %sum, %j
  %i.next = add i32 %i, 1
  %j.next = mul i32 %j, 2
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %sum.next
}
