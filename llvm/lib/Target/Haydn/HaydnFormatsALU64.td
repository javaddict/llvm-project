// ===-- HaydnFormatsALU64.td - CodeGenFormat ALU64 slot -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: license information.
//
// fresh per-slot port of ALL ALU64 ops (FU=0b010, DR64-bank datapath).
// Source of truth: golden/slot1_alu_instruction_list.json (D-ALU unit, 155 ops).
// NOT derived from legacy HaydnInstrInfo*.td. Product encode is Format E
// (golden/format_e_bit_layout_v2). The 8-bit opcode is the golden
// Index field (dense codepoint assignment); FU=0b010 discriminates ALU64 in
// each slot's MSB.
//
// Encoding model:
// ALU64 FU code = 0b010, opcode width = 8 bits.
// s0[47:45]=FU(010) s0[44:37]=opcode(8b) operands + reserved fill [36:0]
// s1[39:37]=FU(010) s1[36:29]=opcode(8b) operands + reserved fill [28:0]
// s2[39:37]=FU(010) s2[36:29]=opcode(8b) operands + reserved fill [28:0]
//
// Field-order convention (mirrors HaydnFormatsALU32.td proven template):
// MSB-first concat = { FU, opcode, reserved, <operand fields high->low> }
// Register operand block: rd/rt HIGHEST, rs1 LOWEST (rs2 mid for 3-op shapes).
// Imm fields sit within the operand block above the lowest register.
//
// Register banks (per golden DR_Read_Port/DR_Write_Port vs GPR_*):
// DR64 (D0-D15) for D-ALU 64-bit operands; GPR32 (R0-R15) for the few ops
// that mix a GPR (shift-by-GPR SRA64 family, SEXT32T64/TRANSF64, LOG2/EXP2
// RECIP/SQRT, SIN_COS, X4SEL16, MOVESFR2GPR/MOVEGPR2SFR, CSRR/CSRW).
//
// Legal slots (ISA legality, supersedes the old §1.3 over-generation):
// ALU64 (D-ALU 64-bit datapath) is Slot 1/2 ONLY (CLAUDE.md "Slot
// architecture": Slot 0 = 32-bit scalar ALU + Load/Store, no D-ALU64).
// Every ALU64 op emits exactly 2 defs (_S1/_S2); the mechanically
// over-generated _S0 variants and orphan HaydnFU_ALU64_S0_* format
// classes were deleted.
// MOVEI_H / MOVEI_L (I32, 32-bit imm > 29b payload) occupy residual
// S0 only; FieldSlots retired — Format E E2 e0 members carry placement.
// CSRR / CSRW (system CSR access) -> s0 ONLY (FU=ALU32; no _S0 def
// exists in this file -- the pre-existing orphan S0_I8_CSRR/CSRW classes
// remain pending a future CSR port).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Shared immediate operands not in HaydnInstrFormats.td.
// uimm4/5/6/8, simm7, simm32_movei already defined upstream.
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//

// S0 (48-bit window) ALU64 format classes -- one per operand shape.

//===----------------------------------------------------------------------===//


// ALU64 RR_DDD in S1: 3 DR64 (rtd, rsd1, rsd2).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S1_RR_DDD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;
  let s1 = {FU, opcode, reserved, rd, rs2, rs1};  // MSB-first -> s1[39:0]
}

// ALU64 RR_DDD in S2: 3 DR64 (rtd, rsd1, rsd2).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S2_RR_DDD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;
  let s2 = {FU, opcode, reserved, rd, rs2, rs1};  // MSB-first -> s2[39:0]
}

// ALU64 RR_DDG in S1: 2 DR64 + 1 GPR32 (rtd, rsd, rs -- shift-by-GPR).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S1_RR_DDG<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  bits<4> rs;
  let s1 = {FU, opcode, reserved, rd, rs, rsd};  // MSB-first -> s1[39:0]
}

// ALU64 RR_DDG in S2: 2 DR64 + 1 GPR32 (rtd, rsd, rs -- shift-by-GPR).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S2_RR_DDG<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  bits<4> rs;
  let s2 = {FU, opcode, reserved, rd, rs, rsd};  // MSB-first -> s2[39:0]
}

