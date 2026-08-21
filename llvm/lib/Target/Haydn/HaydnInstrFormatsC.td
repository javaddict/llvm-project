//===-- HaydnInstrFormatsC.td - Size-6 TableGen shells ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TableGen size-6 (HaydnInst48 / Fmt48_*) shells for live logical opcodes
// such as ADDI32_W, BEQZ_W, and JAL_W. Product encode is Format E member.
//
//===----------------------------------------------------------------------===//

class HaydnInst48<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnInst<6, outs, ins, asmstr, pattern> {
  let TSFlags{4-3} = 0b10;
  let DecoderNamespace = "Haydn48";
}

// NOTE: The legacy `FmtExtI` class (base 32-bit parcel + 16-bit extension
// parcel, width tag 0b10) was removed. TableGen size-6 shell; product encode
// is Format E member. No `def` inherited FmtExtI.

//===----------------------------------------------------------------------===//
// 48-bit S-format (MOVEI) RETIRED.
//
// The Fmt48_MOVEI class and its 2 defs (MOVEI_H/MOVEI_L) had zero live C++
// consumers subsequent and are removed.
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// 48-bit Wide-imm ALU. TableGen size-6 shell; product encode is Format E member.
//
// AUDIT : 8 Fmt48_Wide* classes below are KEPT — each is
// the base class of >=1 live def in HaydnInstrInfo.td. No class is orphaned.
// The live derived def(s) per class:
// Fmt48_WideGPRImm — ADDI32_W, ORI32_W
// Fmt48_WideBR_I20 — JAL_W
// Fmt48_WideBR_RI12 — JALR_W, BEQ/BNE/BGE/BGEU/BLT/BLTU_W (6)
// Fmt48_WideBR_I12 — BEQZ/BNEZ/BGEZ/BLTZ_W (4)
// Fmt48_WideDR_RI6 — SLLI64, SRLI64, SRAI64
// Fmt48_WideSET_HWLOOP — SET_HWLOOP_W
// Fmt48_WideSET_HWLOOP_F2 — SET_HWLOOP_F2_W
// Fmt48_WideSET_HWLOOP_REG— SET_HWLOOP_REG_W
// (Verified: `grep -nE '^def [A-Z0-9_]+ +: Fmt48_' HaydnInstrInfo.td` — 20
// live defs total. CSRW_W is a HaydnInst<0> shell.) 's "retire Fmt48_*" Cycle-B item cannot complete until
// those 20 defs are migrated to s0/s1/s2 format classes owned by
// HaydnFormats*.td no longer carries *_W_S0 projections. Occupancy peels
// ADDI32_W/ORI32_W / far-branch `_W` to compact Format E members.
// The legacy `_W` defs persist as selector-emitted opcodes; leaveRegion
// setDesc commits the generated member. The logical's role is the
// operand shape (rd, rs, imm20) for post-RA setDesc(member).
//
// Fixed Bundle Bit Map (§5.1):
// bits[15:12] = rd/rt (4b destination register — GPR32 or DR64 per Class)
// bits[11:4] = opcode (8b; opcode[7:5]=Class, opcode[4:0]=sub-opcode)
// bits[3:0] = 1101 (pattern)
//
// Class-Dependent high bits (§5.2):
// Class 000 GPR-Imm opcode[7:5]=000 bits[47:28]=imm20 [27:20]=spare
// [19:16]=rs [15:12]=rt
// Class 001 BR opcode[7:5]=001 bits[47:28]=imm20 (JAL I20)
// OR bits[47:36]=off12 [35:32]=rs (RI12)
// OR bits[47:36]=off12 [35:32]=spare (I12)
// Class 010 DR-Imm opcode[7:5]=010 bits[47:28]=imm20 (RI20, opcode[4]=0)
// OR bits[47:42]=imm6 [41:28]=spare (RI6, opcode[4]=1)
// (rd/rt at [15:12] is rtd; rsd tied to rtd)
// Class 011 DRX opcode[7:5]=011 bits[47:16]=imm32 (full 32 bits)
// (rd/rt at [15:12] is rtd; rsd tied to rtd)
// Class 100 CTRL opcode[7:5]=100 per §5.8-5.13 (CSRR/CSRW uimm8 in
// [47:40]; SET_HWLOOP multi-imm forms)
//
// The previous Fmt48_WideImmALU used the obsolete "OLD §5 imm32-in-[43:12]"
// layout with rtd at [47:44]. Slice 1 retires that layout and replaces it
// with the canonical §5.1 fixed-field map (rd/rt at [15:12]). The 12
// defs (ADDI64..MINI64) and the Class 000 GPR WIDE variants (ADDI32_W
// ORI32_W) are re-encoded onto the §5 opcode map. New WIDE-variant defs
// (LUI_W, JAL_W, BEQ_W, CSRR_W, SET_HWLOOP_W,...) carry the §5 opcodes
// 0x0C / 0x20-0x30 / 0x80-0x8A and coexist with the existing 32-bit FmtI
// FmtBR / FmtJ / FmtCSR forms (which stay unchanged for short-offset code).
//===----------------------------------------------------------------------===//

