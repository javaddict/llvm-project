// ===-- HaydnInstrInfoS1ALU64M0.td - s1 ALU64 flat encode variants -*===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ENCODE-only _S1_M0 variants for the FLAT (non-funct-routed) s1 ALU64 ops
// Mirrors the proven slice-2 ADD64_S1_M0 mechanism (HaydnInstrInfo.td
// 315 — class M0_S1_ALU64_RR + FmtM0_S1_ALU64, removed), extended to the
// full flat
// ALU64 inventory (50 ops excluding the 5 already migrated ADD64/SUB64
// AND64/OR64/XOR64).
//
// Source of truth: encoding_manual.md §6.6 (s1 ALU64 layout, 22-bit window):
// bits[45:44] = FU(2) = 00 (ALU)
// bits[43:40] = rsd1(4) — DR64 or GPR32 source 1 (4-bit encoding value)
// bits[39:36] = rtd(4) — DR64 or GPR32 dest (4-bit encoding value)
// bits[35:24] = opcode(12) — flat (funct-routed uses bits[11:10]=01 marker)
//
// The destructive s1 layout encodes ONLY (rtd, rsd1) reg fields. The.td
// models may carry a 2nd source (rs2) or an imm — those operands are
// UNENCODED at the bundle level (the hand-rolled packInstructionIntoSlot
// PS_S1 FU_ALU64 path reads only ReadReg(0)=rtd + ReadReg(1)=rsd1 and uses
// EncOpcode for the 12-bit opcode field; the SIMD shift-imm ops OR the
// shamt into the opcode low bits via getShiftImmWidth). These variants
// replicate that exactly via the tablegen Inst field bindings.
//
// (Flat2 migration, codex Option B; numbered because was
// already taken by the funct-3field-bit-budget ): the variants are
// now FORMAT-FIRST tablegen — isPseudo=0
// DecoderNamespace="HaydnM0S1ALU64Flat2*" (the unary/imm/SFR ops), so they
// populate a tablegen DecoderTable trie that the s1 Mode-0 decoder dispatches
// to (tryDecodeM0S1ALU64Flat2Table). The prior hand-rolled
// reverseEncLookupByFU decode for these ops is now redundant
// (retirement tracked separately). isCodeGenOnly is NOT set on the Flat2
// variants (they are real tablegen decodable Insts); they still do not enter
// HaydnAsmMatcher.td (this file is excluded from the matcher build).
// The SHAMT5/4 variants keep their own Shamt5/Shamt4 namespaces (split
// opcode_hi/shamt layout differs from the 2-field Flat2). The funct-routed
// family is unaffected (handled in HaydnInstrInfoM0.td).
//
// Operand-model classes (setDesc preserves operand count + order 1:1):
// (U) UNARY 2-op — generic (rd, rs); variant (rtd, rsd1). 25 ops.
// (R) RR3_COPY — generic (rd, rs1, rs2); variant (rtd, rs1, rs2).
// rs2 unencoded (decoded as a copy of rsd1). 4 ops.
// (I) RI6_ZERO — generic (rd, rs1, imm6); imm unencoded. 3 ops.
// (J) RI16_ZERO — generic (rd, rs1, imm16); imm unencoded. 2 ops.
// (H) SHAMT5/4 — SIMD shift-imm; shamt folded into opcode. 8 ops.
// (S) SFR 1-op — MOVEGPR2SFR (ins $rs), MOVESFR2GPR (outs $rd).
// Variant (rtd|rsd1) — single reg in one field. 2 ops.
//
// This file is included by Haydn.td (so -gen-emitter / -gen-instr-info see
// the variants) but NOT by HaydnAsmMatcher.td (asm-matcher sees generic only
// per the build-system split).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Format classes. The UNARY/RR3_COPY/RI6_ZERO/RI16_ZERO/SFR classes
// all bind Inst{43:40}=rsd1, Inst{39:36}=rtd, Inst{35:24}=opcode via
// FmtM0_S1_ALU64_FLAT2 (the 2-field Flat2 layout). The SHAMT5/4 classes use
// a split opcode_hi/shamt layout (their own namespaces). They differ only in
// the dag operand count + types so setDesc preserves the generic operand
// shape 1:1.
//===----------------------------------------------------------------------===//

