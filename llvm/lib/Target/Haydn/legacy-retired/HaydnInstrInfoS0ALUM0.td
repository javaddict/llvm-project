// ===-- HaydnInstrInfoS0ALUM0.td - s0 ALU32 decode variants -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Decode-included s0 ALU32 variants (Mode 0, — item 3).
//
// One <OP>_M0S0ALU def per s0 ALU32 opcode routed through getEncOpcodeMap's
// FU_ALU32 table (HaydnDClassOpcodes.cpp:72-127). These are DECODE targets:
// when the Mode-0 disassembler routes a bundle's s0 ALU window, the tablegen
// decoder trie over DecoderNamespace "HaydnM0S0ALU" resolves it to one of
// these defs, and the asm printer emits the bare generic mnemonic.
//
// Source of truth: encoding_manual.md §6.5 (s0 ALU layout, 20-bit window):
// bit[23] = FU (0 = ALU)
// bits[22:17]= opcode(6) — FLAT 6-bit ALU32 opcode (0x00-0x3F, MULL=0x3F)
// bits[16:13]= rs (GPR32 source 1, or rs1 for MOVT/MOVF/MULL)
// bits[12:9] = rt (GPR32 dest, or rd for MOVT/MOVF/MULL)
// bits[8:4] = ext(5) — class-partitioned by opcode:
// RR-form (ADD32/SUB32/...): ext[3:0]=rs2, ext[4]=0 fixed
// RI-form (SLLI32/ADDI32/...): ext[4:0]=imm5 (uimm5/simm5)
// MOVT32/MOVF32 (ISA-63): ext[3:0]=rs2 (predicate)
// ext[4]=0 reserved
// MULL family (0x3F, E6): ext[4:2]=sub-variant
// ext[1:0]=0 fixed
//
// IMPLEMENTATION NOTES
//
// * ext[4] for RR / MOVT / MOVF is fixed to 0 — the tablegen trie matches it
// as a constant, so a spurious ext[4]=1 word fails the table and falls
// through (preserves the hand-rolled `Rs2 >= 16` reject for RR and the
// `Ext >> 4` reject for MOVT/MOVF).
//
// * The MULL family shares opcode 0x3F across 4 variants; the sub-variant
// is the static funct template param (ext[4:2]=0/1/2/3), and ext[1:0]=0
// is fixed. The trie matches both the opcode 0x3F AND the static funct
// bits — the same trick FmtALU32Ext uses to disambiguate the 4 variants
// in the legacy 32-bit decode table. The destructive tie "$rd = $rs2" is
// reconstructed by tablegen (it emits the tied source from the dest
// reg field automatically).
//
// * MOVT32/MOVF32 use a 4-operand model with a tied dest ($rd = $rd_src):
// (outs GPR32:$rd), (ins GPR32:$rd_src, GPR32:$rs1, GPR32:$rs2).
// The s0 layout's rt field carries rd, rs field carries rs1, ext[3:0]
// carries rs2. The tied $rd_src is reconstructed by tablegen from the
// dest field (no hand-rolled copy(op0) needed).
//
// isCodeGenOnly is 0 (default — do NOT set isCodeGenOnly=1, or the decoder
// table won't generate); Pattern=; bare mnemonic; DecoderNamespace=
// "HaydnM0S0ALU" isolates these from the DEFAULT asm-match + Haydn64 decode
// namespaces so they DECODE only (asm parse of "add32 r,r,r" still matches
// the generic FmtALU32 ADD32). Mirrors the proven s0 LS / s1 LD patterns
// (HaydnInstrInfoS0LSM0.td, HaydnInstrInfoS1LDM0.td) + MAC
// pattern (HaydnInstrInfoMACM0.td).
//
// This file is included by Haydn.td (so -gen-disassembler / -gen-asm-writer
// gen-instr-info / -gen-emitter see the variants) but NOT by HaydnAsmMatcher.td
// (so -gen-asm-matcher does NOT — the build-system split).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Format classes — partition the s0 ALU ext field (bits[8:4]) per §6.5.
//
// s0 slot window (20 bits @ bits[23:4]):
// bit[23] = FU (0 = ALU)
// bits[22:17]= opcode(6)
// bits[16:13]= rs (GPR32)
// bits[12:9] = rt (GPR32)
// bits[8:4] = ext (5b: class-partitioned)
//
// The four classes differ in how the ext field is split into fixed constant
// bits (matched by the tablegen trie to disambiguate the MULL 0x3F sub
// variants and the RR/MOVT reserved ext[4]=0) and a variable field bound
// to the `$rs2` / `$imm5` operand.
//
// CRITICAL: the bits<N> field name MUST match the ins `$`-operand name (e.g.
// `rs2`, `imm5`). A mismatch caused "No operand named X" in the prior
// attempt — the tablegen decoder emitter binds the named operand to the
// named field by name, not position. (See HaydnInstrInfoS1LDM0.td header.)
//===----------------------------------------------------------------------===//

