//===-- HaydnInstrFormatsD.td - D-Class 64-bit Bundle Formats -*- tablegen -*-=
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// D-class 64-bit bundle encoding format classes for the Haydn 3-issue VLIW DSP.
//
// These encodings are derived from encoding_manual.md and define per-slot
// format classes for the 64-bit bundle modes (Mode 0 through Mode 3).
//
// Mode discrimination via bits[3:0]:
// Mode 0 (0011): Independent 3-FU — s0=20b, s1=22b, s2=18b
// Mode 1 (0111): RESERVED — illegal_bundle trap (see §7, no format classes)
// Mode 2 (1011): 4x14b compressed — s0/s1a/s1b/s2, r0-r7 only
// Mode 3 (1111): Wide-s2 — s0=18b, s1=20b, s2=22b (full RRR MAC)
//
// All D-class bundles are 64 bits wide. Each bundle contains 3 issue slots
// whose widths and FU assignments vary by mode and row.
//
// GISel/CodeGen are encoding-independent — only MC-layer files (emitter
// disassembler, HaydnDClassInfo.h) consume these format classes directly.
//
//===----------------------------------------------------------------------===//
//
// This file is NO LONGER included by HaydnGeneric.td. Mode-0/2/3
// format classes (FmtM0_*/FmtM2_*/FmtM3_*) had zero live instruction
// instantiations after residual shell retirement. Do not re-include
// without live consumers. Island instruction files: legacy-retired/.
//

//===----------------------------------------------------------------------===//
// D-class base instruction class
//===----------------------------------------------------------------------===//

// STATUS : LIVE — KEEP (this IS the 128-bit-only target).
// The FmtM0_* / M0_* classes here and the _M0 instruction defs that use
// them (HaydnInstrInfo.td, HaydnInstrInfoS*/*M0.td) are the Mode-0 D-class
// 64-bit-bundle-slice encoding — i.e. the 128-bit-only TARGET format
// NOT legacy multi-width. The "Mode-0 _M0" name is a spec mode id, not a
// legacy-width tag. grep evidence: every sampled _M0 def (ADD32_M0
// SUB32_M0, ADD64_S1_M0, OR64_S1_M0, MOVE32_M0,...) has Haydn:: enum refs
// in.cpp AND an OPC_Decode transition in HaydnGenDisassemblerTables.inc.
// These are the live co-tenant of the _S* defs (HaydnFormats*.td);
// both are 128-bit paths. Do NOT retire.

// Base class for all D-class 64-bit bundle instructions.
// Extends HaydnInst with a 64-bit encoding field and sets the TSFlags
// width tag to 11 (64-bit) and decoder namespace to Haydn64.
class HaydnInstD<int sz, dag outs, dag ins, string asmstr, list<dag> pattern>
    : HaydnInst<sz, outs, ins, asmstr, pattern> {
  let TSFlags{4-3} = 0b11;
  let DecoderNamespace = "Haydn64";
}

