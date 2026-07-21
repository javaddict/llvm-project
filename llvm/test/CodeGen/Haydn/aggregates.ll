; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; Test aggregate type handling: arrays, structs, vectors.

;Return array (decomposes to multiple values)
define [2 x i32] @return_array() {
; CHECK-LABEL: return_array:
  %result = insertvalue [2 x i32] undef, i32 42, 0
  %result2 = insertvalue [2 x i32] %result, i32 99, 1
  ret [2 x i32] %result2
}

;Pass array as argument
define i32 @sum_array_arg([2 x i32] %arr) {
; CHECK-LABEL: sum_array_arg:
  %e0 = extractvalue [2 x i32] %arr, 0
  %e1 = extractvalue [2 x i32] %arr, 1
  %sum = add i32 %e0, %e1
  ret i32 %sum
}

;Return struct {i32, i32, i32}
define { i32, i32, i32 } @return_3field() {
; CHECK-LABEL: return_3field:
  %r = insertvalue { i32, i32, i32 } undef, i32 1, 0
  %r2 = insertvalue { i32, i32, i32 } %r, i32 2, 1
  %r3 = insertvalue { i32, i32, i32 } %r2, i32 3, 2
  ret { i32, i32, i32 } %r3
}

;Extract struct field by field
define i32 @get_third_field({ i32, i32, i32 } %s) {
; CHECK-LABEL: get_third_field:
  %f = extractvalue { i32, i32, i32 } %s, 2
  ret i32 %f
}

;Nested struct access
define i32 @nested_struct({ i32, { i32, i32 } } %s) {
; CHECK-LABEL: nested_struct:
  %inner = extractvalue { i32, { i32, i32 } } %s, 1
  %field = extractvalue { i32, i32 } %inner, 0
  ret i32 %field
}

;Struct with i64 field
define i64 @return_struct_i64() {
; CHECK-LABEL: return_struct_i64:
  %r = insertvalue { i64, i32 } undef, i64 123456789, 0
  %r2 = insertvalue { i64, i32 } %r, i32 42, 1
  %v = extractvalue { i64, i32 } %r2, 0
  ret i64 %v
}

;Large struct passed byval
define i32 @large_struct_byval(ptr byval([6 x i32]) %s) {
; CHECK-LABEL: large_struct_byval:
  %e0 = getelementptr [6 x i32], ptr %s, i32 0, i32 0
  %v0 = load i32, ptr %e0
  ret i32 %v0
}

;Array literal as parameter
define i32 @array_literal() {
; CHECK-LABEL: array_literal:
  %arr = insertvalue [2 x i32] undef, i32 10, 0
  %arr2 = insertvalue [2 x i32] %arr, i32 20, 1
  %sum = call i32 @sum_array_arg([2 x i32] %arr2)
  ret i32 %sum
}

;Struct with mixed types
define { i8, i16, i32, i64 } @mixed_struct() {
; CHECK-LABEL: mixed_struct:
  %r = insertvalue { i8, i16, i32, i64 } undef, i8 7, 0
  %r2 = insertvalue { i8, i16, i32, i64 } %r, i16 42, 1
  %r3 = insertvalue { i8, i16, i32, i64 } %r2, i32 99, 2
  %r4 = insertvalue { i8, i16, i32, i64 } %r3, i64 1000, 3
  ret { i8, i16, i32, i64 } %r4
}

;Empty struct
define {} @empty_struct() {
; CHECK-LABEL: empty_struct:
  ret {} undef
}

;Array of i64
define [2 x i64] @array_i64() {
; CHECK-LABEL: array_i64:
  %r = insertvalue [2 x i64] undef, i64 111, 0
  %r2 = insertvalue [2 x i64] %r, i64 222, 1
  ret [2 x i64] %r2
}

;Memcpy-style aggregate copy
define void @aggregate_copy(ptr %dst, ptr %src) {
; CHECK-LABEL: aggregate_copy:
  %v = load [4 x i32], ptr %src
  store [4 x i32] %v, ptr %dst
  ret void
}

;Global struct constant
@global_struct = constant { i32, i32 } { i32 1, i32 2 }

define i32 @read_global_struct() {
; CHECK-LABEL: read_global_struct:
  %v = load { i32, i32 }, ptr @global_struct
  %f = extractvalue { i32, i32 } %v, 0
  ret i32 %f
}

;Array index out of bounds (legal in IR, undefined at runtime)
define i32 @out_of_bounds(ptr %arr) {
; CHECK-LABEL: out_of_bounds:
; No bounds checking in LLVM IR
  %ptr = getelementptr i32, ptr %arr, i32 9999
  %v = load i32, ptr %ptr
  ret i32 %v
}
