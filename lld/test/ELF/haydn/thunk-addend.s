# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOC %s
# RUN: not ld.lld %t.o -o %t --section-start=.text=0x10000 2>&1 | FileCheck %s
#
# D1.57 fail-closed restamp: the retired R0-borrowing call veneer used to
# honor the relocation addend (callee+16) with 3 × production EncodedBytes
# parcels. No veneer exists now, so the addend-carrying far call is an
# explicit link error naming the veneer ABI gap (D1.57 / ISA-70). The
# addend is still visible in the reloc record (RELOC pin).

# Product call reloc is WIDE under Format E; addend 0x10 preserved.
# RELOC: R_HAYDN_WIDE_CallSImm20 callee 0x10

.section .text
.globl _start
_start:
    jal lr, callee + 16
    .space 0x140000

.globl callee
callee:
    add32 r3, r3, r3
    .space 12
.globl skip_pad
skip_pad:
    add32 r4, r4, r4
    .size callee, .-callee
    .size _start, .-_start

# CHECK: error: {{.*}}.o:({{.*}}relocation R_HAYDN_WIDE_CallSImm20 to '{{.*}}' needs a linker range-extension veneer{{.*}}Haydn veneer ABI is not approved (D1.57 / ISA-70)
# CHECK-NOT: __haydn_thunk
