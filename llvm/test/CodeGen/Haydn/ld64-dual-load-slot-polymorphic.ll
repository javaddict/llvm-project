; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; Dual independent i64 loads must use 64-bit load forms (ld64 / d_ldw_*).

define void @dual_load_i64_slot_poly(ptr %p, ptr %q) nounwind {
; CHECK-LABEL: dual_load_i64_slot_poly:
; CHECK: {{ld64|d_ldw}}
; CHECK: {{add64|ld32}}
; CHECK: {{d_sw|st64|st32}}
entry:
  %a = load i64, ptr %p
  %p2 = getelementptr inbounds i64, ptr %p, i64 1
  %b = load i64, ptr %p2
  %s = add i64 %a, %b
  store i64 %s, ptr %q
  ret void
}

define void @triple_load_i64_slot_poly(ptr %p, ptr %q) nounwind {
; CHECK-LABEL: triple_load_i64_slot_poly:
; CHECK: {{ld64|d_ldw|ld32}}
; CHECK: {{add64|ld32}}
; CHECK: {{d_sw|st64|st32}}
entry:
  %a = load i64, ptr %p
  %p2 = getelementptr inbounds i64, ptr %p, i64 1
  %b = load i64, ptr %p2
  %p3 = getelementptr inbounds i64, ptr %p, i64 2
  %c = load i64, ptr %p3
  %s1 = add i64 %a, %b
  %s = add i64 %s1, %c
  store i64 %s, ptr %q
  ret void
}
