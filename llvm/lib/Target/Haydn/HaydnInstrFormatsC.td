//===-- HaydnInstrFormatsC.td - Compressed Formats ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Compressed 16-bit and extended 48-bit instruction formats.
// Per §2.7 and §2.9 of the plan.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Compressed 16-bit and extended 48-bit instruction formats.
// Per §2.7 and §2.9 of the plan.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Compressed 16-bit instruction formats — encoding_manual.md §2 (Slice 4
// spec migration).
//
// §2.1 Bundle Bit Map:
// bits[15:4] = opcode / operands (12b)
// bits[3:0] = pattern (4b). bits[3:1] select one of 8 patterns; bit[0]=0
// is the primary 16-bit-width discriminator (§1 decode tree).
//
// §2.2 Pattern Table:
// 0000 NOP all-zero 16-bit window (canonical bundle NOP, §2.4/§10.5).
// 0010 HINT hint(8) | 0000(4 reserved).
// 0100 ALU32 R opc(6) | rs(3) | rt(3). 64 ops, GPR32Lo (r0-r7).
// 0110 MOVI opc(2) | rd(3) | imm7(7). MOVI rd, imm7 (-64..+63).
// 1000 BRANCH opc(2) | imm10(10). PC-rel branch (+-512 words).
// 1010 ALU32 RI opc(4) | rs(3) | rt(3) | imm2(2). Tiny imm 0-3.
// 1100 ALU32 SI opc(4) | rd(3) | imm5(5). Destructive rd=rd OP imm.
// 1110 Reserved (future extension).
//
// §2.3 Key Design Changes vs the legacy layout (bits[15:14]=00 + 6b opcode):
// ZERO/CLR eliminated; ZERO_GPR = MOVI rd, 0.
// ALU32 R opcode expanded 4b -> 6b (16 -> 64 ops).
// HINT expanded 4b -> 8b.
// MOVI immediate expanded imm5 -> imm7 (+-16 -> +-64).
// BRANCH offset expanded imm8 -> imm10 (+-128 -> +-512 words).
//
// The legacy FmtC_ALU/FmtC_LS/FmtC_ALUImm4/FmtC_II classes (6b opcode at
// [13:8] with rd at [7:4]) are RETIRED by this migration. Concrete C_*
// instruction defs in HaydnInstrInfoC.td are re-encoded onto the §2.2
// pattern classes below.
//===----------------------------------------------------------------------===//

class HaydnInst16<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnInst<2, outs, ins, asmstr, pattern> {
  let TSFlags{4-3} = 0b00;
  let DecoderNamespace = "Haydn16";
}

//===----------------------------------------------------------------------===//
// FmtC_* 16-bit compressed format classes RETIRED (Bundle128-only).
// HaydnInst16 kept — NOP still uses the 16-bit all-zero encoding.
// C_* instruction shells deleted from HaydnInstrInfoC.td.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 48-bit extended format
// 32-bit R/I-type base + 16-bit extension payload
// Consumes one owner issue slot (the extension is NOT an issue slot)
//===----------------------------------------------------------------------===//

class HaydnInst48<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnInst<6, outs, ins, asmstr, pattern> {
  let TSFlags{4-3} = 0b10;
  let DecoderNamespace = "Haydn48";
}

// NOTE: The legacy `FmtExtI` class (base 32-bit parcel + 16-bit extension
// parcel, width tag 0b10) was removed — it contradicted encoding_manual.md
// §4/§5, which define the two 48-bit formats (MOVEI, Wide-imm ALU) as flat
// 6-byte parcels, not a base+extension split. No `def` inherited FmtExtI
// (grep across lib/Target/Haydn/ returned only the class definition).
// The spec-aligned 48-bit formats are Fmt48_MOVEI (§4) and Fmt48_WideImmALU
// (§5) below. See -fmt-exti-disposition-and-widemm-isPseudo.md.

