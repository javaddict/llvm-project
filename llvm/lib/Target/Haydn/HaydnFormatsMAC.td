//===-- HaydnFormatsMAC.td - CodeGenFormat MAC FU ( P2) -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// P2 -- fresh per-slot port of ALL MAC ops (357 codepoints, Index 1..357
// dense) from the golden source:
// golden/slot1_mac_instruction_list.json (Unit = "D-MAC"; slot1 + slot2
// share the MAC datapath, so slot1_mac is the canonical list).
//
// NOT derived from legacy HaydnInstrInfo*.td. Product encode is Format E
// (golden/format_e_bit_layout_v2). The 9-bit opcode is the golden
// Index field (dense codepoint assignment 1..357, fits 9 bits = max 511);
// FU=0b100 discriminates MAC in each slot's MSB.
// (ISA legality, supersedes the old "s0/s1/s2-legal=357"
// over-generation claim): the MAC datapath is Slot 1/2 ONLY (CLAUDE.md "Slot
// architecture" -- MAC is the Slot 2 dedicated datapath; Slot 0 is the 32-bit
// scalar ALU + Load/Store and has NO MAC unit). FieldSlot `_S1`/`_S2` defs
// and LogicalMaterialize rows are retired (R13); occupancy Mask stays in
// HaydnGenAltOccupancy.inc. These unused HaydnFU_MAC_S1_*/S2_* classes
// remain only as residual format templates. Product members are Format E.
//
// Encoding model:
// MAC FU code = 0b100, opcode width = 9 bits.
// s0[47:45]=FU(100) s0[44:36]=opcode(9b) operands + reserved fill [35:0]
// s1[39:37]=FU(100) s1[36:28]=opcode(9b) operands + reserved fill [27:0]
// s2[39:37]=FU(100) s2[36:28]=opcode(9b) operands + reserved fill [27:0]
//
// Field-order convention (mirrors HaydnFormatsALU32/ALU64/LS/LD proven
// templates): MSB-first concat = { FU, opcode, reserved, <operand fields
// high->low> }. Register operand block: dest HIGHEST, sources LOWEST
// (rsd2 mid for 3-source shapes).
//
// Operand shapes (per golden DR_Read_Port/DR_Write_Port vs GPR_*):
// GRR : 3 GPR32 (rt,rs1,rs2) -- MULL family (4 ops)
// D_R : 2 DR64 (rtd,rsd) -- X4ENERGY16/X4CMUL16 family (5 ops)
// D_RR : 3 DR64 non-accum (rtd,rsd1,rsd2) -- 114 ops
// D_RR2 : 4 DR64 non-accum 2W (rtd1,rtd2,rsd1,rsd2) -- 7 ops
// D_RRA : 3 DR64 accum (rtd,rsd1,rsd2) + rta tied=rtd -- 219 ops
// D_RRA2 : 4 DR64 accum 2W (rtd1,rtd2,rsd1,rsd2) + rta1=rtd1,rta2=rtd2 ties -- 8 ops
//
// Accumulator tie (ra = rd): accumulating MAC ops (X*MULA*, X*MULS*) have an
// extra accumulator-input operand that is tied to the destination and is NOT
// separately encoded -- the encoder drops it; the decoder reconstructs it as a
// copy (mirrors the proven X2MULA32_S2 placeholder / legacy
// LayoutRRRR_MAC TiedDef field). The tie is declared via
// `let Constraints = "$ra = $rd";` and `bits<4> ra;` is declared but NOT
// placed in the `let s<slot> = {... }` concat. For 2-accumulator ops, two
// ties: $rta1=$rtd1 + $rta2=$rtd2.
//
// Register banks (per golden port lists):
// DR64 (D0-D15) -- D-MAC 64-bit datapath, the bulk of MAC ops.
// GPR32 (R0-R15) -- the 4 MULL* ops (GPR-only low-word multiplies).
//
// Additive only: this file declares the MAC FU format classes and concrete
// per-op defs. Does NOT touch HaydnSlots.td, Haydn.td, CodeGenFormat backend
// or generated.inc files. Replaces the prior P1b X2MULA32_S{1,2} reps.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// S0 (48-bit window) MAC format classes -- one per operand shape.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// S1 (40-bit window) MAC format classes -- one per operand shape.
//===----------------------------------------------------------------------===//

// MAC GRR in S1: 3 GPR32 (rt, rs1, rs2).
// Payload 3(FU)+9(opcode)+16(reserved)+<ops> = 40b.
class HaydnFU_MAC_S1_GRR<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<16> reserved = 0;
  bits<4> rt;
  bits<4> rs1;
  bits<4> rs2;
  let s1 = {FU, opcode, reserved, rt, rs2, rs1};  // MSB-first -> s1[39:0]
}

