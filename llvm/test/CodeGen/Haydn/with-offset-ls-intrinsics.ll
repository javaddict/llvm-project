; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-after=instruction-select -verify-machineinstrs -o - < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj -o %t.o < %s
;
; C0.1 / G-CAPI: Golden LS WITH_* are IntrHasSideEffects and arrive as
; G_INTRINSIC_W_SIDE_EFFECTS. They must route into selectIntrinsic (no
; cannot-select) and become the logical WITH opcodes. Object emission is the
; second RUN (hard fail if select/encode crashes).

;===----------------------------------------------------------------------===
; DR64 WITH loads
;===----------------------------------------------------------------------===

; CHECK-LABEL: name: test_d_ldw_with_imm
; CHECK: D_LDW_WITH_IMM
define i64 @test_d_ldw_with_imm(ptr %base) {
  %r = call i64 @llvm.haydn.d.ldw.with.imm(ptr %base, i32 0)
  ret i64 %r
}

; CHECK-LABEL: name: test_d_ldw_with_reg
; CHECK: D_LDW_WITH_REG
define i64 @test_d_ldw_with_reg(ptr %base, i32 %off) {
  %r = call i64 @llvm.haydn.d.ldw.with.reg(ptr %base, i32 %off)
  ret i64 %r
}

; CHECK-LABEL: name: test_d_lhw_with_imm
; CHECK: D_LHW_WITH_IMM
define i64 @test_d_lhw_with_imm(ptr %base) {
  %r = call i64 @llvm.haydn.d.lhw.with.imm(ptr %base, i32 2)
  ret i64 %r
}

; CHECK-LABEL: name: test_d_lhw_with_reg
; CHECK: D_LHW_WITH_REG
define i64 @test_d_lhw_with_reg(ptr %base, i32 %off) {
  %r = call i64 @llvm.haydn.d.lhw.with.reg(ptr %base, i32 %off)
  ret i64 %r
}

; CHECK-LABEL: name: test_d_lw_with_imm
; CHECK: D_LW_WITH_IMM
define i64 @test_d_lw_with_imm(ptr %base) {
  %r = call i64 @llvm.haydn.d.lw.with.imm(ptr %base, i32 4)
  ret i64 %r
}

; CHECK-LABEL: name: test_d_lw_with_reg
; CHECK: D_LW_WITH_REG
define i64 @test_d_lw_with_reg(ptr %base, i32 %off) {
  %r = call i64 @llvm.haydn.d.lw.with.reg(ptr %base, i32 %off)
  ret i64 %r
}

;===----------------------------------------------------------------------===
; GPR WITH loads
;===----------------------------------------------------------------------===

