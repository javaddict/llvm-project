// ===-- HaydnFormatsLD.td - LD Flex slot formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// fresh per-slot port of ALL Load-unit (LD) ops from the spec
// (golden/slot1_load_unit_instruction_list.json, 56 codepoints).
// Authored per-slot (one def per op per legal slot) to future-proof against
// per-slot format divergence. NOT derived from legacy HaydnInstrInfo*.td
// haydn_instruction_db.json, or stale HaydnFlexLayout -- fresh golden port.
//
// LD is the slot-1 dedicated Load unit (distinct from LS = slot-0
// load_and_store). The golden LD op set is the LOAD-ONLY subset of the
// load/store family: D_LDW / S_LW / D_LW / S_LHWS / D_LHW / S_LHWU / S_LBS
// S_LBU (POST/WITH/PRE x IMM/REG) + BREV loads (D_LDW_BREV_IMM/REG
// S_LW_BREV_IMM/REG) + AR-type (PLDWWUA, D_LQHWUA_POST, D_LTWUA_POST, FLAR).
// NO store ops (those live in the LS FU).
//
// Encoding model (encoding_manual_flex.md section 1.2):
// LD FU code = 0b011, opcode width = 6 bits.
// s0[47:45]=FU(011) s0[44:39]=opcode(6b) operands + reserved fill [38:0]
// s1[39:37]=FU(011) s1[36:31]=opcode(6b) operands + reserved fill [30:0]
// s2[39:37]=FU(011) s2[36:31]=opcode(6b) operands + reserved fill [30:0]
//
// Field-order convention (mirrors proven HaydnFormatsLS.td template):
// MSB-first concat = { FU, opcode, reserved, <operand fields high->low> }
// Register operand block: rt/rtd HIGHEST, rs2 mid, rs/rs1 LOWEST.
// Imm fields sit within the operand block above the lowest register.
//
// Register banks (per golden DR_Read_Port/DR_Write_Port vs GPR_Read/Write):
// D_* loads (D_LDW/D_LHW): DR64 dest `rtd` (DR_Write_Port), GPR32 base `rs`.
// S_* loads (S_LW/S_LHWS/S_LHWU/S_LBS/S_LBU): GPR32 dest `rt`, base `rs`.
// BREV: bit-reversed addressing; same dest/source shape as the base LD op.
// AR: PLDWWUA/FLAR (no DR/GPR data reg); D_LQHWUA_POST/D_LTWUA_POST (DR64
// dest + ar_sel + rs1 + rs2 + dir_sel).
//
// Legal slots (section 1.2 + user revision): LD is dynamically
// dispatchable in ALL 3 slots (s0/s1/s2-legal=56). Every LD op gets 3 defs.
//
// Naming: LD op mnemonics in golden (e.g. D_LDW_POST_IMM) are identical to the
// LS op names -- they are the same ISA ops, just re-encoded under the LD FU.
// To avoid tablegen def collisions with the LS file (which owns
// `<OP>_S{0,1,2}`), LD defs are named `<OP>_LD_S{0,1,2}`. The `_LD_`
// infix mirrors the `HaydnFU_LD_*` class-name prefix and the FU discriminator.
//
// Additive only: does NOT touch HaydnSlots.td, Haydn.td include wiring
// CodeGenFormat.cpp, or the other HaydnFormats*.td files.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand dag convention (driven by golden GPR/DR Read/Write port lists):
//
// D_LD_*_IMM (outs DR64:$rtd) (ins GPR32:$rs, simm6:$imm6)
// D_LD_*_REG (outs DR64:$rtd) (ins GPR32:$rs1, GPR32:$rs2)
// D_LD_*_POST/PRE_IMM (outs DR64:$rtd, GPR32:$rs) (ins simm6:$imm6)
// ^-- golden lists rs in BOTH GPR_Read_Port and GPR_Write_Port for PRE/POST
// forms: rs is read (base) and written (updated). The updated value is
// modeled as an OUT tied to an IN ($rs and $rs_in share the same reg
// via tied-use). For the format-class declaration we list the OUT only;
// the tied-use Constraint is a CodeGen concern and is NOT added at the
// format layer.
// D_LD_*_POST/PRE_REG (outs DR64:$rtd, GPR32:$rs1) (ins GPR32:$rs2)
//
// S_LD_* (same pattern with GPR32:$rt dest in place of DR64:$rtd)
//
// BREV_* -- bit-reversed LD: same dest/source shape as the base LD op.
// AR: PLDWWUA (outs)(ins GPR32:$rs, uimm2:$ar_sel);
// FLAR (outs)(ins uimm2:$ar_sel);
// D_LQHWUA_POST/D_LTWUA_POST
// (outs DR64:$rtd, GPR32:$rs1)
// (ins GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// S0 (48-bit window) LD format classes -- one per operand shape.
// Payload budget after FU(3) + opcode(6) = 39 bits.
//===----------------------------------------------------------------------===//

