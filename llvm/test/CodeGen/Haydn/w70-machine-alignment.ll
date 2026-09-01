; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs %s -o - 2>/dev/null | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -verify-machineinstrs %s -o - 2>/dev/null | FileCheck %s --check-prefix=ASM0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -filetype=obj %s -o %t.o
; RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SEC
; REQUIRES: haydn-registered-target
;
; Role: run-through — W70.2 function-entry alignment is written by the
; HaydnMachineAlignment pass (addPostBBSections, after the closure
; Finalize+Verify and before the freeze verifier), NOT by AsmPrinter
; label growth. GOALS W70.2: "AIE MachineAlignment seat after first
; Finalize and after S2 closure: pad/elongate with a legal generated idle
; row. Delete printer emitFunctionEntryLabel growth. Prefix budgets
; charge the same pad."
;
; aligned256 body is 4 parcels (48 B); the entry grid is
; lcm(256, 12) = 768, so 60 idle parcels pad the extent to 768 — the same
; lcm bound HaydnMCELFStreamer::emitCodeAlignment walks. Every pad parcel
; is a real committed BUNDLE ({ nop; nop } = E2 row + AllEntriesReal with
; a pad NOP), so getInstSizeInBytes charges it in every distance walk.
; plain() has default Align 4 which divides the parcel size: zero pad.
;
; The AsmPrinter no longer emits ANY alignment directive: alignment is
; real bytes + sh_addralign promotion (serialize-only).

define void @plain() {
  ret void
}

define void @aligned256() align 256 {
  ret void
}

; Default-alignment function takes no idle parcels (extent is a whole
; parcel stream, 12 % 4 == 0).
; ASM-LABEL: plain:
; ASM-NOT: { nop; nop }
; ASM: .cfi_startproc

; ASM-LABEL: aligned256:
; ASM-NOT: .p2align
; ASM-NOT: .balign
; ASM-NOT: .align
; ASM: .cfi_startproc
; 60 idle parcels then the 4-parcel body (spill-KPI and bb comments sit
; between cfi and the first parcel).
; ASM: { nop; nop }
; ASM: { nop; nop }
; ASM: { nop; nop }

; O0 (incl. optnone paths) pads identically — alignment is layout, not
; optimization.
; ASM0-LABEL: aligned256:
; ASM0-NOT: .p2align
; ASM0: .cfi_startproc
; ASM0: { nop; nop }
; ASM0: { nop; nop }

; Object: section alignment promoted (serialize-only; label sits at
; section offset 0 so sh_addralign carries aligned(256) to LLD), and the
; section size is the real byte stream: plain 48 B (4 parcels, unpadded)
; + aligned256 768 B (60 idle + 4 body parcels) = 816 B. No MC fill in
; front of either label.
; SEC: Name: .text
; SEC: Size: 816
; SEC: AddressAlignment: 256
