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
// NOT derived from legacy HaydnInstrInfo*.td, haydn_instruction_db.json, or
// stale HaydnFlexLayout -- fresh golden port. The 9-bit opcode is the golden
// Index field (dense codepoint assignment 1..357, fits 9 bits = max 511);
// FU=0b100 discriminates MAC in each slot's MSB (encoding_manual_flex.md
// sec 1.2). (ISA legality, supersedes the old "s0/s1/s2-legal=357"
// over-generation claim): the MAC datapath is Slot 1/2 ONLY (CLAUDE.md "Slot
// architecture" -- MAC is the Slot 2 dedicated datapath; Slot 0 is the 32-bit
// scalar ALU + Load/Store and has NO MAC unit). Every MAC op emits exactly 2
// defs (_S1/_S2); the mechanically-over-generated _S0 variants and
// their orphan HaydnFU_MAC_S0_* format classes were deleted.
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
// MAC per-op defs (357 ops x 3 slots = 1071 defs).
// Mnemonic lower-case with underscore -> dot substitution (e.g. MUL64_LL
// > mul64.ll). Phase 3: all slots share bare AsmStrings (no `.sN` infix).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MAC GRR ops (4 ops x 3 slots = 12 defs).
// 3 GPR32 (rt, rs1, rs2)
//===----------------------------------------------------------------------===//

def MULL_S1 : HaydnFU_MAC_S1_GRR<0b000000001,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "mull\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rt = $rs2";
}
def MULL_S2 : HaydnFU_MAC_S2_GRR<0b000000001,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "mull\t$rt, $rs1, $rs2", []> {
  let Constraints = "$rt = $rs2";
}

def MULSSH_S1 : HaydnFU_MAC_S1_GRR<0b000000010,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "mulssh\t$rt, $rs1, $rs2", []>;
def MULSSH_S2 : HaydnFU_MAC_S2_GRR<0b000000010,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "mulssh\t$rt, $rs1, $rs2", []>;

def MULSUH_S1 : HaydnFU_MAC_S1_GRR<0b000000011,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "mulsuh\t$rt, $rs1, $rs2", []>;
def MULSUH_S2 : HaydnFU_MAC_S2_GRR<0b000000011,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "mulsuh\t$rt, $rs1, $rs2", []>;

def MULUUH_S1 : HaydnFU_MAC_S1_GRR<0b000000100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "muluuh\t$rt, $rs1, $rs2", []>;
def MULUUH_S2 : HaydnFU_MAC_S2_GRR<0b000000100,
    (outs GPR32:$rt), (ins GPR32:$rs1, GPR32:$rs2),
    "muluuh\t$rt, $rs1, $rs2", []>;


//===----------------------------------------------------------------------===//
// MAC D_R ops (5 ops x 3 slots = 15 defs).
// 2 DR64 (rtd, rsd)
//===----------------------------------------------------------------------===//

def X4ENERGY16_S1 : HaydnFU_MAC_S1_D_R<0b100100101,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4energy16\t$rtd, $rsd", []>;
def X4ENERGY16_S2 : HaydnFU_MAC_S2_D_R<0b100100101,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4energy16\t$rtd, $rsd", []>;

def X4CMUL16_S1 : HaydnFU_MAC_S1_D_R<0b100110000,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16\t$rtd, $rsd", []>;
def X4CMUL16_S2 : HaydnFU_MAC_S2_D_R<0b100110000,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16\t$rtd, $rsd", []>;

def X4CMUL16S_S1 : HaydnFU_MAC_S1_D_R<0b100110001,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16s\t$rtd, $rsd", []>;
def X4CMUL16S_S2 : HaydnFU_MAC_S2_D_R<0b100110001,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16s\t$rtd, $rsd", []>;

def X4CMUL16_F2_S1 : HaydnFU_MAC_S1_D_R<0b100110010,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16.f2\t$rtd, $rsd", []>;
def X4CMUL16_F2_S2 : HaydnFU_MAC_S2_D_R<0b100110010,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16.f2\t$rtd, $rsd", []>;

def X4CMUL16S_F2_S1 : HaydnFU_MAC_S1_D_R<0b100110011,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16s.f2\t$rtd, $rsd", []>;
def X4CMUL16S_F2_S2 : HaydnFU_MAC_S2_D_R<0b100110011,
    (outs DR64:$rtd), (ins DR64:$rsd),
    "x4cmul16s.f2\t$rtd, $rsd", []>;


//===----------------------------------------------------------------------===//
// MAC D_RR ops (114 ops x 3 slots = 342 defs).
// 3 DR64 non-accum (rtd, rsd1, rsd2)
//===----------------------------------------------------------------------===//

def MUL64_LL_S1 : HaydnFU_MAC_S1_D_RR<0b000000101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.ll\t$rtd, $rsd1, $rsd2", []>;
def MUL64_LL_S2 : HaydnFU_MAC_S2_D_RR<0b000000101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.ll\t$rtd, $rsd1, $rsd2", []>;

def MUL64_LH_S1 : HaydnFU_MAC_S1_D_RR<0b000000110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.lh\t$rtd, $rsd1, $rsd2", []>;
def MUL64_LH_S2 : HaydnFU_MAC_S2_D_RR<0b000000110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.lh\t$rtd, $rsd1, $rsd2", []>;

def MUL64_HL_S1 : HaydnFU_MAC_S1_D_RR<0b000000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.hl\t$rtd, $rsd1, $rsd2", []>;
def MUL64_HL_S2 : HaydnFU_MAC_S2_D_RR<0b000000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.hl\t$rtd, $rsd1, $rsd2", []>;

def MUL64_HH_S1 : HaydnFU_MAC_S1_D_RR<0b000001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.hh\t$rtd, $rsd1, $rsd2", []>;
def MUL64_HH_S2 : HaydnFU_MAC_S2_D_RR<0b000001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.hh\t$rtd, $rsd1, $rsd2", []>;

def MUL64_ULL_S1 : HaydnFU_MAC_S1_D_RR<0b000001001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.ull\t$rtd, $rsd1, $rsd2", []>;
def MUL64_ULL_S2 : HaydnFU_MAC_S2_D_RR<0b000001001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.ull\t$rtd, $rsd1, $rsd2", []>;

def MUL64_ULH_S1 : HaydnFU_MAC_S1_D_RR<0b000001010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.ulh\t$rtd, $rsd1, $rsd2", []>;
def MUL64_ULH_S2 : HaydnFU_MAC_S2_D_RR<0b000001010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.ulh\t$rtd, $rsd1, $rsd2", []>;

def MUL64_UHL_S1 : HaydnFU_MAC_S1_D_RR<0b000001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uhl\t$rtd, $rsd1, $rsd2", []>;
def MUL64_UHL_S2 : HaydnFU_MAC_S2_D_RR<0b000001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uhl\t$rtd, $rsd1, $rsd2", []>;

def MUL64_UHH_S1 : HaydnFU_MAC_S1_D_RR<0b000001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uhh\t$rtd, $rsd1, $rsd2", []>;
def MUL64_UHH_S2 : HaydnFU_MAC_S2_D_RR<0b000001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uhh\t$rtd, $rsd1, $rsd2", []>;

def MUL64_LUL_S1 : HaydnFU_MAC_S1_D_RR<0b000001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.lul\t$rtd, $rsd1, $rsd2", []>;
def MUL64_LUL_S2 : HaydnFU_MAC_S2_D_RR<0b000001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.lul\t$rtd, $rsd1, $rsd2", []>;

def MUL64_LUH_S1 : HaydnFU_MAC_S1_D_RR<0b000001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.luh\t$rtd, $rsd1, $rsd2", []>;
def MUL64_LUH_S2 : HaydnFU_MAC_S2_D_RR<0b000001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.luh\t$rtd, $rsd1, $rsd2", []>;

def MUL64_HUL_S1 : HaydnFU_MAC_S1_D_RR<0b000001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.hul\t$rtd, $rsd1, $rsd2", []>;
def MUL64_HUL_S2 : HaydnFU_MAC_S2_D_RR<0b000001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.hul\t$rtd, $rsd1, $rsd2", []>;

def MUL64_HUH_S1 : HaydnFU_MAC_S1_D_RR<0b000010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.huh\t$rtd, $rsd1, $rsd2", []>;
def MUL64_HUH_S2 : HaydnFU_MAC_S2_D_RR<0b000010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.huh\t$rtd, $rsd1, $rsd2", []>;

def MUL64_ULUL_S1 : HaydnFU_MAC_S1_D_RR<0b000010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.ulul\t$rtd, $rsd1, $rsd2", []>;
def MUL64_ULUL_S2 : HaydnFU_MAC_S2_D_RR<0b000010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.ulul\t$rtd, $rsd1, $rsd2", []>;

def MUL64_ULUH_S1 : HaydnFU_MAC_S1_D_RR<0b000010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uluh\t$rtd, $rsd1, $rsd2", []>;
def MUL64_ULUH_S2 : HaydnFU_MAC_S2_D_RR<0b000010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uluh\t$rtd, $rsd1, $rsd2", []>;

def MUL64_UHUL_S1 : HaydnFU_MAC_S1_D_RR<0b000010011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uhul\t$rtd, $rsd1, $rsd2", []>;
def MUL64_UHUL_S2 : HaydnFU_MAC_S2_D_RR<0b000010011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uhul\t$rtd, $rsd1, $rsd2", []>;

def MUL64_UHUH_S1 : HaydnFU_MAC_S1_D_RR<0b000010100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uhuh\t$rtd, $rsd1, $rsd2", []>;
def MUL64_UHUH_S2 : HaydnFU_MAC_S2_D_RR<0b000010100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul64.uhuh\t$rtd, $rsd1, $rsd2", []>;

