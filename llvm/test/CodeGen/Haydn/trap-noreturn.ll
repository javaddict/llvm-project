; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
;
; A.5: llvm.trap must call abort (not only soft-RET). Epilogue may still exist
; if abort were to return; product abort is noreturn. Historical bug was
; trap → RET with no abort call at all.

; CHECK-LABEL: t_trap:
; CHECK: jal{{(_[pP][23][0-9]_[A-Z0-9]+)?}} {{.*}}abort
; CHECK: jal{{(_[pP][23][0-9]_[A-Z0-9]+)?}} {{.*}}abort
define void @t_trap() noreturn {
  call void @llvm.trap()
  unreachable
}

; CHECK-LABEL: t_ubsantrap:
; CHECK: jal{{(_[pP][23][0-9]_[A-Z0-9]+)?}} {{.*}}abort
; CHECK: jal{{(_[pP][23][0-9]_[A-Z0-9]+)?}} {{.*}}abort
define void @t_ubsantrap() noreturn {
  call void @llvm.ubsantrap(i8 1)
  unreachable
}

declare void @llvm.trap()
declare void @llvm.ubsantrap(i8 immarg)