//===----------------------------------------------------------------------===//
// Mode 0 (bits[3:0] = 0011): Independent 3-FU
//
// Bit layout (64 bits):
// bits[63:46] = s2 (18 bits)
// bits[45:24] = s1 (22 bits)
// bits[23:4] = s0 (20 bits)
// bits[3:0] = 0011 (pattern)
//
// s0 (20b): FU(1b) | opcode(6b) | rs(4b) | rt(4b) | ext(5b)
// bit[23] = FU: 0=ALU, 1=LS
// bits[22:17] = opcode
// bits[16:13] = rs (GPR source)
// bits[12:9] = rt (GPR dest)
// bits[8:4] = ext (imm/shift/extended opcode)
//
// s1 (22b): FU(2b) | payload(20b, varies by FU)
// bits[45:44] = FU: 00=ALU, 01=LD, 10=MAC, 11=reserved (Mode 0)
// ALU (FU=00): ALU64 by default; ALU32 when opc_imm[11:10]=11 marker
// (encoding_manual.md §6 s1 ALU32 sub-row).
// ALU64: rsd1(4) | rtd(4) | opcode(12): opc_imm[11:10]=00-10
// ALU32: rs(4) | rt(4) | marker(2)=11 | opcode(6) | class(2) | rsvd(2)
// LD: rs(4) | rtd(4) | imm8(8) + opcode(4)
// MAC: rsd1(4) | rsd2(4)| rtd(4) + opcode(8)
//
// s2 (18b): FU(1b) | payload(17b, varies by FU)
// bit[63] = FU: 0=ALU, 1=MAC
// ALU (FU=0): ALU64 by default; ALU32 when opcode[8]=1 marker
// (encoding_manual.md §6 s2 ALU32 sub-row).
// ALU64: rsd1(4) | rtd(4) | opcode(9): opcode[8]=0
// ALU32: rs(4) | rt(4) | flag(1)=1 | opcode(6) | class(2)
// MAC: rsd1(4) | rtd(4) | opcode(9) [destructive: rtd=rsd1]
//===----------------------------------------------------------------------===//

// Mode 0, s0 ALU format (20 bits at bits[23:4])
// FU select = 0 (ALU32), opcode in bits[22:17]

// Mode 0 Slot 0 — ALU32 register format.
// s0 ALU: FU(1)=0 | opcode(6) | rs(4) | rt(4) | ext(5)
// rs/rt are GPR32 registers. ext is imm/shift/extended opcode.
class FmtM0_S0_ALU<bits<6> opcode, dag outs, dag ins, string asmstr,
                    list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rs;
  bits<4> rt;
  bits<5> ext;

  let Inst{63-24} = 0;   // s2 + s1 placeholder (filled by bundler)
  let Inst{23}    = 0;    // FU = 0 (ALU)
  let Inst{22-17} = opcode;
  let Inst{16-13} = rs;
  let Inst{12-9}  = rt;
  let Inst{8-4}   = ext;
  let Inst{3-0}   = 0b0011;
}

// Mode 0 Slot 0 — ALU32 register format with no ext field (RR only).
// ext is encoded as 0.
class FmtM0_S0_ALU_RR<bits<6> opcode, dag outs, dag ins, string asmstr,
                       list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rs;
  bits<4> rt;

  let Inst{63-24} = 0;   // s2 + s1 placeholder
  let Inst{23}    = 0;    // FU = 0 (ALU)
  let Inst{22-17} = opcode;
  let Inst{16-13} = rs;
  let Inst{12-9}  = rt;
  let Inst{8-4}   = 0;   // ext = 0
  let Inst{3-0}   = 0b0011;
}

// Mode 0, s0 LS format (20 bits at bits[23:4])
// FU select = 1 (Load/Store), opcode in bits[22:17], ext[0] extends opcode

// Mode 0 Slot 0 LS: the base class FmtM0_S0_LS was removed in (zero
// subclass instantiations — all s0 LS variants use FmtM0_S0_LS_IMM4 / _SLD
// _SST / _REG / _Page1 in HaydnInstrInfoS0LSM0.td, which inherit HaydnInstD
// directly, not this base). The "FmtM0_S0_LS_*" family name persists in
// comments to describe the shared s0-LS shape (FU(1)=1 | opcode(6) | rs(4)
// rt(4) | ext(5)); no class or def references this removed base.
// Mode 0, s1 ALU64 format (22 bits at bits[45:24])
// FU select = 00 (ALU). opc_imm[11:10]=00-10 selects ALU64; =11 selects
// ALU32 (encoding_manual.md §6 s1 ALU32 sub-row). opcode in bits[35:24].

// Mode 0 Slot 1 ALU64: the base class FmtM0_S1_ALU64 was removed in
// (it was already documented DEAD after Flat2 migration — zero subclass
// instantiations). The 2-field Flat2 ops use FmtM0_S1_ALU64_FLAT2 (below);
// the 3-field RR-flat ops use FmtM0_S1_ALU64_RR3FIELD. Historical
// references to the dropped-rsd2 miscompile describe the old shape.

