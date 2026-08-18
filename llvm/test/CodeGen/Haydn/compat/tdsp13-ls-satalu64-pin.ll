; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:   -filetype=asm %s -o - | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 \
; RUN:   -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJ
;
; T-DSP13 declared-vs-tested pin for the LS pre/post-inc residual plus
; saturating ALU64 high/low halves. Names the never-lit matrix cells
; without a 746-name harness. Empty object output fails.

declare { i64, ptr } @llvm.haydn.d.ldw.pre.reg(ptr, i32)
declare { i64, ptr } @llvm.haydn.d.lhw.post.imm(ptr, i32)
declare { i64, ptr } @llvm.haydn.d.lhw.post.reg(ptr, i32)
declare { i64, ptr } @llvm.haydn.d.lhw.pre.imm(ptr, i32)
declare { i64, ptr } @llvm.haydn.d.lw.post.reg(ptr, i32)
declare { i64, ptr } @llvm.haydn.d.lw.pre.imm(ptr, i32)
declare { i64, ptr } @llvm.haydn.d.lw.pre.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lbs.post.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lbu.post.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lbu.post.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lbu.pre.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lhws.post.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lhws.post.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lhws.pre.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lhws.pre.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lhwu.post.imm(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lhwu.post.reg(ptr, i32)
declare { i32, ptr } @llvm.haydn.s.lhwu.pre.imm(ptr, i32)
declare i64 @llvm.haydn.add64s.h(i64, i64)
declare i64 @llvm.haydn.add64s.l(i64, i64)
declare i64 @llvm.haydn.sub64s.h(i64, i64)
declare i64 @llvm.haydn.sub64s.l(i64, i64)

define i64 @pin_d_ldw_pre_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_d_ldw_pre_reg:
; CHECK: d_ldw_pre_reg
; OBJ-LABEL: <pin_d_ldw_pre_reg>:
; OBJ: d_ldw_pre_reg
  %p = call { i64, ptr } @llvm.haydn.d.ldw.pre.reg(ptr %base, i32 %off)
  %d = extractvalue { i64, ptr } %p, 0
  ret i64 %d
}

define i64 @pin_d_lhw_post_imm(ptr %base) {
; CHECK-LABEL: pin_d_lhw_post_imm:
; CHECK: d_lhw_post_imm
; OBJ-LABEL: <pin_d_lhw_post_imm>:
; OBJ: d_lhw_post_imm
  %p = call { i64, ptr } @llvm.haydn.d.lhw.post.imm(ptr %base, i32 2)
  %d = extractvalue { i64, ptr } %p, 0
  ret i64 %d
}

define i64 @pin_d_lhw_post_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_d_lhw_post_reg:
; CHECK: d_lhw_post_reg
; OBJ-LABEL: <pin_d_lhw_post_reg>:
; OBJ: d_lhw_post_reg
  %p = call { i64, ptr } @llvm.haydn.d.lhw.post.reg(ptr %base, i32 %off)
  %d = extractvalue { i64, ptr } %p, 0
  ret i64 %d
}

define i64 @pin_d_lhw_pre_imm(ptr %base) {
; CHECK-LABEL: pin_d_lhw_pre_imm:
; CHECK: d_lhw_pre_imm
; OBJ-LABEL: <pin_d_lhw_pre_imm>:
; OBJ: d_lhw_pre_imm
  %p = call { i64, ptr } @llvm.haydn.d.lhw.pre.imm(ptr %base, i32 2)
  %d = extractvalue { i64, ptr } %p, 0
  ret i64 %d
}

define i64 @pin_d_lw_post_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_d_lw_post_reg:
; CHECK: d_lw_post_reg
; OBJ-LABEL: <pin_d_lw_post_reg>:
; OBJ: d_lw_post_reg
  %p = call { i64, ptr } @llvm.haydn.d.lw.post.reg(ptr %base, i32 %off)
  %d = extractvalue { i64, ptr } %p, 0
  ret i64 %d
}

define i64 @pin_d_lw_pre_imm(ptr %base) {
; CHECK-LABEL: pin_d_lw_pre_imm:
; CHECK: d_lw_pre_imm
; OBJ-LABEL: <pin_d_lw_pre_imm>:
; OBJ: d_lw_pre_imm
  %p = call { i64, ptr } @llvm.haydn.d.lw.pre.imm(ptr %base, i32 1)
  %d = extractvalue { i64, ptr } %p, 0
  ret i64 %d
}

define i64 @pin_d_lw_pre_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_d_lw_pre_reg:
; CHECK: d_lw_pre_reg
; OBJ-LABEL: <pin_d_lw_pre_reg>:
; OBJ: d_lw_pre_reg
  %p = call { i64, ptr } @llvm.haydn.d.lw.pre.reg(ptr %base, i32 %off)
  %d = extractvalue { i64, ptr } %p, 0
  ret i64 %d
}

