// ===-- HaydnFormatsLS.td - Load/Store Flex slot formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// fresh per-slot port of ALL Load/Store (LS) ops from the spec
// (golden/slot0_load_and_store_unit_instruction_list.json, 109 codepoints).
// Authored per-slot (one def per op per legal slot) to future-proof against
// per-slot format divergence. NOT derived from legacy HaydnInstrInfo*.td.
// Product encode is Format E (golden/format_e_bit_layout_v2).
//
// Encoding model (Format E FU/opcode widths):
// LS FU code = 0b001, opcode width = 7 bits.
// s0[47:45]=FU(001) s0[44:38]=opcode(7b) operands + reserved fill [37:0]
// s1[39:37]=FU(001) s1[36:30]=opcode(7b) operands + reserved fill [29:0]
// s2[39:37]=FU(001) s2[36:30]=opcode(7b) operands + reserved fill [29:0]
//
// Field-order convention (mirrors proven HaydnFormatsALU32.td template):
// MSB-first concat = { FU, opcode, reserved, <operand fields high->low> }
// Register operand block: rt/rtd HIGHEST, rs2 mid, rs/rs1 LOWEST.
// Imm fields sit within the operand block above the lowest register.
// 6-bit imm (RI6): reserved+imm6 pack into the high-operand region.
//
// Register banks (per golden DR_Read_Port/DR_Write_Port vs GPR_Read/Write):
// D_* loads (LD/LW/LHW): DR64 dest `rtd` (DR_Write_Port), GPR32 base `rs`.
// D_* stores (SD/SW_L/SW_H/SHW): DR64 source `rtd` (DR_Read_Port), base.
// S_* loads (LW/LHWS/LHWU/LBS/LBU): GPR32 dest `rt`, GPR32 base `rs`.
// S_* stores (SW/SHW/SB): GPR32 source `rt`, GPR32 base `rs`.
// BREV: bit-reversed addressing; same dest/source shape as LD/SD base op.
//
// Legal slots (§1.2 + user revision): LS is dynamically dispatchable in
// ALL 3 slots (s0/s1/s2) -- NOT s0-only. Every LS op gets 3 defs unless the
// spec restricts it; golden lists no LS op as slot-restricted, so 3 defs each.
//
// Additive only: does NOT touch HaydnSlots.td, Haydn.td include wiring
// CodeGenFormat.cpp, or the other HaydnFormats*.td files.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Shared operand: signed 8-bit immediate (CBRI circular-buffer stride).
// golden CBRI ops use `imm8` (sign-extended <<3 in Behavior) -- model as simm8.
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// Operand dag convention (driven by golden GPR/DR Read/Write port lists):
//
// D_LD_*_IMM (outs DR64:$rtd) (ins GPR32:$rs, simm6:$imm6)
// D_LD_*_REG (outs DR64:$rtd) (ins GPR32:$rs1, GPR32:$rs2)
// D_LD_*_POST/PRE_IMM (outs DR64:$rtd, GPR32:$rs) (ins GPR32:$rs_in, simm6:$imm6)
// ^-- golden lists rs in BOTH GPR_Read_Port and GPR_Write_Port for PRE/POST
// forms: rs is read (base) and written (updated). The updated value is
// modeled as an OUT tied to an IN ($rs and $rs_in share the same reg
// via tied-use). For the format-class declaration we list both: out
// `rs` (written back) and in `rs_in` (read base); the caller ties them.
// D_LD_*_POST/PRE_REG (outs DR64:$rtd, GPR32:$rs1) (ins GPR32:$rs1_in, GPR32:$rs2)
//
// S_LD_* (same pattern with GPR32:$rt dest in place of DR64:$rtd)
//
// D_SD_* / S_SW_* / D_SHW_* / S_SHW_* / S_SB_* / D_SW_L / D_SW_H -- STORES:
// rtd/rt is the SOURCE value (in DR_Read_Port / GPR_Read_Port), never a
// write. OUTs are only the base regs that golden marks as GPR_Write_Port
// (rs/rs1 for POST/PRE; nothing for WITH).
//
// BREV_* -- bit-reversed LD/SD: same dest/source shape as the base LD/SD.
// CBRI: cbr_sel(uimm2) + rtd(DR64) + rs(GPR32) + imm8(simm8).
// CBRR: cbr_sel(uimm2) + rtd(DR64) + rs1(GPR32) + rs2(GPR32).
// AR: ar_sel(uimm2) + dir_sel(uimm1) + per-op register operands.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// S0 (48-bit window) LS format classes -- one per operand shape.
//===----------------------------------------------------------------------===//

// LS D_LD_RI6 in S0: rtd(DR64 dest), rs(GPR32 base), imm6.
// Used by D_LD_*_WITH_IMM (no base writeback): no rs OUT.
// Payload 3+7+24+6+4+4 = 48b.
class HaydnFU_LS_S0_D_LD_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<24> reserved = 0;     // [37:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 dest (HIGHEST)
  let s0 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s0[47:0]
}

// LS S_LD_RI6 in S0: rt(GPR32 dest), rs(GPR32 base), imm6.
// Used by S_LD_*_WITH_IMM (no base writeback): no rt/rs OUT for WITH form.
// For S_LD_*_POST/PRE_IMM, rs is written back (OUT rs + IN rs_in tied).
// Payload 3+7+24+6+4+4 = 48b.
class HaydnFU_LS_S0_S_LD_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<24> reserved = 0;     // [37:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 dest (HIGHEST)
  let s0 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s0[47:0]
}

// LS D_LD_RR in S0: rtd(DR64 dest), rs1, rs2 (GPR32).
// Used by D_LD_*_WITH_REG (no base writeback).
// Payload 3+7+26+4+4+4 = 48b.
class HaydnFU_LS_S0_D_LD_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<26> reserved = 0;     // [37:12] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s0 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s0[47:0]
}

// LS S_LD_RR in S0: rt(GPR32 dest), rs1, rs2 (GPR32).
// Used by S_LD_*_WITH_REG (no base writeback).
// Payload 3+7+26+4+4+4 = 48b.
class HaydnFU_LS_S0_S_LD_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<26> reserved = 0;     // [37:12] unused
  bits<4> rt;                // GPR32 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s0 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s0[47:0]
}

// LS STORE_RI6 in S0 (D_SD_* / D_SW_* / D_SHW_* family): rtd(DR64 SOURCE)
// rs(GPR32 base), imm6. rtd is never written (it is the store SOURCE).
// Payload 3+7+24+6+4+4 = 48b.
class HaydnFU_LS_S0_D_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<24> reserved = 0;     // [37:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 source (HIGHEST)
  let s0 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s0[47:0]
}

// LS STORE_RI6 in S0 (S_SW_* / S_SHW_* / S_SB_* family): rt(GPR32 SOURCE)
// rs(GPR32 base), imm6. rt is never written (store SOURCE).
// Payload 3+7+24+6+4+4 = 48b.
class HaydnFU_LS_S0_S_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<24> reserved = 0;     // [37:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 source (HIGHEST)
  let s0 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s0[47:0]
}

// LS STORE_RR in S0 (D_SD_* / D_SW_* / D_SHW_* family): rtd(DR64 SOURCE)
// rs1, rs2 (GPR32).
// Payload 3+7+26+4+4+4 = 48b.
class HaydnFU_LS_S0_D_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<26> reserved = 0;     // [37:12] unused
  bits<4> rtd;               // DR64 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s0 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s0[47:0]
}

// LS STORE_RR in S0 (S_SW_* / S_SHW_* / S_SB_* family): rt(GPR32 SOURCE)
// rs1, rs2 (GPR32).
// Payload 3+7+26+4+4+4 = 48b.
class HaydnFU_LS_S0_S_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<26> reserved = 0;     // [37:12] unused
  bits<4> rt;                // GPR32 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s0 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s0[47:0]
}

// LS CBRI in S0 (D_LDW_CB_IMM / D_SDW_CB_IMM): cbr_sel(uimm2), rtd(DR64)
// rs(GPR32 base), imm8(simm8).
// Payload 3+7+20+2+8+4+4 = 48b.
class HaydnFU_LS_S0_CBRI<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<20> reserved = 0;     // [37:18] unused
  bits<2> cbr_sel;           // circular-buffer register selector
  bits<8> imm8;              // signed stride (simm8)
  bits<4> rtd;               // DR64 (HIGHEST; dest for LD, source for SD)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, cbr_sel, imm8, rtd, rs};  // s0[47:0]
}

// LS CBRR in S0 (D_LDW_CB_REG / D_SDW_CB_REG): cbr_sel(uimm2), rtd(DR64)
// rs1, rs2 (GPR32).
// Payload 3+7+24+2+4+4+4 = 48b.
class HaydnFU_LS_S0_CBRR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<24> reserved = 0;     // [37:14] unused
  bits<2> cbr_sel;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s0 = {FU, opcode, reserved, cbr_sel, rtd, rs2, rs1};  // s0[47:0]
}

// LS PLDWWUA in S0 (AR type): ar_sel(uimm2), rs(GPR32). Pre-load AR.
// Payload 3+7+32+2+4 = 48b.
class HaydnFU_LS_S0_AR_PLDW<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<32> reserved = 0;     // [37:6] unused
  bits<2> ar_sel;
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, ar_sel, rs};     // MSB-first -> s0[47:0]
}

// LS FLAR in S0 (AR type): ar_sel(uimm2). Flush AR.
// Payload 3+7+36+2 = 48b.
class HaydnFU_LS_S0_AR_FLAR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<36> reserved = 0;     // [37:2] unused
  bits<2> ar_sel;
  let s0 = {FU, opcode, reserved, ar_sel};        // MSB-first -> s0[47:0]
}

// LS WBARWUA in S0 (AR type): ar_sel(uimm2), rs(GPR32), dir_sel(uimm1).
// Payload 3+7+31+2+4+1 = 48b -> reserved33: 3+7+33+2+1(dir)+2(pad)...
// Use reserved32 + ar_sel + rs + dir_sel + pad1 = 3+7+32+2+1(dir)+1+4? = 50b
// Re-split: reserved31 + ar_sel(2) + rs(4) + dir_sel(1) = 3+7+31+2+4+1 = 48b.
class HaydnFU_LS_S0_AR_WBAR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<31> reserved = 0;     // [37:7] unused
  bits<2> ar_sel;
  bits<4> rs;                // GPR32 base
  bits<1> dir_sel;           // 0=normal, 1=reverse
  let s0 = {FU, opcode, reserved, ar_sel, rs, dir_sel};  // s0[47:0]
}

// LS D_LQHWUA_POST / D_LTWUA_POST / D_SQHWUA_POST / D_STWUA_POST in S0
// (AR type): rtd(DR64), ar_sel(uimm2), rs1, rs2 (GPR32), dir_sel(uimm1).
// Payload 3+7+22+1(dir)+2(ar)+4+4+4+1 = 48b.
// Re-split: reserved23 + rtd(4) + ar_sel(2) + rs2(4) + rs1(4) + dir_sel(1)
// + reserved_pad(6) -> 3+7+23+4+2+4+4+1 = 48b. Add pad0 to fill.
// Simpler: reserved22 + rtd(4) + ar_sel(2) + rs2(4) + rs1(4) + dir_sel(1)
// + pad0(3) = 3+7+22+4+2+4+4+1+3 = 50b -- too many.
// Final split: FU(3)+opcode(7)+reserved(20)+rtd(4)+ar_sel(2)+rs2(4)+rs1(4)
// +dir_sel(1)+pad(3) = 3+7+20+4+2+4+4+1+3 = 48b.
class HaydnFU_LS_S0_AR_UA<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<20> reserved = 0;     // [37:18] unused
  bits<4> rtd;               // DR64 (HIGHEST; dest for L*, source for S*)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 48b
  let s0 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s0[47:0]
}

//===----------------------------------------------------------------------===//
// S1 / S2 (40-bit window) LS format classes -- one per operand shape.
//===----------------------------------------------------------------------===//

// LS D_LD_RI6 in S1: rtd(DR64), rs(GPR32), imm6.
// Payload 3+7+16+6+4+4 = 40b.
class HaydnFU_LS_S1_D_LD_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 dest (HIGHEST)
  let s1 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s1[39:0]
}

// LS S_LD_RI6 in S1: rt(GPR32), rs(GPR32), imm6.
// Payload 3+7+16+6+4+4 = 40b.
class HaydnFU_LS_S1_S_LD_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 dest (HIGHEST)
  let s1 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s1[39:0]
}

// LS D_LD_RR in S1: rtd(DR64), rs1, rs2 (GPR32).
// Payload 3+7+18+4+4+4 = 40b.
class HaydnFU_LS_S1_D_LD_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s1[39:0]
}

// LS S_LD_RR in S1: rt(GPR32), rs1, rs2 (GPR32).
// Payload 3+7+18+4+4+4 = 40b.
class HaydnFU_LS_S1_S_LD_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rt;                // GPR32 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s1[39:0]
}

// LS D_ST_RI6 in S1 (D_SD_* / D_SW_* / D_SHW_* family): rtd(DR64 SOURCE)
// rs(GPR32 base), imm6.
// Payload 3+7+16+6+4+4 = 40b.
class HaydnFU_LS_S1_D_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 source (HIGHEST)
  let s1 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s1[39:0]
}

// LS S_ST_RI6 in S1 (S_SW_* / S_SHW_* / S_SB_* family): rt(GPR32 SOURCE)
// rs(GPR32 base), imm6.
// Payload 3+7+16+6+4+4 = 40b.
class HaydnFU_LS_S1_S_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 source (HIGHEST)
  let s1 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s1[39:0]
}

// LS D_ST_RR in S1 (D_SD_* / D_SW_* / D_SHW_* family): rtd(DR64 SOURCE)
// rs1, rs2 (GPR32).
// Payload 3+7+18+4+4+4 = 40b.
class HaydnFU_LS_S1_D_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rtd;               // DR64 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s1[39:0]
}

// LS S_ST_RR in S1 (S_SW_* / S_SHW_* / S_SB_* family): rt(GPR32 SOURCE)
// rs1, rs2 (GPR32).
// Payload 3+7+18+4+4+4 = 40b.
class HaydnFU_LS_S1_S_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rt;                // GPR32 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s1[39:0]
}

// LS CBRI in S1: cbr_sel(uimm2), rtd(DR64), rs(GPR32), imm8(simm8).
// Payload 3+7+8+2+8+4+4 = 36b -> reserved12: 3+7+12+2+8+4+4 = 40b.
class HaydnFU_LS_S1_CBRI<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<12> reserved = 0;     // [29:18] unused
  bits<2> cbr_sel;
  bits<8> imm8;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s1 = {FU, opcode, reserved, cbr_sel, imm8, rtd, rs};  // s1[39:0]
}

// LS CBRR in S1: cbr_sel(uimm2), rtd(DR64), rs1, rs2 (GPR32).
// Payload 3+7+16+2+4+4+4 = 40b.
class HaydnFU_LS_S1_CBRR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<2> cbr_sel;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, cbr_sel, rtd, rs2, rs1};  // s1[39:0]
}

// LS PLDWWUA in S1 (AR type): ar_sel(uimm2), rs(GPR32).
// Payload 3+7+24+2+4 = 40b.
class HaydnFU_LS_S1_AR_PLDW<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<24> reserved = 0;     // [29:6] unused
  bits<2> ar_sel;
  bits<4> rs;                // GPR32 base (LOWEST)
  let s1 = {FU, opcode, reserved, ar_sel, rs};     // MSB-first -> s1[39:0]
}

// LS FLAR in S1 (AR type): ar_sel(uimm2).
// Payload 3+7+28+2 = 40b.
class HaydnFU_LS_S1_AR_FLAR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<28> reserved = 0;     // [29:2] unused
  bits<2> ar_sel;
  let s1 = {FU, opcode, reserved, ar_sel};        // MSB-first -> s1[39:0]
}

// LS WBARWUA in S1 (AR type): ar_sel(uimm2), rs(GPR32), dir_sel(uimm1).
// Payload 3+7+23+2+4+1 = 40b.
class HaydnFU_LS_S1_AR_WBAR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<23> reserved = 0;     // [29:7] unused
  bits<2> ar_sel;
  bits<4> rs;
  bits<1> dir_sel;
  let s1 = {FU, opcode, reserved, ar_sel, rs, dir_sel};  // s1[39:0]
}

