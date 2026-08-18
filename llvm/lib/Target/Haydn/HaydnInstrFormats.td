//===-- HaydnInstrFormats.td - Haydn Instruction Formats ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Haydn instruction formats for the 3-issue VLIW DSP.
//
// LIVE encode path : Format E only — 12-byte parcel
// {s2[39:0], s1[39:0], s0[47:0]} via HaydnFormats*.td + HaydnCompositeFormats.td.
// These legacy format classes still back codegen *names* (selector MIR) and
// residual `_W` / G / C defs until Phase-5 M0 island delete. Width bits in
// TSFlags are NOT consulted by getInstSizeInBytes — sizes from committed
// FormatID → EncodedBytes / ProductFormatDesc.Bytes (AIE Format->getSize).
//
// TSFlags layout (historical; sizing ignores retired width tags):
// Bits [2:0] = VLIW slot mask (0b001=Slot0, 0b010=Slot1, 0b100=Slot2)
// Bits [4:3] = Encoded width tag (retired for emit sizing)
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Assembly operand matching classes (must precede Operand definitions)
//===----------------------------------------------------------------------===//

class SImmAsmOperand<int width> : AsmOperandClass {
  let Name = "SImm" # width;
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidSImm" # width;
}

class UImmAsmOperand<int width> : AsmOperandClass {
  let Name = "UImm" # width;
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidUImm" # width;
}

//===----------------------------------------------------------------------===//
// Immediate operand classes — AIE-style (peer: AIEBaseInstrInfo.td txxs/txxu)
//===----------------------------------------------------------------------===//
//
// Signed immediates ALWAYS bind a signed DecoderMethod. Tblgen's default
// decoder zero-extends the N-bit field, so LO residuals with bit{N-1} set
// print as unsigned > 2^{N-1}-1 and BundleSim's catalog rejects them
// (simm6, /simm16, /simm20). One class → no one-off misses.
//
// Encoder stays default getMachineOpValue (N-bit trunc); ImmLeaf still gates
// ImmediateCode is a function body — must `return`. Without it GISelEmitter
// emits isInt<N>(Imm); llvm_unreachable(...) and Pats abort at selection.
//
// assembly/isInt. Scaled / fixup-bearing WIDE operands below keep their own
// EncoderMethod+DecoderMethod pairs (immxstep-style).

// Signed N-bit immediate. Peer of AIE `txxs`.
class HaydnSImm<int n> : Operand<i32>,
                         ImmLeaf<i32, [{ return isInt<}] # n # [{>(Imm); }]> {
  let ParserMatchClass = SImmAsmOperand<n>;
  let DecoderMethod =
      "decodeSImmOperandXStepWide<" # n # ",0,/*IsSigned*/1>";
}

// Unsigned N-bit immediate. Peer of AIE `txxu` (zero-extend on decode).
class HaydnUImm<int n> : Operand<i32>,
                         ImmLeaf<i32, [{ return isUInt<}] # n # [{>(Imm); }]> {
  let ParserMatchClass = UImmAsmOperand<n>;
  let DecoderMethod =
      "decodeSImmOperandXStepWide<" # n # ",0,/*IsSigned*/0>";
}

//===----------------------------------------------------------------------===//
// Operand definitions
//===----------------------------------------------------------------------===//

def HaydnSlotMask : Operand<i32> {
  let PrintMethod = "printSlotMask";
}

// G-format / misc unsigned immediates
def uimm1  : HaydnUImm<1>;   // WIDE SET_HWLOOP hwlr_sel
def uimm2  : HaydnUImm<2>;   // G-format imm2; CBRI cbr_sel
def uimm4  : HaydnUImm<4>;
def uimm5  : HaydnUImm<5>;
def uimm6  : HaydnUImm<6>;
def uimm7  : HaydnUImm<7>;
def uimm8  : HaydnUImm<8>;
def uimm10 : HaydnUImm<10>;
def uimm12 : HaydnUImm<12>;
def uimm16 : HaydnUImm<16>;
def uimm20 : HaydnUImm<20>;  // ORI32/ANDI32/XORI32; LUI-style

// ALU / LS signed immediates (natural signed decode via HaydnSImm)
def simm6  : HaydnSImm<6>;   // RI6 element index / post-inc stride
def simm7  : HaydnSImm<7>;   // 16-bit MOVI residual
def simm8  : HaydnSImm<8>;   // LS CBRI imm8
def simm10 : HaydnSImm<10>;  // 16-bit branch residual
def simm12 : HaydnSImm<12>;  // RI12-class abs imms
def simm16 : HaydnSImm<16>;  // LD/ST offsets, legacy ADDI-class
def simm20 : HaydnSImm<20>;  // ALU RI20 (ADDI32/SUBI32/SET_HWLOOP)
// ADDI32_W uses simm20_wide_abs (EncoderMethod+signed decode) below.

def brtarget : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
}

def calltarget : Operand<OtherVT> {
  let EncoderMethod = "getCallTargetOpValue";
}

// JAL_S0: signed 20-bit PC-relative BYTE offset (no ÷2 scale).
// Same signed-decode invariant as HaydnSImm<20>; OtherVT so no ImmLeaf.
def calltarget_s0 : Operand<OtherVT> {
  let DecoderMethod = "decodeSImmOperandXStepWide<20,0,/*IsSigned*/1>";
}

// JALR_S0 RI12: signed 12-bit rs-relative BYTE offset (no ÷2).
def calltarget_s0_ri12 : Operand<OtherVT> {
  let DecoderMethod = "decodeSImmOperandXStepWide<12,0,/*IsSigned*/1>";
}

