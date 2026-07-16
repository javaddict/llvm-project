; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck %s
;
; REGRESSION TEST: Advanced circular buffer and bit-reversed addressing intrinsics.
;
; Intrinsic names use the canonical dotted form (llvm.haydn.ldw.cb.imm etc.)
; matching cb-load-store.ll / circular-buffer-intrinsics.ll. IMM variants require
; constant cbr_sel / stride arguments (selector folds Imm); REG variants take a
; register stride. Underscore forms are not recognized as intrinsics.
;
; Haydn has hardware circular buffer registers (CBR0-CBR3) that define
; buffer begin/end boundaries. The CB load/store instructions automatically
; wrap the address pointer within the circular buffer region using modulo
; arithmetic. This is critical for DSP FIR/IIR filter delay lines and
; double-buffered DMA ping-pong patterns.
;
; Bit-reversed addressing applies REVERSE32 to the base address before
; memory access, then post-increments the original (non-reversed) base
; register. Used for FFT butterfly addressing patterns.
;
; Test strategy: Use -stop-after=instruction-select because CB/BREV
; instructions with tied operands and immediate fields are handled by
; custom C++ in the instruction selector. At the MIR level we can verify
; the correct machine instructions are selected.
;
; If any intrinsic fails to lower, llc will crash with -global-isel-abort=1.

;=============================================================================
; Part 1: Circular Buffer Load (CBR) — immediate stride
;=============================================================================
; D_LDW_CB_IMM: 64-bit load from circular buffer, post-increment by imm8<<3
; Intrinsic: (i32 ptr_base, i32 cbr_sel, i32 stride) -> i64

; CHECK-LABEL: name: test_ldw_cb_imm
; CHECK: D_LDW_CB_IMM
define i64 @test_ldw_cb_imm(i32 %ptr) {
  %r = call i64 @llvm.haydn.ldw.cb.imm(i32 %ptr, i32 0, i32 8)
  ret i64 %r
}

;=============================================================================
; Part 2: Circular Buffer Load (CBR) — register stride
;=============================================================================
; D_LDW_CB_REG: 64-bit load from circular buffer, post-increment by register

; CHECK-LABEL: name: test_ldw_cb_reg
; CHECK: D_LDW_CB_REG
define i64 @test_ldw_cb_reg(i32 %ptr, i32 %stride) {
  %r = call i64 @llvm.haydn.ldw.cb.reg(i32 %ptr, i32 1, i32 %stride)
  ret i64 %r
}

;=============================================================================
; Part 3: Circular Buffer Store (CBR) — immediate / register stride
;=============================================================================
; D_SDW_CB_IMM/REG: void IntrWriteMem — side effects keep them live.

; CHECK-LABEL: name: test_sdw_cb_imm
; CHECK: D_SDW_CB_IMM
define void @test_sdw_cb_imm(i64 %data, i32 %ptr) {
  call void @llvm.haydn.sdw.cb.imm(i64 %data, i32 %ptr, i32 0, i32 8)
  ret void
}

; CHECK-LABEL: name: test_sdw_cb_reg
; CHECK: D_SDW_CB_REG
define void @test_sdw_cb_reg(i64 %data, i32 %ptr, i32 %stride) {
  call void @llvm.haydn.sdw.cb.reg(i64 %data, i32 %ptr, i32 1, i32 %stride)
  ret void
}

;=============================================================================
; Part 4: Bit-Reversed Load — 64-bit, immediate / register stride
;=============================================================================

; CHECK-LABEL: name: test_ldw_brev_imm
; CHECK: D_LDW_BREV_IMM
define i32 @test_ldw_brev_imm(i32 %ptr) {
  %r = call i32 @llvm.haydn.ldw.brev.imm(i32 %ptr, i32 4)
  ret i32 %r
}

; CHECK-LABEL: name: test_ldw_brev_reg
; CHECK: D_LDW_BREV_REG
define i32 @test_ldw_brev_reg(i32 %ptr, i32 %stride) {
  %r = call i32 @llvm.haydn.ldw.brev.reg(i32 %ptr, i32 %stride)
  ret i32 %r
}

;=============================================================================
; Part 5: Bit-Reversed Load — 32-bit, immediate / register stride
;=============================================================================

