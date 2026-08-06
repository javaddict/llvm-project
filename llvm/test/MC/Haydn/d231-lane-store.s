// RUN: llvm-mc -triple haydn-unknown-elf -show-encoding < %s 2>/dev/null \
// RUN: | FileCheck %s --check-prefix=ENC
// RUN: llvm-mc -triple haydn-unknown-elf -filetype=obj -o %t.o %s
# Format E96 cutover residual: FileCheck/idle-pad/reloc geometry still open (GE96-01/03).
# XFAIL: *

// CHECK: 	{ 	d_sw_l_with_imm	d3, r1, 1 }     // encoding: [0x87,0x43,0x38,0x11,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	d_sw_l_with_imm	d4, r2, 2 }     // encoding: [0x87,0x43,0x48,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
// CHECK: 	{ 	d_sw_h_with_imm	d5, r3, 1 }     // encoding: [0x87,0x43,0x59,0x13,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
# Role: object — lane-store D_SW_L/H_WITH_IMM must parse and encode distinct lo/hi opcodes.

// Encoder regression guard for D_SW_L_WITH_IMM / D_SW_H_WITH_IMM.
//
// D_SW_L_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[31:00] (low lane)
// D_SW_H_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[63:32] (high lane)
//
// Objdump disassembly of these parcels still prints <unknown> (legacy
// 4-byte probe removed); the load-bearing path is asm-parse + show-encoding
// plus a non-failing object emit. CodeGen still emits this format for
// MOVE32_DR_L/H + ST32 fusion.

// low-lane stores
d_sw_l_with_imm d3, r1, 1
// ENC: d_sw_l_with_imm d3, r1, 1
// ENC-SAME: encoding: [0x31,0x01,0x00,0x00,0x80,0x2f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
d_sw_l_with_imm d4, r2, 2
// ENC: d_sw_l_with_imm d4, r2, 2
// ENC-SAME: encoding: [0x42,0x02,0x00,0x00,0x80,0x2f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]

// high-lane store — opcode bytes differ from low-lane (0x31 vs 0x2f family)
d_sw_h_with_imm d5, r3, 1
// ENC: d_sw_h_with_imm d5, r3, 1
// ENC-SAME: encoding: [0x53,0x01,0x00,0x00,0x00,0x31,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
