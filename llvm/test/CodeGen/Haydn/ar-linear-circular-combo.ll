; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck %s
;
; REGRESSION TEST: Combined linear and circular addressing patterns.
;
; Tests that CB/BREV addressing can coexist with regular GPR-based
; addressing in the same function. Verifies selection under pressure when
; circular buffer and bit-reversed addressing share the register file with
; normal loads/stores.
;
; Intrinsic names use the canonical dotted form; IMM variants take constant
; cbr_sel / stride (see circular-buffer-intrinsics.ll).
;
; If any intrinsic fails to lower, llc will crash with -global-isel-abort=1.

;=============================================================================
; Part 1: Mix circular buffer load with normal load
;=============================================================================

; CHECK-LABEL: name: test_cb_and_linear_load
; CHECK: D_LDW_CB_IMM
define i64 @test_cb_and_linear_load(i32 %cb_ptr, ptr %linear_ptr) {
  %cb_val_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %cb_ptr, i32 0, i32 8)
  %cb_val = extractvalue { i64, i32 } %cb_val_pair, 0
  %lin_val = load i64, ptr %linear_ptr
  %result = add i64 %cb_val, %lin_val
  ret i64 %result
}

;=============================================================================
; Part 2: Mix circular buffer store with normal store
;=============================================================================

; CHECK-LABEL: name: test_cb_and_linear_store
; CHECK: D_SDW_CB_IMM
define void @test_cb_and_linear_store(i64 %data, i32 %cb_ptr, ptr %linear_ptr) {
  call i32 @llvm.haydn.sdw.cb.imm(i64 %data, i32 %cb_ptr, i32 0, i32 8)
  store i64 %data, ptr %linear_ptr
  ret void
}

;=============================================================================
; Part 3: Bit-reversed load combined with circular buffer load
;=============================================================================

; CHECK-LABEL: name: test_brev_and_cb_load
; CHECK: D_LDW_BREV_IMM
; CHECK: D_LDW_CB_IMM
define i64 @test_brev_and_cb_load(i32 %brev_ptr, i32 %cb_ptr) {
  %brev_val = call i32 @llvm.haydn.ldw.brev.imm(i32 %brev_ptr, i32 4)
  %cb_val_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %cb_ptr, i32 0, i32 8)
  %cb_val = extractvalue { i64, i32 } %cb_val_pair, 0
  %ext = zext i32 %brev_val to i64
  %result = add i64 %cb_val, %ext
  ret i64 %result
}

;=============================================================================
; Part 4: Multiple CB operations with different CBR selectors
;=============================================================================

; CHECK-LABEL: name: test_dual_cb_channels
; CHECK: D_LDW_CB_IMM
; CHECK: D_LDW_CB_IMM
define i64 @test_dual_cb_channels(i32 %ptr0, i32 %ptr1) {
  %val0_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %ptr0, i32 0, i32 8)
  %val0 = extractvalue { i64, i32 } %val0_pair, 0
  %val1_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %ptr1, i32 1, i32 8)
  %val1 = extractvalue { i64, i32 } %val1_pair, 0
  %result = add i64 %val0, %val1
  ret i64 %result
}

;=============================================================================
; Part 5: CB load-process-store pipeline
;=============================================================================

; CHECK-LABEL: name: test_cb_load_compute_store
; CHECK: D_LDW_CB_IMM
; CHECK: D_SDW_CB_IMM
define i64 @test_cb_load_compute_store(i32 %ptr, i64 %coeff) {
  %sample_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %ptr, i32 0, i32 8)
  %sample = extractvalue { i64, i32 } %sample_pair, 0
  %result = mul i64 %sample, %coeff
  call i32 @llvm.haydn.sdw.cb.imm(i64 %result, i32 %ptr, i32 0, i32 8)
  ret i64 %result
}

;=============================================================================
; Part 6: Bit-reversed load-store with different widths
;=============================================================================

; CHECK-LABEL: name: test_brev_mixed_width
; CHECK: S_LW_BREV_IMM
; CHECK: D_LDW_BREV_IMM
define i32 @test_brev_mixed_width(i32 %ptr32, i32 %ptr64) {
  %val32 = call i32 @llvm.haydn.lw.brev.imm(i32 %ptr32, i32 2)
  %val64_ext = call i32 @llvm.haydn.ldw.brev.imm(i32 %ptr64, i32 4)
  %result = add i32 %val32, %val64_ext
  ret i32 %result
}

;=============================================================================
; Part 7: Register-pressure stress test
;=============================================================================

; CHECK-LABEL: name: test_register_pressure
; CHECK: D_LDW_CB_IMM
; CHECK: D_LDW_BREV_IMM
define i64 @test_register_pressure(i32 %ptr_cb, i32 %ptr_brev, i64 %a, i64 %b, i64 %c, i64 %d) {
  %cb_val_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %ptr_cb, i32 0, i32 8)
  %cb_val = extractvalue { i64, i32 } %cb_val_pair, 0
  %brev_ext = call i32 @llvm.haydn.ldw.brev.imm(i32 %ptr_brev, i32 4)
  %brev_val = zext i32 %brev_ext to i64
  %sum1 = add i64 %a, %b
  %sum2 = add i64 %c, %d
  %sum3 = add i64 %cb_val, %brev_val
  %result = add i64 %sum1, %sum2
  %final = add i64 %result, %sum3
  ret i64 %final
}

;=============================================================================
; Part 8: CB register stride variant combined with CB immediate stride
;=============================================================================

; CHECK-LABEL: name: test_cb_mixed_stride
; CHECK: D_LDW_CB_IMM
; CHECK: D_LDW_CB_REG
define i64 @test_cb_mixed_stride(i32 %ptr, i32 %reg_stride) {
  %val1_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %ptr, i32 0, i32 8)
  %val1 = extractvalue { i64, i32 } %val1_pair, 0
  %val2_pair = call { i64, i32 } @llvm.haydn.ldw.cb.reg(i32 %ptr, i32 1, i32 %reg_stride)
  %val2 = extractvalue { i64, i32 } %val2_pair, 0
  %result = add i64 %val1, %val2
  ret i64 %result
}

;Intrinsic declarations

declare { i64, i32 } @llvm.haydn.ldw.cb.imm(i32, i32, i32)
declare { i64, i32 } @llvm.haydn.ldw.cb.reg(i32, i32, i32)
declare i32 @llvm.haydn.sdw.cb.imm(i64, i32, i32, i32)
declare i32 @llvm.haydn.sdw.cb.reg(i64, i32, i32, i32)

declare i32 @llvm.haydn.ldw.brev.imm(i32, i32)
declare i32 @llvm.haydn.ldw.brev.reg(i32, i32)
declare i32 @llvm.haydn.lw.brev.imm(i32, i32)
declare i32 @llvm.haydn.lw.brev.reg(i32, i32)
declare i32 @llvm.haydn.sdw.brev.imm(i32, i32, i32)
declare i32 @llvm.haydn.sdw.brev.reg(i32, i32, i32)
declare i32 @llvm.haydn.sw.brev.imm(i32, i32)
declare i32 @llvm.haydn.sw.brev.reg(i32, i32)
