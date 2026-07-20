; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; REGRESSION TEST: Circular buffer (CB) and bit-reversed (BREV) addressing
; intrinsics must emit native mnemonics (not pseudos / libcalls).
;
; NOTE: scalar MAC32 was removed in (MULA* is DR64-only). tryFormMAC
; tryFormShiftAddMAC stub-return false. Stale mac-fusion.ll
; post-select-mac.ll / shift-add-mac.ll were deleted — unrelated
; to these BREV/CB defs.
;
; Haydn has 4 hardware Circular Buffer Registers (CBR0-CBR3) that define
; buffer boundaries. CB load/store instructions automatically wrap the
; address pointer within the circular buffer region using modulo arithmetic.
; Bit-reversed addressing applies REVERSE32 to the base address before
; memory access, used for FFT butterfly patterns.
;
; Bug: D_LDW_BREV_IMM/REG, D_SDW_BREV_IMM, S_LW_BREV_IMM/REG, S_SW_BREV_IMM
; were HaydnInst<4> pseudos inside the blanket `let isPseudo = 1` scope
; (HaydnInstrInfoAuto.td); AsmPrinter silently dropped them. D_LDW_CB_IMM/REG
; had the same problem. The selector emitted the MCInst opcodes correctly
; but the mnemonics never reached assembly. Additionally, the SDW_BREV
; intrinsics had a 2-arg IR signature missing the data operand (/).
;
; Fix: corrected the SDW_BREV IR signature to (data, ptr_base, stride).
; converted D_LDW_BREV_IMM/REG and D_LDW_CB_IMM/REG to real defs with
; `let isPseudo = 0` (the override required inside the surrounding
; `let isPseudo = 1 in {... }` scope). applied the same fix to the
; four remaining broken BREV defs (D_SDW_BREV_IMM, S_LW_BREV_IMM/REG
; S_SW_BREV_IMM) — same defect class as the fix for
; D_LDW_POST_IMM / S_LW_POST_IMM. D_SDW_CB_IMM/REG stores are still pseudos
; (4-operand void shape, encoding pending). If this regresses, the mnemonics
; will disappear from assembly again.
;
; Test design: each function returns the intrinsic result so the call
; survives DCE. If any intrinsic fails to lower, llc will crash with
; global-isel-abort=1.

;Circular Buffer Load (CBR) — D208: {data, new_ptr}

; CHECK-LABEL: test_ldw_cb_imm:
; CHECK: d_ldw_cb_imm
define i64 @test_ldw_cb_imm(i32 %ptr) {
  %r = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %ptr, i32 0, i32 1)
  %d = extractvalue { i64, i32 } %r, 0
  ret i64 %d
}

; CHECK-LABEL: test_ldw_cb_imm_chain:
; Two CB loads must use the AGU-updated pointer (new_ptr), not re-feed %ptr.
; CHECK: d_ldw_cb_imm
; CHECK: d_ldw_cb_imm
define i64 @test_ldw_cb_imm_chain(i32 %ptr) {
  %r0 = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %ptr, i32 0, i32 1)
  %p1 = extractvalue { i64, i32 } %r0, 1
  %r1 = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %p1, i32 0, i32 1)
  %d1 = extractvalue { i64, i32 } %r1, 0
  ret i64 %d1
}

; CHECK-LABEL: test_ldw_cb_reg:
; CHECK: d_ldw_cb_reg
define i64 @test_ldw_cb_reg(i32 %ptr, i32 %stride) {
  %r = call { i64, i32 } @llvm.haydn.ldw.cb.reg(i32 %ptr, i32 1, i32 %stride)
  %d = extractvalue { i64, i32 } %r, 0
  ret i64 %d
}

;Circular Buffer Store (CBR) — returns updated ptr (D208)
; CHECK-LABEL: test_sdw_cb_imm:
; CHECK: d_sdw_cb_imm
define i32 @test_sdw_cb_imm(i64 %data, i32 %ptr) {
  %np = call i32 @llvm.haydn.sdw.cb.imm(i64 %data, i32 %ptr, i32 0, i32 1)
  ret i32 %np
}

;Bit-Reversed Load

; CHECK-LABEL: test_ldw_brev_imm:
; CHECK: d_ldw_brev_imm
define i32 @test_ldw_brev_imm(i32 %ptr) {
  %r = call i32 @llvm.haydn.ldw.brev.imm(i32 %ptr, i32 4)
  ret i32 %r
}