// Mode 0 Slot 1 — ALU64 RR-flat 3-FIELD format (the binary ops:
// ADD64/SUB64/AND64/OR64/XOR64/ADD64S/X2ADD32/X2SUB32/X2SLL32/X2SRL32
// X2SRA32). THREE register fields (rsd1, rsd2, rtd) — binary ops need 2
// distinct sources + dest (spec quickref: rtd = rsd1 OP rsd2). The old
// 2-field FmtM0_S1_ALU64 above DROPS rsd2 (the silent-miscompile);
// this class encodes all 3. opcode[7:6] != 0b01 is the RR-flat marker
// (distinguishes from the funct-routed family). FORMAT-FIRST tblgen (AIE
// model): the Inst IS the encoding (getBinaryCodeForInstr) AND
// DecoderNamespace="HaydnM0S1ALU64RR3" drives a tablegen DecoderTable trie
// (decodeInstruction reads rsd1/rsd2/rtd directly — no hand-rolled predicate
// no reverseEncLookupByFU for these ops). Mirrors FmtM0_S1_ALU64_Fn (funct).
class FmtM0_S1_ALU64_RR3FIELD<bits<8> opcode, dag outs, dag ins, string asmstr,
                              list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S1ALU64RR3";
  bits<4> rsd1;
  bits<4> rs2;
  bits<4> rtd;

  let Inst{63-46} = 0;    // s2 placeholder
  let Inst{45-44} = 0b00; // FU = 00 (ALU)
  let Inst{43-40} = rsd1;
  let Inst{39-36} = rs2;  // rsd2 (second source — NEW vs FmtM0_S1_ALU64)
  let Inst{35-32} = rtd;  // dest (moved from {39:36})
  let Inst{31-24} = opcode; // 8-bit; opcode[7:6]!=01 marker, [5:0] op-select
  let Inst{23-4}  = 0;    // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

// (numbered because was already taken by the funct-3field
// bit-budget ): Mode 0 Slot 1 — ALU64 flat 2-FIELD format (the
// unary/imm/SFR ops: MOVE64/NEG64/.../SLLI64/ARCTAN/SIN_COS/MOVEGPR2SFR
// MOVESFR2GPR). TWO register fields (rsd1, rtd) + a 12-bit opcode. These
// ops are unary or carry an unencoded imm/shamt operand (the imm ops use a
// fixed-zero imm field or a DecoderMethod hook in the variant class — see
// M0_S1_ALU64_RI6_ZERO / RR3_COPY below). opcode[11:10]!=01 is the ALU64
// marker (distinguishes from funct-routed AND from the ALU32 sub-row at
// bits[35:34]==11). FORMAT-FIRST tblgen (AIE model, mirrors
// FmtM0_S1_ALU64_RR3FIELD): the Inst IS the encoding AND
// DecoderNamespace="HaydnM0S1ALU64Flat2" drives a tablegen DecoderTable
// trie (decodeInstruction reads rsd1/rtd directly — no hand-rolled
// predicate, no reverseEncLookupByFU for these ops). SHAMT ops do NOT use
// this class (their split opcode_hi/shamt layout needs its own namespace
// see M0_S1_ALU64_SHAMT5/4).
class FmtM0_S1_ALU64_FLAT2<bits<12> opcode, dag outs, dag ins, string asmstr,
                           list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S1ALU64Flat2";
  bits<4> rsd1;
  bits<4> rtd;

  let Inst{63-46} = 0;    // s2 placeholder
  let Inst{45-44} = 0b00; // FU = 00 (ALU)
  let Inst{43-40} = rsd1;
  let Inst{39-36} = rtd;
  let Inst{35-24} = opcode;
  let Inst{23-4}  = 0;    // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