; CHECK-LABEL: name: test_s_lbs_with_imm
; CHECK: S_LBS_WITH_IMM
define i32 @test_s_lbs_with_imm(ptr %base) {
  %r = call i32 @llvm.haydn.s.lbs.with.imm(ptr %base, i32 0)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lbs_with_reg
; CHECK: S_LBS_WITH_REG
define i32 @test_s_lbs_with_reg(ptr %base, i32 %off) {
  %r = call i32 @llvm.haydn.s.lbs.with.reg(ptr %base, i32 %off)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lbu_with_imm
; CHECK: S_LBU_WITH_IMM
define i32 @test_s_lbu_with_imm(ptr %base) {
  %r = call i32 @llvm.haydn.s.lbu.with.imm(ptr %base, i32 1)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lbu_with_reg
; CHECK: S_LBU_WITH_REG
define i32 @test_s_lbu_with_reg(ptr %base, i32 %off) {
  %r = call i32 @llvm.haydn.s.lbu.with.reg(ptr %base, i32 %off)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lhws_with_imm
; CHECK: S_LHWS_WITH_IMM
define i32 @test_s_lhws_with_imm(ptr %base) {
  %r = call i32 @llvm.haydn.s.lhws.with.imm(ptr %base, i32 2)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lhws_with_reg
; CHECK: S_LHWS_WITH_REG
define i32 @test_s_lhws_with_reg(ptr %base, i32 %off) {
  %r = call i32 @llvm.haydn.s.lhws.with.reg(ptr %base, i32 %off)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lhwu_with_imm
; CHECK: S_LHWU_WITH_IMM
define i32 @test_s_lhwu_with_imm(ptr %base) {
  %r = call i32 @llvm.haydn.s.lhwu.with.imm(ptr %base, i32 2)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lhwu_with_reg
; CHECK: S_LHWU_WITH_REG
define i32 @test_s_lhwu_with_reg(ptr %base, i32 %off) {
  %r = call i32 @llvm.haydn.s.lhwu.with.reg(ptr %base, i32 %off)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lw_with_imm
; CHECK: S_LW_WITH_IMM
define i32 @test_s_lw_with_imm(ptr %base) {
  %r = call i32 @llvm.haydn.s.lw.with.imm(ptr %base, i32 4)
  ret i32 %r
}

; CHECK-LABEL: name: test_s_lw_with_reg
; CHECK: S_LW_WITH_REG
define i32 @test_s_lw_with_reg(ptr %base, i32 %off) {
  %r = call i32 @llvm.haydn.s.lw.with.reg(ptr %base, i32 %off)
  ret i32 %r
}

;===----------------------------------------------------------------------===
; DR64 WITH stores
;===----------------------------------------------------------------------===

; CHECK-LABEL: name: test_d_sdw_with_imm
; CHECK: D_SDW_WITH_IMM
define void @test_d_sdw_with_imm(i64 %data, ptr %base) {
  call void @llvm.haydn.d.sdw.with.imm(i64 %data, ptr %base, i32 0)
  ret void
}

; CHECK-LABEL: name: test_d_sdw_with_reg
; CHECK: D_SDW_WITH_REG
define void @test_d_sdw_with_reg(i64 %data, ptr %base, i32 %off) {
  call void @llvm.haydn.d.sdw.with.reg(i64 %data, ptr %base, i32 %off)
  ret void
}

; CHECK-LABEL: name: test_d_shw_with_imm
; CHECK: D_SHW_WITH_IMM
define void @test_d_shw_with_imm(i64 %data, ptr %base) {
  call void @llvm.haydn.d.shw.with.imm(i64 %data, ptr %base, i32 2)
  ret void
}

; CHECK-LABEL: name: test_d_shw_with_reg
; CHECK: D_SHW_WITH_REG
define void @test_d_shw_with_reg(i64 %data, ptr %base, i32 %off) {
  call void @llvm.haydn.d.shw.with.reg(i64 %data, ptr %base, i32 %off)
  ret void
}

; CHECK-LABEL: name: test_d_sw_h_with_imm
; CHECK: D_SW_H_WITH_IMM
define void @test_d_sw_h_with_imm(i64 %data, ptr %base) {
  call void @llvm.haydn.d.sw.h.with.imm(i64 %data, ptr %base, i32 4)
  ret void
}

; CHECK-LABEL: name: test_d_sw_h_with_reg
; CHECK: D_SW_H_WITH_REG
define void @test_d_sw_h_with_reg(i64 %data, ptr %base, i32 %off) {
  call void @llvm.haydn.d.sw.h.with.reg(i64 %data, ptr %base, i32 %off)
  ret void
}

; CHECK-LABEL: name: test_d_sw_l_with_imm
; CHECK: D_SW_L_WITH_IMM
define void @test_d_sw_l_with_imm(i64 %data, ptr %base) {
  call void @llvm.haydn.d.sw.l.with.imm(i64 %data, ptr %base, i32 4)
  ret void
}

; CHECK-LABEL: name: test_d_sw_l_with_reg
; CHECK: D_SW_L_WITH_REG
define void @test_d_sw_l_with_reg(i64 %data, ptr %base, i32 %off) {
  call void @llvm.haydn.d.sw.l.with.reg(i64 %data, ptr %base, i32 %off)
  ret void
}

;===----------------------------------------------------------------------===
; GPR WITH stores
;===----------------------------------------------------------------------===

; CHECK-LABEL: name: test_s_sb_with_imm
; CHECK: S_SB_WITH_IMM
define void @test_s_sb_with_imm(i32 %data, ptr %base) {
  call void @llvm.haydn.s.sb.with.imm(i32 %data, ptr %base, i32 0)
  ret void
}

; CHECK-LABEL: name: test_s_sb_with_reg
; CHECK: S_SB_WITH_REG
define void @test_s_sb_with_reg(i32 %data, ptr %base, i32 %off) {
  call void @llvm.haydn.s.sb.with.reg(i32 %data, ptr %base, i32 %off)
  ret void
}

; CHECK-LABEL: name: test_s_shw_with_imm
; CHECK: S_SHW_WITH_IMM
define void @test_s_shw_with_imm(i32 %data, ptr %base) {
  call void @llvm.haydn.s.shw.with.imm(i32 %data, ptr %base, i32 2)
  ret void
}

; CHECK-LABEL: name: test_s_shw_with_reg
; CHECK: S_SHW_WITH_REG
define void @test_s_shw_with_reg(i32 %data, ptr %base, i32 %off) {
  call void @llvm.haydn.s.shw.with.reg(i32 %data, ptr %base, i32 %off)
  ret void
}

; CHECK-LABEL: name: test_s_sw_with_imm
; CHECK: S_SW_WITH_IMM
define void @test_s_sw_with_imm(i32 %data, ptr %base) {
  call void @llvm.haydn.s.sw.with.imm(i32 %data, ptr %base, i32 4)
  ret void
}

; CHECK-LABEL: name: test_s_sw_with_reg
; CHECK: S_SW_WITH_REG
define void @test_s_sw_with_reg(i32 %data, ptr %base, i32 %off) {
  call void @llvm.haydn.s.sw.with.reg(i32 %data, ptr %base, i32 %off)
  ret void
}

declare i64 @llvm.haydn.d.ldw.with.imm(ptr, i32)
declare i64 @llvm.haydn.d.ldw.with.reg(ptr, i32)
declare i64 @llvm.haydn.d.lhw.with.imm(ptr, i32)
declare i64 @llvm.haydn.d.lhw.with.reg(ptr, i32)
declare i64 @llvm.haydn.d.lw.with.imm(ptr, i32)
declare i64 @llvm.haydn.d.lw.with.reg(ptr, i32)

declare i32 @llvm.haydn.s.lbs.with.imm(ptr, i32)
declare i32 @llvm.haydn.s.lbs.with.reg(ptr, i32)
declare i32 @llvm.haydn.s.lbu.with.imm(ptr, i32)
declare i32 @llvm.haydn.s.lbu.with.reg(ptr, i32)
declare i32 @llvm.haydn.s.lhws.with.imm(ptr, i32)
declare i32 @llvm.haydn.s.lhws.with.reg(ptr, i32)
declare i32 @llvm.haydn.s.lhwu.with.imm(ptr, i32)
declare i32 @llvm.haydn.s.lhwu.with.reg(ptr, i32)
declare i32 @llvm.haydn.s.lw.with.imm(ptr, i32)
declare i32 @llvm.haydn.s.lw.with.reg(ptr, i32)

declare void @llvm.haydn.d.sdw.with.imm(i64, ptr, i32)
declare void @llvm.haydn.d.sdw.with.reg(i64, ptr, i32)
declare void @llvm.haydn.d.shw.with.imm(i64, ptr, i32)
declare void @llvm.haydn.d.shw.with.reg(i64, ptr, i32)
declare void @llvm.haydn.d.sw.h.with.imm(i64, ptr, i32)
declare void @llvm.haydn.d.sw.h.with.reg(i64, ptr, i32)
declare void @llvm.haydn.d.sw.l.with.imm(i64, ptr, i32)
declare void @llvm.haydn.d.sw.l.with.reg(i64, ptr, i32)

declare void @llvm.haydn.s.sb.with.imm(i32, ptr, i32)
declare void @llvm.haydn.s.sb.with.reg(i32, ptr, i32)
declare void @llvm.haydn.s.shw.with.imm(i32, ptr, i32)
declare void @llvm.haydn.s.shw.with.reg(i32, ptr, i32)
declare void @llvm.haydn.s.sw.with.imm(i32, ptr, i32)
declare void @llvm.haydn.s.sw.with.reg(i32, ptr, i32)