// M0_S1_ALU64_UNARY — 2-op unary s1 ALU64 (rd, rs). 25 ops: MOVE64/NEG64
// NEG64S/NOT64/ABS64/ABS64S/SEQ64/SLT64/SLE64/MOVT64/MOVF64/TRANSF64*
// EXP2/LOG2/RECIP/SQRT/NSA*/NSAZ*/POPCOUNT64.
// Generic is (outs R:$rd), (ins R:$rs); variant (outs R:$rtd), (ins R:$rsd1).
// The register bank (DR64 vs GPR32) is the encoding value's low 4 bits in
// both cases — the field is `bits<4>` so the variant accepts either bank.
// The dag operand TYPE is preserved per-op (DR64 for MOVE64; GPR32 for
// EXP2; mixed for POPCOUNT64/TRANSF64) so the MC model matches the generic.
// now format-first via FmtM0_S1_ALU64_FLAT2 — the Inst IS the
// encoding (2 register fields + 12-bit opcode) AND these defs populate
// DecoderTableHaydnM0S1ALU64Flat2 (no hand-rolled reverseEncLookupByFU).
class M0_S1_ALU64_UNARY<bits<12> opcode, dag outs, dag ins, string asmstr>
  : FmtM0_S1_ALU64_FLAT2<opcode, outs, ins, asmstr, []> {
  let isPseudo = 0;
}

// the old single M0_S1_ALU64_RR3 class covered a MIX of operand
// shapes (DR64 rs2, GPR32 rs2, uimm6 imm, simm16 imm). A naive namespace
// flip to Flat2 fails tblgen with "could not find field for operand
// imm/rs2" — the 2-field format has no rs2/imm field for tblgen to bind.
// Codex Option B: SPLIT the class by operand shape so each subclass's dag
// matches the 2-field Inst layout (only rtd + rsd1 are encoded).
// M0_S1_ALU64_RR3_COPY — 3-op RR s1 ALU64 whose rs2 is UNENCODED
// (ADD64S_H/ADD64S_L/ADD64_H/ADD64_L: rd = rs1 + rs2, but the bundle/Mode-0
// form does not carry rs2 — the hand-rolled decode copied rsd1 into op2).
// Format-first via FmtM0_S1_ALU64_FLAT2 + a DecoderMethod hook that adds
// rtd, rsd1, rsd1 (rs2 := rsd1 copy) to match the legacy decode output.
class M0_S1_ALU64_RR3_COPY<bits<12> opcode, dag outs, dag ins, string asmstr>
  : FmtM0_S1_ALU64_FLAT2<opcode, outs, ins, asmstr, []> {
  let isPseudo = 0;
  let DecoderMethod = "decodeM0S1ALU64Flat2RR3Copy";
}

// M0_S1_ALU64_RI6_ZERO — 3-op s1 ALU64 with an UNENCODED 6-bit imm
// (SLLI64/SRLI64/SRAI64: the real imm lives in the standalone 48-bit WIDE
// parcel; the bundle form carries only the base opcode). The imm is modeled
// as a fixed-zero field so tblgen decoder generation has a field to bind.
// foot-gun: the `bits<6> imm` field below is a DECODER-ONLY binding so
// tblgen's decoder generator has a field to populate (the generated
// tryDecodeM0S1ALU64Flat2Table reads imm from the Inst and feeds it to
// addOperand). It is NEVER bound to Inst bits — the real imm lives in the
// standalone 48-bit WIDE parcel, NOT the bundle/Mode-0 form. Adding
// `let Inst{...} = imm` here would SILENTLY change the encoding (the imm
// would alias opcode or register bits in the 12-bit opcode field).
class M0_S1_ALU64_RI6_ZERO<bits<12> opcode, dag outs, dag ins, string asmstr>
  : FmtM0_S1_ALU64_FLAT2<opcode, outs, ins, asmstr, []> {
  let isPseudo = 0;
  bits<6> imm;
  let imm = 0;
}

