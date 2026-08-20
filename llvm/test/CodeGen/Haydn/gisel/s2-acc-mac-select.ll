; RUN: llc -mtriple=haydn-unknown-elf -mcpu=haydn -global-isel-abort=1 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs -o - %s \
; RUN:     | FileCheck %s
;
; Role: semantic — S2 accumulating FMULAS32S / FMULSA32S / SMULA16 / MUL32X16
; select as tied-acc MAC. Ternary IR (acc, a, b); zero-accumulate forms are
; the binary MUL32X16 family.

declare i64 @llvm.haydn.fmulas32s.hhll(i64, i64, i64)
declare i64 @llvm.haydn.fmulas32s.hllh(i64, i64, i64)
declare i64 @llvm.haydn.fmulsa32s.hhll(i64, i64, i64)
declare i64 @llvm.haydn.smula16.00(i64, i64, i64)
declare i64 @llvm.haydn.smuls16.00(i64, i64, i64)
declare i64 @llvm.haydn.mulsa32.hhll(i64, i64, i64)
declare i64 @llvm.haydn.mulss32.hllh(i64, i64, i64)
declare i64 @llvm.haydn.mulsa32x16.h1.l0(i64, i64, i64)
declare i64 @llvm.haydn.mul32x16.h0(i64, i64)
declare i64 @llvm.haydn.fmulaa16.hs.13.02(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.fmulss16.ls.11.00(i64, <4 x i16>, <4 x i16>)
declare i64 @llvm.haydn.f2mulas32rs.hhll(i64, <2 x i32>, <2 x i32>)

define i64 @sel_fmulas32s_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_fmulas32s_hhll
; CHECK: FMULAS32S_HHLL
  %r = call i64 @llvm.haydn.fmulas32s.hhll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_fmulas32s_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_fmulas32s_hllh
; CHECK: FMULAS32S_HLLH
  %r = call i64 @llvm.haydn.fmulas32s.hllh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_fmulsa32s_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_fmulsa32s_hhll
; CHECK: FMULSA32S_HHLL
  %r = call i64 @llvm.haydn.fmulsa32s.hhll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_smula16_00(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_smula16_00
; CHECK: SMULA16_00 %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}
  %r = call i64 @llvm.haydn.smula16.00(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_smuls16_00(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_smuls16_00
; CHECK: SMULS16_00 %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}
  %r = call i64 @llvm.haydn.smuls16.00(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_mulsa32_hhll(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_mulsa32_hhll
; CHECK: MULSA32_HHLL %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}
  %r = call i64 @llvm.haydn.mulsa32.hhll(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_mulss32_hllh(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_mulss32_hllh
; CHECK: MULSS32_HLLH %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}
  %r = call i64 @llvm.haydn.mulss32.hllh(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_mulsa32x16_h1_l0(i64 %acc, i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_mulsa32x16_h1_l0
; CHECK: MULSA32X16_H1_L0 %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}
  %r = call i64 @llvm.haydn.mulsa32x16.h1.l0(i64 %acc, i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_mul32x16_h0(i64 %a, i64 %b) {
; CHECK-LABEL: name: sel_mul32x16_h0
; CHECK: MUL32X16_H0
  %r = call i64 @llvm.haydn.mul32x16.h0(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @sel_fmulaa16_hs_13_02(i64 %acc, <4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: name: sel_fmulaa16_hs_13_02
; CHECK: FMULAA16_HS_13_02 %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}
  %r = call i64 @llvm.haydn.fmulaa16.hs.13.02(i64 %acc, <4 x i16> %a, <4 x i16> %b)
  ret i64 %r
}

define i64 @sel_fmulss16_ls_11_00(i64 %acc, <4 x i16> %a, <4 x i16> %b) {
; CHECK-LABEL: name: sel_fmulss16_ls_11_00
; CHECK: FMULSS16_LS_11_00 %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}
  %r = call i64 @llvm.haydn.fmulss16.ls.11.00(i64 %acc, <4 x i16> %a, <4 x i16> %b)
  ret i64 %r
}

define i64 @sel_f2mulas32rs_hhll(i64 %acc, <2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: name: sel_f2mulas32rs_hhll
; CHECK: F2MULAS32RS_HHLL %{{[^,]+}}, %{{[^,]+}}, %{{[^,]+}}
  %r = call i64 @llvm.haydn.f2mulas32rs.hhll(i64 %acc, <2 x i32> %a, <2 x i32> %b)
  ret i64 %r
}