; CHECK-LABEL: name: test_lw_brev_imm
; CHECK: S_LW_BREV_IMM
define i32 @test_lw_brev_imm(i32 %ptr) {
  %r = call i32 @llvm.haydn.lw.brev.imm(i32 %ptr, i32 2)
  ret i32 %r
}

; CHECK-LABEL: name: test_lw_brev_reg
; CHECK: S_LW_BREV_REG
define i32 @test_lw_brev_reg(i32 %ptr, i32 %stride) {
  %r = call i32 @llvm.haydn.lw.brev.reg(i32 %ptr, i32 %stride)
  ret i32 %r
}

;=============================================================================
; Part 6: Bit-Reversed Store — 64-bit, immediate / register stride
;=============================================================================

; CHECK-LABEL: name: test_sdw_brev_imm
; CHECK: D_SDW_BREV_IMM
define i32 @test_sdw_brev_imm(i32 %data, i32 %ptr) {
  %r = call i32 @llvm.haydn.sdw.brev.imm(i32 %data, i32 %ptr, i32 8)
  ret i32 %r
}

; CHECK-LABEL: name: test_sdw_brev_reg
; CHECK: D_SDW_BREV_REG
define i32 @test_sdw_brev_reg(i32 %data, i32 %ptr, i32 %stride) {
  %r = call i32 @llvm.haydn.sdw.brev.reg(i32 %data, i32 %ptr, i32 %stride)
  ret i32 %r
}

;=============================================================================
; Part 7: Bit-Reversed Store — 32-bit, immediate / register stride
;=============================================================================

; CHECK-LABEL: name: test_sw_brev_imm
; CHECK: S_SW_BREV_IMM
define i32 @test_sw_brev_imm(i32 %ptr) {
  %r = call i32 @llvm.haydn.sw.brev.imm(i32 %ptr, i32 4)
  ret i32 %r
}

; CHECK-LABEL: name: test_sw_brev_reg
; CHECK: S_SW_BREV_REG
define i32 @test_sw_brev_reg(i32 %ptr, i32 %stride) {
  %r = call i32 @llvm.haydn.sw.brev.reg(i32 %ptr, i32 %stride)
  ret i32 %r
}

;=============================================================================
; Part 8: FFT butterfly pattern — BREV load + BREV store
;=============================================================================

; CHECK-LABEL: name: test_fft_butterfly
; CHECK: D_LDW_BREV_IMM
; CHECK: D_SDW_BREV_IMM
define i32 @test_fft_butterfly(i32 %ptr_in, i32 %ptr_out) {
  %val = call i32 @llvm.haydn.ldw.brev.imm(i32 %ptr_in, i32 4)
  %updated = call i32 @llvm.haydn.sdw.brev.imm(i32 %val, i32 %ptr_out, i32 8)
  ret i32 %updated
}

;=============================================================================
; Part 9: Circular buffer FIR delay line pattern
;=============================================================================

; CHECK-LABEL: name: test_cb_delay_line
; CHECK: D_LDW_CB_IMM
; CHECK: D_SDW_CB_IMM
define i64 @test_cb_delay_line(i32 %ptr, i64 %new_sample) {
  %old_sample = call i64 @llvm.haydn.ldw.cb.imm(i32 %ptr, i32 0, i32 8)
  call void @llvm.haydn.sdw.cb.imm(i64 %new_sample, i32 %ptr, i32 0, i32 8)
  ret i64 %old_sample
}

;Intrinsic declarations

declare i64 @llvm.haydn.ldw.cb.imm(i32, i32, i32)
declare i64 @llvm.haydn.ldw.cb.reg(i32, i32, i32)
declare void @llvm.haydn.sdw.cb.imm(i64, i32, i32, i32)
declare void @llvm.haydn.sdw.cb.reg(i64, i32, i32, i32)

declare i32 @llvm.haydn.ldw.brev.imm(i32, i32)
declare i32 @llvm.haydn.ldw.brev.reg(i32, i32)
declare i32 @llvm.haydn.lw.brev.imm(i32, i32)
declare i32 @llvm.haydn.lw.brev.reg(i32, i32)

declare i32 @llvm.haydn.sdw.brev.imm(i32, i32, i32)
declare i32 @llvm.haydn.sdw.brev.reg(i32, i32, i32)
declare i32 @llvm.haydn.sw.brev.imm(i32, i32)
declare i32 @llvm.haydn.sw.brev.reg(i32, i32)