// Mode 0 Slot 1 — ALU32 sub-row format (encoding_manual.md §6 s1 ALU32
// sub-row). The s1 ALU slot multiplexes ALU32 and ALU64 via the
// opc_imm[11:10] marker: ALU64 uses 00-10, ALU32 uses 11. This format bakes
// the ALU32 sub-mode layout so the tablegen slot-OR encoder can emit unary
// ALU32 ops in s1 without the hand-rolled packInstructionIntoSlot PS_S1
// FU_ALU32 path.
// bits[45:44] = 00 (FU = ALU)
// bits[43:40] = rsd1 (GPR source, operand 1)
// bits[39:36] = rtd (GPR dest, operand 0)
// bits[35:34] = 11 (ALU32 sub-mode marker)
// bits[33:28] = opcode (6b — same ALU32_opc value as s0 ALU32)
// bits[27:26] = 00 (class = RR, the only legal class for s1 ALU32)
// bits[25:24] = 0 (reserved)
// Only the 2-operand unary ALU32 ops (NOT32/NEG32/NEG32S/NSA32/NSAU32
// POPCOUNT32) use this format — the s1 ALU32 sub-row has no rs2/immediate
// field, so the 3-operand RR family and RI forms stay s0-only.
class FmtM0_S1_ALU32<bits<6> opcode, dag outs, dag ins, string asmstr,
                     list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rsd1;
  bits<4> rtd;

  let Inst{63-46} = 0;       // s2 placeholder
  let Inst{45-44} = 0b00;    // FU = 00 (ALU)
  let Inst{43-40} = rsd1;    // rs (source)
  let Inst{39-36} = rtd;     // rt (dest)
  let Inst{35-34} = 0b11;    // ALU32 sub-mode marker
  let Inst{33-28} = opcode;  // ALU32_opc (6b)
  let Inst{27-26} = 0b00;    // class = RR
  let Inst{25-24} = 0;       // reserved
  let Inst{23-4}  = 0;       // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

// Mode 0 Slot 1 — ALU64 funct-routed view (Phase 4 tablegen decode).
// Per encoding_manual.md §6.6.1: s1 ALU64 funct-routing uses a 12-bit opcode
// field (bits[35:24]) = marker bits[11:10]=01 + funct(10) at bits[33:24].
// Each funct-routed op has a UNIQUE funct value (§6.6.2), so a tablegen
// decoder trie over the "HaydnM0S1ALU64" namespace routes cleanly — replacing
// the hand-rolled reverseFunctLookup (HaydnDClassOpcodes.cpp). Mechanism
// proven in-tree by FmtALU32Ext / DecoderNamespace="HaydnM0Only" (the MULL
// family uses the same funct-template-param pattern). The destructive s1
// layout (rtd=rd, rsd1=rs2, rs1=rd tied) is expressed via the standard
// `Constraints "$rtd = $rs1"` tied-operand decode — zero C++ reconstruction.
class FmtM0_S1_ALU64_Fn<bits<10> funct, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S1ALU64";
  bits<4> rsd1;
  bits<4> rtd;

  // Only the s1 window + pattern are assigned. The s0 (bits[23:4]) and s2
  // (bits[63:46]) windows are LEFT UNSET: the tablegen decoder treats unset
  // bits as wildcard (don't-care), so this def matches any s0/s2 content
  // required for per-slot decode of multi-slot bundles (codex). The encoder
  // (getBinaryCodeForInstr) emits 0 for unset bits, which the slot-OR bundler
  // relies on. (codex.)
  let Inst{45-44} = 0b00;  // FU = 00 (ALU)
  let Inst{43-40} = rsd1;  // rs2 (2nd source)
  let Inst{39-36} = rtd;   // rd (dest = rs1 tied, destructive)
  let Inst{35-34} = 0b01;  // funct-routing marker
  let Inst{33-24} = funct; // unique per-op funct key (§6.6.2)
  let Inst{3-0}   = 0b0011;
}

