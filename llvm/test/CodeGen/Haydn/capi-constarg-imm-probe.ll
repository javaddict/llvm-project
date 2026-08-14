; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s

; Role: semantic — ImmArg encoding fields on ISel-required immediates select to native MI when constant; bare Imm after legalize is accepted via getConstOp*.

; C0.4 / C5.1: ImmArg encoding fields on ISel-required immediates select to
; native MI when constant; bare Imm after legalize is accepted via getConstOp*.
; C2.1 peer: UA bases are llvm_ptr_ty (not i32) — keep continuous with
; IntrinsicsHaydn.td pointer contracts so this probe is not XFAIL'd.

declare i32 @llvm.haydn.slli32(i32, i32)
declare i64 @llvm.haydn.srai64(i64, i32)
declare i64 @llvm.haydn.sin.cos(i32, i32)
declare i64 @llvm.haydn.movei.h(i32)
declare i32 @llvm.haydn.arctan(i64, i32)
declare void @llvm.haydn.setcbr.begin(i32, i32)
declare void @llvm.haydn.flar(i32)
declare void @llvm.haydn.pldwwua(i32, ptr)
declare i64 @llvm.haydn.d.ltwua.post(ptr, i32, i32, i32)
declare void @llvm.haydn.wbarwua(i32, ptr, i32)
declare <2 x i32> @llvm.haydn.x2slli32(<2 x i32>, i32)
declare <4 x i16> @llvm.haydn.x4seli16(<4 x i16>, <4 x i16>, i32)

define i32 @probe_slli32(i32 %a) {
  %r = call i32 @llvm.haydn.slli32(i32 %a, i32 5)
  ret i32 %r
}

; CHECK-LABEL: probe_srai64:
; CHECK: srai64
define i64 @probe_srai64(i64 %a) {
  %r = call i64 @llvm.haydn.srai64(i64 %a, i32 3)
  ret i64 %r
}

; CHECK-LABEL: probe_sin_cos:
; CHECK: sin_cos
define i64 @probe_sin_cos(i32 %phase) {
  %r = call i64 @llvm.haydn.sin.cos(i32 %phase, i32 4)
  ret i64 %r
}

; CHECK-LABEL: probe_movei_h:
; CHECK: movei_h
define i64 @probe_movei_h() {
  %r = call i64 @llvm.haydn.movei.h(i32 4660)
  ret i64 %r
}

; CHECK-LABEL: probe_arctan:
; CHECK: arctan
define i32 @probe_arctan(i64 %xy) {
  %r = call i32 @llvm.haydn.arctan(i64 %xy, i32 8)
  ret i32 %r
}

; CHECK-LABEL: probe_setcbr:
; CHECK: csrw{{(_w)?}}
define void @probe_setcbr(i32 %val) {
  call void @llvm.haydn.setcbr.begin(i32 0, i32 %val)
  ret void
}

; CHECK-LABEL: probe_ua_stream:
; CHECK: pldwwua
; CHECK: d_ltwua_post
; CHECK: flar
; CHECK: wbarwua
define i64 @probe_ua_stream(ptr %base, ptr %ptr, i32 %stride) {
  call void @llvm.haydn.pldwwua(i32 0, ptr %base)
  %v = call i64 @llvm.haydn.d.ltwua.post(ptr %ptr, i32 0, i32 %stride, i32 0)
  call void @llvm.haydn.flar(i32 0)
  call void @llvm.haydn.wbarwua(i32 1, ptr %ptr, i32 0)
  ret i64 %v
}

; CHECK-LABEL: probe_x2slli:
; CHECK: x2slli32
define <2 x i32> @probe_x2slli(<2 x i32> %a) {
  %r = call <2 x i32> @llvm.haydn.x2slli32(<2 x i32> %a, i32 2)
  ret <2 x i32> %r
}

; CHECK-LABEL: probe_x4seli:
; CHECK: x4seli16
define <4 x i16> @probe_x4seli(<4 x i16> %a, <4 x i16> %b) {
  %r = call <4 x i16> @llvm.haydn.x4seli16(<4 x i16> %a, <4 x i16> %b, i32 5)
  ret <4 x i16> %r
}
