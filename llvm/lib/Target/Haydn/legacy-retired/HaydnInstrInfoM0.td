//===----------------------------------------------------------------------===//
//
// Auto-generated funct-routed s1 ALU64 decoder defs (Phase 4).
//
// One <OP>_M0S1 def per RR (FmtALU64) funct-routed op. These are decode
// targets: when the Mode-0 disassembler routes a bundle's s1 window to a
// given funct value, the tablegen decoder trie over DecoderNamespace
// "HaydnM0S1ALU64" resolves it to one of these defs, and the asm printer
// emits the bare generic mnemonic (no _m0s1 suffix).
//
// Source of truth: getFunct (MCTargetDesc/HaydnDClassOpcodes.cpp) lists all
// 346 funct-routed ops. isAccumulateFunct in the same file marks the 129
// accumulate (FmtALU64Acc) funct values — those have a 3-way destructive
// s1 layout (rd_in=rd, rs1=rd, rs2) blocked by a tablegen tie limitation
// and are DEFERRED. The remaining 221 are RR (FmtALU64) ops and are
// emitted here. The rs2 operand type (DR64 or GPR32) mirrors the source def
// in HaydnInstrInfoAuto.td for each op (9 SIMD-shift-by-GPR ops use GPR32).
//
// isCodeGenOnly is 0 (default — do NOT set isCodeGenOnly=1, or the decoder
// table won't generate); Pattern=; the destructive s1 layout means
// $rtd=rd (dest), $rsd1=rs2 (2nd source), $rs1=rd (tied via Constraints
// "$rtd = $rs1"). The def NAME carries the _M0S1 suffix to keep the enum
// entry distinct; the asm string uses the bare generic mnemonic so objdump
// output matches the FmtALU64 instruction.
//
// NOTE: this file is not yet included by Haydn.td — coordinator integrates
// and reconciles the standalone MAX64_M0S1 spike def in HaydnInstrInfo.td
// (which this file also instantiates).
//
//===----------------------------------------------------------------------===//

// Each flat def mirrors the validated MAX64_M0S1 spike pattern:
// def <OP>_M0S1 : FmtM0_S1_ALU64_Fn<Funct
// (outs DR64:$rtd), (ins DR64:$rs1, <rs2type>:$rsd1)
// "<mnemonic>\t$rtd, $rs1, $rsd1", > {
// let Constraints = "$rtd = $rs1";
// let isPseudo = 0;
// }
// Emitted flat (not via multiclass) so the rs2 operand type can be set
// per-def from the source def — 9 SIMD-shift-by-GPR ops use GPR32, the
// rest use DR64. A multiclass with a generic Operand template param cannot
// cleanly express the mixed type without ambiguous substitution.

def MAX64_M0S1 : FmtM0_S1_ALU64_Fn<0xA4,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "max64\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

// slice 4c: cross-bank unary (GPR32 -> DR64) funct-routed s1 variant.
// SEXT_GPR32_TO_DR64 standalone = FmtALU64Unary<0x67, 0x197>; the s1 funct
// value carries over (0x197) — same identity scheme as MAX64 (0x0A4). Pure
// tblgen encode (Inst is the encoding); the GPR32 source binds to bits[43:40]
// via the new FmtM0_S1_ALU64_Fn_Unary_GPRToDR class. No tied constraint
// (cross-bank: DR64 dest ≠ GPR32 source). Closes the hwloop-div4 bundle fatal
// under -haydn-pure-tblgen-emit.
def SEXT_GPR32_TO_DR64_S1_M0 : FmtM0_S1_ALU64_Fn_Unary_GPRToDR<0x197,
    (outs DR64:$rtd), (ins GPR32:$rs),
    "sext32t64\t$rtd, $rs", []> {
  let isPseudo = 0;
  // slice D: DECODE-ENABLED. Inherits DecoderNamespace="HaydnM0S1ALU64"
  // from FmtM0_S1_ALU64_Fn_Unary_GPRToDR (HaydnInstrFormatsD.td:308) and
  // isCodeGenOnly=0 (default), so the variant now populates the funct trie
  // (DecoderTableHaydnM0S1ALU6464). The encoder routes SEXT_GPR32_TO_DR64 ->
  // this variant via HaydnSlotVariantFinalizer::getM0Variant (the cross-bank
  // unary mapping, line ~353), emitting a Mode-0 s1 funct bundle (bits[33:24]
  // = 0x197, marker bits[35:34]=01); the funct trie now round-trips those
  // exact bytes back to this def.
  //
  // Collision proof (the concern that motivated the prior encode-only guard):
  // funct 0x197 is UNIQUE in the HaydnM0S1ALU64 namespace. A full audit of
  // every FmtM0_S1_ALU64_Fn<> instantiation shows the highest existing funct
  // is 0x18F; 0x197 collides with NO other s1 funct-routed entry. The
  // tablegen trie keys on the funct value at bits[33:24], so the variant
  // matches only genuine Mode-0 SEXT bytes — there is no "stealing" of legacy
  // standalone sext32t64 bytes (the legacy standalone SEXT_GPR32_TO_DR64
  // FmtALU64Unary<0x67, 0x197> lives in a DIFFERENT namespace, and its bytes
  // are 32-bit FmtALU64 parcels dispatched by the standalone decode path
  // never reaching the s1 funct trie).
}