// LS D_LQHWUA_POST / D_LTWUA_POST / D_SQHWUA_POST / D_STWUA_POST in S1
// (AR type): rtd(DR64), ar_sel(uimm2), rs1, rs2 (GPR32), dir_sel(uimm1).
// Payload 3+7+12+4+2+4+4+1+3 = 40b (pad0 fills the last 3 bits).
class HaydnFU_LS_S1_AR_UA<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<12> reserved = 0;     // [29:18] unused
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 40b
  let s1 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s1[39:0]
}

// LS D_LD_RI6 in S2: rtd(DR64), rs(GPR32), imm6. Mirrors S1.
class HaydnFU_LS_S2_D_LD_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 dest (HIGHEST)
  let s2 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s2[39:0]
}

// LS S_LD_RI6 in S2: rt(GPR32), rs(GPR32), imm6. Mirrors S1.
class HaydnFU_LS_S2_S_LD_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 dest (HIGHEST)
  let s2 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s2[39:0]
}

// LS D_LD_RR in S2: rtd(DR64), rs1, rs2 (GPR32). Mirrors S1.
class HaydnFU_LS_S2_D_LD_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s2[39:0]
}

// LS S_LD_RR in S2: rt(GPR32), rs1, rs2 (GPR32). Mirrors S1.
class HaydnFU_LS_S2_S_LD_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 1;  // setDesc preserves load semantic
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rt;                // GPR32 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s2[39:0]
}

// LS D_ST_RI6 in S2 (D_SD_* / D_SW_* / D_SHW_* family). Mirrors S1.
class HaydnFU_LS_S2_D_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 source (HIGHEST)
  let s2 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s2[39:0]
}

// LS S_ST_RI6 in S2 (S_SW_* / S_SHW_* / S_SB_* family). Mirrors S1.
class HaydnFU_LS_S2_S_ST_RI6<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 source (HIGHEST)
  let s2 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s2[39:0]
}

// LS D_ST_RR in S2 (D_SD_* / D_SW_* / D_SHW_* family). Mirrors S1.
class HaydnFU_LS_S2_D_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rtd;               // DR64 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s2[39:0]
}

// LS S_ST_RR in S2 (S_SW_* / S_SHW_* / S_SB_* family). Mirrors S1.
class HaydnFU_LS_S2_S_ST_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;  // setDesc preserves store semantic
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<18> reserved = 0;     // [29:12] unused
  bits<4> rt;                // GPR32 source (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s2[39:0]
}

// LS CBRI in S2: cbr_sel(uimm2), rtd(DR64), rs(GPR32), imm8(simm8).
// Mirrors S1.
class HaydnFU_LS_S2_CBRI<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<12> reserved = 0;     // [29:18] unused
  bits<2> cbr_sel;
  bits<8> imm8;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s2 = {FU, opcode, reserved, cbr_sel, imm8, rtd, rs};  // s2[39:0]
}

// LS CBRR in S2: cbr_sel(uimm2), rtd(DR64), rs1, rs2 (GPR32). Mirrors S1.
class HaydnFU_LS_S2_CBRR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<16> reserved = 0;     // [29:14] unused
  bits<2> cbr_sel;
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, cbr_sel, rtd, rs2, rs1};  // s2[39:0]
}

// LS PLDWWUA in S2 (AR type): ar_sel(uimm2), rs(GPR32). Mirrors S1.
class HaydnFU_LS_S2_AR_PLDW<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<24> reserved = 0;     // [29:6] unused
  bits<2> ar_sel;
  bits<4> rs;                // GPR32 base (LOWEST)
  let s2 = {FU, opcode, reserved, ar_sel, rs};     // MSB-first -> s2[39:0]
}

// LS FLAR in S2 (AR type): ar_sel(uimm2). Mirrors S1.
class HaydnFU_LS_S2_AR_FLAR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<28> reserved = 0;     // [29:2] unused
  bits<2> ar_sel;
  let s2 = {FU, opcode, reserved, ar_sel};        // MSB-first -> s2[39:0]
}

// LS WBARWUA in S2 (AR type): ar_sel(uimm2), rs(GPR32), dir_sel(uimm1).
// Mirrors S1.
class HaydnFU_LS_S2_AR_WBAR<bits<7> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<23> reserved = 0;     // [29:7] unused
  bits<2> ar_sel;
  bits<4> rs;
  bits<1> dir_sel;
  let s2 = {FU, opcode, reserved, ar_sel, rs, dir_sel};  // s2[39:0]
}

// LS D_LQHWUA_POST / D_LTWUA_POST / D_SQHWUA_POST / D_STWUA_POST in S2
// (AR type). Mirrors S1.
class HaydnFU_LS_S2_AR_UA<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_LS;   // S2 memory: Data_Latency 2 (not Slot2_ALU)
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<12> reserved = 0;     // [29:18] unused
  bits<4> rtd;               // DR64 (HIGHEST)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 40b
  let s2 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s2[39:0]
}

// TODO(golden) per-op field ordering: the exact MSB->LSB split of ar_sel vs
// dir_sel vs cbr_sel in the AR/CB families is not fully specified in Format E
// golden (only FU/opcode widths are frozen). The layout above is best-effort
// (2-bit ar_sel, 1-bit dir_sel, 2-bit cbr_sel). Refine once golden confirms
// bit-exact placement.

//===----------------------------------------------------------------------===//
// LS3 multiclasses — PRE/POST/WITH with tied base def (refactor)
//===----------------------------------------------------------------------===//

// LS3_D_LD_RI6_PostPre retired (R13). Format E members fill e0/e1/e2.
// LS3_D_LD_RI6_With retired (R13). Format E members fill e0/e1/e2.
// LS3_S_LD_RI6_PostPre retired (R13). Format E members fill e0/e1/e2.
// LS3_S_LD_RI6_With retired (R13). Format E members fill e0/e1/e2.
// LS3_D_LD_RR_PostPre retired (R13). Format E members fill e0/e1;
// D_LDW_POST_REG keeps explicit S2 Fallback (no e2 member).
// LS3_D_LD_RR_With retired (R13). Format E members fill e0/e1/e2.
// LS3_S_LD_RR_PostPre retired (R13). Format E members fill e0/e1/e2.
// LS3_S_LD_RR_With retired (R13). Format E members fill e0/e1/e2.
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
multiclass LS3_D_ST_RI6_PostPre<bits<7> opc, string mnem> {
  def _S1 : HaydnFU_LS_S1_D_ST_RI6<opc, (outs GPR32:$rs_wb),
      (ins DR64:$rtd, GPR32:$rs, simm6:$imm6), !strconcat(mnem, "\t$rtd, $rs, $imm6"), []> {
    let Constraints = "$rs = $rs_wb";
  }
  def _S2 : HaydnFU_LS_S2_D_ST_RI6<opc, (outs GPR32:$rs_wb),
      (ins DR64:$rtd, GPR32:$rs, simm6:$imm6), !strconcat(mnem, "\t$rtd, $rs, $imm6"), []> {
    let Constraints = "$rs = $rs_wb";
  }
}
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
multiclass LS3_D_ST_RI6_With<bits<7> opc, string mnem> {
  def _S1 : HaydnFU_LS_S1_D_ST_RI6<opc, (outs),
      (ins DR64:$rtd, GPR32:$rs, simm6:$imm6), !strconcat(mnem, "\t$rtd, $rs, $imm6"), []>;
  def _S2 : HaydnFU_LS_S2_D_ST_RI6<opc, (outs),
      (ins DR64:$rtd, GPR32:$rs, simm6:$imm6), !strconcat(mnem, "\t$rtd, $rs, $imm6"), []>;
}
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
multiclass LS3_S_ST_RI6_PostPre<bits<7> opc, string mnem> {
  def _S1 : HaydnFU_LS_S1_S_ST_RI6<opc, (outs GPR32:$rs_wb),
      (ins GPR32:$rt, GPR32:$rs, simm6:$imm6), !strconcat(mnem, "\t$rt, $rs, $imm6"), []> {
    let Constraints = "$rs = $rs_wb";
  }
  def _S2 : HaydnFU_LS_S2_S_ST_RI6<opc, (outs GPR32:$rs_wb),
      (ins GPR32:$rt, GPR32:$rs, simm6:$imm6), !strconcat(mnem, "\t$rt, $rs, $imm6"), []> {
    let Constraints = "$rs = $rs_wb";
  }
}
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
multiclass LS3_S_ST_RI6_With<bits<7> opc, string mnem> {
  def _S1 : HaydnFU_LS_S1_S_ST_RI6<opc, (outs),
      (ins GPR32:$rt, GPR32:$rs, simm6:$imm6), !strconcat(mnem, "\t$rt, $rs, $imm6"), []>;
  def _S2 : HaydnFU_LS_S2_S_ST_RI6<opc, (outs),
      (ins GPR32:$rt, GPR32:$rs, simm6:$imm6), !strconcat(mnem, "\t$rt, $rs, $imm6"), []>;
}
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
multiclass LS3_D_ST_RR_PostPre<bits<7> opc, string mnem> {
  def _S1 : HaydnFU_LS_S1_D_ST_RR<opc, (outs GPR32:$rs1_wb),
      (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2), !strconcat(mnem, "\t$rtd, $rs1, $rs2"), []> {
    let Constraints = "$rs1 = $rs1_wb";
  }
  def _S2 : HaydnFU_LS_S2_D_ST_RR<opc, (outs GPR32:$rs1_wb),
      (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2), !strconcat(mnem, "\t$rtd, $rs1, $rs2"), []> {
    let Constraints = "$rs1 = $rs1_wb";
  }
}
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
multiclass LS3_D_ST_RR_With<bits<7> opc, string mnem> {
  def _S1 : HaydnFU_LS_S1_D_ST_RR<opc, (outs),
      (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2), !strconcat(mnem, "\t$rtd, $rs1, $rs2"), []>;
  def _S2 : HaydnFU_LS_S2_D_ST_RR<opc, (outs),
      (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2), !strconcat(mnem, "\t$rtd, $rs1, $rs2"), []>;
}
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
multiclass LS3_S_ST_RR_PostPre<bits<7> opc, string mnem> {
  def _S1 : HaydnFU_LS_S1_S_ST_RR<opc, (outs GPR32:$rs1_wb),
      (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2), !strconcat(mnem, "\t$rt, $rs1, $rs2"), []> {
    let Constraints = "$rs1 = $rs1_wb";
  }
  def _S2 : HaydnFU_LS_S2_S_ST_RR<opc, (outs GPR32:$rs1_wb),
      (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2), !strconcat(mnem, "\t$rt, $rs1, $rs2"), []> {
    let Constraints = "$rs1 = $rs1_wb";
  }
}
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
multiclass LS3_S_ST_RR_With<bits<7> opc, string mnem> {
  def _S1 : HaydnFU_LS_S1_S_ST_RR<opc, (outs),
      (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2), !strconcat(mnem, "\t$rt, $rs1, $rs2"), []>;
  def _S2 : HaydnFU_LS_S2_S_ST_RR<opc, (outs),
      (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2), !strconcat(mnem, "\t$rt, $rs1, $rs2"), []>;
}