def X2DOT32_S1 : HaydnFU_MAC_S1_D_RR<0b001011000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2dot32\t$rtd, $rsd1, $rsd2", []>;
def X2DOT32_S2 : HaydnFU_MAC_S2_D_RR<0b001011000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2dot32\t$rtd, $rsd1, $rsd2", []>;

def FMUL32S_LL_S1 : HaydnFU_MAC_S1_D_RR<0b001011001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul32s.ll\t$rtd, $rsd1, $rsd2", []>;
def FMUL32S_LL_S2 : HaydnFU_MAC_S2_D_RR<0b001011001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul32s.ll\t$rtd, $rsd1, $rsd2", []>;

def FMUL32S_LH_S1 : HaydnFU_MAC_S1_D_RR<0b001011010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul32s.lh\t$rtd, $rsd1, $rsd2", []>;
def FMUL32S_LH_S2 : HaydnFU_MAC_S2_D_RR<0b001011010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul32s.lh\t$rtd, $rsd1, $rsd2", []>;

def FMUL32S_HH_S1 : HaydnFU_MAC_S1_D_RR<0b001011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul32s.hh\t$rtd, $rsd1, $rsd2", []>;
def FMUL32S_HH_S2 : HaydnFU_MAC_S2_D_RR<0b001011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul32s.hh\t$rtd, $rsd1, $rsd2", []>;

def FMULA32S_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b001011100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula32s.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA32S_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b001011100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula32s.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA32S_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b001011101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula32s.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA32S_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b001011101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula32s.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA32S_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b001011110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula32s.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA32S_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b001011110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula32s.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULS32S_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b001011111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmuls32s.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULS32S_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b001011111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmuls32s.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULS32S_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b001100000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmuls32s.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULS32S_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b001100000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmuls32s.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULS32S_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b001100001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmuls32s.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULS32S_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b001100001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmuls32s.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MUL32R_LL_S1 : HaydnFU_MAC_S1_D_RR<0b001100010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32r.ll\t$rtd, $rsd1, $rsd2", []>;
def FF2MUL32R_LL_S2 : HaydnFU_MAC_S2_D_RR<0b001100010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32r.ll\t$rtd, $rsd1, $rsd2", []>;

def FF2MUL32RS_LL_S1 : HaydnFU_MAC_S1_D_RR<0b001100011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32rs.ll\t$rtd, $rsd1, $rsd2", []>;
def FF2MUL32RS_LL_S2 : HaydnFU_MAC_S2_D_RR<0b001100011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32rs.ll\t$rtd, $rsd1, $rsd2", []>;

def FF2MUL32R_LH_S1 : HaydnFU_MAC_S1_D_RR<0b001100100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32r.lh\t$rtd, $rsd1, $rsd2", []>;
def FF2MUL32R_LH_S2 : HaydnFU_MAC_S2_D_RR<0b001100100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32r.lh\t$rtd, $rsd1, $rsd2", []>;

def FF2MUL32RS_LH_S1 : HaydnFU_MAC_S1_D_RR<0b001100101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32rs.lh\t$rtd, $rsd1, $rsd2", []>;
def FF2MUL32RS_LH_S2 : HaydnFU_MAC_S2_D_RR<0b001100101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32rs.lh\t$rtd, $rsd1, $rsd2", []>;

def FF2MUL32R_HH_S1 : HaydnFU_MAC_S1_D_RR<0b001100110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32r.hh\t$rtd, $rsd1, $rsd2", []>;
def FF2MUL32R_HH_S2 : HaydnFU_MAC_S2_D_RR<0b001100110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32r.hh\t$rtd, $rsd1, $rsd2", []>;

def FF2MUL32RS_HH_S1 : HaydnFU_MAC_S1_D_RR<0b001100111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32rs.hh\t$rtd, $rsd1, $rsd2", []>;
def FF2MUL32RS_HH_S2 : HaydnFU_MAC_S2_D_RR<0b001100111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "ff2mul32rs.hh\t$rtd, $rsd1, $rsd2", []>;

def X2FMUL32RS_S1 : HaydnFU_MAC_S1_D_RR<0b001110100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rs\t$rtd, $rsd1, $rsd2", []>;
def X2FMUL32RS_S2 : HaydnFU_MAC_S2_D_RR<0b001110100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rs\t$rtd, $rsd1, $rsd2", []>;

def X2FMUL32RSS_S1 : HaydnFU_MAC_S1_D_RR<0b001110101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rss\t$rtd, $rsd1, $rsd2", []>;
def X2FMUL32RSS_S2 : HaydnFU_MAC_S2_D_RR<0b001110101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32rss\t$rtd, $rsd1, $rsd2", []>;

def X2FMUL32TS_S1 : HaydnFU_MAC_S1_D_RR<0b001111010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32ts\t$rtd, $rsd1, $rsd2", []>;
def X2FMUL32TS_S2 : HaydnFU_MAC_S2_D_RR<0b001111010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmul32ts\t$rtd, $rsd1, $rsd2", []>;

def X2MULPL32_S1 : HaydnFU_MAC_S1_D_RR<0b001111101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulpl32\t$rtd, $rsd1, $rsd2", []>;
def X2MULPL32_S2 : HaydnFU_MAC_S2_D_RR<0b001111101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulpl32\t$rtd, $rsd1, $rsd2", []>;

def X2MULPH32_S1 : HaydnFU_MAC_S1_D_RR<0b010000000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulph32\t$rtd, $rsd1, $rsd2", []>;
def X2MULPH32_S2 : HaydnFU_MAC_S2_D_RR<0b010000000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulph32\t$rtd, $rsd1, $rsd2", []>;

def X2FCMUL32RS_S1 : HaydnFU_MAC_S1_D_RR<0b010000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rs\t$rtd, $rsd1, $rsd2", []>;
def X2FCMUL32RS_S2 : HaydnFU_MAC_S2_D_RR<0b010000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rs\t$rtd, $rsd1, $rsd2", []>;

def X2FCMUL32RSS_S1 : HaydnFU_MAC_S1_D_RR<0b010001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rss\t$rtd, $rsd1, $rsd2", []>;
def X2FCMUL32RSS_S2 : HaydnFU_MAC_S2_D_RR<0b010001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fcmul32rss\t$rtd, $rsd1, $rsd2", []>;

def FMULZAA32S_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa32s.hhll\t$rtd, $rsd1, $rsd2", []>;
def FMULZAA32S_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa32s.hhll\t$rtd, $rsd1, $rsd2", []>;

def FMULZAA32S_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa32s.hllh\t$rtd, $rsd1, $rsd2", []>;
def FMULZAA32S_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa32s.hllh\t$rtd, $rsd1, $rsd2", []>;

def FMULZSS32S_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss32s.hhll\t$rtd, $rsd1, $rsd2", []>;
def FMULZSS32S_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss32s.hhll\t$rtd, $rsd1, $rsd2", []>;

def FMULZSS32S_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss32s.hllh\t$rtd, $rsd1, $rsd2", []>;
def FMULZSS32S_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss32s.hllh\t$rtd, $rsd1, $rsd2", []>;

def FMULZAS32S_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzas32s.hhll\t$rtd, $rsd1, $rsd2", []>;
def FMULZAS32S_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzas32s.hhll\t$rtd, $rsd1, $rsd2", []>;

def FMULZAS32S_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzas32s.hllh\t$rtd, $rsd1, $rsd2", []>;
def FMULZAS32S_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzas32s.hllh\t$rtd, $rsd1, $rsd2", []>;

def FMULZSA32S_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzsa32s.hhll\t$rtd, $rsd1, $rsd2", []>;
def FMULZSA32S_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzsa32s.hhll\t$rtd, $rsd1, $rsd2", []>;

def FMULZSA32S_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzsa32s.hllh\t$rtd, $rsd1, $rsd2", []>;
def FMULZSA32S_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzsa32s.hllh\t$rtd, $rsd1, $rsd2", []>;

def MULZAA32_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzaa32.hhll\t$rtd, $rsd1, $rsd2", []>;
def MULZAA32_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzaa32.hhll\t$rtd, $rsd1, $rsd2", []>;

def MULZAA32_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010011100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzaa32.hllh\t$rtd, $rsd1, $rsd2", []>;
def MULZAA32_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010011100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzaa32.hllh\t$rtd, $rsd1, $rsd2", []>;

def MULZSS32_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010011101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzss32.hhll\t$rtd, $rsd1, $rsd2", []>;
def MULZSS32_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010011101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzss32.hhll\t$rtd, $rsd1, $rsd2", []>;

def MULZSS32_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010011110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzss32.hllh\t$rtd, $rsd1, $rsd2", []>;
def MULZSS32_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010011110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzss32.hllh\t$rtd, $rsd1, $rsd2", []>;

def MULZAS32_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010011111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzas32.hhll\t$rtd, $rsd1, $rsd2", []>;
def MULZAS32_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010011111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzas32.hhll\t$rtd, $rsd1, $rsd2", []>;

def MULZAS32_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010100000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzas32.hllh\t$rtd, $rsd1, $rsd2", []>;
def MULZAS32_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010100000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzas32.hllh\t$rtd, $rsd1, $rsd2", []>;

def MULZSA32_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010100001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzsa32.hhll\t$rtd, $rsd1, $rsd2", []>;
def MULZSA32_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010100001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzsa32.hhll\t$rtd, $rsd1, $rsd2", []>;

