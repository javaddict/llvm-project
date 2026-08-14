; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -stop-after=instruction-select -verify-machineinstrs < %s \
; RUN:     | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 \
; RUN:     -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM

; Role: MIR — product ISel post-inc form owns fused AGU + MMO transfer.
; Leftover *_POST_INC residual expand is expand-postinc-early-mmo.mir
; (haydn-expand-pseudos).

; ISEL-LABEL: name: postinc_i32_mmo
; ISEL: S_LW_POST_IMM{{.*}}load
; ISEL-NOT: LD32_POST_INC
; ASM-LABEL: postinc_i32_mmo:
; ASM: s_lw_post_imm
define i32 @postinc_i32_mmo(ptr %p) {
  %v = load i32, ptr %p, align 4
  %q = getelementptr i8, ptr %p, i32 4
  store ptr %q, ptr @sink_ptr
  ret i32 %v
}

@sink_ptr = global ptr null

; ISEL-LABEL: name: postinc_i64_mmo
; ISEL: D_LDW_POST_IMM{{.*}}load
; ISEL-NOT: LD64_POST_INC
; ASM-LABEL: postinc_i64_mmo:
; ASM: d_ldw_post_imm
define i64 @postinc_i64_mmo(ptr %p) {
  %v = load i64, ptr %p, align 8
  %q = getelementptr i8, ptr %p, i32 8
  store ptr %q, ptr @sink_ptr
  ret i64 %v
}

; ISEL-LABEL: name: postinc_store_i32_mmo
; ISEL: {{ST32_POST|S_SW_POST_IMM}}{{.*}}store
; ISEL-NOT: ST32_POST_INC
; ASM-LABEL: postinc_store_i32_mmo:
; ASM: s_sw_post_imm
define void @postinc_store_i32_mmo(ptr %p, i32 %v) {
  store i32 %v, ptr %p, align 4
  %q = getelementptr i8, ptr %p, i32 4
  store ptr %q, ptr @sink_ptr
  ret void
}

; Under-aligned s64: ISel split (not D_LDW fused) must keep load MMOs.
; ISEL-LABEL: name: postinc_i64_split_mmo
; ISEL-DAG: LD32{{.*}}load
; ISEL-NOT: D_LDW_POST_IMM
; ISEL-NOT: LD64_POST_INC
define i64 @postinc_i64_split_mmo(ptr %p) {
  %v = load i64, ptr %p, align 4
  %q = getelementptr i8, ptr %p, i32 8
  store ptr %q, ptr @sink_ptr
  ret i64 %v
}