//===----------------------------------------------------------------------===//
// LS op defs -- 3 per op (s0/s1/s2). Opcodes are golden Index (7-bit, dense).
//
// POST/PRE writeback (golden: base in GPR_Read_Port AND GPR_Write_Port):
// model as tied base def ($rs_wb / $rs1_wb) for LLVM SSA. Encoding fields
// bind to the use ($rs / $rs1). Not a second ISA return; not same-cycle data.
// All PRE/POST must exploit the tied def — LS3_* multiclasses enforce this.
//===----------------------------------------------------------------------===//

// #1 D_LDW_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #2 D_LDW_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #3 D_LDW_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #4 D_LDW_POST_REG (RR) — S0/S1 retired (R13). No Format E e2 member;
// S2 Fallback required. Occupancy Mask 0x7 stays.
def D_LDW_POST_REG_S2 : HaydnFU_LS_S2_D_LD_RR<0b0000100, (outs DR64:$rtd, GPR32:$rs_wb),
    (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_post_reg\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
// #5 D_LDW_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #6 D_LDW_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #7 S_LW_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #8 S_LW_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #9 S_LW_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #10 S_LW_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #11 S_LW_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #12 S_LW_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #13 D_LW_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #14 D_LW_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #15 D_LW_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #16 D_LW_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #17 D_LW_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #18 D_LW_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #19 S_LHWS_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #20 S_LHWS_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #21 S_LHWS_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #22 S_LHWS_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #23 S_LHWS_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #24 S_LHWS_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #25 D_LHW_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #26 D_LHW_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #27 D_LHW_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #28 D_LHW_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #29 D_LHW_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #30 D_LHW_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #31 S_LHWU_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #32 S_LHWU_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #33 S_LHWU_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #34 S_LHWU_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #35 S_LHWU_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #36 S_LHWU_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #37 S_LBS_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #38 S_LBS_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #39 S_LBS_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #40 S_LBS_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #41 S_LBS_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #42 S_LBS_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #43 S_LBU_POST_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #44 S_LBU_WITH_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #45 S_LBU_PRE_IMM FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #46 S_LBU_POST_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #47 S_LBU_WITH_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.

// #48 S_LBU_PRE_REG FieldSlots retired (R13). Occupancy Mask 0x7 stays.
// #49 D_SDW_POST_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// Batch 3: aligned to legacy tied-def shape (HaydnInstrInfoAuto.td):
// (outs GPR32:$rs_wb), (ins DR64:$rtd, GPR32:$rs, simm6:$scaled_imm) with
// Constraints = "$rs = $rs_wb". The input $rs is the base being updated;
// $rs_wb is the writeback output. Format class bits field `rs` binds to the
// input $rs (the encoded base register).
defm D_SDW_POST_IMM : LS3_D_ST_RI6_PostPre<0b0110001, "d_sdw_post_imm">;
// #50 D_SDW_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SDW_WITH_IMM : LS3_D_ST_RI6_With<0b0110010, "d_sdw_with_imm">;

// #51 D_SDW_PRE_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SDW_PRE_IMM : LS3_D_ST_RI6_PostPre<0b0110011, "d_sdw_pre_imm">;

// #52 D_SDW_POST_REG (RR)
// Batch 3: aligned to legacy tied-def shape. Format class bits field
// `rs1` binds to input $rs1 (the encoded base register).
defm D_SDW_POST_REG : LS3_D_ST_RR_PostPre<0b0110100, "d_sdw_post_reg">;
// #53 D_SDW_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SDW_WITH_REG : LS3_D_ST_RR_With<0b0110101, "d_sdw_with_reg">;

// #54 D_SDW_PRE_REG (RR)
defm D_SDW_PRE_REG : LS3_D_ST_RR_PostPre<0b0110110, "d_sdw_pre_reg">;

// #55 S_SW_POST_IMM (RI6)
// Batch 3: aligned to legacy tied-def shape. Format class bits field
// `rs` binds to input $rs (the encoded base register).
defm S_SW_POST_IMM : LS3_S_ST_RI6_PostPre<0b0110111, "s_sw_post_imm">;
// ST32_POST (legacy codegen name for S_SW_POST_IMM). AlternateInsts /
// leaveRegion setDesc needs a matching _S1 member for the legacy name.
// Same format class + codepoint as S_SW_POST_IMM_S1, so it must stay out of
// the decoder trie — but via DecoderNamespace, NOT isCodeGenOnly (d463 /
// CB-142): the latter also drops the mnemonic from the asm matcher, so
// `clang -S` printed `st32_post` that no assembler could read back.
// ST32_POST is S1 (Format E LOAD1).
def ST32_POST_S1 : HaydnFU_LS_S1_S_ST_RI6<0b0110111, (outs GPR32:$rs_wb), (ins GPR32:$rt, GPR32:$rs, simm6:$imm6), "st32_post\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
  let DecoderNamespace = "FlexEmitterOnly";
}

// ST64_POST (legacy codegen name for D_SDW_POST_IMM, FmtLSPostInc<0x49>).
// Same rationale as ST32_POST above — the fused post-inc store produced by
// HaydnExpandPostIncEarly carries the legacy name, so AlternateInsts /
// setDesc needs a matching _S1 member. Same format class (D_ST_RI6 — DR64
// source) + codepoint as D_SDW_POST_IMM_S1; DecoderNamespace (not
// isCodeGenOnly — d463) keeps it out of the decoder trie while leaving the
// mnemonic matchable. ST64_POST is S1 (Format E LOAD1, opcode 0x4).
def ST64_POST_S1 : HaydnFU_LS_S1_D_ST_RI6<0b0110001, (outs GPR32:$rs_wb), (ins DR64:$rtd, GPR32:$rs, simm6:$imm6), "st64_post\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
  let DecoderNamespace = "FlexEmitterOnly";
}