// Helper: emit the fixed low 16 bits of any §5 WIDE instruction (rd/rt +
// opcode + pattern). All Classes share this layout.
class Haydn48WideBase<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnInst48<outs, ins, asmstr, pattern> {
  // Default itinerary + slot bits — subclasses override per Class.
  let Itinerary = Slot012_ALU;
  let TSFlags{2-0} = 0b111;   // slots 0+1+2 default; overridden per Class
}

//===----------------------------------------------------------------------===//
// Class 000 — GPR Immediate (§5.4). GPR32 read-modify-write, 20-bit imm.
// bits[47:28] = imm20 bits[27:20] = spare(0)
// bits[19:16] = rs bits[15:12] = rt bits[11:4] = opcode bits[3:0] = 1101
//===----------------------------------------------------------------------===//

// 48-bit WIDE GPR-Imm format. TableGen size-6 shell; product encode is Format E member.
// `rt = rs OP (imm20)` (sign-extended for ADDI32_W/SUBI32_W/...; zero
// extended for ANDI32_W/ORI32_W/XORI32_W/LUI_W). The constraint
// `$rt = $rs` makes the read-modify-write explicit; the encoder writes the
// same physical register into both rt[15:12] and rs[19:16].
class Fmt48_WideGPRImm<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  // ALU32 ops execute in ANY slot (same itinerary as 32-bit FmtI ADDI32).
  let Itinerary = Slot012_ALU;
  let TSFlags{2-0} = 0b111;   // slots 0+1+2

  bits<4> rt;       // destination GPR32 (bits[15:12])
  bits<4> rs;       // source GPR32 (bits[19:16]); tied to rt
  bits<20> imm;     // 20-bit immediate (bits[47:28])

  let Inst{47 - 28} = imm;
  let Inst{27 - 20} = 0;       // spare (8b)
  let Inst{19 - 16} = rs;
  let Inst{15 - 12} = rt;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;    // §5 pattern
}

//===----------------------------------------------------------------------===//
// Class 001 — Branch (§5.5). PC-relative branches in 2-byte units (§5.14).
// I20 (JAL): bits[47:28]=offset20 [15:12]=rd opcode=0x20
// RI12 (BEQ...):bits[47:36]=offset12 [35:32]=rt [15:12]=rs opcode=0x24-0x29
// I12 (BEQZ..):bits[47:36]=offset12 [35:32]=spare[15:12]=rs opcode=0x2C-0x2F
// RI12 (JALR): bits[47:36]=offset12 [35:32]=rd [15:12]=rs opcode=0x30
//===----------------------------------------------------------------------===//

// 48-bit WIDE Branch I20 format (JAL). TableGen size-6 shell; product encode is Format E member.
// `rt = PC+6; PC += sext(offset20) << 1`. The 20-bit offset is in 2-byte
// units (§5.14, D1). The MC encoder emits a FIXUP_HAYDN_CallSImm20 fixup
// for symbolic targets; the encoded immediate is `(target - PC) >> 1`.
class Fmt48_WideBR_I20<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;
  let TSFlags{2-0} = 0b001;   // slot 0 only (branches)
  // isBranch/isTerminator set per-def (calls vs branches differ) — /.

  bits<4> rt;             // link GPR (bits[15:12])
  bits<20> offset;        // bits[47:28]

  let Inst{47 - 28} = offset;
  let Inst{27 - 16} = 0;  // spare (12b)
  let Inst{15 - 12} = rt;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

