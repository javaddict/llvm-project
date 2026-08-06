// ===-- HaydnFormatsALU32.td - ALU32 per-FU slot formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: license information.
//
// fresh per-slot port of ALL ALU32 ops from the spec
// (VLIW_Engine_ISA_Reference.md "Slot 0 -- S-ALU", 66 codepoints).
// Authored per-slot (one def per op per legal slot) to future-proof against
// per-slot format divergence. NOT derived from legacy HaydnInstrInfo*.td or
// haydn_instruction_db.json — fresh port from the spec table.
//
// Encoding model (encoding_manual_flex.md §1.2):
// ALU32 FU code = 0b000, opcode width = 7 bits.
// s0[47:45]=FU(000) s0[44:38]=opcode(7b) operands + reserved fill [37:0]
// s1[39:37]=FU(000) s1[36:30]=opcode(7b) operands + reserved fill [29:0]
// s2[39:37]=FU(000) s2[36:30]=opcode(7b) operands + reserved fill [29:0]
//
// Field-order convention (mirrors proven ADD32 / HaydnFU_ALU32_S1_RR):
// MSB-first concat = { FU, opcode, reserved, <imm MSB>, rd/rt, rs2, rs1/rs }
// Register fields: rs1/rs LOWEST, rs2 next, rd/rt HIGHEST (ST_RRR / ST_RRI).
// Immediate fields sit ABOVE the register block in the upper operand region.
//
// Legal slots (CLAUDE.md slot architecture):
// RR / R / RI20 / RI5 / I12(LUI) / I8(ZERO_GPR) / MOVE32_DR -> s0, s1, s2
// Branches / JAL / JALR / HWLOOP / WFI / CSR (CFG/system) -> s0 ONLY
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Shared operand: signed 12-bit immediate (RI12 / I12 branch & JALR offsets).
// The spec lists RI12 as rs1,rs2,imm12 (signed PC-relative branch target).
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// S0 (48-bit window) ALU32 format classes — one per operand shape.
//===----------------------------------------------------------------------===//

// ALU32 RR in S0: rt,rs1,rs2 (3 GPR32). Payload 3+7+26+4+4+4 = 48b.
class HaydnFU_ALU32_S0_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<26> reserved = 0;    // [37:12] unused
  bits<4> rt;
  bits<4> rs1;
  bits<4> rs2;
  let s0 = {FU, opcode, reserved, rt, rs2, rs1};  // MSB-first -> s0[47:0]
}

// ALU32 R in S0: rt,rs (2 GPR32). Payload 3+7+30+4+4 = 48b.
// Some legacy bases that select into this format (MOVE32/ABS32/ABS32S, all
// FmtALU32) declare a vestigial 2nd source operand ($rs2) that the runtime MI
// carries but the Bundle128 encoding does not use. `rs2` is an unencoded field
// (always 0, NOT in the concat) so the operand binds without affecting bytes.
// Unary legacy bases (FmtALU32Unary: NSA32/NOT32/NEG32/...) do not reference
// $rs2 and are unaffected. See BUG 1.
class HaydnFU_ALU32_S0_R<bits<7> opc, dag outs, dag ins, string asmstr,
                         list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<30> reserved = 0;    // [37:8] unused
  bits<4> rt;
  bits<4> rs;
  bits<4> rs2 = 0;          // vestigial legacy $rs2 (unencoded, always 0)
  let s0 = {FU, opcode, reserved, rt, rs};  // MSB-first -> s0[47:0]
}

// ALU32 RI20 in S0: rt,rs,imm20 (2 GPR32 + simm20). Payload 3+7+10+20+4+4 = 48b.
class HaydnFU_ALU32_S0_RI20<bits<7> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<20> imm20;
  bits<10> reserved = 0;    // [17:8] unused
  bits<4> rt;
  bits<4> rs;
  let s0 = {FU, opcode, imm20, reserved, rt, rs};  // MSB-first -> s0[47:0]
}

// ALU32 RI5 in S0: rt,rs,imm5 (2 GPR32 + uimm5). Payload 3+7+25+5+4+4 = 48b.
class HaydnFU_ALU32_S0_RI5<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<25> reserved = 0;    // [37:13] unused
  bits<5> imm5;
  bits<4> rt;
  bits<4> rs;
  let s0 = {FU, opcode, reserved, imm5, rt, rs};  // MSB-first -> s0[47:0]
}

// ALU32 I12 (LUI) in S0: rt,imm12 (GPR32 + uimm12).
// Payload 3+7+22+12+4 = 48b.
// LUI's legacy base (FmtI) declares a vestigial $rs operand (a leftover from
// the I-type template) that the runtime MI carries but the Bundle128 encoding
// does not use. `rs` is an unencoded field (always 0, NOT in the concat) so the
// operand binds without affecting the bytes. See BUG 1.
class HaydnFU_ALU32_S0_I12<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<22> reserved = 0;    // [37:16] unused
  bits<12> imm12;
  bits<4> rt;
  bits<4> rs = 0;           // vestigial legacy $rs (unencoded, always 0)
  let s0 = {FU, opcode, reserved, imm12, rt};  // MSB-first -> s0[47:0]
}

// ALU32 I8 (ZERO_GPR) in S0: rt (1 GPR32). Payload 3+7+34+4 = 48b.
class HaydnFU_ALU32_S0_I8<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<34> reserved = 0;    // [37:4] unused
  bits<4> rt;
  let s0 = {FU, opcode, reserved, rt};  // MSB-first -> s0[47:0]
}

// ALU32 MOVE32_DR (cross-bank) in S0: rt(GPR32), rsd(DR64).
// Payload 3+7+30+4+4 = 48b.
class HaydnFU_ALU32_S0_R_DR<bits<7> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<30> reserved = 0;    // [37:8] unused
  bits<4> rt;               // GPR32
  bits<4> rsd;              // DR64
  let s0 = {FU, opcode, reserved, rt, rsd};  // MSB-first -> s0[47:0]
}

//===----------------------------------------------------------------------===//
// S1 / S2 (40-bit window) ALU32 format classes — one per operand shape.
//===----------------------------------------------------------------------===//

// ALU32 RR in S1: rt,rs1,rs2 (3 GPR32). Payload 3+7+18+4+4+4 = 40b.
class HaydnFU_ALU32_S1_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<18> reserved = 0;    // [29:12] unused
  bits<4> rt;
  bits<4> rs1;
  bits<4> rs2;
  let s1 = {FU, opcode, reserved, rt, rs2, rs1};  // MSB-first -> s1[39:0]
}

// ALU32 R in S1: rt,rs (2 GPR32). Payload 3+7+22+4+4 = 40b.
class HaydnFU_ALU32_S1_R<bits<7> opc, dag outs, dag ins, string asmstr,
                         list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<22> reserved = 0;    // [29:8] unused
  bits<4> rt;
  bits<4> rs;
  bits<4> rs2 = 0;          // vestigial legacy $rs2 (unencoded, always 0)
  let s1 = {FU, opcode, reserved, rt, rs};  // MSB-first -> s1[39:0]
}

// ALU32 RI20 in S1: rt,rs,imm20. Payload 3+7+2+20+4+4 = 40b.
class HaydnFU_ALU32_S1_RI20<bits<7> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<20> imm20;
  bits<2> reserved = 0;     // [9:8] unused
  bits<4> rt;
  bits<4> rs;
  let s1 = {FU, opcode, imm20, reserved, rt, rs};  // MSB-first -> s1[39:0]
}

// ALU32 RI5 in S1: rt,rs,imm5. Payload 3+7+20+5+? -> 3+7+17+5+4+4 = 40b.
class HaydnFU_ALU32_S1_RI5<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<17> reserved = 0;    // [24:8] unused
  bits<5> imm5;
  bits<4> rt;
  bits<4> rs;
  let s1 = {FU, opcode, reserved, imm5, rt, rs};  // MSB-first -> s1[39:0]
}

