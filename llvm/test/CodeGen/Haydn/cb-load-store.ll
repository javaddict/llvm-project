; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s
;
; REGRESSION TEST: Circular buffer (CB) load intrinsics must emit native
; mnemonics (not be dropped as pseudos).
;
; Bug: D_LDW_CB_IMM/REG were HaydnInst<4> pseudos inside the blanket
; `let isPseudo = 1` scope (HaydnInstrInfoAuto.td). The GISel selector
; emitted the MCInst opcodes correctly, but AsmPrinter silently dropped
; them (MCID::Pseudo), so the d_ldw_cb_imm/d_ldw_cb_reg mnemonics never
; reached assembly.
;
; Fix : convert each pseudo to a real encoding (explicit Inst{}
; bits under isAsmParserOnly=1, mirrors wave 2's SDW_BREV_REG/S_SW_BREV_REG
; treatment). If this regresses, the CB load mnemonics will disappear from
; assembly again.
;
; NOTE: CB store intrinsics (haydn_sdw_cb_imm/reg) are now also encoded
; see circular-buffer-load.ll for the load+store regression that
; guards against the AsmPrinter silently dropping D_SDW_CB_IMM/REG as
; pseudos. This file remains the focused probe for the load fix.
;
; Test design: Exercise CB load with both immediate and register stride
; variants. The loaded i64 value is returned, forcing the intrinsic call
; to survive DCE.

declare { i64, i32 } @llvm.haydn.ldw.cb.imm(i32, i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.cb.reg(i32, i32, i32)

; CB load with immediate stride
define i64 @test_ldw_cb_imm(i32 %base) {
; CHECK: test_ldw_cb_imm:
; CHECK: d_ldw_cb_imm
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %base, i32 0, i32 8)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}

; CB load with register stride
define i64 @test_ldw_cb_reg(i32 %base, i32 %stride) {
; CHECK: test_ldw_cb_reg:
; CHECK: d_ldw_cb_reg
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.cb.reg(i32 %base, i32 1, i32 %stride)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}
