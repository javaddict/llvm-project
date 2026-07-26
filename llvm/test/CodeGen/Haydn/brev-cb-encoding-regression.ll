; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s
;
; REGRESSION: BREV/CB load and BREV store encodings must emit native
; mnemonics (not MCID::Pseudo / libcalls). BREV frexp pair model:
;   loads  -> {data, new_ptr}
;   stores -> new_ptr

declare { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr, i32)
declare { i64, ptr } @llvm.haydn.ldw.brev.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.reg(ptr, i32)
declare ptr @llvm.haydn.sdw.brev.imm(i64, ptr, i32)
declare ptr @llvm.haydn.sw.brev.imm(i32, ptr, i32)
declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr, i32, i32)

; CHECK-LABEL: test_ldw_brev_imm:
; CHECK: d_ldw_brev_imm
define i64 @test_ldw_brev_imm(ptr %p) {
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %p, i32 8)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

; CHECK-LABEL: test_ldw_brev_reg:
; CHECK: d_ldw_brev_reg
define i64 @test_ldw_brev_reg(ptr %p, i32 %s) {
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.brev.reg(ptr %p, i32 %s)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

; CHECK-LABEL: test_lw_brev_imm:
; CHECK: s_lw_brev_imm
define i32 @test_lw_brev_imm(ptr %p) {
  %r_pair = call { i32, ptr } @llvm.haydn.lw.brev.imm(ptr %p, i32 4)
  %r = extractvalue { i32, ptr } %r_pair, 0
  ret i32 %r
}

; CHECK-LABEL: test_lw_brev_reg:
; CHECK: s_lw_brev_reg
define i32 @test_lw_brev_reg(ptr %p, i32 %s) {
  %r_pair = call { i32, ptr } @llvm.haydn.lw.brev.reg(ptr %p, i32 %s)
  %r = extractvalue { i32, ptr } %r_pair, 0
  ret i32 %r
}

; CHECK-LABEL: test_sdw_brev_imm:
; CHECK: d_sdw_brev_imm
define ptr @test_sdw_brev_imm(i64 %d, ptr %p) {
  %r = call ptr @llvm.haydn.sdw.brev.imm(i64 %d, ptr %p, i32 8)
  ret ptr %r
}

; CHECK-LABEL: test_sw_brev_imm:
; CHECK: s_sw_brev_imm
define ptr @test_sw_brev_imm(i32 %d, ptr %p) {
  %r = call ptr @llvm.haydn.sw.brev.imm(i32 %d, ptr %p, i32 4)
  ret ptr %r
}

; CHECK-LABEL: test_ldw_cb_imm:
; CHECK: d_ldw_cb_imm
define i64 @test_ldw_cb_imm(ptr %p) {
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %p, i32 0, i32 8)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

; CHECK-LABEL: test_ldw_cb_reg:
; CHECK: d_ldw_cb_reg
define i64 @test_ldw_cb_reg(ptr %p, i32 %s) {
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr %p, i32 1, i32 %s)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}