def MULZSA32_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010100010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzsa32.hllh\t$rtd, $rsd1, $rsd2", []>;
def MULZSA32_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010100010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulzsa32.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULZAA32R_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010101011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzaa32r.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULZAA32R_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010101011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzaa32r.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULZAA32RS_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010101100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzaa32rs.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULZAA32RS_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010101100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzaa32rs.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULZAA32R_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010101101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzaa32r.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULZAA32R_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010101101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzaa32r.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULZAA32RS_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010101110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzaa32rs.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULZAA32RS_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010101110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzaa32rs.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULZSS32R_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010101111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzss32r.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULZSS32R_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010101111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzss32r.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULZSS32RS_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010110000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzss32rs.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULZSS32RS_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010110000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzss32rs.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULZSS32R_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010110001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzss32r.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULZSS32R_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010110001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzss32r.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULZSS32RS_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010110010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzss32rs.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULZSS32RS_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010110010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzss32rs.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULZAS32R_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010110011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzas32r.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULZAS32R_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010110011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzas32r.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULZAS32RS_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010110100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzas32rs.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULZAS32RS_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010110100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzas32rs.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULZAS32R_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010110101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzas32r.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULZAS32R_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010110101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzas32r.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULZAS32RS_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010110110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzas32rs.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULZAS32RS_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010110110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzas32rs.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULZSA32R_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010110111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzsa32r.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULZSA32R_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010110111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzsa32r.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULZSA32RS_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010111000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzsa32rs.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULZSA32RS_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010111000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzsa32rs.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULZSA32R_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010111001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzsa32r.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULZSA32R_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010111001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzsa32r.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULZSA32RS_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010111010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzsa32rs.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULZSA32RS_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010111010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulzsa32rs.hllh\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS00_S1 : HaydnFU_MAC_S1_D_RR<0b011001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls00\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS00_S2 : HaydnFU_MAC_S2_D_RR<0b011001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls00\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS01_S1 : HaydnFU_MAC_S1_D_RR<0b011001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls01\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS01_S2 : HaydnFU_MAC_S2_D_RR<0b011001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls01\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS02_S1 : HaydnFU_MAC_S1_D_RR<0b011001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls02\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS02_S2 : HaydnFU_MAC_S2_D_RR<0b011001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls02\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS03_S1 : HaydnFU_MAC_S1_D_RR<0b011001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls03\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS03_S2 : HaydnFU_MAC_S2_D_RR<0b011001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls03\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS11_S1 : HaydnFU_MAC_S1_D_RR<0b011001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls11\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS11_S2 : HaydnFU_MAC_S2_D_RR<0b011001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls11\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS12_S1 : HaydnFU_MAC_S1_D_RR<0b011010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls12\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS12_S2 : HaydnFU_MAC_S2_D_RR<0b011010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls12\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS13_S1 : HaydnFU_MAC_S1_D_RR<0b011010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls13\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS13_S2 : HaydnFU_MAC_S2_D_RR<0b011010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls13\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS22_S1 : HaydnFU_MAC_S1_D_RR<0b011010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls22\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS22_S2 : HaydnFU_MAC_S2_D_RR<0b011010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls22\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS23_S1 : HaydnFU_MAC_S1_D_RR<0b011010011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls23\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS23_S2 : HaydnFU_MAC_S2_D_RR<0b011010011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls23\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_LS33_S1 : HaydnFU_MAC_S1_D_RR<0b011010100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls33\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_LS33_S2 : HaydnFU_MAC_S2_D_RR<0b011010100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.ls33\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS00_S1 : HaydnFU_MAC_S1_D_RR<0b011010101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs00\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS00_S2 : HaydnFU_MAC_S2_D_RR<0b011010101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs00\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS01_S1 : HaydnFU_MAC_S1_D_RR<0b011010110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs01\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS01_S2 : HaydnFU_MAC_S2_D_RR<0b011010110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs01\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS02_S1 : HaydnFU_MAC_S1_D_RR<0b011010111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs02\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS02_S2 : HaydnFU_MAC_S2_D_RR<0b011010111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs02\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS03_S1 : HaydnFU_MAC_S1_D_RR<0b011011000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs03\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS03_S2 : HaydnFU_MAC_S2_D_RR<0b011011000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs03\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS11_S1 : HaydnFU_MAC_S1_D_RR<0b011011001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs11\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS11_S2 : HaydnFU_MAC_S2_D_RR<0b011011001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs11\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS12_S1 : HaydnFU_MAC_S1_D_RR<0b011011010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs12\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS12_S2 : HaydnFU_MAC_S2_D_RR<0b011011010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs12\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS13_S1 : HaydnFU_MAC_S1_D_RR<0b011011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs13\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS13_S2 : HaydnFU_MAC_S2_D_RR<0b011011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs13\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS22_S1 : HaydnFU_MAC_S1_D_RR<0b011011100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs22\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS22_S2 : HaydnFU_MAC_S2_D_RR<0b011011100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs22\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS23_S1 : HaydnFU_MAC_S1_D_RR<0b011011101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs23\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS23_S2 : HaydnFU_MAC_S2_D_RR<0b011011101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs23\t$rtd, $rsd1, $rsd2", []>;

def FMUL16_HS33_S1 : HaydnFU_MAC_S1_D_RR<0b011011110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs33\t$rtd, $rsd1, $rsd2", []>;
def FMUL16_HS33_S2 : HaydnFU_MAC_S2_D_RR<0b011011110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmul16.hs33\t$rtd, $rsd1, $rsd2", []>;

def FMULZAA16_LS_11_00_S1 : HaydnFU_MAC_S1_D_RR<0b100000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.ls.11.00\t$rtd, $rsd1, $rsd2", []>;
def FMULZAA16_LS_11_00_S2 : HaydnFU_MAC_S2_D_RR<0b100000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.ls.11.00\t$rtd, $rsd1, $rsd2", []>;

def FMULZAA16_LS_33_22_S1 : HaydnFU_MAC_S1_D_RR<0b100001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.ls.33.22\t$rtd, $rsd1, $rsd2", []>;
def FMULZAA16_LS_33_22_S2 : HaydnFU_MAC_S2_D_RR<0b100001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.ls.33.22\t$rtd, $rsd1, $rsd2", []>;

def FMULZAA16_LS_13_02_S1 : HaydnFU_MAC_S1_D_RR<0b100001001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.ls.13.02\t$rtd, $rsd1, $rsd2", []>;
def FMULZAA16_LS_13_02_S2 : HaydnFU_MAC_S2_D_RR<0b100001001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.ls.13.02\t$rtd, $rsd1, $rsd2", []>;

def FMULZSS16_LS_11_00_S1 : HaydnFU_MAC_S1_D_RR<0b100001010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.ls.11.00\t$rtd, $rsd1, $rsd2", []>;
def FMULZSS16_LS_11_00_S2 : HaydnFU_MAC_S2_D_RR<0b100001010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.ls.11.00\t$rtd, $rsd1, $rsd2", []>;

def FMULZSS16_LS_33_22_S1 : HaydnFU_MAC_S1_D_RR<0b100001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.ls.33.22\t$rtd, $rsd1, $rsd2", []>;
def FMULZSS16_LS_33_22_S2 : HaydnFU_MAC_S2_D_RR<0b100001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.ls.33.22\t$rtd, $rsd1, $rsd2", []>;

def FMULZSS16_LS_13_02_S1 : HaydnFU_MAC_S1_D_RR<0b100001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.ls.13.02\t$rtd, $rsd1, $rsd2", []>;
def FMULZSS16_LS_13_02_S2 : HaydnFU_MAC_S2_D_RR<0b100001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.ls.13.02\t$rtd, $rsd1, $rsd2", []>;

def FMULZAA16_HS_11_00_S1 : HaydnFU_MAC_S1_D_RR<0b100010011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.hs.11.00\t$rtd, $rsd1, $rsd2", []>;
def FMULZAA16_HS_11_00_S2 : HaydnFU_MAC_S2_D_RR<0b100010011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.hs.11.00\t$rtd, $rsd1, $rsd2", []>;

def FMULZAA16_HS_33_22_S1 : HaydnFU_MAC_S1_D_RR<0b100010100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.hs.33.22\t$rtd, $rsd1, $rsd2", []>;
def FMULZAA16_HS_33_22_S2 : HaydnFU_MAC_S2_D_RR<0b100010100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.hs.33.22\t$rtd, $rsd1, $rsd2", []>;

def FMULZAA16_HS_13_02_S1 : HaydnFU_MAC_S1_D_RR<0b100010101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.hs.13.02\t$rtd, $rsd1, $rsd2", []>;
def FMULZAA16_HS_13_02_S2 : HaydnFU_MAC_S2_D_RR<0b100010101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzaa16.hs.13.02\t$rtd, $rsd1, $rsd2", []>;

def FMULZSS16_HS_11_00_S1 : HaydnFU_MAC_S1_D_RR<0b100010110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.hs.11.00\t$rtd, $rsd1, $rsd2", []>;
def FMULZSS16_HS_11_00_S2 : HaydnFU_MAC_S2_D_RR<0b100010110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.hs.11.00\t$rtd, $rsd1, $rsd2", []>;

def FMULZSS16_HS_33_22_S1 : HaydnFU_MAC_S1_D_RR<0b100010111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.hs.33.22\t$rtd, $rsd1, $rsd2", []>;
def FMULZSS16_HS_33_22_S2 : HaydnFU_MAC_S2_D_RR<0b100010111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.hs.33.22\t$rtd, $rsd1, $rsd2", []>;

def FMULZSS16_HS_13_02_S1 : HaydnFU_MAC_S1_D_RR<0b100011000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.hs.13.02\t$rtd, $rsd1, $rsd2", []>;
def FMULZSS16_HS_13_02_S2 : HaydnFU_MAC_S2_D_RR<0b100011000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulzss16.hs.13.02\t$rtd, $rsd1, $rsd2", []>;

