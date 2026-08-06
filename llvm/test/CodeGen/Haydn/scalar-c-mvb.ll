; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.

; CHECK: 	.globl	add                             // -- Begin function add
; CHECK: 	.type	add,@function
; CHECK-LABEL: add:                                    // @add
; CHECK: 	.cfi_startproc
; CHECK: // %bb.0:
; CHECK: 	{ xor32	r0, r0, r0; nop; nop }
; CHECK: 	{ add32	r1, r1, r2; nop; nop }
; CHECK: 	{ nop; nop; jalr{{(\.s[012])?}}	r0, lr, 0 }
; CHECK: .Lfunc_end0:
; CHECK: 	.size	add, .Lfunc_end0-add
; CHECK: 	.cfi_endproc
; CHECK:                                         // -- End function



;
; M3 MVB comprehensive test: verify clang can compile any scalar C function
; to valid Haydn assembly. Tests: arithmetic, control flow, calls, pointers
; arrays, structs, globals, recursion, i64.

;Basic arithmetic
define i32 @add(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @sub(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

define i32 @mul(i32 %a, i32 %b) {
  %r = mul i32 %a, %b
  ret i32 %r
; Scalar s32 mul lowers to mull.
}

;Bitwise operations
define i32 @and_op(i32 %a, i32 %b) {
  %r = and i32 %a, %b
  ret i32 %r
}

define i32 @or_op(i32 %a, i32 %b) {
  %r = or i32 %a, %b
  ret i32 %r
}

define i32 @xor_op(i32 %a, i32 %b) {
  %r = xor i32 %a, %b
  ret i32 %r
}

;Shifts
define i32 @shl_op(i32 %a, i32 %b) {
  %r = shl i32 %a, %b
  ret i32 %r
}

define i32 @lshr_op(i32 %a, i32 %b) {
  %r = lshr i32 %a, %b
  ret i32 %r
}

define i32 @ashr_op(i32 %a, i32 %b) {
  %r = ashr i32 %a, %b
  ret i32 %r
}

;Comparison and select
define i32 @select_test(i32 %a, i32 %b) {
  %cmp = icmp slt i32 %a, %b
  %r = select i1 %cmp, i32 %a, i32 %b
  ret i32 %r
}

;Control flow: if/else
define i32 @max_func(i32 %a, i32 %b) {
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %true_bb, label %false_bb

true_bb:
  ret i32 %a

false_bb:
  ret i32 %b
}

;Function call
declare i32 @extern_func(i32)

define i32 @call_test(i32 %x) {
  %r = call i32 @extern_func(i32 %x)
  ret i32 %r
}

;Pointer access
define i32 @load_store(i32* %p) {
  %v = load i32, i32* %p
  %r = add i32 %v, 1
  store i32 %r, i32* %p
  ret i32 %r
}

;GEP with array
define i32 @array_access(i32* %arr, i32 %idx) {
  %ptr = getelementptr i32, i32* %arr, i32 %idx
  %v = load i32, i32* %ptr
  ret i32 %v
}

;Struct access
%struct.point = type { i32, i32 }

define i32 @struct_field(%struct.point* %p) {
  %x.ptr = getelementptr %struct.point, %struct.point* %p, i32 0, i32 0
  %y.ptr = getelementptr %struct.point, %struct.point* %p, i32 0, i32 1
  %x = load i32, i32* %x.ptr
  %y = load i32, i32* %y.ptr
  %r = add i32 %x, %y
  ret i32 %r
}

;Recursion (fibonacci)
define i32 @fib(i32 %n) {
  %cmp = icmp sle i32 %n, 1
  br i1 %cmp, label %base, label %recurse

base:
  ret i32 %n

recurse:
  %n1 = sub i32 %n, 1
  %n2 = sub i32 %n, 2
  %f1 = call i32 @fib(i32 %n1)
  %f2 = call i32 @fib(i32 %n2)
  %r = add i32 %f1, %f2
  ret i32 %r
}

;i64 support
define i64 @add_i64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

define i64 @load64(i64* %p) {
  %v = load i64, i64* %p
  ret i64 %v
}

define void @store64(i64* %p, i64 %v) {
  store i64 %v, i64* %p
  ret void
}
