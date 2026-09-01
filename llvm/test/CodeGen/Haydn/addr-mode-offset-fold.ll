; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — Constant GEP offsets fold into simm field of word load/store (ld32/s_lw_* or st32/s_sw_*), not a free-standing ADDI + zero-offset LS.

; Constant GEP offsets fold into simm field of word load/store
; (ld32/s_lw_* or st32/s_sw_*), not a free-standing ADDI + zero-offset LS.

define i32 @test_load_gep_pos(ptr %p) {
; CHECK-LABEL: test_load_gep_pos:
; element 4 → pre-imm 4 (s_lw) or byte 16 (ld32)
; CHECK: {{s_lw_pre_imm.*4|ld32.*, 4}}
  %q = getelementptr i32, ptr %p, i32 4
  %v = load i32, ptr %q
  ret i32 %v
}

define i32 @test_load_gep_neg(ptr %p) {
; CHECK-LABEL: test_load_gep_neg:
; element -3 → pre-imm -3 or byte -12
; CHECK: {{s_lw_pre_imm.*-3|ld32.*, -3}}
  %q = getelementptr i32, ptr %p, i32 -3
  %v = load i32, ptr %q
  ret i32 %v
}

define void @test_store_gep_pos(ptr %p, i32 %v) {
; CHECK-LABEL: test_store_gep_pos:
; CHECK: {{s_sw_pre_imm.*2|st32.*, 2}}
  %q = getelementptr i32, ptr %p, i32 2
  store i32 %v, ptr %q
  ret void
}

define i32 @test_load_gep_zero(ptr %p) {
; CHECK-LABEL: test_load_gep_zero:
; CHECK: {{ld32|s_lw}}
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @test_multiple_offsets(ptr %p) {
; CHECK-LABEL: test_multiple_offsets:
; Two independent loads; may pack into one bundle.
; CHECK-DAG: {{ld32|s_lw}}
; CHECK-DAG: {{ld32|s_lw}}
; CHECK: add32
  %a = getelementptr i32, ptr %p, i32 1
  %b = getelementptr i32, ptr %p, i32 3
  %va = load i32, ptr %a
  %vb = load i32, ptr %b
  %s = add i32 %va, %vb
  ret i32 %s
}