def X4DOT16_S1 : HaydnFU_MAC_S1_D_RR<0b100100100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4dot16\t$rtd, $rsd1, $rsd2", []>;
def X4DOT16_S2 : HaydnFU_MAC_S2_D_RR<0b100100100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4dot16\t$rtd, $rsd1, $rsd2", []>;

def X4FMUL16TS_S1 : HaydnFU_MAC_S1_D_RR<0b100100110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16ts\t$rtd, $rsd1, $rsd2", []>;
def X4FMUL16TS_S2 : HaydnFU_MAC_S2_D_RR<0b100100110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16ts\t$rtd, $rsd1, $rsd2", []>;

def X4FMUL16RS_S1 : HaydnFU_MAC_S1_D_RR<0b100100111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rs\t$rtd, $rsd1, $rsd2", []>;
def X4FMUL16RS_S2 : HaydnFU_MAC_S2_D_RR<0b100100111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rs\t$rtd, $rsd1, $rsd2", []>;

def X4FMUL16RSS_S1 : HaydnFU_MAC_S1_D_RR<0b100101000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rss\t$rtd, $rsd1, $rsd2", []>;
def X4FMUL16RSS_S2 : HaydnFU_MAC_S2_D_RR<0b100101000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fmul16rss\t$rtd, $rsd1, $rsd2", []>;

def X4FCMUL16RS_S1 : HaydnFU_MAC_S1_D_RR<0b100101100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rs\t$rtd, $rsd1, $rsd2", []>;
def X4FCMUL16RS_S2 : HaydnFU_MAC_S2_D_RR<0b100101100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rs\t$rtd, $rsd1, $rsd2", []>;

def X4FCMUL16RSS_S1 : HaydnFU_MAC_S1_D_RR<0b100101101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rss\t$rtd, $rsd1, $rsd2", []>;
def X4FCMUL16RSS_S2 : HaydnFU_MAC_S2_D_RR<0b100101101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4fcmul16rss\t$rtd, $rsd1, $rsd2", []>;

def MUL16ZAQ_S1 : HaydnFU_MAC_S1_D_RR<0b100110100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul16zaq\t$rtd, $rsd1, $rsd2", []>;
def MUL16ZAQ_S2 : HaydnFU_MAC_S2_D_RR<0b100110100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mul16zaq\t$rtd, $rsd1, $rsd2", []>;

def X4CMUL16S_H_S1 : HaydnFU_MAC_S1_D_RR<0b100110110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s.h\t$rtd, $rsd1, $rsd2", []>;
def X4CMUL16S_H_S2 : HaydnFU_MAC_S2_D_RR<0b100110110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s.h\t$rtd, $rsd1, $rsd2", []>;

def X4CMUL16S_L_S1 : HaydnFU_MAC_S1_D_RR<0b100110111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s.l\t$rtd, $rsd1, $rsd2", []>;
def X4CMUL16S_L_S2 : HaydnFU_MAC_S2_D_RR<0b100110111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmul16s.l\t$rtd, $rsd1, $rsd2", []>;

def X4CJMUL16S_H_S1 : HaydnFU_MAC_S1_D_RR<0b100111010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s.h\t$rtd, $rsd1, $rsd2", []>;
def X4CJMUL16S_H_S2 : HaydnFU_MAC_S2_D_RR<0b100111010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s.h\t$rtd, $rsd1, $rsd2", []>;

def X4CJMUL16S_L_S1 : HaydnFU_MAC_S1_D_RR<0b100111011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s.l\t$rtd, $rsd1, $rsd2", []>;
def X4CJMUL16S_L_S2 : HaydnFU_MAC_S2_D_RR<0b100111011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmul16s.l\t$rtd, $rsd1, $rsd2", []>;


//===----------------------------------------------------------------------===//
// MAC D_RR2 ops (7 ops x 3 slots = 21 defs).
// 4 DR64 non-accum 2W (rtd1, rtd2, rsd1, rsd2).
// (Path B): TRUE 2-output (golden DR_Write_Port=[rtd1,rtd2]). Revert the
// 1-dest collapse — bind BOTH dest fields so no orphan bits-field remains
// (every format-class field maps 1:1 to a dag operand, AIE-faithful). The
// selector now emits a 2-def MI; the encoder reads all 4 bound operands.
//===----------------------------------------------------------------------===//

def X2MUL32_S1 : HaydnFU_MAC_S1_D_RR2<0b001010101,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mul32\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X2MUL32_S2 : HaydnFU_MAC_S2_D_RR2<0b001010101,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mul32\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X2CMUL32_S1 : HaydnFU_MAC_S1_D_RR2<0b010000011,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X2CMUL32_S2 : HaydnFU_MAC_S2_D_RR2<0b010000011,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X2CMUL32_F2_S1 : HaydnFU_MAC_S1_D_RR2<0b010000100,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32.f2\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X2CMUL32_F2_S2 : HaydnFU_MAC_S2_D_RR2<0b010000100,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32.f2\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X2CMUL32S_S1 : HaydnFU_MAC_S1_D_RR2<0b010000101,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X2CMUL32S_S2 : HaydnFU_MAC_S2_D_RR2<0b010000101,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X2CMUL32S_F2_S1 : HaydnFU_MAC_S1_D_RR2<0b010000110,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s.f2\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X2CMUL32S_F2_S2 : HaydnFU_MAC_S2_D_RR2<0b010000110,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x2cmul32s.f2\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X4MUL16_S1 : HaydnFU_MAC_S1_D_RR2<0b100011111,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x4mul16\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X4MUL16_S2 : HaydnFU_MAC_S2_D_RR2<0b100011111,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x4mul16\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X4FF2MUL16S_S1 : HaydnFU_MAC_S1_D_RR2<0b100101001,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x4ff2mul16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X4FF2MUL16S_S2 : HaydnFU_MAC_S2_D_RR2<0b100101001,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rsd1, DR64:$rsd2),
    "x4ff2mul16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;


//===----------------------------------------------------------------------===//
// MAC D_RRA ops (219 ops x 3 slots = 657 defs).
// 3 DR64 accum (rtd, rsd1, rsd2) + rta tied=rtd (not encoded)
//===----------------------------------------------------------------------===//

