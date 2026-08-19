# RUN: llvm-mc -triple=haydn-unknown-elf -show-encoding %s | FileCheck %s
# REQUIRES: haydn-registered-target
#
# MemberId packing: first generated member's Mode/EntryIdx, high-entry-first
# TEXT, architectural NOP in every unused entry of that row. E2-only ADDI32
# is a two-entry parcel; E3-only LOG2 is three-entry; dual-mode ADD32 follows
# its first member (E2 e0). Full-bundle NOP idle is ordinary fill. No
# singleton/underfill packet.

# CHECK: { nop; addi32 r1, r2, 1 } // encoding: [0x07,{{.*}}]
{ nop; addi32 r1, r2, 1 }

# CHECK: { nop; add32 r1, r2, r3 } // encoding: [0x07,{{.*}}]
{ nop; add32 r1, r2, r3 }

# CHECK: { nop; nop; log2 r1, r2 } // encoding: [0x4f,{{.*}}]
{ nop; nop; log2 r1, r2 }

# CHECK: { nop; nop } // encoding: [0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
{ nop; nop }

# CHECK: { nop; nop }
{ nop; nop; nop }
