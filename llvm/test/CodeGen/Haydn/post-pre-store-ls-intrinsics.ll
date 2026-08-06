; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-after=instruction-select -verify-machineinstrs -o - < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj -o %t.o < %s

; Role: object — Golden LS POST/PRE stores are IntrHasSideEffects and arrive as G_INTRINSIC_W_SIDE_EFFECTS.

; C0.2 / G-CAPI: Golden LS POST/PRE stores are IntrHasSideEffects and arrive
; as G_INTRINSIC_W_SIDE_EFFECTS. They must route into selectIntrinsic (no
; cannot-select), lower as single-ret writeback stores to logical Golden MI,
; and emit object.

;===----------------------------------------------------------------------===
; DR64 POST/PRE stores
;===----------------------------------------------------------------------===


define ptr @test_d_sdw_post_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sdw.post.imm(i64 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sdw_post_reg
; CHECK: D_SDW_POST_REG
define ptr @test_d_sdw_post_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.sdw.post.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sdw_pre_imm
; CHECK: D_SDW_PRE_IMM
define ptr @test_d_sdw_pre_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sdw.pre.imm(i64 %data, ptr %base, i32 1)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sdw_pre_reg
; CHECK: D_SDW_PRE_REG
define ptr @test_d_sdw_pre_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.sdw.pre.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_shw_post_imm
; CHECK: D_SHW_POST_IMM
define ptr @test_d_shw_post_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.shw.post.imm(i64 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_shw_post_reg
; CHECK: D_SHW_POST_REG
define ptr @test_d_shw_post_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.shw.post.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_shw_pre_imm
; CHECK: D_SHW_PRE_IMM
define ptr @test_d_shw_pre_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.shw.pre.imm(i64 %data, ptr %base, i32 2)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_shw_pre_reg
; CHECK: D_SHW_PRE_REG
define ptr @test_d_shw_pre_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.shw.pre.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sw_h_post_imm
; CHECK: D_SW_H_POST_IMM
define ptr @test_d_sw_h_post_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sw.h.post.imm(i64 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sw_h_post_reg
; CHECK: D_SW_H_POST_REG
define ptr @test_d_sw_h_post_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.sw.h.post.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sw_h_pre_imm
; CHECK: D_SW_H_PRE_IMM
define ptr @test_d_sw_h_pre_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sw.h.pre.imm(i64 %data, ptr %base, i32 1)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sw_h_pre_reg
; CHECK: D_SW_H_PRE_REG
define ptr @test_d_sw_h_pre_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.sw.h.pre.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sw_l_post_imm
; CHECK: D_SW_L_POST_IMM
define ptr @test_d_sw_l_post_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sw.l.post.imm(i64 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sw_l_post_reg
; CHECK: D_SW_L_POST_REG
define ptr @test_d_sw_l_post_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.sw.l.post.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sw_l_pre_imm
; CHECK: D_SW_L_PRE_IMM
define ptr @test_d_sw_l_pre_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sw.l.pre.imm(i64 %data, ptr %base, i32 3)
  ret ptr %r
}

; CHECK-LABEL: name: test_d_sw_l_pre_reg
; CHECK: D_SW_L_PRE_REG
define ptr @test_d_sw_l_pre_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.sw.l.pre.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

;===----------------------------------------------------------------------===
; GPR POST/PRE stores
;===----------------------------------------------------------------------===

; CHECK-LABEL: name: test_s_sb_post_imm
; CHECK: S_SB_POST_IMM
define ptr @test_s_sb_post_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.s.sb.post.imm(i32 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_sb_post_reg
; CHECK: S_SB_POST_REG
define ptr @test_s_sb_post_reg(i32 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.s.sb.post.reg(i32 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_sb_pre_imm
; CHECK: S_SB_PRE_IMM
define ptr @test_s_sb_pre_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.s.sb.pre.imm(i32 %data, ptr %base, i32 1)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_sb_pre_reg
; CHECK: S_SB_PRE_REG
define ptr @test_s_sb_pre_reg(i32 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.s.sb.pre.reg(i32 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_shw_post_imm
; CHECK: S_SHW_POST_IMM
define ptr @test_s_shw_post_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.s.shw.post.imm(i32 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_shw_post_reg
; CHECK: S_SHW_POST_REG
define ptr @test_s_shw_post_reg(i32 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.s.shw.post.reg(i32 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_shw_pre_imm
; CHECK: S_SHW_PRE_IMM
define ptr @test_s_shw_pre_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.s.shw.pre.imm(i32 %data, ptr %base, i32 2)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_shw_pre_reg
; CHECK: S_SHW_PRE_REG
define ptr @test_s_shw_pre_reg(i32 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.s.shw.pre.reg(i32 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_sw_post_imm
; CHECK: S_SW_POST_IMM
define ptr @test_s_sw_post_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.s.sw.post.imm(i32 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_sw_post_reg
; CHECK: S_SW_POST_REG
define ptr @test_s_sw_post_reg(i32 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.s.sw.post.reg(i32 %data, ptr %base, i32 %off)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_sw_pre_imm
; CHECK: S_SW_PRE_IMM
define ptr @test_s_sw_pre_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.s.sw.pre.imm(i32 %data, ptr %base, i32 4)
  ret ptr %r
}

; CHECK-LABEL: name: test_s_sw_pre_reg
; CHECK: S_SW_PRE_REG
define ptr @test_s_sw_pre_reg(i32 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.s.sw.pre.reg(i32 %data, ptr %base, i32 %off)
  ret ptr %r
}

declare ptr @llvm.haydn.d.sdw.post.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.sdw.post.reg(i64, ptr, i32)
declare ptr @llvm.haydn.d.sdw.pre.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.sdw.pre.reg(i64, ptr, i32)
declare ptr @llvm.haydn.d.shw.post.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.shw.post.reg(i64, ptr, i32)
declare ptr @llvm.haydn.d.shw.pre.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.shw.pre.reg(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.h.post.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.h.post.reg(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.h.pre.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.h.pre.reg(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.l.post.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.l.post.reg(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.l.pre.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.l.pre.reg(i64, ptr, i32)
declare ptr @llvm.haydn.s.sb.post.imm(i32, ptr, i32)
declare ptr @llvm.haydn.s.sb.post.reg(i32, ptr, i32)
declare ptr @llvm.haydn.s.sb.pre.imm(i32, ptr, i32)
declare ptr @llvm.haydn.s.sb.pre.reg(i32, ptr, i32)
declare ptr @llvm.haydn.s.shw.post.imm(i32, ptr, i32)
declare ptr @llvm.haydn.s.shw.post.reg(i32, ptr, i32)
declare ptr @llvm.haydn.s.shw.pre.imm(i32, ptr, i32)
declare ptr @llvm.haydn.s.shw.pre.reg(i32, ptr, i32)
declare ptr @llvm.haydn.s.sw.post.imm(i32, ptr, i32)
declare ptr @llvm.haydn.s.sw.post.reg(i32, ptr, i32)
declare ptr @llvm.haydn.s.sw.pre.imm(i32, ptr, i32)
declare ptr @llvm.haydn.s.sw.pre.reg(i32, ptr, i32)
