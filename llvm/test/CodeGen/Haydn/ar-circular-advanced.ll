; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck %s

; Role: MIR — Advanced circular buffer and bit-reversed addressing intrinsics.

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
; Contracts pinned below:
;   loads  -> frexp {data, new_ptr} (two MIR defs: DR64/GPR + GPR writeback)
;   stores -> new_ptr only (one MIR def: GPR writeback)
;   IMM    -> constant cbr_sel / stride appear as bare immediates
;   REG    -> cbr_sel ImmArg remains immediate; stride is a virtual reg
;
; If any intrinsic fails to lower, llc will crash with -global-isel-abort=1.

;=============================================================================
; Part 1: Circular Buffer Load (CBR) — immediate stride
;=============================================================================
; D_LDW_CB_IMM: 64-bit load from circular buffer, post-increment by imm
; Intrinsic: (ptr ptr_base, i32 cbr_sel, i32 stride) -> {i64, ptr}


define i64 @test_ldw_cb_imm(ptr %ptr) {
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

;=============================================================================
; Part 2: Circular Buffer Load (CBR) — register stride
;=============================================================================
; D_LDW_CB_REG: 64-bit load from circular buffer, post-increment by register

; CHECK-LABEL: name: test_ldw_cb_reg
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_REG 1, {{%.*}}, {{%.*}},
define i64 @test_ldw_cb_reg(ptr %ptr, i32 %stride) {
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.cb.reg(ptr %ptr, i32 1, i32 %stride)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

;=============================================================================
; Part 3: Circular Buffer Store (CBR) — immediate / register stride
;=============================================================================
; D_SDW_CB_IMM/REG: returns AGU-updated ptr — side effects keep them live.

; CHECK-LABEL: name: test_sdw_cb_imm
; CHECK: {{%.*}}:gpr32 = D_SDW_CB_IMM 0, {{%.*}}, {{%.*}}, 1,
define ptr @test_sdw_cb_imm(i64 %data, ptr %ptr) {
  %np = call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %ptr, i32 0, i32 1)
  ret ptr %np
}

; CHECK-LABEL: name: test_sdw_cb_reg
; CHECK: {{%.*}}:gpr32 = D_SDW_CB_REG 1, {{%.*}}, {{%.*}}, {{%.*}},
define ptr @test_sdw_cb_reg(i64 %data, ptr %ptr, i32 %stride) {
  %np = call ptr @llvm.haydn.sdw.cb.reg(i64 %data, ptr %ptr, i32 1, i32 %stride)
  ret ptr %np
}

;=============================================================================
; Part 4: Bit-Reversed Load — 64-bit, immediate / register stride
;=============================================================================

; CHECK-LABEL: name: test_ldw_brev_imm
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_BREV_IMM {{%.*}}, 4
define i64 @test_ldw_brev_imm(ptr %ptr) {
  %r = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %ptr, i32 4)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

; CHECK-LABEL: name: test_ldw_brev_reg
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_BREV_REG {{%.*}}, {{%.*}}
define i64 @test_ldw_brev_reg(ptr %ptr, i32 %stride) {
  %r = call { i64, ptr } @llvm.haydn.ldw.brev.reg(ptr %ptr, i32 %stride)
  %d = extractvalue { i64, ptr } %r, 0
  ret i64 %d
}

;=============================================================================
; Part 5: Bit-Reversed Load — 32-bit, immediate / register stride
;=============================================================================

; CHECK-LABEL: name: test_lw_brev_imm
; CHECK: {{%.*}}:gpr32, {{%.*}}:gpr32 = S_LW_BREV_IMM {{%.*}}, 2
define i32 @test_lw_brev_imm(ptr %ptr) {
  %r = call { i32, ptr } @llvm.haydn.lw.brev.imm(ptr %ptr, i32 2)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

; CHECK-LABEL: name: test_lw_brev_reg
; CHECK: {{%.*}}:gpr32, {{%.*}}:gpr32 = S_LW_BREV_REG {{%.*}}, {{%.*}}
define i32 @test_lw_brev_reg(ptr %ptr, i32 %stride) {
  %r = call { i32, ptr } @llvm.haydn.lw.brev.reg(ptr %ptr, i32 %stride)
  %d = extractvalue { i32, ptr } %r, 0
  ret i32 %d
}

;=============================================================================
; Part 6: Bit-Reversed Store — 64-bit, immediate / register stride
;=============================================================================

; CHECK-LABEL: name: test_sdw_brev_imm
; CHECK: {{%.*}}:gpr32 = D_SDW_BREV_IMM {{%.*}}, {{%.*}}, 8
define ptr @test_sdw_brev_imm(i64 %data, ptr %ptr) {
  %r = call ptr @llvm.haydn.sdw.brev.imm(i64 %data, ptr %ptr, i32 8)
  ret ptr %r
}

; CHECK-LABEL: name: test_sdw_brev_reg
; CHECK: {{%.*}}:gpr32 = D_SDW_BREV_REG {{%.*}}, {{%.*}}, {{%.*}}
define ptr @test_sdw_brev_reg(i64 %data, ptr %ptr, i32 %stride) {
  %r = call ptr @llvm.haydn.sdw.brev.reg(i64 %data, ptr %ptr, i32 %stride)
  ret ptr %r
}

;=============================================================================
; Part 7: Bit-Reversed Store — 32-bit, immediate / register stride
;=============================================================================

; CHECK-LABEL: name: test_sw_brev_imm
; CHECK: {{%.*}}:gpr32 = S_SW_BREV_IMM {{%.*}}, {{%.*}}, 4
define ptr @test_sw_brev_imm(i32 %data, ptr %ptr) {
  %r = call ptr @llvm.haydn.sw.brev.imm(i32 %data, ptr %ptr, i32 4)
  ret ptr %r
}

; CHECK-LABEL: name: test_sw_brev_reg
; CHECK: {{%.*}}:gpr32 = S_SW_BREV_REG {{%.*}}, {{%.*}}, {{%.*}}
define ptr @test_sw_brev_reg(i32 %data, ptr %ptr, i32 %stride) {
  %r = call ptr @llvm.haydn.sw.brev.reg(i32 %data, ptr %ptr, i32 %stride)
  ret ptr %r
}

;=============================================================================
; Part 8: FFT butterfly pattern — BREV load + BREV store
;=============================================================================

; CHECK-LABEL: name: test_fft_butterfly
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_BREV_IMM {{%.*}}, 4
; CHECK: {{%.*}}:gpr32 = D_SDW_BREV_IMM {{%.*}}, {{%.*}}, 8
define ptr @test_fft_butterfly(ptr %ptr_in, ptr %ptr_out) {
  %val_pair = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %ptr_in, i32 4)
  %val = extractvalue { i64, ptr } %val_pair, 0
  %updated = call ptr @llvm.haydn.sdw.brev.imm(i64 %val, ptr %ptr_out, i32 8)
  ret ptr %updated
}

;=============================================================================
; Part 9: Circular buffer FIR delay line pattern
;=============================================================================

; CHECK-LABEL: name: test_cb_delay_line
; CHECK: {{%.*}}:dr64, {{%.*}}:gpr32 = D_LDW_CB_IMM {{%.*}}, 0, 1,
; CHECK: {{%.*}}:gpr32 = D_SDW_CB_IMM 0, {{%.*}}, {{%.*}}, 1,
define i64 @test_cb_delay_line(ptr %ptr, i64 %new_sample) {
  %old_sample_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %old_sample = extractvalue { i64, ptr } %old_sample_pair, 0
  %np = call ptr @llvm.haydn.sdw.cb.imm(i64 %new_sample, ptr %ptr, i32 0, i32 1)
  ret i64 %old_sample
}

;=============================================================================
; Part 10: frexp writeback must chain — second op uses new_ptr, not the original
;=============================================================================

; CHECK-LABEL: name: test_ldw_cb_imm_chain
; CHECK: [[BASE:%[0-9]+]]:gpr32 = COPY $r1
; CHECK: {{%[0-9]+}}:dr64, [[WB:%[0-9]+]]:gpr32 = D_LDW_CB_IMM [[BASE]], 0, 1,
; CHECK: {{%[0-9]+}}:dr64, {{%[0-9]+}}:gpr32 = D_LDW_CB_IMM [[WB]], 0, 1,
define i64 @test_ldw_cb_imm_chain(ptr %ptr) {
  %r0 = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %ptr, i32 0, i32 1)
  %p1 = extractvalue { i64, ptr } %r0, 1
  %r1 = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %p1, i32 0, i32 1)
  %d1 = extractvalue { i64, ptr } %r1, 0
  ret i64 %d1
}

