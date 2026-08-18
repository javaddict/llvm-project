; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCELFStreamer.cpp --check-prefix=SRC
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj \
; RUN:     -verify-machineinstrs %s -o %t.o
; RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=DIS
; REQUIRES: haydn-registered-target
;
; Role: object — 38bd405 emitCodeAlignment MaxParcels pin.
; Function align 256 pads with whole 12-byte idle parcels. After 8
; parcels (96 B) the next 256-grid is 768 (offset 0x300). AsmPrinter
; defaults MaxBytesToEmit=Alignment (256 → 21 parcels) and used to
; stop at 288. Bound is Align/gcd(12,Align)=64; do not consult
; MaxBytesToEmit. Peer: AIETargetELFStreamer.cpp:73-81
; emitCodeAlignment(Align(16)); Haydn overlays EncodedBytes=12.
;
; SRC-DAG: emitCodeAlignment
; SRC-DAG: ensureMinAlignment
; SRC-DAG: MaxParcels
; SRC-DAG: gcd
; SRC-DAG: Always walk the lcm bound
; SRC-DAG: do not consult MaxBytesToEmit

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
