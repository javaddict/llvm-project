; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCELFStreamer.cpp --check-prefix=SRC
; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCAsmInfo.cpp --check-prefix=ASMINFO
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj \
; RUN:     -verify-machineinstrs %s -o %t.o
; RUN: llvm-readobj -S --symbols %t.o | FileCheck %s --check-prefix=SEC
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=DIS
; REQUIRES: haydn-registered-target
;
; Role: object — W70.2r function-entry alignment is pre-label MC fill.
; HasFunctionAlignment=true → AsmPrinter::emitAlignment →
; HaydnMCELFStreamer::emitCodeAlignment walks whole Format E idle parcels
; BEFORE the entry label. lead is 4 parcels (48 B, default Align 4 divides
; 12). aligned256's label is placed at the next 0-mod-lcm(256,12) address
; (768 = 0x300), not at 0x30 with a post-label extent pad. The aligned(256)
; language guarantee holds in shared .text; function sections are optional.
;
; SRC still pins the streamer law (whole-parcel idle walk, lcm bound,
; never MaxBytesToEmit). ASMINFO pins the product HasFunctionAlignment path.
;
; SRC-DAG: emitCodeAlignment
; SRC-DAG: ensureMinAlignment
; SRC-DAG: MaxParcels
; SRC-DAG: gcd
; SRC-DAG: Always walk the lcm bound
; SRC-DAG: do not consult MaxBytesToEmit
; ASMINFO: HasFunctionAlignment = true

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

; lead: 4 parcels, no pad (default Align 4 divides the parcel size).
; Pre-label idle fill 0x30..0x2f4 lives under <lead> (no gap symbol).
; DIS-LABEL: <lead>:
; DIS:         {{^[[:space:]]*0:}}
; DIS:         {{^[[:space:]]*30:}} {{.*}}nop
; aligned256: label at 0x300 (0 mod 256), not 0x30.
; DIS-LABEL: <aligned256>:
; DIS-NEXT: { {{.*}}xor32