// MAC D_R in S1: 2 DR64 (rtd, rsd).
// Payload 3(FU)+9(opcode)+20(reserved)+<ops> = 40b.
class HaydnFU_MAC_S1_D_R<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<20> reserved = 0;
  bits<4> rtd;              // destination (DR64)
  bits<4> rsd;              // source (DR64)
  let s1 = {FU, opcode, reserved, rtd, rsd};  // MSB-first -> s1[39:0]
}

// MAC D_RR in S1: 3 DR64 non-accum (rtd, rsd1, rsd2).
// Payload 3(FU)+9(opcode)+16(reserved)+<ops> = 40b.
class HaydnFU_MAC_S1_D_RR<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<16> reserved = 0;
  bits<4> rtd;              // destination (DR64)
  bits<4> rsd1;             // source 1 (DR64)
  bits<4> rsd2;             // source 2 (DR64)
  let s1 = {FU, opcode, reserved, rtd, rsd2, rsd1};  // MSB-first -> s1[39:0]
}

// MAC D_RR2 in S1: 4 DR64 non-accum 2W (rtd1, rtd2, rsd1, rsd2).
// Payload 3(FU)+9(opcode)+12(reserved)+<ops> = 40b.
class HaydnFU_MAC_S1_D_RR2<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<12> reserved = 0;
  bits<4> rtd1;             // destination 1 (DR64)
  bits<4> rtd2;             // destination 2 (DR64)
  bits<4> rsd1;             // source 1 (DR64)
  bits<4> rsd2;             // source 2 (DR64)
  let s1 = {FU, opcode, reserved, rtd1, rtd2, rsd2, rsd1};  // MSB-first -> s1[39:0]
}

// MAC D_RRA in S1: 3 DR64 accum (rtd, rsd1, rsd2) + rta tied=rtd (not encoded).
// Payload 3(FU)+9(opcode)+16(reserved)+<ops> = 40b.
class HaydnFU_MAC_S1_D_RRA<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<16> reserved = 0;
  bits<4> rtd;              // destination (DR64); accumulator write-back
  bits<4> rsd1;             // source 1 (DR64)
  bits<4> rsd2;             // source 2 (DR64)
  bits<4> rta;              // accumulator input (DR64) -- tied to rtd, NOT encoded
  let s1 = {FU, opcode, reserved, rtd, rsd2, rsd1};  // MSB-first -> s1[39:0]
}

// MAC D_RRA2 in S1: 4 DR64 accum 2W (rtd1, rtd2, rsd1, rsd2) + rta1=rtd1,rta2=rtd2 ties.
// Payload 3(FU)+9(opcode)+12(reserved)+<ops> = 40b.
class HaydnFU_MAC_S1_D_RRA2<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS1<outs, ins, asmstr, pattern> {
  let Itinerary = Slot1_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<12> reserved = 0;
  bits<4> rtd1;             // destination 1 (DR64); accumulator 1 write-back
  bits<4> rtd2;             // destination 2 (DR64); accumulator 2 write-back
  bits<4> rsd1;             // source 1 (DR64)
  bits<4> rsd2;             // source 2 (DR64)
  bits<4> rta1;             // accumulator 1 input (DR64) -- tied to rtd1, NOT encoded
  bits<4> rta2;             // accumulator 2 input (DR64) -- tied to rtd2, NOT encoded
  let s1 = {FU, opcode, reserved, rtd1, rtd2, rsd2, rsd1};  // MSB-first -> s1[39:0]
}

//===----------------------------------------------------------------------===//
// S2 (40-bit window) MAC format classes -- one per operand shape.
//===----------------------------------------------------------------------===//

// MAC GRR in S2: 3 GPR32 (rt, rs1, rs2).
// Payload 3(FU)+9(opcode)+16(reserved)+<ops> = 40b.
class HaydnFU_MAC_S2_GRR<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<16> reserved = 0;
  bits<4> rt;
  bits<4> rs1;
  bits<4> rs2;
  let s2 = {FU, opcode, reserved, rt, rs2, rs1};  // MSB-first -> s2[39:0]
}

// MAC D_R in S2: 2 DR64 (rtd, rsd).
// Payload 3(FU)+9(opcode)+20(reserved)+<ops> = 40b.
class HaydnFU_MAC_S2_D_R<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<20> reserved = 0;
  bits<4> rtd;              // destination (DR64)
  bits<4> rsd;              // source (DR64)
  let s2 = {FU, opcode, reserved, rtd, rsd};  // MSB-first -> s2[39:0]
}

// MAC D_RR in S2: 3 DR64 non-accum (rtd, rsd1, rsd2).
// Payload 3(FU)+9(opcode)+16(reserved)+<ops> = 40b.
class HaydnFU_MAC_S2_D_RR<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<16> reserved = 0;
  bits<4> rtd;              // destination (DR64)
  bits<4> rsd1;             // source 1 (DR64)
  bits<4> rsd2;             // source 2 (DR64)
  let s2 = {FU, opcode, reserved, rtd, rsd2, rsd1};  // MSB-first -> s2[39:0]
}

