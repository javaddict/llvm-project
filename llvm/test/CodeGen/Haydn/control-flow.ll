; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Simple if.

; Simple if

define i32 @test_if(i32 %a, i32 %b) {
  %cmp = icmp sgt i32 %a, 0
  br i1 %cmp, label %then, label %else
then:
  %r1 = add i32 %a, %b
  br label %join
else:
  %r2 = sub i32 %a, %b
  br label %join
join:
  %r = phi i32 [%r1, %then], [%r2, %else]
  ret i32 %r
}
; CHECK-LABEL: test_if:
; EarlyIfConversion may select if/else as slt+movt instead of beqz.
; CHECK: slt32
; CHECK: {{beqz|movt32}}

; Loop
define i32 @test_loop(i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %next = add i32 %i, 1
  %cmp = icmp slt i32 %next, %n
  br i1 %cmp, label %loop, label %exit
exit:
  ret i32 %next
}
; CHECK-LABEL: test_loop:
; mattr=-hwloop keeps a software back-edge (blt_w or slt+branch; foldCmpBranch off).
; CHECK: {{blt|slt32}}