//===----------------------------------------------------------------------===//
// 48-bit WIDE scaled-immediate operand classes (Stage 1).
//
// AIE-style operands (mirrors AIEBaseInstrInfo.td:61-70 `immxstep`). Each class
// binds the operand to ONE templated C++ encoder/decoder pair that performs the
// exact same bit/scale/sign/fixup work as the corresponding hand-rolled
// `encode48Bit*` function in HaydnMCCodeEmitter.cpp, so a tablegen-driven
// `getBinaryCodeForInstr` call produces byte-for-byte identical output AND
// pushes the correct MCFixup for symbolic operands.
//
// Template parameters (see getSImmOpValueXStepWide / decodeSImmOperandXStepWide
// in HaydnMCCodeEmitter.cpp / HaydnDisassembler.cpp):
// N = encoded field width in bits
// Shift = right-shift applied to the immediate before masking (0 for
// GE96-03 branch/call byte PC+imm, ÷4 for SET_HWLOOP offsets).
// IsSigned = 1 => sign-extend the masked field on decode; 0 => zero-extend.
// IsPCRel = 1 => the symbolic fixup is PC-relative; 0 => absolute.
// FixupKind = Haydn::FIXUP_HAYDN_* kind pushed when the operand is symbolic
// or `fixup_none` when no fixup is ever emitted (pure-immediate
// fields like ADDI64 imm20, SLLI64 imm6, MAXI64 imm32, CSR uimm8
// hwloop cnt). `fixup_none` makes the encoder skip the fixup push.
//
// These classes are USED ONLY by the Fmt48_Wide* / Fmt48_MOVEI formats in
// HaydnInstrFormatsC.td. The shared 16/32-bit operands (`simm16`, `simm20`
// `uimm20`, `brtarget`, `calltarget`) are left untouched — their scale / sign
// fixup geometry differs from the 48-bit forms.
//
// The `Fixup` template argument is a `Haydn::Fixups` enum value. For
// pure-immediate fields that never emit a fixup (ADDI64 imm20, SLLI64 imm6
// MAXI64 imm32, CSR uimm8, hwloop cnt), pass `Haydn::FIXUP_HAYDN_NONE`: the
// encoder tests for NONE and skips the MCFixup::create call.
//===----------------------------------------------------------------------===//

// 48-bit MOVEI imm32 field. TableGen size-6 shell; product encode is Format E member. Full 32-bit immediate at
// bits[47:16]; signed (MOVEI_H sign-extends the high half). Symbolic targets
// emit FIXUP_HAYDN_32 (absolute, 4-byte field) — matches encode48Bit.
def simm32_movei
    : Operand<i32>, ImmLeaf<i32, [{ return isInt<32>(Imm); }]> {
  // No ParserMatchClass: a full 32-bit immediate accepts any int32, so the
  // generic Imm matcher (HaydnOperand::isImm) is correct. A width-constrained
  // SImmAsmOperand<32> would generate an HaydnOperand::isSImm32 predicate we
  // do not implement. Pre-S1 these used `i32imm` (same generic parse).
  let EncoderMethod =
      "getSImmOpValueXStepWide<32,0,/*IsSigned*/1,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_32>";
  let DecoderMethod = "decodeSImmOperandXStepWide<32,0,/*IsSigned*/1>";
}

// 48-bit WIDE GPR-Imm signed imm20 (ADDI32_W / SUBI32_W / ADDI32S_W /
// SUBI32S_W / ANDI32_W / XORI32_W). TableGen size-6 shell; product encode
// is Format E member. bits[47:28]; field
// is the masked 20-bit value. Symbolic targets emit FIXUP_HAYDN_LO20
// (absolute) — matches encode48BitGPR for the signed-ALU opcodes.
def simm20_wide_abs
    : Operand<i32>, ImmLeaf<i32, [{ return isInt<20>(Imm); }]> {
  let ParserMatchClass = SImmAsmOperand<20>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<20,0,/*IsSigned*/1,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_LO20>";
  let DecoderMethod = "decodeSImmOperandXStepWide<20,0,/*IsSigned*/1>";
}

// 48-bit WIDE GPR-Imm unsigned imm20 (LUI_W / ORI32_W). bits[47:28];
// zero-extended. Symbolic targets emit FIXUP_HAYDN_LO20 (absolute) — matches
// encode48BitGPR for the zero-extended opcodes.
def uimm20_wide_abs
    : Operand<i32>, ImmLeaf<i32, [{ return isUInt<20>(Imm); }]> {
  let ParserMatchClass = UImmAsmOperand<20>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<20,0,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_LO20>";
  let DecoderMethod = "decodeSImmOperandXStepWide<20,0,/*IsSigned*/0>";
}

// 48-bit WIDE Branch I20 (JAL_W). TableGen size-6 shell; product encode is Format E member. bits[47:28];
// JAL 20-bit PC-relative BYTE offset (GE96-03: field = PC+imm, no ÷2).
def brtarget_wide_i20 : Operand<OtherVT> {
  let EncoderMethod =
      "getSImmOpValueXStepWide<20,/*Shift*/0,/*IsSigned*/1,/*IsPCRel*/1,"
      "Haydn::FIXUP_HAYDN_WIDE_CallSImm20>";
  let DecoderMethod = "decodeSImmOperandXStepWide<20,0,/*IsSigned*/1>";
}

// WIDE Branch RI12 (BEQ_W..BLTU_W).
// HaydnFU_ALU32_S0_RI12: imm12 at s0 bits[19:8], byte PC+imm (GE96-03).
// Uses FIXUP_HAYDN_WIDE_BranchSImm12_RI (FieldLsb=8) — NOT the I12 kind
// (FieldLsb=4), which would clobber rt/rs on two-register branches.
def brtarget_wide_ri12 : Operand<OtherVT> {
  let EncoderMethod =
      "getSImmOpValueXStepWide<12,/*Shift*/0,/*IsSigned*/1,/*IsPCRel*/1,"
      "Haydn::FIXUP_HAYDN_WIDE_BranchSImm12_RI>";
  let DecoderMethod = "decodeSImmOperandXStepWide<12,0,/*IsSigned*/1>";
}

