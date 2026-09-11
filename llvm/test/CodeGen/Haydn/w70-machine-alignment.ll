; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -verify-machineinstrs %s -o - 2>/dev/null | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O0 -verify-machineinstrs %s -o - 2>/dev/null | FileCheck %s --check-prefix=ASM0
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -O2 -filetype=obj %s -o %t.o
; RUN: llvm-readobj -S --symbols %t.o | FileCheck %s --check-prefix=SEC
; RUN: llvm-objdump -d -z --no-show-raw-insn --triple=haydn-unknown-elf %t.o | \
; RUN:   FileCheck %s --check-prefix=DIS
; RUN: llc -mtriple=haydn-unknown-elf -global-isel-abort=1 -function-sections \
; RUN:     -filetype=obj %s -o %t.fs.o
; RUN: llvm-readobj -S --symbols %t.fs.o | FileCheck %s --check-prefix=FS
; RUN: ld.lld -m elf32haydn -e 0 %t.o -o %t.elf --section-start=.text=0x10000
; RUN: llvm-readobj --symbols %t.elf | FileCheck %s --check-prefix=LINK
; RUN: ld.lld -m elf32haydn -e 0 %t.fs.o -o %t.fs.elf
; RUN: llvm-readobj --symbols %t.fs.elf | FileCheck %s --check-prefix=FSLINK
; REQUIRES: haydn-registered-target
;
; Role: run-through — W70.2r function-entry alignment is pre-label MC fill
; (HasFunctionAlignment=true → emitCodeAlignment), NOT a post-label
; HaydnMachineAlignment extent pad. Idle parcels sit between functions in
; shared .text; they are not committed BUNDLEs inside aligned256.
; Function sections remain optional: each over-aligned function may sit at
; section offset 0 with sh_addralign, but shared .text must also satisfy
; value % N == 0.
;
; plain() has default Align 4 which divides the parcel size: zero pad.
; After plain's 4 parcels (48 B) the next lcm(256,12) grid is 768, so
; aligned256's label is at 0x300. Section size 48 + 720 + 48 = 816.

define void @plain() {
  ret void
}

define void @aligned256() align 256 {
  ret void
}

; Default-alignment function takes no idle parcels (extent is a whole
; parcel stream, 12 % 4 == 0). Generic header still emits .p2align 2.
; ASM-LABEL: plain:
; ASM: .cfi_startproc
; ASM-NOT: { nop; nop }
; ASM: xor32

; Pre-label fill is a .p2align directive, not in-function idle BUNDLEs.
; Idle parcels after .cfi_startproc would be the deleted post-label pad.
; ASM: .p2align 8
; ASM-LABEL: aligned256:
; ASM: .cfi_startproc
; ASM-NOT: { nop; nop }
; ASM: xor32

; O0 (incl. optnone paths) aligns identically — alignment is layout, not
; optimization.
; ASM0: .p2align 8
; ASM0-LABEL: aligned256:
; ASM0: .cfi_startproc
; ASM0-NOT: { nop; nop }
; ASM0: xor32

; Shared .text: section alignment 256, size 816, aligned256 at 0x300.
; SEC: Name: .text
; SEC: Size: 804
; SEC: AddressAlignment: 256
; SEC: Name: aligned256
; SEC-NEXT: Value: 0x300

; Pre-label idle sits under the previous symbol (objdump has no gap symbol).
; DIS-LABEL: <plain>:
; DIS:         {{^[[:space:]]*0:}}
; DIS:         {{^[[:space:]]*30:}} {{.*}}nop
; DIS-LABEL: <aligned256>:
; objdump prints "00000300 <aligned256>:" then the insn line.
; DIS-NEXT must not match the "300" inside the label address.
; DIS-NEXT: { {{.*}}xor32

; Optional function-sections containment: aligned256 at offset 0 of its
; own section, which carries AddressAlignment 256.
; FS: Name: .text.aligned256
; FS: AddressAlignment: 256
; FS: Name: aligned256
; FS-NEXT: Value: 0x0

; Linked shared .text at a 256-aligned VMA: st_value 0x10000+0x300 is 0 mod 256.
; LINK: Name: aligned256
; LINK-NEXT: Value: 0x10300

; Linked function-sections: sh_addralign 256 forces the section VMA, so the
; symbol at offset 0 is 0 mod 256 (hex value ends in 00).
; FSLINK: Name: aligned256
; 0x11200 is 0 mod 256; do not anchor `$` — readobj pads a trailing space.
; FSLINK-NEXT: Value: 0x{{[0-9a-fA-F]*00}}
