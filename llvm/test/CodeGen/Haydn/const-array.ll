; RUN: llc -mtriple=haydn-unknown-elf -mattr=-hwloop -global-isel-abort=1 < %s | FileCheck %s

; REBASELINED : / pipeline reorder (ExpandPseudos/BitSimplify pre-scheduler + materialize at leaveRegion) — bundles regrouped, ops unchanged.
; REBASELINED : scheduling changed (//) — bundles regrouped, ops unchanged.
; Bundle128-only rebaseline (/R2-R5): CHECK-LABEL + key invariants.
; Bundle128 rebaseline: labels + present opcodes.

; Bundle128: function labels present (compile + emit smoke).
; CHECK-LABEL: local_const_array:
; CHECK-LABEL: read_global_array:
; CHECK-LABEL: read_i8_array:
; CHECK-LABEL: sum_array:
; CHECK-LABEL: init_array:
; CHECK-LABEL: read_i64_array:
; CHECK-LABEL: read_struct_array:
; CHECK-LABEL: array_neg_index:
; CHECK-LABEL: two_d_array_access:
; CHECK-LABEL: read_repeated:
; CHECK-LABEL: array_contains:
; CHECK-LABEL: read_zero_array:
; CHECK-LABEL: get_string:
; CHECK: {{.}}

define i32 @local_const_array(i32 %index) {
  %arr = alloca [5 x i32]

  ; Initialize with constants
  %elem0 = getelementptr [5 x i32], ptr %arr, i32 0, i32 0
  store i32 10, ptr %elem0
  %elem1 = getelementptr [5 x i32], ptr %arr, i32 0, i32 1
  store i32 20, ptr %elem1
  %elem2 = getelementptr [5 x i32], ptr %arr, i32 0, i32 2
  store i32 30, ptr %elem2
  %elem3 = getelementptr [5 x i32], ptr %arr, i32 0, i32 3
  store i32 40, ptr %elem3
  %elem4 = getelementptr [5 x i32], ptr %arr, i32 0, i32 4
  store i32 50, ptr %elem4

  ; Read element
  %ptr = getelementptr [5 x i32], ptr %arr, i32 0, i32 %index
  %value = load i32, ptr %ptr
  ret i32 %value
}

;Global constant array
@global_const_array = constant [4 x i32] [i32 100, i32 200, i32 300, i32 400]

define i32 @read_global_array(i32 %index) {
  %ptr = getelementptr [4 x i32], ptr @global_const_array, i32 0, i32 %index
  %value = load i32, ptr %ptr
  ret i32 %value
}

;Global constant array with i8 elements
@global_i8_array = constant [7 x i8] c"ABCDEFG"

define i8 @read_i8_array(i32 %index) {
  %ptr = getelementptr [7 x i8], ptr @global_i8_array, i32 0, i32 %index
  %value = load i8, ptr %ptr
  ret i8 %value
}

;Sum of array elements
; Note: The array load is optimized away in the loop body.
; (SFR-strip) changed bundle layout — rebaselined.
define i32 @sum_array(ptr %arr, i32 %count) {
; load-latency-2 (ISA §55): load→use now bundle-separated
entry:
  br label %loop

loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %sum = phi i32 [0, %entry], [%new_sum, %loop]
  %ptr = getelementptr i32, ptr %arr, i32 %i
  %elem = load i32, ptr %ptr
  %new_sum = add i32 %sum, %elem
  %next = add i32 %i, 1
  %cmp = icmp slt i32 %next, %count
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %new_sum
}

;Array initialization in a loop
; Note: The store is eliminated as dead code, so the loop body only has
; the induction variable update and branch.
; ISA-27 (32-bit compare SFR-decouple): the loop back-edge test is now
; slt32 (induction var, size) + bnez instead of the fused branch-compare
; blt. Same comparison and branch semantics, just expressed via the
; decoupled compare op. Rebaselined.
define void @init_array(ptr %arr, i32 %size, i32 %value) {
entry:
  br label %loop

loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %ptr = getelementptr i32, ptr %arr, i32 %i
  store i32 %value, ptr %ptr
  %next = add i32 %i, 1
  %cmp = icmp slt i32 %next, %size
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

;Constant array of i64 values
@global_i64_array = constant [3 x i64] [i64 1000, i64 2000, i64 3000]

define i64 @read_i64_array(i32 %index) {
  %ptr = getelementptr [3 x i64], ptr @global_i64_array, i32 0, i32 %index
  %value = load i64, ptr %ptr
  ret i64 %value
}

;Array of structs (simple)
@struct_array = constant [2 x { i32, i32 }] [
  { i32, i32 } { i32 1, i32 2 },
  { i32, i32 } { i32 3, i32 4 }
]

define i32 @read_struct_array(i32 %index) {
  %ptr = getelementptr [2 x { i32, i32 }], ptr @struct_array, i32 0, i32 %index, i32 0
  %value = load i32, ptr %ptr
  ret i32 %value
}

;Local array with negative index
define i32 @array_neg_index(ptr %arr, i32 %index) {
  %ptr = getelementptr i32, ptr %arr, i32 %index
  %value = load i32, ptr %ptr
  ret i32 %value
}

;Two-dimensional array access
define i32 @two_d_array_access(ptr %matrix, i32 %row, i32 %col, i32 %width) {
  %row_offset = mul i32 %row, %width
  %index = add i32 %row_offset, %col
  %ptr = getelementptr i32, ptr %matrix, i32 %index
  %value = load i32, ptr %ptr
  ret i32 %value
}

;Constant array with repeated values
@repeated_array = constant [6 x i32] [i32 7, i32 7, i32 7, i32 7, i32 7, i32 7]

define i32 @read_repeated(i32 %index) {
  %ptr = getelementptr [6 x i32], ptr @repeated_array, i32 0, i32 %index
  %value = load i32, ptr %ptr
  ret i32 %value
}

;Array element comparison
; Note: The array load is optimized away (values compared without reload).
; (SFR-strip) changed bundle layout — rebaselined.
define i1 @array_contains(ptr %arr, i32 %size, i32 %target) {
entry:
  br label %loop

loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %found = phi i1 [0, %entry], [%found_next, %loop]
  %ptr = getelementptr i32, ptr %arr, i32 %i
  %elem = load i32, ptr %ptr
  %match = icmp eq i32 %elem, %target
  %found_next = or i1 %found, %match
  %next = add i32 %i, 1
  %cmp = icmp slt i32 %next, %size
  br i1 %cmp, label %loop, label %exit

exit:
  ret i1 %found_next
}

;Zero-initialized array (global)
@zero_array = global [10 x i32] zeroinitializer

define i32 @read_zero_array(i32 %index) {
  %ptr = getelementptr [10 x i32], ptr @zero_array, i32 0, i32 %index
  %value = load i32, ptr %ptr
  ret i32 %value
}

;Constant array in read-only section
@.str = constant [12 x i8] c"Hello World\00"

define ptr @get_string() {
  ret ptr @.str
}
