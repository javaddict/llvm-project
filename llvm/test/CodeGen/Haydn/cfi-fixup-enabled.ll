; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -debug-pass=Structure -o /dev/null %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PASS
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; Role: semantic — CFIFixup is product-enabled (Options.EnableCFIFixup).
; Companion: cfi-callee-saves.ll / cfi-cfa-object-offset.ll (CFA-relative
; offsets + epilogue FrameDestroy CFA restore).

; PASS: Insert CFI remember/restore state instructions{{$}}

declare void @use(i32)

define void @cfi_fixup_frame() {
; CHECK-LABEL: cfi_fixup_frame:
; CHECK:       .cfi_def_cfa_offset
; CHECK:       .cfi_offset
; CHECK:       jal
; CHECK:       .cfi_def_cfa {{sp|r13}}, 0
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
