; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — A.5: llvm.trap must call abort (not only soft-RET).

; A.5: llvm.trap must call abort (not only soft-RET). Epilogue may still exist
; if abort were to return; product abort is noreturn. Historical bug was
; trap → RET with no abort call at all.


define void @t_trap() noreturn {
  call void @llvm.trap()
  unreachable
}

; CHECK-LABEL: t_trap:
; CHECK: lui{{.*}}abort
; CHECK: addi32{{.*}}abort
; CHECK: jalr{{(\.s[012])?}} lr,
; CHECK-NOT: jalr{{(\.s[012])?}} lr,

; CHECK-LABEL: t_ubsantrap:
; CHECK: lui{{.*}}abort
; CHECK: addi32{{.*}}abort
; CHECK: jalr{{(\.s[012])?}} lr,
; CHECK-NOT: jalr{{(\.s[012])?}} lr,
define void @t_ubsantrap() noreturn {
  call void @llvm.ubsantrap(i8 1)
  unreachable
}

declare void @llvm.trap()
declare void @llvm.ubsantrap(i8 immarg)
