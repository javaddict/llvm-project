; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -o - < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -filetype=obj -o %t.o < %s && \
; RUN:   llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ
;
; REGRESSION: D_*_CB_IMM members must encode cbr_sel vs imm correctly.
; Desc-only AsmPrinter (FormatsLS CBRI) prints: d_*_cb_imm cbr_sel, rtd, rs, imm
; so ldw(ptr, sel=0, imm=1) dumps as "d_ldw_cb_imm 0, dN, rM, 1" (sel/imm not swapped).
; Before fix: ldw(ptr,0,1) encoded as cbr=1,imm=0.

declare { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr, i32, i32)
declare ptr @llvm.haydn.sdw.cb.imm(i64, ptr, i32, i32)
declare void @llvm.haydn.setcbr.begin(i32, i32)
declare void @llvm.haydn.setcbr.end(i32, i32)

define i64 @cb_ld_sel0_imm1(ptr %base) nounwind {
; CHECK-LABEL: cb_ld_sel0_imm1:
; Formats CBRI print: cbr_sel, rtd, rs, imm
; CHECK: d_ldw_cb_imm 0, {{d[0-9]+}}, {{r[0-9]+}}, 1
; OBJ-LABEL: <cb_ld_sel0_imm1>:
; OBJ: d_ldw_cb_imm 0, {{d[0-9]+}}, {{r[0-9]+}}, 1
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %base, i32 0, i32 1)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

define i64 @cb_ld_sel1_imm2(ptr %base) nounwind {
; CHECK-LABEL: cb_ld_sel1_imm2:
; CHECK: d_ldw_cb_imm 1, {{d[0-9]+}}, {{r[0-9]+}}, 2
; OBJ-LABEL: <cb_ld_sel1_imm2>:
; OBJ: d_ldw_cb_imm 1, {{d[0-9]+}}, {{r[0-9]+}}, 2
  %r_pair = call { i64, ptr } @llvm.haydn.ldw.cb.imm(ptr %base, i32 1, i32 2)
  %r = extractvalue { i64, ptr } %r_pair, 0
  ret i64 %r
}

define void @cb_st_sel0_imm1(i64 %data, ptr %base) nounwind {
; CHECK-LABEL: cb_st_sel0_imm1:
; CHECK: d_sdw_cb_imm 0, {{d[0-9]+}}, {{r[0-9]+}}, 1
; OBJ-LABEL: <cb_st_sel0_imm1>:
; OBJ: d_sdw_cb_imm 0, {{d[0-9]+}}, {{r[0-9]+}}, 1
  call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %base, i32 0, i32 1)
  ret void
}

define void @cb_setup_and_st(i64 %data, ptr %base) nounwind {
; CHECK-LABEL: cb_setup_and_st:
; CHECK: csrw
; CHECK: d_sdw_cb_imm 0, {{d[0-9]+}}, {{r[0-9]+}}, 1
  call void @llvm.haydn.setcbr.begin(i32 0, i32 4096)
  call void @llvm.haydn.setcbr.end(i32 0, i32 4351)
  call ptr @llvm.haydn.sdw.cb.imm(i64 %data, ptr %base, i32 0, i32 1)
  ret void
}
