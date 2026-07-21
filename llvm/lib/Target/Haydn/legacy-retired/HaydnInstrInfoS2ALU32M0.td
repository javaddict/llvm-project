// ===-- HaydnInstrInfoS2ALU32M0.td - s2 ALU32 sub-row encode variants ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ENCODE-only _S2_M0 variants for the 2-operand unary ALU32 ops that the s2
// ALU32 sub-row accepts (encoding_manual.md §6 s2 ALU32 sub-row).
// Mirrors the proven s1 mechanism (HaydnInstrInfoS1ALU32M0.td : FmtM0_S1_ALU32
// shipped), specialized for the s2 ALU32 sub-mode flag (bit[54]=1).
//
// Source of truth: encoding_manual.md §6 s2 ALU32 sub-row (18-bit window):
// bit[63] = 0 (FU = ALU)
// bits[62:59] = rs (GPR source, operand 1)
// bits[58:55] = rt (GPR dest, operand 0)
// bit[54] = 1 (ALU32 sub-mode flag = opcode[8])
// bits[53:48] = ALU32_opc(6) — same 6-bit opcode value as s0/s1 ALU32
// bits[47:46] = 00 (class = RR, the only legal class for s2 ALU32)
//
// These variants retire the hand-rolled packInstructionIntoSlot PS_S2 FU_ALU32
// block (HaydnMCCodeEmitter.cpp). The 6 ALU32_opc values come from
// getEncOpcodeMap FU_ALU32/S0 (HaydnDClassOpcodes.cpp L95-103): NOT32=0x11
// NEG32=0x12, NEG32S=0x13, NSA32=0x17, NSAU32=0x18, POPCOUNT32=0x19.
//
// ALL variants are MC-only: isCodeGenOnly=1, isPseudo=0. D-3a (Phase D-3)
// gives these a DEDICATED DecoderNamespace="HaydnM0S2ALU32" so the tablegen
// decoder table DecoderTableHaydnM0S2ALU3232 is generated + probed by
// tryDecodeM0S2ALU32Table. Format-first dispatch makes this collision
// FREE: FmtM0_S2_ALU32 fixes the ALU32 sub-mode flag bit[54]=1 (=opcode[8])
// which is DISJOINT from every funct-routed s2 ALU64 opcode (opcode[8]=0
// bit[54]=0). The old "would alias" note applied to the pre-format
// first mixed-dispatch decoder; under per-format-table dispatch each trie
// serves a fully-constrained input space.
//
// Operand model: generic FmtALU32Unary is 2-op (outs GPR32:$rd), (ins GPR32:$rs);
// variant is (outs GPR32:$rtd), (ins GPR32:$rsd1) — blind setDesc preserves
// operand count + order 1:1. No Constraints tie (the s2 ALU32 sub-row is
// non-destructive: rt and rs are independent fields, unlike s2 ALU64 which
// ties rd=rs1).
//
// This file is included by Haydn.td (so -gen-emitter / -gen-instr-info see
// the variants) but NOT by HaydnAsmMatcher.td (asm-matcher sees generic only
// per the build-system split). isCodeGenOnly=1 also excludes from
// the matcher.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Format class — encode-only specialization of FmtM0_S2_ALU32.
//===----------------------------------------------------------------------===//

// M0_S2_ALU32_UNARY — 2-op unary s2 ALU32 (rt, rs). 6 ops: NOT32/NEG32
// NEG32S/NSA32/NSAU32/POPCOUNT32. Generic is (outs GPR32:$rd), (ins GPR32:$rs);
// variant is (outs GPR32:$rtd), (ins GPR32:$rsd1). Both operands are GPR32
// (4-bit encoding value). Blind setDesc from the generic op is safe (same
// operand count + order + register bank).
class M0_S2_ALU32_UNARY<bits<6> opcode, dag outs, dag ins, string asmstr>
  : FmtM0_S2_ALU32<opcode, outs, ins, asmstr, []> {
  // isCodeGenOnly is 0 (default — do NOT set =1, or the decoder table won't
  // generate, per the MAC/S0ALU/S0LS pattern). DecoderNamespace="HaydnM0S2ALU32"
  // isolates these from the default asm-match namespace.
  let isPseudo = 0;
  let DecoderNamespace = "HaydnM0S2ALU32";
}

//===----------------------------------------------------------------------===//
// Unary 2-op variants (6 ops). ALU32_opc values from getEncOpcodeMap
// (HaydnDClassOpcodes.cpp L95-103, FU_ALU32/S0 row — same opcode value as
// s0/s1 — see encoding_manual.md §6.4 ALU32 opcode class partition).
//===----------------------------------------------------------------------===//

def NOT32_S2_M0      : M0_S2_ALU32_UNARY<0x11,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "not32_s2_m0\t$rtd, $rsd1">;
def NEG32_S2_M0      : M0_S2_ALU32_UNARY<0x12,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "neg32_s2_m0\t$rtd, $rsd1">;
def NEG32S_S2_M0     : M0_S2_ALU32_UNARY<0x13,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "neg32s_s2_m0\t$rtd, $rsd1">;
def NSA32_S2_M0      : M0_S2_ALU32_UNARY<0x17,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "nsa32_s2_m0\t$rtd, $rsd1">;
def NSAU32_S2_M0     : M0_S2_ALU32_UNARY<0x18,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "nsau32_s2_m0\t$rtd, $rsd1">;
def POPCOUNT32_S2_M0 : M0_S2_ALU32_UNARY<0x19,
    (outs GPR32:$rtd), (ins GPR32:$rsd1), "popcount32_s2_m0\t$rtd, $rsd1">;