// LD D_LD_RI6 in S0: rtd(DR64 dest), rs(GPR32 base), imm6.
// Used by D_LD_*_WITH_IMM (no base writeback): no rs OUT.
// For D_LD_*_POST/PRE_IMM, rs is written back (OUT rs).
// Payload 3+6+25+6+4+4 = 48b.
class HaydnFU_LD_S0_D_LD_RI6<bits<6> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<25> reserved = 0;     // [38:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 dest (HIGHEST)
  let s0 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s0[47:0]
}

// LD S_LD_RI6 in S0: rt(GPR32 dest), rs(GPR32 base), imm6.
// Used by S_LD_*_WITH_IMM (no base writeback).
// For S_LD_*_POST/PRE_IMM, rs is written back (OUT rs).
// Payload 3+6+25+6+4+4 = 48b.
class HaydnFU_LD_S0_S_LD_RI6<bits<6> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<25> reserved = 0;     // [38:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 dest (HIGHEST)
  let s0 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s0[47:0]
}

// LD D_LD_RR in S0: rtd(DR64 dest), rs1, rs2 (GPR32).
// Used by D_LD_*_WITH_REG (no base writeback).
// Payload 3+6+27+4+4+4 = 48b.
class HaydnFU_LD_S0_D_LD_RR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<27> reserved = 0;     // [38:12] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s0 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s0[47:0]
}

// LD S_LD_RR in S0: rt(GPR32 dest), rs1, rs2 (GPR32).
// Used by S_LD_*_WITH_REG (no base writeback).
// Payload 3+6+27+4+4+4 = 48b.
class HaydnFU_LD_S0_S_LD_RR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<27> reserved = 0;     // [38:12] unused
  bits<4> rt;                // GPR32 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s0 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s0[47:0]
}

// LD PLDWWUA in S0 (AR type): ar_sel(uimm2), rs(GPR32). Pre-load AR.
// Payload 3+6+33+2+4 = 48b.
class HaydnFU_LD_S0_AR_PLDW<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<33> reserved = 0;     // [38:6] unused
  bits<2> ar_sel;
  bits<4> rs;                // GPR32 base (LOWEST)
  let s0 = {FU, opcode, reserved, ar_sel, rs};     // MSB-first -> s0[47:0]
}

// LD FLAR in S0 (AR type): ar_sel(uimm2). Flush AR.
// Payload 3+6+37+2 = 48b.
class HaydnFU_LD_S0_AR_FLAR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<37> reserved = 0;     // [38:2] unused
  bits<2> ar_sel;
  let s0 = {FU, opcode, reserved, ar_sel};        // MSB-first -> s0[47:0]
}

// LD D_LQHWUA_POST / D_LTWUA_POST in S0 (AR type): rtd(DR64 dest)
// ar_sel(uimm2), rs1, rs2 (GPR32), dir_sel(uimm1).
// Payload 3+6+21+4+2+4+4+1+3 = 48b (pad0 fills the last 3 bits).
class HaydnFU_LD_S0_AR_UA<bits<6> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_LS;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<21> reserved = 0;     // [38:18] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 48b
  let s0 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s0[47:0]
}

//===----------------------------------------------------------------------===//
// S1 / S2 (40-bit window) LD format classes -- one per operand shape.
// Payload budget after FU(3) + opcode(6) = 31 bits.
//===----------------------------------------------------------------------===//

// LD D_LD_RI6 in S1: rtd(DR64 dest), rs(GPR32 base), imm6.
// Payload 3+6+17+6+4+4 = 40b.
class HaydnFU_LD_S1_D_LD_RI6<bits<6> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<17> reserved = 0;     // [30:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 dest (HIGHEST)
  let s1 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s1[39:0]
}

// LD S_LD_RI6 in S1: rt(GPR32 dest), rs(GPR32 base), imm6.
// Payload 3+6+17+6+4+4 = 40b.
class HaydnFU_LD_S1_S_LD_RI6<bits<6> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<17> reserved = 0;     // [30:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 dest (HIGHEST)
  let s1 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s1[39:0]
}