// #56 S_SW_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm S_SW_WITH_IMM : LS3_S_ST_RI6_With<0b0111000, "s_sw_with_imm">;

// #57 S_SW_PRE_IMM (RI6)
defm S_SW_PRE_IMM : LS3_S_ST_RI6_PostPre<0b0111001, "s_sw_pre_imm">;

// #58 S_SW_POST_REG (RR)
// Batch 3: aligned to legacy tied-def shape. Format class bits field
// `rs1` binds to input $rs1 (the encoded base register).
defm S_SW_POST_REG : LS3_S_ST_RR_PostPre<0b0111010, "s_sw_post_reg">;
// #59 S_SW_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm S_SW_WITH_REG : LS3_S_ST_RR_With<0b0111011, "s_sw_with_reg">;

// #60 S_SW_PRE_REG (RR)
defm S_SW_PRE_REG : LS3_S_ST_RR_PostPre<0b0111100, "s_sw_pre_reg">;

// #61 D_SW_L_POST_IMM (RI6)
defm D_SW_L_POST_IMM : LS3_D_ST_RI6_PostPre<0b0111101, "d_sw_l_post_imm">;

// #62 D_SW_L_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SW_L_WITH_IMM : LS3_D_ST_RI6_With<0b0111110, "d_sw_l_with_imm">;

// #63 D_SW_L_PRE_IMM (RI6)
defm D_SW_L_PRE_IMM : LS3_D_ST_RI6_PostPre<0b0111111, "d_sw_l_pre_imm">;

// #64 D_SW_L_POST_REG (RR)
defm D_SW_L_POST_REG : LS3_D_ST_RR_PostPre<0b1000000, "d_sw_l_post_reg">;

// #65 D_SW_L_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SW_L_WITH_REG : LS3_D_ST_RR_With<0b1000001, "d_sw_l_with_reg">;

// #66 D_SW_L_PRE_REG (RR)
defm D_SW_L_PRE_REG : LS3_D_ST_RR_PostPre<0b1000010, "d_sw_l_pre_reg">;

// #67 D_SW_H_POST_IMM (RI6)
defm D_SW_H_POST_IMM : LS3_D_ST_RI6_PostPre<0b1000011, "d_sw_h_post_imm">;

// #68 D_SW_H_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SW_H_WITH_IMM : LS3_D_ST_RI6_With<0b1000100, "d_sw_h_with_imm">;

// #69 D_SW_H_PRE_IMM (RI6)
defm D_SW_H_PRE_IMM : LS3_D_ST_RI6_PostPre<0b1000101, "d_sw_h_pre_imm">;

// #70 D_SW_H_POST_REG (RR)
defm D_SW_H_POST_REG : LS3_D_ST_RR_PostPre<0b1000110, "d_sw_h_post_reg">;

// #71 D_SW_H_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SW_H_WITH_REG : LS3_D_ST_RR_With<0b1000111, "d_sw_h_with_reg">;

// #72 D_SW_H_PRE_REG (RR)
defm D_SW_H_PRE_REG : LS3_D_ST_RR_PostPre<0b1001000, "d_sw_h_pre_reg">;

// #73 S_SHW_POST_IMM (RI6)
defm S_SHW_POST_IMM : LS3_S_ST_RI6_PostPre<0b1001001, "s_shw_post_imm">;

// #74 S_SHW_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm S_SHW_WITH_IMM : LS3_S_ST_RI6_With<0b1001010, "s_shw_with_imm">;

// #75 S_SHW_PRE_IMM (RI6)
defm S_SHW_PRE_IMM : LS3_S_ST_RI6_PostPre<0b1001011, "s_shw_pre_imm">;

// #76 S_SHW_POST_REG (RR)
defm S_SHW_POST_REG : LS3_S_ST_RR_PostPre<0b1001100, "s_shw_post_reg">;

// #77 S_SHW_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm S_SHW_WITH_REG : LS3_S_ST_RR_With<0b1001101, "s_shw_with_reg">;

// #78 S_SHW_PRE_REG (RR)
defm S_SHW_PRE_REG : LS3_S_ST_RR_PostPre<0b1001110, "s_shw_pre_reg">;

// #79 D_SHW_POST_IMM (RI6)
defm D_SHW_POST_IMM : LS3_D_ST_RI6_PostPre<0b1001111, "d_shw_post_imm">;

// #80 D_SHW_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SHW_WITH_IMM : LS3_D_ST_RI6_With<0b1010000, "d_shw_with_imm">;

// #81 D_SHW_PRE_IMM (RI6)
defm D_SHW_PRE_IMM : LS3_D_ST_RI6_PostPre<0b1010001, "d_shw_pre_imm">;

// #82 D_SHW_POST_REG (RR)
defm D_SHW_POST_REG : LS3_D_ST_RR_PostPre<0b1010010, "d_shw_post_reg">;

// #83 D_SHW_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm D_SHW_WITH_REG : LS3_D_ST_RR_With<0b1010011, "d_shw_with_reg">;

// #84 D_SHW_PRE_REG (RR)
defm D_SHW_PRE_REG : LS3_D_ST_RR_PostPre<0b1010100, "d_shw_pre_reg">;

// #85 S_SB_POST_IMM (RI6)
defm S_SB_POST_IMM : LS3_S_ST_RI6_PostPre<0b1010101, "s_sb_post_imm">;

// #86 S_SB_WITH_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm S_SB_WITH_IMM : LS3_S_ST_RI6_With<0b1010110, "s_sb_with_imm">;

// #87 S_SB_PRE_IMM (RI6)
defm S_SB_PRE_IMM : LS3_S_ST_RI6_PostPre<0b1010111, "s_sb_pre_imm">;

// #88 S_SB_POST_REG (RR)
defm S_SB_POST_REG : LS3_S_ST_RR_PostPre<0b1011000, "s_sb_post_reg">;

// #89 S_SB_WITH_REG (RR) — S0 retired (R13). Occupancy Mask 0x7 stays.
defm S_SB_WITH_REG : LS3_S_ST_RR_With<0b1011001, "s_sb_with_reg">;

// #90 S_SB_PRE_REG (RR)
defm S_SB_PRE_REG : LS3_S_ST_RR_PostPre<0b1011010, "s_sb_pre_reg">;

// #91–#94 Circular buffer load/store (CBRI / CBRR)
//
// HW AGU post-updates the base GPR (BundleSim pw_gpr). Model as tied writeback
// so PostRA pack cannot co-issue two CB ops that write the same base (B6
// WRITE_CONFLICT: three d_ldw_cb_imm same r5 in one bundle).
//
// Operand list MUST match public logical D_*_CB_* (HaydnInstrInfoAuto.td) and
// GISel: MC encode after setDesc is Desc-as-is (no operand rewrite).
// Bit layout binds by name (cbr_sel, imm8, rtd, rs) — $rs is the encoded base.
//
//   LDW_IMM: (outs rtd, rs_wb), (ins rs, cbr_sel, imm8)  Constraints rs=rs_wb
//   LDW_REG: (outs rtd, rs1_wb), (ins rs1, cbr_sel, rs2)
//   SDW_IMM: (outs rs_wb), (ins rtd, rs, cbr_sel, imm8)
//   SDW_REG: (outs rs1_wb), (ins rtd, rs1, cbr_sel, rs2)
//
// #91 D_LDW_CB_IMM (CBRI) — mayLoad so setDesc from logical preserves load.
def D_LDW_CB_IMM_S0 : HaydnFU_LS_S0_CBRI<0b1011011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, uimm2:$cbr_sel, simm8:$imm8), "d_ldw_cb_imm\t$cbr_sel, $rtd, $rs, $imm8", []> {
  let mayLoad = 1;
  let Constraints = "$rs = $rs_wb";
}
def D_LDW_CB_IMM_S1 : HaydnFU_LS_S1_CBRI<0b1011011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, uimm2:$cbr_sel, simm8:$imm8), "d_ldw_cb_imm\t$cbr_sel, $rtd, $rs, $imm8", []> {
  let mayLoad = 1;
  let Constraints = "$rs = $rs_wb";
}
def D_LDW_CB_IMM_S2 : HaydnFU_LS_S2_CBRI<0b1011011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, uimm2:$cbr_sel, simm8:$imm8), "d_ldw_cb_imm\t$cbr_sel, $rtd, $rs, $imm8", []> {
  let mayLoad = 1;
  let Constraints = "$rs = $rs_wb";
}

