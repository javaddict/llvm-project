; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj \
; RUN:     -verify-machineinstrs %s -o %t.o
; RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=DIS
; REQUIRES: haydn-registered-target
;
; Role: object — function align 256 must pad with whole 12-byte idle
; parcels, not a short writeNopData fill. After 8 parcels (96 B) the next
; 256-grid is 768 (offset 0x300). A 16-parcel guard stopped at 288.
; Peer: AIETargetELFStreamer.cpp:73-80 (power-of-two Align(16)); Haydn
; overlays Format E EncodedBytes=12.

define void @lead() {
  ret void
}

define void @aligned256() align 256 {
  ret void
}

; SEC: Name: .text
; SEC: AddressAlignment: 256

; DIS-LABEL: <lead>:
; DIS:         0:
; DIS-LABEL: <aligned256>:
; DIS:       300:
