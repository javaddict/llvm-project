; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -verify-machineinstrs < %s | FileCheck %s

; CHECK: 	.globl	test_add                        // -- Begin function test_add
; CHECK: 	.type	test_add,@function
; CHECK-LABEL: test_add:                               // @test_add
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ 	xor32	r0, r0, r0 }
; CHECK: 	{ 	add32	r1, r1, r2 }
; CHECK: 	{ 	jalr_w{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	test_add, .Lfunc_end0-test_add
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function

;
; REGRESSION TEST: Smoke test at -O0 (no optimization)
;
; Verifies that the Haydn backend produces valid assembly for all basic
; operation categories at -O0 (no optimizations). This is the most
; conservative codegen path — every operation is legalized and emitted
; without any combiner or optimization passes.
;
; If this test fails, the most likely cause is a legalizer gap for the
; failing operation type, or a missing instruction selector entry for
; a newly-added G_* opcode. Check the legalizer info and selector table
; for the specific opcode mentioned in the error.

;Simple arithmetic: add, sub, mul, and, or, xor

define i32 @test_add(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @test_sub(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

define i32 @test_mul(i32 %a, i32 %b) {
  %r = mul i32 %a, %b
  ret i32 %r
}

define i32 @test_and(i32 %a, i32 %b) {
  %r = and i32 %a, %b
  ret i32 %r
}

define i32 @test_or(i32 %a, i32 %b) {
  %r = or i32 %a, %b
  ret i32 %r
}

define i32 @test_xor(i32 %a, i32 %b) {
  %r = xor i32 %a, %b
  ret i32 %r
}

;Control flow: if/else

define i32 @test_if_else(i32 %a, i32 %b) {
entry:
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %then, label %else

then:
  %r1 = add i32 %a, %b
  br label %merge

else:
  %r2 = sub i32 %a, %b
  br label %merge

merge:
  %phi = phi i32 [%r1, %then], [%r2, %else]
  ret i32 %phi
}

;Control flow: simple loop

define i32 @test_loop(i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.next, %loop]
  %i.next = add i32 %i, 1
  %sum.next = add i32 %sum, %i
  %cmp = icmp slt i32 %i.next, %n
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}

;Function calls with various argument counts

declare i32 @ext_1arg(i32)
declare i32 @ext_3args(i32, i32, i32)
declare i32 @ext_7args(i32, i32, i32, i32, i32, i32, i32)
declare i32 @ext_9args(i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @test_call_1arg(i32 %a) {
  %r = call i32 @ext_1arg(i32 %a)
  ret i32 %r
}

define i32 @test_call_3args(i32 %a, i32 %b, i32 %c) {
  %r = call i32 @ext_3args(i32 %a, i32 %b, i32 %c)
  ret i32 %r
}

define i32 @test_call_7args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g) {
  %r = call i32 @ext_7args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g)
  ret i32 %r
}

; 9 args exceeds R1-R7 register arg count, forces stack passing
define i32 @test_call_9args(i32 %a) {
  %r = call i32 @ext_9args(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 %a)
  ret i32 %r
}

;Stack variables (local allocas)

define i32 @test_stack_var(i32 %a) {
  %p = alloca i32
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @test_stack_array(i32 %a) {
  %arr = alloca [8 x i32], align 8
  %p0 = getelementptr [8 x i32], ptr %arr, i32 0, i32 0
  store i32 %a, ptr %p0
  %v = load i32, ptr %p0
  ret i32 %v
}

;Mixed operations: shifts and comparisons

define i32 @test_shl(i32 %a) {
  %r = shl i32 %a, 4
  ret i32 %r
}

define i32 @test_lshr(i32 %a) {
  %r = lshr i32 %a, 4
  ret i32 %r
}

define i32 @test_ashr(i32 %a) {
  %r = ashr i32 %a, 4
  ret i32 %r
}

define i1 @test_icmp_eq(i32 %a, i32 %b) {
  %r = icmp eq i32 %a, %b
  ret i1 %r
}

define i1 @test_icmp_slt(i32 %a, i32 %b) {
  %r = icmp slt i32 %a, %b
  ret i1 %r
}

define i1 @test_icmp_ult(i32 %a, i32 %b) {
  %r = icmp ult i32 %a, %b
  ret i1 %r
}

;Constant materialization

define i32 @test_const_small() {
  ret i32 42
}

define i32 @test_const_zero() {
  ret i32 0
}

define i32 @test_const_neg1() {
  ret i32 -1
}

define i32 @test_const_large() {
  ret i32 65536
}