// ALU64 R_DD in S1: 2 DR64 (rtd, rsd).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S1_R_DD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  let s1 = {FU, opcode, reserved, rd, rsd};  // MSB-first -> s1[39:0]
}

// ALU64 R_DD in S2: 2 DR64 (rtd, rsd).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S2_R_DD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  let s2 = {FU, opcode, reserved, rd, rsd};  // MSB-first -> s2[39:0]
}

// ALU64 R_GD in S1: 1 GPR32(out) + 1 DR64(in) (rt, rsd -- NSA/POPCOUNT/HMAX).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S1_R_GD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rt;
  bits<4> rsd;
  let s1 = {FU, opcode, reserved, rt, rsd};  // MSB-first -> s1[39:0]
}

// ALU64 R_GD in S2: 1 GPR32(out) + 1 DR64(in) (rt, rsd -- NSA/POPCOUNT/HMAX).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S2_R_GD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rt;
  bits<4> rsd;
  let s2 = {FU, opcode, reserved, rt, rsd};  // MSB-first -> s2[39:0]
}

// ALU64 R_DG in S1: 1 DR64(out) + 1 GPR32(in) (rtd, rs -- SEXT/TRANSF).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S1_R_DG<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rd;
  bits<4> rs;
  let s1 = {FU, opcode, reserved, rd, rs};  // MSB-first -> s1[39:0]
}

// ALU64 R_DG in S2: 1 DR64(out) + 1 GPR32(in) (rtd, rs -- SEXT/TRANSF).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S2_R_DG<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rd;
  bits<4> rs;
  let s2 = {FU, opcode, reserved, rd, rs};  // MSB-first -> s2[39:0]
}

// ALU64 R_GG in S1: 2 GPR32 (rt, rs -- LOG2/EXP2/RECIP/SQRT).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S1_R_GG<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rt;
  bits<4> rs;
  let s1 = {FU, opcode, reserved, rt, rs};  // MSB-first -> s1[39:0]
}

// ALU64 R_GG in S2: 2 GPR32 (rt, rs -- LOG2/EXP2/RECIP/SQRT).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S2_R_GG<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rt;
  bits<4> rs;
  let s2 = {FU, opcode, reserved, rt, rs};  // MSB-first -> s2[39:0]
}

// ALU64 R_CMP in S1: 2 DR64(in) -> SFR(out) (rsd1, rsd2 -- SLT/SLE/SEQ).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S1_R_CMP<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rs1;
  bits<4> rs2;
  let s1 = {FU, opcode, reserved, rs1, rs2};  // MSB-first -> s1[39:0]
}

// ALU64 R_CMP in S2: 2 DR64(in) -> SFR(out) (rsd1, rsd2 -- SLT/SLE/SEQ).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S2_R_CMP<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rs1;
  bits<4> rs2;
  let s2 = {FU, opcode, reserved, rs1, rs2};  // MSB-first -> s2[39:0]
}

// ALU64 R_COND in S1: 1 DR64(out) + 1 DR64(in) | SFR(in) (rtd, rsd -- MOVT/MOVF).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S1_R_COND<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  let s1 = {FU, opcode, reserved, rd, rsd};  // MSB-first -> s1[39:0]
}

// ALU64 R_COND in S2: 1 DR64(out) + 1 DR64(in) | SFR(in) (rtd, rsd -- MOVT/MOVF).
// Payload 3(FU)+8(opcode)+21(reserved)+8(ops) = 40b.
class HaydnFU_ALU64_S2_R_COND<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<21> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  let s2 = {FU, opcode, reserved, rd, rsd};  // MSB-first -> s2[39:0]
}

// ALU64 RI6 in S1: 2 DR64 + uimm6 (rtd, rsd, uimm6).
// Payload 3(FU)+8(opcode)+15(reserved)+14(ops) = 40b.
class HaydnFU_ALU64_S1_RI6<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<15> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  bits<6> imm6;
  let s1 = {FU, opcode, reserved, rd, imm6, rsd};  // MSB-first -> s1[39:0]
}

