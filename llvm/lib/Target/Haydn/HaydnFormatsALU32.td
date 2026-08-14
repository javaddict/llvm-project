// ===-- HaydnFormatsALU32.td - ALU32 per-FU slot formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: license information.
//
// fresh per-slot port of ALL ALU32 ops from the spec
// (golden Format E ALU0/ALU1/ALU2 unit members).
// Authored per-slot (one def per op per legal slot) to future-proof against
// per-slot format divergence. NOT derived from legacy HaydnInstrInfo*.td.
// Product encode is Format E (golden/format_e_bit_layout_v2).
//
// Encoding model (Format E FU/opcode widths):
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
// carries but the encoding does not use. `rs2` is an unencoded field
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
// the I-type template) that the runtime MI carries but the encoding
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
// SET_HWLOOP model (loop_id uimm1 + count imm + target imm; golden HWLR
// displacement <<2). Authoring s0-only so the codepoints exist; refine the
// field split once Format E golden confirms the bit-exact placement.

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
// ALU32 RI5 ops — s0, s1, s2 (3 defs each). rt,rs,uimm5.
//===----------------------------------------------------------------------===//





//===----------------------------------------------------------------------===//
// ALU32 I12 (LUI) — s0 ONLY (LUI is a wide-immediate form; per spec #23 it is
// the I12 class. s0 hosts long-immediate; s1/s2 do not — s0-only here.)
//===----------------------------------------------------------------------===//

// #23 LUI — FieldSlot retired. Logical LUI still carries the vestigial
// $rs operand (always R0/zero) so runtime MI operand count matches.

//===----------------------------------------------------------------------===//
// ALU32 I8 (ZERO_GPR) — s0, s1, s2 (3 defs). rt only.
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// ALU32 MOVE32_DR_L / MOVE32_DR_H (#21/22) — s0, s1, s2 (3 defs each).
// Cross-bank rt(GPR32) <- rsd(DR64) lane extract. Explicit bank
// crossing only — not a soft alias of a GPR pair for DR64. Prefer e2e DR64
// ops; PostSelect elideCrossBankRoundTrips removes identity extract+repack.
// Field layout: L/H share R-type (rt + rsd); distinction is opcode-implicit.
// One def per slot per variant.
//===----------------------------------------------------------------------===//



//===----------------------------------------------------------------------===//
// S0-ONLY CFG / system family. Compact non-W FieldSlots (BEQ/JAL/CSR/LUI/
// SET_HWLOOP) are retired — occupancy uses Format E e0 members. WFI has no
// Format E member, so WFI_S0 stays. *_W_S0 FieldSlots stay (StripWide=false).
//===----------------------------------------------------------------------===//

// #63 WFI (HINT: no operands). No Format E member — occupancy Fallback.
def WFI_S0 : HaydnFU_ALU32_S0_HINT<0b0111111,
    (outs), (ins),
    "wfi", []>;

// CSRW_W — s0 CSR write (AsmPrinter SETCBR / HWLR path).
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
// (verified: only ADDI32_W + ORI32_W derive from Fmt48_WideGPRImm). s0
// peers for non-existent legacy opcodes would be dead (no AlternateInsts row).
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
// siblings is WRONG historically: `_W` used Shift=1. GE96-03: both are
// byte PC+imm (Shift=0). Distinct codepoints remain for operand-class
// pairing; scale no longer differs.
//
// FIX (option 2): DISTINCT codepoints (67-71, free s0-ALU32-FU range
// above the golden set ending at 66) carrying the ÷2-scaled branch-target
// operands. These reuse the EXISTING wide branch operand classes
// (`brtarget_wide_i20` / `brtarget_wide_ri12` / `brtarget_wide_i12`
// `calltarget_wide_ri12`) which already encode the correct N/Shift/IsSigned.
// NO `isCodeGenOnly`: these are real round-trippable s0 instructions
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
// `_W_S0` so by BranchRelaxation time the live opcodes are the s0 peers.

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
// isCodeGenOnly: real round-trippable s0 instructions (each gets
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
// (C_*/G_*/16-bit NOP). These opcodes are NOT residual slot windows — they're
// standalone parcels. They need `_S0` variants so the encoder routes them
// through the single-op composite path. Without these, standalone emission
// via `llvm-mc` / AsmPrinter hits the forcing function ("no Format E form").
//
// Codepoints 72-79 (0b1001000-0b1001111) — free s0-ALU32-FU range above the
// golden set (ending 66) and the far-branch block (67-71). Each def
// reuses the EXISTING s0 format class for its operand shape. `isCodeGenOnly=1`
// excludes them from the decoder trie (the legacy compressed/G-format encodings
// remain the decoder entries); the emitter table includes them so
// getBinaryCodeForInstr resolves them.
//
// AlternateInsts name-peer alt: `NOP` -> `NOP_S0` (CompletionState pad).
// Compact/G FieldSlots C_ADD32_S0 / C_ADDI32_S0 / SUB32_G_S0 are deleted:
// no LogicalMaterialize row, no C++ enum use, no Format E member.
//===----------------------------------------------------------------------===//

// NOP logical mnemonic. A standalone NOP as a
// slot-0: FU=0, opcode=0, all-zero payload (matches the all-zero composite).
// isReMaterializable mirrors the legacy NOP def.
def NOP_S0 : HaydnFU_ALU32_S0_HINT<0b0000000,
    (outs), (ins), "nop", []> {
  let hasSideEffects = 0;
  let isReMaterializable = 1;
  let isCodeGenOnly = 1;
}

// SET_HWLOOP_W — residual s0 form (legacy wide 0x88 geometry mapped into
// ALU32 S0). Live path: real SET_HWLOOP_W before pack → leaveRegion setDesc
// → SET_HWLOOP_W_S0 member → Desc-only MC encode.
//
// must NOT be isCodeGenOnly. That flag excludes the def from the
// DecoderTableS048 trie, so llvm-objdump printed `<unknown>` for every
// memset/memcpy SET_HWLOOP parcel on direct-ELF (ISS never armed HWLR).
// Same class as ADDI32_W_S0: CodeGenOnly for "legacy alias" was wrong
// once emission used these as the real slot instructions.
let hasSideEffects = 1, isNotDuplicable = 1 in
def SET_HWLOOP_W_S0 : HaydnFU_ALU32_S0_HWLOOP_W<0b1001011,
    (outs), (ins uimm1:$sel, hwloop_off1:$offset1, hwloop_off2:$offset2,
                 uimm16_hwloop_cnt:$cnt),
    "set_hwloop_w\t$sel, $offset1, $offset2, $cnt", []>;

// SET_HWLOOP_F2_W — s0 form (register trip count). Primary form
// emitted for runtime-count ZOL (LoopStart / SET_HWLOOP_REG path).
// isCodeGenOnly removed so DecoderTableS048 matches opcode 0b1001100.
let hasSideEffects = 1, isNotDuplicable = 1 in
def SET_HWLOOP_F2_W_S0 : HaydnFU_ALU32_S0_HWLOOP_F2_W<0b1001100,
    (outs), (ins uimm1:$sel, hwloop_off1:$offset1, hwloop_off2:$offset2,
                 GPR32:$rs),
    "set_hwloop_f2_w\t$sel, $offset1, $offset2, $rs", []>;