// FmtM0_S0_ALU_RR_D — RR-form ALU32 (29 ops). ext[3:0]=rs2 (GPR32, 4 bits)
// ext[4]=0 fixed (reserved — must be clear; trie rejects spurious set bit).
// Used by ADD32/SUB32/AND32/OR32/XOR32/SLL32/SRL32/SRA32/SLT32/SLTU32
// SEQ32/MAX32/MIN32/MAXU32/MINU32/MOVE32/NOT32/NEG32/NEG32S/ABS32/ABS32S
// BREV32/NSA32/NSAU32/POPCOUNT32/ADD32S/SUB32S/SLE32/SRA32R.
// The `_D` (decode) suffix distinguishes this from the existing encoder-side
// `FmtM0_S0_ALU_RR` (HaydnInstrFormatsD.td:101) which has ext=0 fixed and
// is used by the isCodeGenOnly=1 _M0 encoder variants. This decode-only
// class carries the rs2 field at ext[3:0] so the tablegen trie can decode
// RR-form ops directly from the s0 window.
// Operand order matches the generic FmtALU32/FmtALU32Unary defs:
// (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2)
// The unary ops (NOT32/NEG32/...) carry the same 3-operand MC model
// their generic FmtALU32Unary def's rs2 field is wired to the encoded 0
// (the decoder reconstructs rs2 from ext, which is 0 for unary-encoded
// ops, so the rendered MCInst matches the generic 3-op form). For decode
// ext[3:0] is read directly: unary-encoded ops yield rs2=R0, RR-encoded
// ops yield rs2=source.
class FmtM0_S0_ALU_RR_D<bits<6> opcode, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0ALU";
  bits<4> rs;
  bits<4> rt;
  bits<4> rs2;

  let Inst{63-24} = 0;    // s2 + s1 placeholder
  let Inst{23}    = 0;    // FU = 0 (ALU)
  let Inst{22-17} = opcode;
  let Inst{16-13} = rs;
  let Inst{12-9}  = rt;
  let Inst{8}     = 0;    // ext[4] reserved (must be 0)
  let Inst{7-4}   = rs2;  // ext[3:0] = rs2 (GPR32)
  let Inst{3-0}   = 0b0011;
}

// FmtM0_S0_ALU_RI_D — RI-form ALU32 (12 ops). ext[4:0]=imm5 (uimm5 shift or
// raw 5-bit imm fragment). Used by SLLI32/SRLI32/SRAI32/SRAI32R/ADDI32
// ADDI32S/SUBI32/SUBI32S/ANDI32/ORI32/XORI32/LUI.
// Operand order matches the generic FmtI (RI) defs:
// (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5)
// The s0 ext field carries a 5-bit immediate fragment (the wide-imm form
// for these ops is the 48-bit WIDE parcel, NOT s0 — see HaydnMCCodeEmitter
// Stage-1). Decode zero-extends the 5 bits via the uimm5 operand class.
class FmtM0_S0_ALU_RI_D<bits<6> opcode, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0ALU";
  bits<4> rs;
  bits<4> rt;
  bits<5> imm5;

  let Inst{63-24} = 0;    // s2 + s1 placeholder
  let Inst{23}    = 0;    // FU = 0 (ALU)
  let Inst{22-17} = opcode;
  let Inst{16-13} = rs;
  let Inst{12-9}  = rt;
  let Inst{8-4}   = imm5; // ext[4:0] = imm5
  let Inst{3-0}   = 0b0011;
}

