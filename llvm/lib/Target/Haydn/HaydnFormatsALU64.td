// ===-- HaydnFormatsALU64.td - CodeGenFormat ALU64 slot -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: license information.
//
// fresh per-slot port of ALL ALU64 ops (FU=0b010, DR64-bank datapath).
// Source of truth: golden/slot1_alu_instruction_list.json (D-ALU unit, 155 ops).
// NOT derived from legacy HaydnInstrInfo*.td, haydn_instruction_db.json, or
// stale HaydnFlexLayout -- fresh golden port. The 8-bit opcode is the golden
// Index field (dense codepoint assignment); FU=0b010 discriminates ALU64 in
// each slot's MSB (encoding_manual_flex.md §1.2).
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
// MOVEI_H / MOVEI_L (I32, 32-bit imm > 29b payload) -> s0 ONLY (kept:
// the only ISA-legal ALU64 _S0, imm does not fit the 40-bit s1/s2
// window).
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
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
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
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
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
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
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
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
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

// #1 ADD64
def ADD64_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00000001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64\t$rd, $rs1, $rs2", []>;
// #1 ADD64
def ADD64_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00000001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64\t$rd, $rs1, $rs2", []>;
// #2 ADD64_L
def ADD64_L_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00000010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64_l\t$rd, $rs1, $rs2", []>;
// #2 ADD64_L
def ADD64_L_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00000010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64_l\t$rd, $rs1, $rs2", []>;
// #3 ADD64_H
def ADD64_H_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00000011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64_h\t$rd, $rs1, $rs2", []>;
// #3 ADD64_H
def ADD64_H_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00000011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64_h\t$rd, $rs1, $rs2", []>;
// #4 ADD64S
def ADD64S_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00000100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64s\t$rd, $rs1, $rs2", []>;
// #4 ADD64S
def ADD64S_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00000100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64s\t$rd, $rs1, $rs2", []>;
// #5 ADD64S_L
def ADD64S_L_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00000101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64s_l\t$rd, $rs1, $rs2", []>;
// #5 ADD64S_L
def ADD64S_L_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00000101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64s_l\t$rd, $rs1, $rs2", []>;
// #6 ADD64S_H
def ADD64S_H_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00000110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64s_h\t$rd, $rs1, $rs2", []>;
// #6 ADD64S_H
def ADD64S_H_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00000110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "add64s_h\t$rd, $rs1, $rs2", []>;
// #7 SUB64
def SUB64_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00000111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64\t$rd, $rs1, $rs2", []>;
// #7 SUB64
def SUB64_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00000111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64\t$rd, $rs1, $rs2", []>;
// #8 SUB64_L
def SUB64_L_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00001000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64_l\t$rd, $rs1, $rs2", []>;
// #8 SUB64_L
def SUB64_L_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00001000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64_l\t$rd, $rs1, $rs2", []>;
// #9 SUB64_H
def SUB64_H_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00001001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64_h\t$rd, $rs1, $rs2", []>;
// #9 SUB64_H
def SUB64_H_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00001001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64_h\t$rd, $rs1, $rs2", []>;
// #10 SUB64S
def SUB64S_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00001010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64s\t$rd, $rs1, $rs2", []>;
// #10 SUB64S
def SUB64S_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00001010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64s\t$rd, $rs1, $rs2", []>;
// #11 SUB64S_L
def SUB64S_L_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00001011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64s_l\t$rd, $rs1, $rs2", []>;
// #11 SUB64S_L
def SUB64S_L_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00001011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64s_l\t$rd, $rs1, $rs2", []>;
// #12 SUB64S_H
def SUB64S_H_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00001100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64s_h\t$rd, $rs1, $rs2", []>;
// #12 SUB64S_H
def SUB64S_H_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00001100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "sub64s_h\t$rd, $rs1, $rs2", []>;
// #26 AND64
def AND64_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00011010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "and64\t$rd, $rs1, $rs2", []>;
// #26 AND64
def AND64_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00011010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "and64\t$rd, $rs1, $rs2", []>;
// #27 OR64
def OR64_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00011011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "or64\t$rd, $rs1, $rs2", []>;
// #27 OR64
def OR64_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00011011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "or64\t$rd, $rs1, $rs2", []>;
// #29 XOR64
def XOR64_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00011101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "xor64\t$rd, $rs1, $rs2", []>;
// #29 XOR64
def XOR64_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00011101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "xor64\t$rd, $rs1, $rs2", []>;
// #37 MAX64
def MAX64_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00100101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "max64\t$rd, $rs1, $rs2", []>;
// #37 MAX64
def MAX64_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00100101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "max64\t$rd, $rs1, $rs2", []>;
// #38 MIN64
def MIN64_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00100110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "min64\t$rd, $rs1, $rs2", []>;
// #38 MIN64
def MIN64_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00100110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "min64\t$rd, $rs1, $rs2", []>;
// #53 X2ADD32
def X2ADD32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00110101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2add32\t$rd, $rs1, $rs2", []>;
// #53 X2ADD32
def X2ADD32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00110101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2add32\t$rd, $rs1, $rs2", []>;
// #54 X2ADD32S
def X2ADD32S_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00110110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2add32s\t$rd, $rs1, $rs2", []>;
// #54 X2ADD32S
def X2ADD32S_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00110110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2add32s\t$rd, $rs1, $rs2", []>;
// #55 X2ADD32_HLLH
def X2ADD32_HLLH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00110111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2add32_hllh\t$rd, $rs1, $rs2", []>;
// #55 X2ADD32_HLLH
def X2ADD32_HLLH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00110111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2add32_hllh\t$rd, $rs1, $rs2", []>;
// #56 X2ADD32S_HLLH
def X2ADD32S_HLLH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00111000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2add32s_hllh\t$rd, $rs1, $rs2", []>;
// #56 X2ADD32S_HLLH
def X2ADD32S_HLLH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00111000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2add32s_hllh\t$rd, $rs1, $rs2", []>;
// #61 X2SUB32
def X2SUB32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00111101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sub32\t$rd, $rs1, $rs2", []>;
// #61 X2SUB32
def X2SUB32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00111101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sub32\t$rd, $rs1, $rs2", []>;
// #62 X2SUB32S
def X2SUB32S_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00111110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sub32s\t$rd, $rs1, $rs2", []>;
// #62 X2SUB32S
def X2SUB32S_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00111110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sub32s\t$rd, $rs1, $rs2", []>;
// #63 X2SUB32_HLLH
def X2SUB32_HLLH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b00111111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sub32_hllh\t$rd, $rs1, $rs2", []>;
// #63 X2SUB32_HLLH
def X2SUB32_HLLH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b00111111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sub32_hllh\t$rd, $rs1, $rs2", []>;
// #64 X2SUB32S_HLLH
def X2SUB32S_HLLH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01000000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sub32s_hllh\t$rd, $rs1, $rs2", []>;
// #64 X2SUB32S_HLLH
def X2SUB32S_HLLH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01000000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sub32s_hllh\t$rd, $rs1, $rs2", []>;
// #65 X2ADDSUB32
def X2ADDSUB32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01000001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32\t$rd, $rs1, $rs2", []>;
// #65 X2ADDSUB32
def X2ADDSUB32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01000001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32\t$rd, $rs1, $rs2", []>;
// #66 X2ADDSUB32S
def X2ADDSUB32S_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01000010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32s\t$rd, $rs1, $rs2", []>;
// #66 X2ADDSUB32S
def X2ADDSUB32S_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01000010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32s\t$rd, $rs1, $rs2", []>;
// #67 X2ADDSUB32_HLLH
def X2ADDSUB32_HLLH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01000011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32_hllh\t$rd, $rs1, $rs2", []>;
// #67 X2ADDSUB32_HLLH
def X2ADDSUB32_HLLH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01000011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32_hllh\t$rd, $rs1, $rs2", []>;
// #68 X2ADDSUB32S_HLLH
def X2ADDSUB32S_HLLH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01000100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32s_hllh\t$rd, $rs1, $rs2", []>;
// #68 X2ADDSUB32S_HLLH
def X2ADDSUB32S_HLLH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01000100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2addsub32s_hllh\t$rd, $rs1, $rs2", []>;
// #69 X2SUBADD32
def X2SUBADD32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01000101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32\t$rd, $rs1, $rs2", []>;
// #69 X2SUBADD32
def X2SUBADD32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01000101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32\t$rd, $rs1, $rs2", []>;
// #70 X2SUBADD32S
def X2SUBADD32S_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01000110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32s\t$rd, $rs1, $rs2", []>;
// #70 X2SUBADD32S
def X2SUBADD32S_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01000110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32s\t$rd, $rs1, $rs2", []>;
// #71 X2SUBADD32_HLLH
def X2SUBADD32_HLLH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01000111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32_hllh\t$rd, $rs1, $rs2", []>;
// #71 X2SUBADD32_HLLH
def X2SUBADD32_HLLH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01000111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32_hllh\t$rd, $rs1, $rs2", []>;
// #72 X2SUBADD32S_HLLH
def X2SUBADD32S_HLLH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01001000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32s_hllh\t$rd, $rs1, $rs2", []>;
// #72 X2SUBADD32S_HLLH
def X2SUBADD32S_HLLH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01001000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2subadd32s_hllh\t$rd, $rs1, $rs2", []>;
// #87 X2MAX32
def X2MAX32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01010111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2max32\t$rd, $rs1, $rs2", []>;
// #87 X2MAX32
def X2MAX32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01010111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2max32\t$rd, $rs1, $rs2", []>;
// #88 X2MIN32
def X2MIN32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01011000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2min32\t$rd, $rs1, $rs2", []>;
// #88 X2MIN32
def X2MIN32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01011000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2min32\t$rd, $rs1, $rs2", []>;
// #91 X2CLAMP32
def X2CLAMP32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01011011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2clamp32\t$rd, $rs1, $rs2", []>;
// #91 X2CLAMP32
def X2CLAMP32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01011011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2clamp32\t$rd, $rs1, $rs2", []>;
// #100 X2SEL32_LL
def X2SEL32_LL_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01100100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_ll\t$rd, $rs1, $rs2", []>;
// #100 X2SEL32_LL
def X2SEL32_LL_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01100100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_ll\t$rd, $rs1, $rs2", []>;
// #101 X2SEL32_LH
def X2SEL32_LH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01100101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_lh\t$rd, $rs1, $rs2", []>;
// #101 X2SEL32_LH
def X2SEL32_LH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01100101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_lh\t$rd, $rs1, $rs2", []>;
// #102 X2SEL32_HL
def X2SEL32_HL_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01100110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_hl\t$rd, $rs1, $rs2", []>;
// #102 X2SEL32_HL
def X2SEL32_HL_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01100110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_hl\t$rd, $rs1, $rs2", []>;
// #103 X2SEL32_HH
def X2SEL32_HH_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01100111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_hh\t$rd, $rs1, $rs2", []>;
// #103 X2SEL32_HH
def X2SEL32_HH_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01100111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sel32_hh\t$rd, $rs1, $rs2", []>;
// #104 X4ADD16
def X4ADD16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01101000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4add16\t$rd, $rs1, $rs2", []>;
// #104 X4ADD16
def X4ADD16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01101000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4add16\t$rd, $rs1, $rs2", []>;
// #105 X4ADD16S
def X4ADD16S_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01101001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4add16s\t$rd, $rs1, $rs2", []>;
// #105 X4ADD16S
def X4ADD16S_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01101001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4add16s\t$rd, $rs1, $rs2", []>;
// #106 X4SUB16
def X4SUB16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01101010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4sub16\t$rd, $rs1, $rs2", []>;
// #106 X4SUB16
def X4SUB16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01101010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4sub16\t$rd, $rs1, $rs2", []>;
// #107 X4SUB16S
def X4SUB16S_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01101011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4sub16s\t$rd, $rs1, $rs2", []>;
// #107 X4SUB16S
def X4SUB16S_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01101011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4sub16s\t$rd, $rs1, $rs2", []>;
// #115 X4MAX16
def X4MAX16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01110011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4max16\t$rd, $rs1, $rs2", []>;
// #115 X4MAX16
def X4MAX16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01110011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4max16\t$rd, $rs1, $rs2", []>;
// #116 X4MIN16
def X4MIN16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01110100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4min16\t$rd, $rs1, $rs2", []>;
// #116 X4MIN16
def X4MIN16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01110100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4min16\t$rd, $rs1, $rs2", []>;
// #119 X4CLAMP16
def X4CLAMP16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01110111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4clamp16\t$rd, $rs1, $rs2", []>;
// #119 X4CLAMP16
def X4CLAMP16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01110111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4clamp16\t$rd, $rs1, $rs2", []>;
// #139 X2FRSST32
def X2FRSST32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b10001011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2frsst32\t$rd, $rs1, $rs2", []>;
// #139 X2FRSST32
def X2FRSST32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b10001011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2frsst32\t$rd, $rs1, $rs2", []>;
// #140 X2FRST32
def X2FRST32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b10001100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2frst32\t$rd, $rs1, $rs2", []>;
// #140 X2FRST32
def X2FRST32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b10001100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2frst32\t$rd, $rs1, $rs2", []>;
// #141 X2FF2RSST32
def X2FF2RSST32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b10001101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2ff2rsst32\t$rd, $rs1, $rs2", []>;
// #141 X2FF2RSST32
def X2FF2RSST32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b10001101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2ff2rsst32\t$rd, $rs1, $rs2", []>;
// #142 X2FF2RST32
def X2FF2RST32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b10001110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2ff2rst32\t$rd, $rs1, $rs2", []>;
// #142 X2FF2RST32
def X2FF2RST32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b10001110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2ff2rst32\t$rd, $rs1, $rs2", []>;
// #143 X4FRSST16
def X4FRSST16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b10001111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4frsst16\t$rd, $rs1, $rs2", []>;
// #143 X4FRSST16
def X4FRSST16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b10001111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4frsst16\t$rd, $rs1, $rs2", []>;
// #144 X4FRST16
def X4FRST16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b10010000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4frst16\t$rd, $rs1, $rs2", []>;
// #144 X4FRST16
def X4FRST16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b10010000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4frst16\t$rd, $rs1, $rs2", []>;
// #145 X4SAT32T16
def X4SAT32T16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b10010001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4sat32t16\t$rd, $rs1, $rs2", []>;
// #145 X4SAT32T16
def X4SAT32T16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b10010001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4sat32t16\t$rd, $rs1, $rs2", []>;