// LD S_LD_RI16 in S1: rt(GPR32 dest), rs(GPR32 base), offset(simm16).
// RI16 layout for the slot-1 plain-load promoted forms LD32_S1
// LD64_S1 (FmtLS<0x44>/<0x43>, simm16). The golden LD codepoints use RI6
// (imm6 byte offset); the legacy FmtLS promoted loads carry simm16, so a
// dedicated RI16 class is required. Payload 3+6+7+16+4+4 = 40b. mayLoad=1
// so leaveRegion setDesc preserves the load memory semantic.
class HaydnFU_LD_S1_S_LD_RI16<bits<6> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 1;
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<7> reserved = 0;      // [30:24] unused
  bits<16> offset;
  bits<4> rs;
  bits<4> rt;                // GPR32 dest (HIGHEST)
  let s1 = {FU, opcode, reserved, offset, rt, rs};  // MSB-first -> s1[39:0]
}

// LD D_LD_RI16 in S1: rtd(DR64 dest), rs(GPR32 base), offset(simm16).
// DR64 counterpart of S_LD_RI16 for LD64_S1. rt named per legacy LD64
// operand convention. Payload 3+6+7+16+4+4 = 40b. mayLoad=1.
class HaydnFU_LD_S1_D_LD_RI16<bits<6> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let mayLoad = 1;
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<7> reserved = 0;      // [30:24] unused
  bits<16> offset;
  bits<4> rs;
  bits<4> rt;                // DR64 dest (HIGHEST)
  let s1 = {FU, opcode, reserved, offset, rt, rs};  // MSB-first -> s1[39:0]
}

// LD D_LD_RR in S1: rtd(DR64 dest), rs1, rs2 (GPR32).
// Payload 3+6+19+4+4+4 = 40b.
class HaydnFU_LD_S1_D_LD_RR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<19> reserved = 0;     // [30:12] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s1[39:0]
}

// LD S_LD_RR in S1: rt(GPR32 dest), rs1, rs2 (GPR32).
// Payload 3+6+19+4+4+4 = 40b.
class HaydnFU_LD_S1_S_LD_RR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<19> reserved = 0;     // [30:12] unused
  bits<4> rt;                // GPR32 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s1 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s1[39:0]
}

// LD PLDWWUA in S1 (AR type): ar_sel(uimm2), rs(GPR32).
// Payload 3+6+25+2+4 = 40b.
class HaydnFU_LD_S1_AR_PLDW<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<25> reserved = 0;     // [30:6] unused
  bits<2> ar_sel;
  bits<4> rs;                // GPR32 base (LOWEST)
  let s1 = {FU, opcode, reserved, ar_sel, rs};     // MSB-first -> s1[39:0]
}

// LD FLAR in S1 (AR type): ar_sel(uimm2).
// Payload 3+6+29+2 = 40b.
class HaydnFU_LD_S1_AR_FLAR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<29> reserved = 0;     // [30:2] unused
  bits<2> ar_sel;
  let s1 = {FU, opcode, reserved, ar_sel};        // MSB-first -> s1[39:0]
}

// LD D_LQHWUA_POST / D_LTWUA_POST in S1 (AR type): rtd(DR64 dest)
// ar_sel(uimm2), rs1, rs2 (GPR32), dir_sel(uimm1).
// Payload 3+6+13+4+2+4+4+1+3 = 40b (pad0 fills the last 3 bits).
class HaydnFU_LD_S1_AR_UA<bits<6> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot1_LD;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<13> reserved = 0;     // [30:18] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 40b
  let s1 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s1[39:0]
}

// LD D_LD_RI6 in S2: rtd(DR64 dest), rs(GPR32 base), imm6. Mirrors S1.
class HaydnFU_LD_S2_D_LD_RI6<bits<6> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<17> reserved = 0;     // [30:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rtd;               // DR64 dest (HIGHEST)
  let s2 = {FU, opcode, reserved, imm6, rtd, rs};  // MSB-first -> s2[39:0]
}

// LD S_LD_RI6 in S2: rt(GPR32 dest), rs(GPR32 base), imm6. Mirrors S1.
class HaydnFU_LD_S2_S_LD_RI6<bits<6> opc, dag outs, dag ins, string asmstr,
                             list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<17> reserved = 0;     // [30:14] unused
  bits<6> imm6;
  bits<4> rs;
  bits<4> rt;                // GPR32 dest (HIGHEST)
  let s2 = {FU, opcode, reserved, imm6, rt, rs};   // MSB-first -> s2[39:0]
}

