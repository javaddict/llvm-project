; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s
;
; E2E: AR unaligned stream intrinsics (golden LS #103-#109) must select to
; native Bundle128 mnemonics (pldwwua / d_lqhwua_post / d_ltwua_post / flar
; wbarwua / d_sqhwua_post / d_stwua_post), not drop as pseudos or libcalls.
;
; Typical stream order exercised in @ar_stream_load / @ar_stream_store.

declare void @llvm.haydn.pldwwua(i32, i32)
declare i64 @llvm.haydn.d.lqhwua.post(i32, i32, i32, i32)
declare i64 @llvm.haydn.d.ltwua.post(i32, i32, i32, i32)
declare void @llvm.haydn.flar(i32)
declare void @llvm.haydn.wbarwua(i32, i32, i32)
declare void @llvm.haydn.d.sqhwua.post(i64, i32, i32, i32, i32)
declare void @llvm.haydn.d.stwua.post(i64, i32, i32, i32, i32)

define void @test_pldwwua(i32 %ptr) {
; CHECK-LABEL: test_pldwwua:
; CHECK: pldwwua
  call void @llvm.haydn.pldwwua(i32 0, i32 %ptr)
  ret void
}

define void @test_flar() {
; CHECK-LABEL: test_flar:
; CHECK: flar
  call void @llvm.haydn.flar(i32 1)
  ret void
}

define void @test_wbarwua(i32 %ptr) {
; CHECK-LABEL: test_wbarwua:
; CHECK: wbarwua
  call void @llvm.haydn.wbarwua(i32 0, i32 %ptr, i32 0)
  ret void
}

define i64 @test_d_lqhwua_post(i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_d_lqhwua_post:
; CHECK: d_lqhwua_post
  %r = call i64 @llvm.haydn.d.lqhwua.post(i32 %ptr, i32 0, i32 %stride, i32 0)
  ret i64 %r
}

define i64 @test_d_ltwua_post(i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_d_ltwua_post:
; CHECK: d_ltwua_post
  %r = call i64 @llvm.haydn.d.ltwua.post(i32 %ptr, i32 2, i32 %stride, i32 1)
  ret i64 %r
}

define void @test_d_sqhwua_post(i64 %data, i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_d_sqhwua_post:
; CHECK: d_sqhwua_post
  call void @llvm.haydn.d.sqhwua.post(i64 %data, i32 %ptr, i32 0, i32 %stride, i32 0)
  ret void
}

define void @test_d_stwua_post(i64 %data, i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_d_stwua_post:
; CHECK: d_stwua_post
  call void @llvm.haydn.d.stwua.post(i64 %data, i32 %ptr, i32 1, i32 %stride, i32 0)
  ret void
}

; Full unaligned load stream: PLDW → load → load → FLAR
define i64 @ar_stream_load(i32 %base, i32 %ptr, i32 %stride) {
; CHECK-LABEL: ar_stream_load:
; CHECK: pldwwua
; CHECK: d_lqhwua_post
; CHECK: d_ltwua_post
; CHECK: flar
  call void @llvm.haydn.pldwwua(i32 0, i32 %base)
  %a = call i64 @llvm.haydn.d.lqhwua.post(i32 %ptr, i32 0, i32 %stride, i32 0)
  %b = call i64 @llvm.haydn.d.ltwua.post(i32 %ptr, i32 0, i32 %stride, i32 0)
  call void @llvm.haydn.flar(i32 0)
  %or = or i64 %a, %b
  ret i64 %or
}

; Full unaligned store stream: PLDW → store → store → WBAR
define void @ar_stream_store(i32 %base, i32 %ptr, i32 %stride, i64 %d0, i64 %d1) {
; CHECK-LABEL: ar_stream_store:
; CHECK: pldwwua
; CHECK: d_sqhwua_post
; CHECK: d_stwua_post
; CHECK: wbarwua
  call void @llvm.haydn.pldwwua(i32 0, i32 %base)
  call void @llvm.haydn.d.sqhwua.post(i64 %d0, i32 %ptr, i32 0, i32 %stride, i32 0)
  call void @llvm.haydn.d.stwua.post(i64 %d1, i32 %ptr, i32 0, i32 %stride, i32 0)
  call void @llvm.haydn.wbarwua(i32 0, i32 %ptr, i32 0)
  ret void
}
