# REQUIRES: haydn
# RUN: llvm-mc -filetype=obj -triple=haydn-unknown-elf %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck --check-prefix=RELOCS %s
# RUN: ld.lld %t.o -o %t --section-start=.text=0x10000
# RUN: llvm-objdump -d --triple=haydn-unknown-elf %t | FileCheck %s
#
# REGRESSION: Format E BEQ/BNE/BEQZ/BLT patch byte PC+imm (ValueShift=0)
# at the product FieldLsb. Linked branches must disassemble with non-zero
# targets. Parcel stride is EncodedBytes=12 (addresses advance 0xc).

# Same-section labels are resolved by MC (no reloc) for near targets.
# RELOCS: Relocations [

.section .text
.globl _start
_start:
    # CHECK-LABEL: <_start>:
    # Forward BEQ over two Format E parcels (24 B).
    # CHECK: 10000: {{.*}} beq{{.*}}r0,{{.*}}r1,
    BEQ R0, R1, forward_target

    # CHECK: 1000c: {{.*}} add32
    ADD32 R2, R2, R2
    # CHECK: 10018: {{.*}} add32
    ADD32 R3, R3, R3

forward_target:
    # CHECK-LABEL: <forward_target>:
    # CHECK: {{.*}} bne
    BNE R4, R5, _start

    # CHECK: {{.*}} beqz
    BEQZ R6, zero_target

    ADD32 R7, R7, R7

zero_target:
    # CHECK-LABEL: <zero_target>:
    # CHECK: {{.*}} blt
    BLT R8, R9, end

    ADD32 R10, R10, R10

end:
    # CHECK-LABEL: <end>:
    # CHECK: {{.*}} add32
    ADD32 R0, R0, R0

    .size _start, .-_start