// ALU64 RI6 in S2: 2 DR64 + uimm6 (rtd, rsd, uimm6).
// Payload 3(FU)+8(opcode)+15(reserved)+14(ops) = 40b.
class HaydnFU_ALU64_S2_RI6<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<15> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  bits<6> imm6;
  let s2 = {FU, opcode, reserved, rd, imm6, rsd};  // MSB-first -> s2[39:0]
}

// ALU64 RI5 in S1: 2 DR64 + uimm5 (rtd, rsd, uimm5).
// Payload 3(FU)+8(opcode)+16(reserved)+13(ops) = 40b.
class HaydnFU_ALU64_S1_RI5<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<16> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  bits<5> imm5;
  let s1 = {FU, opcode, reserved, rd, imm5, rsd};  // MSB-first -> s1[39:0]
}

// ALU64 RI5 in S2: 2 DR64 + uimm5 (rtd, rsd, uimm5).
// Payload 3(FU)+8(opcode)+16(reserved)+13(ops) = 40b.
class HaydnFU_ALU64_S2_RI5<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<16> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  bits<5> imm5;
  let s2 = {FU, opcode, reserved, rd, imm5, rsd};  // MSB-first -> s2[39:0]
}

// ALU64 RI4_DD in S1: 2 DR64 + uimm4 (rtd, rsd, uimm4).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S1_RI4_DD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  bits<4> imm4;
  let s1 = {FU, opcode, reserved, rd, imm4, rsd};  // MSB-first -> s1[39:0]
}

// ALU64 RI4_DD in S2: 2 DR64 + uimm4 (rtd, rsd, uimm4).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S2_RI4_DD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rd;
  bits<4> rsd;
  bits<4> imm4;
  let s2 = {FU, opcode, reserved, rd, imm4, rsd};  // MSB-first -> s2[39:0]
}

// ALU64 RI4_DG in S1: 1 DR64 + 1 GPR32 + uimm4 (rtd, rs, uimm4 -- SIN_COS).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S1_RI4_DG<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  // SIN_COS_S1 only. Conservative dest Data_Latency 17 (golden uimm4+2 max).
  let Itinerary = Slot1_ALU_SinCosLat;
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rd;
  bits<4> rs;
  bits<4> imm4;
  let s1 = {FU, opcode, reserved, rd, imm4, rs};  // MSB-first -> s1[39:0]
}

// ALU64 RI4_DG in S2: 1 DR64 + 1 GPR32 + uimm4 (rtd, rs, uimm4 -- SIN_COS).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S2_RI4_DG<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  // SIN_COS_S2 only. Conservative dest Data_Latency 17 (golden uimm4+2 max).
  let Itinerary = Slot2_ALU_SinCosLat;
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rd;
  bits<4> rs;
  bits<4> imm4;
  let s2 = {FU, opcode, reserved, rd, imm4, rs};  // MSB-first -> s2[39:0]
}

// ALU64 RI4_GD in S1: 1 GPR32(out) + 1 DR64(in) + uimm4 (rt, rsd, uimm4 -- ARCTAN).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S1_RI4_GD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  // ARCTAN_S1 only. Conservative dest Data_Latency 17 (golden uimm4+2 max).
  let Itinerary = Slot1_ALU_SinCosLat;
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rt;
  bits<4> rsd;
  bits<4> imm4;
  let s1 = {FU, opcode, reserved, rt, imm4, rsd};  // MSB-first -> s1[39:0]
}

// ALU64 RI4_GD in S2: 1 GPR32(out) + 1 DR64(in) + uimm4 (rt, rsd, uimm4 -- ARCTAN).
// Payload 3(FU)+8(opcode)+17(reserved)+12(ops) = 40b.
class HaydnFU_ALU64_S2_RI4_GD<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  // ARCTAN_S2 only. Conservative dest Data_Latency 17 (golden uimm4+2 max).
  let Itinerary = Slot2_ALU_SinCosLat;
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<17> reserved = 0;
  bits<4> rt;
  bits<4> rsd;
  bits<4> imm4;
  let s2 = {FU, opcode, reserved, rt, imm4, rsd};  // MSB-first -> s2[39:0]
}