def MULA64_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b000010101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b000010101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b000010110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b000010110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_HL_S1 : HaydnFU_MAC_S1_D_RRA<0b000010111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.hl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_HL_S2 : HaydnFU_MAC_S2_D_RRA<0b000010111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.hl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b000011000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b000011000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_ULL_S1 : HaydnFU_MAC_S1_D_RRA<0b000011001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.ull\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_ULL_S2 : HaydnFU_MAC_S2_D_RRA<0b000011001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.ull\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_ULH_S1 : HaydnFU_MAC_S1_D_RRA<0b000011010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.ulh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_ULH_S2 : HaydnFU_MAC_S2_D_RRA<0b000011010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.ulh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_UHL_S1 : HaydnFU_MAC_S1_D_RRA<0b000011011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uhl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_UHL_S2 : HaydnFU_MAC_S2_D_RRA<0b000011011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uhl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_UHH_S1 : HaydnFU_MAC_S1_D_RRA<0b000011100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uhh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_UHH_S2 : HaydnFU_MAC_S2_D_RRA<0b000011100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uhh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_LUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000011101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.lul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_LUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000011101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.lul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_LUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000011110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.luh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_LUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000011110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.luh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_HUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000011111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.hul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_HUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000011111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.hul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_HUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000100000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.huh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_HUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000100000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.huh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_ULUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000100001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.ulul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_ULUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000100001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.ulul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_ULUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000100010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uluh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_ULUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000100010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uluh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_UHUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000100011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uhul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_UHUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000100011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uhul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULA64_UHUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000100100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uhuh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULA64_UHUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000100100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mula64.uhuh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b000100101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b000100101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b000100110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b000100110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_HL_S1 : HaydnFU_MAC_S1_D_RRA<0b000100111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.hl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_HL_S2 : HaydnFU_MAC_S2_D_RRA<0b000100111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.hl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b000101000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b000101000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_ULL_S1 : HaydnFU_MAC_S1_D_RRA<0b000101001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.ull\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_ULL_S2 : HaydnFU_MAC_S2_D_RRA<0b000101001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.ull\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_ULH_S1 : HaydnFU_MAC_S1_D_RRA<0b000101010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.ulh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_ULH_S2 : HaydnFU_MAC_S2_D_RRA<0b000101010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.ulh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_UHL_S1 : HaydnFU_MAC_S1_D_RRA<0b000101011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uhl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_UHL_S2 : HaydnFU_MAC_S2_D_RRA<0b000101011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uhl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_UHH_S1 : HaydnFU_MAC_S1_D_RRA<0b000101100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uhh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_UHH_S2 : HaydnFU_MAC_S2_D_RRA<0b000101100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uhh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_LUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000101101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.lul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_LUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000101101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.lul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_LUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000101110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.luh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_LUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000101110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.luh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_HUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000101111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.hul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_HUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000101111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.hul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_HUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000110000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.huh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_HUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000110000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.huh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_ULUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000110001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.ulul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_ULUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000110001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.ulul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_ULUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000110010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uluh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_ULUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000110010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uluh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_UHUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000110011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uhul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_UHUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000110011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uhul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS64_UHUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000110100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uhuh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS64_UHUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000110100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas64.uhuh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b000110101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b000110101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b000110110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b000110110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_HL_S1 : HaydnFU_MAC_S1_D_RRA<0b000110111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.hl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_HL_S2 : HaydnFU_MAC_S2_D_RRA<0b000110111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.hl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b000111000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b000111000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_ULL_S1 : HaydnFU_MAC_S1_D_RRA<0b000111001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.ull\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_ULL_S2 : HaydnFU_MAC_S2_D_RRA<0b000111001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.ull\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_ULH_S1 : HaydnFU_MAC_S1_D_RRA<0b000111010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.ulh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_ULH_S2 : HaydnFU_MAC_S2_D_RRA<0b000111010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.ulh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_UHL_S1 : HaydnFU_MAC_S1_D_RRA<0b000111011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uhl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_UHL_S2 : HaydnFU_MAC_S2_D_RRA<0b000111011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uhl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_UHH_S1 : HaydnFU_MAC_S1_D_RRA<0b000111100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uhh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_UHH_S2 : HaydnFU_MAC_S2_D_RRA<0b000111100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uhh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_LUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000111101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.lul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_LUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000111101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.lul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_LUH_S1 : HaydnFU_MAC_S1_D_RRA<0b000111110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.luh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_LUH_S2 : HaydnFU_MAC_S2_D_RRA<0b000111110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.luh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_HUL_S1 : HaydnFU_MAC_S1_D_RRA<0b000111111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.hul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_HUL_S2 : HaydnFU_MAC_S2_D_RRA<0b000111111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.hul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_HUH_S1 : HaydnFU_MAC_S1_D_RRA<0b001000000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.huh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_HUH_S2 : HaydnFU_MAC_S2_D_RRA<0b001000000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.huh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_ULUL_S1 : HaydnFU_MAC_S1_D_RRA<0b001000001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.ulul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_ULUL_S2 : HaydnFU_MAC_S2_D_RRA<0b001000001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.ulul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_ULUH_S1 : HaydnFU_MAC_S1_D_RRA<0b001000010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uluh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_ULUH_S2 : HaydnFU_MAC_S2_D_RRA<0b001000010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uluh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_UHUL_S1 : HaydnFU_MAC_S1_D_RRA<0b001000011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uhul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_UHUL_S2 : HaydnFU_MAC_S2_D_RRA<0b001000011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uhul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULS64_UHUH_S1 : HaydnFU_MAC_S1_D_RRA<0b001000100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uhuh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULS64_UHUH_S2 : HaydnFU_MAC_S2_D_RRA<0b001000100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "muls64.uhuh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b001000101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b001000101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b001000110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b001000110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_HL_S1 : HaydnFU_MAC_S1_D_RRA<0b001000111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.hl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_HL_S2 : HaydnFU_MAC_S2_D_RRA<0b001000111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.hl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b001001000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b001001000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_ULL_S1 : HaydnFU_MAC_S1_D_RRA<0b001001001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.ull\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_ULL_S2 : HaydnFU_MAC_S2_D_RRA<0b001001001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.ull\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_ULH_S1 : HaydnFU_MAC_S1_D_RRA<0b001001010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.ulh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_ULH_S2 : HaydnFU_MAC_S2_D_RRA<0b001001010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.ulh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_UHL_S1 : HaydnFU_MAC_S1_D_RRA<0b001001011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uhl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_UHL_S2 : HaydnFU_MAC_S2_D_RRA<0b001001011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uhl\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_UHH_S1 : HaydnFU_MAC_S1_D_RRA<0b001001100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uhh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_UHH_S2 : HaydnFU_MAC_S2_D_RRA<0b001001100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uhh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_LUL_S1 : HaydnFU_MAC_S1_D_RRA<0b001001101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.lul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_LUL_S2 : HaydnFU_MAC_S2_D_RRA<0b001001101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.lul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_LUH_S1 : HaydnFU_MAC_S1_D_RRA<0b001001110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.luh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_LUH_S2 : HaydnFU_MAC_S2_D_RRA<0b001001110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.luh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_HUL_S1 : HaydnFU_MAC_S1_D_RRA<0b001001111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.hul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_HUL_S2 : HaydnFU_MAC_S2_D_RRA<0b001001111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.hul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_HUH_S1 : HaydnFU_MAC_S1_D_RRA<0b001010000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.huh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_HUH_S2 : HaydnFU_MAC_S2_D_RRA<0b001010000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.huh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_ULUL_S1 : HaydnFU_MAC_S1_D_RRA<0b001010001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.ulul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_ULUL_S2 : HaydnFU_MAC_S2_D_RRA<0b001010001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.ulul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_ULUH_S1 : HaydnFU_MAC_S1_D_RRA<0b001010010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uluh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_ULUH_S2 : HaydnFU_MAC_S2_D_RRA<0b001010010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uluh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_UHUL_S1 : HaydnFU_MAC_S1_D_RRA<0b001010011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uhul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_UHUL_S2 : HaydnFU_MAC_S2_D_RRA<0b001010011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uhul\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS64_UHUH_S1 : HaydnFU_MAC_S1_D_RRA<0b001010100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uhuh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULSS64_UHUH_S2 : HaydnFU_MAC_S2_D_RRA<0b001010100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulss64.uhuh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULA32R_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b001101000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32r.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULA32R_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b001101000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32r.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULA32RS_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b001101001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32rs.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULA32RS_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b001101001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32rs.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULA32R_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b001101010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32r.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULA32R_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b001101010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32r.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULA32RS_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b001101011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32rs.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULA32RS_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b001101011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32rs.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULA32R_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b001101100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32r.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULA32R_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b001101100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32r.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULA32RS_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b001101101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32rs.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULA32RS_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b001101101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2mula32rs.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULS32R_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b001101110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32r.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULS32R_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b001101110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32r.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULS32RS_LL_S1 : HaydnFU_MAC_S1_D_RRA<0b001101111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32rs.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULS32RS_LL_S2 : HaydnFU_MAC_S2_D_RRA<0b001101111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32rs.ll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULS32R_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b001110000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32r.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULS32R_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b001110000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32r.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULS32RS_LH_S1 : HaydnFU_MAC_S1_D_RRA<0b001110001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32rs.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULS32RS_LH_S2 : HaydnFU_MAC_S2_D_RRA<0b001110001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32rs.lh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULS32R_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b001110010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32r.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULS32R_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b001110010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32r.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FF2MULS32RS_HH_S1 : HaydnFU_MAC_S1_D_RRA<0b001110011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32rs.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FF2MULS32RS_HH_S2 : HaydnFU_MAC_S2_D_RRA<0b001110011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "ff2muls32rs.hh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def X2FMULA32RS_S1 : HaydnFU_MAC_S1_D_RR<0b001110110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rs\t$rtd, $rsd1, $rsd2", []>;
def X2FMULA32RS_S2 : HaydnFU_MAC_S2_D_RR<0b001110110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rs\t$rtd, $rsd1, $rsd2", []>;

def X2FMULA32RSS_S1 : HaydnFU_MAC_S1_D_RR<0b001110111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rss\t$rtd, $rsd1, $rsd2", []>;
def X2FMULA32RSS_S2 : HaydnFU_MAC_S2_D_RR<0b001110111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmula32rss\t$rtd, $rsd1, $rsd2", []>;

def X2FMULS32RS_S1 : HaydnFU_MAC_S1_D_RR<0b001111000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rs\t$rtd, $rsd1, $rsd2", []>;
def X2FMULS32RS_S2 : HaydnFU_MAC_S2_D_RR<0b001111000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rs\t$rtd, $rsd1, $rsd2", []>;

def X2FMULS32RSS_S1 : HaydnFU_MAC_S1_D_RR<0b001111001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rss\t$rtd, $rsd1, $rsd2", []>;
def X2FMULS32RSS_S2 : HaydnFU_MAC_S2_D_RR<0b001111001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32rss\t$rtd, $rsd1, $rsd2", []>;

def X2FMULA32TS_S1 : HaydnFU_MAC_S1_D_RR<0b001111011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmula32ts\t$rtd, $rsd1, $rsd2", []>;
def X2FMULA32TS_S2 : HaydnFU_MAC_S2_D_RR<0b001111011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmula32ts\t$rtd, $rsd1, $rsd2", []>;

def X2FMULS32TS_S1 : HaydnFU_MAC_S1_D_RR<0b001111100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32ts\t$rtd, $rsd1, $rsd2", []>;
def X2FMULS32TS_S2 : HaydnFU_MAC_S2_D_RR<0b001111100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2fmuls32ts\t$rtd, $rsd1, $rsd2", []>;

def X2MULAPL32_S1 : HaydnFU_MAC_S1_D_RR<0b001111110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulapl32\t$rtd, $rsd1, $rsd2", []>;
def X2MULAPL32_S2 : HaydnFU_MAC_S2_D_RR<0b001111110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulapl32\t$rtd, $rsd1, $rsd2", []>;

def X2MULSPL32_S1 : HaydnFU_MAC_S1_D_RR<0b001111111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulspl32\t$rtd, $rsd1, $rsd2", []>;
def X2MULSPL32_S2 : HaydnFU_MAC_S2_D_RR<0b001111111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulspl32\t$rtd, $rsd1, $rsd2", []>;

def X2MULAPH32_S1 : HaydnFU_MAC_S1_D_RR<0b010000001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulaph32\t$rtd, $rsd1, $rsd2", []>;
def X2MULAPH32_S2 : HaydnFU_MAC_S2_D_RR<0b010000001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulaph32\t$rtd, $rsd1, $rsd2", []>;

def X2MULSPH32_S1 : HaydnFU_MAC_S1_D_RR<0b010000010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulsph32\t$rtd, $rsd1, $rsd2", []>;
def X2MULSPH32_S2 : HaydnFU_MAC_S2_D_RR<0b010000010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x2mulsph32\t$rtd, $rsd1, $rsd2", []>;