// ALU32 I8 (ZERO_GPR) in S1: rt. Payload 3+7+26+4 = 40b.
class HaydnFU_ALU32_S1_I8<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<26> reserved = 0;    // [29:4] unused
  bits<4> rt;
  let s1 = {FU, opcode, reserved, rt};  // MSB-first -> s1[39:0]
}

// ALU32 MOVE32_DR (cross-bank) in S1: rt(GPR32), rsd(DR64).
// Payload 3+7+22+4+4 = 40b.
class HaydnFU_ALU32_S1_R_DR<bits<7> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<22> reserved = 0;    // [29:8] unused
  bits<4> rt;               // GPR32
  bits<4> rsd;              // DR64
  let s1 = {FU, opcode, reserved, rt, rsd};  // MSB-first -> s1[39:0]
}

// ALU32 RR in S2: rt,rs1,rs2 (3 GPR32). Payload 3+7+18+4+4+4 = 40b.
class HaydnFU_ALU32_S2_RR<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<18> reserved = 0;    // [29:12] unused
  bits<4> rt;
  bits<4> rs1;
  bits<4> rs2;
  let s2 = {FU, opcode, reserved, rt, rs2, rs1};  // MSB-first -> s2[39:0]
}

// ALU32 R in S2: rt,rs (2 GPR32). Payload 3+7+22+4+4 = 40b.
class HaydnFU_ALU32_S2_R<bits<7> opc, dag outs, dag ins, string asmstr,
                         list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<22> reserved = 0;    // [29:8] unused
  bits<4> rt;
  bits<4> rs;
  bits<4> rs2 = 0;          // vestigial legacy $rs2 (unencoded, always 0)
  let s2 = {FU, opcode, reserved, rt, rs};  // MSB-first -> s2[39:0]
}

// ALU32 RI20 in S2: rt,rs,imm20. Payload 3+7+2+20+4+4 = 40b.
class HaydnFU_ALU32_S2_RI20<bits<7> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<20> imm20;
  bits<2> reserved = 0;     // [9:8] unused
  bits<4> rt;
  bits<4> rs;
  let s2 = {FU, opcode, imm20, reserved, rt, rs};  // MSB-first -> s2[39:0]
}

// ALU32 RI5 in S2: rt,rs,imm5. Payload 3+7+17+5+4+4 = 40b.
class HaydnFU_ALU32_S2_RI5<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<17> reserved = 0;    // [24:8] unused
  bits<5> imm5;
  bits<4> rt;
  bits<4> rs;
  let s2 = {FU, opcode, reserved, imm5, rt, rs};  // MSB-first -> s2[39:0]
}

// ALU32 I8 (ZERO_GPR) in S2: rt. Payload 3+7+26+4 = 40b.
class HaydnFU_ALU32_S2_I8<bits<7> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<26> reserved = 0;    // [29:4] unused
  bits<4> rt;
  let s2 = {FU, opcode, reserved, rt};  // MSB-first -> s2[39:0]
}

// ALU32 MOVE32_DR (cross-bank) in S2: rt(GPR32), rsd(DR64).
// Payload 3+7+22+4+4 = 40b.
class HaydnFU_ALU32_S2_R_DR<bits<7> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<22> reserved = 0;    // [29:8] unused
  bits<4> rt;               // GPR32
  bits<4> rsd;              // DR64
  let s2 = {FU, opcode, reserved, rt, rsd};  // MSB-first -> s2[39:0]
}

//===----------------------------------------------------------------------===//
// S0-ONLY CFG/system format classes (branches / JAL / JALR / HWLOOP / WFI / CSR)
//===----------------------------------------------------------------------===//

// ALU32 RI12 branch in S0: rs1,rs2,imm12 (2 GPR32 + simm12).
// Payload 3+7+16+12+4+4 = 46b -> reserved18: 3+7+18+12+4+4 = 48b.
class HaydnFU_ALU32_S0_RI12<bits<7> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32 (CFG family within s0)
  bits<7> opcode = opc;
  bits<18> reserved = 0;    // [37:20] unused
  bits<12> imm12;
  bits<4> rt;
  bits<4> rs;
  let s0 = {FU, opcode, reserved, imm12, rt, rs};  // MSB-first -> s0[47:0]
}

// ALU32 I12 branch in S0: rs,imm12 (1 GPR32 + simm12).
// Payload 3+7+22+12+4 = 48b.
class HaydnFU_ALU32_S0_I12_ONE<bits<7> opc, dag outs, dag ins, string asmstr,
                               list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<22> reserved = 0;    // [37:16] unused
  bits<12> imm12;
  bits<4> rs;
  let s0 = {FU, opcode, reserved, imm12, rs};  // MSB-first -> s0[47:0]
}

// ALU32 I20 (JAL) in S0: rt,imm20 (1 GPR32 + simm20).
// Payload 3+7+14+20+4 = 48b.
class HaydnFU_ALU32_S0_I20<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<14> reserved = 0;    // [37:24] unused
  bits<20> imm20;
  bits<4> rt;
  let s0 = {FU, opcode, reserved, imm20, rt};  // MSB-first -> s0[47:0]
}

// ALU32 HINT (WFI) in S0: no operands. Payload 3+7+38 = 48b.
class HaydnFU_ALU32_S0_HINT<bits<7> opc, dag outs, dag ins, string asmstr,
                            list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<38> reserved = 0;    // [37:0] unused
  let s0 = {FU, opcode, reserved};  // MSB-first -> s0[47:0]
}

// ALU32 CSR (CSRR/CSRW) in S0: csr(uimm8) + rs/rt (GPR32).
// Payload 3+7+26+8+4 = 48b.
class HaydnFU_ALU32_S0_CSR<bits<7> opc, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<26> reserved = 0;    // [37:12] unused
  bits<8> csr;              // uimm8 CSR index
  bits<4> r;                // GPR32 (rt for CSRR, rs for CSRW)
  bits<4> rs = 0;           // vestigial legacy $rs (unencoded, always 0)
  let s0 = {FU, opcode, reserved, csr, r};  // MSB-first -> s0[47:0]
}

// TODO(spec): SET_HWLOOP / SET_HWLOOP_F2 / SET_HWLOOP_REG operand encoding is
// under-specified in the ISA reference (HWLRIII / HWLRIIR / HWLRRRR shape
// names only). Best-effort layout below reserves operand bits per the WIDE
// SET_HWLOOP model (encoding_manual.md §5.11-5.13: loop_id uimm1 + count imm +
// target imm). Authoring s0-only so the codepoints exist; P2 will refine the
// field split once the spec field layout is confirmed.

// ALU32 SET_HWLOOP in S0: loop_id(uimm1), count(simm20), target(simm10).
// TODO(spec): exact field split of count vs target TBD.
// Payload 3+7+7+1+20+10 = 48b.
class HaydnFU_ALU32_S0_HWLOOP<bits<7> opc, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<7> reserved = 0;     // [37:31] unused
  bits<1> loop_id;          // 0=loop0, 1=loop1
  bits<20> count;           // iteration count
  bits<10> target;          // loop-body target (placeholder width)
  let s0 = {FU, opcode, reserved, loop_id, count, target};  // s0[47:0]
}

// ALU32 SET_HWLOOP_F2 in S0: loop_id(uimm1), count(simm20), rs(GPR32).
// TODO(spec): HWLRIIR field split TBD.
// Payload 3+7+13+1+20+4 = 48b.
class HaydnFU_ALU32_S0_HWLOOP_F2<bits<7> opc, dag outs, dag ins,
                                 string asmstr, list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<13> reserved = 0;    // [37:25] unused
  bits<1> loop_id;
  bits<20> count;
  bits<4> rs;
  let s0 = {FU, opcode, reserved, loop_id, count, rs};  // s0[47:0]
}

// ALU32 SET_HWLOOP_REG in S0: 4 GPR32 (HWLRRRR: loop_id implied via reg).
// TODO(spec): HWLRRRR exact reg roles TBD (count/start/end/step?).
// Payload 3+7+9+4+4+4+4+13 = 48b.
class HaydnFU_ALU32_S0_HWLOOP_REG<bits<7> opc, dag outs, dag ins,
                                  string asmstr, list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;   // audit: genuinely side-effecting (override base 0)
  let Itinerary = Slot0_ALU;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<9> reserved = 0;     // [37:29] unused
  bits<4> r0;
  bits<4> r1;
  bits<4> r2;
  bits<4> r3;
  bits<13> tail = 0;        // [12:0] unused (pad to 48b)
  let s0 = {FU, opcode, reserved, r0, r1, r2, r3, tail};  // s0[47:0]
}

// ALU32 SET_HWLOOP_W in S0: sel(uimm1), offset1(uimm6), offset2(uimm12)
// cnt(uimm16). Wide hwloop begin/end offsets + 16-bit count (§5.11).
// Payload 3+7+3+16+12+6+1 = 48b.
class HaydnFU_ALU32_S0_HWLOOP_W<bits<7> opc, dag outs, dag ins, string asmstr,
                                list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;
  let isNotDuplicable = 1;
  let Itinerary = Slot0_ALU;
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<3> reserved = 0;     // [37:35] unused
  bits<16> cnt;             // uimm16 iteration count
  bits<12> offset2;         // uimm12 end offset (4-byte units)
  bits<6> offset1;          // uimm6 begin offset (4-byte units)
  bits<1> sel;              // 0=loop0, 1=loop1
  let s0 = {FU, opcode, reserved, cnt, offset2, offset1, sel};  // s0[47:0]
}

// ALU32 SET_HWLOOP_F2_W in S0: sel(uimm1), offset1(uimm6), offset2(uimm12)
// rs(GPR32 count source). Wide hwloop with register count (§5.12).
// Payload 3+7+15+4+12+6+1 = 48b.
class HaydnFU_ALU32_S0_HWLOOP_F2_W<bits<7> opc, dag outs, dag ins,
                                   string asmstr, list<dag> pattern>
    : HaydnSlotS0<outs, ins, asmstr, pattern> {
  let hasSideEffects = 1;
  let isNotDuplicable = 1;
  let Itinerary = Slot0_ALU;
  bits<3> FU = 0b000;       // ALU32
  bits<7> opcode = opc;
  bits<15> reserved = 0;    // [37:23] unused
  bits<4> rs;               // GPR32 count source
  bits<12> offset2;         // uimm12 end offset
  bits<6> offset1;          // uimm6 begin offset
  bits<1> sel;              // 0=loop0, 1=loop1
  let s0 = {FU, opcode, reserved, rs, offset2, offset1, sel};  // s0[47:0]
}

//===----------------------------------------------------------------------===//
// ALU32 RR ops — s0, s1, s2 (3 defs each). Spec codepoints 5-47 (RR family).
//===----------------------------------------------------------------------===//

// #5 ADD32
def ADD32_S0 : HaydnFU_ALU32_S0_RR<0b0000101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "add32\t$rt, $rs1, $rs2", []>;
def ADD32_S1 : HaydnFU_ALU32_S1_RR<0b0000101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "add32\t$rt, $rs1, $rs2", []>;
def ADD32_S2 : HaydnFU_ALU32_S2_RR<0b0000101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "add32\t$rt, $rs1, $rs2", []>;

// #6 ADD32S
def ADD32S_S0 : HaydnFU_ALU32_S0_RR<0b0000110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "add32s\t$rt, $rs1, $rs2", []>;
def ADD32S_S1 : HaydnFU_ALU32_S1_RR<0b0000110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "add32s\t$rt, $rs1, $rs2", []>;
def ADD32S_S2 : HaydnFU_ALU32_S2_RR<0b0000110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "add32s\t$rt, $rs1, $rs2", []>;

// #7 SUB32
def SUB32_S0 : HaydnFU_ALU32_S0_RR<0b0000111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sub32\t$rt, $rs1, $rs2", []>;
def SUB32_S1 : HaydnFU_ALU32_S1_RR<0b0000111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sub32\t$rt, $rs1, $rs2", []>;
def SUB32_S2 : HaydnFU_ALU32_S2_RR<0b0000111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sub32\t$rt, $rs1, $rs2", []>;

// #8 SUB32S
def SUB32S_S0 : HaydnFU_ALU32_S0_RR<0b0001000,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sub32s\t$rt, $rs1, $rs2", []>;
def SUB32S_S1 : HaydnFU_ALU32_S1_RR<0b0001000,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sub32s\t$rt, $rs1, $rs2", []>;
def SUB32S_S2 : HaydnFU_ALU32_S2_RR<0b0001000,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sub32s\t$rt, $rs1, $rs2", []>;

// #13 AND32
def AND32_S0 : HaydnFU_ALU32_S0_RR<0b0001101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "and32\t$rt, $rs1, $rs2", []>;
def AND32_S1 : HaydnFU_ALU32_S1_RR<0b0001101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "and32\t$rt, $rs1, $rs2", []>;
def AND32_S2 : HaydnFU_ALU32_S2_RR<0b0001101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "and32\t$rt, $rs1, $rs2", []>;

// #14 OR32
def OR32_S0 : HaydnFU_ALU32_S0_RR<0b0001110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "or32\t$rt, $rs1, $rs2", []>;
def OR32_S1 : HaydnFU_ALU32_S1_RR<0b0001110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "or32\t$rt, $rs1, $rs2", []>;
def OR32_S2 : HaydnFU_ALU32_S2_RR<0b0001110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "or32\t$rt, $rs1, $rs2", []>;

// #15 XOR32
def XOR32_S0 : HaydnFU_ALU32_S0_RR<0b0001111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "xor32\t$rt, $rs1, $rs2", []>;
def XOR32_S1 : HaydnFU_ALU32_S1_RR<0b0001111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "xor32\t$rt, $rs1, $rs2", []>;
def XOR32_S2 : HaydnFU_ALU32_S2_RR<0b0001111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "xor32\t$rt, $rs1, $rs2", []>;

// #28 SRL32
def SRL32_S0 : HaydnFU_ALU32_S0_RR<0b0011100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "srl32\t$rt, $rs1, $rs2", []>;
def SRL32_S1 : HaydnFU_ALU32_S1_RR<0b0011100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "srl32\t$rt, $rs1, $rs2", []>;
def SRL32_S2 : HaydnFU_ALU32_S2_RR<0b0011100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "srl32\t$rt, $rs1, $rs2", []>;

// #29 SRA32
def SRA32_S0 : HaydnFU_ALU32_S0_RR<0b0011101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sra32\t$rt, $rs1, $rs2", []>;
def SRA32_S1 : HaydnFU_ALU32_S1_RR<0b0011101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sra32\t$rt, $rs1, $rs2", []>;
def SRA32_S2 : HaydnFU_ALU32_S2_RR<0b0011101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sra32\t$rt, $rs1, $rs2", []>;

// #30 SRA32R
def SRA32R_S0 : HaydnFU_ALU32_S0_RR<0b0011110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sra32r\t$rt, $rs1, $rs2", []>;
def SRA32R_S1 : HaydnFU_ALU32_S1_RR<0b0011110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sra32r\t$rt, $rs1, $rs2", []>;
def SRA32R_S2 : HaydnFU_ALU32_S2_RR<0b0011110,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sra32r\t$rt, $rs1, $rs2", []>;

// #31 SLL32
def SLL32_S0 : HaydnFU_ALU32_S0_RR<0b0011111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sll32\t$rt, $rs1, $rs2", []>;
def SLL32_S1 : HaydnFU_ALU32_S1_RR<0b0011111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sll32\t$rt, $rs1, $rs2", []>;
def SLL32_S2 : HaydnFU_ALU32_S2_RR<0b0011111,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sll32\t$rt, $rs1, $rs2", []>;

// #34 MAX32
def MAX32_S0 : HaydnFU_ALU32_S0_RR<0b0100010,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "max32\t$rt, $rs1, $rs2", []>;
def MAX32_S1 : HaydnFU_ALU32_S1_RR<0b0100010,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "max32\t$rt, $rs1, $rs2", []>;
def MAX32_S2 : HaydnFU_ALU32_S2_RR<0b0100010,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "max32\t$rt, $rs1, $rs2", []>;

