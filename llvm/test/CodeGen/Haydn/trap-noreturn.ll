; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — A.5: llvm.trap must call abort (not only soft-RET).

; A.5: llvm.trap must call abort (not only soft-RET). Epilogue may still exist
; if abort were to return; product abort is noreturn. Historical bug was
; trap → RET with no abort call at all.


define void @t_trap() noreturn {
  call void @llvm.trap()
  unreachable
}

; CHECK-LABEL: t_ubsantrap:
; CHECK: jal{{(\.s[012])?}} {{.*}}abort
; CHECK: jal{{(\.s[012])?}} {{.*}}abort
define void @t_ubsantrap() noreturn {
  call void @llvm.ubsantrap(i8 1)
  unreachable
}

declare void @llvm.trap()
declare void @llvm.ubsantrap(i8 immarg)