def MIN64_M0S1 : FmtM0_S1_ALU64_Fn<0xA5,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "min64\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SLL64_M0S1 : FmtM0_S1_ALU64_Fn<0x10C,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "sll64\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SRA64_M0S1 : FmtM0_S1_ALU64_Fn<0x135,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "sra64\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SRA64R_M0S1 : FmtM0_S1_ALU64_Fn<0x136,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "sra64r\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SRL64_M0S1 : FmtM0_S1_ALU64_Fn<0x137,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "srl64\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SUB64S_M0S1 : FmtM0_S1_ALU64_Fn<0x138,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "sub64s\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SUB64S_H_M0S1 : FmtM0_S1_ALU64_Fn<0x139,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "sub64s_h\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SUB64S_L_M0S1 : FmtM0_S1_ALU64_Fn<0x13A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "sub64s_l\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SUB64_H_M0S1 : FmtM0_S1_ALU64_Fn<0x13B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "sub64_h\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SUB64_L_M0S1 : FmtM0_S1_ALU64_Fn<0x13C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "sub64_l\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2ADD32S_M0S1 : FmtM0_S1_ALU64_Fn<0x13D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2add32s\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2ADD32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x13E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2add32s_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2ADD32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x13F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2add32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2ADDSUB32_M0S1 : FmtM0_S1_ALU64_Fn<0x140,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2addsub32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2ADDSUB32S_M0S1 : FmtM0_S1_ALU64_Fn<0x141,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2addsub32s\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2ADDSUB32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x142,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2addsub32s_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2ADDSUB32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x143,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2addsub32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2CLAMP32_M0S1 : FmtM0_S1_ALU64_Fn<0x144,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2clamp32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2FF2RSST32_M0S1 : FmtM0_S1_ALU64_Fn<0x14A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2ff2rsst32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2FF2RST32_M0S1 : FmtM0_S1_ALU64_Fn<0x14B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2ff2rst32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2FRSST32_M0S1 : FmtM0_S1_ALU64_Fn<0x155,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2frsst32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2FRST32_M0S1 : FmtM0_S1_ALU64_Fn<0x156,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2frst32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2MAX32_M0S1 : FmtM0_S1_ALU64_Fn<0x157,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2max32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2MIN32_M0S1 : FmtM0_S1_ALU64_Fn<0x158,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2min32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SEL32_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x15F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2sel32_hh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SEL32_HL_M0S1 : FmtM0_S1_ALU64_Fn<0x160,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2sel32_hl\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SEL32_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x161,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2sel32_lh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SEL32_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x162,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2sel32_ll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SRA32R_M0S1 : FmtM0_S1_ALU64_Fn<0x163,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "x2sra32r\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SUB32S_M0S1 : FmtM0_S1_ALU64_Fn<0x164,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2sub32s\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SUB32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x165,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2sub32s_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SUB32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x166,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2sub32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SUBADD32_M0S1 : FmtM0_S1_ALU64_Fn<0x167,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2subadd32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SUBADD32S_M0S1 : FmtM0_S1_ALU64_Fn<0x168,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2subadd32s\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SUBADD32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x169,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2subadd32s_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X2SUBADD32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x16A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x2subadd32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4ADD16S_M0S1 : FmtM0_S1_ALU64_Fn<0x16B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x4add16s\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4CLAMP16_M0S1 : FmtM0_S1_ALU64_Fn<0x170,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x4clamp16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4FRSST16_M0S1 : FmtM0_S1_ALU64_Fn<0x17D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x4frsst16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4FRST16_M0S1 : FmtM0_S1_ALU64_Fn<0x17E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x4frst16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4MAX16_M0S1 : FmtM0_S1_ALU64_Fn<0x17F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x4max16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4MIN16_M0S1 : FmtM0_S1_ALU64_Fn<0x180,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x4min16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4SAT32T16_M0S1 : FmtM0_S1_ALU64_Fn<0x181,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x4sat32t16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4SLL16_M0S1 : FmtM0_S1_ALU64_Fn<0x182,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "x4sll16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4SRA16_M0S1 : FmtM0_S1_ALU64_Fn<0x183,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "x4sra16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4SRA16R_M0S1 : FmtM0_S1_ALU64_Fn<0x184,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "x4sra16r\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4SRL16_M0S1 : FmtM0_S1_ALU64_Fn<0x185,
    (outs DR64:$rtd), (ins DR64:$rs1, GPR32:$rsd1),
    "x4srl16\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def X4SUB16S_M0S1 : FmtM0_S1_ALU64_Fn<0x186,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "x4sub16s\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULAS32RS_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x9,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulas32rs_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULAS32RS_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0xA,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulas32rs_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULAS32R_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0xB,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulas32r_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULAS32R_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0xC,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulas32r_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULSA32RS_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0xD,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulsa32rs_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULSA32RS_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0xE,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulsa32rs_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULSA32R_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0xF,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulsa32r_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULSA32R_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x10,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulsa32r_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZAA32RS_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x15,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzaa32rs_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZAA32RS_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x16,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzaa32rs_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZAA32R_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x17,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzaa32r_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZAA32R_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x18,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzaa32r_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZAS32RS_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x19,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzas32rs_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZAS32RS_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x1A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzas32rs_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZAS32R_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x1B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzas32r_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZAS32R_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x1C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzas32r_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZSA32RS_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x1D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzsa32rs_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZSA32RS_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x1E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzsa32rs_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZSA32R_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x1F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzsa32r_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZSA32R_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x20,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzsa32r_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZSS32RS_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x21,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzss32rs_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZSS32RS_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x22,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzss32rs_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZSS32R_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x23,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzss32r_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def F2MULZSS32R_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x24,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "f2mulzss32r_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FF2MUL32RS_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x25,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "ff2mul32rs_hh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FF2MUL32RS_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x26,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "ff2mul32rs_lh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FF2MUL32RS_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x27,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "ff2mul32rs_ll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FF2MUL32R_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x28,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "ff2mul32r_hh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FF2MUL32R_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x29,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "ff2mul32r_lh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FF2MUL32R_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x2A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "ff2mul32r_ll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_HH_M0S1 : FmtM0_S1_ALU64_Fn<0xA8,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_hh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_HL_M0S1 : FmtM0_S1_ALU64_Fn<0xA9,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_hl\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_HUH_M0S1 : FmtM0_S1_ALU64_Fn<0xAA,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_huh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_HUL_M0S1 : FmtM0_S1_ALU64_Fn<0xAB,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_hul\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_LH_M0S1 : FmtM0_S1_ALU64_Fn<0xAC,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_lh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_LL_M0S1 : FmtM0_S1_ALU64_Fn<0xAD,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_ll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_LUH_M0S1 : FmtM0_S1_ALU64_Fn<0xAE,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_luh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_LUL_M0S1 : FmtM0_S1_ALU64_Fn<0xAF,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_lul\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_UHH_M0S1 : FmtM0_S1_ALU64_Fn<0xB0,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_uhh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_UHL_M0S1 : FmtM0_S1_ALU64_Fn<0xB1,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_uhl\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_UHUH_M0S1 : FmtM0_S1_ALU64_Fn<0xB2,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_uhuh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_UHUL_M0S1 : FmtM0_S1_ALU64_Fn<0xB3,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_uhul\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_ULH_M0S1 : FmtM0_S1_ALU64_Fn<0xB4,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_ulh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_ULL_M0S1 : FmtM0_S1_ALU64_Fn<0xB5,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_ull\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_ULUH_M0S1 : FmtM0_S1_ALU64_Fn<0xB6,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_uluh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL64_ULUL_M0S1 : FmtM0_S1_ALU64_Fn<0xB7,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul64_ulul\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS00_M0S1 : FmtM0_S1_ALU64_Fn<0x37,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS01_M0S1 : FmtM0_S1_ALU64_Fn<0x38,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs01\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS02_M0S1 : FmtM0_S1_ALU64_Fn<0x39,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS03_M0S1 : FmtM0_S1_ALU64_Fn<0x3A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs03\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS11_M0S1 : FmtM0_S1_ALU64_Fn<0x3B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs11\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS12_M0S1 : FmtM0_S1_ALU64_Fn<0x3C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs12\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS13_M0S1 : FmtM0_S1_ALU64_Fn<0x3D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs13\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS22_M0S1 : FmtM0_S1_ALU64_Fn<0x3E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS23_M0S1 : FmtM0_S1_ALU64_Fn<0x3F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs23\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_HS33_M0S1 : FmtM0_S1_ALU64_Fn<0x40,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_hs33\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS00_M0S1 : FmtM0_S1_ALU64_Fn<0x41,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS01_M0S1 : FmtM0_S1_ALU64_Fn<0x42,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls01\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS02_M0S1 : FmtM0_S1_ALU64_Fn<0x43,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS03_M0S1 : FmtM0_S1_ALU64_Fn<0x44,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls03\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS11_M0S1 : FmtM0_S1_ALU64_Fn<0x45,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls11\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS12_M0S1 : FmtM0_S1_ALU64_Fn<0x46,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls12\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS13_M0S1 : FmtM0_S1_ALU64_Fn<0x47,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls13\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS22_M0S1 : FmtM0_S1_ALU64_Fn<0x48,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS23_M0S1 : FmtM0_S1_ALU64_Fn<0x49,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls23\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL16_LS33_M0S1 : FmtM0_S1_ALU64_Fn<0x4A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul16_ls33\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS00_M0S1 : FmtM0_S1_ALU64_Fn<0x6F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS01_M0S1 : FmtM0_S1_ALU64_Fn<0x70,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs01\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS02_M0S1 : FmtM0_S1_ALU64_Fn<0x71,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS03_M0S1 : FmtM0_S1_ALU64_Fn<0x72,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs03\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS11_M0S1 : FmtM0_S1_ALU64_Fn<0x73,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs11\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS12_M0S1 : FmtM0_S1_ALU64_Fn<0x74,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs12\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS13_M0S1 : FmtM0_S1_ALU64_Fn<0x75,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs13\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS22_M0S1 : FmtM0_S1_ALU64_Fn<0x76,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS23_M0S1 : FmtM0_S1_ALU64_Fn<0x77,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs23\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_HS33_M0S1 : FmtM0_S1_ALU64_Fn<0x78,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_hs33\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS00_M0S1 : FmtM0_S1_ALU64_Fn<0x79,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS01_M0S1 : FmtM0_S1_ALU64_Fn<0x7A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls01\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS02_M0S1 : FmtM0_S1_ALU64_Fn<0x7B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS03_M0S1 : FmtM0_S1_ALU64_Fn<0x7C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls03\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS11_M0S1 : FmtM0_S1_ALU64_Fn<0x7D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls11\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS12_M0S1 : FmtM0_S1_ALU64_Fn<0x7E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls12\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS13_M0S1 : FmtM0_S1_ALU64_Fn<0x7F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls13\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS22_M0S1 : FmtM0_S1_ALU64_Fn<0x80,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS23_M0S1 : FmtM0_S1_ALU64_Fn<0x81,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls23\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULS16_LS33_M0S1 : FmtM0_S1_ALU64_Fn<0x82,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmuls16_ls33\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_00_M0S1 : FmtM0_S1_ALU64_Fn<0x10D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_10_M0S1 : FmtM0_S1_ALU64_Fn<0x10E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_10\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_11_M0S1 : FmtM0_S1_ALU64_Fn<0x10F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_11\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_20_M0S1 : FmtM0_S1_ALU64_Fn<0x110,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_20\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_21_M0S1 : FmtM0_S1_ALU64_Fn<0x111,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_21\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_22_M0S1 : FmtM0_S1_ALU64_Fn<0x112,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_30_M0S1 : FmtM0_S1_ALU64_Fn<0x113,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_30\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_31_M0S1 : FmtM0_S1_ALU64_Fn<0x114,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_31\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_32_M0S1 : FmtM0_S1_ALU64_Fn<0x115,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16S_33_M0S1 : FmtM0_S1_ALU64_Fn<0x116,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16s_33\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_00_M0S1 : FmtM0_S1_ALU64_Fn<0x117,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_10_M0S1 : FmtM0_S1_ALU64_Fn<0x118,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_10\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_11_M0S1 : FmtM0_S1_ALU64_Fn<0x119,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_11\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_20_M0S1 : FmtM0_S1_ALU64_Fn<0x11A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_20\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_21_M0S1 : FmtM0_S1_ALU64_Fn<0x11B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_21\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_22_M0S1 : FmtM0_S1_ALU64_Fn<0x11C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_30_M0S1 : FmtM0_S1_ALU64_Fn<0x11D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_30\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_31_M0S1 : FmtM0_S1_ALU64_Fn<0x11E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_31\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_32_M0S1 : FmtM0_S1_ALU64_Fn<0x11F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULA16_33_M0S1 : FmtM0_S1_ALU64_Fn<0x120,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smula16_33\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_00_M0S1 : FmtM0_S1_ALU64_Fn<0x121,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_10_M0S1 : FmtM0_S1_ALU64_Fn<0x122,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_10\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_11_M0S1 : FmtM0_S1_ALU64_Fn<0x123,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_11\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_20_M0S1 : FmtM0_S1_ALU64_Fn<0x124,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_20\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_21_M0S1 : FmtM0_S1_ALU64_Fn<0x125,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_21\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_22_M0S1 : FmtM0_S1_ALU64_Fn<0x126,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_30_M0S1 : FmtM0_S1_ALU64_Fn<0x127,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_30\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_31_M0S1 : FmtM0_S1_ALU64_Fn<0x128,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_31\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_32_M0S1 : FmtM0_S1_ALU64_Fn<0x129,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16S_33_M0S1 : FmtM0_S1_ALU64_Fn<0x12A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16s_33\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_00_M0S1 : FmtM0_S1_ALU64_Fn<0x12B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_10_M0S1 : FmtM0_S1_ALU64_Fn<0x12C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_10\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_11_M0S1 : FmtM0_S1_ALU64_Fn<0x12D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_11\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_20_M0S1 : FmtM0_S1_ALU64_Fn<0x12E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_20\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_21_M0S1 : FmtM0_S1_ALU64_Fn<0x12F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_21\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_22_M0S1 : FmtM0_S1_ALU64_Fn<0x130,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_30_M0S1 : FmtM0_S1_ALU64_Fn<0x131,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_30\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_31_M0S1 : FmtM0_S1_ALU64_Fn<0x132,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_31\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_32_M0S1 : FmtM0_S1_ALU64_Fn<0x133,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_32\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def SMULS16_33_M0S1 : FmtM0_S1_ALU64_Fn<0x134,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "smuls16_33\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL32S_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x4B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul32s_hh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL32S_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x4C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul32s_lh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMUL32S_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x4D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmul32s_ll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAA32S_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x96,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzaa32s_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAA32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x97,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzaa32s_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAS32S_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x98,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzas32s_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAS32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x99,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzas32s_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSA32S_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x9A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzsa32s_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSA32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x9B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzsa32s_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSS32S_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0xA2,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzss32s_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSS32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0xA3,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzss32s_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MUL16ZAQ_M0S1 : FmtM0_S1_ALU64_Fn<0xA7,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mul16zaq\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULSA32_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0xED,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulsa32_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULSA32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0xEE,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulsa32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULSS32_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0xEF,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulss32_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULSS32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0xF0,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulss32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULZAA32_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x104,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulzaa32_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULZAA32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x105,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulzaa32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULZAS32_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x106,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulzas32_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULZAS32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x107,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulzas32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULZSA32_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x108,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulzsa32_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULZSA32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x109,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulzsa32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULZSS32_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x10A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulzss32_hhll\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def MULZSS32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x10B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "mulzss32_hllh\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULAA16_HS_13_02_M0S1 : FmtM0_S1_ALU64_Fn<0x66,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulaa16_hs_13_02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULAA16_HS_33_22_M0S1 : FmtM0_S1_ALU64_Fn<0x67,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulaa16_hs_33_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULAA16_LS_11_00_M0S1 : FmtM0_S1_ALU64_Fn<0x68,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulaa16_ls_11_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULAA16_LS_13_02_M0S1 : FmtM0_S1_ALU64_Fn<0x69,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulaa16_ls_13_02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULAA16_LS_33_22_M0S1 : FmtM0_S1_ALU64_Fn<0x6A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulaa16_ls_33_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULSS16_HS_13_02_M0S1 : FmtM0_S1_ALU64_Fn<0x89,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulss16_hs_13_02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULSS16_HS_33_22_M0S1 : FmtM0_S1_ALU64_Fn<0x8A,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulss16_hs_33_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULSS16_LS_11_00_M0S1 : FmtM0_S1_ALU64_Fn<0x8B,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulss16_ls_11_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULSS16_LS_13_02_M0S1 : FmtM0_S1_ALU64_Fn<0x8C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulss16_ls_13_02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULSS16_LS_33_22_M0S1 : FmtM0_S1_ALU64_Fn<0x8D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulss16_ls_33_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAA16_HS_11_00_M0S1 : FmtM0_S1_ALU64_Fn<0x90,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzaa16_hs_11_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAA16_HS_13_02_M0S1 : FmtM0_S1_ALU64_Fn<0x91,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzaa16_hs_13_02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAA16_HS_33_22_M0S1 : FmtM0_S1_ALU64_Fn<0x92,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzaa16_hs_33_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAA16_LS_11_00_M0S1 : FmtM0_S1_ALU64_Fn<0x93,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzaa16_ls_11_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAA16_LS_13_02_M0S1 : FmtM0_S1_ALU64_Fn<0x94,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzaa16_ls_13_02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZAA16_LS_33_22_M0S1 : FmtM0_S1_ALU64_Fn<0x95,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzaa16_ls_33_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSS16_HS_11_00_M0S1 : FmtM0_S1_ALU64_Fn<0x9C,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzss16_hs_11_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSS16_HS_13_02_M0S1 : FmtM0_S1_ALU64_Fn<0x9D,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzss16_hs_13_02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSS16_HS_33_22_M0S1 : FmtM0_S1_ALU64_Fn<0x9E,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzss16_hs_33_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSS16_LS_11_00_M0S1 : FmtM0_S1_ALU64_Fn<0x9F,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzss16_ls_11_00\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSS16_LS_13_02_M0S1 : FmtM0_S1_ALU64_Fn<0xA0,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzss16_ls_13_02\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}