// #35 MAXU32
def MAXU32_S0 : HaydnFU_ALU32_S0_RR<0b0100011,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "maxu32\t$rt, $rs1, $rs2", []>;
def MAXU32_S1 : HaydnFU_ALU32_S1_RR<0b0100011,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "maxu32\t$rt, $rs1, $rs2", []>;
def MAXU32_S2 : HaydnFU_ALU32_S2_RR<0b0100011,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "maxu32\t$rt, $rs1, $rs2", []>;

// #36 MIN32
def MIN32_S0 : HaydnFU_ALU32_S0_RR<0b0100100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "min32\t$rt, $rs1, $rs2", []>;
def MIN32_S1 : HaydnFU_ALU32_S1_RR<0b0100100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "min32\t$rt, $rs1, $rs2", []>;
def MIN32_S2 : HaydnFU_ALU32_S2_RR<0b0100100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "min32\t$rt, $rs1, $rs2", []>;

// #37 MINU32
def MINU32_S0 : HaydnFU_ALU32_S0_RR<0b0100101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "minu32\t$rt, $rs1, $rs2", []>;
def MINU32_S1 : HaydnFU_ALU32_S1_RR<0b0100101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "minu32\t$rt, $rs1, $rs2", []>;
def MINU32_S2 : HaydnFU_ALU32_S2_RR<0b0100101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "minu32\t$rt, $rs1, $rs2", []>;

// #40 BREV32
def BREV32_S0 : HaydnFU_ALU32_S0_RR<0b0101000,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "brev32\t$rt, $rs1, $rs2", []>;
def BREV32_S1 : HaydnFU_ALU32_S1_RR<0b0101000,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "brev32\t$rt, $rs1, $rs2", []>;
def BREV32_S2 : HaydnFU_ALU32_S2_RR<0b0101000,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "brev32\t$rt, $rs1, $rs2", []>;

// #42 SLT32
def SLT32_S0 : HaydnFU_ALU32_S0_RR<0b0101010,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "slt32\t$rt, $rs1, $rs2", []>;
def SLT32_S1 : HaydnFU_ALU32_S1_RR<0b0101010,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "slt32\t$rt, $rs1, $rs2", []>;
def SLT32_S2 : HaydnFU_ALU32_S2_RR<0b0101010,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "slt32\t$rt, $rs1, $rs2", []>;

// #43 SLTU32
def SLTU32_S0 : HaydnFU_ALU32_S0_RR<0b0101011,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sltu32\t$rt, $rs1, $rs2", []>;
def SLTU32_S1 : HaydnFU_ALU32_S1_RR<0b0101011,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sltu32\t$rt, $rs1, $rs2", []>;
def SLTU32_S2 : HaydnFU_ALU32_S2_RR<0b0101011,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sltu32\t$rt, $rs1, $rs2", []>;

// #44 SLE32
def SLE32_S0 : HaydnFU_ALU32_S0_RR<0b0101100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sle32\t$rt, $rs1, $rs2", []>;
def SLE32_S1 : HaydnFU_ALU32_S1_RR<0b0101100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sle32\t$rt, $rs1, $rs2", []>;
def SLE32_S2 : HaydnFU_ALU32_S2_RR<0b0101100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "sle32\t$rt, $rs1, $rs2", []>;

// #45 SEQ32
def SEQ32_S0 : HaydnFU_ALU32_S0_RR<0b0101101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "seq32\t$rt, $rs1, $rs2", []>;
def SEQ32_S1 : HaydnFU_ALU32_S1_RR<0b0101101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "seq32\t$rt, $rs1, $rs2", []>;
def SEQ32_S2 : HaydnFU_ALU32_S2_RR<0b0101101,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "seq32\t$rt, $rs1, $rs2", []>;

// #46 MOVT32
def MOVT32_S0 : HaydnFU_ALU32_S0_RR<0b0101110,
    (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs1, GPR32:$rs2),
    "movt32\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rt = $rt_src";
}
def MOVT32_S1 : HaydnFU_ALU32_S1_RR<0b0101110,
    (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs1, GPR32:$rs2),
    "movt32\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rt = $rt_src";
}
def MOVT32_S2 : HaydnFU_ALU32_S2_RR<0b0101110,
    (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs1, GPR32:$rs2),
    "movt32\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rt = $rt_src";
}

// #47 MOVF32
def MOVF32_S0 : HaydnFU_ALU32_S0_RR<0b0101111,
    (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs1, GPR32:$rs2),
    "movf32\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rt = $rt_src";
}
def MOVF32_S1 : HaydnFU_ALU32_S1_RR<0b0101111,
    (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs1, GPR32:$rs2),
    "movf32\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rt = $rt_src";
}
def MOVF32_S2 : HaydnFU_ALU32_S2_RR<0b0101111,
    (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs1, GPR32:$rs2),
    "movf32\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rt = $rt_src";
}

//===----------------------------------------------------------------------===//
// ALU32 R ops — s0, s1, s2 (3 defs each). 2 GPR32.
//===----------------------------------------------------------------------===//

// #1 NSA32
def NSA32_S0 : HaydnFU_ALU32_S0_R<0b0000001,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "nsa32\t$rt, $rs", []>;
def NSA32_S1 : HaydnFU_ALU32_S1_R<0b0000001,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "nsa32\t$rt, $rs", []>;
def NSA32_S2 : HaydnFU_ALU32_S2_R<0b0000001,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "nsa32\t$rt, $rs", []>;

// #2 NSAU32
def NSAU32_S0 : HaydnFU_ALU32_S0_R<0b0000010,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "nsau32\t$rt, $rs", []>;
def NSAU32_S1 : HaydnFU_ALU32_S1_R<0b0000010,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "nsau32\t$rt, $rs", []>;
def NSAU32_S2 : HaydnFU_ALU32_S2_R<0b0000010,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "nsau32\t$rt, $rs", []>;

// #16 NOT32
def NOT32_S0 : HaydnFU_ALU32_S0_R<0b0010000,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "not32\t$rt, $rs", []>;
def NOT32_S1 : HaydnFU_ALU32_S1_R<0b0010000,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "not32\t$rt, $rs", []>;
def NOT32_S2 : HaydnFU_ALU32_S2_R<0b0010000,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "not32\t$rt, $rs", []>;

// #20 MOVE32
def MOVE32_S0 : HaydnFU_ALU32_S0_R<0b0010100,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "move32\t$rt, $rs", []>;
def MOVE32_S1 : HaydnFU_ALU32_S1_R<0b0010100,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "move32\t$rt, $rs", []>;
def MOVE32_S2 : HaydnFU_ALU32_S2_R<0b0010100,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "move32\t$rt, $rs", []>;

// #32 ABS32S
def ABS32S_S0 : HaydnFU_ALU32_S0_R<0b0100000,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "abs32s\t$rt, $rs", []>;
def ABS32S_S1 : HaydnFU_ALU32_S1_R<0b0100000,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "abs32s\t$rt, $rs", []>;
def ABS32S_S2 : HaydnFU_ALU32_S2_R<0b0100000,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "abs32s\t$rt, $rs", []>;

// #33 ABS32
def ABS32_S0 : HaydnFU_ALU32_S0_R<0b0100001,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "abs32\t$rt, $rs", []>;
def ABS32_S1 : HaydnFU_ALU32_S1_R<0b0100001,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "abs32\t$rt, $rs", []>;
def ABS32_S2 : HaydnFU_ALU32_S2_R<0b0100001,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "abs32\t$rt, $rs", []>;

// #38 NEG32
def NEG32_S0 : HaydnFU_ALU32_S0_R<0b0100110,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "neg32\t$rt, $rs", []>;
def NEG32_S1 : HaydnFU_ALU32_S1_R<0b0100110,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "neg32\t$rt, $rs", []>;
def NEG32_S2 : HaydnFU_ALU32_S2_R<0b0100110,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "neg32\t$rt, $rs", []>;