// 48-bit WIDE Branch RI12 format (BEQ/BNE/BGE/BGEU/BLT/BLTU/JALR).
// TableGen size-6 shell; product encode is Format E member.
// Two-register form: `if (rs COND rt) PC += sext(off12)<<1`
// (or for JALR `rt=PC+6; PC=rs+sext(off12)`). bits[35:32]=rt, bits[15:12]=rs.
class Fmt48_WideBR_RI12<bits<8> opcode, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;
  let TSFlags{2-0} = 0b001;
  // isBranch/isTerminator set per-def (calls vs branches differ) — /.

  bits<4> rs;             // GPR (bits[15:12])
  bits<4> rt;             // GPR (bits[35:32])
  bits<12> offset;        // bits[47:36]

  let Inst{47 - 36} = offset;
  let Inst{35 - 32} = rt;
  let Inst{31 - 16} = 0;  // spare (16b)
  let Inst{15 - 12} = rs;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

// 48-bit WIDE Branch I12 format (BEQZ/BNEZ/BGEZ/BLTZ).
// TableGen size-6 shell; product encode is Format E member. Single-register form:
// `if (rs COND 0) PC += sext(off12)<<1`. bits[15:12]=rs; bits[35:32]=spare.
class Fmt48_WideBR_I12<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;
  let TSFlags{2-0} = 0b001;
  // isBranch/isTerminator set per-def (calls vs branches differ) — /.

  bits<4> rs;             // GPR (bits[15:12])
  bits<12> offset;        // bits[47:36]

  let Inst{47 - 36} = offset;
  let Inst{35 - 16} = 0;  // spare (20b)
  let Inst{15 - 12} = rs;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

//===----------------------------------------------------------------------===//
// Class 010 — DR Immediate (§5.6). DR64 read-modify-write.
// RI20 (ADDI64..XORI64/SLTI64/SLTUI64, opcode[4]=0):
// bits[47:28]=imm20 [27:16]=spare(0) [15:12]=rtd(opcode) opcode=0x40-0x49
// RI6 (SLLI64/SRLI64/SRAI64, opcode[4]=1):
// bits[47:42]=imm6 [41:16]=spare(0) [15:12]=rtd(opcode) opcode=0x50-0x52
//
// Fmt48_WideDR_RI20 RETIRED — its 7 defs (ADDI64/SUBI64
// ANDI64/ORI64/XORI64/SLTI64/SLTUI64) had zero live C++ consumers subsequent.
// The RI6 shift format (Fmt48_WideDR_RI6) below is KEPT — SLLI64/SRLI64
// SRAI64 remain live logical opcodes.
//===----------------------------------------------------------------------===//

// 48-bit WIDE DR-Imm RI6 format (shifts). TableGen size-6 shell; product encode is Format E member.
// `rtd = rsd <shift> imm6` (rsd tied to rtd).
class Fmt48_WideDR_RI6<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  // 2026-08-21 itinerary re-map: golden SLLI64/SRLI64/SRAI64 Available =
  // ALU0|ALU1|ALU2 (was Slot12_ALU).
  let Itinerary = Slot012_ALU;
  let TSFlags{2-0} = 0b110;

  bits<4> rtd;            // DR64 destination (bits[15:12]); rsd tied
  bits<6> imm;            // bits[47:42]

  let Inst{47 - 42} = imm;
  let Inst{41 - 16} = 0;  // spare (26b)
  let Inst{15 - 12} = rtd;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

//===----------------------------------------------------------------------===//
// Class 011 — DR Extended (§5.7). DR64 min/max with full 32-bit immediate.
// bits[47:16]=imm32 (all 32 bits, SE) [15:12]=rtd(opcode) opcode=0x60/0x61
//
// Fmt48_WideDRX RETIRED — its 2 defs (MAXI64/MINI64) had
// zero live C++ consumers subsequent.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Class 100 — Control (§5.8-5.13). CSR + ZERO_GPR + SET_HWLOOP forms.
//===----------------------------------------------------------------------===//