// M0_S1_ALU64_RI16_ZERO — 3-op s1 ALU64 with an UNENCODEED 16-bit imm
// (ARCTAN/SIN_COS: simm16, not carried in the bundle form). Same fixed-zero
// imm technique as RI6_ZERO. Register banks are mixed (GPR32 dst, DR64 src)
// but the per-variant dag operand TYPE routes the 4-bit fields to the right
// class — no C++ hook needed (codex).
// foot-gun: the `bits<16> imm` field is a DECODER-ONLY binding so
// tblgen's decoder generator has a field to populate. It is NEVER bound to
// Inst bits — the real imm lives in the standalone 48-bit WIDE parcel
// NOT the bundle/Mode-0 form. The 16-bit imm cannot fit in the 12-bit
// opcode field anyway; a future `let Inst{...} = imm` binding would alias
// opcode/register bits and silently corrupt the encoding.
class M0_S1_ALU64_RI16_ZERO<bits<12> opcode, dag outs, dag ins, string asmstr>
  : FmtM0_S1_ALU64_FLAT2<opcode, outs, ins, asmstr, []> {
  let isPseudo = 0;
  bits<16> imm;
  let imm = 0;
}

// M0_S1_ALU64_RR3_BIN — 3-field binary s1 ALU64 (rd, rs1, rs2) for the
// ~6 binary RR-flat ops (ADD64S/X2ADD32/X2SUB32/X2SLL32/X2SRL32/X2SRA32).
// Inherits FmtM0_S1_ALU64_RR3FIELD (3-field: rsd1+rsd2+rtd) — encodes all 3
// registers (the old M0_S1_ALU64_RR3 / FmtM0_S1_ALU64, removed, dropped
// rsd2).
// Format-first: DecoderNamespace "HaydnM0S1ALU64RR3" -> DecoderTable trie.
// Distinct from M0_S1_ALU64_RR3 (2-field, kept for the imm ops SLLI64/ARCTAN
// etc. whose 2nd operand is an unencoded imm, not a register).
class M0_S1_ALU64_RR3_BIN<bits<8> opcode, dag outs, dag ins, string asmstr>
  : FmtM0_S1_ALU64_RR3FIELD<opcode, outs, ins, asmstr, []> {
  let isPseudo = 0;
  // isCodeGenOnly NOT set (inherits 0) — populates DecoderTableHaydnM0S1ALU64RR3.
  // Mirrors M0_S1_ALU64_RR / the funct family.
}