// ALU64 RRI4 in S1: 3 DR64 + uimm4 (rtd, rsd1, rsd2, uimm4 -- X4SELI16).
// Payload 3(FU)+8(opcode)+13(reserved)+16(ops) = 40b.
class HaydnFU_ALU64_S1_RRI4<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<13> reserved = 0;
  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;
  bits<4> imm4;
  let s1 = {FU, opcode, reserved, rd, imm4, rs2, rs1};  // MSB-first -> s1[39:0]
}

// ALU64 RRI4 in S2: 3 DR64 + uimm4 (rtd, rsd1, rsd2, uimm4 -- X4SELI16).
// Payload 3(FU)+8(opcode)+13(reserved)+16(ops) = 40b.
class HaydnFU_ALU64_S2_RRI4<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<13> reserved = 0;
  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;
  bits<4> imm4;
  let s2 = {FU, opcode, reserved, rd, imm4, rs2, rs1};  // MSB-first -> s2[39:0]
}

// ALU64 RRR in S1: 3 DR64 + 1 GPR32 (rtd, rsd1, rsd2, rs -- X4SEL16).
// Payload 3(FU)+8(opcode)+13(reserved)+16(ops) = 40b.
class HaydnFU_ALU64_S1_RRR<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<13> reserved = 0;
  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;
  bits<4> rs;
  let s1 = {FU, opcode, reserved, rd, rs, rs2, rs1};  // MSB-first -> s1[39:0]
}

// ALU64 RRR in S2: 3 DR64 + 1 GPR32 (rtd, rsd1, rsd2, rs -- X4SEL16).
// Payload 3(FU)+8(opcode)+13(reserved)+16(ops) = 40b.
class HaydnFU_ALU64_S2_RRR<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<13> reserved = 0;
  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;
  bits<4> rs;
  let s2 = {FU, opcode, reserved, rd, rs, rs2, rs1};  // MSB-first -> s2[39:0]
}

// ALU64 I32 in S0: 1 DR64 + simm32 (rtd, imm32 -- MOVEI_H/L).
// Payload 3(FU)+8(opcode)+1(reserved)+36(ops) = 48b.
class HaydnFU_ALU64_S0_I32<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<1> reserved = 0;
  bits<4> rd;
  bits<32> imm32;
  let s0 = {FU, opcode, reserved, rd, imm32};  // MSB-first -> s0[47:0]
}

// ALU64 SFR_OUT in S1: 1 GPR32(out) | SFR(in) (rt -- MOVESFR2GPR).
// Payload 3(FU)+8(opcode)+25(reserved)+4(ops) = 40b.
class HaydnFU_ALU64_S1_SFR_OUT<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<25> reserved = 0;
  bits<4> rt;
  let s1 = {FU, opcode, reserved, rt};  // MSB-first -> s1[39:0]
}

// ALU64 SFR_OUT in S2: 1 GPR32(out) | SFR(in) (rt -- MOVESFR2GPR).
// Payload 3(FU)+8(opcode)+25(reserved)+4(ops) = 40b.
class HaydnFU_ALU64_S2_SFR_OUT<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<25> reserved = 0;
  bits<4> rt;
  let s2 = {FU, opcode, reserved, rt};  // MSB-first -> s2[39:0]
}

// ALU64 SFR_IN in S1: 1 GPR32(in) | SFR(out) (rs -- MOVEGPR2SFR).
// Payload 3(FU)+8(opcode)+25(reserved)+4(ops) = 40b.
class HaydnFU_ALU64_S1_SFR_IN<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<25> reserved = 0;
  bits<4> rs;
  let s1 = {FU, opcode, reserved, rs};  // MSB-first -> s1[39:0]
}

// ALU64 SFR_IN in S2: 1 GPR32(in) | SFR(out) (rs -- MOVEGPR2SFR).
// Payload 3(FU)+8(opcode)+25(reserved)+4(ops) = 40b.
class HaydnFU_ALU64_S2_SFR_IN<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<25> reserved = 0;
  bits<4> rs;
  let s2 = {FU, opcode, reserved, rs};  // MSB-first -> s2[39:0]
}

