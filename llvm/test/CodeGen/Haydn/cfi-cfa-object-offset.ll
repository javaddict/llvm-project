; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; Role: semantic — .cfi_offset must be CFA-relative (getObjectOffset), never
; new-SP-relative addressing offsets. With .cfi_def_cfa_offset N, a CSR at
; SP+(N-4) has CFA offset -4 (not +(N-4)).
;
; Also: epilogue must emit FrameDestroy .cfi_def_cfa sp, 0 so shrink-wrap /
; multi-exit paths can restore CFA state. Requires (needsFrameMoves).

declare void @use(i32)

define void @cfi_sp_cfa_object_offset() {
; CHECK-LABEL: cfi_sp_cfa_object_offset:
; CHECK:       .cfi_def_cfa_offset [[SS:[0-9]+]]
; Offsets must be negative (or zero) CFA-relative — never positive SP-relative.
; CHECK:       .cfi_offset {{[a-z0-9]+}}, -
; CHECK:       jal
; Epilogue restores CFA to entry SP.
; CHECK:       .cfi_def_cfa {{sp|r13}}, 0
; CHECK:       jalr{{(\.s[012])?}} r0, lr, 0
entry:
  call void @use(i32 1)
  call void @use(i32 2)
  call void @use(i32 3)
  call void @use(i32 4)
  call void @use(i32 5)
  call void @use(i32 6)
  call void @use(i32 7)
  call void @use(i32 8)
  ret void
}
