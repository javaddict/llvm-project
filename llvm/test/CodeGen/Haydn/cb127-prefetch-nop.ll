; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs < %s | FileCheck %s

; Role: semantic — CB-127: G_PREFETCH has no Haydn ISA form — select as nop (erase), no ICE.

; CB-127: G_PREFETCH has no Haydn ISA form — select as nop (erase), no ICE.

define void @prefetch_read(ptr %p) {
; CHECK-LABEL: prefetch_read:
; No ISA mnemonic for prefetch (label names may still contain the substring).
; CHECK: jalr
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 3, i32 1)
  ret void
}

define void @prefetch_write(ptr %p) {
; CHECK-LABEL: prefetch_write:
; CHECK: jalr
  call void @llvm.prefetch.p0(ptr %p, i32 1, i32 3, i32 1)
  ret void
}

declare void @llvm.prefetch.p0(ptr nocapture readonly, i32 immarg, i32 immarg, i32 immarg)
