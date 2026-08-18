; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 \
; RUN:     -verify-machineinstrs -enable-misched=false \
; RUN:     -enable-post-misched=false < %s | FileCheck %s
;
; Role: semantic — NSAZ64 plus i64-return JALR must verify.
; RET rebuilds as JALR_W r0, r15, 0 with implicit-use of the return
; physreg. Implicit $d0 / $r15 never occupy explicit JALR slots.

declare i32 @llvm.haydn.nsaz64(i64)

; CHECK-LABEL: nsaz64_i32:
; CHECK: nsaz64
; CHECK: jalr
define i32 @nsaz64_i32(i64 %a) nounwind {
  %r = call i32 @llvm.haydn.nsaz64(i64 %a)
  ret i32 %r
}

; CHECK-LABEL: ret_i64_jalr:
; CHECK: jalr
define i64 @ret_i64_jalr(i64 %a) nounwind {
  ret i64 %a
}