// M0_S1_ALU64_SHAMT — 3-op SIMD shift-imm (rd, rs1, shamt). The s1 12-bit
// opcode field carries BOTH the op-select base (high bits) AND the per
// instance shift amount (low N bits, encoding_manual.md §6.6).
// X2* family (32-bit lanes): shamt occupies bits[28:24] (N=5). The op
// select base has bits[28:24]=0 (stored values: X2SLLI32=0x060
// X2SRLI32=0x080, X2SRAI32=0x0A0, X2SRAI32R=0x0C0 — all have low 5 bits
// zero). The high 7 bits go in `opcode_hi` (bits[35:29]); the shamt
// operand fills bits[28:24].
// X4* family (16-bit lanes): shamt occupies bits[27:24] (N=4). Stored
// bases X4SLLI16=0x0E0, X4SRLI16=0x100, X4SRAI16=0x120, X4SRAI16R=0x140
// have low 4 bits zero. The high 8 bits go in `opcode_hi` (bits[35:28]);
// the shamt operand fills bits[27:24].
// The split is into DISJOINT Inst sub-ranges (no overlap), so tablegen
// emits a single word where opcode = (opcode_hi << N) | shamt — byte-equal
// to the hand-rolled `OpField = EncOpcode & 0xFFF; OpField |= shamt` fold
// (HaydnMCCodeEmitter.cpp:586-593). rs1/rtd carry the reg fields as usual.
class M0_S1_ALU64_SHAMT5<bits<7> opcode_hi, dag outs, dag ins, string asmstr>
  : HaydnInstD<8, outs, ins, asmstr, []> {
  // does NOT inherit FmtM0_S1_ALU64_FLAT2 — its split opcode_hi/shamt
  // layout differs. Gets its own DecoderTableHaydnM0S1ALU64Flat2Shamt5 trie.
  let isPseudo = 0;
  let DecoderNamespace = "HaydnM0S1ALU64Flat2Shamt5";
  bits<4> rtd;
  bits<4> rsd1;
  bits<5> shamt;
  let Inst{63-46} = 0;        // s2 placeholder
  let Inst{45-44} = 0b00;     // FU = 00 (ALU)
  let Inst{43-40} = rsd1;
  let Inst{39-36} = rtd;
  let Inst{35-29} = opcode_hi;  // op-select base high 7 bits (static)
  let Inst{28-24} = shamt;      // shamt low 5 bits (variable operand)
  let Inst{23-4}  = 0;        // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

class M0_S1_ALU64_SHAMT4<bits<8> opcode_hi, dag outs, dag ins, string asmstr>
  : HaydnInstD<8, outs, ins, asmstr, []> {
  // does NOT inherit FmtM0_S1_ALU64_FLAT2 — its split opcode_hi/shamt
  // layout differs. Gets its own DecoderTableHaydnM0S1ALU64Flat2Shamt4 trie.
  let isPseudo = 0;
  let DecoderNamespace = "HaydnM0S1ALU64Flat2Shamt4";
  bits<4> rtd;
  bits<4> rsd1;
  bits<4> shamt;
  let Inst{63-46} = 0;        // s2 placeholder
  let Inst{45-44} = 0b00;     // FU = 00 (ALU)
  let Inst{43-40} = rsd1;
  let Inst{39-36} = rtd;
  let Inst{35-28} = opcode_hi;  // op-select base high 8 bits (static)
  let Inst{27-24} = shamt;      // shamt low 4 bits (variable operand)
  let Inst{23-4}  = 0;        // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

// M0_S1_ALU64_SFR — single-reg SFR transfer (MOVEGPR2SFR / MOVESFR2GPR).
// These have asymmetric operand models (ins-only / outs-only) but both
// carry a single GPR32 operand. The hand-rolled PS_S1 FU_ALU64 path reads
// ReadReg(0)→rtd and ReadReg(1)→rsd1 (which is 0 since there is no op1);
// this variant mirrors that exactly: bind the single reg to rtd@39:36
// rsd1@43:40 stays 0 (no $rsd1 operand — so this class does NOT inherit
// FmtM0_S1_ALU64, removed, which would force a $rsd1 operand binding).
// Opcodes from getEncOpcodeMap: MOVEGPR2SFR=0x028, MOVESFR2GPR=0x029.
class M0_S1_ALU64_SFR<bits<12> opcode, dag outs, dag ins, string asmstr>
  : HaydnInstD<8, outs, ins, asmstr, []> {
  // format-first. DecoderNamespace "HaydnM0S1ALU64Flat2" populates
  // DecoderTableHaydnM0S1ALU64Flat2. MOVESFR2GPR's outs-only operand is
  // populated from rtd@39:36 by the trie (codex confirmed this works).
  let isPseudo = 0;
  let DecoderNamespace = "HaydnM0S1ALU64Flat2";
  bits<4> rtd;
  let Inst{63-46} = 0;        // s2 placeholder
  let Inst{45-44} = 0b00;     // FU = 00 (ALU)
  let Inst{43-40} = 0;        // rsd1 field unused (no second operand)
  let Inst{39-36} = rtd;
  let Inst{35-24} = opcode;
  let Inst{23-4}  = 0;        // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

//===----------------------------------------------------------------------===//
// UNARY 2-op variants (25 ops). Bank matches generic per-op.
//===----------------------------------------------------------------------===//

// DR64 dest + DR64 src unary (FmtALU64Unary real encodings + HaydnInst stubs)
def MOVE64_S1_M0     : M0_S1_ALU64_UNARY<0x006,
    (outs DR64:$rtd), (ins DR64:$rsd1), "move64\t$rtd, $rsd1">;
def NEG64_S1_M0      : M0_S1_ALU64_UNARY<0x007,
    (outs DR64:$rtd), (ins DR64:$rsd1), "neg64\t$rtd, $rsd1">;
def NEG64S_S1_M0     : M0_S1_ALU64_UNARY<0x008,
    (outs DR64:$rtd), (ins DR64:$rsd1), "neg64s\t$rtd, $rsd1">;
def NOT64_S1_M0      : M0_S1_ALU64_UNARY<0x009,
    (outs DR64:$rtd), (ins DR64:$rsd1), "not64\t$rtd, $rsd1">;
def ABS64_S1_M0      : M0_S1_ALU64_UNARY<0x00A,
    (outs DR64:$rtd), (ins DR64:$rsd1), "abs64\t$rtd, $rsd1">;
def ABS64S_S1_M0     : M0_S1_ALU64_UNARY<0x00B,
    (outs DR64:$rtd), (ins DR64:$rsd1), "abs64s\t$rtd, $rsd1">;
def SEQ64_S1_M0      : M0_S1_ALU64_UNARY<0x00C,
    (outs DR64:$rtd), (ins DR64:$rsd1), "seq64\t$rtd, $rsd1">;
def SLT64_S1_M0      : M0_S1_ALU64_UNARY<0x00D,
    (outs DR64:$rtd), (ins DR64:$rsd1), "slt64\t$rtd, $rsd1">;
def SLE64_S1_M0      : M0_S1_ALU64_UNARY<0x00E,
    (outs DR64:$rtd), (ins DR64:$rsd1), "sle64\t$rtd, $rsd1">;
def MOVT64_S1_M0     : M0_S1_ALU64_UNARY<0x00F,
    (outs DR64:$rtd), (ins DR64:$rsd1), "movt64\t$rtd, $rsd1">;
def MOVF64_S1_M0     : M0_S1_ALU64_UNARY<0x010,
    (outs DR64:$rtd), (ins DR64:$rsd1), "movf64\t$rtd, $rsd1">;
def TRANSF64_S1_M0   : M0_S1_ALU64_UNARY<0x020,
    (outs DR64:$rtd), (ins GPR32:$rsd1), "transf64\t$rtd, $rsd1">;
def TRANSF64F2_S1_M0 : M0_S1_ALU64_UNARY<0x021,
    (outs DR64:$rtd), (ins GPR32:$rsd1), "transf64f2\t$rtd, $rsd1">;
def TRANSF64_H_S1_M0 : M0_S1_ALU64_UNARY<0x022,
    (outs DR64:$rtd), (ins GPR32:$rsd1), "transf64_h\t$rtd, $rsd1">;
def TRANSF64_L_S1_M0 : M0_S1_ALU64_UNARY<0x023,
    (outs DR64:$rtd), (ins GPR32:$rsd1), "transf64_l\t$rtd, $rsd1">;
def NSA64_S1_M0      : M0_S1_ALU64_UNARY<0x01D,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "nsa64\t$rtd, $rsd1">;
def NSAZ64_S1_M0     : M0_S1_ALU64_UNARY<0x01E,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "nsaz64\t$rtd, $rsd1">;
def POPCOUNT64_S1_M0 : M0_S1_ALU64_UNARY<0x01F,
    (outs GPR32:$rtd), (ins DR64:$rsd1), "popcount64\t$rtd, $rsd1">;
def EXP2_S1_M0       : M0_S1_ALU64_UNARY<0x024,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "exp2\t$rtd, $rsd1">;
def LOG2_S1_M0       : M0_S1_ALU64_UNARY<0x025,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "log2\t$rtd, $rsd1">;
def RECIP_S1_M0      : M0_S1_ALU64_UNARY<0x026,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "recip\t$rtd, $rsd1">;
def SQRT_S1_M0       : M0_S1_ALU64_UNARY<0x027,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "sqrt\t$rtd, $rsd1">;
def NSA16_L_S1_M0    : M0_S1_ALU64_UNARY<0x02C,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "nsa16_l\t$rtd, $rsd1">;
def NSA32_L_S1_M0    : M0_S1_ALU64_UNARY<0x02D,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "nsa32_l\t$rtd, $rsd1">;
def NSAZ16_L_S1_M0   : M0_S1_ALU64_UNARY<0x02E,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "nsaz16_l\t$rtd, $rsd1">;
def NSAZ32_L_S1_M0   : M0_S1_ALU64_UNARY<0x02F,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "nsaz32_l\t$rtd, $rsd1">;

//===----------------------------------------------------------------------===//
// RR 3-op variants (no shamt folding) — rs2 unencoded.
//===----------------------------------------------------------------------===//

// FmtALU64 3-op DR64 RR (hand-roller reads op0/op1, drops rs2)
def ADD64S_S1_M0   : M0_S1_ALU64_RR3_BIN<0x3F,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rs2),
    "add64s\t$rtd, $rsd1, $rs2">;
// fix: opcodes corrected to the HW values from getEncOpcodeMap
// (HaydnDClassOpcodes.cpp:215-218): ADD64S_H=0x019, ADD64S_L=0x01A
// ADD64_H=0x01B, ADD64_L=0x01C. The old M0_S1_ALU64_RR3 stubs carried
// placeholder ordinals 0x001-0x004 which COLLIDE with the RR3_BIN ops
// SUB64/AND64/OR64/XOR64 (0x001-0x004) in the Flat2 12-bit opcode window
// when bits[35:32]=0 — the decoder could not distinguish them. The HW
// opcodes 0x019-0x01C are disjoint from RR3_BIN, so the Flat2 RR3_COPY
// trie routes these correctly. The hand-rolled reverseEncLookupByFU always
// used the correct 0x019-0x01C (via getEncOpcodeMap), so this also fixes a
// latent encode/decode mismatch (the format-first encoder emits the.td
// opcode bits directly).
def ADD64S_H_S1_M0 : M0_S1_ALU64_RR3_COPY<0x019,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rs2),
    "add64s_h\t$rtd, $rsd1, $rs2">;
def ADD64S_L_S1_M0 : M0_S1_ALU64_RR3_COPY<0x01A,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rs2),
    "add64s_l\t$rtd, $rsd1, $rs2">;