// FmtM0_S0_ALU_PRED_D — MOVT32/MOVF32 predicate form (2 ops, ISA-63).
// ext[3:0]=rs2 (predicate GPR32), ext[4]=0 reserved. The rt field carries
// rd (write-gated dest), rs carries rs1, ext[3:0] carries rs2.
// Operand order matches the generic FmtALU32 MOVT32/MOVF32 defs:
// (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs, GPR32:$rs2)
// let Constraints = "$rt = $rt_src";
// Tablegen's decodeInstruction auto-reconstructs the tied $rt_src from the
// rt dest field (the tied constraint is enforced at the regalloc/MIR level;
// the MC layer carries all 4 operands).
class FmtM0_S0_ALU_PRED_D<bits<6> opcode, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0ALU";
  bits<4> rs;
  bits<4> rt;
  bits<4> rs2;

  let Inst{63-24} = 0;    // s2 + s1 placeholder
  let Inst{23}    = 0;    // FU = 0 (ALU)
  let Inst{22-17} = opcode;
  let Inst{16-13} = rs;   // rs1
  let Inst{12-9}  = rt;   // rd (write-gated dest)
  let Inst{8}     = 0;    // ext[4] reserved (must be 0)
  let Inst{7-4}   = rs2;  // ext[3:0] = rs2 (predicate)
  let Inst{3-0}   = 0b0011;
}

// FmtM0_S0_ALU_MULL_D — MULL family (4 ops, 0x3F sub-variant).
// opcode=0x3F (extended-ALU32 marker), ext[4:2]=funct (sub-variant 0/1/2/3)
// ext[1:0]=0 fixed. The rs field carries rs1, rt carries rd.
// Operand order matches the generic FmtALU32Ext MULL/MULSSH/MULSUH/MULUUH:
// (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2)
// let Constraints = "$rt = $rs2";
// The destructive wire form encodes only rs + rt; operand 2 (rs2) is tied
// to $rt (dest). Tablegen's decodeInstruction auto-reconstructs the tied
// $rs2 from the rt dest field (no hand-rolled copy(op0) needed).
class FmtM0_S0_ALU_MULL_D<bits<3> funct, dag outs, dag ins, string asmstr,
                          list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0ALU";
  bits<4> rs;
  bits<4> rt;

  let Inst{63-24} = 0;    // s2 + s1 placeholder
  let Inst{23}    = 0;    // FU = 0 (ALU)
  let Inst{22-17} = 0x3F; // opcode = 0x3F (extended-ALU32 marker)
  let Inst{16-13} = rs;   // rs1
  let Inst{12-9}  = rt;   // rd (dest, tied to rs2)
  let Inst{8-6}   = funct;// ext[4:2] = sub-variant (static)
  let Inst{5-4}   = 0;    // ext[1:0] = 0 (fixed per §6.5)
  let Inst{3-0}   = 0b0011;
}

// FmtM0_S0_ALU_DREXT — DR→GPR extended-op escape (Phase 1d, spec-blessed).
// MOVE32_DR_L/H cross-bank DR64→GPR32 lane extract via the opcode-0x3F escape.
// DISJOINT from the MULL family by ext[4]:
// MULL ext[4]=0, ext[4:2]=funct(0-3), ext[1:0]=0 -> ext 0x00-0x0C
// DREXT ext[4]=1, ext[3:0]=sub_opcode -> ext 0x10-0x1F
// The rs field (bits[16:13]) carries the DR64 source index; rt (bits[12:9])
// the GPR32 dest. The decoder operand TYPE (DR64 vs GPR32) drives the reg
// class — the 4-bit field is reinterpreted per the.td operand type. The new
// decoder gates on opcode[5:0]=0x3F first, so it never reads ext as a MULL
// sub-variant. (spec: encoding_manual.md §1.2/§3 + the 0x3F escape.)
class FmtM0_S0_ALU_DREXT<bits<5> sub_opcode, dag outs, dag ins, string asmstr,
                         list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0ALU";
  bits<4> rsd;   // DR64 source (bits[16:13] — the rs field reinterpreted)
  bits<4> rt;    // GPR32 dest (bits[12:9])
  let Inst{63-24} = 0;          // s2 + s1 placeholder
  let Inst{23}    = 0;          // FU = 0 (ALU)
  let Inst{22-17} = 0x3F;       // opcode = extended-ALU32 escape
  let Inst{16-13} = rsd;        // DR64 source
  let Inst{12-9}  = rt;         // GPR32 dest
  let Inst{8-4}   = sub_opcode; // ext[4:0]; bit4=1 (0x10+) -> disjoint from MULL
  let Inst{3-0}   = 0b0011;
}