// #92 D_LDW_CB_REG (CBRR)
def D_LDW_CB_REG_S0 : HaydnFU_LS_S0_CBRR<0b1011100, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, uimm2:$cbr_sel, GPR32:$rs2), "d_ldw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let mayLoad = 1;
  let Constraints = "$rs1 = $rs1_wb";
}
def D_LDW_CB_REG_S1 : HaydnFU_LS_S1_CBRR<0b1011100, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, uimm2:$cbr_sel, GPR32:$rs2), "d_ldw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let mayLoad = 1;
  let Constraints = "$rs1 = $rs1_wb";
}
def D_LDW_CB_REG_S2 : HaydnFU_LS_S2_CBRR<0b1011100, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, uimm2:$cbr_sel, GPR32:$rs2), "d_ldw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let mayLoad = 1;
  let Constraints = "$rs1 = $rs1_wb";
}

// #93 D_SDW_CB_IMM (CBRI) — mayStore so setDesc from logical preserves store.
def D_SDW_CB_IMM_S0 : HaydnFU_LS_S0_CBRI<0b1011101, (outs GPR32:$rs_wb), (ins DR64:$rtd, GPR32:$rs, uimm2:$cbr_sel, simm8:$imm8), "d_sdw_cb_imm\t$cbr_sel, $rtd, $rs, $imm8", []> {
  let mayLoad = 0;
  let mayStore = 1;
  let Constraints = "$rs = $rs_wb";
}
def D_SDW_CB_IMM_S1 : HaydnFU_LS_S1_CBRI<0b1011101, (outs GPR32:$rs_wb), (ins DR64:$rtd, GPR32:$rs, uimm2:$cbr_sel, simm8:$imm8), "d_sdw_cb_imm\t$cbr_sel, $rtd, $rs, $imm8", []> {
  let mayLoad = 0;
  let mayStore = 1;
  let Constraints = "$rs = $rs_wb";
}
def D_SDW_CB_IMM_S2 : HaydnFU_LS_S2_CBRI<0b1011101, (outs GPR32:$rs_wb), (ins DR64:$rtd, GPR32:$rs, uimm2:$cbr_sel, simm8:$imm8), "d_sdw_cb_imm\t$cbr_sel, $rtd, $rs, $imm8", []> {
  let mayLoad = 0;
  let mayStore = 1;
  let Constraints = "$rs = $rs_wb";
}

// #94 D_SDW_CB_REG (CBRR)
def D_SDW_CB_REG_S0 : HaydnFU_LS_S0_CBRR<0b1011110, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, uimm2:$cbr_sel, GPR32:$rs2), "d_sdw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let mayLoad = 0;
  let mayStore = 1;
  let Constraints = "$rs1 = $rs1_wb";
}
def D_SDW_CB_REG_S1 : HaydnFU_LS_S1_CBRR<0b1011110, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, uimm2:$cbr_sel, GPR32:$rs2), "d_sdw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let mayLoad = 0;
  let mayStore = 1;
  let Constraints = "$rs1 = $rs1_wb";
}
def D_SDW_CB_REG_S2 : HaydnFU_LS_S2_CBRR<0b1011110, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, uimm2:$cbr_sel, GPR32:$rs2), "d_sdw_cb_reg\t$cbr_sel, $rtd, $rs1, $rs2", []> {
  let mayLoad = 0;
  let mayStore = 1;
  let Constraints = "$rs1 = $rs1_wb";
}

// BREV-load FieldSlots retired (R13). Occupancy Mask 0x7 stays; Fallback
// is 0. Format E members keep the tied dest2_wb POST-increment shape.

// #99 D_SDW_BREV_IMM (RI6) — S0 retired (R13). Occupancy Mask 0x7 stays.
// same tied-def POST-increment as D_SDW_BREV_REG. Legacy
// D_SDW_BREV_IMM (HaydnInstrInfoAuto.td:3011) is (outs GPR32:$rs_wb) +
// (ins DR64:$rtd, GPR32:$rs, simm6) + Constraints "$rs = $rs_wb". Collapsed
// 3-op members (no tie) were invalid for setDesc.
def D_SDW_BREV_IMM_S1 : HaydnFU_LS_S1_D_ST_RI6<0b1100011, (outs GPR32:$rs_wb), (ins DR64:$rtd, GPR32:$rs, simm6:$imm6), "d_sdw_brev_imm\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_SDW_BREV_IMM_S2 : HaydnFU_LS_S2_D_ST_RI6<0b1100011, (outs GPR32:$rs_wb), (ins DR64:$rtd, GPR32:$rs, simm6:$imm6), "d_sdw_brev_imm\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #100 D_SDW_BREV_REG (RR)
// BREV store is a tied-def POST-increment on the base (rs1) — legacy
// D_SDW_BREV_REG (HaydnInstrInfoAuto.td:2491) is (outs GPR32:$rs1_wb) +
// (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2) + Constraints "$rs1 = $rs1_wb".
// Collapsed members (no tie) caused "tied-def without tie" and
// "extra explicit operand" verifier errors. Mirrors D_SDW_POST_REG shape.
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
def D_SDW_BREV_REG_S1 : HaydnFU_LS_S1_D_ST_RR<0b1100100, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2), "d_sdw_brev_reg\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_SDW_BREV_REG_S2 : HaydnFU_LS_S2_D_ST_RR<0b1100100, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2), "d_sdw_brev_reg\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs1_wb";
}

// #101 S_SW_BREV_IMM (RI6)
// Tied logical shape (HaydnInstrInfoAuto.td:3700-3705 S_SW_BREV_IMM is 4-op
// `$rs=$rs_wb`). Collapsed 3-op members blocked AIE-unconditional setDesc
// (AIEMachineScheduler.cpp:1126-1132). Mirror S_SW_POST_IMM / D_SDW_BREV_IMM_S*.
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
def S_SW_BREV_IMM_S1 : HaydnFU_LS_S1_S_ST_RI6<0b1100101, (outs GPR32:$rs_wb), (ins GPR32:$rt, GPR32:$rs, simm6:$imm6), "s_sw_brev_imm\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_SW_BREV_IMM_S2 : HaydnFU_LS_S2_S_ST_RI6<0b1100101, (outs GPR32:$rs_wb), (ins GPR32:$rt, GPR32:$rs, simm6:$imm6), "s_sw_brev_imm\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #102 S_SW_BREV_REG (RR)
// Tied logical shape (HaydnInstrInfoAuto.td:2534-2539 S_SW_BREV_REG 4-op
// `$rs1=$rs1_wb`). Mirror S_SW_POST_REG / D_SDW_BREV_REG_S*.
// S0 retired (R13). Format E e0 LOADSTORE0 fills index 0. S1/S2 Fallback stays.
def S_SW_BREV_REG_S1 : HaydnFU_LS_S1_S_ST_RR<0b1100110, (outs GPR32:$rs1_wb), (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2), "s_sw_brev_reg\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def S_SW_BREV_REG_S2 : HaydnFU_LS_S2_S_ST_RR<0b1100110, (outs GPR32:$rs1_wb), (ins GPR32:$rt, GPR32:$rs1, GPR32:$rs2), "s_sw_brev_reg\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs1_wb";
}

