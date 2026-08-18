; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -enable-misched=false \
; RUN:     -enable-post-misched=false < %s | FileCheck %s
;
; Role: semantic — CB AGU writeback must stay defined after Format E setDesc.
; D_LDW_CB_IMM_E2 lists dest2 as an input only; the keep-map drops the
; logical rs_wb dest. Implicit-def of that physreg (PEI) keeps the chained
; second load legal. AIE keeps the tied dest on the member.

declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)

; CHECK-LABEL: cb_ldw_chain:
; CHECK: d_ldw_cb_imm
; CHECK: d_ldw_cb_imm
; CHECK: jalr
define i64 @cb_ldw_chain(ptr %ptr) nounwind {
  %r0 = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %p1 = extractvalue { i64, ptr } %r0, 1
  %r1 = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %p1, i32 0, i32 1)
  %d1 = extractvalue { i64, ptr } %r1, 0
  ret i64 %d1
}

; CHECK-LABEL: cb_sdw_ret_ptr:
; CHECK: d_sdw_cb_imm
; CHECK: jalr
define ptr @cb_sdw_ret_ptr(i64 %data, ptr %ptr) nounwind {
  %np = call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %ptr, i32 0, i32 1)
  ret ptr %np
}