//===----------------------------------------------------------------------===//
// ALU64 RR_DDG ops -- 2 DR64 + 1 GPR32 (rtd, rsd, rs -- shift-by-GPR).
//===----------------------------------------------------------------------===//

// #17 SRA64
def SRA64_S1 : HaydnFU_ALU64_S1_RR_DDG<0b00010001,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "sra64\t$rd, $rsd, $rs", []>;
// #17 SRA64
def SRA64_S2 : HaydnFU_ALU64_S2_RR_DDG<0b00010001,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "sra64\t$rd, $rsd, $rs", []>;
// #18 SRL64
def SRL64_S1 : HaydnFU_ALU64_S1_RR_DDG<0b00010010,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "srl64\t$rd, $rsd, $rs", []>;
// #18 SRL64
def SRL64_S2 : HaydnFU_ALU64_S2_RR_DDG<0b00010010,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "srl64\t$rd, $rsd, $rs", []>;
// #19 SRA64R
def SRA64R_S1 : HaydnFU_ALU64_S1_RR_DDG<0b00010011,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "sra64r\t$rd, $rsd, $rs", []>;
// #19 SRA64R
def SRA64R_S2 : HaydnFU_ALU64_S2_RR_DDG<0b00010011,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "sra64r\t$rd, $rsd, $rs", []>;
// #20 SLL64
def SLL64_S1 : HaydnFU_ALU64_S1_RR_DDG<0b00010100,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "sll64\t$rd, $rsd, $rs", []>;
// #20 SLL64
def SLL64_S2 : HaydnFU_ALU64_S2_RR_DDG<0b00010100,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "sll64\t$rd, $rsd, $rs", []>;
// #77 X2SRA32
def X2SRA32_S1 : HaydnFU_ALU64_S1_RR_DDG<0b01001101,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x2sra32\t$rd, $rsd, $rs", []>;
// #77 X2SRA32
def X2SRA32_S2 : HaydnFU_ALU64_S2_RR_DDG<0b01001101,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x2sra32\t$rd, $rsd, $rs", []>;
// #78 X2SRL32
def X2SRL32_S1 : HaydnFU_ALU64_S1_RR_DDG<0b01001110,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x2srl32\t$rd, $rsd, $rs", []>;
// #78 X2SRL32
def X2SRL32_S2 : HaydnFU_ALU64_S2_RR_DDG<0b01001110,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x2srl32\t$rd, $rsd, $rs", []>;
// #79 X2SRA32R
def X2SRA32R_S1 : HaydnFU_ALU64_S1_RR_DDG<0b01001111,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x2sra32r\t$rd, $rsd, $rs", []>;
// #79 X2SRA32R
def X2SRA32R_S2 : HaydnFU_ALU64_S2_RR_DDG<0b01001111,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x2sra32r\t$rd, $rsd, $rs", []>;
// #80 X2SLL32
def X2SLL32_S1 : HaydnFU_ALU64_S1_RR_DDG<0b01010000,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x2sll32\t$rd, $rsd, $rs", []>;
// #80 X2SLL32
def X2SLL32_S2 : HaydnFU_ALU64_S2_RR_DDG<0b01010000,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x2sll32\t$rd, $rsd, $rs", []>;
// #133 X4SRA16
def X4SRA16_S1 : HaydnFU_ALU64_S1_RR_DDG<0b10000101,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x4sra16\t$rd, $rsd, $rs", []>;
// #133 X4SRA16
def X4SRA16_S2 : HaydnFU_ALU64_S2_RR_DDG<0b10000101,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x4sra16\t$rd, $rsd, $rs", []>;
// #134 X4SRL16
def X4SRL16_S1 : HaydnFU_ALU64_S1_RR_DDG<0b10000110,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x4srl16\t$rd, $rsd, $rs", []>;
// #134 X4SRL16
def X4SRL16_S2 : HaydnFU_ALU64_S2_RR_DDG<0b10000110,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x4srl16\t$rd, $rsd, $rs", []>;
// #135 X4SRA16R
def X4SRA16R_S1 : HaydnFU_ALU64_S1_RR_DDG<0b10000111,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x4sra16r\t$rd, $rsd, $rs", []>;
// #135 X4SRA16R
def X4SRA16R_S2 : HaydnFU_ALU64_S2_RR_DDG<0b10000111,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x4sra16r\t$rd, $rsd, $rs", []>;
// #136 X4SLL16
def X4SLL16_S1 : HaydnFU_ALU64_S1_RR_DDG<0b10001000,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x4sll16\t$rd, $rsd, $rs", []>;
// #136 X4SLL16
def X4SLL16_S2 : HaydnFU_ALU64_S2_RR_DDG<0b10001000,
    (outs DR64:$rd), (ins DR64:$rsd, GPR32:$rs),
    "x4sll16\t$rd, $rsd, $rs", []>;

