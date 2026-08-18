; RUN: rm -rf %t && split-file %s %t
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/slli32-33.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SLLI32-OOB
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/slli32-5.ll \
; RUN:     | FileCheck %s --check-prefix=SLLI32-OK
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/slli64-65.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SLLI64-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/sincos-16.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=SINCOS-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/arctan-16.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=ARCTAN-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/cb-ld-128.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=CBLD-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/cb-st-128.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=CBST-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/x2slli32-33.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=X2SIMD-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/x4slli16-16.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=X4SIMD-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/x2srai32r-33.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=X2R-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/x4srai16r-16.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=X4R-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/ldw-brev-33.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=BREV-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/ldw-post-33.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=POST-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/ldw-with-33.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=WITH-OOB
; RUN: not llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -o - %t/cb-ld-sel2.ll 2>&1 \
; RUN:     | FileCheck %s --check-prefix=CBSEL-OOB
;
; REGRESSION TEST: intrinsic ImmArg arms must fail-closed on out-of-range
; immediates. MC N-bit-truncates otherwise (silent wrong code).
;
; Bug (GOALS W52 / PA2-B7): GPR32 RI5 shifts, DR64 RI6 shifts, SIN_COS/
; ARCTAN uimm4, and CB IMM stride (product member simm8) emitted the raw
; constant with no range check. Sibling X4SELI16 already rejected ImmVal>15.
; Fix: one expectUImm/expectSImm helper; out of range returns false.
;
; Extension (same bug class, closed by the same mechanism): X2/X4 SIMD imm
; shifts (RI5/RI4 members), X2SRAI32R/X4SRAI16R, BREV imm stride (RI6
; simm6), POST/PRE/WITH imm offsets (RI6 simm6), and CB cbr_sel (uimm2
; field; addCircularBufferUse only models CBR0/CBR1). All widths are the
; generated Format E member field, not the loose logical-stub td operand.
;
; Test design: llvm.haydn.slli32(..., 33) must not encode as slli32 ..., 1
; (33 & 31). cannot-select is the correct failure. In-range 5 still encodes
; as 5 so the arm is not deleted. Other listed arms use the first illegal
; value of their product field (uimm6 65, uimm4 16, simm8 128, simm6 33,
; cbr_sel 2).

;--- slli32-33.ll
declare i32 @llvm.haydn.slli32(i32, i32)
define i32 @slli32_amt33(i32 %a) {
  ; SLLI32-OOB: {{cannot select|Cannot select|unable to select}}
  ; SLLI32-OOB-NOT: slli32 {{.*}}, 1
  %r = call i32 @llvm.haydn.slli32(i32 %a, i32 33)
  ret i32 %r
}

;--- slli32-5.ll
declare i32 @llvm.haydn.slli32(i32, i32)
define i32 @slli32_amt5(i32 %a) {
  ; SLLI32-OK-LABEL: slli32_amt5:
  ; SLLI32-OK: slli32 {{r[0-9]+}}, {{r[0-9]+}}, 5
  ; SLLI32-OK-NOT: slli32 {{.*}}, 1
  %r = call i32 @llvm.haydn.slli32(i32 %a, i32 5)
  ret i32 %r
}

;--- slli64-65.ll
declare i64 @llvm.haydn.slli64(i64, i32)
define i64 @slli64_amt65(i64 %a) {
  ; SLLI64-OOB: {{cannot select|Cannot select|unable to select}}
  ; SLLI64-OOB-NOT: slli64 {{.*}}, 1
  %r = call i64 @llvm.haydn.slli64(i64 %a, i32 65)
  ret i64 %r
}

;--- sincos-16.ll
declare i64 @llvm.haydn.sin.cos(i32, i32)
define i64 @sin_cos_imm16(i32 %phase) {
  ; SINCOS-OOB: {{cannot select|Cannot select|unable to select}}
  %r = call i64 @llvm.haydn.sin.cos(i32 %phase, i32 16)
  ret i64 %r
}

;--- arctan-16.ll
declare i32 @llvm.haydn.arctan(i64, i32)
define i32 @arctan_imm16(i64 %xy) {
  ; ARCTAN-OOB: {{cannot select|Cannot select|unable to select}}
  %r = call i32 @llvm.haydn.arctan(i64 %xy, i32 16)
  ret i32 %r
}