// #39 NEG32S
def NEG32S_S0 : HaydnFU_ALU32_S0_R<0b0100111,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "neg32s\t$rt, $rs", []>;
def NEG32S_S1 : HaydnFU_ALU32_S1_R<0b0100111,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "neg32s\t$rt, $rs", []>;
def NEG32S_S2 : HaydnFU_ALU32_S2_R<0b0100111,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "neg32s\t$rt, $rs", []>;

// #41 POPCOUNT32
def POPCOUNT32_S0 : HaydnFU_ALU32_S0_R<0b0101001,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "popcount32\t$rt, $rs", []>;
def POPCOUNT32_S1 : HaydnFU_ALU32_S1_R<0b0101001,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "popcount32\t$rt, $rs", []>;
def POPCOUNT32_S2 : HaydnFU_ALU32_S2_R<0b0101001,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "popcount32\t$rt, $rs", []>;

//===----------------------------------------------------------------------===//
// ALU32 RI20 ops — s0, s1, s2 (3 defs each). rt,rs,simm20.
//===----------------------------------------------------------------------===//

// #9 ADDI32
def ADDI32_S0 : HaydnFU_ALU32_S0_RI20<0b0001001,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "addi32\t$rt, $rs, $imm20", []>;
def ADDI32_S1 : HaydnFU_ALU32_S1_RI20<0b0001001,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "addi32\t$rt, $rs, $imm20", []>;
def ADDI32_S2 : HaydnFU_ALU32_S2_RI20<0b0001001,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "addi32\t$rt, $rs, $imm20", []>;

// #10 ADDI32S
def ADDI32S_S0 : HaydnFU_ALU32_S0_RI20<0b0001010,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "addi32s\t$rt, $rs, $imm20", []>;
def ADDI32S_S1 : HaydnFU_ALU32_S1_RI20<0b0001010,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "addi32s\t$rt, $rs, $imm20", []>;
def ADDI32S_S2 : HaydnFU_ALU32_S2_RI20<0b0001010,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "addi32s\t$rt, $rs, $imm20", []>;

// #11 SUBI32
def SUBI32_S0 : HaydnFU_ALU32_S0_RI20<0b0001011,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "subi32\t$rt, $rs, $imm20", []>;
def SUBI32_S1 : HaydnFU_ALU32_S1_RI20<0b0001011,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "subi32\t$rt, $rs, $imm20", []>;
def SUBI32_S2 : HaydnFU_ALU32_S2_RI20<0b0001011,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "subi32\t$rt, $rs, $imm20", []>;

// #12 SUBI32S
def SUBI32S_S0 : HaydnFU_ALU32_S0_RI20<0b0001100,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "subi32s\t$rt, $rs, $imm20", []>;
def SUBI32S_S1 : HaydnFU_ALU32_S1_RI20<0b0001100,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "subi32s\t$rt, $rs, $imm20", []>;
def SUBI32S_S2 : HaydnFU_ALU32_S2_RI20<0b0001100,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20:$imm20),
    "subi32s\t$rt, $rs, $imm20", []>;

// #17–#19 ANDI/ORI/XORI — ZEXT imm20 (ISA), not SEXT. Peers match logical bases.
// #17 ANDI32
def ANDI32_S0 : HaydnFU_ALU32_S0_RI20<0b0010001,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "andi32\t$rt, $rs, $imm20", []>;
def ANDI32_S1 : HaydnFU_ALU32_S1_RI20<0b0010001,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "andi32\t$rt, $rs, $imm20", []>;
def ANDI32_S2 : HaydnFU_ALU32_S2_RI20<0b0010001,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "andi32\t$rt, $rs, $imm20", []>;

// #18 ORI32
def ORI32_S0 : HaydnFU_ALU32_S0_RI20<0b0010010,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "ori32\t$rt, $rs, $imm20", []>;
def ORI32_S1 : HaydnFU_ALU32_S1_RI20<0b0010010,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "ori32\t$rt, $rs, $imm20", []>;
def ORI32_S2 : HaydnFU_ALU32_S2_RI20<0b0010010,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "ori32\t$rt, $rs, $imm20", []>;

// #19 XORI32
def XORI32_S0 : HaydnFU_ALU32_S0_RI20<0b0010011,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "xori32\t$rt, $rs, $imm20", []>;
def XORI32_S1 : HaydnFU_ALU32_S1_RI20<0b0010011,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "xori32\t$rt, $rs, $imm20", []>;
def XORI32_S2 : HaydnFU_ALU32_S2_RI20<0b0010011,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20:$imm20),
    "xori32\t$rt, $rs, $imm20", []>;

//===----------------------------------------------------------------------===//
// ALU32 RI5 ops — s0, s1, s2 (3 defs each). rt,rs,uimm5.
//===----------------------------------------------------------------------===//

// #24 SRLI32
def SRLI32_S0 : HaydnFU_ALU32_S0_RI5<0b0011000,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srli32\t$rt, $rs, $imm5", []>;
def SRLI32_S1 : HaydnFU_ALU32_S1_RI5<0b0011000,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srli32\t$rt, $rs, $imm5", []>;
def SRLI32_S2 : HaydnFU_ALU32_S2_RI5<0b0011000,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srli32\t$rt, $rs, $imm5", []>;

// #25 SRAI32
def SRAI32_S0 : HaydnFU_ALU32_S0_RI5<0b0011001,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srai32\t$rt, $rs, $imm5", []>;
def SRAI32_S1 : HaydnFU_ALU32_S1_RI5<0b0011001,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srai32\t$rt, $rs, $imm5", []>;
def SRAI32_S2 : HaydnFU_ALU32_S2_RI5<0b0011001,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srai32\t$rt, $rs, $imm5", []>;

// #26 SRAI32R
def SRAI32R_S0 : HaydnFU_ALU32_S0_RI5<0b0011010,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srai32r\t$rt, $rs, $imm5", []>;
def SRAI32R_S1 : HaydnFU_ALU32_S1_RI5<0b0011010,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srai32r\t$rt, $rs, $imm5", []>;
def SRAI32R_S2 : HaydnFU_ALU32_S2_RI5<0b0011010,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srai32r\t$rt, $rs, $imm5", []>;

// #27 SLLI32
def SLLI32_S0 : HaydnFU_ALU32_S0_RI5<0b0011011,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "slli32\t$rt, $rs, $imm5", []>;
def SLLI32_S1 : HaydnFU_ALU32_S1_RI5<0b0011011,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "slli32\t$rt, $rs, $imm5", []>;
def SLLI32_S2 : HaydnFU_ALU32_S2_RI5<0b0011011,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "slli32\t$rt, $rs, $imm5", []>;

//===----------------------------------------------------------------------===//
// ALU32 I12 (LUI) — s0 ONLY (LUI is a wide-immediate form; per spec #23 it is
// the I12 class. s0 hosts long-immediate; s1/s2 do not — s0-only here.)
//===----------------------------------------------------------------------===//

// #23 LUI
// The legacy LUI (FmtI) carries a vestigial $rs operand (always R0/zero, a
// leftover from the I-type template). The slot dag must declare it so the
// runtime MI's operand count matches. `rs` binds to the unencoded rs field
// (always 0); the encoding is unchanged. See BUG 1.
def LUI_S0 : HaydnFU_ALU32_S0_I12<0b0010111,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm12:$imm12),
    "lui\t$rt, $imm12", []>;

//===----------------------------------------------------------------------===//
// ALU32 I8 (ZERO_GPR) — s0, s1, s2 (3 defs). rt only.
//===----------------------------------------------------------------------===//

// #64 ZERO_GPR
def ZERO_GPR_S0 : HaydnFU_ALU32_S0_I8<0b1000000,
    (outs GPR32:$rt), (ins),
    "zero_gpr\t$rt", []>;
def ZERO_GPR_S1 : HaydnFU_ALU32_S1_I8<0b1000000,
    (outs GPR32:$rt), (ins),
    "zero_gpr\t$rt", []>;
def ZERO_GPR_S2 : HaydnFU_ALU32_S2_I8<0b1000000,
    (outs GPR32:$rt), (ins),
    "zero_gpr\t$rt", []>;

