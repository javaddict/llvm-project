; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -o - < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -filetype=obj -o %t.o < %s && \
; RUN:   llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ
;
; REGRESSION: D_*_CB_IMM Flex peers must share logical MCInst order
; (rs, cbr_sel, imm) so encodeBundle128 setOpcode materialize does not swap
; encoded cbr_sel vs imm. Before fix: ldw(ptr,0,1) encoded as cbr=1,imm=0.
;
; Disasm (Formats/BundleSim print order): d_*_cb_imm cbr_sel, rtd, rs, imm

declare { i64, i32 } @llvm.haydn.ldw.cb.imm(i32, i32, i32)
declare i32 @llvm.haydn.sdw.cb.imm(i64, i32, i32, i32)
declare void @llvm.haydn.setcbr.begin(i32, i32)
declare void @llvm.haydn.setcbr.end(i32, i32)

define i64 @cb_ld_sel0_imm1(i32 %base) nounwind {
; CHECK-LABEL: cb_ld_sel0_imm1:
; Logical asm (Auto.td): rtd, cbr_sel, rs, imm
; CHECK: d_ldw_cb_imm{{.*}}, 0, {{.*}}, 1
; OBJ-LABEL: <cb_ld_sel0_imm1>:
; OBJ: d_ldw_cb_imm 0, {{d[0-9]+}}, {{r[0-9]+}}, 1
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %base, i32 0, i32 1)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}

define i64 @cb_ld_sel1_imm2(i32 %base) nounwind {
; CHECK-LABEL: cb_ld_sel1_imm2:
; CHECK: d_ldw_cb_imm{{.*}}, 1, {{.*}}, 2
; OBJ-LABEL: <cb_ld_sel1_imm2>:
; OBJ: d_ldw_cb_imm 1, {{d[0-9]+}}, {{r[0-9]+}}, 2
  %r_pair = call { i64, i32 } @llvm.haydn.ldw.cb.imm(i32 %base, i32 1, i32 2)
  %r = extractvalue { i64, i32 } %r_pair, 0
  ret i64 %r
}

define void @cb_st_sel0_imm1(i64 %data, i32 %base) nounwind {
; CHECK-LABEL: cb_st_sel0_imm1:
; CHECK: d_sdw_cb_imm{{.*}}0{{.*}}, 1
; OBJ-LABEL: <cb_st_sel0_imm1>:
; OBJ: d_sdw_cb_imm 0, {{d[0-9]+}}, {{r[0-9]+}}, 1
  call i32 @llvm.haydn.sdw.cb.imm(i64 %data, i32 %base, i32 0, i32 1)
  ret void
}

define void @cb_setup_and_st(i64 %data, i32 %base) nounwind {
; CHECK-LABEL: cb_setup_and_st:
; CHECK: csrw
; CHECK: d_sdw_cb_imm{{.*}}0{{.*}}, 1
  call void @llvm.haydn.setcbr.begin(i32 0, i32 4096)
  call void @llvm.haydn.setcbr.end(i32 0, i32 4351)
  call i32 @llvm.haydn.sdw.cb.imm(i64 %data, i32 %base, i32 0, i32 1)
  ret void
}