//===----------------------------------------------------------------------===//
// ALU64 R_DD ops -- 2 DR64 (rtd, rsd).
//===----------------------------------------------------------------------===//

// #28 NOT64 — private encode peers for logical NOT64 (FmtALU64Unary).
// Class field is `$rsd` (HaydnFU_ALU64_*_R_DD); logical uses `$rs1`.
// Alts index materialize: AlternateInsts + leaveRegion setDesc (same ABS64/NEG64).
def NOT64_S1 : HaydnFU_ALU64_S1_R_DD<0b00011100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "not64\t$rd, $rsd", []>;
def NOT64_S2 : HaydnFU_ALU64_S2_R_DD<0b00011100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "not64\t$rd, $rsd", []>;
// #30 MOVE64
def MOVE64_S1 : HaydnFU_ALU64_S1_R_DD<0b00011110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "move64\t$rd, $rsd", []>;
// #30 MOVE64
def MOVE64_S2 : HaydnFU_ALU64_S2_R_DD<0b00011110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "move64\t$rd, $rsd", []>;
// #35 ABS64
def ABS64_S1 : HaydnFU_ALU64_S1_R_DD<0b00100011,
    (outs DR64:$rd), (ins DR64:$rsd),
    "abs64\t$rd, $rsd", []>;
// #35 ABS64
def ABS64_S2 : HaydnFU_ALU64_S2_R_DD<0b00100011,
    (outs DR64:$rd), (ins DR64:$rsd),
    "abs64\t$rd, $rsd", []>;
// #36 ABS64S
def ABS64S_S1 : HaydnFU_ALU64_S1_R_DD<0b00100100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "abs64s\t$rd, $rsd", []>;
// #36 ABS64S
def ABS64S_S2 : HaydnFU_ALU64_S2_R_DD<0b00100100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "abs64s\t$rd, $rsd", []>;
// #39 NEG64
def NEG64_S1 : HaydnFU_ALU64_S1_R_DD<0b00100111,
    (outs DR64:$rd), (ins DR64:$rsd),
    "neg64\t$rd, $rsd", []>;
// #39 NEG64
def NEG64_S2 : HaydnFU_ALU64_S2_R_DD<0b00100111,
    (outs DR64:$rd), (ins DR64:$rsd),
    "neg64\t$rd, $rsd", []>;
// #40 NEG64S
def NEG64S_S1 : HaydnFU_ALU64_S1_R_DD<0b00101000,
    (outs DR64:$rd), (ins DR64:$rsd),
    "neg64s\t$rd, $rsd", []>;
// #40 NEG64S
def NEG64S_S2 : HaydnFU_ALU64_S2_R_DD<0b00101000,
    (outs DR64:$rd), (ins DR64:$rsd),
    "neg64s\t$rd, $rsd", []>;
