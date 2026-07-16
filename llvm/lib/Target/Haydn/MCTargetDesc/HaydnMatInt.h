//===- HaydnMatInt.h - Immediate materialisation --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a utility to generate optimal instruction sequences for
// materialising integer constants into registers on the Haydn target.
//
// Haydn has the following relevant instructions for constant materialisation:
//
// ADDI32 Rd, Rs, Simm16 — add 16-bit signed immediate
// LUI Rd, Rs, Uimm12 — load upper 12-bit immediate (bits [31:20])
// ORI32 Rd, Rs, Uimm16 — or with 16-bit unsigned immediate
// SLLI32 Rd, Rs, Uimm5 — shift left logical by 0–31
// ADDI32_W Rt, Rs, Simm20 — 48-bit wide add 20-bit signed immediate
//
// 32-bit constant materialisation strategies (in priority order):
//
// 1. Zero: ADDI32 Rd, R0, 0
// 2. Signed 16-bit: ADDI32 Rd, R0, Imm
// 3. Upper-half-only: LUI Rd, R0, Hi16
// 4. Power of 2: SLLI32 Rd, R0, Shift
// 5. Shifted 16-bit mask: SLLI32 + ANDI32 or SLLI32 + ORI32
// 6. LUI + ADDI32 (with sign-extension compensation)
// 7. LUI + ORI32 (for patterns where OR avoids compensation)
// 8. LUI + ADDI32_W (universal 2-instr; 12+20 covers all 32 bits)
//
// 64-bit constants:
// Built in 32-bit halves using MOV_GPR_TO_DR64 to merge.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMATINT_H
#define LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMATINT_H

#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace llvm {

class APInt;

namespace HaydnMatInt {

// A single instruction in a constant-materialisation sequence.
struct Inst {
  unsigned Opc;  //< Opcode (e.g. Haydn::ADDI32, Haydn::LUI)
  int64_t Imm;   //< Immediate operand value

  Inst(unsigned Opc, int64_t Imm) : Opc(Opc), Imm(Imm) {}
};

// Ordered sequence of instructions to materialise a constant.
using InstSeq = SmallVector<Inst, 4>;

// Generate an optimal instruction sequence that materialises \p Value into a
// GPR32 register.
// For values fitting in a signed 16-bit immediate, a single ADDI32 is
// emitted. For power-of-2 values, a single SLLI32 may be used.
// For other 32-bit values the function chooses the shortest sequence from
// {LUI, ADDI32, ORI32, SLLI32}. For 64-bit values the sequence builds the
// upper and lower halves using MOV_GPR_TO_DR64.
// \param Value The integer constant to materialise.
// \returns The instruction sequence.
InstSeq generate(int64_t Value);

// Estimate the number of instructions required to materialise \p Val as
// an \p Size-bit integer.
int getIntMatCost(const APInt &Val, unsigned Size);

} // namespace HaydnMatInt
} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_MCTARGETDESC_HAYDNMATINT_H
