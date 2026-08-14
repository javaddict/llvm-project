; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -filetype=obj \
; RUN:   -o %t.o < %s
; RUN: llvm-readobj -h %t.o | FileCheck %s --check-prefix=HDR
; RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
;
; Role: object — product ELF e_flags identity on the CodeGen path.
; Pins EF_HAYDN_E96 (0x1) on every llc object and .text parcel geometry
; (EncodedBytes=12). Companion MC pin: MC/Haydn/eflags-e96-product-profile.s.
; Consumer fail-closed for wrong/zero flags: BundleSim test_elf.

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-v64:32-v128:64-a:0:32-n32-S64"
target triple = "haydn-unknown-elf"

define i32 @eflags_codegen(i32 %a, i32 %b) nounwind {
entry:
  %s = add i32 %a, %b
  ret i32 %s
}

; HDR: Flags [ (0x1)
; HDR-NEXT: 0x1

; Product text is whole Format-E parcels (12-byte EncodedBytes).
; SEC: Name: .text
; SEC: Size: {{(12|24|36|48|60|72)}}