// #44 TRANSF64_L
def TRANSF64_L_S1 : HaydnFU_ALU64_S1_R_DD<0b00101100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "transf64_l\t$rd, $rsd", []>;
// #44 TRANSF64_L
def TRANSF64_L_S2 : HaydnFU_ALU64_S2_R_DD<0b00101100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "transf64_l\t$rd, $rsd", []>;
// #45 TRANSF64_H
def TRANSF64_H_S1 : HaydnFU_ALU64_S1_R_DD<0b00101101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "transf64_h\t$rd, $rsd", []>;
// #45 TRANSF64_H
def TRANSF64_H_S2 : HaydnFU_ALU64_S2_R_DD<0b00101101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "transf64_h\t$rd, $rsd", []>;
// #57 X2HADD32_L
def X2HADD32_L_S1 : HaydnFU_ALU64_S1_R_DD<0b00111001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2hadd32_l\t$rd, $rsd", []>;
// #57 X2HADD32_L
def X2HADD32_L_S2 : HaydnFU_ALU64_S2_R_DD<0b00111001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2hadd32_l\t$rd, $rsd", []>;
// #58 X2HADD32_H
def X2HADD32_H_S1 : HaydnFU_ALU64_S1_R_DD<0b00111010,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2hadd32_h\t$rd, $rsd", []>;
// #58 X2HADD32_H
def X2HADD32_H_S2 : HaydnFU_ALU64_S2_R_DD<0b00111010,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2hadd32_h\t$rd, $rsd", []>;
// #59 X2HADD32S_L
def X2HADD32S_L_S1 : HaydnFU_ALU64_S1_R_DD<0b00111011,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2hadd32s_l\t$rd, $rsd", []>;
// #59 X2HADD32S_L
def X2HADD32S_L_S2 : HaydnFU_ALU64_S2_R_DD<0b00111011,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2hadd32s_l\t$rd, $rsd", []>;
// #60 X2HADD32S_H
def X2HADD32S_H_S1 : HaydnFU_ALU64_S1_R_DD<0b00111100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2hadd32s_h\t$rd, $rsd", []>;
// #60 X2HADD32S_H
def X2HADD32S_H_S2 : HaydnFU_ALU64_S2_R_DD<0b00111100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2hadd32s_h\t$rd, $rsd", []>;
// #81 X2ABS32
def X2ABS32_S1 : HaydnFU_ALU64_S1_R_DD<0b01010001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2abs32\t$rd, $rsd", []>;
// #81 X2ABS32
def X2ABS32_S2 : HaydnFU_ALU64_S2_R_DD<0b01010001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2abs32\t$rd, $rsd", []>;
// #82 X2ABS32S
def X2ABS32S_S1 : HaydnFU_ALU64_S1_R_DD<0b01010010,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2abs32s\t$rd, $rsd", []>;
// #82 X2ABS32S
def X2ABS32S_S2 : HaydnFU_ALU64_S2_R_DD<0b01010010,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2abs32s\t$rd, $rsd", []>;
// #83 X2NEG32
def X2NEG32_S1 : HaydnFU_ALU64_S1_R_DD<0b01010011,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2neg32\t$rd, $rsd", []>;
// #83 X2NEG32
def X2NEG32_S2 : HaydnFU_ALU64_S2_R_DD<0b01010011,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2neg32\t$rd, $rsd", []>;
// #84 X2NEG32S
def X2NEG32S_S1 : HaydnFU_ALU64_S1_R_DD<0b01010100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2neg32s\t$rd, $rsd", []>;
// #84 X2NEG32S
def X2NEG32S_S2 : HaydnFU_ALU64_S2_R_DD<0b01010100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2neg32s\t$rd, $rsd", []>;
// #85 X2NEG32_L
def X2NEG32_L_S1 : HaydnFU_ALU64_S1_R_DD<0b01010101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2neg32_l\t$rd, $rsd", []>;
// #85 X2NEG32_L
def X2NEG32_L_S2 : HaydnFU_ALU64_S2_R_DD<0b01010101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2neg32_l\t$rd, $rsd", []>;
// #86 X2NEG32S_L
def X2NEG32S_L_S1 : HaydnFU_ALU64_S1_R_DD<0b01010110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2neg32s_l\t$rd, $rsd", []>;
// #86 X2NEG32S_L
def X2NEG32S_L_S2 : HaydnFU_ALU64_S2_R_DD<0b01010110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2neg32s_l\t$rd, $rsd", []>;
// #92 X2SWAP32
def X2SWAP32_S1 : HaydnFU_ALU64_S1_R_DD<0b01011100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2swap32\t$rd, $rsd", []>;
// #92 X2SWAP32
def X2SWAP32_S2 : HaydnFU_ALU64_S2_R_DD<0b01011100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2swap32\t$rd, $rsd", []>;
// #93 X2MJSWAP32
def X2MJSWAP32_S1 : HaydnFU_ALU64_S1_R_DD<0b01011101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2mjswap32\t$rd, $rsd", []>;
// #93 X2MJSWAP32
def X2MJSWAP32_S2 : HaydnFU_ALU64_S2_R_DD<0b01011101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2mjswap32\t$rd, $rsd", []>;
// #94 X2MJSWAP32S
def X2MJSWAP32S_S1 : HaydnFU_ALU64_S1_R_DD<0b01011110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2mjswap32s\t$rd, $rsd", []>;
// #94 X2MJSWAP32S
def X2MJSWAP32S_S2 : HaydnFU_ALU64_S2_R_DD<0b01011110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x2mjswap32s\t$rd, $rsd", []>;
// #108 X4HADD16_L
def X4HADD16_L_S1 : HaydnFU_ALU64_S1_R_DD<0b01101100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4hadd16_l\t$rd, $rsd", []>;
// #108 X4HADD16_L
def X4HADD16_L_S2 : HaydnFU_ALU64_S2_R_DD<0b01101100,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4hadd16_l\t$rd, $rsd", []>;
// #109 X4HADD16_H
def X4HADD16_H_S1 : HaydnFU_ALU64_S1_R_DD<0b01101101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4hadd16_h\t$rd, $rsd", []>;
// #109 X4HADD16_H
def X4HADD16_H_S2 : HaydnFU_ALU64_S2_R_DD<0b01101101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4hadd16_h\t$rd, $rsd", []>;
// #110 X4SWAP16
def X4SWAP16_S1 : HaydnFU_ALU64_S1_R_DD<0b01101110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4swap16\t$rd, $rsd", []>;
// #110 X4SWAP16
def X4SWAP16_S2 : HaydnFU_ALU64_S2_R_DD<0b01101110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4swap16\t$rd, $rsd", []>;
// #111 X4MJSWAP16
def X4MJSWAP16_S1 : HaydnFU_ALU64_S1_R_DD<0b01101111,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4mjswap16\t$rd, $rsd", []>;
// #111 X4MJSWAP16
def X4MJSWAP16_S2 : HaydnFU_ALU64_S2_R_DD<0b01101111,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4mjswap16\t$rd, $rsd", []>;
// #112 X4MJSWAP16S
def X4MJSWAP16S_S1 : HaydnFU_ALU64_S1_R_DD<0b01110000,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4mjswap16s\t$rd, $rsd", []>;
// #112 X4MJSWAP16S
def X4MJSWAP16S_S2 : HaydnFU_ALU64_S2_R_DD<0b01110000,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4mjswap16s\t$rd, $rsd", []>;
// #113 X4CONJ16
def X4CONJ16_S1 : HaydnFU_ALU64_S1_R_DD<0b01110001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4conj16\t$rd, $rsd", []>;
// #113 X4CONJ16
def X4CONJ16_S2 : HaydnFU_ALU64_S2_R_DD<0b01110001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4conj16\t$rd, $rsd", []>;
// #114 X4CONJ16S
def X4CONJ16S_S1 : HaydnFU_ALU64_S1_R_DD<0b01110010,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4conj16s\t$rd, $rsd", []>;
// #114 X4CONJ16S
def X4CONJ16S_S2 : HaydnFU_ALU64_S2_R_DD<0b01110010,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4conj16s\t$rd, $rsd", []>;
// #120 X4ABS16
def X4ABS16_S1 : HaydnFU_ALU64_S1_R_DD<0b01111000,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4abs16\t$rd, $rsd", []>;
// #120 X4ABS16
def X4ABS16_S2 : HaydnFU_ALU64_S2_R_DD<0b01111000,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4abs16\t$rd, $rsd", []>;
// #121 X4ABS16S
def X4ABS16S_S1 : HaydnFU_ALU64_S1_R_DD<0b01111001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4abs16s\t$rd, $rsd", []>;
// #121 X4ABS16S
def X4ABS16S_S2 : HaydnFU_ALU64_S2_R_DD<0b01111001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4abs16s\t$rd, $rsd", []>;
// #122 X4NEG16
def X4NEG16_S1 : HaydnFU_ALU64_S1_R_DD<0b01111010,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4neg16\t$rd, $rsd", []>;
// #122 X4NEG16
def X4NEG16_S2 : HaydnFU_ALU64_S2_R_DD<0b01111010,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4neg16\t$rd, $rsd", []>;
// #123 X4NEG16S
def X4NEG16S_S1 : HaydnFU_ALU64_S1_R_DD<0b01111011,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4neg16s\t$rd, $rsd", []>;
// #123 X4NEG16S
def X4NEG16S_S2 : HaydnFU_ALU64_S2_R_DD<0b01111011,
    (outs DR64:$rd), (ins DR64:$rsd),
    "x4neg16s\t$rd, $rsd", []>;