// LD D_LD_RR in S2: rtd(DR64 dest), rs1, rs2 (GPR32). Mirrors S1.
class HaydnFU_LD_S2_D_LD_RR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<19> reserved = 0;     // [30:12] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rtd, rs2, rs1};  // MSB-first -> s2[39:0]
}

// LD S_LD_RR in S2: rt(GPR32 dest), rs1, rs2 (GPR32). Mirrors S1.
class HaydnFU_LD_S2_S_LD_RR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<19> reserved = 0;     // [30:12] unused
  bits<4> rt;                // GPR32 dest (HIGHEST)
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  let s2 = {FU, opcode, reserved, rt, rs2, rs1};   // MSB-first -> s2[39:0]
}

// LD PLDWWUA in S2 (AR type): ar_sel(uimm2), rs(GPR32). Mirrors S1.
class HaydnFU_LD_S2_AR_PLDW<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<25> reserved = 0;     // [30:6] unused
  bits<2> ar_sel;
  bits<4> rs;                // GPR32 base (LOWEST)
  let s2 = {FU, opcode, reserved, ar_sel, rs};     // MSB-first -> s2[39:0]
}

// LD FLAR in S2 (AR type): ar_sel(uimm2). Mirrors S1.
class HaydnFU_LD_S2_AR_FLAR<bits<6> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<29> reserved = 0;     // [30:2] unused
  bits<2> ar_sel;
  let s2 = {FU, opcode, reserved, ar_sel};        // MSB-first -> s2[39:0]
}

// LD D_LQHWUA_POST / D_LTWUA_POST in S2 (AR type). Mirrors S1.
class HaydnFU_LD_S2_AR_UA<bits<6> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b011;        // LD
  bits<6> opcode = opc;
  bits<13> reserved = 0;     // [30:18] unused
  bits<4> rtd;               // DR64 dest (HIGHEST)
  bits<2> ar_sel;
  bits<4> rs2;
  bits<4> rs1;               // base (LOWEST)
  bits<1> dir_sel;
  bits<3> pad0 = 0;          // [2:0] fill to 40b
  let s2 = {FU, opcode, reserved, rtd, ar_sel, rs2, rs1, dir_sel, pad0};  // s2[39:0]
}

// TODO(golden) per-op field ordering: the exact MSB->LSB split of ar_sel vs
// dir_sel in the AR family is not fully specified in encoding_manual_flex.md
// section 1.2 (only FU/opcode widths are frozen). The layout above is the
// author's best-effort per operands_info.md (2-bit ar_sel, 1-bit dir_sel). P2
// will refine once the spec confirms the bit-exact field placement.

//===----------------------------------------------------------------------===//
// LD op defs -- 3 per op (s0/s1/s2). Opcodes are golden Index (6-bit, dense).
//
// POST/PRE writeback (rs/rs1 read-modify-write): the base reg is declared as
// an OUT operand only -- it owns the field bits and the read is implicit
// (standard read-modify-write encoding-layer convention). The tied-use
// Constraint is a CodeGen concern and is NOT added at the format layer.
//===----------------------------------------------------------------------===//

// #1 D_LDW_POST_IMM (RI6) -- golden Index 1.
def D_LDW_POST_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b000001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LDW_POST_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b000001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LDW_POST_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b000001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #2 D_LDW_WITH_IMM (RI6) -- golden Index 2.
def D_LDW_WITH_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b000010, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_ldw_with_imm.ld\t$rtd, $rs, $imm6", []>;
def D_LDW_WITH_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b000010, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_ldw_with_imm.ld\t$rtd, $rs, $imm6", []>;
def D_LDW_WITH_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b000010, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_ldw_with_imm.ld\t$rtd, $rs, $imm6", []>;

// #3 D_LDW_PRE_IMM (RI6) -- golden Index 3.
def D_LDW_PRE_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b000011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LDW_PRE_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b000011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LDW_PRE_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b000011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #4 D_LDW_POST_REG (RR) -- golden Index 4.
def D_LDW_POST_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b000100, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LDW_POST_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b000100, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LDW_POST_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b000100, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #5 D_LDW_WITH_REG (RR) -- golden Index 5.
def D_LDW_WITH_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b000101, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_with_reg.ld\t$rtd, $rs1, $rs2", []>;
def D_LDW_WITH_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b000101, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_with_reg.ld\t$rtd, $rs1, $rs2", []>;
def D_LDW_WITH_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b000101, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_with_reg.ld\t$rtd, $rs1, $rs2", []>;

