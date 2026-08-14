; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -verify-machineinstrs -filetype=obj \
; RUN:   --force-dwarf-frame-section -o - < %s \
; RUN:   | llvm-dwarfdump -debug-frame - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=FRAME

; Role: object — .cfi_offset is CFA-relative (negative) and CIE/FDE are real.
; Companion: cfi-cfa-object-offset.ll (epilogue CFA restore).

declare void @use(i32)

define void @cfi_test() {
; CHECK-LABEL: cfi_test:
; CHECK:       .cfi_def_cfa_offset [[SS:[0-9]+]]
; Offsets must be negative CFA-relative — never positive SP-relative.
; CHECK:       .cfi_offset {{[a-z0-9]+}}, -
; CHECK:       jal
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

; FRAME: CIE
; FRAME: DW_CFA_def_cfa: R13 +0
; FRAME: FDE cie=