//===----------------------------------------------------------------------===//
// ALU64 R_GD ops -- 1 GPR32(out) + 1 DR64(in) (rt, rsd -- NSA/POPCOUNT/HMAX).
//===----------------------------------------------------------------------===//

// #46 NSA64
def NSA64_S1 : HaydnFU_ALU64_S1_R_GD<0b00101110,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsa64\t$rt, $rsd", []>;
// #46 NSA64
def NSA64_S2 : HaydnFU_ALU64_S2_R_GD<0b00101110,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsa64\t$rt, $rsd", []>;
// #47 NSAZ64
def NSAZ64_S1 : HaydnFU_ALU64_S1_R_GD<0b00101111,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsaz64\t$rt, $rsd", []>;
// #47 NSAZ64
def NSAZ64_S2 : HaydnFU_ALU64_S2_R_GD<0b00101111,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsaz64\t$rt, $rsd", []>;
// #48 NSA32_L
def NSA32_L_S1 : HaydnFU_ALU64_S1_R_GD<0b00110000,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsa32_l\t$rt, $rsd", []>;
// #48 NSA32_L
def NSA32_L_S2 : HaydnFU_ALU64_S2_R_GD<0b00110000,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsa32_l\t$rt, $rsd", []>;
// #49 NSAZ32_L
def NSAZ32_L_S1 : HaydnFU_ALU64_S1_R_GD<0b00110001,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsaz32_l\t$rt, $rsd", []>;
// #49 NSAZ32_L
def NSAZ32_L_S2 : HaydnFU_ALU64_S2_R_GD<0b00110001,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsaz32_l\t$rt, $rsd", []>;
// #50 NSA16_L
def NSA16_L_S1 : HaydnFU_ALU64_S1_R_GD<0b00110010,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsa16_l\t$rt, $rsd", []>;
// #50 NSA16_L
def NSA16_L_S2 : HaydnFU_ALU64_S2_R_GD<0b00110010,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsa16_l\t$rt, $rsd", []>;
// #51 NSAZ16_L
def NSAZ16_L_S1 : HaydnFU_ALU64_S1_R_GD<0b00110011,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsaz16_l\t$rt, $rsd", []>;
// #51 NSAZ16_L
def NSAZ16_L_S2 : HaydnFU_ALU64_S2_R_GD<0b00110011,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "nsaz16_l\t$rt, $rsd", []>;
// #52 POPCOUNT64
def POPCOUNT64_S1 : HaydnFU_ALU64_S1_R_GD<0b00110100,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "popcount64\t$rt, $rsd", []>;
// #52 POPCOUNT64
def POPCOUNT64_S2 : HaydnFU_ALU64_S2_R_GD<0b00110100,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "popcount64\t$rt, $rsd", []>;
// #89 X2HMAX32
def X2HMAX32_S1 : HaydnFU_ALU64_S1_R_GD<0b01011001,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "x2hmax32\t$rt, $rsd", []>;
// #89 X2HMAX32
def X2HMAX32_S2 : HaydnFU_ALU64_S2_R_GD<0b01011001,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "x2hmax32\t$rt, $rsd", []>;
// #90 X2HMIN32
def X2HMIN32_S1 : HaydnFU_ALU64_S1_R_GD<0b01011010,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "x2hmin32\t$rt, $rsd", []>;
// #90 X2HMIN32
def X2HMIN32_S2 : HaydnFU_ALU64_S2_R_GD<0b01011010,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "x2hmin32\t$rt, $rsd", []>;
// #117 X4HMAX16
def X4HMAX16_S1 : HaydnFU_ALU64_S1_R_GD<0b01110101,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "x4hmax16\t$rt, $rsd", []>;
// #117 X4HMAX16
def X4HMAX16_S2 : HaydnFU_ALU64_S2_R_GD<0b01110101,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "x4hmax16\t$rt, $rsd", []>;
// #118 X4HMIN16
def X4HMIN16_S1 : HaydnFU_ALU64_S1_R_GD<0b01110110,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "x4hmin16\t$rt, $rsd", []>;
// #118 X4HMIN16
def X4HMIN16_S2 : HaydnFU_ALU64_S2_R_GD<0b01110110,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "x4hmin16\t$rt, $rsd", []>;

//===----------------------------------------------------------------------===//
// ALU64 R_DG ops -- 1 DR64(out) + 1 GPR32(in) (rtd, rs -- SEXT/TRANSF).
//===----------------------------------------------------------------------===//

// #41 SEXT32T64
def SEXT32T64_S1 : HaydnFU_ALU64_S1_R_DG<0b00101001,
    (outs DR64:$rd), (ins GPR32:$rs),
    "sext32t64\t$rd, $rs", []>;
// #41 SEXT32T64
def SEXT32T64_S2 : HaydnFU_ALU64_S2_R_DG<0b00101001,
    (outs DR64:$rd), (ins GPR32:$rs),
    "sext32t64\t$rd, $rs", []>;

// SEXT_GPR32_TO_DR64 (legacy codegen name for SEXT32T64). AlternateInsts /
// setDesc needs a matching _S1 member for the legacy name. Same format
// class + codepoint as SEXT32T64_S1; isCodeGenOnly excludes it from the
// decoder trie (the SEXT32T64_S1 entry is the canonical decoder form).
let isCodeGenOnly = 1 in
def SEXT_GPR32_TO_DR64_S1 : HaydnFU_ALU64_S1_R_DG<0b00101001,
    (outs DR64:$rd), (ins GPR32:$rs),
    "sext32t64\t$rd, $rs", []>;
// #42 TRANSF64
def TRANSF64_S1 : HaydnFU_ALU64_S1_R_DG<0b00101010,
    (outs DR64:$rd), (ins GPR32:$rs),
    "transf64\t$rd, $rs", []>;
// #42 TRANSF64
def TRANSF64_S2 : HaydnFU_ALU64_S2_R_DG<0b00101010,
    (outs DR64:$rd), (ins GPR32:$rs),
    "transf64\t$rd, $rs", []>;
// #43 TRANSF64F2
def TRANSF64F2_S1 : HaydnFU_ALU64_S1_R_DG<0b00101011,
    (outs DR64:$rd), (ins GPR32:$rs),
    "transf64f2\t$rd, $rs", []>;
// #43 TRANSF64F2
def TRANSF64F2_S2 : HaydnFU_ALU64_S2_R_DG<0b00101011,
    (outs DR64:$rd), (ins GPR32:$rs),
    "transf64f2\t$rd, $rs", []>;

//===----------------------------------------------------------------------===//
// ALU64 R_GG ops -- 2 GPR32 (rt, rs -- LOG2/EXP2/RECIP/SQRT).
//===----------------------------------------------------------------------===//