// MAC D_RR2 in S2: 4 DR64 non-accum 2W (rtd1, rtd2, rsd1, rsd2).
// Payload 3(FU)+9(opcode)+12(reserved)+<ops> = 40b.
class HaydnFU_MAC_S2_D_RR2<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<12> reserved = 0;
  bits<4> rtd1;             // destination 1 (DR64)
  bits<4> rtd2;             // destination 2 (DR64)
  bits<4> rsd1;             // source 1 (DR64)
  bits<4> rsd2;             // source 2 (DR64)
  let s2 = {FU, opcode, reserved, rtd1, rtd2, rsd2, rsd1};  // MSB-first -> s2[39:0]
}

// MAC D_RRA in S2: 3 DR64 accum (rtd, rsd1, rsd2) + rta tied=rtd (not encoded).
// Payload 3(FU)+9(opcode)+16(reserved)+<ops> = 40b.
class HaydnFU_MAC_S2_D_RRA<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<16> reserved = 0;
  bits<4> rtd;              // destination (DR64); accumulator write-back
  bits<4> rsd1;             // source 1 (DR64)
  bits<4> rsd2;             // source 2 (DR64)
  bits<4> rta;              // accumulator input (DR64) -- tied to rtd, NOT encoded
  let s2 = {FU, opcode, reserved, rtd, rsd2, rsd1};  // MSB-first -> s2[39:0]
}

// MAC D_RRA2 in S2: 4 DR64 accum 2W (rtd1, rtd2, rsd1, rsd2) + rta1=rtd1,rta2=rtd2 ties.
// Payload 3(FU)+9(opcode)+12(reserved)+<ops> = 40b.
class HaydnFU_MAC_S2_D_RRA2<bits<9> opc, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnSlotS2<outs, ins, asmstr, pattern> {
  let Itinerary = Slot2_MAC;   // single-slot, mirrors encoder commit
  bits<3> FU = 0b100;       // MAC
  bits<9> opcode = opc;     // 9b MAC opcode
  bits<12> reserved = 0;
  bits<4> rtd1;             // destination 1 (DR64); accumulator 1 write-back
  bits<4> rtd2;             // destination 2 (DR64); accumulator 2 write-back
  bits<4> rsd1;             // source 1 (DR64)
  bits<4> rsd2;             // source 2 (DR64)
  bits<4> rta1;             // accumulator 1 input (DR64) -- tied to rtd1, NOT encoded
  bits<4> rta2;             // accumulator 2 input (DR64) -- tied to rtd2, NOT encoded
  let s2 = {FU, opcode, reserved, rtd1, rtd2, rsd2, rsd1};  // MSB-first -> s2[39:0]
}

//===----------------------------------------------------------------------===//
// MAC FieldSlot per-op defs retired (R13). Logical opcodes live in Auto.td;
// product members are Format E. Residual occupancy Mask is S1|S2 (0x6).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC GRR ops (4 ops x 3 slots = 12 defs).
// 3 GPR32 (rt, rs1, rs2)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_R ops (5 ops x 3 slots = 15 defs).
// 2 DR64 (rtd, rsd)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_RR ops (114 ops x 3 slots = 342 defs).
// 3 DR64 non-accum (rtd, rsd1, rsd2)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_RR2 ops (7 ops x 3 slots = 21 defs).
// 4 DR64 non-accum 2W (rtd1, rtd2, rsd1, rsd2).
// (Path B): TRUE 2-output (golden DR_Write_Port=[rtd1,rtd2]). Revert the
// 1-dest collapse — bind BOTH dest fields so no orphan bits-field remains
// (every format-class field maps 1:1 to a dag operand, AIE-faithful). The
// selector now emits a 2-def MI; the encoder reads all 4 bound operands.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_RRA ops (219 ops x 3 slots = 657 defs).
// 3 DR64 accum (rtd, rsd1, rsd2) + rta tied=rtd (not encoded)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC D_RRA2 ops (8 ops x 3 slots = 24 defs).
// 4 DR64 accum 2W (rtd1, rtd2, rsd1, rsd2) + rta1=rtd1, rta2=rtd2 ties.
// (Path B): TRUE 2-output accumulate (golden DR_Write_Port=[rtd1,rtd2]
// DR_Read_Port=[rtd1,rtd2,rsd1,rsd2]). Revert the 1-dest collapse — bind
// BOTH dest fields AND both accumulator inputs so no orphan bits-field remains
// (6 fields = 6 operands, AIE-faithful). The tied-def Constraints force
// regalloc to coalesce rta1->rtd1, rta2->rtd2 so silicon's accumulator read
// (from the encoded dest fields) sees the accumulator value (2-dest form).
//===----------------------------------------------------------------------===//