// ALU64 I8_CSRR in S0: 1 GPR32(out) + uimm8 (rt, uimm8 -- CSRR).
// Payload 3(FU)+8(opcode)+25(reserved)+12(ops) = 48b.
class HaydnFU_ALU64_S0_I8_CSRR<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<25> reserved = 0;
  bits<4> rt;
  bits<8> csr;
  let s0 = {FU, opcode, reserved, rt, csr};  // MSB-first -> s0[47:0]
}

// ALU64 I8_CSRW in S0: uimm8 + 1 GPR32(in) (uimm8, rs -- CSRW).
// Payload 3(FU)+8(opcode)+25(reserved)+12(ops) = 48b.
class HaydnFU_ALU64_S0_I8_CSRW<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<25> reserved = 0;
  bits<8> csr;
  bits<4> rs;
  let s0 = {FU, opcode, reserved, csr, rs};  // MSB-first -> s0[47:0]
}

// ALU64 I8_ZERODR in S1: 1 DR64(out) (rtd -- ZERO_DR).
// Payload 3(FU)+8(opcode)+25(reserved)+4(ops) = 40b.
class HaydnFU_ALU64_S1_I8_ZERODR<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<25> reserved = 0;
  bits<4> rd;
  let s1 = {FU, opcode, reserved, rd};  // MSB-first -> s1[39:0]
}

// ALU64 I8_ZERODR in S2: 1 DR64(out) (rtd -- ZERO_DR).
// Payload 3(FU)+8(opcode)+25(reserved)+4(ops) = 40b.
class HaydnFU_ALU64_S2_I8_ZERODR<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<25> reserved = 0;
  bits<4> rd;
  let s2 = {FU, opcode, reserved, rd};  // MSB-first -> s2[39:0]
}

// ALU64 I8_ZEROSFR in S1: no operands (ZERO_SFR -> SFR).
// Payload 3(FU)+8(opcode)+29(reserved)+0(ops) = 40b.
class HaydnFU_ALU64_S1_I8_ZEROSFR<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<29> reserved = 0;
  let s1 = {FU, opcode, reserved};  // MSB-first -> s1[39:0]
}

// ALU64 I8_ZEROSFR in S2: no operands (ZERO_SFR -> SFR).
// Payload 3(FU)+8(opcode)+29(reserved)+0(ops) = 40b.
class HaydnFU_ALU64_S2_I8_ZEROSFR<bits<8> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b010;       // ALU64
  bits<8> opcode = opc;
  bits<29> reserved = 0;
  let s2 = {FU, opcode, reserved};  // MSB-first -> s2[39:0]
}

//===----------------------------------------------------------------------===//
// ALU64 RR_DDD ops -- 3 DR64 (rtd, rsd1, rsd2).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 RR_DDG ops -- 2 DR64 + 1 GPR32 (rtd, rsd, rs -- shift-by-GPR).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 R_DD ops -- 2 DR64 (rtd, rsd).
//===----------------------------------------------------------------------===//

// #28 NOT64 — private encode peers for logical NOT64 (FmtALU64Unary).
// Class field is `$rsd` (HaydnFU_ALU64_*_R_DD); logical uses `$rs1`.
// Alts index materialize: AlternateInsts + leaveRegion setDesc (same ABS64/NEG64).

//===----------------------------------------------------------------------===//
// ALU64 R_GD ops -- 1 GPR32(out) + 1 DR64(in) (rt, rsd -- NSA/POPCOUNT/HMAX).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 R_DG ops -- 1 DR64(out) + 1 GPR32(in) (rtd, rs -- SEXT/TRANSF).
//===----------------------------------------------------------------------===//

// SEXT32T64 / SEXT_GPR32_TO_DR64 FieldSlots retired (R13). Occupancy
// Mask 0x2 stays; Fallback is 0. Placement uses SEXT32T64 Format E
// members (peel maps SEXT_GPR32_TO_DR64 → SEXT32T64).