//===----------------------------------------------------------------------===//
// 48-bit S-format (MOVEI) — encoding_manual.md §4.
//
// Retired once for having zero live C++ consumers, and restored because the
// logicals it carries are needed again: GISel used to select the format member
// `MOVEI_H_S0` directly, which pins every movei to one placement and does not
// survive a change of member spelling. Selecting the logical and letting the
// placement auction choose a member requires the logical to exist and to carry
// a real encoding — a bare HaydnInst with no `Inst` bits is inferred
// MCID::Pseudo and AsmPrinter drops it, which is the same defect X4ABS16 was
// promoted out of (HaydnInstrInfo.td).
//
// The encoding here is vestigial in the same sense as every other multi-member
// logical: materializeMultiOpcodeInstrs commits the instruction to a member
// before it is ever encoded, so these bits exist to keep the def out of
// MCID::Pseudo, not to be emitted.
//
// The class itself is defined below, after Haydn48WideBase which it derives
// from.
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// 48-bit Wide-imm ALU — encoding_manual.md §5 (Slice 1 spec migration
// RESOLVES.
//
// AUDIT : all 9 Fmt48_Wide* classes below are KEPT — each is
// the base class of >=1 live def in HaydnInstrInfo.td. No class is orphaned.
// The live derived def(s) per class:
// Fmt48_WideGPRImm — ADDI32_W, ORI32_W
// Fmt48_WideBR_I20 — JAL_W
// Fmt48_WideBR_RI12 — JALR_W, BEQ/BNE/BGE/BGEU/BLT/BLTU_W (6)
// Fmt48_WideBR_I12 — BEQZ/BNEZ/BGEZ/BLTZ_W (4)
// Fmt48_WideDR_RI6 — SLLI64, SRLI64, SRAI64
// Fmt48_WideCSR — CSRW_W
// Fmt48_WideSET_HWLOOP — SET_HWLOOP
// Fmt48_WideSET_HWLOOP_F2 — SET_HWLOOP_F2
// Fmt48_WideSET_HWLOOP_REG— SET_HWLOOP_REG
// (Verified: `grep -nE '^def [A-Z0-9_]+ +: Fmt48_' HaydnInstrInfo.td` — 21
// live defs total.) 's "retire Fmt48_*" Cycle-B item cannot complete until
// those 21 defs are migrated to Bundle128 s0/s1/s2 format classes owned by
// HaydnFormats*.td (the / _W_S0 defs are the s0 projections for
// ADDI32_W/ORI32_W + far branches + CSR; s1/s2 projections for SLLI64 etc.
// are forward work). The legacy defs persist as selector-emitted opcodes that
// leaveRegion setDesc commits to their _S0 member twins (alts-derived).
// ADDI32_W/ORI32_W are NOT re-based onto HaydnFU_ALU32_S0_RI20 here
// doing so would duplicate the isCodeGenOnly=1 DISTINCT-codepoint
// ADDI32_W_S0/ORI32_W_S0 defs (HaydnFormatsALU32.td:1185-1196) and
// break the logical→member alt pairing; the legacy def's role is to carry
// the operand shape (rd, rs, imm20) for post-RA setDesc(member).
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
// (LUI_W, JAL_W, BEQ_W, CSRR_W, SET_HWLOOP,...) carry the §5 opcodes
// 0x0C / 0x20-0x30 / 0x80-0x8A and coexist with the existing 32-bit FmtI
// FmtBR / FmtJ / FmtCSR forms (which stay unchanged for short-offset code).
//===----------------------------------------------------------------------===//

// Helper: emit the fixed low 16 bits of any §5 WIDE instruction (rd/rt +
// opcode + pattern). All Classes share this layout.
class Haydn48WideBase<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnInst48<outs, ins, asmstr, pattern> {
  // Default itinerary + slot bits — subclasses override per Class.
  let Itinerary = NoItinerary;  // per-instruction; see the def
  let TSFlags{2-0} = 0b111;   // slots 0+1+2 default; overridden per Class
}

// 48-bit S-format (MOVEI) — encoding_manual.md §4; see the §4 block above for
// why this class exists again.
// bits[47:16] = imm32  bits[15:12] = rd  bits[11:4] = opcode  bits[3:0] = 1101
class Fmt48_MOVEI<bits<8> opcode, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  bits<4> rd;        // destination DR64 (bits[15:12])
  bits<32> imm32;    // full 32-bit immediate (bits[47:16])

  let Inst{47 - 16} = imm32;
  let Inst{15 - 12} = rd;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

//===----------------------------------------------------------------------===//
// Class 000 — GPR Immediate (§5.4). GPR32 read-modify-write, 20-bit imm.
// bits[47:28] = imm20 bits[27:20] = spare(0)
// bits[19:16] = rs bits[15:12] = rt bits[11:4] = opcode bits[3:0] = 1101
//===----------------------------------------------------------------------===//

// 48-bit WIDE GPR-Imm format. encoding_manual.md §5.4, Class 000.
// `rt = rs OP (imm20)` (sign-extended for ADDI32_W/SUBI32_W/...; zero
// extended for ANDI32_W/ORI32_W/XORI32_W/LUI_W). The constraint
// `$rt = $rs` makes the read-modify-write explicit; the encoder writes the
// same physical register into both rt[15:12] and rs[19:16].
class Fmt48_WideGPRImm<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  // ALU32 ops execute in ANY slot (same itinerary as 32-bit FmtI ADDI32).
  let Itinerary = NoItinerary;  // per-instruction; see the def
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