//===----------------------------------------------------------------------===//
// Decode variants — bare mnemonic, isCodeGenOnly=0, Pattern=.
//
// Operand order and mnemonics match the generic defs so the asm printer emits
// identical output to the hand-rolled path. mayLoad/mayStore stay default 0
// (ALU only).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// RR-form (29 ops). Opcodes from getEncOpcodeMap FU_ALU32 (HaydnDClassOpcodes
// L72-127). ADD32 uses 0x3E (NOP-remapped per §9.4).
//===----------------------------------------------------------------------===//
def ADD32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x3E,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "add32\t$rt, $rs, $rs2", []>;
def SUB32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x01,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "sub32\t$rt, $rs, $rs2", []>;
def AND32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x02,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "and32\t$rt, $rs, $rs2", []>;
def OR32_M0S0ALU   : FmtM0_S0_ALU_RR_D<0x03,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "or32\t$rt, $rs, $rs2", []>;
def XOR32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x04,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "xor32\t$rt, $rs, $rs2", []>;
def SLL32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x05,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "sll32\t$rt, $rs, $rs2", []>;
def SRL32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x06,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "srl32\t$rt, $rs, $rs2", []>;
def SRA32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x07,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "sra32\t$rt, $rs, $rs2", []>;
def SLT32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x08,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "slt32\t$rt, $rs, $rs2", []>;
def SLTU32_M0S0ALU : FmtM0_S0_ALU_RR_D<0x09,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "sltu32\t$rt, $rs, $rs2", []>;
def SEQ32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x0A,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "seq32\t$rt, $rs, $rs2", []>;
def MAX32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x0B,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "max32\t$rt, $rs, $rs2", []>;
def MIN32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x0C,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "min32\t$rt, $rs, $rs2", []>;
def MAXU32_M0S0ALU : FmtM0_S0_ALU_RR_D<0x0D,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "maxu32\t$rt, $rs, $rs2", []>;
def MINU32_M0S0ALU : FmtM0_S0_ALU_RR_D<0x0E,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "minu32\t$rt, $rs, $rs2", []>;
def MOVE32_M0S0ALU : FmtM0_S0_ALU_RR_D<0x10,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "move32\t$rt, $rs", []>;
def NOT32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x11,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "not32\t$rt, $rs", []>;
def NEG32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x12,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "neg32\t$rt, $rs", []>;
def NEG32S_M0S0ALU : FmtM0_S0_ALU_RR_D<0x13,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "neg32s\t$rt, $rs", []>;
def ABS32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x14,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "abs32\t$rt, $rs", []>;
def ABS32S_M0S0ALU : FmtM0_S0_ALU_RR_D<0x15,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "abs32s\t$rt, $rs", []>;
def BREV32_M0S0ALU : FmtM0_S0_ALU_RR_D<0x16,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "brev32\t$rt, $rs, $rs2", []>;
def NSA32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x17,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "nsa32\t$rt, $rs", []>;
def NSAU32_M0S0ALU : FmtM0_S0_ALU_RR_D<0x18,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "nsau32\t$rt, $rs", []>;
def POPCOUNT32_M0S0ALU : FmtM0_S0_ALU_RR_D<0x19,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "popcount32\t$rt, $rs", []>;
def ADD32S_M0S0ALU : FmtM0_S0_ALU_RR_D<0x1B,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "add32s\t$rt, $rs, $rs2", []>;
def SUB32S_M0S0ALU : FmtM0_S0_ALU_RR_D<0x1C,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "sub32s\t$rt, $rs, $rs2", []>;
def SLE32_M0S0ALU  : FmtM0_S0_ALU_RR_D<0x1D,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "sle32\t$rt, $rs, $rs2", []>;
def SRA32R_M0S0ALU : FmtM0_S0_ALU_RR_D<0x1F,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "sra32r\t$rt, $rs, $rs2", []>;