; CHECK-LABEL: name: test_sdw_cb_imm_chain
; CHECK: [[BASE:%[0-9]+]]:gpr32 = COPY $r1
; CHECK: [[WB:%[0-9]+]]:gpr32 = D_SDW_CB_IMM 0, {{%.*}}, [[BASE]], 1,
; CHECK: {{%[0-9]+}}:gpr32 = D_SDW_CB_IMM 0, {{%.*}}, [[WB]], 1,
define ptr @test_sdw_cb_imm_chain(i64 %d0, i64 %d1, ptr %ptr) {
  %p1 = call ptr @llvm.haydn.sdw.cb.imm(i64 %d0, ptr %ptr, i32 0, i32 1)
  %p2 = call ptr @llvm.haydn.sdw.cb.imm(i64 %d1, ptr %p1, i32 0, i32 1)
  ret ptr %p2
}

; CHECK-LABEL: name: test_ldw_brev_imm_chain
; CHECK: [[BASE:%[0-9]+]]:gpr32 = COPY $r1
; CHECK: {{%[0-9]+}}:dr64, [[WB:%[0-9]+]]:gpr32 = D_LDW_BREV_IMM [[BASE]], 4
; CHECK: {{%[0-9]+}}:dr64, {{%[0-9]+}}:gpr32 = D_LDW_BREV_IMM [[WB]], 4
define i64 @test_ldw_brev_imm_chain(ptr %ptr) {
  %r0 = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %ptr, i32 4)
  %p1 = extractvalue { i64, ptr } %r0, 1
  %r1 = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %p1, i32 4)
  %d1 = extractvalue { i64, ptr } %r1, 0
  ret i64 %d1
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