//===----------------------------------------------------------------------===//
// ALU32 MOVE32_DR_L / MOVE32_DR_H (#21/22) — s0, s1, s2 (3 defs each).
// Cross-bank rt(GPR32) <-> rsd(DR64). Field layout note: the spec does not
// distinguish L/H in the operand dag (both are R-type, rt + rsd); the L/H
// distinction is opcode-implicit. Authoring one def per slot per variant.
//===----------------------------------------------------------------------===//

// #21 MOVE32_DR_L
def MOVE32_DR_L_S0 : HaydnFU_ALU32_S0_R_DR<0b0010101,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_l\t$rt, $rsd", []>;
def MOVE32_DR_L_S1 : HaydnFU_ALU32_S1_R_DR<0b0010101,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_l\t$rt, $rsd", []>;
def MOVE32_DR_L_S2 : HaydnFU_ALU32_S2_R_DR<0b0010101,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_l\t$rt, $rsd", []>;

// #22 MOVE32_DR_H
def MOVE32_DR_H_S0 : HaydnFU_ALU32_S0_R_DR<0b0010110,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_h\t$rt, $rsd", []>;
def MOVE32_DR_H_S1 : HaydnFU_ALU32_S1_R_DR<0b0010110,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_h\t$rt, $rsd", []>;
def MOVE32_DR_H_S2 : HaydnFU_ALU32_S2_R_DR<0b0010110,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_h\t$rt, $rsd", []>;

//===----------------------------------------------------------------------===//
// S0-ONLY CFG / system family (branches / JAL / JALR / HWLOOP / WFI / CSR).
// Per spec §1.3 these issue in s0 ONLY — NOT authored for s1/s2.
//===----------------------------------------------------------------------===//

// The conditional-branch members carry isBranch/isTerminator for the same
// reason their `_W_S0` peers do: once CodeGen commits a logical to its member
// (materializeMultiOpcodeInstrs setDesc), the member's MCInstrDesc is the only
// one AsmPrinter sees. Without the flags MachineBasicBlock::terminators() comes
// back empty, isBlockOnlyReachableByFallthrough answers true for a block that
// is in fact a branch target, and the label is emitted as a `// %bb.N` comment
// while the branch still references `.LBB0_N` — output that does not assemble.
let isBranch = 1, isTerminator = 1 in {

// #48 BEQ (RI12: rs1,rs2,imm12)
def BEQ_S0 : HaydnFU_ALU32_S0_RI12<0b0110000,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "beq\t$rs, $rt, $imm12", []>;

// #49 BEQZ (I12: rs,imm12)
def BEQZ_S0 : HaydnFU_ALU32_S0_I12_ONE<0b0110001,
    (outs), (ins GPR32:$rs, brtarget_wide_i12:$imm12),
    "beqz\t$rs, $imm12", []>;

// #50 BNE (RI12: rs1,rs2,imm12)
def BNE_S0 : HaydnFU_ALU32_S0_RI12<0b0110010,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "bne\t$rs, $rt, $imm12", []>;

// #51 BNEZ (I12: rs,imm12)
def BNEZ_S0 : HaydnFU_ALU32_S0_I12_ONE<0b0110011,
    (outs), (ins GPR32:$rs, brtarget_wide_i12:$imm12),
    "bnez\t$rs, $imm12", []>;

// #52 BGE (RI12)
def BGE_S0 : HaydnFU_ALU32_S0_RI12<0b0110100,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "bge\t$rs, $rt, $imm12", []>;

// #53 BGEZ (I12)
def BGEZ_S0 : HaydnFU_ALU32_S0_I12_ONE<0b0110101,
    (outs), (ins GPR32:$rs, brtarget_wide_i12:$imm12),
    "bgez\t$rs, $imm12", []>;

// #54 BGEU (RI12)
def BGEU_S0 : HaydnFU_ALU32_S0_RI12<0b0110110,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "bgeu\t$rs, $rt, $imm12", []>;

// #55 BLT (RI12)
def BLT_S0 : HaydnFU_ALU32_S0_RI12<0b0110111,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "blt\t$rs, $rt, $imm12", []>;

// #56 BLTZ (I12)
def BLTZ_S0 : HaydnFU_ALU32_S0_I12_ONE<0b0111000,
    (outs), (ins GPR32:$rs, brtarget_wide_i12:$imm12),
    "bltz\t$rs, $imm12", []>;

// #57 BLTU (RI12)
def BLTU_S0 : HaydnFU_ALU32_S0_RI12<0b0111001,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "bltu\t$rs, $rt, $imm12", []>;

} // isBranch, isTerminator

// #58 JAL (I20: rt,imm20). : the imm20 field stores the signed 20-bit
// PC-relative byte offset directly (no ÷2 scaling on this slot format, unlike
// legacy JAL). calltarget_s0 sign-extends the field on decode so negative
// call offsets render correctly (-5084, not 1043492). The encoder side is
// unchanged (default getMachineOpValue raw encode = byte-identical).
def JAL_S0 : HaydnFU_ALU32_S0_I20<0b0111010,
    (outs GPR32:$rt), (ins calltarget_s0:$imm20),
    "jal\t$rt, $imm20", []>;

// #59 JALR (RI12: rt,rs,imm12). residual: the imm12 field stores the
// signed 12-bit rs-relative BYTE offset directly (no ÷2 scaling on this slot
// format, matching the WIDE JALR_W `calltarget_wide_ri12` Shift=0). The prior
// `simm12` operand has no DecoderMethod, so the tablegen default stored the
// raw 12-bit field UNSIGNED (-100 -> 3996). calltarget_s0_ri12 sign-extends
// the field on decode so negative offsets render correctly. The encoder side
// is unchanged (default getMachineOpValue raw encode = byte-identical).
def JALR_S0 : HaydnFU_ALU32_S0_RI12<0b0111011,
    (outs GPR32:$rt), (ins GPR32:$rs, calltarget_s0_ri12:$imm12),
    "jalr\t$rt, $rs, $imm12", []>;

// #60 SET_HWLOOP (HWLRIII: loop_id, count, target — TODO(spec) field split)
def SET_HWLOOP_S0 : HaydnFU_ALU32_S0_HWLOOP<0b0111100,
    (outs), (ins uimm1:$loop_id, simm20:$count, simm10:$target),
    "set_hwloop\t$loop_id, $count, $target", []>;

// #61 SET_HWLOOP_F2 (HWLRIIR: loop_id, count, rs — TODO(spec) field split)
def SET_HWLOOP_F2_S0 : HaydnFU_ALU32_S0_HWLOOP_F2<0b0111101,
    (outs), (ins uimm1:$loop_id, simm20:$count, GPR32:$rs),
    "set_hwloop_f2\t$loop_id, $count, $rs", []>;

// #62 SET_HWLOOP_REG (HWLRRRR: 4 GPR32 — TODO(spec) reg roles)
def SET_HWLOOP_REG_S0 : HaydnFU_ALU32_S0_HWLOOP_REG<0b0111110,
    (outs), (ins GPR32:$r0, GPR32:$r1, GPR32:$r2, GPR32:$r3),
    "set_hwloop_reg\t$r0, $r1, $r2, $r3", []>;

// #63 WFI (HINT: no operands)
def WFI_S0 : HaydnFU_ALU32_S0_HINT<0b0111111,
    (outs), (ins),
    "wfi", []>;

// #65 CSRR (I8: rt,uimm8)
def CSRR_S0 : HaydnFU_ALU32_S0_CSR<0b1000001,
    (outs GPR32:$r), (ins uimm8:$csr, GPR32:$rs),
    "csrr\t$r, $csr", []>;

// #66 CSRW (I8: uimm8, rs). Two-operand shape (no dead def) — the decoder
// requires every operand to have an Inst field. Legacy CSRW keeps the 3-op
// FmtCSR shape for MC parity; encodeSlotSubInst drops the leading dead def
// when leaveRegion setDesc commits CSRW → CSRW_S0 (see HaydnMCCodeEmitter.cpp).
let hasSideEffects = 1, Defs = [SFR] in
def CSRW_S0 : HaydnFU_ALU32_S0_CSR<0b1000010,
    (outs), (ins uimm8:$csr, GPR32:$r),
    "csrw\t$csr, $r", []>;

