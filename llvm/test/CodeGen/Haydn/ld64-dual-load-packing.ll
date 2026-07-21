; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; Two independent i64 loads: expect 64-bit load forms (ld64 / d_ldw_*),
; not two slot-1-only conflicts that force separate issue.

define void @dual_load_i64(ptr %a, ptr %b) {
; CHECK-LABEL: dual_load_i64:
; CHECK: {{ld64|d_ldw}}
; CHECK: {{ld64|d_ldw|ld32|add64}}
; CHECK: {{st64|d_sw|st32}}
entry:
  %x = load i64, ptr %a, align 8
  %ap = getelementptr i64, ptr %a, i32 1
  %y = load i64, ptr %ap, align 8
  %s = add i64 %x, %y
  store i64 %s, ptr %b, align 8
  ret void
}
