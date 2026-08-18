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
// SET_HWLOOP / WFI / NOP / *_W) are retired — occupancy uses Format E
// members (WFI peels to the catalog HINT span).
//===----------------------------------------------------------------------===//

// CSRW_W / ADDI32_W / ORI32_W / JAL*_W / B*_W / NOP / SET_HWLOOP*_W / WFI
// FieldSlots retired. Occupancy peels *_W to the compact Format E
// member. Reloc operand classes and branch flags live on those members.
// WFI occupancy fills the generated HINT member (empty dag).