define i32 @pin_s_lbs_post_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_s_lbs_post_reg:
; CHECK: s_lbs_post_reg
; OBJ-LABEL: <pin_s_lbs_post_reg>:
; OBJ: s_lbs_post_reg
  %p = call { i32, ptr } @llvm.haydn.s.lbs.post.reg(ptr %base, i32 %off)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lbu_post_imm(ptr %base) {
; CHECK-LABEL: pin_s_lbu_post_imm:
; CHECK: s_lbu_post_imm
; OBJ-LABEL: <pin_s_lbu_post_imm>:
; OBJ: s_lbu_post_imm
  %p = call { i32, ptr } @llvm.haydn.s.lbu.post.imm(ptr %base, i32 1)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lbu_post_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_s_lbu_post_reg:
; CHECK: s_lbu_post_reg
; OBJ-LABEL: <pin_s_lbu_post_reg>:
; OBJ: s_lbu_post_reg
  %p = call { i32, ptr } @llvm.haydn.s.lbu.post.reg(ptr %base, i32 %off)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lbu_pre_imm(ptr %base) {
; CHECK-LABEL: pin_s_lbu_pre_imm:
; CHECK: s_lbu_pre_imm
; OBJ-LABEL: <pin_s_lbu_pre_imm>:
; OBJ: s_lbu_pre_imm
  %p = call { i32, ptr } @llvm.haydn.s.lbu.pre.imm(ptr %base, i32 1)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lhws_post_imm(ptr %base) {
; CHECK-LABEL: pin_s_lhws_post_imm:
; CHECK: s_lhws_post_imm
; OBJ-LABEL: <pin_s_lhws_post_imm>:
; OBJ: s_lhws_post_imm
  %p = call { i32, ptr } @llvm.haydn.s.lhws.post.imm(ptr %base, i32 2)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lhws_post_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_s_lhws_post_reg:
; CHECK: s_lhws_post_reg
; OBJ-LABEL: <pin_s_lhws_post_reg>:
; OBJ: s_lhws_post_reg
  %p = call { i32, ptr } @llvm.haydn.s.lhws.post.reg(ptr %base, i32 %off)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lhws_pre_imm(ptr %base) {
; CHECK-LABEL: pin_s_lhws_pre_imm:
; CHECK: s_lhws_pre_imm
; OBJ-LABEL: <pin_s_lhws_pre_imm>:
; OBJ: s_lhws_pre_imm
  %p = call { i32, ptr } @llvm.haydn.s.lhws.pre.imm(ptr %base, i32 2)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lhws_pre_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_s_lhws_pre_reg:
; CHECK: s_lhws_pre_reg
; OBJ-LABEL: <pin_s_lhws_pre_reg>:
; OBJ: s_lhws_pre_reg
  %p = call { i32, ptr } @llvm.haydn.s.lhws.pre.reg(ptr %base, i32 %off)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lhwu_post_imm(ptr %base) {
; CHECK-LABEL: pin_s_lhwu_post_imm:
; CHECK: s_lhwu_post_imm
; OBJ-LABEL: <pin_s_lhwu_post_imm>:
; OBJ: s_lhwu_post_imm
  %p = call { i32, ptr } @llvm.haydn.s.lhwu.post.imm(ptr %base, i32 2)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lhwu_post_reg(ptr %base, i32 %off) {
; CHECK-LABEL: pin_s_lhwu_post_reg:
; CHECK: s_lhwu_post_reg
; OBJ-LABEL: <pin_s_lhwu_post_reg>:
; OBJ: s_lhwu_post_reg
  %p = call { i32, ptr } @llvm.haydn.s.lhwu.post.reg(ptr %base, i32 %off)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i32 @pin_s_lhwu_pre_imm(ptr %base) {
; CHECK-LABEL: pin_s_lhwu_pre_imm:
; CHECK: s_lhwu_pre_imm
; OBJ-LABEL: <pin_s_lhwu_pre_imm>:
; OBJ: s_lhwu_pre_imm
  %p = call { i32, ptr } @llvm.haydn.s.lhwu.pre.imm(ptr %base, i32 2)
  %d = extractvalue { i32, ptr } %p, 0
  ret i32 %d
}

define i64 @pin_add64s_h(i64 %a, i64 %b) {
; CHECK-LABEL: pin_add64s_h:
; CHECK: add64s_h
; OBJ-LABEL: <pin_add64s_h>:
; OBJ: add64s_h
  %r = call i64 @llvm.haydn.add64s.h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @pin_add64s_l(i64 %a, i64 %b) {
; CHECK-LABEL: pin_add64s_l:
; CHECK: add64s_l
; OBJ-LABEL: <pin_add64s_l>:
; OBJ: add64s_l
  %r = call i64 @llvm.haydn.add64s.l(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @pin_sub64s_h(i64 %a, i64 %b) {
; CHECK-LABEL: pin_sub64s_h:
; CHECK: sub64s_h
; OBJ-LABEL: <pin_sub64s_h>:
; OBJ: sub64s_h
  %r = call i64 @llvm.haydn.sub64s.h(i64 %a, i64 %b)
  ret i64 %r
}

define i64 @pin_sub64s_l(i64 %a, i64 %b) {
; CHECK-LABEL: pin_sub64s_l:
; CHECK: sub64s_l
; OBJ-LABEL: <pin_sub64s_l>:
; OBJ: sub64s_l
  %r = call i64 @llvm.haydn.sub64s.l(i64 %a, i64 %b)
  ret i64 %r
}