// Fmt48_WideCSR RETIRED — CSRW_W is a HaydnInst<0> logical shell
// (HaydnInstrInfo.td). Product encode is Format E CSRW I8 members after
// MemberId cutover. Reloc uses typed CSR I8 (FIXUP_HAYDN_CSR_UImm8 /
// R_HAYDN_CSR_UImm8). Peer: AIE MultiSlot_Pseudo has no Inst bits
// (AIE2InstrFormats.td:23-31); catalog CSRW is the same HaydnInst<0> shape.

// Fmt48_WideZeroGPR RETIRED — its sole def (ZERO_GPR_W)
// had zero live C++ consumers subsequent.

// 48-bit WIDE SET_HWLOOP format (§5.11, opcode 0x88). All-immediate form.
// bits[47:32]=uimm16_cnt bits[31:26]=uimm6_offset1 bits[25:14]=uimm12_offset2
// bits[13]=hwlr_sel bits[12]=spare(0) bits[11:4]=0x88 bits[3:0]=1101
class Fmt48_WideSET_HWLOOP<bits<8> opcode, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;
  let TSFlags{2-0} = 0b001;
  let hasSideEffects = 1; let isNotDuplicable = 1;

  bits<16> cnt;           // uimm16_cnt (bits[47:32])
  bits<6> offset1;        // uimm6 begin offset (bits[31:26]) — 4-byte units
  bits<12> offset2;       // uimm12 end offset (bits[25:14]) — 4-byte units
  bits<1> sel;            // hwlr_sel (bit[13])

  let Inst{47 - 32} = cnt;
  let Inst{31 - 26} = offset1;
  let Inst{25 - 14} = offset2;
  let Inst{13} = sel;
  let Inst{12} = 0;       // spare
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

// 48-bit WIDE SET_HWLOOP_F2 format (§5.12, opcode 0x89). Mixed form.
// bits[47:36]=spare(0) bits[35:32]=rs(count) bits[31:26]=uimm6_offset1
// bits[25:14]=uimm12_offset2 bits[13]=hwlr_sel bits[12]=spare(0)
// bits[11:4]=0x89 bits[3:0]=1101
class Fmt48_WideSET_HWLOOP_F2<bits<8> opcode, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;
  let TSFlags{2-0} = 0b001;
  let hasSideEffects = 1; let isNotDuplicable = 1;

  bits<4> rs;             // GPR count source (bits[35:32])
  bits<6> offset1;        // bits[31:26]
  bits<12> offset2;       // bits[25:14]
  bits<1> sel;            // bit[13]

  let Inst{47 - 36} = 0;  // spare (12b)
  let Inst{35 - 32} = rs;
  let Inst{31 - 26} = offset1;
  let Inst{25 - 14} = offset2;
  let Inst{13} = sel;
  let Inst{12} = 0;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

// 48-bit WIDE SET_HWLOOP_REG format (§5.13, opcode 0x8A). All-register form.
// bits[47:44]=rs1(begin) bits[43:40]=rs2(end) bits[39:36]=rs3(count)
// bits[35:14]=spare(0) bits[13]=hwlr_sel bits[12]=spare(0)
// bits[11:4]=0x8A bits[3:0]=1101
class Fmt48_WideSET_HWLOOP_REG<bits<8> opcode, dag outs, dag ins, string asmstr,
                               list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = Slot0_ALU;
  let TSFlags{2-0} = 0b001;
  let hasSideEffects = 1; let isNotDuplicable = 1;

  bits<4> rs1;            // GPR begin (bits[47:44])
  bits<4> rs2;            // GPR end (bits[43:40])
  bits<4> rs3;            // GPR count (bits[39:36])
  bits<1> sel;            // bit[13]

  let Inst{47 - 44} = rs1;
  let Inst{43 - 40} = rs2;
  let Inst{39 - 36} = rs3;
  let Inst{35 - 14} = 0;  // spare (22b)
  let Inst{13} = sel;
  let Inst{12} = 0;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

//===----------------------------------------------------------------------===//
// Class 101 — WIDE LS (§5.16). RETIRED by.
//
// Fmt48_WideLS and its 14 _WL defs had zero live C++ consumers subsequent
// (the 48-bit encoder/decoder paths were gutted). Wide-offset LS is now
// reached via the _S0 family-2 defs (HaydnFormatsLS.td).
// The §5.16 bit-layout documentation above is retained for historical
// reference.
//===----------------------------------------------------------------------===//
