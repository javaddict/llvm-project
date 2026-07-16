//===----------------------------------------------------------------------===//
//
// Decode-included s1 Load variants (Mode 0, — item 2).
//
// One <OP>_M0S1LD def per s1 LD opcode routed through getEncOpcodeMap's
// FU_LD table (HaydnDClassOpcodes.cpp:231-270). These are DECODE targets:
// when the Mode-0 disassembler routes a bundle's s1 LD window, the tablegen
// decoder trie over DecoderNamespace "HaydnM0S1LD" resolves it to one of
// these defs, and the asm printer emits the bare generic mnemonic.
//
// Source of truth: encoding_manual.md §6.8 (lines 1228-1272). The s1 LD unit
// is dual-format:
// * Imm-offset (plain + fused post-inc): FU=01@45:44, rs@43:40, rtd@39:36
// imm8@35:28 (signed byte offset, or sign-extended imm6 element index for
// the fused post-inc forms), opcode(4)@27:24.
// * Register-offset POST_REG: FU=01@45:44, rs@43:40, rsd2@39:36
// 0x1@35:32 (POST_REG variant selector — fixed), type(4)@31:28
// (0x0=DW / 0x1=W / 0x2=HW), rt@27:24.
// The POST_REG signature bits[35:32]=0x1 is a fixed constant the trie matches
// to disambiguate from the imm-offset forms sharing nibble 0x3.
//
// IMPLEMENTATION NOTE (encoder/decoder swap vs §6.8): the legacy encoder
// (HaydnMCCodeEmitter.cpp FU_LD L663-670) packs the s1 LD payload as
// bits[35:28] = imm8 (8b) + bits[27:24] = opcode (4b)
// i.e. the §6.8 opcode/imm4 layout is swapped. Encode and decode round-trip
// correctly under the swapped layout (mode0-ld-imm-offset-decode.s pins it).
// These _M0S1LD variants model the SHIPPED encoder layout (imm8@35:28
// opcode@27:24), NOT the §6.8 nominal layout. See + that test's header.
//
// isCodeGenOnly is 0 (default — do NOT set isCodeGenOnly=1, or the decoder
// table won't generate); Pattern=; bare mnemonic; DecoderNamespace=
// "HaydnM0S1LD" isolates these from the DEFAULT asm-match + Haydn64 decode
// namespaces so they DECODE only (asm parse of "ld64_s1..." still matches
// the generic FmtLS LD64_S1). Mirrors the proven s0 LS pattern
// (HaydnInstrInfoS0LSM0.td) + MAC pattern (HaydnInstrInfoMACM0.td).
//
// This file is included by Haydn.td (so -gen-disassembler / -gen-asm-writer
// gen-instr-info / -gen-emitter see the variants) but NOT by HaydnAsmMatcher.td
// (so -gen-asm-matcher does NOT — the build-system split).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand classes.
//
// The imm-offset field carries different semantic content per op:
// * Plain LD64_S1 / LD32_S1 — raw 8-bit unsigned byte offset (encoder
// emits the full simm16 << 0 truncated to 8 bits; decode zero-extends).
// * Fused post-inc LD64_POST / LD32_POST / ST64_POST / ST32_POST
// D_LDW_POST_IMM / S_LW_POST_IMM — sign-extended imm6 element index in
// the low 6 bits of imm8 (stride >> ScaleShift). Decode sign-extends
// from 6 bits so negative (decrementing) strides round-trip. The high
// 2 bits of imm8 are the sign extension filler (decode ignores them
// only the low 6 bits carry the element index).
//===----------------------------------------------------------------------===//

// s1 LD raw imm8 — plain LD64_S1 / LD32_S1 byte offset (8-bit, zero-extended).
// Phase 3 Option A: DUAL-USE — EncoderMethod writes Imm&0xFF (raw
// matching the hand-rolled FU_LD non-fused path which reads MI.getOperand(2)
// directly with no transform).
def s1ld_imm8 : Operand<i32> {
  let DecoderMethod = "decodeSImmOperandXStepWide<8,0,/*IsSigned*/0>";
  let EncoderMethod =
      "getSImmOpValueXStepWide<8,0,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
}