// #103 PLDWWUA (AR) — pre-load AR from 8-byte-aligned mem; mayLoad + AR side-effect.
// D-AR-INTRIN: **logical** PLDWWUA is GISel + post-RA HR identity
// (no slot in opcode). Slot = AltDescs from HR auction. Private _S0/_S1/_S2
// are encode peers only (leaveRegion setDesc(member); Desc-as-is encode).
// Never ISel/PEI-emit *_S* for AR stream.
let mayLoad = 1, hasSideEffects = 1 in
def PLDWWUA : HaydnInst<4, (outs), (ins GPR32:$rs, uimm2:$ar_sel),
                        "pldwwua\t$ar_sel, $rs", []> {
  // Multi-slot placeholder; real exclusivity is alts S0|S1|S2 + HR tryAdd.
  let Itinerary = Slot012_ALU;
}
let mayLoad = 1 in {
def PLDWWUA_S0 : HaydnFU_LS_S0_AR_PLDW<0b1100111, (outs), (ins GPR32:$rs, uimm2:$ar_sel), "pldwwua\t$ar_sel, $rs", []>;
def PLDWWUA_S1 : HaydnFU_LS_S1_AR_PLDW<0b1100111, (outs), (ins GPR32:$rs, uimm2:$ar_sel), "pldwwua\t$ar_sel, $rs", []>;
def PLDWWUA_S2 : HaydnFU_LS_S2_AR_PLDW<0b1100111, (outs), (ins GPR32:$rs, uimm2:$ar_sel), "pldwwua\t$ar_sel, $rs", []>;
}

// #104 D_LQHWUA_POST (AR) — unaligned quad-half load + post-inc on rs1.
// shape: tied-def POST on base (rs1). MCInst order:
// [rtd, rs1_wb, rs1, rs2, ar_sel, dir_sel]
// Logical base for ISel/HR; encode peers _S0/_S1/_S2.
let mayLoad = 1, hasSideEffects = 1 in
def D_LQHWUA_POST : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs1_wb),
    (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_lqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let Itinerary = Slot012_ALU;
}
let mayLoad = 1 in {
def D_LQHWUA_POST_S0 : HaydnFU_LS_S0_AR_UA<0b1101000, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_lqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_LQHWUA_POST_S1 : HaydnFU_LS_S1_AR_UA<0b1101000, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_lqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_LQHWUA_POST_S2 : HaydnFU_LS_S2_AR_UA<0b1101000, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_lqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
}

// #105 D_LTWUA_POST (AR) — unaligned two-word load + post-inc on rs1.
let mayLoad = 1, hasSideEffects = 1 in
def D_LTWUA_POST : HaydnInst<4, (outs DR64:$rtd, GPR32:$rs1_wb),
    (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_ltwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let Itinerary = Slot012_ALU;
}
let mayLoad = 1 in {
def D_LTWUA_POST_S0 : HaydnFU_LS_S0_AR_UA<0b1101001, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_ltwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_LTWUA_POST_S1 : HaydnFU_LS_S1_AR_UA<0b1101001, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_ltwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_LTWUA_POST_S2 : HaydnFU_LS_S2_AR_UA<0b1101001, (outs DR64:$rtd, GPR32:$rs1_wb), (ins GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_ltwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
}

// #106 FLAR (AR) — flush aligned register (AR side-effect only).
let hasSideEffects = 1 in
def FLAR : HaydnInst<4, (outs), (ins uimm2:$ar_sel), "flar\t$ar_sel", []> {
  let Itinerary = Slot012_ALU;
}
// FLAR FieldSlots retired (R13). Occupancy Mask 0x7 stays; Fallback is 0.

// #107 WBARWUA (AR) — write-back residual from AR; mayStore + AR side-effect.
let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def WBARWUA : HaydnInst<4, (outs), (ins GPR32:$rs, uimm2:$ar_sel, uimm1:$dir_sel),
                        "wbarwua\t$ar_sel, $rs, $dir_sel", []> {
  let Itinerary = Slot012_ALU;
}
// S0 retired (R13). Format E e0 Drop-fills index 0 (2-op member vs 3-op
// logical). S1/S2 Fallback stays. Occupancy Mask 0x7 stays.
let mayStore = 1, mayLoad = 0 in {
def WBARWUA_S1 : HaydnFU_LS_S1_AR_WBAR<0b1101011, (outs), (ins GPR32:$rs, uimm2:$ar_sel, uimm1:$dir_sel), "wbarwua\t$ar_sel, $rs, $dir_sel", []>;
def WBARWUA_S2 : HaydnFU_LS_S2_AR_WBAR<0b1101011, (outs), (ins GPR32:$rs, uimm2:$ar_sel, uimm1:$dir_sel), "wbarwua\t$ar_sel, $rs, $dir_sel", []>;
}

// #108 D_SQHWUA_POST (AR) — unaligned quad-half store + post-inc on rs1.
// MCInst order: [rs1_wb, rtd, rs1, rs2, ar_sel, dir_sel]
let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def D_SQHWUA_POST : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_sqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let Itinerary = Slot012_ALU;
}
let mayStore = 1, mayLoad = 0 in {
def D_SQHWUA_POST_S0 : HaydnFU_LS_S0_AR_UA<0b1101100, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_sqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_SQHWUA_POST_S1 : HaydnFU_LS_S1_AR_UA<0b1101100, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_sqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_SQHWUA_POST_S2 : HaydnFU_LS_S2_AR_UA<0b1101100, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_sqhwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
}

// #109 D_STWUA_POST (AR) — unaligned two-word store + post-inc on rs1.
let mayStore = 1, mayLoad = 0, hasSideEffects = 1 in
def D_STWUA_POST : HaydnInst<4, (outs GPR32:$rs1_wb),
    (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel),
    "d_stwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
  let Itinerary = Slot012_ALU;
}
let mayStore = 1, mayLoad = 0 in {
def D_STWUA_POST_S0 : HaydnFU_LS_S0_AR_UA<0b1101101, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_stwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_STWUA_POST_S1 : HaydnFU_LS_S1_AR_UA<0b1101101, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_stwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
def D_STWUA_POST_S2 : HaydnFU_LS_S2_AR_UA<0b1101101, (outs GPR32:$rs1_wb), (ins DR64:$rtd, GPR32:$rs1, GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_stwua_post\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []> {
  let Constraints = "$rs1 = $rs1_wb";
}
}

// AR GISel selects the _S0 encode forms directly (real slot ops
// not isPseudo). Legal-slots come from the `_S0` name suffix (path).
// Multi-slot packing re-maps to _S1/_S2 via name-peer _S* alts + post-RA setDesc.

//===----------------------------------------------------------------------===//
// Family 2 (generic LS) — wide-offset RI16 format classes + _S0 defs.
//
// The selector emits legacy LD32/ST32/LD64/ST64 (FmtLS, simm16) for 16-bit
// frame offsets. The existing LS `_S0` defs are all RI6 (simm6 post-inc)
// or RR (reg+reg) — a DIFFERENT addressing mode. Aliasing LD32 to
// S_LW_WITH_IMM_S0 would truncate a 16-bit offset into a 6-bit field.
//
// These new RI16 format classes carry `simm16` in the 48-bit s0 LS window:
// FU(3) + opcode(7) + reserved(14) + offset(16) + rt(4) + rs(4) = 48b.
// Field names (`rt`, `rs`, `offset`) match the legacy FmtLS operand names so
// leaveRegion setDesc(member) binds correctly (encoder reads MI operands by
// position; the dag operand names match the field names for tablegen
// field-binding). The D-class fields use `rt` (not the golden `rtd`) to match
// legacy LD64/ST64's `$rt` operand.
//===----------------------------------------------------------------------===//

// LS S_LD_RI16 in S0 (LD32): rt(GPR32 dest), rs(GPR32 base), offset(simm16).
// Payload 3+7+14+16+4+4 = 48b.
// mayLoad=1 set at class level so leaveRegion setDesc (legacy LD32 -> LD32_S0)
// preserves the load memory semantic for the verifier. The golden RI6/RR LD
// classes omit it (decoder/emitter-only reach); these RI16 defs are reached
// by CodeGen via post-RA setDesc.
class HaydnFU_LS_S0_S_LD_RI16<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 1;
  let Itinerary = Slot0_LS;
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<14> reserved = 0;     // [37:24] unused
  bits<16> offset;
  bits<4> rt;                // GPR32 dest (HIGHEST of reg block)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, offset, rt, rs};  // s0[47:0]
}

// LS D_LD_RI16 in S0 (LD64): rt(DR64 dest), rs(GPR32 base), offset(simm16).
// Field named `rt` (not `rtd`) to match legacy LD64 operand `$rt`.
// mayLoad=1 — see HaydnFU_LS_S0_S_LD_RI16 comment.
class HaydnFU_LS_S0_D_LD_RI16<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 1;
  let Itinerary = Slot0_LS;
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<14> reserved = 0;     // [37:24] unused
  bits<16> offset;
  bits<4> rt;                // DR64 dest (HIGHEST of reg block)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, offset, rt, rs};  // s0[47:0]
}

