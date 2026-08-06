; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s

; Role: semantic — Bit-reversed (BREV) load/store frexp pair model.

; REGRESSION: Bit-reversed (BREV) load/store frexp pair model.
; Golden:
;   D_LDW_BREV: (ptr, stride) -> {i64 data, i32 new_ptr}
;   S_LW_BREV:  (ptr, stride) -> {i32 data, i32 new_ptr}
;   D_SDW_BREV: (i64 data, ptr, stride) -> i32 new_ptr
;   S_SW_BREV:  (i32 data, ptr, stride) -> i32 new_ptr
; AGU writeback is a live SSA result (not a dead def).

; BREV load intrinsics — frexp pair

declare { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr, i32)
declare { i64, ptr } @llvm.haydn.ldw.brev.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.reg(ptr, i32)

; BREV store intrinsics — return updated ptr
declare ptr @llvm.haydn.sdw.brev.imm(i64, ptr, i32)
declare ptr @llvm.haydn.sdw.brev.reg(i64, ptr, i32)
declare ptr @llvm.haydn.sw.brev.imm(i32, ptr, i32)
declare ptr @llvm.haydn.sw.brev.reg(i32, ptr, i32)

define i64 @test_ldw_brev_imm(ptr %base) {
  %r = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %base, i32 4)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: test_ldw_brev_reg:
; CHECK: d_ldw_brev_reg
define i64 @test_ldw_brev_reg(ptr %base, i32 %stride) {
  %r = call { i64, ptr } @llvm.haydn.ldw.brev.reg(ptr %base, i32 %stride)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: test_lw_brev_imm:
; CHECK: s_lw_brev_imm
define i32 @test_lw_brev_imm(ptr %base) {
  %r = call { i32, ptr } @llvm.haydn.lw.brev.imm(ptr %base, i32 2)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

; CHECK-LABEL: test_lw_brev_reg:
; CHECK: s_lw_brev_reg
define i32 @test_lw_brev_reg(ptr %base, i32 %stride) {
  %r = call { i32, ptr } @llvm.haydn.lw.brev.reg(ptr %base, i32 %stride)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

; Chain: second load uses AGU writeback from the first.
; CHECK-LABEL: test_ldw_brev_imm_chain:
; CHECK: d_ldw_brev_imm
; CHECK: d_ldw_brev_imm
define i64 @test_ldw_brev_imm_chain(ptr %base) {
  %r0 = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %base, i32 1)
  %p1 = extractvalue { i64, ptr } %r0, 1
  %r1 = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %p1, i32 1)
  %d1 = extractvalue { i64, ptr } %r1, 0
  ret i64 %d1
}

; CHECK-LABEL: test_sdw_brev_imm:
; CHECK: d_sdw_brev_imm
define ptr @test_sdw_brev_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.sdw.brev.imm(i64 %data, ptr %base, i32 8)
  ret ptr %r
}

; CHECK-LABEL: test_sdw_brev_reg:
; CHECK: d_sdw_brev_reg
define ptr @test_sdw_brev_reg(i64 %data, ptr %base, i32 %stride) {
  %r = call ptr @llvm.haydn.sdw.brev.reg(i64 %data, ptr %base, i32 %stride)
  ret ptr %r
}

; CHECK-LABEL: test_sw_brev_imm:
; CHECK: s_sw_brev_imm
define ptr @test_sw_brev_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.sw.brev.imm(i32 %data, ptr %base, i32 4)
  ret ptr %r
}

; CHECK-LABEL: test_sw_brev_reg:
; CHECK: s_sw_brev_reg
define ptr @test_sw_brev_reg(i32 %data, ptr %base, i32 %stride) {
  %r = call ptr @llvm.haydn.sw.brev.reg(i32 %data, ptr %base, i32 %stride)
  ret ptr %r
}