//===----------------------------------------------------------------------===//
// RI-form (12 ops). imm5 zero-extends via uimm5 (matches the hand-rolled
// path's raw Ext storage + the generic FmtI imm rendering).
//===----------------------------------------------------------------------===//
def SLLI32_M0S0ALU  : FmtM0_S0_ALU_RI_D<0x20,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "slli32\t$rt, $rs, $imm5", []>;
def SRLI32_M0S0ALU  : FmtM0_S0_ALU_RI_D<0x21,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srli32\t$rt, $rs, $imm5", []>;
def SRAI32_M0S0ALU  : FmtM0_S0_ALU_RI_D<0x22,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srai32\t$rt, $rs, $imm5", []>;
def SRAI32R_M0S0ALU : FmtM0_S0_ALU_RI_D<0x23,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "srai32r\t$rt, $rs, $imm5", []>;
def ADDI32_M0S0ALU  : FmtM0_S0_ALU_RI_D<0x24,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "addi32\t$rt, $rs, $imm5", []>;
def ADDI32S_M0S0ALU : FmtM0_S0_ALU_RI_D<0x25,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "addi32s\t$rt, $rs, $imm5", []>;
def SUBI32_M0S0ALU  : FmtM0_S0_ALU_RI_D<0x26,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "subi32\t$rt, $rs, $imm5", []>;
def SUBI32S_M0S0ALU : FmtM0_S0_ALU_RI_D<0x27,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "subi32s\t$rt, $rs, $imm5", []>;
def ANDI32_M0S0ALU  : FmtM0_S0_ALU_RI_D<0x28,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "andi32\t$rt, $rs, $imm5", []>;
def ORI32_M0S0ALU   : FmtM0_S0_ALU_RI_D<0x29,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "ori32\t$rt, $rs, $imm5", []>;
def XORI32_M0S0ALU  : FmtM0_S0_ALU_RI_D<0x2A,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "xori32\t$rt, $rs, $imm5", []>;
def LUI_M0S0ALU     : FmtM0_S0_ALU_RI_D<0x2B,
    (outs GPR32:$rt), (ins GPR32:$rs, uimm5:$imm5),
    "lui\t$rt, $imm5", []>;

//===----------------------------------------------------------------------===//
// MOVT32/MOVF32 (predicate form, ISA-63). 4-operand model with tied dest
// ($rt = $rt_src) — tablegen reconstructs $rt_src from $rt automatically.
//===----------------------------------------------------------------------===//
let Constraints = "$rt = $rt_src" in {
def MOVT32_M0S0ALU : FmtM0_S0_ALU_PRED_D<0x0F,
    (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs, GPR32:$rs2),
    "movt32\t$rt, $rs, $rs2", []>;

def MOVF32_M0S0ALU : FmtM0_S0_ALU_PRED_D<0x1E,
    (outs GPR32:$rt), (ins GPR32:$rt_src, GPR32:$rs, GPR32:$rs2),
    "movf32\t$rt, $rs, $rs2", []>;
} // Constraints = "$rt = $rt_src"

//===----------------------------------------------------------------------===//
// MULL family (4 ops, 0x3F sub-variant). 3-operand destructive model with
// tied dest ($rt = $rs2) — tablegen reconstructs $rs2 from $rt automatically.
//===----------------------------------------------------------------------===//
let Constraints = "$rt = $rs2" in {
def MULL_M0S0ALU   : FmtM0_S0_ALU_MULL_D<0b000,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "mull\t$rt, $rs, $rs2", []>;

def MULSSH_M0S0ALU : FmtM0_S0_ALU_MULL_D<0b001,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "mulssh\t$rt, $rs, $rs2", []>;

def MULSUH_M0S0ALU : FmtM0_S0_ALU_MULL_D<0b010,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "mulsuh\t$rt, $rs, $rs2", []>;

def MULUUH_M0S0ALU : FmtM0_S0_ALU_MULL_D<0b011,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$rs2),
    "muluuh\t$rt, $rs, $rs2", []>;
} // Constraints = "$rt = $rs2"

//===----------------------------------------------------------------------===//
// MOVE32_DR_L/H — DR→GPR lane extract via the 0x3F escape (Phase 1d
// spec-blessed). ext[4]=1 marks DR-extended (disjoint from MULL ext[4]=0).
// MOVE32_DR_L sub_opcode=0x10 rt = rsd[31:00]
// MOVE32_DR_H sub_opcode=0x11 rt = rsd[63:32]
//===----------------------------------------------------------------------===//
def MOVE32_DR_L_M0S0 : FmtM0_S0_ALU_DREXT<0b10000,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_l\t$rt, $rsd", []>;
def MOVE32_DR_H_M0S0 : FmtM0_S0_ALU_DREXT<0b10001,
    (outs GPR32:$rt), (ins DR64:$rsd),
    "move32_dr_h\t$rt, $rsd", []>;
