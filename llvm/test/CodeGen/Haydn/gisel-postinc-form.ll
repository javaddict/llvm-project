; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -haydn-enable-gisel-update-addr=0 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=GISEL-OFF

; Role: MIR — GISel form is the sole product AGU form home (default ON).

; GISel form is the sole product AGU form home (default ON).
;
; Explicit GISel form off at ISel → plain LD + ADDI.
;
; Product GISel form: G_LOAD/ZEXTLOAD/SEXTLOAD/STORE + G_PTR_ADD → fused AGU.

; ISEL-LABEL: name: postinc_stream_i32
; ISEL: S_LW_POST_IMM
; GISEL-OFF-LABEL: name: postinc_stream_i32
; GISEL-OFF-DAG: LD32
; GISEL-OFF-DAG: ADDI32
; ASM-LABEL: postinc_stream_i32:
; ASM: s_lw_post_imm

define i32 @postinc_stream_i32(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i32 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %ld = load i32, ptr %bp, align 4
  %s2 = add i32 %s, %ld
  %bp2 = getelementptr i8, ptr %bp, i32 4
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s2, %loop ]
  ret i32 %r
}

; ISEL-LABEL: name: postdec_stream_i32
; ISEL: S_LW_POST_IMM
; ASM-LABEL: postdec_stream_i32:
; ASM: s_lw_post_imm
define i32 @postdec_stream_i32(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i32 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %ld = load i32, ptr %bp, align 4
  %s2 = add i32 %s, %ld
  %bp2 = getelementptr i8, ptr %bp, i32 -4
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s2, %loop ]
  ret i32 %r
}

; ISEL-LABEL: name: preinc_stream_i32
; Prefer PRE if IR is ++p then load; accept POST if LSR rewrites shape.
; ISEL-DAG: {{S_LW_PRE_IMM|S_LW_POST_IMM}}
; ASM-LABEL: preinc_stream_i32:
; ASM-DAG: {{s_lw_pre_imm|s_lw_post_imm}}
define i32 @preinc_stream_i32(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i32 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %bp2 = getelementptr i8, ptr %bp, i32 4
  %ld = load i32, ptr %bp2, align 4
  %s2 = add i32 %s, %ld
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s2, %loop ]
  ret i32 %r
}

; ISEL-LABEL: name: postinc_stream_i64
; ISEL: D_LDW_POST_IMM
; ASM-LABEL: postinc_stream_i64:
; ASM: d_ldw_post_imm
define i64 @postinc_stream_i64(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i64 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %ld = load i64, ptr %bp, align 8
  %s2 = add i64 %s, %ld
  %bp2 = getelementptr i8, ptr %bp, i32 8
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i64 [ 0, %entry ], [ %s2, %loop ]
  ret i64 %r
}

; ISEL-LABEL: name: postinc_store_i32
; ISEL: {{ST32_POST|S_SW_POST_IMM}}
; ASM-LABEL: postinc_store_i32:
; ST32_POST member after leaveRegion setDesc — catalog print s_sw_post_imm.
; ASM: s_sw_post_imm
define void @postinc_store_i32(ptr %p, i32 %n, i32 %v) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  store i32 %v, ptr %bp, align 4
  %bp2 = getelementptr i8, ptr %bp, i32 4
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; Byte stream: LSR may rewrite to index IV + AGU PRE_REG (not ptr POST_IMM).
; Residual FormUpdateAddr covers LDU8+ADDI when that shape appears (gaps.mir).
; ISEL-LABEL: name: postinc_stream_i8
; ISEL-DAG: {{S_LBU_POST_IMM|S_LBU_PRE_IMM|S_LBU_POST_REG|S_LBU_PRE_REG|S_LBS_POST_IMM|S_LBS_PRE_IMM|S_LBS_POST_REG|S_LBS_PRE_REG|LDU8|LD8}}
; ASM-LABEL: postinc_stream_i8:
define i32 @postinc_stream_i8(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i32 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %ld = load i8, ptr %bp, align 1
  %z = zext i8 %ld to i32
  %s2 = add i32 %s, %z
  %bp2 = getelementptr i8, ptr %bp, i32 1
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s2, %loop ]
  ret i32 %r
}

