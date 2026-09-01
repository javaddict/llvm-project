; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mcpu=haydn -o - < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -mcpu=haydn -O0 -o - < %s | FileCheck %s

; Golden v2_2 AR_CBR family (WUA-CB): unaligned AR window access whose post
; step is the hardware circular wrap (+8 against CBR_BEGIN/END[cbr_sel]).
; The C cursor is the WRAPPED writeback (funnel selector is rs[2] / rs[2:1]),
; so every op must select to its concrete generated member — never a dropped
; writeback, never a software-wrap residual.

declare ptr @llvm.haydn.pltwwua.cb.post(i32, i32, ptr)
declare ptr @llvm.haydn.plqhwua.cb.post(i32, i32, ptr)
declare { i64, ptr } @llvm.haydn.ltwua.cb.post(ptr, i32, i32)
declare { i64, ptr } @llvm.haydn.lqhwua.cb.post(ptr, i32, i32)
declare ptr @llvm.haydn.stwua.cb.post(i64, ptr, i32, i32)
declare ptr @llvm.haydn.sqhwua.cb.post(i64, ptr, i32, i32)
declare ptr @llvm.haydn.wbarwua.cb(i32, i32, ptr)

define ptr @test_pltwwua_cb_post(ptr %p) {
; CHECK-LABEL: test_pltwwua_cb_post:
; CHECK: pltwwua_cb_post 1, 0, r
  %r = call ptr @llvm.haydn.pltwwua.cb.post(i32 1, i32 0, ptr %p)
  ret ptr %r
}

define ptr @test_plqhwua_cb_post(ptr %p) {
; CHECK-LABEL: test_plqhwua_cb_post:
; CHECK: plqhwua_cb_post 0, 1, r
  %r = call ptr @llvm.haydn.plqhwua.cb.post(i32 0, i32 1, ptr %p)
  ret ptr %r
}

define i64 @test_ltwua_cb_post(ptr %p) {
; CHECK-LABEL: test_ltwua_cb_post:
; CHECK: d_ltwua_cb_post 1, 0, d
  %r = call { i64, ptr } @llvm.haydn.ltwua.cb.post(ptr %p, i32 1, i32 0)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

define i64 @test_lqhwua_cb_post(ptr %p) {
; CHECK-LABEL: test_lqhwua_cb_post:
; CHECK: d_lqhwua_cb_post 0, 1, d
  %r = call { i64, ptr } @llvm.haydn.lqhwua.cb.post(ptr %p, i32 0, i32 1)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

define ptr @test_stwua_cb_post(ptr %p, i64 %d) {
; CHECK-LABEL: test_stwua_cb_post:
; CHECK: d_stwua_cb_post 1, 1, d
  %r = call ptr @llvm.haydn.stwua.cb.post(i64 %d, ptr %p, i32 1, i32 1)
  ret ptr %r
}

define ptr @test_sqhwua_cb_post(ptr %p, i64 %d) {
; CHECK-LABEL: test_sqhwua_cb_post:
; CHECK: d_sqhwua_cb_post 0, 0, d
  %r = call ptr @llvm.haydn.sqhwua.cb.post(i64 %d, ptr %p, i32 0, i32 0)
  ret ptr %r
}

define ptr @test_wbarwua_cb(ptr %p) {
; CHECK-LABEL: test_wbarwua_cb:
; CHECK: wbarwua_cb 1, 0, r
  %r = call ptr @llvm.haydn.wbarwua.cb(i32 1, i32 0, ptr %p)
  ret ptr %r
}

; Full unaligned circular load stream: prime -> load -> load; the C cursor
; fed to each op is the wrapped writeback of the previous one.
define i64 @cb_ua_load_stream(ptr %p) {
; CHECK-LABEL: cb_ua_load_stream:
; CHECK: pltwwua_cb_post 0, 0
; CHECK: d_ltwua_cb_post 0, 0
; CHECK: d_lqhwua_cb_post 0, 0
  %c0 = call ptr @llvm.haydn.pltwwua.cb.post(i32 0, i32 0, ptr %p)
  %r1 = call { i64, ptr } @llvm.haydn.ltwua.cb.post(ptr %c0, i32 0, i32 0)
  %c1 = extractvalue { i64, ptr } %r1, 1
  %d1 = extractvalue { i64, ptr } %r1, 0
  %r2 = call { i64, ptr } @llvm.haydn.lqhwua.cb.post(ptr %c1, i32 0, i32 0)
  %d2 = extractvalue { i64, ptr } %r2, 0
  %x = or i64 %d1, %d2
  ret i64 %x
}

; Full unaligned circular store stream: store -> store -> residual flush.
define void @cb_ua_store_stream(ptr %p, i64 %d0, i64 %d1) {
; CHECK-LABEL: cb_ua_store_stream:
; CHECK: d_stwua_cb_post 0, 0
; CHECK: d_sqhwua_cb_post 0, 0
; CHECK: wbarwua_cb 0, 0
  %c0 = call ptr @llvm.haydn.stwua.cb.post(i64 %d0, ptr %p, i32 0, i32 0)
  %c1 = call ptr @llvm.haydn.sqhwua.cb.post(i64 %d1, ptr %c0, i32 0, i32 0)
  call ptr @llvm.haydn.wbarwua.cb(i32 0, i32 0, ptr %c1)
  ret void
}

; Fail-closed: non-constant ar_sel is rejected by the IR verifier itself
; (immarg); the ImmCheck Sema range check rejects out-of-range constants at
; the C surface, and GISel's expectUImm/admitProductArSel are the backend
; backstop for hand-authored MIR (ar-cbr surface test covers the Sema arm).