def ADD64_H_S1_M0  : M0_S1_ALU64_RR3_COPY<0x01B,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rs2),
    "add64_h\t$rtd, $rsd1, $rs2">;
def ADD64_L_S1_M0  : M0_S1_ALU64_RR3_COPY<0x01C,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rs2),
    "add64_l\t$rtd, $rsd1, $rs2">;
def X2ADD32_S1_M0  : M0_S1_ALU64_RR3_BIN<0x011,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rs2),
    "x2add32\t$rtd, $rsd1, $rs2">;
def X2SUB32_S1_M0  : M0_S1_ALU64_RR3_BIN<0x012,
    (outs DR64:$rtd), (ins DR64:$rsd1, DR64:$rs2),
    "x2sub32\t$rtd, $rsd1, $rs2">;
// FmtALU64 3-op DR64+GPR32 (X2SLL32 etc. — rs2 is GPR32 shift amount)
def X2SLL32_S1_M0  : M0_S1_ALU64_RR3_BIN<0x013,
    (outs DR64:$rtd), (ins DR64:$rsd1, GPR32:$rs2),
    "x2sll32\t$rtd, $rsd1, $rs2">;
def X2SRL32_S1_M0  : M0_S1_ALU64_RR3_BIN<0x014,
    (outs DR64:$rtd), (ins DR64:$rsd1, GPR32:$rs2),
    "x2srl32\t$rtd, $rsd1, $rs2">;