; CHECK-LABEL: test_ldw_brev_reg:
; CHECK: d_ldw_brev_reg
define i32 @test_ldw_brev_reg(i32 %ptr, i32 %stride) {
  %r = call i32 @llvm.haydn.ldw.brev.reg(i32 %ptr, i32 %stride)
  ret i32 %r
}

; fixed the four remaining MCID::Pseudo BREV defs (S_LW_BREV_IMM/REG
; S_SW_BREV_IMM, D_SDW_BREV_IMM): they lived inside the `let isPseudo = 1 in
; {... }` block at HaydnInstrInfoAuto.td:2413 and LACKED the `let isPseudo
; = 0` override that D_LDW_BREV_IMM/REG at line 2467/2477 correctly carry
; (same defect class as the fix for D_LDW_POST_IMM / S_LW_POST_IMM).
; Before, AsmPrinter silently dropped them — the BREV load/store never
; reached assembly, so the intrinsic had NO effect (e.g. test_lw_brev_imm
; emitted just `{ xor32 r0, r0, r0; move32 r1, r2; nop }` — no load). The CHECKs
; below now verify the mnemonics reach assembly; if the isPseudo=0 override
; regresses, the load/store will vanish again — the silent-wrong-code failure.

define i32 @test_lw_brev_imm(i32 %ptr) {
; CHECK-LABEL: test_lw_brev_imm:
; CHECK: s_lw_brev_imm
  %r = call i32 @llvm.haydn.lw.brev.imm(i32 %ptr, i32 2)
  ret i32 %r
}

define i32 @test_lw_brev_reg(i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_lw_brev_reg:
; CHECK: s_lw_brev_reg
  %r = call i32 @llvm.haydn.lw.brev.reg(i32 %ptr, i32 %stride)
  ret i32 %r
}

;Bit-Reversed Store
; Per /, SDW_BREV intrinsics take (data, ptr_base, stride) and return
; the updated ptr_base. The result must be used or the call is DCE'd.

define i32 @test_sdw_brev_imm(i32 %data, i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_sdw_brev_imm:
; CHECK: d_sdw_brev_imm
  %r = call i32 @llvm.haydn.sdw.brev.imm(i32 %data, i32 %ptr, i32 8)
  ret i32 %r
}

; CHECK-LABEL: test_sdw_brev_reg:
; CHECK: d_sdw_brev_reg
define i32 @test_sdw_brev_reg(i32 %data, i32 %ptr, i32 %stride) {
  %r = call i32 @llvm.haydn.sdw.brev.reg(i32 %data, i32 %ptr, i32 %stride)
  ret i32 %r
}

define i32 @test_sw_brev_imm(i32 %ptr, i32 %stride) {
; CHECK-LABEL: test_sw_brev_imm:
; CHECK: s_sw_brev_imm
  %r = call i32 @llvm.haydn.sw.brev.imm(i32 %ptr, i32 4)
  ret i32 %r
}

; CHECK-LABEL: test_sw_brev_reg:
; CHECK: s_sw_brev_reg
define i32 @test_sw_brev_reg(i32 %ptr, i32 %stride) {
  %r = call i32 @llvm.haydn.sw.brev.reg(i32 %ptr, i32 %stride)
  ret i32 %r
}

;Intrinsic declarations

; Circular Buffer — D208: load -> {data, new_ptr}; store -> new_ptr
declare { i64, i32 } @llvm.haydn.ldw.cb.imm(i32, i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.cb.reg(i32, i32, i32)
declare i32 @llvm.haydn.sdw.cb.imm(i64, i32, i32, i32)
declare i32 @llvm.haydn.sdw.cb.reg(i64, i32, i32, i32)

; Bit-Reversed Load
declare i32 @llvm.haydn.ldw.brev.imm(i32, i32)
declare i32 @llvm.haydn.ldw.brev.reg(i32, i32)
declare i32 @llvm.haydn.lw.brev.imm(i32, i32)
declare i32 @llvm.haydn.lw.brev.reg(i32, i32)
; Bit-Reversed Store — SDW_BREV takes (data, ptr_base, stride) per.
declare i32 @llvm.haydn.sdw.brev.imm(i32, i32, i32)
declare i32 @llvm.haydn.sdw.brev.reg(i32, i32, i32)
; S_SW_BREV variants take (ptr_base, stride) per.td
declare i32 @llvm.haydn.sw.brev.imm(i32, i32)
declare i32 @llvm.haydn.sw.brev.reg(i32, i32)