// #146 LOG2
def LOG2_S1 : HaydnFU_ALU64_S1_R_GG<0b10010010,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "log2\t$rt, $rs", []>;
// #146 LOG2
def LOG2_S2 : HaydnFU_ALU64_S2_R_GG<0b10010010,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "log2\t$rt, $rs", []>;
// #147 EXP2
def EXP2_S1 : HaydnFU_ALU64_S1_R_GG<0b10010011,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "exp2\t$rt, $rs", []>;
// #147 EXP2
def EXP2_S2 : HaydnFU_ALU64_S2_R_GG<0b10010011,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "exp2\t$rt, $rs", []>;
// #148 RECIP
def RECIP_S1 : HaydnFU_ALU64_S1_R_GG<0b10010100,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "recip\t$rt, $rs", []>;
// #148 RECIP
def RECIP_S2 : HaydnFU_ALU64_S2_R_GG<0b10010100,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "recip\t$rt, $rs", []>;
// #149 SQRT
def SQRT_S1 : HaydnFU_ALU64_S1_R_GG<0b10010101,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "sqrt\t$rt, $rs", []>;
// #149 SQRT
def SQRT_S2 : HaydnFU_ALU64_S2_R_GG<0b10010101,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "sqrt\t$rt, $rs", []>;

//===----------------------------------------------------------------------===//
// ALU64 R_CMP ops -- 2 DR64(in) -> SFR(out) (rsd1, rsd2 -- SLT/SLE/SEQ).
//===----------------------------------------------------------------------===//

// #21 SLT64 / #22 SLE64 / #23 SEQ64
// Golden HW is R_CMP: 2 DR sources, SFR write only (no DR dest). Compiler
// logical + GISel are unary FmtALU64Unary (outs DR64:$rd, ins DR64:$rs1) with
// Defs=[SFR] — see HaydnInstrInfo.td and selectUnary(SLT64).
// AIE setDesc (AIEMachineScheduler.cpp:1126-1132) requires PlacementAlternative
// members to share that unary shape. Use R_DD (rd,rsd) — same 2×4b payload as
// R_CMP — so setDesc preserves NumDefs/NumOperands; do not keep R_CMP's
// (outs),(ins rs1,rs2) which made verifier reject "Explicit operand marked as
// def" after setDesc. Encoding fields bind rd/rsd to the unary operands.
let Defs = [SFR] in {
def SLT64_S1 : HaydnFU_ALU64_S1_R_DD<0b00010101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "slt64\t$rd, $rsd", []>;
def SLT64_S2 : HaydnFU_ALU64_S2_R_DD<0b00010101,
    (outs DR64:$rd), (ins DR64:$rsd),
    "slt64\t$rd, $rsd", []>;
def SLE64_S1 : HaydnFU_ALU64_S1_R_DD<0b00010110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "sle64\t$rd, $rsd", []>;
def SLE64_S2 : HaydnFU_ALU64_S2_R_DD<0b00010110,
    (outs DR64:$rd), (ins DR64:$rsd),
    "sle64\t$rd, $rsd", []>;
def SEQ64_S1 : HaydnFU_ALU64_S1_R_DD<0b00010111,
    (outs DR64:$rd), (ins DR64:$rsd),
    "seq64\t$rd, $rsd", []>;
def SEQ64_S2 : HaydnFU_ALU64_S2_R_DD<0b00010111,
    (outs DR64:$rd), (ins DR64:$rsd),
    "seq64\t$rd, $rsd", []>;
}  // Defs = [SFR] (scalar SLT64/SLE64/SEQ64 block closes here)

// SIMD compares X2SLT32/X2SLE32/X2SEQ32 + X4SLT16/X4SLE16/X4SEQ16.
// Spec (golden DB): DR_read=2, DR_write=0, SFR_write=1. BUT the IR intrinsic
// returns a value, so the.td models a PASSTHROUGH dest (rd = rs1) — same as
// the legacy X2SEQ32 (HaydnInstrInfo.td:1401, FmtALU64Binary 3-operand). The
// GISel selector (selectBinary) emits def + 2 sources. Decoder 16 reads 3 DR64
// at [18:14:10] — identical to RR_DDD. So we MUST use RR_DDD (3-DR64 layout)
// to match encoder/decoder/selector. `let hasSideEffects = 1` models the
// implicit SFR write (AIE pattern; proven at HaydnFormatsALU64.td:569/584).
let hasSideEffects = 1 in {  // implicit SFR write
def X2SLT32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01011111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2slt32\t$rd, $rs1, $rs2", []>;
def X2SLT32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01011111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2slt32\t$rd, $rs1, $rs2", []>;
def X2SLE32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01100000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sle32\t$rd, $rs1, $rs2", []>;
def X2SLE32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01100000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2sle32\t$rd, $rs1, $rs2", []>;
def X2SEQ32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01100001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2seq32\t$rd, $rs1, $rs2", []>;
def X2SEQ32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01100001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2seq32\t$rd, $rs1, $rs2", []>;
def X4SLT16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01111100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4slt16\t$rd, $rs1, $rs2", []>;
def X4SLT16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01111100,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4slt16\t$rd, $rs1, $rs2", []>;
def X4SLE16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01111101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4sle16\t$rd, $rs1, $rs2", []>;
def X4SLE16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01111101,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4sle16\t$rd, $rs1, $rs2", []>;
def X4SEQ16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01111110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4seq16\t$rd, $rs1, $rs2", []>;
def X4SEQ16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01111110,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4seq16\t$rd, $rs1, $rs2", []>;
}  // hasSideEffects = 1

//===----------------------------------------------------------------------===//
// ALU64 R_COND ops -- 1 DR64(out) + 1 DR64(in) | SFR(in) (rtd, rsd -- MOVT/MOVF).
//===----------------------------------------------------------------------===//

// #24 MOVT64
def MOVT64_S1 : HaydnFU_ALU64_S1_R_COND<0b00011000,
    (outs DR64:$rd), (ins DR64:$rsd),
    "movt64\t$rd, $rsd", []>;
// #24 MOVT64
def MOVT64_S2 : HaydnFU_ALU64_S2_R_COND<0b00011000,
    (outs DR64:$rd), (ins DR64:$rsd),
    "movt64\t$rd, $rsd", []>;
// #25 MOVF64
def MOVF64_S1 : HaydnFU_ALU64_S1_R_COND<0b00011001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "movf64\t$rd, $rsd", []>;
// #25 MOVF64
def MOVF64_S2 : HaydnFU_ALU64_S2_R_COND<0b00011001,
    (outs DR64:$rd), (ins DR64:$rsd),
    "movf64\t$rd, $rsd", []>;
// SIMD conditional moves X2MOVT32/X2MOVF32/X4MOVT16/X4MOVF16.
// Spec (golden DB): DR_read=1, DR_write=1, SFR_read=1. BUT the IR intrinsic
// is dst = movt(src_false, src_true) — 2 DR sources + 1 DR dest. The GISel
// selector (selectBinary) emits def + 2 sources. Legacy X2MOVT32
// (HaydnInstrInfo.td:1427, FmtALU64Binary) is 3-operand. Decoder 16 reads
// 3 DR64 at [18:14:10] = RR_DDD. So we MUST use RR_DDD to match.
// `let hasSideEffects = 1` models the implicit SFR read.
let hasSideEffects = 1 in {  // implicit SFR read
// #98 X2MOVT32
def X2MOVT32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01100010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2movt32\t$rd, $rs1, $rs2", []>;
// #98 X2MOVT32
def X2MOVT32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01100010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2movt32\t$rd, $rs1, $rs2", []>;
// #99 X2MOVF32
def X2MOVF32_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01100011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2movf32\t$rd, $rs1, $rs2", []>;
// #99 X2MOVF32
def X2MOVF32_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01100011,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x2movf32\t$rd, $rs1, $rs2", []>;
// #127 X4MOVT16
def X4MOVT16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b01111111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4movt16\t$rd, $rs1, $rs2", []>;
// #127 X4MOVT16
def X4MOVT16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b01111111,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4movt16\t$rd, $rs1, $rs2", []>;
// #128 X4MOVF16
def X4MOVF16_S1 : HaydnFU_ALU64_S1_RR_DDD<0b10000000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4movf16\t$rd, $rs1, $rs2", []>;
// #128 X4MOVF16
def X4MOVF16_S2 : HaydnFU_ALU64_S2_RR_DDD<0b10000000,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2),
    "x4movf16\t$rd, $rs1, $rs2", []>;
}  // hasSideEffects = 1