// s1 LD sign-extended imm6 — fused post-inc element index (low 6 bits of
// imm8, sign-extended; high 2 bits ignored by decode, sign-filler on encode).
// Phase 3 Option A: DUAL-USE — the dedicated EncoderMethod
// `encodeS1LDPostImm6` sign-extends imm6 to the full 8-bit field so the
// high 2 bits carry sign-filler (matching the hand-rolled IsFusedPost path
// at HaydnMCCodeEmitter.cpp L659-661). The generic
// getSImmOpValueXStepWide<6,0,1,0,NONE> template CANNOT be reused here: it
// would zero the high 2 bits, breaking byte-equality with the hand-roller
// for negative (decrementing) strides.
def s1ld_imm6_sext : Operand<i32> {
  let DecoderMethod = "decodeSImmOperandXStepWide<6,0,/*IsSigned*/1>";
  let EncoderMethod = "encodeS1LDPostImm6";
}

//===----------------------------------------------------------------------===//
// Format classes — partition the s1 LD window (bits[45:24]) per §6.8.
//
// s1 slot window (22 bits @ bits[45:24]):
// bits[45:44] = FU (01 = LD)
// bits[43:40] = rs (base GPR32)
// bits[39:36] = rtd (data dest) / rsd2 (POST_REG stride GPR32)
// bits[35:28] = imm8 (imm-offset forms) — OR
// bits[35:32] = 0x1 + bits[31:28] = type (POST_REG form)
// bits[27:24] = opcode(4)
//
// The two classes differ in how bits[35:32] is bound: imm-offset leaves it
// as the high nibble of imm8 (variable, matched by the trie via opcode@27:24
// alone for the non-colliding opcodes); POST_REG fixes bits[35:32]=0x1 and
// bits[31:28]=type as fixed constants the trie matches to disambiguate the
// shared nibble 0x3 from the imm-offset forms.
//
// CRITICAL: the bits<N> field name MUST match the ins `$`-operand name (e.g.
// `scaled_imm`, `imm8`, `rs2`). A mismatch caused "No operand named X" in the
// prior attempt — the tablegen decoder emitter binds the named operand
// to the named field by name, not position.
//===----------------------------------------------------------------------===//

// FmtM0_S1_LD_IMM — imm-offset forms (plain LD64_S1/LD32_S1 + fused
// post-inc LD64_POST/LD32_POST/ST64_POST/ST32_POST/D_LDW_POST_IMM
// S_LW_POST_IMM). rs@43:40, rt@39:36, imm8@35:28 (full 8-bit variable)
// opcode@27:24.
// The full 8-bit imm8 is variable (no high-nibble constraint) — offsets
// 0-255 (or sign-extended imm6 element indices) all decode. The POST_REG
// collision (imm8 high nibble 0x1 + Type ∈ {0,1,2}) is resolved at the
// C++ routing layer (tryDecodeM0S1LDTable): bits[35:32]=0x1 AND
// bits[31:28]∈{0,1,2} routes to the POST_REG table; everything else
// routes here. This mirrors the hand-rolled decoder's fall-through logic
// (mode0-ld-imm-offset-decode.s + disassembler-movt32-postreg-regression.s
// both pin it: imm8 high nibble 0x1 with a RESERVED Type decodes as the
// imm-offset LD, not POST_REG).
class FmtM0_S1_LD_IMM<bits<4> opcode, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S1LD";
  bits<4> rs;
  bits<4> rt;
  bits<8> imm8;

  let Inst{63-46} = 0;    // s2 placeholder
  let Inst{45-44} = 0b01; // FU = 01 (LD)
  let Inst{43-40} = rs;
  let Inst{39-36} = rt;
  let Inst{35-28} = imm8; // full 8-bit imm (variable; POST_REG routed in C++)
  let Inst{27-24} = opcode;
  let Inst{23-4}  = 0;    // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