// #6 D_LDW_PRE_REG (RR) -- golden Index 6.
def D_LDW_PRE_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b000110, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LDW_PRE_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b000110, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LDW_PRE_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b000110, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #7 S_LW_POST_IMM (RI6) -- golden Index 7.
def S_LW_POST_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b000111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LW_POST_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b000111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LW_POST_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b000111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #8 S_LW_WITH_IMM (RI6) -- golden Index 8.
def S_LW_WITH_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b001000, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lw_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LW_WITH_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b001000, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lw_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LW_WITH_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b001000, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lw_with_imm.ld\t$rt, $rs, $imm6", []>;

// #9 S_LW_PRE_IMM (RI6) -- golden Index 9.
def S_LW_PRE_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b001001, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LW_PRE_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b001001, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LW_PRE_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b001001, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #10 S_LW_POST_REG (RR) -- golden Index 10.
def S_LW_POST_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b001010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LW_POST_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b001010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LW_POST_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b001010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #11 S_LW_WITH_REG (RR) -- golden Index 11.
def S_LW_WITH_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b001011, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LW_WITH_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b001011, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LW_WITH_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b001011, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_with_reg.ld\t$rt, $rs1, $rs2", []>;

// #12 S_LW_PRE_REG (RR) -- golden Index 12.
def S_LW_PRE_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b001100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LW_PRE_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b001100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LW_PRE_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b001100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #13 D_LW_POST_IMM (RI6) -- golden Index 13.
def D_LW_POST_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b001101, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LW_POST_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b001101, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LW_POST_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b001101, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #14 D_LW_WITH_IMM (RI6) -- golden Index 14.
def D_LW_WITH_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b001110, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_lw_with_imm.ld\t$rtd, $rs, $imm6", []>;
def D_LW_WITH_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b001110, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_lw_with_imm.ld\t$rtd, $rs, $imm6", []>;
def D_LW_WITH_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b001110, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_lw_with_imm.ld\t$rtd, $rs, $imm6", []>;

// #15 D_LW_PRE_IMM (RI6) -- golden Index 15.
def D_LW_PRE_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b001111, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LW_PRE_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b001111, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LW_PRE_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b001111, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #16 D_LW_POST_REG (RR) -- golden Index 16.
def D_LW_POST_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b010000, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LW_POST_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b010000, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LW_POST_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b010000, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #17 D_LW_WITH_REG (RR) -- golden Index 17.
def D_LW_WITH_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b010001, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_with_reg.ld\t$rtd, $rs1, $rs2", []>;
def D_LW_WITH_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b010001, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_with_reg.ld\t$rtd, $rs1, $rs2", []>;
def D_LW_WITH_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b010001, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_with_reg.ld\t$rtd, $rs1, $rs2", []>;

// #18 D_LW_PRE_REG (RR) -- golden Index 18.
def D_LW_PRE_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b010010, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LW_PRE_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b010010, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LW_PRE_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b010010, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #19 S_LHWS_POST_IMM (RI6) -- golden Index 19.
def S_LHWS_POST_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b010011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhws_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LHWS_POST_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b010011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhws_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LHWS_POST_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b010011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhws_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #20 S_LHWS_WITH_IMM (RI6) -- golden Index 20.
def S_LHWS_WITH_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b010100, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lhws_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LHWS_WITH_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b010100, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lhws_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LHWS_WITH_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b010100, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lhws_with_imm.ld\t$rt, $rs, $imm6", []>;

// #21 S_LHWS_PRE_IMM (RI6) -- golden Index 21.
def S_LHWS_PRE_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b010101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhws_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LHWS_PRE_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b010101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhws_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LHWS_PRE_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b010101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhws_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #22 S_LHWS_POST_REG (RR) -- golden Index 22.
def S_LHWS_POST_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b010110, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LHWS_POST_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b010110, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LHWS_POST_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b010110, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #23 S_LHWS_WITH_REG (RR) -- golden Index 23.
def S_LHWS_WITH_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b010111, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LHWS_WITH_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b010111, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LHWS_WITH_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b010111, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_with_reg.ld\t$rt, $rs1, $rs2", []>;