// slice 4c: Mode 0 Slot 1 — ALU64 funct-routed UNARY, cross-bank
// (GPR32 source -> DR64 dest). Per codex (option a): a dedicated class for
// cross-bank unary ops whose source register class is GPR32, not DR64. The
// GPR32 source occupies the SAME bitfield as FmtM0_S1_ALU64_Fn's rsd1
// (bits[43:40]) — only the register CLASS differs (GPR32 vs DR64), which
// tablegen expresses via the operand type (the AIE explicit-operand pattern
// e.g. OP_mMvSclSrc). No tied constraint (cross-bank: dest ≠ source). The
// standalone funct value (e.g. SEXT_GPR32_TO_DR64 = FmtALU64Unary<0x67
// 0x197>) carries over unchanged to the s1 funct-routed space — verified
// for the existing family (MAX64 standalone 0x0A4 == s1 0xA4).
class FmtM0_S1_ALU64_Fn_Unary_GPRToDR<bits<10> funct, dag outs, dag ins,
                                      string asmstr, list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S1ALU64";
  bits<4> rs;   // GPR32 source
  bits<4> rtd;  // DR64 dest
  let Inst{45-44} = 0b00;  // FU = 00 (ALU)
  let Inst{43-40} = rs;    // GPR32 source (same bitfield as rsd1, GPR32 class)
  let Inst{39-36} = rtd;   // DR64 dest
  let Inst{35-34} = 0b01;  // funct-routing marker
  let Inst{33-24} = funct; // unique per-op funct key (§6.6.2)
  let Inst{3-0}   = 0b0011;
}

// Mode 0 Slot 2 — ALU64 funct-routed view (Phase 4 tablegen decode).
// Per encoding_manual.md §6.7 (ISA-64 erratum): s2 ALU64 funct-routing uses
// the 9-bit opcode field (bits[54:46]) = funct value DIRECTLY (range
// 0x001-0x09C, all bit[8]=0; no marker — bit[8]=0 IS the funct flag, bit[8]=1
// selects the s2 ALU32 sub-mode). Symmetric to FmtM0_S1_ALU64_Fn. Only the s2
// window + pattern assigned; s0/s1 windows UNSET (wildcard). Destructive s2
// layout (rtd=rd, rsd1=rs2, rs1=rd tied) via Constraints "$rtd = $rs1".
class FmtM0_S2_ALU64_Fn<bits<9> funct, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  let DecoderNamespace = "HaydnM0S2ALU64";
  bits<4> rsd1;
  bits<4> rtd;

  let Inst{63}    = 0;     // FU = 0 (ALU)
  let Inst{62-59} = rsd1;  // rs2 (2nd source)
  let Inst{58-55} = rtd;   // rd (dest = rs1 tied, destructive)
  let Inst{54-46} = funct; // = funct value directly (ISA-64 erratum, §6.7)
  let Inst{3-0}   = 0b0011;
}