// FmtM0_S1_LD_POST_REG — register-offset post-increment (D_LDW_POST_REG
// D_LW_POST_REG / D_LHW_POST_REG). rs@43:40, rsd2@39:36, 0x1@35:32 (fixed)
// type@31:28 (fixed per width), rt@27:24.
// Lives in a SEPARATE DecoderNamespace "HaydnM0S1LDPostReg" so the tablegen
// trie can carry it without conflicting with the imm-offset forms (which
// share nibble 0x3 in the s1 LD opcode space). The C++ routing in
// tryDecodeM0S1LDTable gates entry to this table on the POST_REG signature
// (bits[35:32]=0x1 AND bits[31:28]∈{0,1,2}); all other bit patterns route
// to the HaydnM0S1LD imm-offset table.
class FmtM0_S1_LD_POST_REG<bits<4> type, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S1LDPostReg";
  bits<4> rs;
  bits<4> rs2;
  bits<4> rt;

  let Inst{63-46} = 0;    // s2 placeholder
  let Inst{45-44} = 0b01; // FU = 01 (LD)
  let Inst{43-40} = rs;
  let Inst{39-36} = rs2;  // rsd2 (stride GPR32)
  let Inst{35-32} = 0x1;  // POST_REG variant selector (fixed)
  let Inst{31-28} = type; // data-shape selector (fixed per variant)
  let Inst{27-24} = rt;
  let Inst{23-4}  = 0;    // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

//===----------------------------------------------------------------------===//
// Decode variants — bare mnemonic, isCodeGenOnly=0, Pattern=.
//
// POST_REG operand model (mirrors FmtLSPostIncReg.td):
// (outs DR64:$rt, GPR32:$rs_wb), (ins GPR32:$rs, GPR32:$rs2)
// let Constraints = "$rs = $rs_wb";
// MCInst order after decode: [rt, rs_wb, rs(tied), rs2] — tablegen keeps all
// four MC operands (the tied constraint is enforced at the regalloc/MIR
// level, not by dropping the MC operand). The decoder reconstructs rs_wb
// from the same rs@43:40 field as a tied def of $rs.
//
// Fused post-inc imm model (mirrors FmtLSPostInc.td):
// (outs <rtd>:$rt, GPR32:$rs_wb), (ins GPR32:$rs, simm6:$scaled_imm)
// let Constraints = "$rs = $rs_wb";
// MCInst order after decode: [rt, rs_wb, rs(tied), scaled_imm].
//
// Plain LD64_S1 / LD32_S1 model (mirrors FmtLS):
// (outs <rtd>:$rt), (ins GPR32:$rs, <imm>:$imm8)
// MCInst order after decode: [rt, rs, imm8].
//
// Opcode sharing (the disassembler's DB-canonical remap, §3): LD64_POST
// (0x2) and D_LDW_POST_IMM (0x2) share opcode 0x2; LD32_POST (0x3) and
// S_LW_POST_IMM (0x3) share opcode 0x3. The hand-rolled decoder remapped to
// the DB name (D_LDW_POST_IMM / S_LW_POST_IMM); the tablegen trie can only
// carry ONE def per (opcode, namespace), so the _M0S1LD variants use the
// DB-canonical names for the 0x2/0x3 slots (D_LDW_POST_IMM_M0S1LD
// S_LW_POST_IMM_M0S1LD) — matching the as-shipped disassembler behavior
// (mode0-ld-imm-offset-decode.s + d230-fused-postinc-store.s + the LD64_POST
// LD32_POST generic defs stay reachable via asm-match for encode).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// POST_REG — register-offset post-increment (3 defs).
//===----------------------------------------------------------------------===//
let mayLoad = 1, Constraints = "$rs = $rs_wb" in {
def D_LDW_POST_REG_M0S1LD : FmtM0_S1_LD_POST_REG<0x0,
    (outs DR64:$rt, GPR32:$rs_wb), (ins GPR32:$rs, GPR32:$rs2),
    "d_ldw_post_reg\t$rt, $rs, $rs2", []>;

def D_LW_POST_REG_M0S1LD : FmtM0_S1_LD_POST_REG<0x1,
    (outs DR64:$rt, GPR32:$rs_wb), (ins GPR32:$rs, GPR32:$rs2),
    "d_lw_post_reg\t$rt, $rs, $rs2", []>;

def D_LHW_POST_REG_M0S1LD : FmtM0_S1_LD_POST_REG<0x2,
    (outs DR64:$rt, GPR32:$rs_wb), (ins GPR32:$rs, GPR32:$rs2),
    "d_lhw_post_reg\t$rt, $rs, $rs2", []>;
} // mayLoad = 1, Constraints