// #24 S_LHWS_PRE_REG (RR) -- golden Index 24.
def S_LHWS_PRE_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b011000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LHWS_PRE_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b011000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LHWS_PRE_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b011000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhws_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #25 D_LHW_POST_IMM (RI6) -- golden Index 25.
def D_LHW_POST_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b011001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lhw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LHW_POST_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b011001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lhw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LHW_POST_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b011001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lhw_post_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #26 D_LHW_WITH_IMM (RI6) -- golden Index 26.
def D_LHW_WITH_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b011010, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_lhw_with_imm.ld\t$rtd, $rs, $imm6", []>;
def D_LHW_WITH_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b011010, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_lhw_with_imm.ld\t$rtd, $rs, $imm6", []>;
def D_LHW_WITH_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b011010, (outs DR64:$rtd), (ins GPR32:$rs, simm6:$imm6), "d_lhw_with_imm.ld\t$rtd, $rs, $imm6", []>;

// #27 D_LHW_PRE_IMM (RI6) -- golden Index 27.
def D_LHW_PRE_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b011011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lhw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LHW_PRE_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b011011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lhw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LHW_PRE_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b011011, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_lhw_pre_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #28 D_LHW_POST_REG (RR) -- golden Index 28.
def D_LHW_POST_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b011100, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LHW_POST_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b011100, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LHW_POST_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b011100, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_post_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #29 D_LHW_WITH_REG (RR) -- golden Index 29.
def D_LHW_WITH_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b011101, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_with_reg.ld\t$rtd, $rs1, $rs2", []>;
def D_LHW_WITH_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b011101, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_with_reg.ld\t$rtd, $rs1, $rs2", []>;
def D_LHW_WITH_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b011101, (outs DR64:$rtd), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_with_reg.ld\t$rtd, $rs1, $rs2", []>;

// #30 D_LHW_PRE_REG (RR) -- golden Index 30.
def D_LHW_PRE_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b011110, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LHW_PRE_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b011110, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LHW_PRE_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b011110, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_lhw_pre_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #31 S_LHWU_POST_IMM (RI6) -- golden Index 31.
def S_LHWU_POST_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b011111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LHWU_POST_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b011111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LHWU_POST_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b011111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #32 S_LHWU_WITH_IMM (RI6) -- golden Index 32.
def S_LHWU_WITH_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b100000, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LHWU_WITH_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b100000, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LHWU_WITH_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b100000, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_with_imm.ld\t$rt, $rs, $imm6", []>;

// #33 S_LHWU_PRE_IMM (RI6) -- golden Index 33.
def S_LHWU_PRE_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b100001, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LHWU_PRE_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b100001, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LHWU_PRE_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b100001, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lhwu_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #34 S_LHWU_POST_REG (RR) -- golden Index 34.
def S_LHWU_POST_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b100010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LHWU_POST_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b100010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LHWU_POST_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b100010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #35 S_LHWU_WITH_REG (RR) -- golden Index 35.
def S_LHWU_WITH_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b100011, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LHWU_WITH_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b100011, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LHWU_WITH_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b100011, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_with_reg.ld\t$rt, $rs1, $rs2", []>;

// #36 S_LHWU_PRE_REG (RR) -- golden Index 36.
def S_LHWU_PRE_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b100100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LHWU_PRE_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b100100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LHWU_PRE_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b100100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lhwu_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #37 S_LBS_POST_IMM (RI6) -- golden Index 37.
def S_LBS_POST_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b100101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbs_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LBS_POST_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b100101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbs_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LBS_POST_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b100101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbs_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #38 S_LBS_WITH_IMM (RI6) -- golden Index 38.
def S_LBS_WITH_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b100110, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lbs_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LBS_WITH_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b100110, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lbs_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LBS_WITH_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b100110, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lbs_with_imm.ld\t$rt, $rs, $imm6", []>;

// #39 S_LBS_PRE_IMM (RI6) -- golden Index 39.
def S_LBS_PRE_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b100111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbs_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LBS_PRE_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b100111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbs_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LBS_PRE_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b100111, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbs_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #40 S_LBS_POST_REG (RR) -- golden Index 40.
def S_LBS_POST_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b101000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LBS_POST_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b101000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LBS_POST_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b101000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #41 S_LBS_WITH_REG (RR) -- golden Index 41.
def S_LBS_WITH_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b101001, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LBS_WITH_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b101001, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LBS_WITH_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b101001, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_with_reg.ld\t$rt, $rs1, $rs2", []>;

