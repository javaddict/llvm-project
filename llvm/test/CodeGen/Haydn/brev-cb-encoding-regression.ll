; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s
;
; REGRESSION: BREV/CB load and BREV store encodings must emit native
; mnemonics (not MCID::Pseudo / libcalls). BREV frexp pair model:
;   loads  -> {data, new_ptr}
;   stores -> new_ptr

declare { i64, i32 } @llvm.haydn.ldw.brev.imm(i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.brev.reg(i32, i32)
declare { i32, i32 } @llvm.haydn.lw.brev.imm(i32, i32)
declare { i32, i32 } @llvm.haydn.lw.brev.reg(i32, i32)
declare i32 @llvm.haydn.sdw.brev.imm(i64, i32, i32)
declare i32 @llvm.haydn.sw.brev.imm(i32, i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.cb.imm(i32, i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.cb.reg(i32, i32, i32)

; CHECK-LABEL: test_ldw_brev_imm:
; CHECK: d_ldw_brev_imm
define i64 @test_ldw_brev_imm(i32 %p) {
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.brev.imm(i32 %p, i32 8)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}

; CHECK-LABEL: test_ldw_brev_reg:
; CHECK: d_ldw_brev_reg
define i64 @test_ldw_brev_reg(i32 %p, i32 %s) {
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.brev.reg(i32 %p, i32 %s)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}

; CHECK-LABEL: test_lw_brev_imm:
; CHECK: s_lw_brev_imm
define i32 @test_lw_brev_imm(i32 %p) {
  %r_pair = call { i32, i32 } @llvm.haydn.lw.brev.imm(i32 %p, i32 4)
  %r = extractvalue { i32, i32 } %r_pair, 0
  ret i32 %r
}

; CHECK-LABEL: test_lw_brev_reg:
; CHECK: s_lw_brev_reg
define i32 @test_lw_brev_reg(i32 %p, i32 %s) {
  %r_pair = call { i32, i32 } @llvm.haydn.lw.brev.reg(i32 %p, i32 %s)
  %r = extractvalue { i32, i32 } %r_pair, 0
  ret i32 %r
}

; CHECK-LABEL: test_sdw_brev_imm:
; CHECK: d_sdw_brev_imm
define i32 @test_sdw_brev_imm(i64 %d, i32 %p) {
  %r = call i32 @llvm.haydn.sdw.brev.imm(i64 %d, i32 %p, i32 8)
  ret i32 %r
}

; CHECK-LABEL: test_sw_brev_imm:
; CHECK: s_sw_brev_imm
define i32 @test_sw_brev_imm(i32 %d, i32 %p) {
  %r = call i32 @llvm.haydn.sw.brev.imm(i32 %d, i32 %p, i32 4)
  ret i32 %r
}

; CHECK-LABEL: test_ldw_cb_imm:
; CHECK: d_ldw_cb_imm
define i64 @test_ldw_cb_imm(i32 %p) {
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %p, i32 0, i32 8)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}

; CHECK-LABEL: test_ldw_cb_reg:
; CHECK: d_ldw_cb_reg
define i64 @test_ldw_cb_reg(i32 %p, i32 %s) {
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.cb.reg(i32 %p, i32 1, i32 %s)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}
