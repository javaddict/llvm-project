; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs  -enable-misched=false -enable-post-misched=false < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs \
; RUN:   -stop-after=instruction-select < %s | FileCheck %s --check-prefix=MIR

; Role: semantic — DR64 load/store BREV register variants with frexp/store model.

; REGRESSION: DR64 load/store BREV register variants with frexp/store model.
;   D_SDW_BREV_REG: (i64 data, ptr, stride) -> new_ptr
;   S_SW_BREV_REG:  (i32 data, ptr, stride) -> new_ptr
;
; Pointer base + returned writeback pointer (not i32 integer addresses).
; REG stride is a GPR; writeback is a single GPR def (not frexp data pair).

declare ptr @llvm.haydn.sdw.brev.reg(i64, ptr, i32)
declare ptr @llvm.haydn.sw.brev.reg(i32, ptr, i32)

define dso_local ptr @test_sdw_brev_reg(i64 %data, ptr %ptr, i32 %stride) {
; CHECK-LABEL: test_sdw_brev_reg:
; CHECK: d_sdw_brev_reg {{d[0-9]+}}, {{r[0-9]+}}, {{r[0-9]+}}
; MIR-LABEL: name: test_sdw_brev_reg
; MIR: {{%[0-9]+}}:gpr32 = D_SDW_BREV_REG {{%[0-9]+}}, {{%[0-9]+}}, {{%[0-9]+}}
  %r = call ptr @llvm.haydn.sdw.brev.reg(i64 %data, ptr %ptr, i32 %stride)
  ret ptr %r
}

define dso_local ptr @test_sw_brev_reg(i32 %data, ptr %ptr, i32 %stride) {
; CHECK-LABEL: test_sw_brev_reg:
; CHECK: s_sw_brev_reg {{r[0-9]+}}, {{r[0-9]+}}, {{r[0-9]+}}
; MIR-LABEL: name: test_sw_brev_reg
; MIR: {{%[0-9]+}}:gpr32 = S_SW_BREV_REG {{%[0-9]+}}, {{%[0-9]+}}, {{%[0-9]+}}
  %r = call ptr @llvm.haydn.sw.brev.reg(i32 %data, ptr %ptr, i32 %stride)
  ret ptr %r
}

; Store writeback chain: second BREV store consumes returned new_ptr.
define dso_local ptr @test_sdw_brev_reg_chain(i64 %d0, i64 %d1, ptr %ptr, i32 %stride) {
; CHECK-LABEL: test_sdw_brev_reg_chain:
; CHECK: d_sdw_brev_reg
; CHECK: d_sdw_brev_reg
; MIR-LABEL: name: test_sdw_brev_reg_chain
; MIR: [[BASE:%[0-9]+]]:gpr32 = COPY
; MIR: [[WB:%[0-9]+]]:gpr32 = D_SDW_BREV_REG {{%.*}}, [[BASE]], {{%.*}}
; MIR: {{%[0-9]+}}:gpr32 = D_SDW_BREV_REG {{%.*}}, [[WB]], {{%.*}}
  %p1 = call ptr @llvm.haydn.sdw.brev.reg(i64 %d0, ptr %ptr, i32 %stride)
  %p2 = call ptr @llvm.haydn.sdw.brev.reg(i64 %d1, ptr %p1, i32 %stride)
  ret ptr %p2
}
