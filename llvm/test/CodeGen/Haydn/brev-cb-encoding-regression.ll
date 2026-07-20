; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -o - < %s | FileCheck %s
;
; Fixed : the 4 BREV defs (S_LW_BREV_IMM/REG, S_SW_BREV_IMM
; D_SDW_BREV_IMM) were MCID::Pseudo (silently dropped by AsmPrinter); flipped
; isPseudo=0 — now emit. The prior "Making them real regressed MAC fusion" was
; a MISDIAGNOSIS (no PatFrag overlap; MAC32 not in ISA per). See.
; (mac32 -> mul32+add32) via an unknown tablegen interaction; deferred as a
; follow-up. D_LDW_BREV_IMM/REG are real (emit). The CHECKs below document
; the desired output once BREV encoding lands.
;
; REGRESSION TEST: BREV/CB load and BREV immediate-store encodings must
; emit native mnemonics, not be silently dropped as MCID::Pseudo.
;
; Bug: D_LDW_BREV_IMM/REG, D_SDW_BREV_IMM, S_LW_BREV_IMM/REG, S_SW_BREV_IMM
; D_LDW_CB_IMM, D_LDW_CB_REG were HaydnInst<4> pseudos inside the blanket
; `let isPseudo = 1 in {... }` scope (HaydnInstrInfoAuto.td). The GISel
; selector already emitted the correct MCInst opcodes (e.g. D_LDW_BREV_IMM)
; but AsmPrinter silently dropped them because MCID::Pseudo was set, so
; no mnemonic reached assembly.
;
; Compounding bug: the IR intrinsic declarations used the pre-canonical
; underscored names (llvm.haydn.ldw_cb_imm). LLVM treats unknown names as
; external function calls and lowers them to JAL libcalls silently.
;
; Fix: converted D_LDW_BREV_IMM/REG and D_LDW_CB_IMM/REG to real defs
; with `let isPseudo = 0` (the override required inside the surrounding
; `let isPseudo = 1 in {... }` scope). applied the same fix to the
; four remaining broken BREV defs (D_SDW_BREV_IMM, S_LW_BREV_IMM/REG
; S_SW_BREV_IMM) — same defect class as the fix for
; D_LDW_POST_IMM / S_LW_POST_IMM. If this regresses, the BREV/CB mnemonics
; will disappear from assembly again — the silent-wrong-code failure is that
; the load/store has NO effect.
;
; Test design: each function calls exactly one BREV/CB intrinsic and
; returns the result so the call survives IR-level DCE. The CHECK lines
; verify the mnemonic reaches assembly. If the encoding regresses
; (pseudo bit set again, or intrinsic name typo), the mnemonic will
; disappear from the assembly output.

declare i32 @llvm.haydn.ldw.brev.imm(i32, i32)
declare i32 @llvm.haydn.ldw.brev.reg(i32, i32)
declare i32 @llvm.haydn.lw.brev.imm(i32, i32)
declare i32 @llvm.haydn.lw.brev.reg(i32, i32)
declare i32 @llvm.haydn.sdw.brev.imm(i32, i32, i32)
declare i32 @llvm.haydn.sw.brev.imm(i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.cb.imm(i32, i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.cb.reg(i32, i32, i32)

; 64-bit BREV load with immediate stride
define i32 @test_ldw_brev_imm(i32 %p) {
; CHECK-LABEL: test_ldw_brev_imm:
; CHECK: d_ldw_brev_imm
  %r = call i32 @llvm.haydn.ldw.brev.imm(i32 %p, i32 8)
  ret i32 %r
}

; 64-bit BREV load with register stride
define i32 @test_ldw_brev_reg(i32 %p, i32 %s) {
; CHECK-LABEL: test_ldw_brev_reg:
; CHECK: d_ldw_brev_reg
  %r = call i32 @llvm.haydn.ldw.brev.reg(i32 %p, i32 %s)
  ret i32 %r
}

; 32-bit BREV load with immediate stride
define i32 @test_lw_brev_imm(i32 %p) {
; CHECK-LABEL: test_lw_brev_imm:
; CHECK: s_lw_brev_imm
  %r = call i32 @llvm.haydn.lw.brev.imm(i32 %p, i32 4)
  ret i32 %r
}

; 32-bit BREV load with register stride
define i32 @test_lw_brev_reg(i32 %p, i32 %s) {
; CHECK-LABEL: test_lw_brev_reg:
; CHECK: s_lw_brev_reg
  %r = call i32 @llvm.haydn.lw.brev.reg(i32 %p, i32 %s)
  ret i32 %r
}

; 64-bit BREV store with immediate stride (data, ptr, stride) per
define i32 @test_sdw_brev_imm(i32 %d, i32 %p) {
; CHECK-LABEL: test_sdw_brev_imm:
; CHECK: d_sdw_brev_imm
  %r = call i32 @llvm.haydn.sdw.brev.imm(i32 %d, i32 %p, i32 8)
  ret i32 %r
}

; 32-bit BREV store with immediate stride
define i32 @test_sw_brev_imm(i32 %p) {
; CHECK-LABEL: test_sw_brev_imm:
; CHECK: s_sw_brev_imm
  %r = call i32 @llvm.haydn.sw.brev.imm(i32 %p, i32 4)
  ret i32 %r
}

; 64-bit CB load with immediate stride (ptr, cbr_sel, stride)
define i64 @test_ldw_cb_imm(i32 %p) {
; CHECK-LABEL: test_ldw_cb_imm:
; CHECK: d_ldw_cb_imm
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %p, i32 0, i32 8)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}

; 64-bit CB load with register stride (ptr, cbr_sel, stride)
define i64 @test_ldw_cb_reg(i32 %p, i32 %s) {
; CHECK-LABEL: test_ldw_cb_reg:
; CHECK: d_ldw_cb_reg
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.cb.reg(i32 %p, i32 1, i32 %s)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}