// CSRW_W — Bundle128 s0 CSR write (AsmPrinter SETCBR / HWLR path).
let hasSideEffects = 1, Defs = [SFR] in
def CSRW_W_S0 : HaydnFU_ALU32_S0_CSR<0b1010110,
    (outs), (ins uimm8:$csr, GPR32:$r),
    "csrw_w\t$csr, $r", []>;

//===----------------------------------------------------------------------===//
// Family 1 (WideImm-ALU) — DISTINCT-codepoint _W_S0 defs.
//
// The backend emits the legacy `*_W` (Fmt48_WideGPRImm) forms for 20-bit
// immediate ALU. Only TWO such legacy defs exist (HaydnInstrInfo.td:1836-1843):
// ADDI32_W (simm20_wide_abs) — emitted heavily by the selector / lowering
// (HaydnExpandPostIncEarly, HaydnExpandPseudos, HaydnAsmPrinter).
// ORI32_W (uimm20_wide_abs) — exists as a def (paired here for completeness).
// There is NO legacy SUBI32_W / ANDI32_W / XORI32_W / ADDI32S_W / SUBI32S_W
// (verified: only ADDI32_W + ORI32_W derive from Fmt48_WideGPRImm). Bundle128
// _S0 peers for non-existent legacy opcodes would be dead (no AlternateInsts row).
//
// DISTINCT codepoints in the free s0-ALU32-FU range (112-113, above the golden
// set ending at 66). Each `_W_S0` def reuses the EXISTING
// `HaydnFU_ALU32_S0_RI20` layout (same 20-bit imm20 field) but carries the
// wide-reloc operand (`simm20_wide_abs` / `uimm20_wide_abs`) so symbolic
// operands emit `FIXUP_HAYDN_LO20` matching the WIDE encoder. Sharing a
// codepoint with small-imm `_S0` siblings collides on fixup geometry
// (wide-reloc FIXUP_HAYDN_LO20 vs raw-unsigned simm20).
// `isCodeGenOnly = 1` excludes them from the decoder trie; the emitter table
// includes them so `getBinaryCodeForInstr` resolves them. AlternateInsts
// name-peer alts: `<Name>_W` -> `<Name>_W_S0`; leaveRegion setDesc commits
// the member (Desc-as-is encode).
//===----------------------------------------------------------------------===//

// ADDI32_W (legacy Fmt48_WideGPRImm<0x01>, simm20_wide_abs)
// removed isCodeGenOnly=1 — it excluded the def from the AsmMatcher
// breaking round-trip (historical: AsmPrinter emitted addi32_w.s0 earlier).
// The opcode 0b1110000 (112) is distinct from ADDI32_S0 (9) — no decoder
// collision. The def MUST be round-trippable for llc→llvm-mc→objdump.
def ADDI32_W_S0 : HaydnFU_ALU32_S0_RI20<0b1110000,
    (outs GPR32:$rt), (ins GPR32:$rs, simm20_wide_abs:$imm20),
    "addi32_w\t$rt, $rs, $imm20", []>;

// ORI32_W (legacy Fmt48_WideGPRImm<0x06>, uimm20_wide_abs)
// removed isCodeGenOnly=1 (same fix as ADDI32_W_S0 above).
def ORI32_W_S0 : HaydnFU_ALU32_S0_RI20<0b1110001,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm20_wide_abs:$imm20),
    "ori32_w\t$rt, $rs, $imm20", []>;

//===----------------------------------------------------------------------===//
// Family 3 (far branches _W) — DISTINCT-codepoint _W_S0 defs.
//
// The selector emits the legacy `*_W` branch forms (JAL_W, JALR_W, BEQ_W
// BEQZ_W, BNEZ_W). confirmed a pure alias to the non-W `_S0`
// siblings is WRONG: the non-W siblings use Shift=0 operands (raw byte offset
// while the `_W` forms use Shift=1 (offset ÷2, §5.14 D1). A shared
// codepoint would force the decoder to apply ONE shift -> wrong round-trip.
//
// FIX (option 2): DISTINCT codepoints (67-71, free s0-ALU32-FU range
// above the golden set ending at 66) carrying the ÷2-scaled branch-target
// operands. These reuse the EXISTING wide branch operand classes
// (`brtarget_wide_i20` / `brtarget_wide_ri12` / `brtarget_wide_i12`
// `calltarget_wide_ri12`) which already encode the correct N/Shift/IsSigned.
// NO `isCodeGenOnly`: these are real round-trippable Bundle128 s0 instructions
// (each gets its own decoder trie entry with the correct Shift DecoderMethod).
// The existing s0 format classes (`_S0_I20`/`_RI12`/`_I12_ONE`) carry the
// identical field LAYOUT; only the operand SCALING (via the operand class)
// differs from the non-W siblings.
//===----------------------------------------------------------------------===//

// Branch flags mirror the legacy `_W` defs in HaydnInstrInfo.td:1839-1884
// exactly. Without these, BranchRelaxation cannot recognize the `_W_S0`
// opcodes as branches/terminators and skips them, causing far-branch overflow
// (adddf3/divdf3 `-c` crashes). The selector emits the legacy `_W` forms; the
// pre-RA scheduler's materializeMultiOpcodeInstrs setDesc commits them to
// `_W_S0` so by BranchRelaxation time the live opcodes are the Bundle128 peers.

// Calls: caller-saved clobber Defs (same as legacy JAL_W). R12 call-clobbered;
// R14 via regmask only (always CSR, not in Defs). JAL_W_S0: isCall only.
// JALR_W_S0: isTerminator + isCall + isIndirectBranch.
let Defs = [R1, R2, R3, R4, R5, R6, R7, R12,
            D0, D1, D2, D3, D4, D5, D6, D7] in {
let isCall = 1 in
def JAL_W_S0 : HaydnFU_ALU32_S0_I20<0b1000011,
    (outs GPR32:$rt), (ins brtarget_wide_i20:$imm20),
    "jal_w\t$rt, $imm20", []>;

// CB-129: isBarrier — see JALR_W in HaydnInstrInfo.td.
let isTerminator = 1, isBarrier = 1, isCall = 1, isIndirectBranch = 1 in
def JALR_W_S0 : HaydnFU_ALU32_S0_RI12<0b1000100,
    (outs GPR32:$rt), (ins GPR32:$rs, calltarget_wide_ri12:$imm12),
    "jalr_w\t$rt, $rs, $imm12", []>;
} // Defs = caller-saved clobbers for calls (incl. R12 as normal CSR-excluded GPR)

