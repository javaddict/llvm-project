; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-after=instruction-select -verify-machineinstrs -o - < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj -o %t.o < %s
;
; C2.3 / G-MEM-INTRIN: ordinary vs volatile/stateful MMO policy.
;   Ordinary (Golden LS WITH/POST/PRE + BREV):
;     MIR `:: (load|store (sN) from|into %ir.…)` WITHOUT "volatile"
;   Stateful (CB + UA StateMem):
;     MIR `:: (volatile load|store (sN) from|into %ir.…)`
; Dual disjoint ordinary WITH loads retain independent non-volatile MMOs
; (co-issue-ready for AA/scheduler). Peer: Hexagon L2_load*_pbr vs V6_vgatherm*.
; No FormatID / bundle slots in public surface.

declare i64 @llvm.haydn.d.ldw.with.imm(ptr, i32)
declare void @llvm.haydn.d.sdw.with.imm(i64, ptr, i32)
declare { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr, i32)
declare ptr @llvm.haydn.d.sdw.post.imm(i64, ptr, i32)
declare { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr, i32)
declare ptr @llvm.haydn.sdw.brev.imm(i64, ptr, i32)
declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)
declare i64 @llvm.haydn.d.lqhwua.post(ptr, i32)
declare void @llvm.haydn.d.stwua.post(i64, ptr, i32)

; Ordinary MMO prints `load (sN)` / `store (sN)` — not `volatile load` /
; `volatile store`. The non-volatile spelling is the policy proof.
; CHECK-LABEL: name: ordinary_with_load
; CHECK: D_LDW_WITH_IMM{{.*}}:: (load (s64) from %ir.base
define i64 @ordinary_with_load(ptr %base) {
  %r = call i64 @llvm.haydn.d.ldw.with.imm(ptr %base, i32 0)
  ret i64 %r
}

; CHECK-LABEL: name: ordinary_with_store
; CHECK: D_SDW_WITH_IMM{{.*}}:: (store (s64) into %ir.base
define void @ordinary_with_store(i64 %data, ptr %base) {
  call void @llvm.haydn.d.sdw.with.imm(i64 %data, ptr %base, i32 0)
  ret void
}

; CHECK-LABEL: name: ordinary_post_load
; CHECK: D_LDW_POST_IMM{{.*}}:: (load (s64) from %ir.base
define i64 @ordinary_post_load(ptr %base) {
  %r = call { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr %base, i32 1)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: name: ordinary_post_store
; CHECK: D_SDW_POST_IMM{{.*}}:: (store (s64) into %ir.base
define ptr @ordinary_post_store(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sdw.post.imm(i64 %data, ptr %base, i32 0)
  ret ptr %r
}

; CHECK-LABEL: name: ordinary_brev_load
; CHECK: D_LDW_BREV_IMM{{.*}}:: (load (s64) from %ir.base
define i64 @ordinary_brev_load(ptr %base) {
  %r = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %base, i32 4)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: name: ordinary_brev_store
; CHECK: D_SDW_BREV_IMM{{.*}}:: (store (s64) into %ir.base
define ptr @ordinary_brev_store(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.sdw.brev.imm(i64 %data, ptr %base, i32 1)
  ret ptr %r
}

; CHECK-LABEL: name: stateful_cb_load
; CHECK: D_LDW_CB_IMM{{.*}}:: (volatile load (s64) from %ir.base
define i64 @stateful_cb_load(ptr %base) {
  %r = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %base, i32 0, i32 1)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: name: stateful_cb_store
; CHECK: D_SDW_CB_IMM{{.*}}:: (volatile store (s64) into %ir.base
define ptr @stateful_cb_store(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %base, i32 0, i32 1)
  ret ptr %r
}

; CHECK-LABEL: name: stateful_ua_load
; CHECK: D_LQHWUA_POST{{.*}}:: (volatile load (s64) from %ir.p
define i64 @stateful_ua_load(ptr %p) {
  %r = call i64 @llvm.haydn.d.lqhwua.post(ptr %p, i32 0)
  ret i64 %r
}

; CHECK-LABEL: name: stateful_ua_store
; CHECK: D_STWUA_POST{{.*}}:: (volatile store (s64) into %ir.p
define void @stateful_ua_store(i64 %d, ptr %p) {
  call void @llvm.haydn.d.stwua.post(i64 %d, ptr %p, i32 0)
  ret void
}

; Dual disjoint ordinary WITH loads: each keeps its own non-volatile MMO
; (independent objects — co-issue-ready for AA).
; CHECK-LABEL: name: dual_disjoint_ordinary_with
; CHECK-DAG: D_LDW_WITH_IMM{{.*}}:: (load (s64) from %ir.a
; CHECK-DAG: D_LDW_WITH_IMM{{.*}}:: (load (s64) from %ir.b
define i64 @dual_disjoint_ordinary_with(ptr %a, ptr %b) {
  %x = call i64 @llvm.haydn.d.ldw.with.imm(ptr %a, i32 0)
  %y = call i64 @llvm.haydn.d.ldw.with.imm(ptr %b, i32 0)
  %z = add i64 %x, %y
  ret i64 %z
}