// 48-bit WIDE Branch I12 (BEQZ_W..BLTZ_W). TableGen size-6 shell; product encode is Format E member.
// bits[47:36]; byte PC+imm (GE96-03). Symbolic target emits
// FIXUP_HAYDN_WIDE_BranchSImm12 (PC-rel, TargetOffset=36).
def brtarget_wide_i12 : Operand<OtherVT> {
  let EncoderMethod =
      "getSImmOpValueXStepWide<12,/*Shift*/0,/*IsSigned*/1,/*IsPCRel*/1,"
      "Haydn::FIXUP_HAYDN_WIDE_BranchSImm12>";
  let DecoderMethod = "decodeSImmOperandXStepWide<12,0,/*IsSigned*/1>";
}

// 48-bit WIDE JALR_W target. TableGen size-6 shell; product encode is Format E member. bits[47:36].
// One-reg address (rs+imm). Bind WIDE_BranchSImm12, not the two-reg _RI
// kind used by BEQ/BNE. Not PC-relative: even-byte Align is a branch/JAL
// law (MinBundleAddressAlignBytes), not an rs-relative byte offset.
// Symbolic JALR still goes through findFixupFromFixupFields; RI12
// type-opcode 1 has no product reloc row and stays fail-closed.
def calltarget_wide_ri12 : Operand<OtherVT> {
  let EncoderMethod =
      "getSImmOpValueXStepWide<12,/*Shift*/0,/*IsSigned*/1,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_WIDE_BranchSImm12>";
  let DecoderMethod = "decodeSImmOperandXStepWide<12,0,/*IsSigned*/1>";
}

// 48-bit WIDE DR-Imm RI20 (ADDI64..SLTUI64). TableGen size-6 shell; product encode is Format E member.
// bits[47:28]; signed 20-bit; pure immediate — no symbolic fixup emitted.
def simm20_dr
    : Operand<i32>, ImmLeaf<i32, [{ return isInt<20>(Imm); }]> {
  let ParserMatchClass = SImmAsmOperand<20>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<20,0,/*IsSigned*/1,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
  let DecoderMethod = "decodeSImmOperandXStepWide<20,0,/*IsSigned*/1>";
}

// 48-bit WIDE DR-Imm RI6 shift amount (SLLI64/SRLI64/SRAI64, §5.6).
// bits[47:42]; unsigned 6-bit; pure immediate.
def uimm6_dr
    : Operand<i32>, ImmLeaf<i32, [{ return isUInt<6>(Imm); }]> {
  let ParserMatchClass = UImmAsmOperand<6>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<6,0,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
  let DecoderMethod = "decodeSImmOperandXStepWide<6,0,/*IsSigned*/0>";
}

// 48-bit WIDE DRX imm32 (MAXI64/MINI64, §5.7). bits[47:16]; signed full
// 32-bit; pure immediate.
def simm32_drx
    : Operand<i32>, ImmLeaf<i32, [{ return isInt<32>(Imm); }]> {
  // No ParserMatchClass: a full 32-bit immediate accepts any int32, so the
  // generic Imm matcher (HaydnOperand::isImm) is correct. A width-constrained
  // SImmAsmOperand<32> would generate an HaydnOperand::isSImm32 predicate we
  // do not implement. Pre-S1 these used `i32imm` (same generic parse).
  let EncoderMethod =
      "getSImmOpValueXStepWide<32,0,/*IsSigned*/1,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
  let DecoderMethod = "decodeSImmOperandXStepWide<32,0,/*IsSigned*/1>";
}

// 48-bit WIDE CSR uimm8 (CSRR_W/CSRW_W, §5.9/5.10). bits[47:40]; unsigned
// 8-bit; pure immediate.
def uimm8_csr
    : Operand<i32>, ImmLeaf<i32, [{ return isUInt<8>(Imm); }]> {
  let ParserMatchClass = UImmAsmOperand<8>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<8,0,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
  let DecoderMethod = "decodeSImmOperandXStepWide<8,0,/*IsSigned*/0>";
}

// 48-bit WIDE SET_HWLOOP count (§5.11). bits[47:32]; unsigned 16-bit; pure
// immediate.
def uimm16_hwloop_cnt
    : Operand<i32>, ImmLeaf<i32, [{ return isUInt<16>(Imm); }]> {
  let ParserMatchClass = UImmAsmOperand<16>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<16,0,/*IsSigned*/0,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_NONE>";
  let DecoderMethod = "decodeSImmOperandXStepWide<16,0,/*IsSigned*/0>";
}

// SET_HWLOOP offset1 (uimm6, ÷4). Format E product FieldLsb lives in
// HaydnRelocLayout (E2 e0 F2 abs bits[37:32]). Symbolic target emits
// FIXUP_HAYDN_HWLoopOff1 (PC-rel). Residual s0 EncoderMethod still uses this
// operand class; Format E members patch via getMachineOpValue + OpNo.
def hwloop_off1
    : Operand<i32>, ImmLeaf<i32, [{ return isUInt<6>(Imm); }]> {
  let ParserMatchClass = UImmAsmOperand<6>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<6,/*Shift*/2,/*IsSigned*/0,/*IsPCRel*/1,"
      "Haydn::FIXUP_HAYDN_HWLoopOff1>";
  let DecoderMethod = "decodeSImmOperandXStepWide<6,2,/*IsSigned*/0>";
}

// SET_HWLOOP offset2 (uimm12, ÷4). Format E product FieldLsb lives in
// HaydnRelocLayout (E2 e0 F2 abs bits[49:38]). Symbolic target emits
// FIXUP_HAYDN_HWLoopOff2 (PC-rel).
def hwloop_off2
    : Operand<i32>, ImmLeaf<i32, [{ return isUInt<12>(Imm); }]> {
  let ParserMatchClass = UImmAsmOperand<12>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<12,/*Shift*/2,/*IsSigned*/0,/*IsPCRel*/1,"
      "Haydn::FIXUP_HAYDN_HWLoopOff2>";
  let DecoderMethod = "decodeSImmOperandXStepWide<12,2,/*IsSigned*/0>";
}

// 48-bit WIDE LS signed byte offset. TableGen size-6 shell; product encode is Format E member.
// bits[47:28]; signed 20-bit BYTE offset (NOT scaled
// Shift=0; the field stores the raw byte value, so the assembler must
// validate natural-alignment per data type via validateInstruction). Range
// 524288..+524287 bytes.
// Fixup reuse: the WIDE LS imm20 field lives at the SAME bit position as the
// WIDE GPR-Imm imm20 (bits[47:28], §5.1 fixed-field map). The geometric
// layout is therefore byte-identical to FIXUP_HAYDN_LO20 (absolute 20-bit @
// bits[47:28]). Rather than mint a dedicated FIXUP_HAYDN_WIDE_LSOff20 kind
// which would force coordinated edits across HaydnAsmBackend (Names
// static_assert), HaydnRelocLayout (mapFixupKind + getRelocFieldInfo), and
// HaydnELFObjectWriter (owned by sibling agents) — this operand reuses the
// existing FIXUP_HAYDN_LO20. A dedicated kind can be added later as a pure
// rename across the reloc-layout siblings without changing any bytes.
// Product encode is Format E member (R_HAYDN_LS_ABS20 maps to the same field).
def simm20_ls
    : Operand<i32>, ImmLeaf<i32, [{ return isInt<20>(Imm); }]> {
  let ParserMatchClass = SImmAsmOperand<20>;
  let EncoderMethod =
      "getSImmOpValueXStepWide<20,/*Shift*/0,/*IsSigned*/1,/*IsPCRel*/0,"
      "Haydn::FIXUP_HAYDN_LO20>";
  let DecoderMethod = "decodeSImmOperandXStepWide<20,0,/*IsSigned*/1>";
}

// Memory operand types
def HaydnMemMode : Operand<iPTR> {
  let MIOperandInfo = (ops GPR32:$base, simm16:$offset);
  let PrintMethod = "printMemOperand";
}

def HaydnMemPostInc : Operand<iPTR> {
  let MIOperandInfo = (ops GPR32:$base, GPR32:$off);
  let PrintMethod = "printPostIncOperand";
}

//===----------------------------------------------------------------------===//
// Base instruction class
//===----------------------------------------------------------------------===//

class HaydnInst<int sz, dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  let Namespace = "Haydn";
  int Size = sz;
  let hasSideEffects = 0;  // Most instructions have no side effects

  // Widened to bits<64> per /bug #6 so D-class 64-bit bundle formats can
  // address bits[63:0] directly. 16/32/48-bit subclasses only populate the
  // low N bits and emit width-tagged parcels via MCCodeEmitter.
  field bits<64> Inst;

  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Itinerary = NoItinerary;

  // Default decoder namespace — 32-bit instructions.
  // Size-6 shells override this (Haydn48). Product encode is Format E member.
  let DecoderNamespace = "Haydn32";
}

class HaydnPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnInst<0, outs, ins, asmstr, pattern> {
  let isCodeGenOnly = 1;
  let isPseudo = 1;
  // Pseudo instructions inherit NoItinerary from HaydnInst — they are never
  // packetized and do not consume DFA functional units.
}

//===----------------------------------------------------------------------===//
// 32-bit encoding formats (width tag = 01)
//===----------------------------------------------------------------------===//

// R-type: [31:30]=01 | [29:22] opcode(8) | [21:18] rd | [17:14] rs1 | [13:10] rs2 | [9:0] funct
// ALU32 ops can execute in any slot (Slot 0, 1, or 2) — full ALU datapath.
// GPR port constraint (4R2W) limits practical dual-issue; enforced in packetizer.
class FmtALU32<bits<8> opcode, dag outs, dag ins, string asmstr,
               list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b111;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot012_ALU;

  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  // bits 9:0 reserved for funct/sub-opcode
}

// MULL family extended-ALU32 format (spec §6.5 s0 opcode 0x3F). The
// MULL family (MULL/MULSSH/MULSUH/MULUUH) all share opcode 0x3F; the sub
// variant is carried statically in the ext field at Inst bits[8:4]
// (ext[4:2]=variant 0/1/2/3, ext[1:0]=0). A dedicated class with a static
// `funct` template param is REQUIRED so tablegen's FixedLenDecoderEmitter can
// distinguish the four variants — otherwise they share identical Inst bits
// and the decoder table build aborts with "Decoding conflict encountered".
// (FmtALU32 leaves bits[9:0] unset, so all four would collide.)
//
// The Inst bits here are residual TableGen decoder isolation for the MULL
// family (opcode 0x3F would collide with FmtLS loads in a 32-bit table).
// Product encode is Format E generated members; these Inst bits are not the
// wire. funct is written to bits[8:4] so the static decoder mask can
// distinguish variants.
class FmtALU32Ext<bits<8> opcode, bits<5> funct, dag outs, dag ins,
                  string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b111;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot012_ALU;
  // Isolate the MULL family (FmtALU32Ext) from the default 32-bit decode
  // table: opcode 0x3F would otherwise shadow FmtLS loads (e.g. ldu8 decoded
  // as mull). Product decode is Format E entry tables only.
  let DecoderNamespace = "HaydnM0Only";

  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{8 - 4} = funct;   // ext[4:2]=variant (static, distinguishes variants)
  let Inst{9} = 0;           // ext bit 9 unused by §6.5
}


// Unary R-type: [31:30]=01 | [29:22] opcode(8) | [21:18] rd | [17:14] rs1 | [13:10] rs2=0 | [9:0] funct
// For unary operations (NEG, NOT, etc.) where rs2 is unused/encoded as 0
class FmtALU32Unary<bits<8> opcode, dag outs, dag ins, string asmstr,
                    list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b111;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot012_ALU;

  bits<4> rd;
  bits<4> rs1;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = 0;  // rs2 is unused for unary ops, encoded as 0
  // bits 9:0 reserved for funct/sub-opcode
}

// I-type: [31:30]=01 | [29:24] opcode(6) | [23:20] rd | [19:16] rs1 | [15:0] simm16
class FmtI<bits<6> opcode, dag outs, dag ins, string asmstr,
           list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b111;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot012_ALU;

  bits<4> rd;
  bits<4> rs;
  bits<16> imm;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 24} = opcode;
  let Inst{23 - 20} = rd;
  let Inst{19 - 16} = rs;
  let Inst{15 - 0} = imm;
}

// Load/Store format (Slot 0): R-type with memory operand
class FmtLS<bits<8> opcode, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let mayLoad = 1;   // load default; stores MUST override mayLoad = 0
  let Itinerary = Slot0_LS;

  bits<4> rt;
  bits<4> rs;
  bits<10> offset;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rt;
  let Inst{17 - 14} = rs;
  let Inst{13 - 4} = offset;
}

// Load/Store with base-register post/pre-increment (AGU writeback bypass).
//
// fused post-increment loads (spec D_LDW_POST_IMM / S_LW_POST_IMM).
// Single instruction equivalent of HiFi's `ae_l64.ip` / `ae_l32.ip`: load from
// [rs], then rs += (imm6 << ScaleShift). The base writeback rides the AGU
// dedicated write wordline — consumes one LS slot
// no GPR-compute write port. This matches the existing LD64_S1 / LD32_S1 slot-1
// LD layout (s1 FU=LD, 4-bit opcode space, 8-bit immediate).
//
// Two-def form mirroring the FmtALU64Acc tied-def idiom : $rt (loaded
// data, DR64 for D_LDW_POST / GPR32 for S_LW_POST) AND $rs_wb (the base
// register, written back via AGU). The tied-def constraint
// `let Constraints = "$rs = $rs_wb"` (set per-instruction) forces regalloc to
// coalesce the input base and the writeback, so the encoded rs field carries
// both at silicon time. The MC layer encodes $rs into Inst{17-14}; the tied
// input $rs_wb is reconstructed by the decoder from the same field.
//
// The immediate operand ($scaled_imm) is the *element index* (imm6), not the
// byte stride — width-specific scaling is applied in HaydnExpandPostIncEarly
// before the instruction is emitted. Encode/decode keep the raw imm6 in
// bits[13-8]; the MC code-emitter / disassembler sign-extend it from 6 bits.
//
// This class is encoding-agnostic on the wire except for the field widths
// the `Inst{...}` bit assignments match the existing FmtLS layout (rt@21:18
// rs@17:14). Product encode is Format E LD-unit members.
class FmtLSPostInc<bits<8> opcode, dag outs, dag ins, string asmstr,
                   list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b010;   // Slot 1 only (FU=LD, matches LD64_S1)
  let TSFlags{4-3} = 0b01;    // 32-bit width
  let Itinerary = Slot1_LD;   // Same itinerary as LD64_S1 / LD32_S1

  bits<4> rt;        // data destination (rtd/rt; DR64 or GPR32 per def)
  bits<4> rs;        // base register (encoded once; tied to $rs_wb)
  bits<6> scaled_imm;// imm6 element-index stride (sign-extended at encode time)

  let Inst{31, 30} = 0b01;       // 32-bit width tag
  let Inst{29 - 22} = opcode;    // 8-bit opcode (LS family)
  let Inst{21 - 18} = rt;
  let Inst{17 - 14} = rs;
  let Inst{13 - 8}  = scaled_imm;
  // bits[7-2] reserved for funct/sub-mode (POST/PRE/WITH) — M5 wave assigns.
  // bits[1-0] = 01: 32-bit width tag for the parallel decode tree (§1.2) so
  // the disassembler routes this 4-byte parcel through the 32-bit branch
  // not the 16-bit one. Without this the all-zero default mis-routes the
  // parcel as 16-bit (bit[0]=0) and the decoder renders `<unknown>`.
  let Inst{1, 0}    = 0b01;
}

// Load/Store with base-register post/pre-increment by REGISTER stride.
// Sibling of FmtLSPostInc for the D_LDW_POST_REG / D_L*_POST_REG family
// (s1 LD register-offset format). The base
// writeback rides the AGU port exactly as the imm-offset form; the only
// difference is the stride source (GPR32 rs2 vs simm6 scaled_imm).
//
// Two-def form mirroring FmtLSPostInc: $rt (loaded data, DR64) AND $rs_wb
// (base register, written back via AGU). The tied-def constraint
// `let Constraints = "$rs = $rs_wb"` (set per-instruction) forces regalloc
// to coalesce the input base and the writeback. Product encode is Format E
// LD register-offset members.
//
// `type` (4 bits) is the data-shape selector.
// allocates: 0x0=DW (D_LDW_POST_REG), 0x1=W (D_LW_POST_REG)
// 0x2=HW (D_LHW_POST_REG). Standalone opcodes per width: 0x4F / 0x4E
// 0x4D — all free in the LS family (existing range 0x40-0x4C). The
// standalone 32-bit form has no `type` field — the standalone opcode
// encodes width directly. Product Format E members carry `type` in the
// generated LD entry window.
class FmtLSPostIncReg<bits<8> opcode, bits<4> type, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b010;   // Slot 1 only (FU=LD, matches LD64_S1)
  let TSFlags{4-3} = 0b01;    // 32-bit width
  let Itinerary = Slot1_LD;   // Same itinerary as LD64_S1 / LD32_S1

  bits<4> rt;        // data destination (rtd; DR64)
  bits<4> rs;        // base register (encoded once; tied to $rs_wb)
  bits<4> rs2;       // stride register (GPR32; rs += rs2 after load)
  bits<4> Type = type;        // bundle-only field; not stored in standalone Inst

  let Inst{31, 30} = 0b01;       // 32-bit width tag
  let Inst{29 - 22} = opcode;    // 8-bit opcode (LS family)
  let Inst{21 - 18} = rt;
  let Inst{17 - 14} = rs;
  let Inst{13 - 10} = rs2;
  // bits[9-2] reserved for funct/sub-mode (POST/PRE/WITH).
  // bits[1-0] = 01: 32-bit width tag for the parallel decode tree (§1.2)
  // (see FmtLSPostInc for the mis-routing rationale).
  let Inst{1, 0}    = 0b01;
}

// Lane store format (Slot 0 Load/Store): store one 32-bit lane of a
// DR64 register to memory. Models the ISA DB instructions:
// D_SW_L_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[31:00] (low lane)
// D_SW_H_WITH_IMM: mem32[rs + (imm6<<2)] = rtd[63:32] (high lane)
// Layout: [31:30]=01 | [29:22] opcode(8) | [21:18] rtd(4,DR64)
// [17:14] rs(4,GPR32 base) | [13:8] scaled_imm(6) | [7:0] reserved
// The imm6 is word-scaled (byte offset = imm6 << 2), sign-extended.
//
// Product encode is Format E generated members. Historical page-1 / Mode-0
// encoder and decoder helpers are deleted.
class FmtLaneStore<bits<8> opcode, dag outs, dag ins, string asmstr,
                   list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b001;   // Slot 0 (Load/Store unit)
  let TSFlags{4-3} = 0b01;    // 32-bit width
  let mayStore = 1;
  let mayLoad = 0;   // pure lane store; clear FmtLS mayLoad=1 default
  let Itinerary = Slot0_LS;

  bits<4> rtd;       // DR64 data source (the register whose lane is stored)
  bits<4> rs;        // GPR32 base register
  bits<6> scaled_imm;// imm6 word-index offset (sign-extended at encode time)

  let Inst{31, 30} = 0b01;       // 32-bit width tag
  let Inst{29 - 22} = opcode;    // 8-bit opcode (LS family)
  let Inst{21 - 18} = rtd;       // DR64 data register
  let Inst{17 - 14} = rs;        // GPR32 base register
  let Inst{13 - 8}  = scaled_imm;// imm6 word-index offset
  // bits[7-2] reserved for funct/sub-mode.
  // bits[1-0] = 01: 32-bit width tag for the parallel decode tree (§1.2)
  // (see FmtLSPostInc for the mis-routing rationale).
  let Inst{1, 0}    = 0b01;
}

// ALU64 format (Slot 1/2): wider register operands for DR64.
//
// Single-destination form — the bulk of ALU64 defs (rd/rs1/rs2 + 10-bit funct).
// 2-destination SIMD-MAC multiply defs (X2MUL32, X4MUL16) use FmtALU64_2Dest
// instead (Path B — see below).
class FmtALU64<bits<8> opcode, bits<10> funct, dag outs, dag ins,
               string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_ALU;

  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 0} = funct;
}

// ALU64 2-destination format (Slot 1 only) — (Path B) golden-faithful.
//
// Used by the 2-dest SIMD-MAC multiply defs X2MUL32 (opcode 0x5A) and X4MUL16
// (opcode 0x5C). The legacy dag binds (outs rd, rtd2) (ins rs1, rs2) so that
// post-RA materializeMultiOpcodeInstrs setDesc(legacy -> _S<k> member)
// preserves both the operand count (4) and the def count (2). Both defs have
// funct=0x000, so the funct field is carved to 6 bits and the freed bits{9-6}
// carry the 2nd dest `rtd2` — the same legacy-only device used by FmtMAC2Dest
// (the Haydn32 decoder table is dead; encode is Desc-as-is after setDesc).
// See FmtMAC2Dest's header comment for the full rationale.
class FmtALU64_2Dest<bits<8> opcode, bits<6> funct, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_ALU;

  bits<4> rd;       // destination 1 (DR64, encoded @ Inst{21-18})
  bits<4> rtd2;     // destination 2 (DR64, encoded @ Inst{9-6}, legacy-only)
  bits<4> rs1;      // source 1 (DR64, encoded @ Inst{17-14})
  bits<4> rs2;      // source 2 (DR64, encoded @ Inst{13-10})

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 6} = rtd2;   // 2nd dest (legacy-only; slot rewrites before emit)
  let Inst{5 - 0} = funct;
}

// ALU64 accumulator format (Slot 1/2): two DR64 sources + tied accumulator
// destination. Same encoding layout as FmtALU64 — the accumulator input is
// modeled as a tied-def operand `$rd_in` that shares the destination's encoded
// bit field. Silicon reads rtd from the encoded destination field
// (slot-1 DR_Read_Port = [rsd1, rsd2, rtd]); the tied-def constraint
// (`let Constraints = "$rd = $rd_in"` set at the per-instruction def) forces
// regalloc to coalesce Acc and DstReg to the same physical register, so the
// encoded destination field holds the accumulator value at silicon time.
//
// This format avoids `DisableEncoding = "$rd_in"` (the standard LLVM idiom but
// build-breaking in this version — see). Instead the tied input is encoded
// as a duplicate of the destination field: the MIR constraint guarantees
// rd_in's encoded value equals rd's encoded value, so the duplication is
// harmless. The MC tablegen encoder writes rd_in's value into Inst{21-18}
// (same field as rd), and the MC tablegen decoder reconstructs both operands
// from the same field. Functionally equivalent to DisableEncoding when the
// tied-def constraint holds.
//
// Used by every spec accumulator-form MAC selected via `selectAccMAC`:
// MULA64_*/MULS64_*/MULAS64_*/MULSS64_*, FMULA32S_*, FMULS32S_*, FMULAA16_*
// FMULSS16_*, F2MULAA32RS_*, F2MULSS32RS_*, FF2MULA32RS_*, X2FCMULA32RS*.
// (See, reviews/m6-codex-destructive-acc-lanemap.md §1A.)
// Acc-MAC (FF2MULA32RS_*, MULA64_*, F2MULAA_*, …): Slot12_MAC_AccFirst so
// SMS/list see OperandCycles [rd=2, ra=2, rs1=1, rs2=1] matching ins order
// (rd_in, rs1, rs2) — acc→acc RecMII=1 and result writeback=2. Slot12_ALU
// ([1]) treated every FF2MULA like plain ALU (bkfir P3). Same
// InstrStage<1,[SLOT1,SLOT2]> resource as Slot12_ALU (no ResMII inflate).
class FmtALU64Acc<bits<8> opcode, bits<10> funct, dag outs, dag ins,
                  string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_MAC_AccFirst;

  bits<4> rd;
  bits<4> rd_in;  // accumulator input (tied to destination, same encoded field)
  bits<4> rs1;
  bits<4> rs2;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 0} = funct;
}

// ALU64 unary format (Slot 1/2): one DR64 source.
// Layout: [31:30]=01 | [29:22] opcode(8) | [21:18] rd | [17:14] rs1
// [13:10] rs2=0 | [9:0] funct
// Used for unary DR64 ops: SLT64, SLE64, SEQ64, MOVT64, MOVF64, X2ABS32S.
class FmtALU64Unary<bits<8> opcode, bits<10> funct, dag outs, dag ins,
                    string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_ALU;

  bits<4> rd;
  bits<4> rs1;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = 0;  // rs2 unused for unary, encoded as 0
  let Inst{9 - 0} = funct;
}

// ALU64 binary format (Slot 1/2): two DR64 sources.
// Same layout as FmtALU64 but exposed as a distinct class so the.td defs
// can document the operand-arity contract. Used for X2SEQ32, X2SLT32
// X2SLE32, X2MOVF32, X2MOVT32, X4SEQ16, X4SLT16, X4SLE16, X4MOVF16, X4MOVT16.
class FmtALU64Binary<bits<8> opcode, bits<10> funct, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_ALU;

  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 0} = funct;
}

// ALU64 select-immediate format (Slot 1/2): two DR64 sources + 4-bit imm.
// Layout: [31:30]=01 | [29:22] opcode(8) | [21:18] rd | [17:14] rs1
// [13:10] rs2 | [9:4] funct6 | [3:0] imm4
// Used for X4SELI16 (per-lane 16-bit select with 4-bit lane mask).
class FmtALU64SelImm<bits<8> opcode, bits<6> funct, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_ALU;

  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;
  bits<4> imm;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 4} = funct;
  let Inst{3 - 0} = imm;
}

// ALU64 shift-immediate format (Slot 1/2): one DR64 source + a constant shift
// amount (shamt). Format E immediate routing by slot — the s1
// ALU64 12-bit opcode field's low N bits carry the shamt; the high bits are
// op-select. In the standalone 32-bit parcel the shamt lives in funct[4:0].
//
// Layout: [31:30]=01 | [29:22] opcode(8) | [21:18] rd | [17:14] rs1
// [13:10]=0 | [9:5]=0 | [4:0] shamt(5)
// X2* (dual 32-bit lanes) use the full 5-bit shamt (0..31);
// X4* (quad 16-bit lanes) use only shamt[3:0] (0..15), bit 4 stays 0.
//
// Standalone-decode note: tablegen auto-generates a DecoderTableHaydn3232
// entry from these Inst bits. Each instruction's opcode byte (Inst[29:22])
// is distinct (0x69..0x70), so there is no decode-table collision. The bundle
// path (s1 ALU64 12-bit opcode) folds shamt into the opcode low bits — handled
// by the MCCodeEmitter/Disassembler
// shift-imm special cases.
class FmtALU64ShiftImm<bits<8> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b110;   // slots 1+2
  let TSFlags{4-3} = 0b01;    // 32-bit width
  let Itinerary = Slot12_ALU;

  bits<4> rd;
  bits<4> rs1;
  bits<5> shamt;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = 0;
  let Inst{9 - 5} = 0;
  let Inst{4 - 0} = shamt;
}

// MAC format (Slot 1/2)
// Accumulator-form by default: rd = rs1 * rs2 + ra, reading the accumulator
// input `ra`. Uses Slot12_MAC so the MAC unit's internal acc-feedback path
// (1 cycle) and 2-cycle regfile writeback are modeled. Multiply-form MACs
// (MULQ31, MULQ63) override `Itinerary = Slot12_ALU` at their defs since they
// do not read `ra`. See D2XX + HaydnSchedule.td.
class FmtMAC<bits<8> opcode, bits<6> funct, dag outs, dag ins,
             string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b110;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_MAC;

  bits<4> rd;
  bits<4> rs1;
  bits<4> rs2;
  bits<4> ra;  // accumulator input

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 6} = ra;
  let Inst{5 - 0} = funct;
}

// MAC 2-destination format (Slot 1 only) — (Path B) golden-faithful.
//
// Mirrors the slot HaydnFU_MAC_*_D_RR2 / D_RRA2 contract: every dag operand
// binds 1:1 to a declared bits field (no orphans). The legacy dag carries the
// TRUE 2-destination shape (golden DR_Write_Port=[rtd1,rtd2]) so that
// post-RA materializeMultiOpcodeInstrs setDesc(legacy -> _S<k> member)
// preserves both the operand count and the def count.
//
// Encoded fields (4): rd, rtd2, rs1, rs2 — rd/rs1/rs2 sit in the same Inst bit
// positions as FmtMAC. The 2nd dest `rtd2` reuses FmtMAC's old `ra` slot
// (bits{9-6}). Post-RA setDesc commits every legacy 2-dest def to its _S<k>
// member (real 40-bit slot encoding) BEFORE emission; encode is Desc-as-is.
// The legacy Haydn32 decoder table is generated but NO LONGER CONSULTED by the
// disassembler (deleted the last consult — HaydnDisassembler.cpp:2206
// "The legacy DecoderTableHaydn3232 trie... is NO LONGER CONSULTED"). So the
// rtd2↔bits{9-6} binding is a tablegen-consistency device, not a runtime
// contract: it satisfies -gen-emitter (every Inst-bound field has a dag
// operand) and -gen-disassembler (every dag operand has an Inst-bound field)
// without affecting any live decode path.
//
// Declared-but-unencoded passthrough fields (2): ra and ra2 (accumulator
// inputs tied to rd/rtd2 via Constraints in the accum defs). These mirror the
// proven FmtALU64Acc `rd_in` pattern (Option 2): declared bits fields
// NOT placed in any `let Inst{...}` concat, resolved by the tablegen tied
// operand mechanism (input tied to an encoded output). Non-accum defs simply
// don't name $ra/$ra2 in their dag.
//
// Non-accum defs (X2CMUL32 family, X4FF2MUL16S): (outs rd,rtd2) (ins rs1,rs2).
// Accum defs (X2MULA32 family, X4MULA16 family, X4FF2MULA16S, X4MULS16 family):
// (outs rd,rtd2) (ins ra,ra2,rs1,rs2) with `let Constraints = "$rd=$ra
// $rtd2=$ra2"`. The accum inputs are tied to the encoded dests.
class FmtMAC2Dest<bits<8> opcode, bits<6> funct, dag outs, dag ins,
                  string asmstr, list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b010;   // SLOT1-only (overridable per-def)
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot12_MAC;

  bits<4> rd;       // destination 1 (DR64, encoded @ Inst{21-18})
  bits<4> rtd2;     // destination 2 (DR64, encoded @ Inst{9-6}, legacy-only)
  bits<4> rs1;      // source 1 (DR64, encoded @ Inst{17-14})
  bits<4> rs2;      // source 2 (DR64, encoded @ Inst{13-10})
  bits<4> ra;       // accumulator input 1 (DR64, tied to rd, unencoded)
  bits<4> ra2;      // accumulator input 2 (DR64, tied to rtd2, unencoded)

  let Inst{31, 30} = 0b01;
  let Inst{29 - 22} = opcode;
  let Inst{21 - 18} = rd;
  let Inst{17 - 14} = rs1;
  let Inst{13 - 10} = rs2;
  let Inst{9 - 6} = rtd2;   // 2nd dest (legacy-only; slot rewrites before emit)
  let Inst{5 - 0} = funct;
}

// Branch format (unconditional branch: BEQZ R0, offset)
// Offset field at bits [15:0] to match FIXUP_HAYDN_BranchSImm16 fixup position.
class FmtBR<bits<6> opcode, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let isBranch = 1;
  let Itinerary = Slot0_ALU;

  bits<4> rs;
  bits<16> offset;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 24} = opcode;
  let Inst{23 - 20} = rs;
  let Inst{19 - 16} = 0;  // reserved
  let Inst{15 - 0} = offset;
}

// Conditional branch format (two register operands)
class FmtBRCond<bits<6> opcode, dag outs, dag ins, string asmstr,
                 list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let isBranch = 1;
  let Itinerary = Slot0_ALU;

  bits<4> rs1;
  bits<4> rs2;
  bits<16> offset;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 24} = opcode;
  let Inst{23 - 20} = rs1;
  let Inst{19 - 16} = rs2;
  let Inst{15 - 0} = offset;
}

// Jump format (direct) — logical JAL shell (FmtJ). Product encode is
// Format E members; GE96-03 is byte `PC + imm` (ValueShift=0). Do not
// treat this class's leftover Haydn32 bit diagram as golden scale.
//
// JAL uses getCallTargetOpValue. The retired linear layout (not Format E)
// was Inst[31:30]=01, Inst[29:24]=opcode 0x37, Inst[23:20]=rd, Inst[19:0]
// target. That field is a signed byte displacement, not offset>>1.
class FmtJ<bits<6> opcode, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let isCall = 1;
  let Itinerary = Slot0_ALU;

  bits<4> rd;
  bits<20> target;

  let Inst{31-30} = 0b01;
  let Inst{29-24} = opcode;
  let Inst{23-20} = rd;
  let Inst{19-0}  = target;
}

// Jump register format (indirect)
class FmtJR<bits<6> opcode, dag outs, dag ins, string asmstr,
             list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let isCall = 1;
  let Itinerary = Slot0_ALU;

  bits<4> rd;
  bits<4> rs;
  bits<16> target;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 24} = opcode;
  let Inst{23 - 20} = rd;
  let Inst{19 - 16} = rs;
  let Inst{15 - 0} = target;
}

// CSR format
class FmtCSR<bits<6> opcode, dag outs, dag ins, string asmstr,
             list<dag> pattern>
    : HaydnInst<4, outs, ins, asmstr, pattern> {
  let TSFlags{2-0} = 0b001;
  let TSFlags{4-3} = 0b01;
  let Itinerary = Slot0_ALU;

  bits<4> rd;
  bits<8> csr_addr;
  bits<4> rs;

  let Inst{31, 30} = 0b01;
  let Inst{29 - 24} = opcode;
  let Inst{23 - 20} = rd;
  let Inst{19 - 16} = 0;  // reserved for read-only CSR
  let Inst{15 - 8} = csr_addr;
  let Inst{7 - 4} = rs;
  let Inst{3 - 0} = 0;  // reserved
}

//===----------------------------------------------------------------------===//
// FmtG_S0_ALU32 (32-bit G-format s0 sub-slot) RETIRED.
// All *_G instruction shells deleted from HaydnInstrInfo.td.
//===----------------------------------------------------------------------===//