// Mode 0 Slot 2 — ALU32 sub-mode (encoding_manual.md §6 s2
// ALU32 sub-row). When the s2 row slot is FU_ALU, the instruction may be
// either ALU64 (opcode[8]=0, handled by FmtM0_S2_ALU64_Fn; the base
// FmtM0_S2_ALU64 was removed) or ALU32
// (opcode[8]=1, this format). Only the s2 window + pattern are assigned;
// s0/s1 windows are LEFT UNSET (wildcard), matching FmtM0_S2_ALU64_Fn
// the tablegen encoder emits 0 for unset bits, which the slot-OR bundler
// relies on. Symmetric to FmtM0_S1_ALU32.
// Layout per §6 s2 ALU32 sub-encoding detail (22-bit s2 window):
// bit[63] = 0 (FU = ALU)
// bits[62:59] = rs (GPR source, operand 1)
// bits[58:55] = rt (GPR dest, operand 0)
// bit[54] = 1 (ALU32 flag = opcode[8])
// bits[53:48] = ALU32_opc(6) — same 6-bit opcode value as s0/s1 ALU32
// bits[47:46] = 00 (class = RR, the only legal class for s2 ALU32)
// Only the 2-operand unary ALU32 ops (NOT32/NEG32/NEG32S/NSA32/NSAU32
// POPCOUNT32) reach here (getLegalSlots = LSB_S0|LSB_S1|LSB_S2). The 3-operand
// RR family stays s0/s1-only (the s2 layout has no rs2 field).
class FmtM0_S2_ALU32<bits<6> opcode, dag outs, dag ins, string asmstr,
                     list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rsd1;
  bits<4> rtd;

  let Inst{63}    = 0;       // FU = 0 (ALU)
  let Inst{62-59} = rsd1;    // rs (source)
  let Inst{58-55} = rtd;     // rt (dest)
  let Inst{54}    = 0b1;     // ALU32 sub-mode flag (opcode[8]=1)
  let Inst{53-48} = opcode;  // ALU32_opc (6b)
  let Inst{47-46} = 0b00;    // class = RR
  let Inst{3-0}   = 0b0011;
}

// Mode 0, s1 LD format (22 bits at bits[45:24])
// FU select = 01 (LD), opcode(4) + imm8(8)

// Mode 0 Slot 1 Load: the base class FmtM0_S1_LD was removed in (zero
// subclass instantiations — s1 LD variants use FmtM0_S1_ALU64_FLAT2 or
// dedicated defs in HaydnInstrInfoS1LDM0.td that inherit HaydnInstD directly
// not this base). Shape: FU(2)=01 | rs(4) | rtd(4) | imm8(8) | opcode(4).
// Mode 0, s1 MAC format (22 bits at bits[45:24])
// FU select = 10 (MAC), rtd(4) + opcode(8)

// Mode 0 Slot 1 — MAC format (3-register).
// s1 MAC: FU(2)=10 | rsd1(4) | rsd2(4) | rtd(4) | opcode(8)
// All DR64 registers. Non-destructive (rtd != rsd1).
class FmtM0_S1_MAC<bits<8> opcode, dag outs, dag ins, string asmstr,
                    list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rsd1;
  bits<4> rsd2;
  bits<4> rtd;

  let Inst{63-46} = 0;    // s2 placeholder
  let Inst{45-44} = 0b10; // FU = 10 (MAC)
  let Inst{43-40} = rsd1;
  let Inst{39-36} = rsd2;
  let Inst{35-32} = rtd;
  let Inst{31-24} = opcode;
  let Inst{23-4}  = 0;    // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

// Mode 1 (pattern 0111) Slot 1 MAC: the class FmtM1_S1_MAC26 was removed in
// (zero subclass instantiations; Mode 1 is RESERVED per spec §7 — the
// decoder raises an illegal_bundle trap and falls back to a bundle-wide NOP
// and the 357 MAC ops were never defined as Mode-1 defs). The standalone
// Mode-2 dual-ND MAC FmtM2_S1_MAC22 (below) carries the live MAC encodings.

// Mode 2 (pattern 1011) Slot 1 — Dual ND MAC (22-bit s1).
// 61 essential MAC ops, NON-destructive (rsd1+rsd2+rtd separate).
// s0=16b (ALU32 or LOAD), s1=22b, s2=22b (identical layout). No sub-encoding.
// encoding_manual.md §8.
class FmtM2_S1_MAC22<bits<3> macClass, bits<5> variant, dag outs, dag ins,
                     string asmstr, list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rsd1;
  bits<4> rsd2;
  bits<4> rtd;

  let Inst{63-42} = 0;         // s2 placeholder (same layout when occupied)
  let Inst{41-40} = 0b00;      // FU = 00 (MAC in Mode 2)
  let Inst{39-36} = rsd1;
  let Inst{35-32} = rsd2;
  let Inst{31-28} = rtd;
  let Inst{27-25} = macClass;  // static class
  let Inst{24-20} = variant;   // static variant
  let Inst{19-4}  = 0;         // s0 placeholder (16b)
  let Inst{3-0}   = 0b1011;    // Mode 2 pattern
}

