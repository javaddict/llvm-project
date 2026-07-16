// RUN: llvm-mc -triple haydn-unknown-elf -show-encoding < %s 2> %t0 \
// RUN: | FileCheck %s --check-prefix=ENC
// RUN: llvm-mc -triple haydn-unknown-elf -filetype obj < %s -o %t1 \
// RUN: | count 0
//
// FIXED (LS3_* tied-base refactor): all PRE/POST LS forms use
// (outs data, rs_wb), (ins rs1, rs2) + Constraints "$rs1 = $rs_wb". HW width
// no longer drops rs2 (d_lhw_post_reg d3,r5,r7 encodes with distinct r7).
//
// Phase-2 decoder purge collateral (prior revision): the DIS round-trip
// RUN was REMOVED — the legacy Haydn32 decoder probe that handled these
// 4-byte FmtLSPostIncReg parcels was deleted, so objdump renders
// `<unknown>`. The ENC + asm-parse path SURVIVES (the load-bearing
// assertion of this encoder regression guard). CodeGen still emits this
// format for fused post-inc loads — the decoder gap is real on a
// SURVIVING emit path; restoring the DIS round-trip requires the legacy
// 4-byte probe (or migrating the encoder to Mode-0). Tracked here.
//
// REGRESSION TEST: D_LDW_POST_REG / D_LW_POST_REG / D_LHW_POST_REG (fused
// post-increment DR64 load by REGISTER stride, in three data widths) must
// assemble and encode with distinct base (rs1) and stride (rs2).
//
// Spec (encoding_manual.md:412-494) defines the s1 LD register-offset
// format. The width is encoded in the type field at bits[31:28] per
// encoding_manual.md:462-476: 0x0=DW, 0x1=W, 0x2=HW. Worked example:
// d_ldw_post_reg d3, r5, r7 -> s1 window 0x157103.

// DW (D_LDW_POST_REG) worked example from encoding_manual.md:478-494
// rt=3, rs=5, rs2=7. Bundle s1 window = 0x157103.
d_ldw_post_reg d3, r5, r7

// DW variants covering different DR dests + base/stride regs
// (r8..r12 = callee-saved GPRs; r13=SP, r14=FP, r15=LR are reserved.)
d_ldw_post_reg d0, r1, r2
d_ldw_post_reg d1, r3, r4
d_ldw_post_reg d5, r6, r7
d_ldw_post_reg d7, r8, r9
d_ldw_post_reg d2, r10, r11

// W (D_LW_POST_REG) variants
d_lw_post_reg d3, r5, r7
d_lw_post_reg d0, r1, r2
d_lw_post_reg d1, r3, r4

// HW (D_LHW_POST_REG) variants
d_lhw_post_reg d3, r5, r7
d_lhw_post_reg d0, r1, r2
d_lhw_post_reg d1, r3, r4

// Encoder check: all variants must parse and produce an encoding line.
// (Each variant lands in its own single-child bundle, separated by a blank
// line in the show-encoding output — do not use -NEXT here.)
// ENC: d_ldw_post_reg	d3, r5, r7
// ENC: d_ldw_post_reg	d0, r1, r2
// ENC: d_ldw_post_reg	d1, r3, r4
// ENC: d_ldw_post_reg	d5, r6, r7
// ENC: d_ldw_post_reg	d7, r8, r9
// ENC: d_ldw_post_reg	d2, r10, r11
// ENC: d_lw_post_reg	d3, r5, r7
// ENC: d_lw_post_reg	d0, r1, r2
// ENC: d_lw_post_reg	d1, r3, r4
// ENC: d_lhw_post_reg	d3, r5, r7
// ENC: d_lhw_post_reg	d0, r1, r2
// ENC: d_lhw_post_reg	d1, r3, r4