def X2FCMULA32RS_S1 : HaydnFU_MAC_S1_D_RRA<0b010001001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rs\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def X2FCMULA32RS_S2 : HaydnFU_MAC_S2_D_RRA<0b010001001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rs\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def X2FCMULA32RSS_S1 : HaydnFU_MAC_S1_D_RRA<0b010001010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rss\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def X2FCMULA32RSS_S2 : HaydnFU_MAC_S2_D_RRA<0b010001010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "x2fcmula32rss\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULAA32S_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010010011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulaa32s.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULAA32S_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010010011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulaa32s.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULAA32S_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b010010100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulaa32s.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULAA32S_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b010010100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulaa32s.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULSS32S_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010010101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulss32s.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULSS32S_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010010101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulss32s.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULSS32S_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b010010110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulss32s.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULSS32S_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b010010110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulss32s.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULAS32S_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010010111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulas32s.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULAS32S_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010010111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulas32s.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULAS32S_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b010011000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulas32s.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULAS32S_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b010011000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulas32s.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULSA32S_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010011001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulsa32s.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULSA32S_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010011001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulsa32s.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULSA32S_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b010011010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulsa32s.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULSA32S_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b010011010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulsa32s.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAA32_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010100011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulaa32.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAA32_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010100011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulaa32.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAA32_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b010100100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulaa32.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAA32_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b010100100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulaa32.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSS32_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010100101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulss32.hhll\t$rtd, $rsd1, $rsd2", []>;
def MULSS32_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010100101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulss32.hhll\t$rtd, $rsd1, $rsd2", []>;

def MULSS32_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010100110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulss32.hllh\t$rtd, $rsd1, $rsd2", []>;
def MULSS32_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010100110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulss32.hllh\t$rtd, $rsd1, $rsd2", []>;

def MULAS32_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010100111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas32.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS32_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010100111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas32.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULAS32_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b010101000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas32.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MULAS32_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b010101000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mulas32.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MULSA32_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b010101001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulsa32.hhll\t$rtd, $rsd1, $rsd2", []>;
def MULSA32_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b010101001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulsa32.hhll\t$rtd, $rsd1, $rsd2", []>;

def MULSA32_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b010101010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulsa32.hllh\t$rtd, $rsd1, $rsd2", []>;
def MULSA32_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b010101010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "mulsa32.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULAA32R_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010111011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulaa32r.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def F2MULAA32R_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010111011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulaa32r.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def F2MULAA32RS_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010111100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulaa32rs.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def F2MULAA32RS_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010111100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulaa32rs.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def F2MULAA32R_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b010111101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulaa32r.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def F2MULAA32R_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b010111101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulaa32r.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def F2MULAA32RS_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b010111110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulaa32rs.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def F2MULAA32RS_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b010111110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulaa32rs.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def F2MULSS32R_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b010111111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulss32r.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def F2MULSS32R_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b010111111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulss32r.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def F2MULSS32RS_HHLL_S1 : HaydnFU_MAC_S1_D_RRA<0b011000000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulss32rs.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def F2MULSS32RS_HHLL_S2 : HaydnFU_MAC_S2_D_RRA<0b011000000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulss32rs.hhll\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def F2MULSS32R_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b011000001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulss32r.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def F2MULSS32R_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b011000001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulss32r.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def F2MULSS32RS_HLLH_S1 : HaydnFU_MAC_S1_D_RRA<0b011000010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulss32rs.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def F2MULSS32RS_HLLH_S2 : HaydnFU_MAC_S2_D_RRA<0b011000010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "f2mulss32rs.hllh\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def F2MULAS32R_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b011000011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulas32r.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULAS32R_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b011000011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulas32r.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULAS32RS_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b011000100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulas32rs.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULAS32RS_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b011000100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulas32rs.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULAS32R_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b011000101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulas32r.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULAS32R_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b011000101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulas32r.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULAS32RS_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b011000110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulas32rs.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULAS32RS_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b011000110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulas32rs.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULSA32R_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b011000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulsa32r.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULSA32R_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b011000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulsa32r.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULSA32RS_HHLL_S1 : HaydnFU_MAC_S1_D_RR<0b011001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulsa32rs.hhll\t$rtd, $rsd1, $rsd2", []>;
def F2MULSA32RS_HHLL_S2 : HaydnFU_MAC_S2_D_RR<0b011001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulsa32rs.hhll\t$rtd, $rsd1, $rsd2", []>;

def F2MULSA32R_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b011001001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulsa32r.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULSA32R_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b011001001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulsa32r.hllh\t$rtd, $rsd1, $rsd2", []>;

def F2MULSA32RS_HLLH_S1 : HaydnFU_MAC_S1_D_RR<0b011001010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulsa32rs.hllh\t$rtd, $rsd1, $rsd2", []>;
def F2MULSA32RS_HLLH_S2 : HaydnFU_MAC_S2_D_RR<0b011001010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "f2mulsa32rs.hllh\t$rtd, $rsd1, $rsd2", []>;

def FMULA16_LS00_S1 : HaydnFU_MAC_S1_D_RRA<0b011011111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls00\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS00_S2 : HaydnFU_MAC_S2_D_RRA<0b011011111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls00\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS01_S1 : HaydnFU_MAC_S1_D_RRA<0b011100000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls01\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS01_S2 : HaydnFU_MAC_S2_D_RRA<0b011100000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls01\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS02_S1 : HaydnFU_MAC_S1_D_RRA<0b011100001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls02\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS02_S2 : HaydnFU_MAC_S2_D_RRA<0b011100001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls02\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS03_S1 : HaydnFU_MAC_S1_D_RRA<0b011100010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls03\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS03_S2 : HaydnFU_MAC_S2_D_RRA<0b011100010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls03\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS11_S1 : HaydnFU_MAC_S1_D_RRA<0b011100011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls11\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS11_S2 : HaydnFU_MAC_S2_D_RRA<0b011100011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls11\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS12_S1 : HaydnFU_MAC_S1_D_RRA<0b011100100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls12\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS12_S2 : HaydnFU_MAC_S2_D_RRA<0b011100100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls12\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS13_S1 : HaydnFU_MAC_S1_D_RRA<0b011100101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls13\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS13_S2 : HaydnFU_MAC_S2_D_RRA<0b011100101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls13\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS22_S1 : HaydnFU_MAC_S1_D_RRA<0b011100110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls22\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS22_S2 : HaydnFU_MAC_S2_D_RRA<0b011100110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls22\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS23_S1 : HaydnFU_MAC_S1_D_RRA<0b011100111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls23\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS23_S2 : HaydnFU_MAC_S2_D_RRA<0b011100111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls23\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_LS33_S1 : HaydnFU_MAC_S1_D_RRA<0b011101000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls33\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_LS33_S2 : HaydnFU_MAC_S2_D_RRA<0b011101000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.ls33\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS00_S1 : HaydnFU_MAC_S1_D_RRA<0b011101001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs00\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS00_S2 : HaydnFU_MAC_S2_D_RRA<0b011101001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs00\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS01_S1 : HaydnFU_MAC_S1_D_RRA<0b011101010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs01\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS01_S2 : HaydnFU_MAC_S2_D_RRA<0b011101010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs01\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS02_S1 : HaydnFU_MAC_S1_D_RRA<0b011101011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs02\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS02_S2 : HaydnFU_MAC_S2_D_RRA<0b011101011,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs02\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS03_S1 : HaydnFU_MAC_S1_D_RRA<0b011101100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs03\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS03_S2 : HaydnFU_MAC_S2_D_RRA<0b011101100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs03\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS11_S1 : HaydnFU_MAC_S1_D_RRA<0b011101101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs11\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS11_S2 : HaydnFU_MAC_S2_D_RRA<0b011101101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs11\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS12_S1 : HaydnFU_MAC_S1_D_RRA<0b011101110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs12\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS12_S2 : HaydnFU_MAC_S2_D_RRA<0b011101110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs12\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS13_S1 : HaydnFU_MAC_S1_D_RRA<0b011101111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs13\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS13_S2 : HaydnFU_MAC_S2_D_RRA<0b011101111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs13\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS22_S1 : HaydnFU_MAC_S1_D_RRA<0b011110000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs22\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS22_S2 : HaydnFU_MAC_S2_D_RRA<0b011110000,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs22\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS23_S1 : HaydnFU_MAC_S1_D_RRA<0b011110001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs23\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS23_S2 : HaydnFU_MAC_S2_D_RRA<0b011110001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs23\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULA16_HS33_S1 : HaydnFU_MAC_S1_D_RRA<0b011110010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs33\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULA16_HS33_S2 : HaydnFU_MAC_S2_D_RRA<0b011110010,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmula16.hs33\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULS16_LS00_S1 : HaydnFU_MAC_S1_D_RR<0b011110011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls00\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS00_S2 : HaydnFU_MAC_S2_D_RR<0b011110011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls00\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS01_S1 : HaydnFU_MAC_S1_D_RR<0b011110100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls01\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS01_S2 : HaydnFU_MAC_S2_D_RR<0b011110100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls01\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS02_S1 : HaydnFU_MAC_S1_D_RR<0b011110101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls02\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS02_S2 : HaydnFU_MAC_S2_D_RR<0b011110101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls02\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS03_S1 : HaydnFU_MAC_S1_D_RR<0b011110110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls03\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS03_S2 : HaydnFU_MAC_S2_D_RR<0b011110110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls03\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS11_S1 : HaydnFU_MAC_S1_D_RR<0b011110111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls11\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS11_S2 : HaydnFU_MAC_S2_D_RR<0b011110111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls11\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS12_S1 : HaydnFU_MAC_S1_D_RR<0b011111000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls12\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS12_S2 : HaydnFU_MAC_S2_D_RR<0b011111000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls12\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS13_S1 : HaydnFU_MAC_S1_D_RR<0b011111001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls13\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS13_S2 : HaydnFU_MAC_S2_D_RR<0b011111001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls13\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS22_S1 : HaydnFU_MAC_S1_D_RR<0b011111010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls22\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS22_S2 : HaydnFU_MAC_S2_D_RR<0b011111010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls22\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS23_S1 : HaydnFU_MAC_S1_D_RR<0b011111011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls23\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS23_S2 : HaydnFU_MAC_S2_D_RR<0b011111011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls23\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_LS33_S1 : HaydnFU_MAC_S1_D_RR<0b011111100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls33\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_LS33_S2 : HaydnFU_MAC_S2_D_RR<0b011111100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.ls33\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS00_S1 : HaydnFU_MAC_S1_D_RR<0b011111101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs00\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS00_S2 : HaydnFU_MAC_S2_D_RR<0b011111101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs00\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS01_S1 : HaydnFU_MAC_S1_D_RR<0b011111110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs01\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS01_S2 : HaydnFU_MAC_S2_D_RR<0b011111110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs01\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS02_S1 : HaydnFU_MAC_S1_D_RR<0b011111111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs02\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS02_S2 : HaydnFU_MAC_S2_D_RR<0b011111111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs02\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS03_S1 : HaydnFU_MAC_S1_D_RR<0b100000000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs03\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS03_S2 : HaydnFU_MAC_S2_D_RR<0b100000000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs03\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS11_S1 : HaydnFU_MAC_S1_D_RR<0b100000001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs11\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS11_S2 : HaydnFU_MAC_S2_D_RR<0b100000001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs11\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS12_S1 : HaydnFU_MAC_S1_D_RR<0b100000010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs12\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS12_S2 : HaydnFU_MAC_S2_D_RR<0b100000010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs12\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS13_S1 : HaydnFU_MAC_S1_D_RR<0b100000011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs13\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS13_S2 : HaydnFU_MAC_S2_D_RR<0b100000011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs13\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS22_S1 : HaydnFU_MAC_S1_D_RR<0b100000100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs22\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS22_S2 : HaydnFU_MAC_S2_D_RR<0b100000100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs22\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS23_S1 : HaydnFU_MAC_S1_D_RR<0b100000101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs23\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS23_S2 : HaydnFU_MAC_S2_D_RR<0b100000101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs23\t$rtd, $rsd1, $rsd2", []>;