def X2SRA32_S1_M0  : M0_S1_ALU64_RR3_BIN<0x015,
    (outs DR64:$rtd), (ins DR64:$rsd1, GPR32:$rs2),
    "x2sra32\t$rtd, $rsd1, $rs2">;

// SLLI64/SRLI64/SRAI64 (Fmt48_WideDR_RI6 3-op; bundle path uses base opcode
// imm field unencoded — the wide 48-bit parcel is the real encoding form).
def SLLI64_S1_M0 : M0_S1_ALU64_RI6_ZERO<0x016,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm6_dr:$imm),
    "slli64\t$rtd, $rsd1, $imm">;
def SRLI64_S1_M0 : M0_S1_ALU64_RI6_ZERO<0x017,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm6_dr:$imm),
    "srli64\t$rtd, $rsd1, $imm">;
def SRAI64_S1_M0 : M0_S1_ALU64_RI6_ZERO<0x018,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm6_dr:$imm),
    "srai64\t$rtd, $rsd1, $imm">;

//===----------------------------------------------------------------------===//
// SIMD shift-imm — 8 ops. The shamt operand is folded into the s1
// opcode field's low N bits via the M0_S1_ALU64_SHAMT5/4 format classes
// (disjoint Inst sub-ranges). Generic (rd, rs1, shamt) 3-op = variant
// (rtd, rsd1, shamt) 3-op — blind setDesc, byte-equal to the hand-roller's
// `OpField |= shamt & ((1<<ShW)-1)` fold (HaydnMCCodeEmitter.cpp:586-593).
//
// Base op-select values from getEncOpcodeMap (HaydnDClassOpcodes.cpp L415-422)
// with the low N bits zeroed (stored shamt-zero base). The high bits pass as
// opcode_hi; the low N bits come from the shamt operand.
//===----------------------------------------------------------------------===//