// Mode 0 Slot 1 — MAC format, single-source (2-register, dual-field-bind).
// for the 5 TWO-INPUT MAC ops (X4CMUL16 family + X4ENERGY16) whose DB
// syntax is `rtd, rsd` (one DR64 source). The s1 MAC wire format
// `FU(2)+rsd1(4)+rsd2(4)+rtd(4)+opcode(8)` (encoding_manual.md §6.6 L965)
// has separate rsd1@43:40 and rsd2@39:36 fields; for a single-source op the
// architectural source `rsd` populates BOTH fields (silicon reads both as
// MAC sources). This is the dual-field-bind idiom: one `bits<4> rsd`
// operand bound to both Inst ranges, byte-equal to what the hand-rolled
// encoder would emit if it did not have the latent drop-rsd2 bug
// (HaydnMCCodeEmitter.cpp FU_MAC L677 `Rsd2 =... : 0`). Avoids input-input
// `Constraints` ties (which tablegen rejects). The asm string uses 2
// operands (rtd, rsd); decode reconstructs both fields, printer sees 2 ops.
class FmtM0_S1_MAC_1SR<bits<8> opcode, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rsd;
  bits<4> rtd;

  let Inst{63-46} = 0;    // s2 placeholder
  let Inst{45-44} = 0b10; // FU = 10 (MAC)
  let Inst{43-40} = rsd;  // rsd1 field = rsd
  let Inst{39-36} = rsd;  // rsd2 field = rsd (dual-field-bind)
  let Inst{35-32} = rtd;
  let Inst{31-24} = opcode;
  let Inst{23-4}  = 0;    // s0 placeholder
  let Inst{3-0}   = 0b0011;
}

// Mode 0, s2 ALU64 / MAC base classes: FmtM0_S2_ALU64 and FmtM0_S2_MAC were
// removed in (both zero subclass instantiations — the live s2 ops use
// FmtM0_S2_ALU64_Fn / FmtM0_S2_ALU32 / FmtM2_S2_* / FmtM3_S2_* which inherit
// HaydnInstD directly). Shape: s2 ALU64 = FU(1)=0 | rsd1(4) | rtd(4)
// opcode(9); s2 MAC = FU(1)=1 | rsd1(4) | rtd=rsd1(4) | opcode(9).

//===----------------------------------------------------------------------===//
// Mode 1 (bits[3:0] = 0111): RESERVED per encoding_manual.md §7
//
// Mode 1 is a reserved bundle pattern: the decoder raises an
// `illegal_bundle` trap and falls back to a bundle-wide NOP. All four
// FU combinations Mode 1 nominally covered are already expressible in
// Mode 0 (rows 3, 4, 11, 12 per §7), and the Fused FU was never populated
// (0 of 681 instructions). No format classes are defined for Mode 1.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Mode 2 (bits[3:0] = 1011): 4x14b Compressed, r0-r7 only — RETIRED.
//
// All eight Mode-2 format classes (FmtM2_S0_ALU, FmtM2_S0_LS, FmtM2_S1a_ALU64
// FmtM2_S1b_ALU64, FmtM2_S1b_LD, FmtM2_S1b_MAC, FmtM2_S2_ALU64, FmtM2_S2_MAC)
// were removed in : zero subclass instantiations each, zero code-part/.inc
// references. Mode-2 (4x14b compressed, r0-r7 only) was never populated — the
// only live Mode-2-derived class is FmtM2_S1_MAC22 (the dual-ND MAC above
// which shares the 1011 pattern bits but is a standalone 22-bit s1 format with
// its own def family in HaydnInstrInfoMACM1.td). The bit-layout reference for
// the retired 4x14b shape is preserved in encoding_manual.md §8.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Mode 3 (bits[3:0] = 1111): Wide-s2
//
// Bit layout (64 bits):
// bits[63:42] = s2 (22 bits, full RRR MAC)
// bits[41:22] = s1 (20 bits)
// bits[21:4] = s0 (18 bits)
// bits[3:0] = 1111 (pattern)
//
// Note: slot widths are swapped from Mode 0. s2 is now the widest (22b)
// to accommodate full 3-register MAC encoding.
//
// s0 (18b): FU(1b) | payload(17b)
// ALU: FU=0 | opcode(6) | rs(4) | rt(4) | ext(3)
// LS: FU=1 | opcode(6) | rs(4) | rt(4) | ext(3)
//
// s1 (20b): FU(2b) | payload(18b)
// ALU64: FU=00 | rsd1(4) | rtd(4) | opcode(10)
// LD: FU=01 | rs(4) | rtd(4) | imm8(8) + opcode(4) -> 20b
// MAC: FU=10 | rsd1(4) | rsd2(4)| rtd(4) + opcode(6) -> 18b + 2b FU
//
// s2 (22b): FU(2b) | payload(20b)
// ALU64: FU=00 | rsd1(4) | rtd(4) | opcode(10)
// MAC0: FU=01 | rsd1(4) | rsd2(4)| rtd(4) + opcode(8)
// MAC1: FU=10 | rsd1(4) | rsd2(4)| rtd(4) + opcode(8)
//===----------------------------------------------------------------------===//