// #42 S_LBS_PRE_REG (RR) -- golden Index 42.
def S_LBS_PRE_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b101010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LBS_PRE_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b101010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LBS_PRE_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b101010, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbs_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #43 S_LBU_POST_IMM (RI6) -- golden Index 43.
def S_LBU_POST_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b101011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbu_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LBU_POST_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b101011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbu_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LBU_POST_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b101011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbu_post_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #44 S_LBU_WITH_IMM (RI6) -- golden Index 44.
def S_LBU_WITH_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b101100, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lbu_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LBU_WITH_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b101100, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lbu_with_imm.ld\t$rt, $rs, $imm6", []>;
def S_LBU_WITH_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b101100, (outs GPR32:$rt), (ins GPR32:$rs, simm6:$imm6), "s_lbu_with_imm.ld\t$rt, $rs, $imm6", []>;

// #45 S_LBU_PRE_IMM (RI6) -- golden Index 45.
def S_LBU_PRE_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b101101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbu_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LBU_PRE_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b101101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbu_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LBU_PRE_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b101101, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lbu_pre_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #46 S_LBU_POST_REG (RR) -- golden Index 46.
def S_LBU_POST_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b101110, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LBU_POST_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b101110, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LBU_POST_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b101110, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_post_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #47 S_LBU_WITH_REG (RR) -- golden Index 47.
def S_LBU_WITH_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b101111, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LBU_WITH_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b101111, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_with_reg.ld\t$rt, $rs1, $rs2", []>;
def S_LBU_WITH_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b101111, (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_with_reg.ld\t$rt, $rs1, $rs2", []>;

// #48 S_LBU_PRE_REG (RR) -- golden Index 48.
def S_LBU_PRE_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b110000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LBU_PRE_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b110000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LBU_PRE_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b110000, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lbu_pre_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #49 D_LDW_BREV_IMM (RI6) -- golden Index 49.
// Tied POST-inc shape matching logical D_LDW_BREV_IMM and
// LS D_LDW_BREV_IMM_S* / D_LDW_POST_IMM_LD_S* (outs rtd,rs_wb + ins rs,imm6).
// Alts use LS _S* members; post-RA setDesc commits the chosen member.
def D_LDW_BREV_IMM_LD_S0 : HaydnFU_LD_S0_D_LD_RI6<0b110001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_brev_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LDW_BREV_IMM_LD_S1 : HaydnFU_LD_S1_D_LD_RI6<0b110001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_brev_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def D_LDW_BREV_IMM_LD_S2 : HaydnFU_LD_S2_D_LD_RI6<0b110001, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "d_ldw_brev_imm.ld\t$rtd, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #50 D_LDW_BREV_REG (RR) -- golden Index 50.
// Tied POST-inc shape matching logical / LS D_LDW_BREV_REG_S*.
def D_LDW_BREV_REG_LD_S0 : HaydnFU_LD_S0_D_LD_RR<0b110010, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_brev_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LDW_BREV_REG_LD_S1 : HaydnFU_LD_S1_D_LD_RR<0b110010, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_brev_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def D_LDW_BREV_REG_LD_S2 : HaydnFU_LD_S2_D_LD_RR<0b110010, (outs DR64:$rtd, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "d_ldw_brev_reg.ld\t$rtd, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #51 S_LW_BREV_IMM (RI6) -- golden Index 51.
// Tied POST-inc shape matching logical / LS S_LW_BREV_IMM_S*.
def S_LW_BREV_IMM_LD_S0 : HaydnFU_LD_S0_S_LD_RI6<0b110011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_brev_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LW_BREV_IMM_LD_S1 : HaydnFU_LD_S1_S_LD_RI6<0b110011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_brev_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}
def S_LW_BREV_IMM_LD_S2 : HaydnFU_LD_S2_S_LD_RI6<0b110011, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$imm6), "s_lw_brev_imm.ld\t$rt, $rs, $imm6", []> {
  let Constraints = "$rs = $rs_wb";
}

// #52 S_LW_BREV_REG (RR) -- golden Index 52.
// Tied POST-inc shape matching logical / LS S_LW_BREV_REG_S*.
def S_LW_BREV_REG_LD_S0 : HaydnFU_LD_S0_S_LD_RR<0b110100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_brev_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LW_BREV_REG_LD_S1 : HaydnFU_LD_S1_S_LD_RR<0b110100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_brev_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}
def S_LW_BREV_REG_LD_S2 : HaydnFU_LD_S2_S_LD_RR<0b110100, (outs GPR32:$rt, GPR32:$rs_wb), (ins GPR32:$rs1, GPR32:$rs2), "s_lw_brev_reg.ld\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rs1 = $rs_wb";
}

