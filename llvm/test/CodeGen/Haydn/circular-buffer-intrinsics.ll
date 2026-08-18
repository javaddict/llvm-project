; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -enable-misched=false -enable-post-misched=false < %s | FileCheck %s

; Role: semantic — Circular buffer (CB) and bit-reversed (BREV) addressing intrinsics must emit native mnemonics (not pseudos / libcalls).

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


define i64 @test_ldw_cb_imm(ptr %ptr) {
  %r = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: test_ldw_cb_imm_chain:
; Two CB loads must use the AGU-updated pointer (new_ptr), not re-feed %ptr.
; CHECK: d_ldw_cb_imm
; CHECK: d_ldw_cb_imm
define i64 @test_ldw_cb_imm_chain(ptr %ptr) {
  %r0 = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %p1 = extractvalue { i64, ptr } %r0, 1
  %r1 = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %p1, i32 0, i32 1)
  %d1 = extractvalue { i64, ptr } %r1, 0
  ret i64 %d1
}

; CHECK-LABEL: test_ldw_cb_reg:
; CHECK: d_ldw_cb_reg
define i64 @test_ldw_cb_reg(ptr %ptr, i32 %stride) {
  %r = call { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr %ptr, i32 1, i32 %stride)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

;Circular Buffer Store (CBR) — returns updated ptr (D208)
; CHECK-LABEL: test_sdw_cb_imm:
; CHECK: d_sdw_cb_imm
define ptr @test_sdw_cb_imm(i64 %data, ptr %ptr) {
  %np = call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %ptr, i32 0, i32 1)
  ret ptr %np
}

;Bit-Reversed Load — frexp pair {data, new_ptr}

; CHECK-LABEL: test_ldw_brev_imm:
; CHECK: d_ldw_brev_imm
define i64 @test_ldw_brev_imm(ptr %ptr) {
  %r = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %ptr, i32 4)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: test_ldw_brev_reg:
; CHECK: d_ldw_brev_reg
define i64 @test_ldw_brev_reg(ptr %ptr, i32 %stride) {
  %r = call { i64, ptr } @llvm.haydn.ldw.brev.reg(ptr %ptr, i32 %stride)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

define i32 @test_lw_brev_imm(ptr %ptr) {
; CHECK-LABEL: test_lw_brev_imm:
; CHECK: s_lw_brev_imm
  %r = call { i32, ptr } @llvm.haydn.lw.brev.imm(ptr %ptr, i32 2)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

define i32 @test_lw_brev_reg(ptr %ptr, i32 %stride) {
; CHECK-LABEL: test_lw_brev_reg:
; CHECK: s_lw_brev_reg
  %r = call { i32, ptr } @llvm.haydn.lw.brev.reg(ptr %ptr, i32 %stride)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

;Bit-Reversed Store — (data, ptr, stride) -> new_ptr

define ptr @test_sdw_brev_imm(i64 %data, ptr %ptr) {
; CHECK-LABEL: test_sdw_brev_imm:
; CHECK: d_sdw_brev_imm
  %r = call ptr @llvm.haydn.sdw.brev.imm(i64 %data, ptr %ptr, i32 8)
  ret ptr %r
}

; CHECK-LABEL: test_sdw_brev_reg:
; CHECK: d_sdw_brev_reg
define ptr @test_sdw_brev_reg(i64 %data, ptr %ptr, i32 %stride) {
  %r = call ptr @llvm.haydn.sdw.brev.reg(i64 %data, ptr %ptr, i32 %stride)
  ret ptr %r
}

define ptr @test_sw_brev_imm(i32 %data, ptr %ptr) {
; CHECK-LABEL: test_sw_brev_imm:
; CHECK: s_sw_brev_imm
  %r = call ptr @llvm.haydn.sw.brev.imm(i32 %data, ptr %ptr, i32 4)
  ret ptr %r
}

; CHECK-LABEL: test_sw_brev_reg:
; CHECK: s_sw_brev_reg
define ptr @test_sw_brev_reg(i32 %data, ptr %ptr, i32 %stride) {
  %r = call ptr @llvm.haydn.sw.brev.reg(i32 %data, ptr %ptr, i32 %stride)
  ret ptr %r
}

;Intrinsic declarations

; Circular Buffer — D208: load -> {data, new_ptr}; store -> new_ptr
declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.reg(i64, ptr, i32, i32)

; Bit-Reversed Load — frexp pair
declare { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr, i32)
declare { i64, ptr } @llvm.haydn.ldw.brev.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.reg(ptr, i32)
; Bit-Reversed Store
declare ptr @llvm.haydn.sdw.brev.imm(i64, ptr, i32)
declare ptr @llvm.haydn.sdw.brev.reg(i64, ptr, i32)
declare ptr @llvm.haydn.sw.brev.imm(i32, ptr, i32)
declare ptr @llvm.haydn.sw.brev.reg(i32, ptr, i32)