// Mode 3, s2 formats (22 bits at bits[63:42]) — wide s2 for full RRR MAC.
// (: the s0 ALU/LS + s1 ALU64/LD/MAC + s2 ALU64 format classes were
// removed as dead — zero subclass uses; only the s2 MAC0 family is live.
// The s0/s1/s2-ALU64 slots in Mode 3 are never populated by any instruction
// def; the live Mode-3 traffic is MAC-on-s2 only.)

// Mode 3 Slot 2 — MAC0 format (22-bit s2, full RRR).
// FU(2)=01 | rsd1(4) | rsd2(4) | rtd(4) | opcode(8)
// Dual independent MAC units (MAC0, MAC1) enable full RRR dual-MAC.
class FmtM3_S2_MAC0<bits<8> opcode, dag outs, dag ins, string asmstr,
                     list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rsd1;
  bits<4> rsd2;
  bits<4> rtd;

  let Inst{63-62} = 0b01;  // FU = 01 (MAC0)
  let Inst{61-58} = rsd1;
  let Inst{57-54} = rsd2;
  let Inst{53-50} = rtd;
  let Inst{49-42} = opcode;
  let Inst{41-4}  = 0;     // s1 + s0 placeholder
  let Inst{3-0}   = 0b1111;
}

// Mode 3 Slot 2 — MAC0 format, single-source (2-register, dual-field-bind).
// mirror of FmtM0_S1_MAC_1SR for Mode 3 s2. The 5 TWO-INPUT MAC ops
// (X4CMUL16 family + X4ENERGY16) get a _M3S2 variant using this format:
// one `bits<4> rsd` operand bound to BOTH Inst{61-58} (rsd1) and
// Inst{57-54} (rsd2). Byte-equal encode to the s1 _M0S1 variant (same
// opcode, same dual-field source). See FmtM0_S1_MAC_1SR for full rationale.
class FmtM3_S2_MAC0_1SR<bits<8> opcode, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : HaydnInstD<8, outs, ins, asmstr, pattern> {
  bits<4> rsd;
  bits<4> rtd;

  let Inst{63-62} = 0b01;  // FU = 01 (MAC0)
  let Inst{61-58} = rsd;   // rsd1 field = rsd
  let Inst{57-54} = rsd;   // rsd2 field = rsd (dual-field-bind)
  let Inst{53-50} = rtd;
  let Inst{49-42} = opcode;
  let Inst{41-4}  = 0;     // s1 + s0 placeholder
  let Inst{3-0}   = 0b1111;
}