//===----------------------------------------------------------------------===//
// ALU64 RI6 ops -- 2 DR64 + uimm6 (rtd, rsd, uimm6).
//===----------------------------------------------------------------------===//

// Logical SLLI64/SRLI64/SRAI64 carry Constraints = "$rtd = $rsd"
// (HaydnInstrInfo.td). Format-members must keep the same tie so MI.setDesc
// preserves tied-def/use for MachineVerifier (AIE alts share operand
// structure). Operand names on FU classes are $rd/$rsd.
let Constraints = "$rd = $rsd" in {
// #13 SRAI64
def SRAI64_S1 : HaydnFU_ALU64_S1_RI6<0b00001101,
    (outs DR64:$rd), (ins DR64:$rsd, uimm6:$imm6),
    "srai64\t$rd, $rsd, $imm6", []>;
// #13 SRAI64
def SRAI64_S2 : HaydnFU_ALU64_S2_RI6<0b00001101,
    (outs DR64:$rd), (ins DR64:$rsd, uimm6:$imm6),
    "srai64\t$rd, $rsd, $imm6", []>;
// #14 SRLI64
def SRLI64_S1 : HaydnFU_ALU64_S1_RI6<0b00001110,
    (outs DR64:$rd), (ins DR64:$rsd, uimm6:$imm6),
    "srli64\t$rd, $rsd, $imm6", []>;
// #14 SRLI64
def SRLI64_S2 : HaydnFU_ALU64_S2_RI6<0b00001110,
    (outs DR64:$rd), (ins DR64:$rsd, uimm6:$imm6),
    "srli64\t$rd, $rsd, $imm6", []>;
// #16 SLLI64
def SLLI64_S1 : HaydnFU_ALU64_S1_RI6<0b00010000,
    (outs DR64:$rd), (ins DR64:$rsd, uimm6:$imm6),
    "slli64\t$rd, $rsd, $imm6", []>;
// #16 SLLI64
def SLLI64_S2 : HaydnFU_ALU64_S2_RI6<0b00010000,
    (outs DR64:$rd), (ins DR64:$rsd, uimm6:$imm6),
    "slli64\t$rd, $rsd, $imm6", []>;
} // Constraints $rd = $rsd

// #15 SRAI64R — not under logical "$rtd = $rsd" block; no tie.
def SRAI64R_S1 : HaydnFU_ALU64_S1_RI6<0b00001111,
    (outs DR64:$rd), (ins DR64:$rsd, uimm6:$imm6),
    "srai64r\t$rd, $rsd, $imm6", []>;
// #15 SRAI64R
def SRAI64R_S2 : HaydnFU_ALU64_S2_RI6<0b00001111,
    (outs DR64:$rd), (ins DR64:$rsd, uimm6:$imm6),
    "srai64r\t$rd, $rsd, $imm6", []>;

//===----------------------------------------------------------------------===//
// ALU64 RI5 ops -- 2 DR64 + uimm5 (rtd, rsd, uimm5).
//===----------------------------------------------------------------------===//

// #73 X2SRAI32
def X2SRAI32_S1 : HaydnFU_ALU64_S1_RI5<0b01001001,
    (outs DR64:$rd), (ins DR64:$rsd, uimm5:$imm5),
    "x2srai32\t$rd, $rsd, $imm5", []>;
// #73 X2SRAI32
def X2SRAI32_S2 : HaydnFU_ALU64_S2_RI5<0b01001001,
    (outs DR64:$rd), (ins DR64:$rsd, uimm5:$imm5),
    "x2srai32\t$rd, $rsd, $imm5", []>;
// #74 X2SRLI32
def X2SRLI32_S1 : HaydnFU_ALU64_S1_RI5<0b01001010,
    (outs DR64:$rd), (ins DR64:$rsd, uimm5:$imm5),
    "x2srli32\t$rd, $rsd, $imm5", []>;
// #74 X2SRLI32
def X2SRLI32_S2 : HaydnFU_ALU64_S2_RI5<0b01001010,
    (outs DR64:$rd), (ins DR64:$rsd, uimm5:$imm5),
    "x2srli32\t$rd, $rsd, $imm5", []>;
// #75 X2SRAI32R
def X2SRAI32R_S1 : HaydnFU_ALU64_S1_RI5<0b01001011,
    (outs DR64:$rd), (ins DR64:$rsd, uimm5:$imm5),
    "x2srai32r\t$rd, $rsd, $imm5", []>;
// #75 X2SRAI32R
def X2SRAI32R_S2 : HaydnFU_ALU64_S2_RI5<0b01001011,
    (outs DR64:$rd), (ins DR64:$rsd, uimm5:$imm5),
    "x2srai32r\t$rd, $rsd, $imm5", []>;
// #76 X2SLLI32
def X2SLLI32_S1 : HaydnFU_ALU64_S1_RI5<0b01001100,
    (outs DR64:$rd), (ins DR64:$rsd, uimm5:$imm5),
    "x2slli32\t$rd, $rsd, $imm5", []>;
// #76 X2SLLI32
def X2SLLI32_S2 : HaydnFU_ALU64_S2_RI5<0b01001100,
    (outs DR64:$rd), (ins DR64:$rsd, uimm5:$imm5),
    "x2slli32\t$rd, $rsd, $imm5", []>;

//===----------------------------------------------------------------------===//
// ALU64 RI4_DD ops -- 2 DR64 + uimm4 (rtd, rsd, uimm4).
//===----------------------------------------------------------------------===//

// #129 X4SRAI16
def X4SRAI16_S1 : HaydnFU_ALU64_S1_RI4_DD<0b10000001,
    (outs DR64:$rd), (ins DR64:$rsd, uimm4:$imm4),
    "x4srai16\t$rd, $rsd, $imm4", []>;
// #129 X4SRAI16
def X4SRAI16_S2 : HaydnFU_ALU64_S2_RI4_DD<0b10000001,
    (outs DR64:$rd), (ins DR64:$rsd, uimm4:$imm4),
    "x4srai16\t$rd, $rsd, $imm4", []>;
// #130 X4SRLI16
def X4SRLI16_S1 : HaydnFU_ALU64_S1_RI4_DD<0b10000010,
    (outs DR64:$rd), (ins DR64:$rsd, uimm4:$imm4),
    "x4srli16\t$rd, $rsd, $imm4", []>;
// #130 X4SRLI16
def X4SRLI16_S2 : HaydnFU_ALU64_S2_RI4_DD<0b10000010,
    (outs DR64:$rd), (ins DR64:$rsd, uimm4:$imm4),
    "x4srli16\t$rd, $rsd, $imm4", []>;
// #131 X4SRAI16R
def X4SRAI16R_S1 : HaydnFU_ALU64_S1_RI4_DD<0b10000011,
    (outs DR64:$rd), (ins DR64:$rsd, uimm4:$imm4),
    "x4srai16r\t$rd, $rsd, $imm4", []>;
