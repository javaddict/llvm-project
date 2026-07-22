; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s
;
; POST/PRE AGU writeback loads must return {data, new_ptr} and select to the
; 2-def golden mnemonics (data + rs_wb). Mirrors cb-load-store.ll frexp model.

declare { i32, i32 } @llvm.haydn.s.lw.post.imm(i32, i32)
declare { i32, i32 } @llvm.haydn.s.lw.post.reg(i32, i32)
declare { i32, i32 } @llvm.haydn.s.lw.pre.imm(i32, i32)
declare { i64, i32 } @llvm.haydn.d.ldw.post.imm(i32, i32)
declare { i64, i32 } @llvm.haydn.d.ldw.post.reg(i32, i32)
declare { i64, i32 } @llvm.haydn.d.ldw.pre.imm(i32, i32)
declare { i32, i32 } @llvm.haydn.s.lbs.post.imm(i32, i32)
declare { i32, i32 } @llvm.haydn.s.lhwu.pre.reg(i32, i32)
declare { i64, i32 } @llvm.haydn.d.lw.post.imm(i32, i32)
declare { i64, i32 } @llvm.haydn.d.lhw.pre.reg(i32, i32)

define i32 @test_s_lw_post_imm(i32 %base) {
; CHECK-LABEL: test_s_lw_post_imm:
; CHECK: s_lw_post_imm
  %p = call { i32, i32 } @llvm.haydn.s.lw.post.imm(i32 %base, i32 1)
  %d = extractvalue { i32, i32 } %p, 0
  %np = extractvalue { i32, i32 } %p, 1
  %s = add i32 %d, %np
  ret i32 %s
}

define i32 @test_s_lw_post_reg(i32 %base, i32 %off) {
; CHECK-LABEL: test_s_lw_post_reg:
; CHECK: s_lw_post_reg
  %p = call { i32, i32 } @llvm.haydn.s.lw.post.reg(i32 %base, i32 %off)
  %d = extractvalue { i32, i32 } %p, 0
  %np = extractvalue { i32, i32 } %p, 1
  %s = add i32 %d, %np
  ret i32 %s
}

define i32 @test_s_lw_pre_imm(i32 %base) {
; CHECK-LABEL: test_s_lw_pre_imm:
; CHECK: s_lw_pre_imm
  %p = call { i32, i32 } @llvm.haydn.s.lw.pre.imm(i32 %base, i32 2)
  %d = extractvalue { i32, i32 } %p, 0
  ret i32 %d
}

define i64 @test_d_ldw_post_imm(i32 %base) {
; CHECK-LABEL: test_d_ldw_post_imm:
; CHECK: d_ldw_post_imm
  %p = call { i64, i32 } @llvm.haydn.d.ldw.post.imm(i32 %base, i32 1)
  %d = extractvalue { i64, i32 } %p, 0
  ret i64 %d
}

define i64 @test_d_ldw_post_reg(i32 %base, i32 %off) {
; CHECK-LABEL: test_d_ldw_post_reg:
; CHECK: d_ldw_post_reg
  %p = call { i64, i32 } @llvm.haydn.d.ldw.post.reg(i32 %base, i32 %off)
  %d = extractvalue { i64, i32 } %p, 0
  ret i64 %d
}

define i64 @test_d_ldw_pre_imm(i32 %base) {
; CHECK-LABEL: test_d_ldw_pre_imm:
; CHECK: d_ldw_pre_imm
  %p = call { i64, i32 } @llvm.haydn.d.ldw.pre.imm(i32 %base, i32 1)
  %d = extractvalue { i64, i32 } %p, 0
  ret i64 %d
}

define i32 @test_s_lbs_post_imm(i32 %base) {
; CHECK-LABEL: test_s_lbs_post_imm:
; CHECK: s_lbs_post_imm
  %p = call { i32, i32 } @llvm.haydn.s.lbs.post.imm(i32 %base, i32 4)
  %d = extractvalue { i32, i32 } %p, 0
  ret i32 %d
}

define i32 @test_s_lhwu_pre_reg(i32 %base, i32 %off) {
; CHECK-LABEL: test_s_lhwu_pre_reg:
; CHECK: s_lhwu_pre_reg
  %p = call { i32, i32 } @llvm.haydn.s.lhwu.pre.reg(i32 %base, i32 %off)
  %d = extractvalue { i32, i32 } %p, 0
  ret i32 %d
}

define i64 @test_d_lw_post_imm(i32 %base) {
; CHECK-LABEL: test_d_lw_post_imm:
; CHECK: d_lw_post_imm
  %p = call { i64, i32 } @llvm.haydn.d.lw.post.imm(i32 %base, i32 1)
  %d = extractvalue { i64, i32 } %p, 0
  ret i64 %d
}

define i64 @test_d_lhw_pre_reg(i32 %base, i32 %off) {
; CHECK-LABEL: test_d_lhw_pre_reg:
; CHECK: d_lhw_pre_reg
  %p = call { i64, i32 } @llvm.haydn.d.lhw.pre.reg(i32 %base, i32 %off)
  %d = extractvalue { i64, i32 } %p, 0
  ret i64 %d
}
