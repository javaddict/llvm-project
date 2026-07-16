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
; REGRESSION TEST: Bit-reversed (BREV) addressing load/store must emit native
; mnemonics (not be dropped as pseudos).
;
; Bug: D_LDW_BREV_IMM/REG, D_SDW_BREV_IMM, S_LW_BREV_IMM/REG, S_SW_BREV_IMM
; were defined as HaydnInst<4> pseudos inside the blanket `let isPseudo = 1`
; scope (HaydnInstrInfoAuto.td). The GISel selector emitted the MCInst
; opcodes correctly, but AsmPrinter silently dropped them (MCID::Pseudo)
; so the BREV mnemonics never reached assembly.
;
; Fix: converted D_LDW_BREV_IMM/REG to real defs with `let isPseudo = 0`
; (the override required inside the surrounding `let isPseudo = 1 in {... }`
; scope). applied the same fix to the four remaining broken defs
; (D_SDW_BREV_IMM, S_LW_BREV_IMM, S_LW_BREV_REG, S_SW_BREV_IMM) — same defect
; class as the fix for D_LDW_POST_IMM / S_LW_POST_IMM. If this regresses
; (the override removed, or a def moved back under isPseudo=1), the BREV
; mnemonics will disappear from assembly again — the silent-wrong-code failure
; is that the load/store has NO effect (e.g. test_sw_brev_imm would emit just
; `{ xor32 r0, r0, r0; nop; nop }` with no store).
;
; Test design: Exercise BREV load and store with both immediate and register
; stride variants. Each function returns the intrinsic result so the call
; survives DCE.

; BREV load intrinsics — (ptr_base, stride) -> result
declare i32 @llvm.haydn.ldw.brev.imm(i32, i32)
declare i32 @llvm.haydn.ldw.brev.reg(i32, i32)
declare i32 @llvm.haydn.lw.brev.imm(i32, i32)
declare i32 @llvm.haydn.lw.brev.reg(i32, i32)

; BREV store intrinsics:
; D_SDW_BREV variants: (data, ptr_base, stride) -> updated_ptr (3 params)
; S_SW_BREV variants: (ptr_base, stride) -> updated_ptr (2 params)
; Note: IntrNoMem means the result must be used or the call is DCE'd.
declare i32 @llvm.haydn.sdw.brev.imm(i32, i32, i32)
declare i32 @llvm.haydn.sdw.brev.reg(i32, i32, i32)
declare i32 @llvm.haydn.sw.brev.imm(i32, i32)
declare i32 @llvm.haydn.sw.brev.reg(i32, i32)

; BREV load with immediate stride — must not crash
define i32 @test_ldw_brev_imm(i32 %base) {
; CHECK: test_ldw_brev_imm:
; CHECK: d_ldw_brev_imm
  %r = call i32 @llvm.haydn.ldw.brev.imm(i32 %base, i32 4)
  ret i32 %r
}

; BREV load with register stride — must not crash
define i32 @test_ldw_brev_reg(i32 %base, i32 %stride) {
; CHECK: test_ldw_brev_reg:
; CHECK: d_ldw_brev_reg
  %r = call i32 @llvm.haydn.ldw.brev.reg(i32 %base, i32 %stride)
  ret i32 %r
}

; BREV 32-bit load with immediate stride
define i32 @test_lw_brev_imm(i32 %base) {
; CHECK-LABEL: test_lw_brev_imm:
; CHECK: s_lw_brev_imm
  %r = call i32 @llvm.haydn.lw.brev.imm(i32 %base, i32 2)
  ret i32 %r
}

; BREV 32-bit load with register stride
define i32 @test_lw_brev_reg(i32 %base, i32 %stride) {
; CHECK-LABEL: test_lw_brev_reg:
; CHECK: s_lw_brev_reg
  %r = call i32 @llvm.haydn.lw.brev.reg(i32 %base, i32 %stride)
  ret i32 %r
}

; BREV store with immediate stride — result returned to prevent DCE
define i32 @test_sdw_brev_imm(i32 %data, i32 %base) {
; CHECK-LABEL: test_sdw_brev_imm:
; CHECK: d_sdw_brev_imm
  %r = call i32 @llvm.haydn.sdw.brev.imm(i32 %data, i32 %base, i32 8)
  ret i32 %r
}

; BREV store with register stride — result returned to prevent DCE
define i32 @test_sdw_brev_reg(i32 %data, i32 %base, i32 %stride) {
; CHECK: test_sdw_brev_reg:
; CHECK: d_sdw_brev_reg
  %r = call i32 @llvm.haydn.sdw.brev.reg(i32 %data, i32 %base, i32 %stride)
  ret i32 %r
}

; BREV 32-bit store with immediate stride — result returned to prevent DCE
; sw_brev_imm takes (ptr_base, stride) -> updated_ptr
define i32 @test_sw_brev_imm(i32 %base) {
; CHECK-LABEL: test_sw_brev_imm:
; CHECK: s_sw_brev_imm
  %r = call i32 @llvm.haydn.sw.brev.imm(i32 %base, i32 4)
  ret i32 %r
}

; BREV 32-bit store with register stride — result returned to prevent DCE
; sw_brev_reg takes (ptr_base, stride_reg) -> updated_ptr
define i32 @test_sw_brev_reg(i32 %base, i32 %stride) {
; CHECK: test_sw_brev_reg:
; CHECK: s_sw_brev_reg
  %r = call i32 @llvm.haydn.sw.brev.reg(i32 %base, i32 %stride)
  ret i32 %r
}
