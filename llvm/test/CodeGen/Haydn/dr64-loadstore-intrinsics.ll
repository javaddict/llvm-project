; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION: DR64 load/store BREV register variants with frexp/store model.
;   D_SDW_BREV_REG: (i64 data, ptr, stride) -> new_ptr
;   S_SW_BREV_REG:  (i32 data, ptr, stride) -> new_ptr

declare i32 @llvm.haydn.sdw.brev.reg(i64, i32, i32)
declare i32 @llvm.haydn.sw.brev.reg(i32, i32, i32)

define dso_local i32 @test_sdw_brev_reg(i64 %data, i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_sdw_brev_reg:
; CHECK: d_sdw_brev_reg
  %r = call i32 @llvm.haydn.sdw.brev.reg(i64 %data, i32 %ptr, i32 %stride)
  ret i32 %r
}

define dso_local i32 @test_sw_brev_reg(i32 %data, i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_sw_brev_reg:
; CHECK: s_sw_brev_reg
  %r = call i32 @llvm.haydn.sw.brev.reg(i32 %data, i32 %ptr, i32 %stride)
  ret i32 %r
}
