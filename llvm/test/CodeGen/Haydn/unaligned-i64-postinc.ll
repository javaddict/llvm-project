; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 < %s | FileCheck %s
;
; pr57344-3: post-legalizer combiner forms G_HAYDN_POSTINC_LOAD for s64+byte
; after i72 lower. ABI allows align-4 i64, but D_LDW_POST_IMM needs EA%8==0.
; Must not emit d_ldw_post_imm / ld64 for align-4 s64 post-inc.

@s = external global [2 x i8], align 4

; i72-style: load s64 at align 4, then next byte — combiner may post-inc.
define i64 @load_i64_align4_then_byte() {
; CHECK-LABEL: load_i64_align4_then_byte:
; CHECK: // %bb.0:
; CHECK-NOT: d_ldw_post_imm
; CHECK-NOT: {{[[:space:]]}}ld64{{[[:space:]]}}
; CHECK: ld32
; CHECK: ld32
; CHECK: jalr{{.*}}lr
entry:
  %p = getelementptr inbounds i8, ptr @s, i32 12
  %v = load i64, ptr %p, align 4
  ret i64 %v
}

; Explicit post-inc style loop body: load i64 align 4, p += 8.
define i64 @postinc_load_i64_align4(ptr %p) {
; CHECK-LABEL: postinc_load_i64_align4:
; CHECK: // %bb.0:
; CHECK-NOT: d_ldw_post_imm
; CHECK-NOT: {{[[:space:]]}}ld64{{[[:space:]]}}
; CHECK: ld32
; CHECK: ld32
; CHECK: jalr{{.*}}lr
entry:
  %v = load i64, ptr %p, align 4
  %p2 = getelementptr inbounds i8, ptr %p, i32 8
  store ptr %p2, ptr @s, align 4
  ret i64 %v
}

define void @postinc_store_i64_align4(ptr %p, i64 %v) {
; CHECK-LABEL: postinc_store_i64_align4:
; CHECK: // %bb.0:
; CHECK-NOT: d_sdw_post_imm
; CHECK-NOT: {{[[:space:]]}}st64{{[[:space:]]}}
; CHECK: jalr{{.*}}lr
entry:
  store i64 %v, ptr %p, align 4
  %p2 = getelementptr inbounds i8, ptr %p, i32 8
  store ptr %p2, ptr @s, align 4
  ret void
}
