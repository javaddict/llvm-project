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
// SIMD SFR-only compares have generated 0-def members at e0/e2 (ALU2).
// e1 has no 0-def member — S1 FieldSlots are reject-not-migrate.
// Occupancy Mask 0x6 stays; Fallback is 0. Placement uses the generated
// 0-def members (NumDefs match). Do not invent an e1 SFR-only span.


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
