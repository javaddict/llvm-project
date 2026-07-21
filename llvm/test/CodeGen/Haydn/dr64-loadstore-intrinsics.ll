; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: DR64 load/store intrinsics — BREV register variants.
;
; Bug (fixed,): sdw.brev.reg intrinsic was declared with
; wrong parameter count — selector reads 3 source operands (data
; ptr_base, stride) but IntrinsicsHaydn.td defined only 2. IR verifier
; rejected with "Intrinsic has incorrect argument type!". Fixed by adding
; the data operand to both int_haydn_sdw_brev_{imm,reg} and the Clang
; builtins.
;
; What this test guards:
; SDW_BREV_REG (encoded as FmtALU32<0x80>, D_SDW_BREV_REG) emits.
; SW_BREV_REG (encoded as FmtALU32<0x93>, S_SW_BREV_REG) emits.
;
; If the SDW_BREV intrinsic signature regresses (drops the data operand)
; IR verification fails with "Intrinsic has incorrect argument type!"
; before codegen and this test fails.
;
; NOTE: LDW_BREV_REG and LW_BREV_REG are still HaydnInst<4> blanket
; pseudos with no encoding and are dropped by AsmPrinter. The BREV imm
; variants are similarly unencoded. Those gaps are tracked under M5
; wave-7 (encoding-migration-execution.md).

;===----------------------------------------------------------------------===;
; BREV store — register variants (encoded).
; sdw_brev_reg is (data, ptr_base, stride) -> i32 (3 params, — data
; operand added to match hardware + selector).
; sw_brev_reg is (ptr_base, stride) -> i32 (2 params, unchanged).
;===----------------------------------------------------------------------===;

declare i32 @llvm.haydn.sdw.brev.reg(i32, i32, i32)
declare i32 @llvm.haydn.sw.brev.reg(i32, i32)

define dso_local i32 @test_sdw_brev_reg(i32 %data, i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_sdw_brev_reg:
; CHECK: d_sdw_brev_reg
  %r = call i32 @llvm.haydn.sdw.brev.reg(i32 %data, i32 %ptr, i32 %stride)
  ret i32 %r
}

define dso_local i32 @test_sw_brev_reg(i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_sw_brev_reg:
; CHECK: s_sw_brev_reg
  %r = call i32 @llvm.haydn.sw.brev.reg(i32 %ptr, i32 %stride)
  ret i32 %r
}

;===----------------------------------------------------------------------===;
; FIXME: CANNOT SELECT — CB and BREV-imm intrinsics
; The following intrinsics crash llc with CANNOT SELECT.
; When ISel patterns are added, uncomment and add tests:
; ldw.cb.imm, ldw.cb.reg (i64, i32, i32) -> i64
; sdw.cb.imm, sdw.cb.reg (i64, i64, i32, i32) -> void
; ldw.brev.imm, lw.brev.imm (i32, i32) -> i32
; sdw.brev.imm, sw.brev.imm (i32, i32) -> i32
;===----------------------------------------------------------------------===;
