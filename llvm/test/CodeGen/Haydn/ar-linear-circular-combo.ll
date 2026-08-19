; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck %s

; Role: MIR — Combined linear and circular addressing patterns.

; REGRESSION TEST: Combined linear and circular addressing patterns.
;
; Tests that CB/BREV addressing can coexist with regular GPR-based
; addressing in the same function. Verifies selection under pressure when
; circular buffer and bit-reversed addressing share the register file with
; normal loads/stores.
;
; Intrinsic names use the canonical dotted form; IMM variants take constant
; cbr_sel / stride (see circular-buffer-intrinsics.ll). CHECKs pin frexp
; two-def loads, single-def store writeback, and ImmArg cbr_sel values.
;
; If any intrinsic fails to lower, llc will crash with -global-isel-abort=1.

;=============================================================================
; Part 1: Mix circular buffer load with normal load
;=============================================================================


define i64 @test_cb_and_linear_load(ptr %cb_ptr, ptr %linear_ptr) {
  %cb_val_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %cb_ptr, i32 0, i32 1)
  %cb_val = extractvalue { i64, ptr } %cb_val_pair, 0
  %lin_val = load i64, ptr %linear_ptr
  %result = add i64 %cb_val, %lin_val
  ret i64 %result
}

;=============================================================================
; Part 2: Mix circular buffer store with normal store
;=============================================================================

; CHECK-LABEL: name: test_cb_and_linear_store
; CHECK: {{%.*}}:gpr32 = D_SDW_CB_IMM 0, {{%.*}}, {{%.*}}, 1,
define void @test_cb_and_linear_store(i64 %data, ptr %cb_ptr, ptr %linear_ptr) {
  %np = call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %cb_ptr, i32 0, i32 1)
  store i64 %data, ptr %linear_ptr
  ret void
}

;=============================================================================
; Part 3: Bit-reversed load combined with circular buffer load
;=============================================================================

; CHECK-LABEL: name: test_brev_and_cb_load
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_BREV_IMM {{%.*}}, 4
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_IMM {{%.*}}, 0, 1,
define i64 @test_brev_and_cb_load(ptr %brev_ptr, ptr %cb_ptr) {
  %brev_pair = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %brev_ptr, i32 4)
  %brev_val = extractvalue { i64, ptr } %brev_pair, 0
  %cb_val_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %cb_ptr, i32 0, i32 1)
  %cb_val = extractvalue { i64, ptr } %cb_val_pair, 0
  %result = add i64 %cb_val, %brev_val
  ret i64 %result
}

;=============================================================================
; Part 4: Multiple CB operations with different CBR selectors
;=============================================================================

; CHECK-LABEL: name: test_dual_cb_channels
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_IMM {{%.*}}, 0, 1,
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_IMM {{%.*}}, 1, 1,
define i64 @test_dual_cb_channels(ptr %ptr0, ptr %ptr1) {
  %val0_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr0, i32 0, i32 1)
  %val0 = extractvalue { i64, ptr } %val0_pair, 0
  %val1_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr1, i32 1, i32 1)
  %val1 = extractvalue { i64, ptr } %val1_pair, 0
  %result = add i64 %val0, %val1
  ret i64 %result
}

;=============================================================================
; Part 5: CB load-process-store pipeline
;=============================================================================

; CHECK-LABEL: name: test_cb_load_compute_store
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_IMM {{%.*}}, 0, 1,
; CHECK: {{%.*}}:gpr32 = D_SDW_CB_IMM 0, {{%.*}}, {{%.*}}, 1,
define i64 @test_cb_load_compute_store(ptr %ptr, i64 %coeff) {
  %sample_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %sample = extractvalue { i64, ptr } %sample_pair, 0
  %result = mul i64 %sample, %coeff
  %np = call ptr @llvm.haydn.sdw.cb.imm(i64 %result, ptr %ptr, i32 0, i32 1)
  ret i64 %result
}

;=============================================================================
; Part 6: Bit-reversed load-store with different widths
;=============================================================================

; CHECK-LABEL: name: test_brev_mixed_width
; CHECK: {{%.*}}:gpr32, {{%.*}}:gpr32 = S_LW_BREV_IMM {{%.*}}, 2
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_BREV_IMM {{%.*}}, 4
define i32 @test_brev_mixed_width(ptr %ptr32, ptr %ptr64) {
  %val32_pair = call { i32, ptr } @llvm.haydn.lw.brev.imm(ptr %ptr32, i32 2)
  %val32 = extractvalue { i32, ptr } %val32_pair, 0
  %val64_pair = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %ptr64, i32 4)
  %val64 = extractvalue { i64, ptr } %val64_pair, 0
  %val64_lo = trunc i64 %val64 to i32
  %result = add i32 %val32, %val64_lo
  ret i32 %result
}

;=============================================================================
; Part 7: Register-pressure stress test
;=============================================================================

; CHECK-LABEL: name: test_register_pressure
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_IMM {{%.*}}, 0, 1,
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_BREV_IMM {{%.*}}, 4
define i64 @test_register_pressure(ptr %ptr_cb, ptr %ptr_brev, i64 %a, i64 %b, i64 %c, i64 %d) {
  %cb_val_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr_cb, i32 0, i32 1)
  %cb_val = extractvalue { i64, ptr } %cb_val_pair, 0
  %brev_pair = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %ptr_brev, i32 4)
  %brev_val = extractvalue { i64, ptr } %brev_pair, 0
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
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_IMM {{%.*}}, 0, 1,
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_REG 1, {{%.*}}, {{%.*}},
define i64 @test_cb_mixed_stride(ptr %ptr, i32 %reg_stride) {
  %val1_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %val1 = extractvalue { i64, ptr } %val1_pair, 0
  %val2_pair = call { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr %ptr, i32 1, i32 %reg_stride)
  %val2 = extractvalue { i64, ptr } %val2_pair, 0
  %result = add i64 %val1, %val2
  ret i64 %result
}

;=============================================================================
; Part 9: frexp writeback from CB load feeds a linear store (consume new_ptr)
;=============================================================================

; CHECK-LABEL: name: test_cb_wb_to_linear_store
; CHECK: [[BASE:%[0-9]+]]:gpr32 = COPY
; CHECK: {{%[0-9]+}}:dr64, [[WB:%[0-9]+]]:gpr32 = D_LDW_CB_IMM [[BASE]], 0, 1,
; CHECK: ST{{.*}}[[WB]]
define i64 @test_cb_wb_to_linear_store(ptr %cb_ptr, i64 %marker) {
  %pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %cb_ptr, i32 0, i32 1)
  %data = extractvalue { i64, ptr } %pair, 0
  %wb = extractvalue { i64, ptr } %pair, 1
  store i64 %marker, ptr %wb
  ret i64 %data
}

;Intrinsic declarations

declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.reg(i64, ptr, i32, i32)

declare { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr, i32)
declare { i64, ptr } @llvm.haydn.ldw.brev.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.lw.brev.reg(ptr, i32)
declare ptr @llvm.haydn.sdw.brev.imm(i64, ptr, i32)
declare ptr @llvm.haydn.sdw.brev.reg(i64, ptr, i32)
declare ptr @llvm.haydn.sw.brev.imm(i32, ptr, i32)
declare ptr @llvm.haydn.sw.brev.reg(i32, ptr, i32)
