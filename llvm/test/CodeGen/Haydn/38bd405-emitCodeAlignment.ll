; RUN: FileCheck %s --input-file=%S/../../../lib/Target/Haydn/MCTargetDesc/HaydnMCELFStreamer.cpp --check-prefix=SRC
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -filetype=obj \
; RUN:     -verify-machineinstrs %s -o %t.o
; RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=DIS
; REQUIRES: haydn-registered-target
;
; Role: object — W70.2 function-entry alignment is real committed bytes.
; Before W70.2 the AsmPrinter grew the entry label (emitCodeAlignment MC
; fill): aligned256's label was placed at 0x300 by 56 idle parcels emitted
; BEFORE the label. W70.2 moved the writer to HaydnMachineAlignment
; (addPostBBSections, post-Kind-B): lead sits unpadded at 0..0x30
; (4 parcels, default Align 4 divides 12), aligned256's label follows at
; 0x30 and its OWN extent is padded with 60 committed idle parcels
; (lcm(256,12)=768 grid) to 0x330. The aligned(256) language guarantee
; rides the promoted sh_addralign (serialize-only printer residue): the
; product -ffunction-sections link puts every entry at section offset 0,
; so LLD honors it. Single-.text mid-section labels are the documented
; W70.2 residual (GOALS W70.2: printer growth is deleted; the pad is the
; mechanism).
;
; SRC still pins the streamer law (whole-parcel idle walk, lcm bound,
; never MaxBytesToEmit): that path remains the authority for hand-assembly
; .p2align fills (e96-text-align-256-maxparcels.s) even though the
; compiler no longer emits directives.
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

; lead: 4 parcels, no pad (default Align 4 divides the parcel size).
; DIS-LABEL: <lead>:
; DIS:         0:
; aligned256: label at 0x30 (lead extent, no MC fill), then 60 committed
; idle parcels to its 768-byte extent grid (rows 0x30..0x324).
; DIS-LABEL: <aligned256>:
; DIS:        30:
; DIS:        3c:
; DIS:       324:
