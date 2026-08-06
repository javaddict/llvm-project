//===- HaydnMatInt.cpp - Immediate materialisation ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements constant materialisation for the Haydn target.
// It computes the shortest CORRECT instruction sequence to load an arbitrary
// integer constant into a register, using ADDI32, LUI, ORI32, SLLI32, and the
// 48-bit wide-imm ADDI32_W.
//
// The selector chains each sequence from R0 (soft-zero, =0): ADDI32 rd,R0,imm
// = sext(imm); ORI32 rd,R0,imm = imm (zero-extended, so any 0..65535 in 1
// instr); ADDI32_W rd,R0,imm = sext(imm20) (so any simm20 in 1 instr); LUI
// rd,R0,imm = imm12<<20 (rs ignored); SLLI32/ORI32 rd,rs,imm shift/or the
// running value.
//
// ISA (VLIW_Engine_ISA_Reference.md): LUI rt,imm12 loads imm12 into bits
// [31:20]. ADDI32 is simm16. ORI32 is uimm16. ADDI32_W (encoding_manual.md §5
// Class 000, opcode 0x08) is the 48-bit wide variant carrying simm20. So:
// 1 instr: simm16 (ADDI32), or 0..65535 (ORI32 from R0), or simm20 not
// already covered (ADDI32_W from R0)..
// 1-2 instr: LUI [+ ADDI32] when the 12-bit-LUI pair reconstructs V, i.e.
// V-(hi12<<20) fits simm16 (the ADDI32 carry closes the bits[19:16] gap).
// Covers >1MiB constants (e.g. the 0xF0000000 semihost doorbell -> LUI
// 0xF00), powers of two >= 1MiB, and any value with bits[19:16] in {0,F}.
// 2 instr (universal): LUI + ADDI32_W. The 12+20 split covers the
// whole 32-bit space; this wins the bits[19:16] {1..14} "hole" that the
// 12-bit LUI + simm16 ADDI32 cannot reach in 2 instrs.
//
// Pre-ISA-43 the backend treated LUI as 16-bit-shift and emitted SLLI32 as a
// leading instruction from R0 (=0) — both wrong. This rewrite is correct.
//
//===----------------------------------------------------------------------===//

#include "HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {

// LUI + ADDI32_W universal 2-instruction sequence (ISA-44 Branch A
//ADDI32_W is a 48-bit Wide-imm ALU op carrying a 20-bit signed
// immediate: `rt = rs + sext(imm20)`. Combined with LUI (imm12 into
// bits[31:20]), the two fields are contiguous (12 high + 20 low = 32), so
// every 32-bit value materialises in exactly 2 instructions. ADDI32_W's
// sign-extension boundary is bit 19, so we round hi12 by half the simm20
// range (0x80000) to absorb the carry, mirroring the RISC-V lui(20)+addi(12)
// decomposition but split 12-high + 20-low.
// Always succeeds. The result is appended to \p Seq as exactly 2 ops:
// `LUI hi12` then `ADDI32_W lo20`. (1-op degenerate cases are intentionally
// not handled here; the caller already wins those with ADDI32/ORI32/LUI.)
void generateLuiAddi32W(uint32_t Val, HaydnMatInt::InstSeq &Seq) {
  // Round hi12 by half the simm20 range so ADDI32_W's sign-extension carry
  // closes any bits[19:16] gap between LUI's bits[31:20] and the low 20 bits.
  uint32_t Hi12 = ((Val + 0x80000u) >> 20) & 0xFFFu;
  int32_t Lo20 = static_cast<int32_t>(Val - (Hi12 << 20));
  assert(isInt<20>(Lo20) && "LUI+ADDI32_W residual must fit simm20");
  Seq.emplace_back(Haydn::LUI, Hi12);
  Seq.emplace_back(Haydn::ADDI32_W, Lo20);
}

// Generate the shortest CORRECT sequence for a 32-bit constant.
// ADDI32 is WIDE-only (RI20, imm20); the _W rs/rt fields are independent
// (tie removed), so `addi32_w Dst, R0, imm` materialises sext(imm20) from
// the soft-zero in one instruction. ORI32_W zero-extends. LUI+ADDI32_W
// (12+20 split) covers the whole 32-bit space in two instructions.
void generate32BitSeq(int64_t Value, HaydnMatInt::InstSeq &Seq) {
  uint32_t Val = static_cast<uint32_t>(Value);

  // Strategy 1: simm20 — single ADDI32_W from R0 (sign-extended, 1 instr).
  // Includes 0 (addi32_w Dst, R0, 0).
  if (isInt<20>(Value)) {
    Seq.emplace_back(Haydn::ADDI32_W, Value);
    return;
  }

  HaydnMatInt::InstSeq Best;

  // Strategy 2: uimm20 with bit 19 set (doesn't fit simm20) — single ORI32_W
  // from R0 (zero-extended). Covers 0x80000..0xFFFFF in one instruction.
  if (isUInt<20>(Val))
    Best.emplace_back(Haydn::ORI32, Val);

  // Strategy 3: universal LUI + ADDI32_W (exactly 2 instr, always correct).
  // The 12+20 split covers the whole 32-bit space.
  if (Best.empty())
    generateLuiAddi32W(Val, Best);

  assert(!Best.empty() && "No materialisation sequence found");
  Seq.append(Best);
}

} // anonymous namespace

namespace llvm::HaydnMatInt {

InstSeq generate(int64_t Value) {
  InstSeq Seq;

  // 32-bit values: use the optimal 32-bit sequence.
  if (isInt<32>(Value)) {
    generate32BitSeq(Value, Seq);
    return Seq;
  }

  // 64-bit values: materialise in two 32-bit halves, then merge.
  uint32_t Lo32 = static_cast<uint32_t>(Value & 0xFFFFFFFFLL);
  uint32_t Hi32 = static_cast<uint32_t>((Value >> 32) & 0xFFFFFFFFLL);

  int32_t SignedLo32 = static_cast<int32_t>(Lo32);
  if (Hi32 == 0 && SignedLo32 >= 0) {
    generate32BitSeq(SignedLo32, Seq);
    return Seq;
  }
  if (Hi32 == 0xFFFFFFFF && SignedLo32 < 0) {
    generate32BitSeq(SignedLo32, Seq);
    return Seq;
  }

  generate32BitSeq(static_cast<int32_t>(Lo32), Seq);

  InstSeq HiSeq;
  generate32BitSeq(static_cast<int32_t>(Hi32), HiSeq);
  Seq.append(HiSeq);

  // Merge the two GPR32 halves into a DR64 register.
  Seq.emplace_back(Haydn::MOV_GPR_TO_DR64, 0);

  return Seq;
}

int getIntMatCost(const APInt &Val, unsigned Size) {
  // Determine the cost (number of instructions) for materialising the
  // constant. Split into 32-bit chunks.
  int Cost = 0;
  for (unsigned ShiftVal = 0; ShiftVal < Size; ShiftVal += 32) {
    APInt Chunk = Val.ashr(ShiftVal).sextOrTrunc(32);
    int64_t ChunkVal = Chunk.getSExtValue();
    if (ChunkVal == 0)
      continue;
    InstSeq MatSeq = generate(ChunkVal);
    Cost += static_cast<int>(MatSeq.size());
  }
  return std::max(1, Cost);
}

} // namespace llvm::HaydnMatInt
