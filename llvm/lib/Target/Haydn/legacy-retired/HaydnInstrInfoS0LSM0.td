//===----------------------------------------------------------------------===//
//
// Decode-included s0 Load/Store variants (Mode 0).
//
// One <OP>_M0S0LS def per s0 LS width variant (10 total: LD32/ST32/LD64/ST64
// LD16/LDU16/LD8/LDU8/ST16/ST8). These are DECODE targets: when the Mode-0
// disassembler routes a bundle's s0 LS window, the tablegen decoder trie over
// DecoderNamespace "HaydnM0S0LS" resolves it to one of these defs, and the asm
// printer emits the bare generic mnemonic (no _m0s0ls suffix).
//
// Source of truth: encoding_manual.md §6.5 (lines 748-895). The s0 LS unit is
// class-partitioned: LD32/ST32/LD64/ST64 carry a full imm4 at ext[3:0] (scaled
// by data width <<2/<<3); LD16/LDU16/LD8/LDU8 share opcode 0x24 with ext[3:2]
// as a 2-bit type selector and ext[1:0] as imm2 (×1 byte); ST16/ST8 share
// opcode 0x34 with ext[3] as a 1-bit type selector and ext[2:0] as imm3 (×1).
//
// isCodeGenOnly is 0 (default — do NOT set isCodeGenOnly=1, or the decoder
// table won't generate); Pattern=; bare mnemonic; DecoderNamespace=
// "HaydnM0S0LS" isolates these from the DEFAULT asm-match + Haydn64 decode
// namespaces so they DECODE only (asm parse of "ld32 r,r,i" still matches the
// generic FmtLS LD32). Mirrors the proven MAC pattern (HaydnInstrInfoMACM0.td).
//
// This file is included by Haydn.td (so -gen-disassembler / -gen-asm-writer
// gen-instr-info / -gen-emitter see the variants) but NOT by HaydnAsmMatcher.td
// (so -gen-asm-matcher does NOT — the build-system split).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Scaled-immediate operand classes (reuse decodeSImmOperandXStepWide).
//
// Phase 3 Option A: DUAL-USE (encode + decode). Each Operand carries an
// EncoderMethod that is the bit-inverse of its DecoderMethod, so the same
// class drives the tablegen encoder (getBinaryCodeForInstr → Inst) and the
// tablegen decoder. The encoder reuses the WIDE-format template
// getSImmOpValueXStepWide<N,Shift,/*IsSigned*/0,/*IsPCRel*/0
// Haydn::FIXUP_HAYDN_NONE>: for an immediate it returns (Imm>>Shift)&maskN
// the exact inverse of the decoder's zero-extend<<Shift. Pure-immediate
// fields carry FIXUP_HAYDN_NONE so no symbolic fixup is pushed (frame-index
// reloc offsets stay on the generic op + hand-roller per the isMigratable
// concrete-immediate guard).
//
// The decoder reads the encoded imm bits, masks to N, zero-extends (these are
// unsigned byte offsets), and shifts left by `Shift` to recover byte units.
// Shift = 2 (LD32/ST32, W), 3 (LD64/ST64, DW), or 0 (S-LOAD/S-STORE, ×1).
//===----------------------------------------------------------------------===//

// s0 LS imm4 scaled by <<2 — LD32/ST32 (32-bit word offset).
// reloc-aware: FIXUP_HAYDN_S0LSOff4_2 emits a fixup for symbolic
// (frame-index/reloc) offsets so stack spills migrate to slot-OR (the
// isMigratable concrete-imm guard no longer rejects them).
def s0ls_off4_2 : Operand<i32> {
  let DecoderMethod = "decodeSImmOperandXStepWide<4,2,/*IsSigned*/0>";
  let EncoderMethod =
      "getSImmOpValueXStepWide<4,2,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_S0LSOff4_2>";
}

// s0 LS imm4 scaled by <<3 — LD64/ST64 (64-bit doubleword offset).
def s0ls_off4_3 : Operand<i32> {
  let DecoderMethod = "decodeSImmOperandXStepWide<4,3,/*IsSigned*/0>";
  let EncoderMethod =
      "getSImmOpValueXStepWide<4,3,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
}

// s0 LS imm2, unscaled — LD16/LDU16/LD8/LDU8 (S-LOAD shared opcode 0x24).
def s0ls_off2_0 : Operand<i32> {
  let DecoderMethod = "decodeSImmOperandXStepWide<2,0,/*IsSigned*/0>";
  let EncoderMethod =
      "getSImmOpValueXStepWide<2,0,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
}

// s0 LS imm3, unscaled — ST16/ST8 (S-STORE shared opcode 0x34).
def s0ls_off3_0 : Operand<i32> {
  let DecoderMethod = "decodeSImmOperandXStepWide<3,0,/*IsSigned*/0>";
  let EncoderMethod =
      "getSImmOpValueXStepWide<3,0,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
}

//===----------------------------------------------------------------------===//
// Page 1 narrow-store operands (full r0-r15 base + imm5).
//
// SUPERSEDES the imm6 layout (3-bit base r0-r7, imm6 split across
// ext[4:0]+rs[1]). (encoding_manual.md §6.5) reorganizes the page-1
// opcode space: the lane type selector (L/H) migrates INTO the 6-bit opcode
// freeing ext[4:0] for a clean imm5 plus giving the base register a FULL
// contiguous 4-bit field (r0-r15, no constraint — SP r13 works). The base
// register occupies the SAME bits[16:13] position as page 0 (FmtM0_S0_LS_*)
// so page-0 and page-1 LS share the identical base-register decode path.
//
// imm5 field map (encoding_manual.md §6.5):
// imm5[3:0] = ext[3:0] → Inst{7-4} (low 4 bits)
// imm5[4] = ext[4] → Inst{8} (high bit, ALWAYS 1: page selector)
// eff_opc = {ext[4], opcode[5:0]} (7-bit conceptual). Page 1 = ext[4]=1
// (eff_opc 0x40-0x7F); page 0 = ext[4]=0 (eff_opc 0x00-0x3F, the existing
// FmtM0_S0_LS_* family above).
//
// imm5 representable subset (DERIVED — §"imm5 representable subset"):
// ext[4] is BOTH the page-1 selector AND imm[4], so page-1 IMM variants can
// ONLY represent imm5 ∈ {16-31}. Byte ranges on page 1 IMM:
// D_SW_L/H : imm5<<2 → 64-124B
// D_SHW : imm5<<1 → 32-62B
// S_SHW : imm5<<1 → 32-62B
// S_SB : imm5<<0 → 16-31B
// The full 0-31 imm5 range is NOT representable on page 1 IMM (0-15 excluded).
// Smaller offsets use page 0 (imm4); larger/negative/symbolic offsets use
// page-1 REG-offset (s0ls_p1_reg, materialize in rs2) OR WIDE LS class 101
// (Fmt48_WideLS, ±512KB). The EncoderMethod `encodeLSPage1Imm5<Shift>`
// enforces imm5 ∈ {16-31} and rejects (compile-time error) offsets outside.
//
// Three Shift-tagged operand classes mirror the page-0 convention: Shift=0
// (S_SB, ×1), Shift=1 (S_SHW/D_SHW, <<1), Shift=2 (D_SW_L/D_SW_H, <<2).
//===----------------------------------------------------------------------===//

// Page-1 imm5 scaled by <<2 — D_SW_L / D_SW_H (32-bit word lane).
// Representable subset imm5 ∈ {16-31} → byte offsets {64-124}.
def s0ls_p1_off5_2 : Operand<i32> {
  let DecoderMethod = "decodeLSPage1Imm5<2>";
  let EncoderMethod = "encodeLSPage1Imm5<2>";
}

// Page-1 imm5 scaled by <<1 — D_SHW / S_SHW (16-bit halfword lane).
// Representable subset imm5 ∈ {16-31} → byte offsets {32-62}.
def s0ls_p1_off5_1 : Operand<i32> {
  let DecoderMethod = "decodeLSPage1Imm5<1>";
  let EncoderMethod = "encodeLSPage1Imm5<1>";
}

// Page-1 imm5 unscaled — S_SB (8-bit byte).
// Representable subset imm5 ∈ {16-31} → byte offsets {16-31}.
def s0ls_p1_off5_0 : Operand<i32> {
  let DecoderMethod = "decodeLSPage1Imm5<0>";
  let EncoderMethod = "encodeLSPage1Imm5<0>";
}

// Page-1 register-offset operand (ext[3:0] = rs2, the offset GPR). Used by
// D_SW/SHW/S_SW/SHW/SB _POST_REG / _PRE_REG variants when the immediate
// offset does not fit imm5 ∈ {16-31} (small imm5 0-15, negative, > ceiling
// or symbolic/reloc). The compiler materializes the byte offset into rs2
// (LOADI32 / LUI+ADDI32_W) before the load/store. The rs2 register occupies
// ext[3:0] (Inst{7-4}); ext[4] (Inst{8}) is STILL forced to 1 (page-1
// selector) — the page discriminator is independent of IMM vs REG variant.
def s0ls_p1_reg : Operand<i32> {
  let DecoderMethod = "decodeLSPage1Reg";
  let EncoderMethod = "encodeLSPage1Reg";
}

//===----------------------------------------------------------------------===//
// Format classes — partition the s0 LS ext field (bits[8:4]) per §6.5.
//
// s0 slot window (20 bits @ bits[23:4]):
// bit[23] = FU (1 = LS)
// bits[22:17]= opcode(6)
// bits[16:13]= rs (base GPR32)
// bits[12:9] = rt (data GPR32 or DR64)
// bits[8:4] = ext (5b: imm and/or type bits, class-partitioned)
//
// The three classes differ only in how the ext field is split into fixed type
// bits (matched by the tablegen trie to disambiguate the shared opcodes 0x24
// and 0x34) and a variable immediate field bound to the `$off` operand.
//
// CRITICAL: the bits<N> field name MUST be `off` (matching the ins operand
// `$off`). A mismatch (e.g. `imm4`) caused "No operand named imm4" in the
// prior attempt — the tablegen decoder emitter binds the named operand
// to the named field by name, not position.
//===----------------------------------------------------------------------===//

// FmtM0_S0_LS_IMM4 — LD32/ST32/LD64/ST64 (ext[4]=0, ext[3:0]=imm4).
// The 4-bit immediate lives at Inst{7-4}; Inst{8}=0 (ext[4] reserved clear).
class FmtM0_S0_LS_IMM4<bits<6> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0LS";
  bits<4> rs;
  bits<4> rt;
  bits<4> off;

  let Inst{63-24} = 0;    // s2 + s1 placeholder
  let Inst{23}    = 1;    // FU = 1 (LS)
  let Inst{22-17} = opcode;
  let Inst{16-13} = rs;
  let Inst{12-9}  = rt;
  let Inst{8}     = 0;    // ext[4] reserved (must be 0)
  let Inst{7-4}   = off;  // ext[3:0] = imm4 (scaled by data width)
  let Inst{3-0}   = 0b0011;
}

// FmtM0_S0_LS_SLD — S-LOAD shared opcode 0x24 (LD16/LDU16/LD8/LDU8).
// ext[4]=0; ext[3:2]=type (2-bit, fixed per variant); ext[1:0]=imm2.
class FmtM0_S0_LS_SLD<bits<2> type, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0LS";
  bits<4> rs;
  bits<4> rt;
  bits<2> off;

  let Inst{63-24} = 0;    // s2 + s1 placeholder
  let Inst{23}    = 1;    // FU = 1 (LS)
  let Inst{22-17} = 0x24; // shared S-LOAD opcode
  let Inst{16-13} = rs;
  let Inst{12-9}  = rt;
  let Inst{8}     = 0;    // ext[4] reserved (must be 0)
  let Inst{7-6}   = type; // ext[3:2] = S-LOAD type (fixed per variant)
  let Inst{5-4}   = off;  // ext[1:0] = imm2 (×1 byte)
  let Inst{3-0}   = 0b0011;
}

// FmtM0_S0_LS_SST — S-STORE shared opcode 0x34 (ST16/ST8).
// ext[4]=0; ext[3]=type (1-bit, fixed per variant); ext[2:0]=imm3.
class FmtM0_S0_LS_SST<bit type, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0LS";
  bits<4> rs;
  bits<4> rt;
  bits<3> off;

  let Inst{63-24} = 0;    // s2 + s1 placeholder
  let Inst{23}    = 1;    // FU = 1 (LS)
  let Inst{22-17} = 0x34; // shared S-STORE opcode
  let Inst{16-13} = rs;
  let Inst{12-9}  = rt;
  let Inst{8}     = 0;    // ext[4] reserved (must be 0)
  let Inst{7}     = type; // ext[3] = S-STORE type (fixed per variant)
  let Inst{6-4}   = off;  // ext[2:0] = imm3 (×1 byte)
  let Inst{3-0}   = 0b0011;
}

// FmtM0_S0_LS_REG — register-offset LS (encoding_manual.md §6.5).
// ext[4]=0; ext[3:0]=rs2 (the offset GPR). The rs2 register holds the full
// byte offset (any range, signed, relocatable). Used when the immediate
// offset form's imm4 scaled range cannot represent the offset (negative
// stack spills, large GEP offsets, symbolic relocs). The compiler
// materializes the offset into rs2 (LOADI32 / LUI+ADDI32_W) before the
// load/store.
// Opcodes per §6.5 class tables:
// D-LOAD class 00, opcode 0x08 = D_LDW_WITH_REG (LD64, DR64 dest)
// D-STORE class 01, opcode 0x18 = D_SDW_WITH_REG (ST64, DR64 data)
// S-LOAD class 10, opcode 0x27 = S_LW_WITH_REG (LD32, GPR32 dest)
// S-STORE class 11, opcode 0x37 = S_SW_WITH_REG (ST32, GPR32 data)
// The 4-bit rs2 field lives at Inst{7-4}; Inst{8}=0 (ext[4] reserved clear)
// mirroring FmtM0_S0_LS_IMM4 so the decoder-trie bit positions match.
class FmtM0_S0_LS_REG<bits<6> opcode, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0LS";
  bits<4> rs;
  bits<4> rt;
  bits<4> off;  // ext[3:0] = rs2 GPR (the offset register); named `off` to
                // match the `$off` operand (tablegen binds by name, not pos).

  let Inst{63-24} = 0;    // s2 + s1 placeholder
  let Inst{23}    = 1;    // FU = 1 (LS)
  let Inst{22-17} = opcode;
  let Inst{16-13} = rs;   // base GPR32
  let Inst{12-9}  = rt;   // data GPR32 or DR64
  let Inst{8}     = 0;    // ext[4] reserved (must be 0)
  let Inst{7-4}   = off;  // ext[3:0] = offset GPR32 (rs2)
  let Inst{3-0}   = 0b0011;
}

//===----------------------------------------------------------------------===//
// Page 1 narrow-store format (full r0-r15 base + imm5).
//
// SUPERSEDES the layout. encoding_manual.md §6.5 reorganizes the
// page-1 opcode space: lane type selector (L/H) migrates INTO the 6-bit
// opcode, freeing ext[4:0] for a clean imm5 and giving the base register a
// FULL contiguous 4-bit field (r0-r15, no constraint). The base register
// occupies the SAME bits[16:13] position as page 0 (FmtM0_S0_LS_*), so
// page-0 and page-1 LS share the identical base-register decode path.
//
// Page-1 field map (encoding_manual.md §6.5, 20-bit window bits[23:4]):
// bit[23] = FU (1 = LS) → Inst{23}
// bits[22:17]= opcode (6b, variant selector) → Inst{22-17}
// bits[16:13]= rs (4b base GPR32, r0-r15) → Inst{16-13} [FULL, contiguous]
// bits[12:9] = rt (4b data: DR64 for D-STORE, GPR32 for S-STORE) → Inst{12-9}
// bit[8] = ext[4] = imm[4] (ALWAYS 1: page-1 discriminator) → Inst{8}
// bits[7:4] = ext[3:0] = imm[3:0] (IMM variants) OR rs2 (REG variants)
// → Inst{7-4}
// bits[3:0] = 0011 (Mode-0 pattern) → Inst{3-0}
//
// imm5 = {ext[4], ext[3:0]} (5b) is reconstructed by the tablegen trie from
// the two contiguous Inst slices {8}, {7-4}. eff_opc[6:0] = {ext[4]
// opcode[5:0]} (7-bit conceptual): page 1 = eff_opc 0x40-0x7F (ext[4]=1).
//
// imm5 representable subset (§"imm5 representable subset"): because
// ext[4] is BOTH the page selector AND imm[4], page-1 IMM variants can ONLY
// represent imm5 ∈ {16-31}. Byte ranges on page 1 IMM:
// D_SW_L/H : imm5<<2 → 64-124B
// D_SHW : imm5<<1 → 32-62B
// S_SHW : imm5<<1 → 32-62B
// S_SB : imm5<<0 → 16-31B
// imm5 0-15 stays on page 0 (imm4); other offsets use page-1 REG (s0ls_p1_reg)
// or WIDE LS class 101 (Fmt48_WideLS, ±512KB). The EncoderMethod
// `encodeLSPage1Imm5<Shift>` enforces imm5 ∈ {16-31}.
//
// IMM vs REG variant routing: same opcode selects IMM or REG by inspecting
// the operand kind — ext[3:0] is imm[3:0] for IMM variants (the
// s0ls_p1_off5_<Shift> operand) and rs2 (the offset GPR) for REG variants
// (the s0ls_p1_reg operand). The format class itself is opcode-driven; the
// per-def `ins` dag picks IMM vs REG operand class.
//===----------------------------------------------------------------------===//
class FmtM0_S0_LS_Page1<bits<6> opcode, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S0LS";
  bits<4> rs;       // base GPR32 (r0-r15, FULL 4-bit field at bits[16:13])
  bits<4> rt;       // data register (DR64 for D-STORE, GPR32 for S-STORE)
  bits<5> imm5;     // page-1 imm5 (ext[4]=imm[4]=1 forced; ext[3:0]=imm[3:0])

  let Inst{63-24} = 0;        // s2 + s1 placeholder
  let Inst{23}    = 1;        // FU = 1 (LS)
  let Inst{22-17} = opcode;   // page-1 opcode (eff_opc[6]=1 via ext[4])
  let Inst{16-13} = rs;       // base GPR32 r0-r15 (FULL contiguous field)
  let Inst{12-9}  = rt;       // data register
  let Inst{8}     = imm5{4};  // ext[4] = imm[4] (ALSO page-1 selector = 1)
  let Inst{7-4}   = imm5{3-0}; // ext[3:0] = imm[3:0] (IMM) or rs2 (REG)
  let Inst{3-0}   = 0b0011;   // Mode-0 pattern
}

//===----------------------------------------------------------------------===//
// Decode variants — 10 defs, bare mnemonic, isCodeGenOnly=0, Pattern=.
//
// Operand order matches the generic FmtLS defs (HaydnInstrInfo.td:1125+):
// LOAD : (outs GPR32/DR64:$rt), (ins GPR32:$rs, <off>:$off)
// STORE: (outs), (ins GPR32/DR64:$rt, GPR32:$rs, <off>:$off)
// The asm string uses the bare generic mnemonic so objdump output matches the
// FmtLS instruction. mayLoad/mayStore are set to keep the MC model honest.
//===----------------------------------------------------------------------===//

let mayLoad = 1 in {
def LD32_M0S0LS : FmtM0_S0_LS_IMM4<0x21,
    (outs GPR32:$rt), (ins GPR32:$rs, s0ls_off4_2:$off),
    "ld32\t$rt, $rs, $off", []>;

def LD64_M0S0LS : FmtM0_S0_LS_IMM4<0x02,
    (outs DR64:$rt), (ins GPR32:$rs, s0ls_off4_3:$off),
    "ld64\t$rt, $rs, $off", []>;

def LD16_M0S0LS : FmtM0_S0_LS_SLD<0b00,
    (outs GPR32:$rt), (ins GPR32:$rs, s0ls_off2_0:$off),
    "ld16\t$rt, $rs, $off", []>;

def LDU16_M0S0LS : FmtM0_S0_LS_SLD<0b01,
    (outs GPR32:$rt), (ins GPR32:$rs, s0ls_off2_0:$off),
    "ldu16\t$rt, $rs, $off", []>;

def LD8_M0S0LS : FmtM0_S0_LS_SLD<0b10,
    (outs GPR32:$rt), (ins GPR32:$rs, s0ls_off2_0:$off),
    "ld8\t$rt, $rs, $off", []>;

def LDU8_M0S0LS : FmtM0_S0_LS_SLD<0b11,
    (outs GPR32:$rt), (ins GPR32:$rs, s0ls_off2_0:$off),
    "ldu8\t$rt, $rs, $off", []>;
} // mayLoad = 1

let mayStore = 1 in {
def ST32_M0S0LS : FmtM0_S0_LS_IMM4<0x31,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_off4_2:$off),
    "st32\t$rt, $rs, $off", []>;

def ST64_M0S0LS : FmtM0_S0_LS_IMM4<0x12,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_off4_3:$off),
    "st64\t$rt, $rs, $off", []>;

def ST16_M0S0LS : FmtM0_S0_LS_SST<0b0,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_off3_0:$off),
    "st16\t$rt, $rs, $off", []>;

def ST8_M0S0LS : FmtM0_S0_LS_SST<0b1,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_off3_0:$off),
    "st8\t$rt, $rs, $off", []>;
} // mayStore = 1

//===----------------------------------------------------------------------===//
// Register-offset s0 LS variants (encoding_manual.md §6.5 WITH_REG form).
//
// These are selected directly by the GISel selector / eliminateFrameIndex
// when the load/store offset does not fit the imm4 scaled range of the
// immediate-offset _M0S0LS variants above (LD32/ST32: [0,60] / 4-aligned;
// LD64/ST64: [0,120] / 8-aligned). The offset is materialized into a GPR
// (LOADI32 / LUI+ADDI32_W) before the load/store, and the LS instruction
// references that GPR via ext[3:0]=rs2.
//
// Eliminates the legacy Haydn32 LS emit path: out-of-range / negative
// reloc offsets no longer fall through encodeSingleInstruction -> emitWord32
// (bits[31:30]=01), so the scoped LS decoder probe (Gap 1
// tryDecodeLegacyLSProbe) becomes dead.
//
// Operand order matches the immediate-offset forms (rt, rs, off) so the
// asm string matches; the `off` operand here is a GPR32 register (rs2).
//===----------------------------------------------------------------------===//

let mayLoad = 1 in {
def LD32_REG_M0S0LS : FmtM0_S0_LS_REG<0x27,
    (outs GPR32:$rt), (ins GPR32:$rs, GPR32:$off),
    "ld32\t$rt, $rs, $off", []>;

def LD64_REG_M0S0LS : FmtM0_S0_LS_REG<0x08,
    (outs DR64:$rt), (ins GPR32:$rs, GPR32:$off),
    "ld64\t$rt, $rs, $off", []>;
} // mayLoad = 1

let mayStore = 1 in {
def ST32_REG_M0S0LS : FmtM0_S0_LS_REG<0x37,
    (outs), (ins GPR32:$rt, GPR32:$rs, GPR32:$off),
    "st32\t$rt, $rs, $off", []>;

def ST64_REG_M0S0LS : FmtM0_S0_LS_REG<0x18,
    (outs), (ins DR64:$rt, GPR32:$rs, GPR32:$off),
    "st64\t$rt, $rs, $off", []>;
} // mayStore = 1

//===----------------------------------------------------------------------===//
// Page 1 narrow-store variants (full r0-r15 base + imm5).
//
// SUPERSEDES the 5-def set (only WITH_IMM). adds POST/PRE
// (auto-increment AGU writeback) and REG-offset (rs2 holds byte offset)
// variants for both D-STORE (DR64 data) and S-STORE (GPR32 data) classes.
//
// These are DECODE targets (like the _M0S0LS family above): DecoderNamespace
// "HaydnM0S0LS" isolates them from DEFAULT asm-match so they do not collide
// with the generic FmtLaneStore / FmtLS asm-parse forms. isCodeGenOnly=0
// (default — the tablegen decoder trie must see them to resolve page-1
// bundles). Pattern=; the asm string uses the bare generic mnemonic so
// objdump output matches the legacy FmtLaneStore / FmtLS instruction.
//
// Base register: r0-r15 (GPR32, FULL). The wire encoding gives the
// base a contiguous 4-bit field at bits[16:13] (same position as page 0)
// SP (r13), FP (r14), LR (r15) all addressable directly. No GPR32Lo demotion.
//
// imm5 representable subset: page-1 IMM variants can ONLY represent imm5 ∈
// {16-31} (ext[4]=1 forced). The EncoderMethod enforces this; codegen
// selects these variants only for offsets in the representable range
// (HaydnLoadStoreOptimizer / HaydnSlotVariantFinalizer gate). Other offsets
// use the page-1 REG variants (rs2 = materialized byte offset) or WIDE LS
// class 101 (Fmt48_WideLS, ±512KB / reloc).
//
// Opcode mapping (eff_opc[6:0] = {ext[4]=1, opcode[5:0]}; opcode = eff_opc
// with bit 6 cleared):
// D-STORE class 01 (eff_opc 0x50-0x5F, opcode 0x10-0x1F):
// 0x50 D_SW_L_POST_IMM, 0x51 D_SW_H_POST_IMM, 0x52 D_SHW_POST_IMM
// 0x53 D_SW_L_WITH_IMM, 0x54 D_SW_H_WITH_IMM, 0x55 D_SHW_WITH_IMM
// 0x56 D_SW_L_PRE_IMM, 0x57 D_SW_H_PRE_IMM, 0x58 D_SHW_PRE_IMM
// 0x59 D_SW_L_POST_REG, 0x5A D_SW_H_POST_REG, 0x5B D_SHW_POST_REG
// 0x5C D_SW_L_PRE_REG, 0x5D D_SW_H_PRE_REG, 0x5E D_SHW_PRE_REG
// S-STORE class 11 (eff_opc 0x70-0x7F, opcode 0x30-0x3F):
// 0x70 S_SW_POST_IMM, 0x71 S_SW_WITH_IMM, 0x72 S_SW_PRE_IMM
// 0x73 S_SHW_POST_IMM, 0x74 S_SHW_WITH_IMM, 0x75 S_SHW_PRE_IMM
// 0x76 S_SB_POST_IMM, 0x77 S_SB_WITH_IMM, 0x78 S_SB_PRE_IMM
// 0x79 S_SW_POST_REG, 0x7A S_SHW_POST_REG, 0x7B S_SB_POST_REG
// (0x7C-0x7F reserved.)
//
// Operand order: (rt, rs, imm/off) matches the page-0 _M0S0LS convention
// and the generic FmtLS defs so the asm string is identical. rt is DR64 for
// D-STORE, GPR32 for S-STORE. For REG variants, the `off` operand is rs2
// (GPR32 — the offset register); named `off` to match the `$off` operand
// (tablegen binds by name, not position) — same convention as FmtM0_S0_LS_REG.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// D-STORE page-1 IMM variants (eff_opc 0x50-0x58). DR64 data register.
//===----------------------------------------------------------------------===//
let mayStore = 1 in {
def D_SW_L_POST_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x10,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "d_sw_l_post_imm\t$rt, $rs, $imm5", []>;

def D_SW_H_POST_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x11,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "d_sw_h_post_imm\t$rt, $rs, $imm5", []>;

def D_SHW_POST_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x12,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_1:$imm5),
    "d_shw_post_imm\t$rt, $rs, $imm5", []>;