//===----------------------------------------------------------------------===//
// ALU64 R_GG ops -- 2 GPR32 (rt, rs -- LOG2/EXP2/RECIP/SQRT).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 R_CMP ops -- 2 DR64(in) -> SFR(out) (rsd1, rsd2 -- SLT/SLE/SEQ).
//===----------------------------------------------------------------------===//

// SLT64 unary FieldSlots deleted (Format E ALU0/ALU1 match ISel).
// SIMD compares are SFR-only; e1 has no 0-def member, so S1 FieldSlots stay.
let Defs = [SFR] in {
def X2SLT32_S1 : HaydnFU_ALU64_S1_R_CMP<0b01011111,
    (outs), (ins DR64:$rs1, DR64:$rs2),
    "x2slt32\t$rs1, $rs2", []>;
def X2SLE32_S1 : HaydnFU_ALU64_S1_R_CMP<0b01100000,
    (outs), (ins DR64:$rs1, DR64:$rs2),
    "x2sle32\t$rs1, $rs2", []>;
def X2SEQ32_S1 : HaydnFU_ALU64_S1_R_CMP<0b01100001,
    (outs), (ins DR64:$rs1, DR64:$rs2),
    "x2seq32\t$rs1, $rs2", []>;
def X4SLT16_S1 : HaydnFU_ALU64_S1_R_CMP<0b01111100,
    (outs), (ins DR64:$rs1, DR64:$rs2),
    "x4slt16\t$rs1, $rs2", []>;
def X4SLE16_S1 : HaydnFU_ALU64_S1_R_CMP<0b01111101,
    (outs), (ins DR64:$rs1, DR64:$rs2),
    "x4sle16\t$rs1, $rs2", []>;
def X4SEQ16_S1 : HaydnFU_ALU64_S1_R_CMP<0b01111110,
    (outs), (ins DR64:$rs1, DR64:$rs2),
    "x4seq16\t$rs1, $rs2", []>;
}  // Defs = [SFR]


//===----------------------------------------------------------------------===//
// ALU64 RI5 ops -- 2 DR64 + uimm5 (rtd, rsd, uimm5).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 RI4_DD ops -- 2 DR64 + uimm4 (rtd, rsd, uimm4).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 RI4_DG ops -- 1 DR64 + 1 GPR32 + uimm4 (rtd, rs, uimm4 -- SIN_COS).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 RI4_GD ops -- 1 GPR32(out) + 1 DR64(in) + uimm4 (rt, rsd, uimm4 -- ARCTAN).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 RRI4 ops -- 3 DR64 + uimm4 (rtd, rsd1, rsd2, uimm4 -- X4SELI16).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 RRR ops -- 3 DR64 + 1 GPR32 (rtd, rsd1, rsd2, rs -- X4SEL16).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 I32 ops -- 1 DR64 + simm32 (rtd, imm32 -- MOVEI_H/L).
//===----------------------------------------------------------------------===//

// MOVEI_H / MOVEI_L FieldSlots retired (R13). Occupancy Mask 0x1 stays;
// Fallback is 0. Placement uses MOVEI_*_E2_E0_ALU0_I32.

//===----------------------------------------------------------------------===//
// ALU64 SFR_OUT ops -- 1 GPR32(out) | SFR(in) (rt -- MOVESFR2GPR).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 SFR_IN ops -- 1 GPR32(in) | SFR(out) (rs -- MOVEGPR2SFR).
//===----------------------------------------------------------------------===//


// NOTE: CSRR/CSRW (system-register access, s0-only §1.3 family) are FU=ALU32
// system ops — they are defined in HaydnFormatsALU32.td. golden lists them in
// both slot0_alu and slot1_alu, but they classify as ALU32 (FU=000), so they
// are NOT emitted here (avoids the def-already-exists collision).

//===----------------------------------------------------------------------===//
// ALU64 I8_ZERODR ops -- 1 DR64(out) (rtd -- ZERO_DR).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU64 I8_ZEROSFR ops -- no operands (ZERO_SFR -> SFR).
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// End of ALU64 fresh per-slot port.
// Total: 155 golden ops -> 457 defs (s0/s1/s2 per legal slot).
//===----------------------------------------------------------------------===//
