; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-after=instruction-select -verify-machineinstrs -o - < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj -o %t.o < %s

; Role: object — – / : getTgtMemIntrinsic + GISel MMO clone.

; C2.2–C2.3 / G-MEM-INTRIN: getTgtMemIntrinsic + GISel MMO clone.
; Public CB/BREV/Golden WITH/POST/PRE mem intrinsics must:
;   1) Attach MMOs in IRTranslator (via HaydnTargetLowering::getTgtMemIntrinsic)
;   2) Clone those MMOs onto selected target MI
; Exit gate: MIR shows `:: (load|store (sN) from|into %ir.…)` (object + size +
; load/store flags). Align may be elided when natural for the access width.
; C2.3 policy: CB is stateful → `volatile load/store`; Golden LS / BREV ordinary
; → non-volatile. Peer: Hexagon L2_load*_pbr / V6_vgatherm*. No FormatID/slots.

declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)
declare { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr, i32)
declare ptr @llvm.haydn.sdw.brev.imm(i64, ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.imm(ptr, i32)
declare ptr @llvm.haydn.sw.brev.imm(i32, ptr, i32)
declare i64 @llvm.haydn.d.ldw.with.imm(ptr, i32)
declare void @llvm.haydn.d.sdw.with.imm(i64, ptr, i32)
declare i64 @llvm.haydn.d.lw.with.imm(ptr, i32)
declare { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr, i32)
declare ptr @llvm.haydn.d.sdw.post.imm(i64, ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lw.post.imm(ptr, i32)
declare ptr @llvm.haydn.s.sw.post.imm(i32, ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lbs.pre.imm(ptr, i32)
declare ptr @llvm.haydn.s.sb.pre.imm(i32, ptr, i32)

define i64 @mmo_ldw_cb_imm(ptr %base) {
  %r = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %base, i32 0, i32 1)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: name: mmo_sdw_cb_imm
; CHECK: D_SDW_CB_IMM{{.*}}:: (volatile store (s64) into %ir.base
define ptr @mmo_sdw_cb_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %base, i32 0, i32 1)
  ret ptr %r
}

; CHECK-LABEL: name: mmo_ldw_brev_imm
; CHECK: D_LDW_BREV_IMM{{.*}}:: (load (s64) from %ir.base
define i64 @mmo_ldw_brev_imm(ptr %base) {
  %r = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %base, i32 4)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: name: mmo_sdw_brev_imm
; CHECK: D_SDW_BREV_IMM{{.*}}:: (store (s64) into %ir.base
define ptr @mmo_sdw_brev_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.sdw.brev.imm(i64 %data, ptr %base, i32 1)
  ret ptr %r
}

; CHECK-LABEL: name: mmo_lw_brev_imm
; CHECK: S_LW_BREV_IMM{{.*}}:: (load (s32) from %ir.base
define i32 @mmo_lw_brev_imm(ptr %base) {
  %r = call { i32, ptr } @llvm.haydn.lw.brev.imm(ptr %base, i32 2)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

; CHECK-LABEL: name: mmo_sw_brev_imm
; CHECK: S_SW_BREV_IMM{{.*}}:: (store (s32) into %ir.base
define ptr @mmo_sw_brev_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.sw.brev.imm(i32 %data, ptr %base, i32 1)
  ret ptr %r
}

; CHECK-LABEL: name: mmo_d_ldw_with_imm
; CHECK: D_LDW_WITH_IMM{{.*}}:: (load (s64) from %ir.base
define i64 @mmo_d_ldw_with_imm(ptr %base) {
  %r = call i64 @llvm.haydn.d.ldw.with.imm(ptr %base, i32 0)
  ret i64 %r
}

; CHECK-LABEL: name: mmo_d_sdw_with_imm
; CHECK: D_SDW_WITH_IMM{{.*}}:: (store (s64) into %ir.base
define void @mmo_d_sdw_with_imm(i64 %data, ptr %base) {
  call void @llvm.haydn.d.sdw.with.imm(i64 %data, ptr %base, i32 0)
  ret void
}

; CHECK-LABEL: name: mmo_d_lw_with_imm
; CHECK: D_LW_WITH_IMM{{.*}}:: (load (s32) from %ir.base
define i64 @mmo_d_lw_with_imm(ptr %base) {
  %r = call i64 @llvm.haydn.d.lw.with.imm(ptr %base, i32 0)
  ret i64 %r
}

; CHECK-LABEL: name: mmo_d_ldw_post_imm
; CHECK: D_LDW_POST_IMM{{.*}}:: (load (s64) from %ir.base
define i64 @mmo_d_ldw_post_imm(ptr %base) {
  %r = call { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr %base, i32 1)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: name: mmo_d_sdw_post_imm
; CHECK: D_SDW_POST_IMM{{.*}}:: (store (s64) into %ir.base
define ptr @mmo_d_sdw_post_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sdw.post.imm(i64 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: mmo_s_lw_post_imm
; CHECK: S_LW_POST_IMM{{.*}}:: (load (s32) from %ir.base
define i32 @mmo_s_lw_post_imm(ptr %base) {
  %r = call { i32, ptr } @llvm.haydn.s.lw.post.imm(ptr %base, i32 1)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

; CHECK-LABEL: name: mmo_s_sw_post_imm
; CHECK: S_SW_POST_IMM{{.*}}:: (store (s32) into %ir.base
define ptr @mmo_s_sw_post_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.s.sw.post.imm(i32 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: mmo_s_lbs_pre_imm
; CHECK: S_LBS_PRE_IMM{{.*}}:: (load (s8) from %ir.base
define i32 @mmo_s_lbs_pre_imm(ptr %base) {
  %r = call { i32, ptr } @llvm.haydn.s.lbs.pre.imm(ptr %base, i32 1)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

; CHECK-LABEL: name: mmo_s_sb_pre_imm
; CHECK: S_SB_PRE_IMM{{.*}}:: (store (s8) into %ir.base
define ptr @mmo_s_sb_pre_imm(i32 %data, ptr %base) {
  %r = call ptr @llvm.haydn.s.sb.pre.imm(i32 %data, ptr %base, i32 1)
  ret ptr %r
}