// #131 X4SRAI16R
def X4SRAI16R_S2 : HaydnFU_ALU64_S2_RI4_DD<0b10000011,
    (outs DR64:$rd), (ins DR64:$rsd, uimm4:$imm4),
    "x4srai16r\t$rd, $rsd, $imm4", []>;
// #132 X4SLLI16
def X4SLLI16_S1 : HaydnFU_ALU64_S1_RI4_DD<0b10000100,
    (outs DR64:$rd), (ins DR64:$rsd, uimm4:$imm4),
    "x4slli16\t$rd, $rsd, $imm4", []>;
// #132 X4SLLI16
def X4SLLI16_S2 : HaydnFU_ALU64_S2_RI4_DD<0b10000100,
    (outs DR64:$rd), (ins DR64:$rsd, uimm4:$imm4),
    "x4slli16\t$rd, $rsd, $imm4", []>;

//===----------------------------------------------------------------------===//
// ALU64 RI4_DG ops -- 1 DR64 + 1 GPR32 + uimm4 (rtd, rs, uimm4 -- SIN_COS).
//===----------------------------------------------------------------------===//

// #150 SIN_COS
def SIN_COS_S1 : HaydnFU_ALU64_S1_RI4_DG<0b10010110,
    (outs DR64:$rd), (ins GPR32:$rs, uimm4:$imm4),
    "sin_cos\t$rd, $rs, $imm4", []>;
// #150 SIN_COS
def SIN_COS_S2 : HaydnFU_ALU64_S2_RI4_DG<0b10010110,
    (outs DR64:$rd), (ins GPR32:$rs, uimm4:$imm4),
    "sin_cos\t$rd, $rs, $imm4", []>;

//===----------------------------------------------------------------------===//
// ALU64 RI4_GD ops -- 1 GPR32(out) + 1 DR64(in) + uimm4 (rt, rsd, uimm4 -- ARCTAN).
//===----------------------------------------------------------------------===//

// #151 ARCTAN
def ARCTAN_S1 : HaydnFU_ALU64_S1_RI4_GD<0b10010111,
    (outs GPR32:$rt), (ins DR64:$rsd, uimm4:$imm4),
    "arctan\t$rt, $rsd, $imm4", []>;
// #151 ARCTAN
def ARCTAN_S2 : HaydnFU_ALU64_S2_RI4_GD<0b10010111,
    (outs GPR32:$rt), (ins DR64:$rsd, uimm4:$imm4),
    "arctan\t$rt, $rsd, $imm4", []>;

//===----------------------------------------------------------------------===//
// ALU64 RRI4 ops -- 3 DR64 + uimm4 (rtd, rsd1, rsd2, uimm4 -- X4SELI16).
//===----------------------------------------------------------------------===//

// #137 X4SELI16
def X4SELI16_S1 : HaydnFU_ALU64_S1_RRI4<0b10001001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2, uimm4:$imm4),
    "x4seli16\t$rd, $rs1, $rs2, $imm4", []>;
// #137 X4SELI16
def X4SELI16_S2 : HaydnFU_ALU64_S2_RRI4<0b10001001,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2, uimm4:$imm4),
    "x4seli16\t$rd, $rs1, $rs2, $imm4", []>;

//===----------------------------------------------------------------------===//
// ALU64 RRR ops -- 3 DR64 + 1 GPR32 (rtd, rsd1, rsd2, rs -- X4SEL16).
//===----------------------------------------------------------------------===//

// #138 X4SEL16
def X4SEL16_S1 : HaydnFU_ALU64_S1_RRR<0b10001010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2, GPR32:$rs),
    "x4sel16\t$rd, $rs1, $rs2, $rs", []>;
// #138 X4SEL16
def X4SEL16_S2 : HaydnFU_ALU64_S2_RRR<0b10001010,
    (outs DR64:$rd), (ins DR64:$rs1, DR64:$rs2, GPR32:$rs),
    "x4sel16\t$rd, $rs1, $rs2, $rs", []>;

//===----------------------------------------------------------------------===//
// ALU64 I32 ops -- 1 DR64 + simm32 (rtd, imm32 -- MOVEI_H/L).
//===----------------------------------------------------------------------===//

// #31 MOVEI_H
def MOVEI_H_S0 : HaydnFU_ALU64_S0_I32<0b00011111,
    (outs DR64:$rd), (ins simm32_movei:$imm32),
    "movei_h\t$rd, $imm32", []>;
// #32 MOVEI_L
def MOVEI_L_S0 : HaydnFU_ALU64_S0_I32<0b00100000,
    (outs DR64:$rd), (ins simm32_movei:$imm32),
    "movei_l\t$rd, $imm32", []>;

//===----------------------------------------------------------------------===//
// ALU64 SFR_OUT ops -- 1 GPR32(out) | SFR(in) (rt -- MOVESFR2GPR).
//===----------------------------------------------------------------------===//

// #33 MOVESFR2GPR
def MOVESFR2GPR_S1 : HaydnFU_ALU64_S1_SFR_OUT<0b00100001,
    (outs GPR32:$rt), (ins),
    "movesfr2gpr\t$rt", []>;
// #33 MOVESFR2GPR
def MOVESFR2GPR_S2 : HaydnFU_ALU64_S2_SFR_OUT<0b00100001,
    (outs GPR32:$rt), (ins),
    "movesfr2gpr\t$rt", []>;

//===----------------------------------------------------------------------===//
// ALU64 SFR_IN ops -- 1 GPR32(in) | SFR(out) (rs -- MOVEGPR2SFR).
//===----------------------------------------------------------------------===//

// #34 MOVEGPR2SFR
def MOVEGPR2SFR_S1 : HaydnFU_ALU64_S1_SFR_IN<0b00100010,
    (outs), (ins GPR32:$rs),
    "movegpr2sfr\t$rs", []>;
// #34 MOVEGPR2SFR
def MOVEGPR2SFR_S2 : HaydnFU_ALU64_S2_SFR_IN<0b00100010,
    (outs), (ins GPR32:$rs),
    "movegpr2sfr\t$rs", []>;

// NOTE: CSRR/CSRW (system-register access, s0-only §1.3 family) are FU=ALU32
// system ops — they are defined in HaydnFormatsALU32.td. golden lists them in
// both slot0_alu and slot1_alu, but they classify as ALU32 (FU=000), so they
// are NOT emitted here (avoids the def-already-exists collision).

//===----------------------------------------------------------------------===//
// ALU64 I8_ZERODR ops -- 1 DR64(out) (rtd -- ZERO_DR).
//===----------------------------------------------------------------------===//

// #154 ZERO_DR
def ZERO_DR_S1 : HaydnFU_ALU64_S1_I8_ZERODR<0b10011010,
    (outs DR64:$rd), (ins),
    "zero_dr\t$rd", []>;
// #154 ZERO_DR
def ZERO_DR_S2 : HaydnFU_ALU64_S2_I8_ZERODR<0b10011010,
    (outs DR64:$rd), (ins),
    "zero_dr\t$rd", []>;

//===----------------------------------------------------------------------===//
// ALU64 I8_ZEROSFR ops -- no operands (ZERO_SFR -> SFR).
//===----------------------------------------------------------------------===//

// #155 ZERO_SFR
def ZERO_SFR_S1 : HaydnFU_ALU64_S1_I8_ZEROSFR<0b10011011,
    (outs), (ins),
    "zero_sfr", []>;
// #155 ZERO_SFR
def ZERO_SFR_S2 : HaydnFU_ALU64_S2_I8_ZEROSFR<0b10011011,
    (outs), (ins),
    "zero_sfr", []>;

//===----------------------------------------------------------------------===//
// End of ALU64 fresh per-slot port.
// Total: 155 golden ops -> 457 defs (s0/s1/s2 per legal slot).
//===----------------------------------------------------------------------===//