// Conditional branches: isBranch + isTerminator only (no side effects, no
// caller-saved clobbers — mirrors the legacy _W conditional branch block).
let isBranch = 1, isTerminator = 1 in {
// BEQ_W (legacy Fmt48_WideBR_RI12<0x24>, brtarget_wide_ri12, Shift=1)
def BEQ_W_S0 : HaydnFU_ALU32_S0_RI12<0b1000101,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "beq_w\t$rs, $rt, $imm12", []>;

// the remaining 5 RI12 conditional far branches (BNE_W, BGE_W, BGEU_W
// BLT_W, BLTU_W) and 2 I12 zero-branch far forms (BGEZ_W, BLTZ_W) — same
// idiom as BEQ_W/BEQZ_W/BNEZ_W above. HaydnConditionOptimizer / HardwareLoops
// emit the full 6-cond + 4-zero-branch `_W` family; without `_W_S0` peers the
// forcing function fires on whichever the selector picks (BNE_W, BGE_W
// observed in the test sweep). DISTINCT codepoints 72-78 (free s0-ALU32-FU
// range above the set 67-71). Same format classes (RI12 / I12_ONE) and
// ÷2-scaled branch-target operands as the existing siblings. NO
// isCodeGenOnly: real round-trippable Bundle128 s0 instructions (each gets
// its own decoder trie entry with the correct Shift DecoderMethod).

// BNE_W (legacy Fmt48_WideBR_RI12<0x25>, brtarget_wide_ri12, Shift=1)
def BNE_W_S0 : HaydnFU_ALU32_S0_RI12<0b1001111,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "bne_w\t$rs, $rt, $imm12", []>;

// BGE_W (legacy Fmt48_WideBR_RI12<0x26>, brtarget_wide_ri12, Shift=1)
def BGE_W_S0 : HaydnFU_ALU32_S0_RI12<0b1010000,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "bge_w\t$rs, $rt, $imm12", []>;

// BGEU_W (legacy Fmt48_WideBR_RI12<0x27>, brtarget_wide_ri12, Shift=1)
def BGEU_W_S0 : HaydnFU_ALU32_S0_RI12<0b1010001,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "bgeu_w\t$rs, $rt, $imm12", []>;

// BLT_W (legacy Fmt48_WideBR_RI12<0x28>, brtarget_wide_ri12, Shift=1)
def BLT_W_S0 : HaydnFU_ALU32_S0_RI12<0b1010010,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "blt_w\t$rs, $rt, $imm12", []>;

// BLTU_W (legacy Fmt48_WideBR_RI12<0x29>, brtarget_wide_ri12, Shift=1)
def BLTU_W_S0 : HaydnFU_ALU32_S0_RI12<0b1010011,
    (outs), (ins GPR32:$rs, GPR32:$rt, brtarget_wide_ri12:$imm12),
    "bltu_w\t$rs, $rt, $imm12", []>;

// BEQZ_W (legacy Fmt48_WideBR_I12<0x2C>, brtarget_wide_i12, Shift=1)
def BEQZ_W_S0 : HaydnFU_ALU32_S0_I12_ONE<0b1000110,
    (outs), (ins GPR32:$rs, brtarget_wide_i12:$imm12),
    "beqz_w\t$rs, $imm12", []>;

// BNEZ_W (legacy Fmt48_WideBR_I12<0x2D>, brtarget_wide_i12, Shift=1)
def BNEZ_W_S0 : HaydnFU_ALU32_S0_I12_ONE<0b1000111,
    (outs), (ins GPR32:$rs, brtarget_wide_i12:$imm12),
    "bnez_w\t$rs, $imm12", []>;

// BGEZ_W (legacy Fmt48_WideBR_I12<0x2E>, brtarget_wide_i12, Shift=1)
def BGEZ_W_S0 : HaydnFU_ALU32_S0_I12_ONE<0b1010100,
    (outs), (ins GPR32:$rs, brtarget_wide_i12:$imm12),
    "bgez_w\t$rs, $imm12", []>;

// BLTZ_W (legacy Fmt48_WideBR_I12<0x2F>, brtarget_wide_i12, Shift=1)
def BLTZ_W_S0 : HaydnFU_ALU32_S0_I12_ONE<0b1010101,
    (outs), (ins GPR32:$rs, brtarget_wide_i12:$imm12),
    "bltz_w\t$rs, $imm12", []>;
} // isBranch = 1, isTerminator = 1

//===----------------------------------------------------------------------===//
// Family 6 (standalone-parcel migration) — DISTINCT-codepoint _S0 defs.
//
// 's aggressive cutover removed the legacy standalone-parcel emit paths
// (C_*/G_*/16-bit NOP). These opcodes are NOT Bundle128 slot windows — they're
// standalone parcels. They need `_S0` variants so the encoder routes
// them through encodeBundle128 (single-op composite). Without these, standalone
// emission via `llvm-mc` / AsmPrinter hits the forcing function
// ("no Bundle128 form").
//
// Codepoints 72-79 (0b1001000-0b1001111) — free s0-ALU32-FU range above the
// golden set (ending 66) and the far-branch block (67-71). Each def
// reuses the EXISTING s0 format class for its operand shape. `isCodeGenOnly=1`
// excludes them from the decoder trie (the legacy compressed/G-format encodings
// remain the decoder entries); the emitter table includes them so
// getBinaryCodeForInstr resolves them.
//
// AlternateInsts name-peer alts: `NOP` -> `NOP_S0`,
// `C_ADD32` -> `C_ADD32_S0`, etc.; leaveRegion setDesc commits them
// (Desc-as-is encode).
//===----------------------------------------------------------------------===//

// NOP (legacy HaydnInst16, 16-bit all-zero). A standalone NOP as a Bundle128
// slot-0: FU=0, opcode=0, all-zero payload (matches the all-zero composite).
// isReMaterializable mirrors the legacy NOP def.
def NOP_S0 : HaydnFU_ALU32_S0_HINT<0b0000000,
    (outs), (ins), "nop", []> {
  let hasSideEffects = 0;
  let isReMaterializable = 1;
  let isCodeGenOnly = 1;
}

// C_ADD32 (legacy FmtC_ALU32R<0x00>, destructive rt = rt + rs, GPR32Lo).
// Legacy MI operands: [rt(out), rs(in)] — 2 operands. Bundle128 S0 ALU32 R
// (rt, rs) — 2 operands matching the logical shape for setDesc.
let isCodeGenOnly = 1 in
def C_ADD32_S0 : HaydnFU_ALU32_S0_R<0b1001000,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "c_add32\t$rt, $rs", []>;

// C_ADDI32 (legacy FmtC_ALU32SI<0x0>, destructive rt = rt + imm5, GPR32Lo).
// Legacy MI operands: [rt(out), rt_in(in,tied), imm(in)]. Bundle128 S0
// ALU32 RI5 (rt, rs, imm5): setDesc maps $rs <- legacy rt_in (=rt), so rs
// carries the destructive source correctly.
let isCodeGenOnly = 1 in
def C_ADDI32_S0 : HaydnFU_ALU32_S0_RI5<0b1001001,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "c_addi32\t$rt, $rs, $imm5", []>;

// SUB32_G (legacy FmtG_S0_ALU32<0x01>, destructive rd = rd - rs1, GPR32Lo).
// Legacy MI operands: [rd(out), rs1(in)]. Bundle128 S0: use the R class
// (2 ops: rt, rs) so setDesc preserves operand count (RR would be 3 ops).
let isCodeGenOnly = 1 in
def SUB32_G_S0 : HaydnFU_ALU32_S0_R<0b1001010,
    (outs GPR32:$rt), (ins GPR32:$rs),
    "sub32_g\t$rt, $rs", []>;

// SET_HWLOOP_W — Bundle128 s0 form (legacy wide 0x88 geometry mapped into
// ALU32 S0). Live path: real SET_HWLOOP_W before pack → leaveRegion setDesc
// → SET_HWLOOP_W_S0 member → Desc-only encodeBundle128.
//
// must NOT be isCodeGenOnly. That flag excludes the def from the
// DecoderTableS048 trie, so llvm-objdump printed `<unknown>` for every
// memset/memcpy SET_HWLOOP parcel on direct-ELF (ISS never armed HWLR).
// Same class as ADDI32_W_S0: CodeGenOnly for "legacy alias" was wrong
// once Bundle128 emission used these as the real slot instructions.
let hasSideEffects = 1, isNotDuplicable = 1 in
def SET_HWLOOP_W_S0 : HaydnFU_ALU32_S0_HWLOOP_W<0b1001011,
    (outs), (ins uimm1:$sel, hwloop_off1:$offset1, hwloop_off2:$offset2,
                 uimm16_hwloop_cnt:$cnt),
    "set_hwloop_w\t$sel, $offset1, $offset2, $cnt", []>;

// SET_HWLOOP_F2_W — Bundle128 s0 form (register trip count). Primary form
// emitted for runtime-count ZOL (LoopStart / SET_HWLOOP_REG path).
// isCodeGenOnly removed so DecoderTableS048 matches opcode 0b1001100.
let hasSideEffects = 1, isNotDuplicable = 1 in
def SET_HWLOOP_F2_W_S0 : HaydnFU_ALU32_S0_HWLOOP_F2_W<0b1001100,
    (outs), (ins uimm1:$sel, hwloop_off1:$offset1, hwloop_off2:$offset2,
                 GPR32:$rs),
    "set_hwloop_f2_w\t$sel, $offset1, $offset2, $rs", []>;