//===----------------------------------------------------------------------===//
// Fused post-inc imm-offset (DB-canonical names for the 0x2/0x3 slots;
// ST64_POST/ST32_POST keep their codegen names — no DB canonical alias yet).
//===----------------------------------------------------------------------===//
let mayLoad = 1, Constraints = "$rs = $rs_wb" in {
def D_LDW_POST_IMM_M0S1LD : FmtM0_S1_LD_IMM<0x2,
    (outs DR64:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, s1ld_imm6_sext:$imm8),
    "d_ldw_post_imm\t$rt, $rs, $imm8", []>;

def S_LW_POST_IMM_M0S1LD : FmtM0_S1_LD_IMM<0x3,
    (outs GPR32:$rt, GPR32:$rs_wb),
    (ins GPR32:$rs, s1ld_imm6_sext:$imm8),
    "s_lw_post_imm\t$rt, $rs, $imm8", []>;
} // mayLoad = 1, Constraints

let mayStore = 1, Constraints = "$rs = $rs_wb" in {
def ST64_POST_M0S1LD : FmtM0_S1_LD_IMM<0x4,
    (outs GPR32:$rs_wb),
    (ins DR64:$rt, GPR32:$rs, s1ld_imm6_sext:$imm8),
    "st64.post\t$rt, $rs, $imm8", []>;

def ST32_POST_M0S1LD : FmtM0_S1_LD_IMM<0x5,
    (outs GPR32:$rs_wb),
    (ins GPR32:$rt, GPR32:$rs, s1ld_imm6_sext:$imm8),
    "st32.post\t$rt, $rs, $imm8", []>;
} // mayStore = 1, Constraints

//===----------------------------------------------------------------------===//
// Plain LD64_S1 / LD32_S1 — 3-operand (rtd, rs, imm8).
//
// LD64_S1_M0S1LD dest is DR64 (matching the generic LD64_S1 @ HaydnInstrInfo.td
// a 64-bit load writes a DR64). An earlier revision used GPR32:$rt here to
// match the as-shipped hand-rolled decoder (which reconstructed rt as GPR32 for
// the whole FU_LD family), but that was a decoder artifact: the finalizer's
// setDesc swaps this desc onto a DR64-def MI, so a GPR32 dest trips the
// verifier ("Illegal physical register") under -haydn-m0-slot-or. Aligned to
// DR64 — spec-correct (LD64 = 64-bit load) + encode-correct. Decode tests that
// pinned `ld64 rN` (GPR32) are updated to `ld64 dN` (DR64).
//===----------------------------------------------------------------------===//
let mayLoad = 1 in {
def LD64_S1_M0S1LD : FmtM0_S1_LD_IMM<0x0,
    (outs DR64:$rt), (ins GPR32:$rs, s1ld_imm8:$imm8),
    "ld64\t$rt, $rs, $imm8", []>;

def LD32_S1_M0S1LD : FmtM0_S1_LD_IMM<0x8,
    (outs GPR32:$rt), (ins GPR32:$rs, s1ld_imm8:$imm8),
    "ld32_s1\t$rt, $rs, $imm8", []>;
} // mayLoad = 1