def FMULS16_HS33_S1 : HaydnFU_MAC_S1_D_RR<0b100000110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs33\t$rtd, $rsd1, $rsd2", []>;
def FMULS16_HS33_S2 : HaydnFU_MAC_S2_D_RR<0b100000110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmuls16.hs33\t$rtd, $rsd1, $rsd2", []>;

def FMULAA16_LS_11_00_S1 : HaydnFU_MAC_S1_D_RR<0b100001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.ls.11.00\t$rtd, $rsd1, $rsd2", []>;
def FMULAA16_LS_11_00_S2 : HaydnFU_MAC_S2_D_RR<0b100001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.ls.11.00\t$rtd, $rsd1, $rsd2", []>;

def FMULAA16_LS_33_22_S1 : HaydnFU_MAC_S1_D_RR<0b100001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.ls.33.22\t$rtd, $rsd1, $rsd2", []>;
def FMULAA16_LS_33_22_S2 : HaydnFU_MAC_S2_D_RR<0b100001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.ls.33.22\t$rtd, $rsd1, $rsd2", []>;

def FMULAA16_LS_13_02_S1 : HaydnFU_MAC_S1_D_RR<0b100001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.ls.13.02\t$rtd, $rsd1, $rsd2", []>;
def FMULAA16_LS_13_02_S2 : HaydnFU_MAC_S2_D_RR<0b100001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.ls.13.02\t$rtd, $rsd1, $rsd2", []>;

def FMULSS16_LS_11_00_S1 : HaydnFU_MAC_S1_D_RR<0b100010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.ls.11.00\t$rtd, $rsd1, $rsd2", []>;
def FMULSS16_LS_11_00_S2 : HaydnFU_MAC_S2_D_RR<0b100010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.ls.11.00\t$rtd, $rsd1, $rsd2", []>;

def FMULSS16_LS_33_22_S1 : HaydnFU_MAC_S1_D_RR<0b100010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.ls.33.22\t$rtd, $rsd1, $rsd2", []>;
def FMULSS16_LS_33_22_S2 : HaydnFU_MAC_S2_D_RR<0b100010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.ls.33.22\t$rtd, $rsd1, $rsd2", []>;

def FMULSS16_LS_13_02_S1 : HaydnFU_MAC_S1_D_RR<0b100010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.ls.13.02\t$rtd, $rsd1, $rsd2", []>;
def FMULSS16_LS_13_02_S2 : HaydnFU_MAC_S2_D_RR<0b100010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.ls.13.02\t$rtd, $rsd1, $rsd2", []>;

def FMULAA16_HS_11_00_S1 : HaydnFU_MAC_S1_D_RRA<0b100011001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.hs.11.00\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULAA16_HS_11_00_S2 : HaydnFU_MAC_S2_D_RRA<0b100011001,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.hs.11.00\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULAA16_HS_33_22_S1 : HaydnFU_MAC_S1_D_RR<0b100011010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.hs.33.22\t$rtd, $rsd1, $rsd2", []>;
def FMULAA16_HS_33_22_S2 : HaydnFU_MAC_S2_D_RR<0b100011010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.hs.33.22\t$rtd, $rsd1, $rsd2", []>;

def FMULAA16_HS_13_02_S1 : HaydnFU_MAC_S1_D_RR<0b100011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.hs.13.02\t$rtd, $rsd1, $rsd2", []>;
def FMULAA16_HS_13_02_S2 : HaydnFU_MAC_S2_D_RR<0b100011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulaa16.hs.13.02\t$rtd, $rsd1, $rsd2", []>;

def FMULSS16_HS_11_00_S1 : HaydnFU_MAC_S1_D_RRA<0b100011100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulss16.hs.11.00\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def FMULSS16_HS_11_00_S2 : HaydnFU_MAC_S2_D_RRA<0b100011100,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "fmulss16.hs.11.00\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def FMULSS16_HS_33_22_S1 : HaydnFU_MAC_S1_D_RR<0b100011101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.hs.33.22\t$rtd, $rsd1, $rsd2", []>;
def FMULSS16_HS_33_22_S2 : HaydnFU_MAC_S2_D_RR<0b100011101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.hs.33.22\t$rtd, $rsd1, $rsd2", []>;

def FMULSS16_HS_13_02_S1 : HaydnFU_MAC_S1_D_RR<0b100011110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.hs.13.02\t$rtd, $rsd1, $rsd2", []>;
def FMULSS16_HS_13_02_S2 : HaydnFU_MAC_S2_D_RR<0b100011110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "fmulss16.hs.13.02\t$rtd, $rsd1, $rsd2", []>;

def X4FCMULA16RS_S1 : HaydnFU_MAC_S1_D_RRA<0b100101110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rs\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def X4FCMULA16RS_S2 : HaydnFU_MAC_S2_D_RRA<0b100101110,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rs\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def X4FCMULA16RSS_S1 : HaydnFU_MAC_S1_D_RRA<0b100101111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rss\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def X4FCMULA16RSS_S2 : HaydnFU_MAC_S2_D_RRA<0b100101111,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "x4fcmula16rss\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def MUL16AQ_S1 : HaydnFU_MAC_S1_D_RRA<0b100110101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mul16aq\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}
def MUL16AQ_S2 : HaydnFU_MAC_S2_D_RRA<0b100110101,
    (outs DR64:$rtd), (ins DR64:$rta, DR64:$rsd1, DR64:$rsd2),
    "mul16aq\t$rtd, $rsd1, $rsd2", []> {
  let Constraints = "$rta = $rtd";
}

def X4CMULA16S_H_S1 : HaydnFU_MAC_S1_D_RR<0b100111000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s.h\t$rtd, $rsd1, $rsd2", []>;
def X4CMULA16S_H_S2 : HaydnFU_MAC_S2_D_RR<0b100111000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s.h\t$rtd, $rsd1, $rsd2", []>;

def X4CMULA16S_L_S1 : HaydnFU_MAC_S1_D_RR<0b100111001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s.l\t$rtd, $rsd1, $rsd2", []>;
def X4CMULA16S_L_S2 : HaydnFU_MAC_S2_D_RR<0b100111001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cmula16s.l\t$rtd, $rsd1, $rsd2", []>;

def X4CJMULA16S_H_S1 : HaydnFU_MAC_S1_D_RR<0b100111100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s.h\t$rtd, $rsd1, $rsd2", []>;
def X4CJMULA16S_H_S2 : HaydnFU_MAC_S2_D_RR<0b100111100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s.h\t$rtd, $rsd1, $rsd2", []>;

