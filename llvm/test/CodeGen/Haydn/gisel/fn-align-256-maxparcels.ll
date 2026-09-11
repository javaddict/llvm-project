; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj \
; RUN:     -verify-machineinstrs %s -o %t.o
; RUN: llvm-readobj -S --symbols %t.o | FileCheck %s --check-prefix=SEC
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=DIS
; REQUIRES: haydn-registered-target
;
; Role: object — W70.2r pre-label emitCodeAlignment. Function align 256
; must pad with whole 12-byte idle parcels BEFORE the entry label, not a
; short writeNopData fill and not a post-label extent pad. lead is 4
; parcels (48 B); the next lcm(256,12) grid is 768 (offset 0x300).
; Peer: AIETargetELFStreamer.cpp:73-80 (power-of-two Align(16)); Haydn
; overlays Format E EncodedBytes=12.

define void @lead() {
  ret void
}

define void @aligned256() align 256 {
  ret void
}

; SEC: Name: .text
; SEC: Size: 804
; SEC: AddressAlignment: 256
; SEC: Name: aligned256
; SEC-NEXT: Value: 0x300

; DIS-LABEL: <lead>:
; DIS:         {{^[[:space:]]*0:}}
; DIS:         {{^[[:space:]]*30:}} {{.*}}nop
; DIS-LABEL: <aligned256>:
; DIS-NEXT: { {{.*}}xor32