// X2* family (32-bit lanes, 5-bit shamt, bits[28:24]). opcode_hi = base>>5.
def X2SLLI32_S1_M0  : M0_S1_ALU64_SHAMT5<0x03,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm5:$shamt),
    "x2slli32\t$rtd, $rsd1, $shamt">;
def X2SRLI32_S1_M0  : M0_S1_ALU64_SHAMT5<0x04,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm5:$shamt),
    "x2srli32\t$rtd, $rsd1, $shamt">;
def X2SRAI32_S1_M0  : M0_S1_ALU64_SHAMT5<0x05,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm5:$shamt),
    "x2srai32\t$rtd, $rsd1, $shamt">;
def X2SRAI32R_S1_M0 : M0_S1_ALU64_SHAMT5<0x06,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm5:$shamt),
    "x2srai32r\t$rtd, $rsd1, $shamt">;

// X4* family (16-bit lanes, 4-bit shamt, bits[27:24]). opcode_hi = base>>4.
def X4SLLI16_S1_M0  : M0_S1_ALU64_SHAMT4<0x0E,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm4:$shamt),
    "x4slli16\t$rtd, $rsd1, $shamt">;
def X4SRLI16_S1_M0  : M0_S1_ALU64_SHAMT4<0x10,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm4:$shamt),
    "x4srli16\t$rtd, $rsd1, $shamt">;
def X4SRAI16_S1_M0  : M0_S1_ALU64_SHAMT4<0x12,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm4:$shamt),
    "x4srai16\t$rtd, $rsd1, $shamt">;
def X4SRAI16R_S1_M0 : M0_S1_ALU64_SHAMT4<0x14,
    (outs DR64:$rtd), (ins DR64:$rsd1, uimm4:$shamt),
    "x4srai16r\t$rtd, $rsd1, $shamt">;

//===----------------------------------------------------------------------===//
// SFR transfers — MOVEGPR2SFR / MOVESFR2GPR. Single GPR32 operand
// bound to rtd@39:36 (matching the hand-rolled PS_S1 FU_ALU64 path which
// reads ReadReg(0)→rtd and ReadReg(1)→rsd1=0). Generic operand model:
// MOVEGPR2SFR — (outs), (ins GPR32:$rs); op0=$rs → rtd.
// MOVESFR2GPR — (outs GPR32:$rd), (ins); op0=$rd → rtd.
// The variant keeps the SAME operand model so setDesc is identity. The
// variant's $rtd binds to op0 by name; rsd1 stays 0 (no $rsd1 operand).
// Opcodes: MOVEGPR2SFR=0x028, MOVESFR2GPR=0x029.
//===----------------------------------------------------------------------===//
def MOVEGPR2SFR_S1_M0 : M0_S1_ALU64_SFR<0x028,
    (outs), (ins GPR32:$rtd),
    "movegpr2sfr\t$rtd">;
def MOVESFR2GPR_S1_M0 : M0_S1_ALU64_SFR<0x029,
    (outs GPR32:$rtd), (ins),
    "movesfr2gpr\t$rtd">;

//===----------------------------------------------------------------------===//
// ARCTAN / SIN_COS — 3-op with simm16 imm operand that the hand-roller does
// NOT encode (drops it — same as SLLI64 family). These are stubs with no
// real selector lowering; the bundle path is never exercised in tests.
// Generic (rd, rs, imm) = variant (rtd, rsd1, imm) — blind setDesc, imm
// unencoded. Hand-roller reads op0/op1 only.
//===----------------------------------------------------------------------===//
def ARCTAN_S1_M0 : M0_S1_ALU64_RI16_ZERO<0x02A,
    (outs GPR32:$rtd), (ins DR64:$rsd1, simm16:$imm),
    "arctan\t$rtd, $rsd1, $imm">;
def SIN_COS_S1_M0 : M0_S1_ALU64_RI16_ZERO<0x02B,
    (outs DR64:$rtd), (ins GPR32:$rsd1, simm16:$imm),
    "sin_cos\t$rtd, $rsd1, $imm">;