def X4CJMULA16S_L_S1 : HaydnFU_MAC_S1_D_RR<0b100111101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s.l\t$rtd, $rsd1, $rsd2", []>;
def X4CJMULA16S_L_S2 : HaydnFU_MAC_S2_D_RR<0b100111101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "x4cjmula16s.l\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_00_S1 : HaydnFU_MAC_S1_D_RR<0b100111110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.00\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_00_S2 : HaydnFU_MAC_S2_D_RR<0b100111110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.00\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_33_S1 : HaydnFU_MAC_S1_D_RR<0b100111111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.33\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_33_S2 : HaydnFU_MAC_S2_D_RR<0b100111111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.33\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_22_S1 : HaydnFU_MAC_S1_D_RR<0b101000000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.22\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_22_S2 : HaydnFU_MAC_S2_D_RR<0b101000000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.22\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_32_S1 : HaydnFU_MAC_S1_D_RR<0b101000001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.32\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_32_S2 : HaydnFU_MAC_S2_D_RR<0b101000001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.32\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_21_S1 : HaydnFU_MAC_S1_D_RR<0b101000010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.21\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_21_S2 : HaydnFU_MAC_S2_D_RR<0b101000010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.21\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_31_S1 : HaydnFU_MAC_S1_D_RR<0b101000011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.31\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_31_S2 : HaydnFU_MAC_S2_D_RR<0b101000011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.31\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_30_S1 : HaydnFU_MAC_S1_D_RR<0b101000100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.30\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_30_S2 : HaydnFU_MAC_S2_D_RR<0b101000100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.30\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_10_S1 : HaydnFU_MAC_S1_D_RR<0b101000101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.10\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_10_S2 : HaydnFU_MAC_S2_D_RR<0b101000101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.10\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_20_S1 : HaydnFU_MAC_S1_D_RR<0b101000110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.20\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_20_S2 : HaydnFU_MAC_S2_D_RR<0b101000110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.20\t$rtd, $rsd1, $rsd2", []>;

def SMULA16_11_S1 : HaydnFU_MAC_S1_D_RR<0b101000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.11\t$rtd, $rsd1, $rsd2", []>;
def SMULA16_11_S2 : HaydnFU_MAC_S2_D_RR<0b101000111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16.11\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_00_S1 : HaydnFU_MAC_S1_D_RR<0b101001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.00\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_00_S2 : HaydnFU_MAC_S2_D_RR<0b101001000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.00\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_33_S1 : HaydnFU_MAC_S1_D_RR<0b101001001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.33\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_33_S2 : HaydnFU_MAC_S2_D_RR<0b101001001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.33\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_22_S1 : HaydnFU_MAC_S1_D_RR<0b101001010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.22\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_22_S2 : HaydnFU_MAC_S2_D_RR<0b101001010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.22\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_32_S1 : HaydnFU_MAC_S1_D_RR<0b101001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.32\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_32_S2 : HaydnFU_MAC_S2_D_RR<0b101001011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.32\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_21_S1 : HaydnFU_MAC_S1_D_RR<0b101001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.21\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_21_S2 : HaydnFU_MAC_S2_D_RR<0b101001100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.21\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_31_S1 : HaydnFU_MAC_S1_D_RR<0b101001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.31\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_31_S2 : HaydnFU_MAC_S2_D_RR<0b101001101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.31\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_30_S1 : HaydnFU_MAC_S1_D_RR<0b101001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.30\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_30_S2 : HaydnFU_MAC_S2_D_RR<0b101001110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.30\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_10_S1 : HaydnFU_MAC_S1_D_RR<0b101001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.10\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_10_S2 : HaydnFU_MAC_S2_D_RR<0b101001111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.10\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_20_S1 : HaydnFU_MAC_S1_D_RR<0b101010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.20\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_20_S2 : HaydnFU_MAC_S2_D_RR<0b101010000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.20\t$rtd, $rsd1, $rsd2", []>;

def SMULS16_11_S1 : HaydnFU_MAC_S1_D_RR<0b101010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.11\t$rtd, $rsd1, $rsd2", []>;
def SMULS16_11_S2 : HaydnFU_MAC_S2_D_RR<0b101010001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16.11\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_00_S1 : HaydnFU_MAC_S1_D_RR<0b101010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.00\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_00_S2 : HaydnFU_MAC_S2_D_RR<0b101010010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.00\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_33_S1 : HaydnFU_MAC_S1_D_RR<0b101010011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.33\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_33_S2 : HaydnFU_MAC_S2_D_RR<0b101010011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.33\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_22_S1 : HaydnFU_MAC_S1_D_RR<0b101010100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.22\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_22_S2 : HaydnFU_MAC_S2_D_RR<0b101010100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.22\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_32_S1 : HaydnFU_MAC_S1_D_RR<0b101010101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.32\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_32_S2 : HaydnFU_MAC_S2_D_RR<0b101010101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.32\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_21_S1 : HaydnFU_MAC_S1_D_RR<0b101010110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.21\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_21_S2 : HaydnFU_MAC_S2_D_RR<0b101010110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.21\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_31_S1 : HaydnFU_MAC_S1_D_RR<0b101010111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.31\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_31_S2 : HaydnFU_MAC_S2_D_RR<0b101010111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.31\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_30_S1 : HaydnFU_MAC_S1_D_RR<0b101011000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.30\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_30_S2 : HaydnFU_MAC_S2_D_RR<0b101011000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.30\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_10_S1 : HaydnFU_MAC_S1_D_RR<0b101011001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.10\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_10_S2 : HaydnFU_MAC_S2_D_RR<0b101011001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.10\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_20_S1 : HaydnFU_MAC_S1_D_RR<0b101011010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.20\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_20_S2 : HaydnFU_MAC_S2_D_RR<0b101011010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.20\t$rtd, $rsd1, $rsd2", []>;

def SMULA16S_11_S1 : HaydnFU_MAC_S1_D_RR<0b101011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.11\t$rtd, $rsd1, $rsd2", []>;
def SMULA16S_11_S2 : HaydnFU_MAC_S2_D_RR<0b101011011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smula16s.11\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_00_S1 : HaydnFU_MAC_S1_D_RR<0b101011100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.00\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_00_S2 : HaydnFU_MAC_S2_D_RR<0b101011100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.00\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_33_S1 : HaydnFU_MAC_S1_D_RR<0b101011101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.33\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_33_S2 : HaydnFU_MAC_S2_D_RR<0b101011101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.33\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_22_S1 : HaydnFU_MAC_S1_D_RR<0b101011110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.22\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_22_S2 : HaydnFU_MAC_S2_D_RR<0b101011110,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.22\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_32_S1 : HaydnFU_MAC_S1_D_RR<0b101011111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.32\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_32_S2 : HaydnFU_MAC_S2_D_RR<0b101011111,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.32\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_21_S1 : HaydnFU_MAC_S1_D_RR<0b101100000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.21\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_21_S2 : HaydnFU_MAC_S2_D_RR<0b101100000,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.21\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_31_S1 : HaydnFU_MAC_S1_D_RR<0b101100001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.31\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_31_S2 : HaydnFU_MAC_S2_D_RR<0b101100001,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.31\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_30_S1 : HaydnFU_MAC_S1_D_RR<0b101100010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.30\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_30_S2 : HaydnFU_MAC_S2_D_RR<0b101100010,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.30\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_10_S1 : HaydnFU_MAC_S1_D_RR<0b101100011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.10\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_10_S2 : HaydnFU_MAC_S2_D_RR<0b101100011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.10\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_20_S1 : HaydnFU_MAC_S1_D_RR<0b101100100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.20\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_20_S2 : HaydnFU_MAC_S2_D_RR<0b101100100,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.20\t$rtd, $rsd1, $rsd2", []>;

def SMULS16S_11_S1 : HaydnFU_MAC_S1_D_RR<0b101100101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.11\t$rtd, $rsd1, $rsd2", []>;
def SMULS16S_11_S2 : HaydnFU_MAC_S2_D_RR<0b101100101,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rsd2),
    "smuls16s.11\t$rtd, $rsd1, $rsd2", []>;


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

let Constraints = "$rtd1 = $rta1, $rtd2 = $rta2" in {
def X2MULA32_S1 : HaydnFU_MAC_S1_D_RRA2<0b001010110,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x2mula32\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X2MULA32_S2 : HaydnFU_MAC_S2_D_RRA2<0b001010110,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x2mula32\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X2MULS32_S1 : HaydnFU_MAC_S1_D_RRA2<0b001010111,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x2muls32\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X2MULS32_S2 : HaydnFU_MAC_S2_D_RRA2<0b001010111,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x2muls32\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X4MULA16_S1 : HaydnFU_MAC_S1_D_RRA2<0b100100000,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4mula16\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X4MULA16_S2 : HaydnFU_MAC_S2_D_RRA2<0b100100000,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4mula16\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X4MULS16_S1 : HaydnFU_MAC_S1_D_RRA2<0b100100001,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4muls16\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X4MULS16_S2 : HaydnFU_MAC_S2_D_RRA2<0b100100001,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4muls16\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X4MULA16S_S1 : HaydnFU_MAC_S1_D_RRA2<0b100100010,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4mula16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X4MULA16S_S2 : HaydnFU_MAC_S2_D_RRA2<0b100100010,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4mula16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X4MULS16S_S1 : HaydnFU_MAC_S1_D_RRA2<0b100100011,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4muls16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X4MULS16S_S2 : HaydnFU_MAC_S2_D_RRA2<0b100100011,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4muls16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X4FF2MULA16S_S1 : HaydnFU_MAC_S1_D_RRA2<0b100101010,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4ff2mula16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X4FF2MULA16S_S2 : HaydnFU_MAC_S2_D_RRA2<0b100101010,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4ff2mula16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;

def X4FF2MULS16S_S1 : HaydnFU_MAC_S1_D_RRA2<0b100101011,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4ff2muls16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
def X4FF2MULS16S_S2 : HaydnFU_MAC_S2_D_RRA2<0b100101011,
    (outs DR64:$rtd1, DR64:$rtd2), (ins DR64:$rta1, DR64:$rta2, DR64:$rsd1, DR64:$rsd2),
    "x4ff2muls16s\t$rtd1, $rtd2, $rsd1, $rsd2", []>;
}  // Constraints = "$rtd1 = $rta1, $rtd2 = $rta2"

