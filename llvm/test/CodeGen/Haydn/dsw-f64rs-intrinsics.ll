; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -stop-after=instruction-select -verify-machineinstrs -o - < %s | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -o - < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj -o %t.o < %s

; Role: semantic + object — ISA-65 D_SW_F64RS fused round-sat-store selection.
;
; REGRESSION TEST: the four golden D_SW_F64RS_* logical defs were `[]`-pattern
; (MC-only; nothing selected them), so the FIR/IIR drain pattern
; (a + 2^15) >> 16 clamp-store always lowered to a 6-instruction chain.
; This test pins intrinsic → logical D_SW_F64RS_* MI selection for all four
; variants. If a selector arm is lost, llc fails with cannot-select
; (-global-isel-abort=1) at instruction-select.
;
; Semantics (golden, LOADSTORE0):
;   mem32[rs] = SATQ1.31(({rtd[63],rtd} + 2^15) >>> 16)
;   POST: rs_wb = rs + imm6<<2 (IMM) / rs + rs2 (REG); returns updated base.
;   WITH: base+offset addressing, no writeback, void return.
; Immediate is the simm6 *element index* (stride bytes >> 2), same scaled_imm
; convention as the d_sw_h/d_sw_l POST/WITH family.

declare ptr @llvm.haydn.d.sw.f64rs.post.imm(i64, ptr, i32)
declare ptr @llvm.haydn.d.sw.f64rs.post.reg(i64, ptr, i32)
declare void @llvm.haydn.d.sw.f64rs.with.imm(i64, ptr, i32)
declare void @llvm.haydn.d.sw.f64rs.with.reg(i64, ptr, i32)

; MIR-LABEL: name: test_d_sw_f64rs_post_imm
; MIR: {{%[0-9]+}}:gpr32 = D_SW_F64RS_POST_IMM {{%[0-9]+}}, {{%[0-9]+}}, 1
define ptr @test_d_sw_f64rs_post_imm(i64 %data, ptr %base) {
  %r = call ptr @llvm.haydn.d.sw.f64rs.post.imm(i64 %data, ptr %base, i32 1)
  ret ptr %r
}

; MIR-LABEL: name: test_d_sw_f64rs_post_reg
; MIR: {{%[0-9]+}}:gpr32 = D_SW_F64RS_POST_REG {{%[0-9]+}}, {{%[0-9]+}}, {{%[0-9]+}}
define ptr @test_d_sw_f64rs_post_reg(i64 %data, ptr %base, i32 %off) {
  %r = call ptr @llvm.haydn.d.sw.f64rs.post.reg(i64 %data, ptr %base, i32 %off)
  ret ptr %r
}

; MIR-LABEL: name: test_d_sw_f64rs_with_imm
; MIR: D_SW_F64RS_WITH_IMM {{%[0-9]+}}, {{%[0-9]+}}, 2
define void @test_d_sw_f64rs_with_imm(i64 %data, ptr %base) {
  call void @llvm.haydn.d.sw.f64rs.with.imm(i64 %data, ptr %base, i32 2)
  ret void
}

; MIR-LABEL: name: test_d_sw_f64rs_with_reg
; MIR: D_SW_F64RS_WITH_REG {{%[0-9]+}}, {{%[0-9]+}}, {{%[0-9]+}}
define void @test_d_sw_f64rs_with_reg(i64 %data, ptr %base, i32 %off) {
  call void @llvm.haydn.d.sw.f64rs.with.reg(i64 %data, ptr %base, i32 %off)
  ret void
}

; Writeback chain: second POST store consumes the returned updated base —
; pins the single-ret AGU writeback SSA model (same as D_SDW_BREV_REG chain).
; MIR-LABEL: name: test_d_sw_f64rs_post_imm_chain
; MIR: [[WB:%[0-9]+]]:gpr32 = D_SW_F64RS_POST_IMM {{%[0-9]+}}, {{%[0-9]+}}, 1
; MIR: {{%[0-9]+}}:gpr32 = D_SW_F64RS_POST_IMM {{%[0-9]+}}, [[WB]], 1
define ptr @test_d_sw_f64rs_post_imm_chain(i64 %d0, i64 %d1, ptr %base) {
  %p1 = call ptr @llvm.haydn.d.sw.f64rs.post.imm(i64 %d0, ptr %base, i32 1)
  %p2 = call ptr @llvm.haydn.d.sw.f64rs.post.imm(i64 %d1, ptr %p1, i32 1)
  ret ptr %p2
}

; ASM-LABEL: test_d_sw_f64rs_post_imm:
; ASM: d_sw_f64rs_post_imm
; ASM-LABEL: test_d_sw_f64rs_post_reg:
; ASM: d_sw_f64rs_post_reg
; ASM-LABEL: test_d_sw_f64rs_with_imm:
; ASM: d_sw_f64rs_with_imm
; ASM-LABEL: test_d_sw_f64rs_with_reg:
; ASM: d_sw_f64rs_with_reg
; ASM-LABEL: test_d_sw_f64rs_post_imm_chain:
; ASM: d_sw_f64rs_post_imm
; ASM: d_sw_f64rs_post_imm