// #53 PLDWWUA (AR) -- golden Index 53.
def PLDWWUA_LD_S0 : HaydnFU_LD_S0_AR_PLDW<0b110101, (outs), (ins GPR32:$rs, uimm2:$ar_sel), "pldwwua.ld\t$ar_sel, $rs", []>;
def PLDWWUA_LD_S1 : HaydnFU_LD_S1_AR_PLDW<0b110101, (outs), (ins GPR32:$rs, uimm2:$ar_sel), "pldwwua.ld\t$ar_sel, $rs", []>;
def PLDWWUA_LD_S2 : HaydnFU_LD_S2_AR_PLDW<0b110101, (outs), (ins GPR32:$rs, uimm2:$ar_sel), "pldwwua.ld\t$ar_sel, $rs", []>;

// #54 D_LQHWUA_POST (AR) -- golden Index 54.
def D_LQHWUA_POST_LD_S0 : HaydnFU_LD_S0_AR_UA<0b110110, (outs DR64:$rtd, GPR32:$rs1), (ins GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_lqhwua_post.ld\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []>;
def D_LQHWUA_POST_LD_S1 : HaydnFU_LD_S1_AR_UA<0b110110, (outs DR64:$rtd, GPR32:$rs1), (ins GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_lqhwua_post.ld\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []>;
def D_LQHWUA_POST_LD_S2 : HaydnFU_LD_S2_AR_UA<0b110110, (outs DR64:$rtd, GPR32:$rs1), (ins GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_lqhwua_post.ld\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []>;

// #55 D_LTWUA_POST (AR) -- golden Index 55.
def D_LTWUA_POST_LD_S0 : HaydnFU_LD_S0_AR_UA<0b110111, (outs DR64:$rtd, GPR32:$rs1), (ins GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_ltwua_post.ld\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []>;
def D_LTWUA_POST_LD_S1 : HaydnFU_LD_S1_AR_UA<0b110111, (outs DR64:$rtd, GPR32:$rs1), (ins GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_ltwua_post.ld\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []>;
def D_LTWUA_POST_LD_S2 : HaydnFU_LD_S2_AR_UA<0b110111, (outs DR64:$rtd, GPR32:$rs1), (ins GPR32:$rs2, uimm2:$ar_sel, uimm1:$dir_sel), "d_ltwua_post.ld\t$rtd, $ar_sel, $rs1, $rs2, $dir_sel", []>;

// #56 FLAR (AR) -- golden Index 56.
def FLAR_LD_S0 : HaydnFU_LD_S0_AR_FLAR<0b111000, (outs), (ins uimm2:$ar_sel), "flar.ld\t$ar_sel", []>;
def FLAR_LD_S1 : HaydnFU_LD_S1_AR_FLAR<0b111000, (outs), (ins uimm2:$ar_sel), "flar.ld\t$ar_sel", []>;
def FLAR_LD_S2 : HaydnFU_LD_S2_AR_FLAR<0b111000, (outs), (ins uimm2:$ar_sel), "flar.ld\t$ar_sel", []>;

//===----------------------------------------------------------------------===//
// Family 7 — S1 encode peers for logical LD32 / LD64.
//
// Format-first single authority: MIR holds only LD32/LD64; AltDescs
// record slot; leaveRegion setDesc commits these Bundle128 members
// LD32 → { LD32_S0, LD32_S1, 0 }
// LD64 → { LD64_S0, LD64_S1, 0 }
// No intermediate "logical twin" opcode. Public mnemonic is always ld32/ld64
// (bundle position is the slot). DISTINCT codepoints 62-63. FlexEmitterOnly
// keeps them out of the decoder trie; encode is Desc-as-is after setDesc.
// mayLoad=1 at class level.
//===----------------------------------------------------------------------===//

def LD32_S1 : HaydnFU_LD_S1_S_LD_RI16<0b111110,
    (outs GPR32:$rt), (ins GPR32:$rs, simm16:$offset),
    "ld32\t$rt, $rs, $offset", []>;

def LD64_S1 : HaydnFU_LD_S1_D_LD_RI16<0b111111,
    (outs DR64:$rt), (ins GPR32:$rs, simm16:$offset),
    "ld64\t$rt, $rs, $offset", []>;