; Halfword stream: same LSR caveat as i8; form path covered by gaps.mir.
; ISEL-LABEL: name: postinc_stream_i16
; ISEL-DAG: {{S_LHWU_POST_IMM|S_LHWU_PRE_IMM|S_LHWU_POST_REG|S_LHWU_PRE_REG|S_LHWS_POST_IMM|S_LHWS_PRE_IMM|S_LHWS_POST_REG|S_LHWS_PRE_REG|LDU16|LD16}}
; ASM-LABEL: postinc_stream_i16:
define i32 @postinc_stream_i16(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i32 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %ld = load i16, ptr %bp, align 2
  %z = zext i16 %ld to i32
  %s2 = add i32 %s, %z
  %bp2 = getelementptr i8, ptr %bp, i32 2
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s2, %loop ]
  ret i32 %r
}

; Signed byte stream (G_SEXTLOAD fuse → S_LBS_*). LSR may rewrite to PRE_REG.
; ISEL-LABEL: name: postinc_stream_i8_sext
; ISEL-DAG: {{S_LBS_POST_IMM|S_LBS_PRE_IMM|S_LBS_POST_REG|S_LBS_PRE_REG|LD8}}
; ASM-LABEL: postinc_stream_i8_sext:
define i32 @postinc_stream_i8_sext(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i32 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %ld = load i8, ptr %bp, align 1
  %x = sext i8 %ld to i32
  %s2 = add i32 %s, %x
  %bp2 = getelementptr i8, ptr %bp, i32 1
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s2, %loop ]
  ret i32 %r
}

; Signed halfword stream (G_SEXTLOAD fuse → S_LHWS_*).
; ISEL-LABEL: name: postinc_stream_i16_sext
; ISEL-DAG: {{S_LHWS_POST_IMM|S_LHWS_PRE_IMM|S_LHWS_POST_REG|S_LHWS_PRE_REG|LD16}}
; ASM-LABEL: postinc_stream_i16_sext:
define i32 @postinc_stream_i16_sext(ptr %p, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i32 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %ld = load i16, ptr %bp, align 2
  %x = sext i16 %ld to i32
  %s2 = add i32 %s, %x
  %bp2 = getelementptr i8, ptr %bp, i32 2
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i32 [ 0, %entry ], [ %s2, %loop ]
  ret i32 %r
}

; Reg-stride post-inc (variable step). May remain split if LSR hides ADD32.
; ISEL-LABEL: name: postinc_reg_stride_i64
; ISEL-DAG: {{D_LDW_POST_REG|D_LDW_POST_IMM|LD64}}
define i64 @postinc_reg_stride_i64(ptr %p, i32 %n, i32 %step) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %s = phi i64 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i2, %loop ]
  %bp = phi ptr [ %p, %entry ], [ %bp2, %loop ]
  %ld = load i64, ptr %bp, align 8
  %s2 = add i64 %s, %ld
  %bp2 = getelementptr i8, ptr %bp, i32 %step
  %i2 = add i32 %i, 1
  %c = icmp ult i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = phi i64 [ 0, %entry ], [ %s2, %loop ]
  ret i64 %r
}

; GEP+load one-shot: the GEP result dies at the load, so the dead-writeback
; PRE fusion is refused (deadDefHasNoUse hazard) and selection folds the
; byte offset into the plain load — LD32 with offset 4.
; ISEL-LABEL: name: gep_then_load_is_pre
; ISEL-DAG: LD32 {{.*}}, 1
; ASM-LABEL: gep_then_load_is_pre:
; ASM-DAG: ld32 {{.*}}, 1
define i32 @gep_then_load_is_pre(ptr %p) {
  %q = getelementptr i8, ptr %p, i32 4
  %v = load i32, ptr %q, align 4
  ret i32 %v
}

; Load with EA offset + different pointer update stride: leave unfused
; (non-zero EA with separate update is not a pure post/pre form).
; Covered by form-update-addr-gaps.mir nonzero_offset_no_fuse.