// LS S_ST_RI16 in S0 (ST32): rt(GPR32 SOURCE), rs(GPR32 base), offset(simm16).
// rt is never written (store SOURCE).
// mayStore=1 — see HaydnFU_LS_S0_S_LD_RI16 comment (store counterpart).
class HaydnFU_LS_S0_S_ST_RI16<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 1;
  let Itinerary = Slot0_LS;
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<14> reserved = 0;     // [37:24] unused
  bits<16> offset;
  bits<4> rt;                // GPR32 source (HIGHEST of reg block)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, offset, rt, rs};  // s0[47:0]
}

// LS D_ST_RI16 in S0 (ST64): rt(DR64 SOURCE), rs(GPR32 base), offset(simm16).
// Field named `rt` to match legacy ST64 operand `$rt`. rt is never written.
// mayStore=1 — see HaydnFU_LS_S0_S_LD_RI16 comment (store counterpart).
class HaydnFU_LS_S0_D_ST_RI16<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 1;
  let Itinerary = Slot0_LS;
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<14> reserved = 0;     // [37:24] unused
  bits<16> offset;
  bits<4> rt;                // DR64 source (HIGHEST of reg block)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, offset, rt, rs};  // s0[47:0]
}

//===----------------------------------------------------------------------===//
// Family 2 defs — LD32/ST32/LD64/ST64 _S0 (codepoints 110-113).
// DISTINCT codepoints in the free s0-LS-FU range (above the golden 109 RI6
// codepoints). These MUST be reachable by the AsmParser (so `{ st32... }`
// parses) but must NOT appear in the decoder trie (the golden
// S_LW_WITH_IMM_S0 etc. remain the decoder entries; the emitter table
// still resolves these via `getBinaryCodeForInstr`).
//
// `isCodeGenOnly = 1` was WRONG here — that bit excludes the instr from
// BOTH the decoder trie AND the asm matcher (AsmMatcherEmitter.cpp:1587 and
// DecoderEmitter.cpp:1378 both gate on it). The result was `st32`
// `ld32` / `ld64` / `st64` being unmatchable in bundles ("failed to
// match instruction in bundle"), even though the DR64 ALU64 `_S{0,1,2}`
// siblings (which have NO isCodeGenOnly) match fine.
//
// FIX: replace `isCodeGenOnly = 1` with an unused `DecoderNamespace`. The
// matcher ignores DecoderNamespace (AsmMatcherEmitter never reads it), so the
// instr re-enters the matcher; the decoder emitter groups by namespace, so an
// otherwise-unreferenced namespace keeps these out of every live decode trie.
// This is the precise "in matcher, NOT in decoder" idiom.
//===----------------------------------------------------------------------===//

// LD32/ST32/LD64/ST64 FieldSlots retired (R13). Occupancy Mask stays;
// Fallback is 0. Placement uses Format E LOADSTORE0/LOAD1 members.

//===----------------------------------------------------------------------===//
// Family 2b — byte/half LS _S0 defs (ST8, LDU8).
//
// The selector emits legacy ST8 (FmtLS<0x42>, byte store) and LDU8
// (FmtLS<0x3F>, unsigned byte load) for i8 memory access (HaydnInstruction
// Selector selectBase register-load/store cases 1 & 8). Without _S0
// variants the forcing function fires ("opcode 'ST8' has no s0
// form"). These reuse the EXISTING RI16 S_ST / S_LD format classes (same
// operand shape as ST32/LD32: rt GPR32 + rs GPR32 + simm16); the access
// WIDTH is a CodeGen/selector property, not encoded in the s0 slot
// window (Format E members carry the same opcode-discriminated
// byte/word semantics). DISTINCT codepoints
// 118-119 in the free s0-LS-FU range above the golden set. DecoderNamespace
// = "FlexEmitterOnly" keeps them out of the decoder trie (/ idiom)
// while leaving them matchable by the AsmParser. AlternateInsts name-peer
// alts; leaveRegion setDesc commits ST8 -> ST8_S0 and LDU8 -> LDU8_S0.
//===----------------------------------------------------------------------===//

// ST8 FieldSlot retired (R13). Occupancy Mask 0x1 stays; Fallback is 0.

// LDU8 / ST16 / LDU16 / LD16 / LD8 FieldSlots retired (R13). Occupancy
// Mask 0x1 stays; Fallback is 0. Placement uses Format E e0 members.

//===----------------------------------------------------------------------===//
// Family 4 (reg-offset LS) — private encode-peer _S0 defs only.
//
// Public MIR identity (HaydnInstrInfo.td): LD32_REG_M0S0LS / LD64_REG_M0S0LS
// ST32_REG_M0S0LS / ST64_REG_M0S0LS. These `_S0` records are s0 Inst
// tables only — ISel/PEI must never emit them. AlternateInsts discover
// name-peer _S* alts; leaveRegion setDesc commits the member.
//
// Operand field names match the logical MI (`rt`, `rs`, `off`) so setDesc
// is operand-shape-preserving. DISTINCT codepoints 114-117 in free s0-LS-FU.
// DecoderNamespace FlexEmitterOnly keeps them out of the live decode trie
// while leaving them matchable by the AsmParser (idiom).
//===----------------------------------------------------------------------===//

// LS S_LD_RR_OFF in S0 (LD32_REG): rt(GPR32 dest), rs(GPR32 base), off(GPR32).
// Field names match logical REG-offset operands ($rt, $rs, $off).
// mayLoad=1 — see HaydnFU_LS_S0_S_LD_RI16 comment.
class HaydnFU_LS_S0_S_LD_RR_OFF<bits<7> opc, dag outs, dag ins, string asmstr,
                                list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 1;
  let Itinerary = Slot0_LS;
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<26> reserved = 0;     // [37:12] unused
  bits<4> rt;                // GPR32 dest (HIGHEST)
  bits<4> off;               // GPR32 offset register (mid)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, rt, off, rs};  // s0[47:0]
}

// LS D_LD_RR_OFF in S0 (LD64_REG): rt(DR64 dest), rs(GPR32 base), off(GPR32).
// mayLoad=1 — see HaydnFU_LS_S0_S_LD_RI16 comment.
class HaydnFU_LS_S0_D_LD_RR_OFF<bits<7> opc, dag outs, dag ins, string asmstr,
                                list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 1;
  let Itinerary = Slot0_LS;
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<26> reserved = 0;     // [37:12] unused
  bits<4> rt;                // DR64 dest (HIGHEST)
  bits<4> off;               // GPR32 offset register (mid)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, rt, off, rs};  // s0[47:0]
}

// LS S_ST_RR_OFF in S0 (ST32_REG): rt(GPR32 SOURCE), rs(GPR32 base), off(GPR32).
// mayStore=1 — see HaydnFU_LS_S0_S_LD_RI16 comment (store counterpart).
class HaydnFU_LS_S0_S_ST_RR_OFF<bits<7> opc, dag outs, dag ins, string asmstr,
                                list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 1;
  let Itinerary = Slot0_LS;
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<26> reserved = 0;     // [37:12] unused
  bits<4> rt;                // GPR32 source (HIGHEST)
  bits<4> off;               // GPR32 offset register (mid)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, rt, off, rs};  // s0[47:0]
}

// LS D_ST_RR_OFF in S0 (ST64_REG): rt(DR64 SOURCE), rs(GPR32 base), off(GPR32).
// mayStore=1 — see HaydnFU_LS_S0_S_LD_RI16 comment (store counterpart).
class HaydnFU_LS_S0_D_ST_RR_OFF<bits<7> opc, dag outs, dag ins, string asmstr,
                                list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let mayLoad = 0;
  let mayStore = 1;
  let hasSideEffects = 1;
  let Itinerary = Slot0_LS;
  bits<3> FU = 0b001;        // LS
  bits<7> opcode = opc;
  bits<26> reserved = 0;     // [37:12] unused
  bits<4> rt;                // DR64 source (HIGHEST)
  bits<4> off;               // GPR32 offset register (mid)
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, rt, off, rs};  // s0[47:0]
}

// *_REG_M0S0LS FieldSlots retired (R13). Occupancy Mask 0x1 stays;
// Fallback is 0. Placement uses Format E WITH_REG e0 members.

