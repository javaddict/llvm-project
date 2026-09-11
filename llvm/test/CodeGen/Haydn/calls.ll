; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 < %s | FileCheck %s

; Role: semantic — Simple call.

; Simple call

declare i32 @extern_func(i32)
define i32 @test_call(i32 %a) {
  %r = call i32 @extern_func(i32 %a)
  ret i32 %r
}
; CHECK-LABEL: test_call:
; CHECK: lui{{.*}}extern_func
; CHECK: addi32{{.*}}extern_func
; CHECK: jalr{{.*}}lr

; Multiple args
declare i32 @multi_arg(i32, i32, i32)
define i32 @test_multiarg(i32 %a, i32 %b, i32 %c) {
  %r = call i32 @multi_arg(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}
; CHECK-LABEL: test_multiarg:
; CHECK: lui{{.*}}multi_arg
; CHECK: addi32{{.*}}multi_arg
; CHECK: jalr{{.*}}lr

; Local call
define i32 @callee(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}
define i32 @test_local_call(i32 %a) {
  %r = call i32 @callee(i32 %a)
  ret i32 %r
}
; CHECK-LABEL: test_local_call:
; CHECK: lui{{.*}}callee
; CHECK: addi32{{.*}}callee
; CHECK: jalr{{.*}}lr
