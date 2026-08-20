# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=haydn-unknown-elf -filetype=obj %s -o %t.o && \
# RUN:   llvm-objdump -d -z --triple=haydn-unknown-elf %t.o | FileCheck %s --check-prefix=OBJ
# RUN: FileCheck --input-file=%S/../../../lib/Target/Haydn/HaydnInstrInfo.td %s \
# RUN:   --check-prefix=TD
# REQUIRES: haydn-registered-target
#
# REGRESSION TEST: legacy all-zero 16-bit `def NOP : HaydnInst16` with
# `Inst{15 - 0} = 0x0000` is gone. All-zero is not a valid Format E bundle
# (indicator 000). User-facing `nop` remains the logical mnemonic; product
# encoding is Format E (header 0x07). If the 16-bit all-zero def returns,
# a bare `nop` can emit a 2-byte 0x0000 word. If the logical mnemonic
# disappears, the assembler rejects `nop`.
#
# Test design: bare `nop` must assemble to one 12-byte Format E parcel
# whose first byte is not 0x00. TD FileCheck pins the HaydnInst16 residue
# out of HaydnInstrInfo.td. `{ op; nop; nop }` keeps nop as Format E pad.

# CHECK: { nop } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# CHECK-NOT: encoding: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# CHECK: { nop; nop } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# CHECK: { xor32 r0, r0, r0; nop } // encoding: [0x{{[1-9a-f][0-9a-f]|[0-9a-f][1-9a-f]}},{{.*}}]
# OBJ: 07 00 00 00 00 00 00 00 00 00 00 00 {{.*}}{ nop; nop }
# OBJ: 07 00 00 00 00 00 00 00 00 00 00 00 {{.*}}{ nop; nop }
# TD-NOT: def NOP : HaydnInst16
# TD-NOT: Inst{15 - 0} = 0x0000

nop

{ nop; nop }

{ xor32 r0, r0, r0; nop; nop }
