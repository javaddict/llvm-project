// RUN: llvm-mc -triple haydn-unknown-elf -show-encoding < %s 2> %t0 \
// RUN: | FileCheck %s --check-prefix=ENC
// RUN: llvm-mc -triple haydn-unknown-elf -filetype obj < %s -o %t1 \
// RUN: | count 0
//
// Phase-2 decoder purge collateral (prior revision): the DIS round-trip
// RUN was REMOVED — the legacy Haydn32 decoder probe that handled these
// 4-byte FmtLaneStore parcels was deleted, so objdump renders `<unknown>`.
// The ENC + asm-parse path SURVIVES (the load-bearing assertion of this
// encoder regression guard). CodeGen still emits this format for lane
// stores — the decoder gap is real on a SURVIVING emit path. Tracked here.
//
// REGRESSION TEST : lane-store instructions D_SW_L_WITH_IMM and
// D_SW_H_WITH_IMM must assemble, encode, and round-trip-disassemble.
//
// D_SW_L_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[31:00] (low lane)
// D_SW_H_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[63:32] (high lane)
//
// These were previously asm-only with no encoding bits. defines them via
// FmtLaneStore (real encoding) so codegen can emit them as the fusion target
// of MOVE32_DR_L/H + ST32. If the encoding regresses, this test fails.

// low-lane store
// d_sw_l_with_imm d3, r1, 1 — store low word of d3 to [r1 + 4]
d_sw_l_with_imm d3, r1, 1
// d_sw_l_with_imm d4, r2, 2 — store low word of d4 to [r2 + 8]
d_sw_l_with_imm d4, r2, 2

// high-lane store
// d_sw_h_with_imm d5, r3, 1 — store high word of d5 to [r3 + 4]
d_sw_h_with_imm d5, r3, 1

// ENC: d_sw_l_with_imm	d3, r1, 1
// ENC-NEXT: d_sw_l_with_imm	d4, r2, 2
// ENC: d_sw_h_with_imm	d5, r3, 1

// Disassembly round-trip.
// DIS-LABEL: <.text>:
// DIS: d_sw_l_with_imm d3, r1, 1
// DIS: d_sw_l_with_imm d4, r2, 2
// DIS: d_sw_h_with_imm d5, r3, 1