;--- cb-ld-128.ll
declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
define i64 @cb_ld_stride128(ptr %base) {
  ; CBLD-OOB: {{cannot select|Cannot select|unable to select}}
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %base, i32 0, i32 128)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

;--- cb-st-128.ll
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)
define void @cb_st_stride128(i64 %data, ptr %base) {
  ; CBST-OOB: {{cannot select|Cannot select|unable to select}}
  call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %base, i32 0, i32 128)
  ret void
}

;--- x2slli32-33.ll
; X2 lanes are 32-bit: generated members X2SLLI32_*_RI5 (uimm5). 33 wraps.
declare <2 x i32> @llvm.haydn.x2slli32(<2 x i32>, i32)
define <2 x i32> @x2slli_amt33(<2 x i32> %a) {
  ; X2SIMD-OOB: {{cannot select|Cannot select|unable to select}}
  %r = call <2 x i32> @llvm.haydn.x2slli32(<2 x i32> %a, i32 33)
  ret <2 x i32> %r
}

;--- x4slli16-16.ll
; X4 lanes are 16-bit: generated members X4SLLI16_*_RI4 (uimm4). 16 wraps.
declare <4 x i16> @llvm.haydn.x4slli16(<4 x i16>, i32)
define <4 x i16> @x4slli_amt16(<4 x i16> %a) {
  ; X4SIMD-OOB: {{cannot select|Cannot select|unable to select}}
  %r = call <4 x i16> @llvm.haydn.x4slli16(<4 x i16> %a, i32 16)
  ret <4 x i16> %r
}

;--- x2srai32r-33.ll
; Rounding SIMD shift: generated members X2SRAI32R_*_RI5 (uimm5).
declare <2 x i32> @llvm.haydn.x2srai32r(<2 x i32>, i32)
define <2 x i32> @x2srai32r_amt33(<2 x i32> %a) {
  ; X2R-OOB: {{cannot select|Cannot select|unable to select}}
  %r = call <2 x i32> @llvm.haydn.x2srai32r(<2 x i32> %a, i32 33)
  ret <2 x i32> %r
}

;--- x4srai16r-16.ll
; Rounding SIMD shift, 16-bit lanes: X4SRAI16R_*_RI4 (uimm4).
declare <4 x i16> @llvm.haydn.x4srai16r(<4 x i16>, i32)
define <4 x i16> @x4srai16r_amt16(<4 x i16> %a) {
  ; X4R-OOB: {{cannot select|Cannot select|unable to select}}
  %r = call <4 x i16> @llvm.haydn.x4srai16r(<4 x i16> %a, i32 16)
  ret <4 x i16> %r
}

;--- ldw-brev-33.ll
; BREV stride: generated members D_LDW_BREV_IMM_*_RI6 (simm6). 33 wraps.
declare { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr, i32)
define i64 @ldw_brev_stride33(ptr %base) {
  ; BREV-OOB: {{cannot select|Cannot select|unable to select}}
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.brev.imm(ptr %base, i32 33)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

;--- ldw-post-33.ll
; POST/PRE offset: generated members D_LDW_POST_IMM_*_RI6 (simm6).
declare { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr, i32)
define i64 @ldw_post_off33(ptr %base) {
  ; POST-OOB: {{cannot select|Cannot select|unable to select}}
  %r_pair = call { i64, ptr } @llvm.haydn.d.ldw.post.imm(ptr %base, i32 33)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

;--- ldw-with-33.ll
; WITH offset: generated members D_LDW_WITH_IMM_*_RI6 (simm6).
declare i64 @llvm.haydn.d.ldw.with.imm(ptr, i32)
define i64 @ldw_with_off33(ptr %base) {
  ; WITH-OOB: {{cannot select|Cannot select|unable to select}}
  %r = call i64 @llvm.haydn.d.ldw.with.imm(ptr %base, i32 33)
  ret i64 %r
}

;--- cb-ld-sel2.ll
; cbr_sel: uimm2 field but only CBR0/CBR1 are modeled
; (addCircularBufferUse). 2 selects a nonexistent CBR.
declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
define i64 @cb_ld_sel2(ptr %base) {
  ; CBSEL-OOB: {{cannot select|Cannot select|unable to select}}
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %base, i32 2, i32 8)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}