// 48-bit WIDE Branch I20 format (JAL). encoding_manual.md §5.5.
// `rt = PC+6; PC += sext(offset20) << 1`. The 20-bit offset is in 2-byte
// units (§5.14, D1). The MC encoder emits a FIXUP_HAYDN_CallSImm20 fixup
// for symbolic targets; the encoded immediate is `(target - PC) >> 1`.
class Fmt48_WideBR_I20<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = NoItinerary;  // per-instruction; see the def
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
// encoding_manual.md §5.5. Two-register form: `if (rs COND rt) PC += sext(off12)<<1`
// (or for JALR `rt=PC+6; PC=rs+sext(off12)`). bits[35:32]=rt, bits[15:12]=rs.
class Fmt48_WideBR_RI12<bits<8> opcode, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = NoItinerary;  // per-instruction; see the def
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
// encoding_manual.md §5.5. Single-register form:
// `if (rs COND 0) PC += sext(off12)<<1`. bits[15:12]=rs; bits[35:32]=spare.
class Fmt48_WideBR_I12<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = NoItinerary;  // per-instruction; see the def
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
// SRAI64 are consumed by HaydnDClassOpcodes.cpp.
//===----------------------------------------------------------------------===//

// 48-bit WIDE DR-Imm RI6 format (shifts). encoding_manual.md §5.6.
// `rtd = rsd <shift> imm6` (rsd tied to rtd).
class Fmt48_WideDR_RI6<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = NoItinerary;  // per-instruction; see the def
  let TSFlags{2-0} = 0b110;

  // The source register has no home in this 48-bit form, which is the only
  // reason SLLI64/SRLI64/SRAI64 are declared `$rtd = $rsd`. The hardware does
  // not tie them: the database gives them independent read and write ports and
  // the format E members encode both. The tie cannot come off while they are
  // derived from this class — an untied operand needs a field to encode into —
  // so it goes when the Bundle128 formats do (§ 5.2), and until then it costs
  // a register copy. FORMAT-E-SWITCH-PLAN.md 5.11.
  bits<4> rtd;            // DR64 destination (bits[15:12]); rsd tied to it
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

// 48-bit WIDE CSR format (CSRR/CSRW). encoding_manual.md §5.9, §5.10.
// CSRR: `rt = CSR[uimm8]`. CSRW: `CSR[uimm8] = rs`. bits[47:40]=uimm8
// bits[39:16]=spare(0), bits[15:12]=rt/rs, opcode=0x80/0x81.
class Fmt48_WideCSR<bits<8> opcode, dag outs, dag ins, string asmstr,
                    list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = NoItinerary;  // per-instruction; see the def
  let TSFlags{2-0} = 0b001;
  let hasSideEffects = 1;   // CSR accesses are side-effecting

  bits<4> rt;             // GPR (bits[15:12]); CSRR=dest, CSRW=src
  bits<8> uimm8;          // CSR address (bits[47:40])

  let Inst{47 - 40} = uimm8;
  let Inst{39 - 16} = 0;  // spare (24b)
  let Inst{15 - 12} = rt;
  let Inst{11 - 4} = opcode;
  let Inst{3 - 0} = 0b1101;
}

// Fmt48_WideZeroGPR RETIRED — its sole def (ZERO_GPR_W)
// had zero live C++ consumers subsequent.

// 48-bit WIDE SET_HWLOOP format (§5.11, opcode 0x88). All-immediate form.
// bits[47:32]=uimm16_cnt bits[31:26]=uimm6_offset1 bits[25:14]=uimm12_offset2
// bits[13]=hwlr_sel bits[12]=spare(0) bits[11:4]=0x88 bits[3:0]=1101
class Fmt48_WideSET_HWLOOP<bits<8> opcode, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = NoItinerary;  // per-instruction; see the def
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
  let Itinerary = NoItinerary;  // per-instruction; see the def
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

// 48-bit WIDE SET_HWLOOP_F2 format (§5.13, opcode 0x8A). All-register form.
// bits[47:44]=rs1(begin) bits[43:40]=rs2(end) bits[39:36]=rs3(count)
// bits[35:14]=spare(0) bits[13]=hwlr_sel bits[12]=spare(0)
// bits[11:4]=0x8A bits[3:0]=1101
class Fmt48_WideSET_HWLOOP_REG<bits<8> opcode, dag outs, dag ins, string asmstr,
                               list<dag> pattern>
    : Haydn48WideBase<outs, ins, asmstr, pattern> {
  let Itinerary = NoItinerary;  // per-instruction; see the def
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
// reached via the Bundle128 _S0 family-2 defs (HaydnFormatsLS.td).
// The §5.16 bit-layout documentation above is retained for historical
// reference.
//===----------------------------------------------------------------------===//