def FMULZSS16_LS_33_22_M0S1 : FmtM0_S1_ALU64_Fn<0xA1,
    (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1),
    "fmulzss16_ls_33_22\t$rtd, $rs1, $rsd1", []> {
  let Constraints = "$rtd = $rs1";
  let isPseudo = 0;
}


//===----------------------------------------------------------------------===//
// Phase-4: 125 ACC (FmtALU64Acc) funct-routed s1 ALU64 decoder defs.
// 3-operand model (rd_in tied to rtd; rs1=rd printed via $rtd twice — the
// destructive s1 form). Avoids the tablegen 2-tie ban (gotcha 2).
def F2MULAA32RS_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x005,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "f2mulaa32rs_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def F2MULAA32RS_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x006,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "f2mulaa32rs_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def F2MULAA32R_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x007,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "f2mulaa32r_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def F2MULAA32R_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x008,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "f2mulaa32r_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def F2MULSS32RS_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x011,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "f2mulss32rs_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def F2MULSS32RS_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x012,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "f2mulss32rs_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def F2MULSS32R_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x013,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "f2mulss32r_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def F2MULSS32R_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x014,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "f2mulss32r_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULA32RS_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x02B,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2mula32rs_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULA32RS_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x02C,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2mula32rs_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULA32RS_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x02D,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2mula32rs_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULA32R_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x02E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2mula32r_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULA32R_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x02F,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2mula32r_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULA32R_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x030,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2mula32r_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULS32RS_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x031,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2muls32rs_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULS32RS_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x032,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2muls32rs_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULS32RS_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x033,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2muls32rs_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULS32R_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x034,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2muls32r_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULS32R_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x035,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2muls32r_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FF2MULS32R_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x036,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "ff2muls32r_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS00_M0S1 : FmtM0_S1_ALU64_Fn<0x04E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs00\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS01_M0S1 : FmtM0_S1_ALU64_Fn<0x04F,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs01\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS02_M0S1 : FmtM0_S1_ALU64_Fn<0x050,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs02\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS03_M0S1 : FmtM0_S1_ALU64_Fn<0x051,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs03\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS11_M0S1 : FmtM0_S1_ALU64_Fn<0x052,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs11\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS12_M0S1 : FmtM0_S1_ALU64_Fn<0x053,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs12\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS13_M0S1 : FmtM0_S1_ALU64_Fn<0x054,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs13\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS22_M0S1 : FmtM0_S1_ALU64_Fn<0x055,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs22\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS23_M0S1 : FmtM0_S1_ALU64_Fn<0x056,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs23\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_HS33_M0S1 : FmtM0_S1_ALU64_Fn<0x057,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_hs33\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS01_M0S1 : FmtM0_S1_ALU64_Fn<0x059,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls01\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS02_M0S1 : FmtM0_S1_ALU64_Fn<0x05A,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls02\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS03_M0S1 : FmtM0_S1_ALU64_Fn<0x05B,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls03\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS11_M0S1 : FmtM0_S1_ALU64_Fn<0x05C,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls11\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS12_M0S1 : FmtM0_S1_ALU64_Fn<0x05D,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls12\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS13_M0S1 : FmtM0_S1_ALU64_Fn<0x05E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls13\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS22_M0S1 : FmtM0_S1_ALU64_Fn<0x05F,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls22\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS23_M0S1 : FmtM0_S1_ALU64_Fn<0x060,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls23\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA16_LS33_M0S1 : FmtM0_S1_ALU64_Fn<0x061,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula16_ls33\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA32S_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x062,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula32s_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA32S_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x063,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula32s_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULA32S_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x064,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmula32s_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULAA16_HS_11_00_M0S1 : FmtM0_S1_ALU64_Fn<0x065,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulaa16_hs_11_00\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULAA32S_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x06B,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulaa32s_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULAA32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x06C,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulaa32s_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULAS32S_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x06D,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulas32s_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULAS32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x06E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulas32s_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULS32S_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x083,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmuls32s_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULS32S_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x084,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmuls32s_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULS32S_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x085,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmuls32s_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULSA32S_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x086,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulsa32s_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULSA32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x087,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulsa32s_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULSS16_HS_11_00_M0S1 : FmtM0_S1_ALU64_Fn<0x088,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulss16_hs_11_00\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULSS32S_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x08E,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulss32s_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def FMULSS32S_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x08F,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "fmulss32s_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MUL16AQ_M0S1 : FmtM0_S1_ALU64_Fn<0x0A6,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mul16aq\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x0B8,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_HL_M0S1 : FmtM0_S1_ALU64_Fn<0x0B9,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_hl\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_HUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0BA,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_huh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_HUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0BB,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_hul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x0BC,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x0BD,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_LUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0BE,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_luh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_LUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0BF,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_lul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_UHH_M0S1 : FmtM0_S1_ALU64_Fn<0x0C0,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_uhh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_UHL_M0S1 : FmtM0_S1_ALU64_Fn<0x0C1,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_uhl\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_UHUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0C2,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_uhuh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_UHUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0C3,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_uhul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_ULH_M0S1 : FmtM0_S1_ALU64_Fn<0x0C4,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_ulh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_ULL_M0S1 : FmtM0_S1_ALU64_Fn<0x0C5,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_ull\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_ULUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0C6,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_uluh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULA64_ULUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0C7,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mula64_ulul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAA32_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x0C8,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulaa32_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAA32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x0C9,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulaa32_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS32_HHLL_M0S1 : FmtM0_S1_ALU64_Fn<0x0CA,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas32_hhll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS32_HLLH_M0S1 : FmtM0_S1_ALU64_Fn<0x0CB,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas32_hllh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x0CC,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_HL_M0S1 : FmtM0_S1_ALU64_Fn<0x0CD,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_hl\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_HUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0CE,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_huh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_HUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0CF,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_hul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x0D0,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x0D1,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_LUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0D2,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_luh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_LUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0D3,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_lul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_UHH_M0S1 : FmtM0_S1_ALU64_Fn<0x0D4,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_uhh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_UHL_M0S1 : FmtM0_S1_ALU64_Fn<0x0D5,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_uhl\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_UHUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0D6,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_uhuh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_UHUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0D7,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_uhul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_ULH_M0S1 : FmtM0_S1_ALU64_Fn<0x0D8,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_ulh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_ULL_M0S1 : FmtM0_S1_ALU64_Fn<0x0D9,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_ull\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_ULUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0DA,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_uluh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULAS64_ULUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0DB,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulas64_ulul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x0DD,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_HL_M0S1 : FmtM0_S1_ALU64_Fn<0x0DE,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_hl\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_HUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0DF,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_huh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_HUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0E0,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_hul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x0E1,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x0E2,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_LUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0E3,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_luh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_LUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0E4,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_lul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_UHH_M0S1 : FmtM0_S1_ALU64_Fn<0x0E5,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_uhh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_UHL_M0S1 : FmtM0_S1_ALU64_Fn<0x0E6,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_uhl\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_UHUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0E7,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_uhuh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_UHUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0E8,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_uhul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_ULH_M0S1 : FmtM0_S1_ALU64_Fn<0x0E9,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_ulh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_ULL_M0S1 : FmtM0_S1_ALU64_Fn<0x0EA,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_ull\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_ULUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0EB,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_uluh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULS64_ULUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0EC,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "muls64_ulul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_HH_M0S1 : FmtM0_S1_ALU64_Fn<0x0F1,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_hh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_HL_M0S1 : FmtM0_S1_ALU64_Fn<0x0F2,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_hl\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_HUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0F3,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_huh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_HUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0F4,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_hul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_LH_M0S1 : FmtM0_S1_ALU64_Fn<0x0F5,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_lh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_LL_M0S1 : FmtM0_S1_ALU64_Fn<0x0F6,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_ll\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_LUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0F7,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_luh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_LUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0F8,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_lul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_UHH_M0S1 : FmtM0_S1_ALU64_Fn<0x0F9,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_uhh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_UHL_M0S1 : FmtM0_S1_ALU64_Fn<0x0FA,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_uhl\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_UHUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0FB,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_uhuh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_UHUL_M0S1 : FmtM0_S1_ALU64_Fn<0x0FC,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_uhul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_ULH_M0S1 : FmtM0_S1_ALU64_Fn<0x0FD,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_ulh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_ULL_M0S1 : FmtM0_S1_ALU64_Fn<0x0FE,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_ull\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_ULUH_M0S1 : FmtM0_S1_ALU64_Fn<0x0FF,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_uluh\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}
def MULSS64_ULUL_M0S1 : FmtM0_S1_ALU64_Fn<0x100,
    (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1),
    "mulss64_ulul\t$rtd, $rtd, $rsd1", []> {
  bits<4> rd_in;
  let Constraints = "$rtd = $rd_in";
  let isPseudo = 0;
}

