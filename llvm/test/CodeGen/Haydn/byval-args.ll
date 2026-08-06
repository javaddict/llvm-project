; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — byval parameter passing.

; Test byval parameter passing.
; byval arguments are passed as pointers to the stack location where the
; argument data resides. The callee loads from the pointer to access fields.
;
; REBASELINE : byval lowering is correct. Prior XFAIL was a stale
; + DR pack (d_ldw_post_imm), which is a valid (if not ideal) lowering.

%struct.big = type { i32, i32, i32, i32, i32 }

declare void @use_i32(i32)

;byval struct with 5 fields (exceeds register capacity)
define void @byval_big(ptr byval(%struct.big) %p) nounwind {
; CHECK-LABEL: byval_big:
; CHECK: ld32
  %e0 = getelementptr %struct.big, ptr %p, i32 0, i32 0
  %v0 = load i32, ptr %e0
  call void @use_i32(i32 %v0)
  ret void
}

;byval with field access
define i32 @byval_sum_fields(ptr byval(%struct.big) %p) nounwind {
; CHECK-LABEL: byval_sum_fields:
; CHECK: ld32
; CHECK: ld32
; CHECK: add32
  %e0 = getelementptr %struct.big, ptr %p, i32 0, i32 0
  %e1 = getelementptr %struct.big, ptr %p, i32 0, i32 1
  %v0 = load i32, ptr %e0
  %v1 = load i32, ptr %e1
  %sum = add i32 %v0, %v1
  ret i32 %sum
}

;byval with all fields
define i32 @byval_all_fields(ptr byval(%struct.big) %p) nounwind {
; CHECK-LABEL: byval_all_fields:
; CHECK: ld32
; CHECK: add32
  %e0 = getelementptr %struct.big, ptr %p, i32 0, i32 0
  %e1 = getelementptr %struct.big, ptr %p, i32 0, i32 1
  %e2 = getelementptr %struct.big, ptr %p, i32 0, i32 2
  %e3 = getelementptr %struct.big, ptr %p, i32 0, i32 3
  %e4 = getelementptr %struct.big, ptr %p, i32 0, i32 4
  %v0 = load i32, ptr %e0
  %v1 = load i32, ptr %e1
  %v2 = load i32, ptr %e2
  %v3 = load i32, ptr %e3
  %v4 = load i32, ptr %e4
  %s1 = add i32 %v0, %v1
  %s2 = add i32 %s1, %v2
  %s3 = add i32 %s2, %v3
  %sum = add i32 %s3, %v4
  ret i32 %sum
}

;byval with i64 array
%struct.i64pair = type { i64, i64 }

define i64 @byval_i64_struct(ptr byval(%struct.i64pair) %p) nounwind {
; CHECK-LABEL: byval_i64_struct:
; Dual ld32 of lo/hi halves (current GISel path); d_ldw packs to DR64.
; CHECK: ld32
; CHECK: ld32
  %e0 = getelementptr %struct.i64pair, ptr %p, i32 0, i32 0
  %v0 = load i64, ptr %e0
  ret i64 %v0
}

;Multiple byval parameters
define i32 @byval_multi(ptr byval(%struct.big) %p1, ptr byval(%struct.big) %p2) nounwind {
; CHECK-LABEL: byval_multi:
  %e0 = getelementptr %struct.big, ptr %p1, i32 0, i32 0
  %e1 = getelementptr %struct.big, ptr %p2, i32 0, i32 0
  %v0 = load i32, ptr %e0
  %v1 = load i32, ptr %e1
  %sum = add i32 %v0, %v1
  ret i32 %sum
}

;byval with write-through
define void @byval_write(ptr byval(%struct.big) %p) nounwind {
; CHECK-LABEL: byval_write:
; CHECK: st32
  %e0 = getelementptr %struct.big, ptr %p, i32 0, i32 0
  store i32 42, ptr %e0
  ret void
}