def D_SW_L_WITH_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x13,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "d_sw_l_with_imm\t$rt, $rs, $imm5", []>;

def D_SW_H_WITH_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x14,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "d_sw_h_with_imm\t$rt, $rs, $imm5", []>;

def D_SHW_WITH_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x15,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_1:$imm5),
    "d_shw_with_imm\t$rt, $rs, $imm5", []>;

def D_SW_L_PRE_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x16,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "d_sw_l_pre_imm\t$rt, $rs, $imm5", []>;

def D_SW_H_PRE_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x17,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "d_sw_h_pre_imm\t$rt, $rs, $imm5", []>;

def D_SHW_PRE_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x18,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_off5_1:$imm5),
    "d_shw_pre_imm\t$rt, $rs, $imm5", []>;
} // mayStore = 1

//===----------------------------------------------------------------------===//
// D-STORE page-1 REG-offset variants (eff_opc 0x59-0x5E). ext[3:0]=rs2 GPR.
//===----------------------------------------------------------------------===//
let mayStore = 1 in {
def D_SW_L_POST_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x19,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "d_sw_l_post_reg\t$rt, $rs, $imm5", []>;

def D_SW_H_POST_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x1A,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "d_sw_h_post_reg\t$rt, $rs, $imm5", []>;

def D_SHW_POST_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x1B,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "d_shw_post_reg\t$rt, $rs, $imm5", []>;

def D_SW_L_PRE_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x1C,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "d_sw_l_pre_reg\t$rt, $rs, $imm5", []>;

def D_SW_H_PRE_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x1D,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "d_sw_h_pre_reg\t$rt, $rs, $imm5", []>;

def D_SHW_PRE_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x1E,
    (outs), (ins DR64:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "d_shw_pre_reg\t$rt, $rs, $imm5", []>;
} // mayStore = 1

//===----------------------------------------------------------------------===//
// S-STORE page-1 IMM variants (eff_opc 0x70-0x78). GPR32 data register.
//===----------------------------------------------------------------------===//
let mayStore = 1 in {
def S_SW_POST_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x30,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "s_sw_post_imm\t$rt, $rs, $imm5", []>;

def S_SW_WITH_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x31,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "s_sw_with_imm\t$rt, $rs, $imm5", []>;

def S_SW_PRE_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x32,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_2:$imm5),
    "s_sw_pre_imm\t$rt, $rs, $imm5", []>;

def S_SHW_POST_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x33,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_1:$imm5),
    "s_shw_post_imm\t$rt, $rs, $imm5", []>;

def S_SHW_WITH_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x34,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_1:$imm5),
    "s_shw_with_imm\t$rt, $rs, $imm5", []>;

def S_SHW_PRE_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x35,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_1:$imm5),
    "s_shw_pre_imm\t$rt, $rs, $imm5", []>;

def S_SB_POST_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x36,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_0:$imm5),
    "s_sb_post_imm\t$rt, $rs, $imm5", []>;

def S_SB_WITH_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x37,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_0:$imm5),
    "s_sb_with_imm\t$rt, $rs, $imm5", []>;

def S_SB_PRE_IMM_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x38,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_off5_0:$imm5),
    "s_sb_pre_imm\t$rt, $rs, $imm5", []>;
} // mayStore = 1

//===----------------------------------------------------------------------===//
// S-STORE page-1 REG-offset variants (eff_opc 0x79-0x7B). ext[3:0]=rs2 GPR.
//===----------------------------------------------------------------------===//
let mayStore = 1 in {
def S_SW_POST_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x39,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "s_sw_post_reg\t$rt, $rs, $imm5", []>;

def S_SHW_POST_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x3A,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "s_shw_post_reg\t$rt, $rs, $imm5", []>;

def S_SB_POST_REG_M0S0LS_P1 : FmtM0_S0_LS_Page1<0x3B,
    (outs), (ins GPR32:$rt, GPR32:$rs, s0ls_p1_reg:$imm5),
    "s_sb_post_reg\t$rt, $rs, $imm5", []>;
} // mayStore = 1