//===--- Phase-4: s2 funct-routed ALU64 defs (§6.7, opcode=funct, <0x100)
def F2MULAS32RS_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x009, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulas32rs_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULAS32RS_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x00A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulas32rs_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULAS32R_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x00B, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulas32r_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULAS32R_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x00C, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulas32r_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULSA32RS_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x00D, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulsa32rs_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULSA32RS_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x00E, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulsa32rs_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULSA32R_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x00F, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulsa32r_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULSA32R_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x010, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulsa32r_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZAA32RS_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x015, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzaa32rs_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZAA32RS_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x016, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzaa32rs_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZAA32R_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x017, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzaa32r_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZAA32R_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x018, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzaa32r_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZAS32RS_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x019, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzas32rs_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZAS32RS_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x01A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzas32rs_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZAS32R_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x01B, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzas32r_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZAS32R_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x01C, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzas32r_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZSA32RS_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x01D, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzsa32rs_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZSA32RS_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x01E, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzsa32rs_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZSA32R_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x01F, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzsa32r_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZSA32R_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x020, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzsa32r_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZSS32RS_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x021, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzss32rs_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZSS32RS_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x022, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzss32rs_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZSS32R_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x023, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzss32r_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULZSS32R_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x024, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "f2mulzss32r_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FF2MUL32RS_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x025, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "ff2mul32rs_hh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FF2MUL32RS_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x026, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "ff2mul32rs_lh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FF2MUL32RS_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x027, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "ff2mul32rs_ll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FF2MUL32R_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x028, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "ff2mul32r_hh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FF2MUL32R_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x029, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "ff2mul32r_lh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FF2MUL32R_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x02A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "ff2mul32r_ll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS00_M0S2 : FmtM0_S2_ALU64_Fn<0x037, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS01_M0S2 : FmtM0_S2_ALU64_Fn<0x038, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs01\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS02_M0S2 : FmtM0_S2_ALU64_Fn<0x039, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS03_M0S2 : FmtM0_S2_ALU64_Fn<0x03A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs03\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS11_M0S2 : FmtM0_S2_ALU64_Fn<0x03B, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs11\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS12_M0S2 : FmtM0_S2_ALU64_Fn<0x03C, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs12\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS13_M0S2 : FmtM0_S2_ALU64_Fn<0x03D, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs13\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS22_M0S2 : FmtM0_S2_ALU64_Fn<0x03E, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS23_M0S2 : FmtM0_S2_ALU64_Fn<0x03F, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs23\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_HS33_M0S2 : FmtM0_S2_ALU64_Fn<0x040, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_hs33\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS00_M0S2 : FmtM0_S2_ALU64_Fn<0x041, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS01_M0S2 : FmtM0_S2_ALU64_Fn<0x042, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls01\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS02_M0S2 : FmtM0_S2_ALU64_Fn<0x043, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS03_M0S2 : FmtM0_S2_ALU64_Fn<0x044, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls03\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS11_M0S2 : FmtM0_S2_ALU64_Fn<0x045, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls11\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS12_M0S2 : FmtM0_S2_ALU64_Fn<0x046, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls12\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS13_M0S2 : FmtM0_S2_ALU64_Fn<0x047, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls13\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS22_M0S2 : FmtM0_S2_ALU64_Fn<0x048, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS23_M0S2 : FmtM0_S2_ALU64_Fn<0x049, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls23\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL16_LS33_M0S2 : FmtM0_S2_ALU64_Fn<0x04A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul16_ls33\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL32S_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x04B, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul32s_hh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL32S_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x04C, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul32s_lh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMUL32S_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x04D, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmul32s_ll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULAA16_HS_13_02_M0S2 : FmtM0_S2_ALU64_Fn<0x066, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulaa16_hs_13_02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULAA16_HS_33_22_M0S2 : FmtM0_S2_ALU64_Fn<0x067, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulaa16_hs_33_22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULAA16_LS_11_00_M0S2 : FmtM0_S2_ALU64_Fn<0x068, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulaa16_ls_11_00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULAA16_LS_13_02_M0S2 : FmtM0_S2_ALU64_Fn<0x069, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulaa16_ls_13_02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULAA16_LS_33_22_M0S2 : FmtM0_S2_ALU64_Fn<0x06A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulaa16_ls_33_22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS00_M0S2 : FmtM0_S2_ALU64_Fn<0x06F, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS01_M0S2 : FmtM0_S2_ALU64_Fn<0x070, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs01\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS02_M0S2 : FmtM0_S2_ALU64_Fn<0x071, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS03_M0S2 : FmtM0_S2_ALU64_Fn<0x072, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs03\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS11_M0S2 : FmtM0_S2_ALU64_Fn<0x073, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs11\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS12_M0S2 : FmtM0_S2_ALU64_Fn<0x074, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs12\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS13_M0S2 : FmtM0_S2_ALU64_Fn<0x075, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs13\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS22_M0S2 : FmtM0_S2_ALU64_Fn<0x076, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS23_M0S2 : FmtM0_S2_ALU64_Fn<0x077, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs23\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_HS33_M0S2 : FmtM0_S2_ALU64_Fn<0x078, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_hs33\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS00_M0S2 : FmtM0_S2_ALU64_Fn<0x079, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS01_M0S2 : FmtM0_S2_ALU64_Fn<0x07A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls01\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS02_M0S2 : FmtM0_S2_ALU64_Fn<0x07B, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS03_M0S2 : FmtM0_S2_ALU64_Fn<0x07C, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls03\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS11_M0S2 : FmtM0_S2_ALU64_Fn<0x07D, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls11\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS12_M0S2 : FmtM0_S2_ALU64_Fn<0x07E, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls12\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS13_M0S2 : FmtM0_S2_ALU64_Fn<0x07F, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls13\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS22_M0S2 : FmtM0_S2_ALU64_Fn<0x080, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS23_M0S2 : FmtM0_S2_ALU64_Fn<0x081, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls23\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULS16_LS33_M0S2 : FmtM0_S2_ALU64_Fn<0x082, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmuls16_ls33\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULSS16_HS_13_02_M0S2 : FmtM0_S2_ALU64_Fn<0x089, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulss16_hs_13_02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULSS16_HS_33_22_M0S2 : FmtM0_S2_ALU64_Fn<0x08A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulss16_hs_33_22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULSS16_LS_11_00_M0S2 : FmtM0_S2_ALU64_Fn<0x08B, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulss16_ls_11_00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULSS16_LS_13_02_M0S2 : FmtM0_S2_ALU64_Fn<0x08C, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulss16_ls_13_02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULSS16_LS_33_22_M0S2 : FmtM0_S2_ALU64_Fn<0x08D, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulss16_ls_33_22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAA16_HS_11_00_M0S2 : FmtM0_S2_ALU64_Fn<0x090, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzaa16_hs_11_00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAA16_HS_13_02_M0S2 : FmtM0_S2_ALU64_Fn<0x091, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzaa16_hs_13_02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAA16_HS_33_22_M0S2 : FmtM0_S2_ALU64_Fn<0x092, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzaa16_hs_33_22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAA16_LS_11_00_M0S2 : FmtM0_S2_ALU64_Fn<0x093, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzaa16_ls_11_00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAA16_LS_13_02_M0S2 : FmtM0_S2_ALU64_Fn<0x094, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzaa16_ls_13_02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAA16_LS_33_22_M0S2 : FmtM0_S2_ALU64_Fn<0x095, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzaa16_ls_33_22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAA32S_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x096, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzaa32s_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAA32S_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x097, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzaa32s_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAS32S_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x098, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzas32s_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZAS32S_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x099, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzas32s_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSA32S_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x09A, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzsa32s_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSA32S_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x09B, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzsa32s_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSS16_HS_11_00_M0S2 : FmtM0_S2_ALU64_Fn<0x09C, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzss16_hs_11_00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSS16_HS_13_02_M0S2 : FmtM0_S2_ALU64_Fn<0x09D, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzss16_hs_13_02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSS16_HS_33_22_M0S2 : FmtM0_S2_ALU64_Fn<0x09E, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzss16_hs_33_22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSS16_LS_11_00_M0S2 : FmtM0_S2_ALU64_Fn<0x09F, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzss16_ls_11_00\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSS16_LS_13_02_M0S2 : FmtM0_S2_ALU64_Fn<0x0A0, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzss16_ls_13_02\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSS16_LS_33_22_M0S2 : FmtM0_S2_ALU64_Fn<0x0A1, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzss16_ls_33_22\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSS32S_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x0A2, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzss32s_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def FMULZSS32S_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x0A3, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "fmulzss32s_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MAX64_M0S2 : FmtM0_S2_ALU64_Fn<0x0A4, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "max64\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MIN64_M0S2 : FmtM0_S2_ALU64_Fn<0x0A5, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "min64\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL16ZAQ_M0S2 : FmtM0_S2_ALU64_Fn<0x0A7, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul16zaq\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x0A8, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_hh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_HL_M0S2 : FmtM0_S2_ALU64_Fn<0x0A9, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_hl\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_HUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0AA, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_huh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_HUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0AB, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_hul\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x0AC, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_lh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x0AD, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_ll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_LUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0AE, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_luh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_LUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0AF, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_lul\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_UHH_M0S2 : FmtM0_S2_ALU64_Fn<0x0B0, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_uhh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_UHL_M0S2 : FmtM0_S2_ALU64_Fn<0x0B1, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_uhl\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_UHUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0B2, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_uhuh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_UHUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0B3, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_uhul\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_ULH_M0S2 : FmtM0_S2_ALU64_Fn<0x0B4, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_ulh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_ULL_M0S2 : FmtM0_S2_ALU64_Fn<0x0B5, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_ull\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_ULUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0B6, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_uluh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MUL64_ULUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0B7, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mul64_ulul\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MULSA32_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x0ED, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mulsa32_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MULSA32_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x0EE, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mulsa32_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MULSS32_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x0EF, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mulss32_hhll\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def MULSS32_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x0F0, (outs DR64:$rtd), (ins DR64:$rs1, DR64:$rsd1), "mulss32_hllh\t$rtd, $rs1, $rsd1", []> { let Constraints = "$rtd = $rs1"; let isPseudo = 0; }
def F2MULAA32RS_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x005, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "f2mulaa32rs_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def F2MULAA32RS_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x006, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "f2mulaa32rs_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def F2MULAA32R_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x007, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "f2mulaa32r_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def F2MULAA32R_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x008, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "f2mulaa32r_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def F2MULSS32RS_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x011, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "f2mulss32rs_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def F2MULSS32RS_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x012, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "f2mulss32rs_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def F2MULSS32R_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x013, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "f2mulss32r_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def F2MULSS32R_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x014, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "f2mulss32r_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULA32RS_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x02B, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2mula32rs_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULA32RS_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x02C, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2mula32rs_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULA32RS_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x02D, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2mula32rs_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULA32R_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x02E, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2mula32r_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULA32R_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x02F, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2mula32r_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULA32R_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x030, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2mula32r_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULS32RS_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x031, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2muls32rs_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULS32RS_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x032, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2muls32rs_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULS32RS_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x033, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2muls32rs_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULS32R_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x034, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2muls32r_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULS32R_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x035, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2muls32r_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FF2MULS32R_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x036, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "ff2muls32r_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS00_M0S2 : FmtM0_S2_ALU64_Fn<0x04E, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs00\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS01_M0S2 : FmtM0_S2_ALU64_Fn<0x04F, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs01\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS02_M0S2 : FmtM0_S2_ALU64_Fn<0x050, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs02\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS03_M0S2 : FmtM0_S2_ALU64_Fn<0x051, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs03\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS11_M0S2 : FmtM0_S2_ALU64_Fn<0x052, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs11\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS12_M0S2 : FmtM0_S2_ALU64_Fn<0x053, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs12\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS13_M0S2 : FmtM0_S2_ALU64_Fn<0x054, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs13\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS22_M0S2 : FmtM0_S2_ALU64_Fn<0x055, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs22\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS23_M0S2 : FmtM0_S2_ALU64_Fn<0x056, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs23\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_HS33_M0S2 : FmtM0_S2_ALU64_Fn<0x057, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_hs33\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS00_M0S2 : FmtM0_S2_ALU64_Fn<0x058, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls00\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS01_M0S2 : FmtM0_S2_ALU64_Fn<0x059, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls01\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS02_M0S2 : FmtM0_S2_ALU64_Fn<0x05A, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls02\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS03_M0S2 : FmtM0_S2_ALU64_Fn<0x05B, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls03\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS11_M0S2 : FmtM0_S2_ALU64_Fn<0x05C, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls11\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS12_M0S2 : FmtM0_S2_ALU64_Fn<0x05D, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls12\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS13_M0S2 : FmtM0_S2_ALU64_Fn<0x05E, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls13\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS22_M0S2 : FmtM0_S2_ALU64_Fn<0x05F, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls22\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS23_M0S2 : FmtM0_S2_ALU64_Fn<0x060, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls23\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA16_LS33_M0S2 : FmtM0_S2_ALU64_Fn<0x061, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula16_ls33\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA32S_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x062, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula32s_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA32S_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x063, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula32s_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULA32S_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x064, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmula32s_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULAA16_HS_11_00_M0S2 : FmtM0_S2_ALU64_Fn<0x065, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulaa16_hs_11_00\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULAA32S_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x06B, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulaa32s_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULAA32S_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x06C, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulaa32s_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULAS32S_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x06D, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulas32s_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULAS32S_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x06E, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulas32s_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULS32S_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x083, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmuls32s_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULS32S_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x084, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmuls32s_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULS32S_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x085, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmuls32s_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULSA32S_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x086, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulsa32s_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULSA32S_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x087, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulsa32s_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULSS16_HS_11_00_M0S2 : FmtM0_S2_ALU64_Fn<0x088, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulss16_hs_11_00\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULSS32S_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x08E, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulss32s_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def FMULSS32S_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x08F, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "fmulss32s_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MUL16AQ_M0S2 : FmtM0_S2_ALU64_Fn<0x0A6, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mul16aq\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x0B8, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_HL_M0S2 : FmtM0_S2_ALU64_Fn<0x0B9, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_hl\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_HUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0BA, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_huh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_HUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0BB, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_hul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x0BC, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x0BD, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_LUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0BE, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_luh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_LUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0BF, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_lul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_UHH_M0S2 : FmtM0_S2_ALU64_Fn<0x0C0, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_uhh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_UHL_M0S2 : FmtM0_S2_ALU64_Fn<0x0C1, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_uhl\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_UHUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0C2, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_uhuh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_UHUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0C3, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_uhul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_ULH_M0S2 : FmtM0_S2_ALU64_Fn<0x0C4, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_ulh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_ULL_M0S2 : FmtM0_S2_ALU64_Fn<0x0C5, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_ull\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_ULUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0C6, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_uluh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULA64_ULUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0C7, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mula64_ulul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAA32_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x0C8, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulaa32_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAA32_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x0C9, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulaa32_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS32_HHLL_M0S2 : FmtM0_S2_ALU64_Fn<0x0CA, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas32_hhll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS32_HLLH_M0S2 : FmtM0_S2_ALU64_Fn<0x0CB, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas32_hllh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x0CC, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_HL_M0S2 : FmtM0_S2_ALU64_Fn<0x0CD, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_hl\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_HUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0CE, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_huh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_HUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0CF, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_hul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x0D0, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x0D1, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_LUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0D2, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_luh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_LUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0D3, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_lul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_UHH_M0S2 : FmtM0_S2_ALU64_Fn<0x0D4, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_uhh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_UHL_M0S2 : FmtM0_S2_ALU64_Fn<0x0D5, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_uhl\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_UHUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0D6, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_uhuh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_UHUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0D7, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_uhul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_ULH_M0S2 : FmtM0_S2_ALU64_Fn<0x0D8, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_ulh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_ULL_M0S2 : FmtM0_S2_ALU64_Fn<0x0D9, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_ull\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_ULUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0DA, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_uluh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULAS64_ULUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0DB, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulas64_ulul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x0DD, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_HL_M0S2 : FmtM0_S2_ALU64_Fn<0x0DE, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_hl\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_HUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0DF, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_huh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_HUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0E0, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_hul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x0E1, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x0E2, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_LUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0E3, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_luh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_LUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0E4, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_lul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_UHH_M0S2 : FmtM0_S2_ALU64_Fn<0x0E5, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_uhh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_UHL_M0S2 : FmtM0_S2_ALU64_Fn<0x0E6, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_uhl\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_UHUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0E7, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_uhuh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_UHUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0E8, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_uhul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_ULH_M0S2 : FmtM0_S2_ALU64_Fn<0x0E9, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_ulh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_ULL_M0S2 : FmtM0_S2_ALU64_Fn<0x0EA, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_ull\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_ULUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0EB, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_uluh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULS64_ULUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0EC, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "muls64_ulul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_HH_M0S2 : FmtM0_S2_ALU64_Fn<0x0F1, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_hh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_HL_M0S2 : FmtM0_S2_ALU64_Fn<0x0F2, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_hl\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_HUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0F3, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_huh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_HUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0F4, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_hul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_LH_M0S2 : FmtM0_S2_ALU64_Fn<0x0F5, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_lh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_LL_M0S2 : FmtM0_S2_ALU64_Fn<0x0F6, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_ll\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_LUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0F7, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_luh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_LUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0F8, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_lul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_UHH_M0S2 : FmtM0_S2_ALU64_Fn<0x0F9, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_uhh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_UHL_M0S2 : FmtM0_S2_ALU64_Fn<0x0FA, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_uhl\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_UHUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0FB, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_uhuh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_UHUL_M0S2 : FmtM0_S2_ALU64_Fn<0x0FC, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_uhul\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_ULH_M0S2 : FmtM0_S2_ALU64_Fn<0x0FD, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_ulh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_ULL_M0S2 : FmtM0_S2_ALU64_Fn<0x0FE, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_ull\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
def MULSS64_ULUH_M0S2 : FmtM0_S2_ALU64_Fn<0x0FF, (outs DR64:$rtd), (ins DR64:$rd_in, DR64:$rsd1), "mulss64_uluh\t$rtd, $rtd, $rsd1", []> { bits<4> rd_in; let Constraints = "$rtd = $rd_in"; let isPseudo = 0; }
