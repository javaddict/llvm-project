//===-- HaydnPostSelectOptimize.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// Post-select peepholes (O1+). Live: cross-bank LD64/ST64 round-trip elide.
// MAC fusion helpers exist but are driver-disabled until MIR matrix.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTSELECTOPTIMIZE_H
#define LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTSELECTOPTIMIZE_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class FunctionPass;
class MachineInstr;
class MachineRegisterInfo;
class HaydnInstrInfo;

class HaydnPostSelectOptimize : public MachineFunctionPass {
public:
  static char ID;

  HaydnPostSelectOptimize();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn Post-Selection Optimizer";
  }

private:
  // Main driver that scans for all MAC formation opportunities.
  bool formMACs(MachineFunction &MF);

  // Main driver for SIMD optimization passes.
  bool optimizeSIMD(MachineFunction &MF);

  // Main driver for cross-bank pack/unpack elision. Runs after the selector
  // to catch round-trip patterns where a MOV_GPR_TO_DR64 / MOV_DR64_TO_GPR
  // pseudo directly feeds or is fed by a 64-bit memory access — replacing
  // the 5-instruction stack-staged pseudo expansion with a 2-instruction
  // ST32/LD32 pair. Recovers ~10 instructions/iteration in IIR/FIR kernels
  // and unblocks VLIW packetization into 2-3 slot bundles.
  bool elideCrossBankRoundTrips(MachineFunction &MF);

  // Try to fold `MOV_GPR_TO_DR64 %dr, %lo, %hi` immediately followed by
  // `ST64 %dr, %base, %off` into `ST32 %lo, %base, %off` + `ST32 %hi
  // %base, %off+4`. Eliminates the 5-cycle Slot-0 serial pack chain.
  bool tryFoldMovToSt64(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                        const HaydnInstrInfo &TII);

  // Try to fold `LD64 %dr, %base, %off` immediately followed by
  // `MOV_DR64_TO_GPR %lo, %hi, %dr` into `LD32 %lo, %base, %off` +
  // `LD32 %hi, %base, %off+4`. Eliminates the 5-cycle Slot-0 serial
  // unpack chain.
  bool tryFoldLd64ToMovFrom(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                            const HaydnInstrInfo &TII);

  // Try to fold `MOV_GPR_TO_DR64 %dr, %x, %x` (both halves equal — the
  // sign-extend-of-i32 shape produced by any path that sign-extends a GPR32
  // into a DR64 via two equal halves) into the native `SEXT_GPR32_TO_DR64 %dr
  // %x`. Eliminates the 5+ bundle SP-relative spill chain that MOV_GPR_TO_DR64
  // expands to post-RA. Defensive: the InstructionSelector already lowers
  // G_SEXT s32→s64 directly to SEXT_GPR32_TO_DR64; this catches any
  // surviving sext-shape merge (e.g. from intrinsic selection).
  bool tryFoldSextMovToDirect(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                              const HaydnInstrInfo &TII);

  // Try to fold `MOVE32_DR_L/H %gpr, %dr` immediately followed by
  // `ST32 %gpr, %base, %off` (where %gpr feeds only the store) into the
  // single lane-store `D_SW_L/H_WITH_IMM %dr, %base, (off>>2)`. Eliminates
  // the cross-bank lane extract: the DR64 lane goes directly to memory via
  // the slot-0 Load/Store unit, skipping the GPR32 intermediate.
  bool tryFoldMove32DrToSw(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                           const HaydnInstrInfo &TII);

  // Same-BB CSE of MOV_GPR_TO_DR64 whose GPR32 sources are constants
  // (ADDI32 r0, C or LOADI32 C). Cross-block reuse is left to MachineCSE.
  bool tryCSEConstantDR64(MachineInstr &MovInst, MachineRegisterInfo &MRI,
                          const HaydnInstrInfo &TII);

  // Try to form a scalar MAC32 from ADD32 + MUL32.
  bool tryFormMAC(MachineInstr &AddInst, MachineRegisterInfo &MRI,
                  const HaydnInstrInfo &TII);

  // Try to form a SIMD X2MULA32 from X2ADD32 + X2MUL32 (v2i32).
  bool tryFormSIMDMAC(MachineInstr &AddInst, MachineRegisterInfo &MRI,
                      const HaydnInstrInfo &TII);

  // Try to form a SIMD X2MULS32 from X2SUB32 + X2MUL32 (v2i32).
  bool tryFormSIMDMSub(MachineInstr &SubInst, MachineRegisterInfo &MRI,
                       const HaydnInstrInfo &TII);

  // tryFormFracMACQ31 REMOVED — MACQ31 was a phantom instruction
  // (not in the ISA DB; the real Q-format MAC family is FF2MULA32RS_*).
  // The fractional-MAC combine that formed MACQ31 from ADD32 + MULQ31 is
  // disabled. The selector now lowers haydn_macq31 directly to
  // ADD32(acc, MULSSH(a,b)).

  // Try to recognize a scalarized horizontal add pattern and replace
  // with X2HADD32_L. Pattern: MOV_DR64_TO_GPR extracting two halves
  // followed by ADD32 of those halves -> X2HADD32_L.
  bool tryFormSIMDHAdd(MachineInstr &AddInst, MachineRegisterInfo &MRI,
                       const HaydnInstrInfo &TII);

  // Try to recognize X2MUL32 followed by X2HADD32_L and replace with
  // X2DOT32 (dot product = sum of two element-wise products).
  bool tryFormSIMDDotProduct(MachineInstr &HAddInst,
                             MachineRegisterInfo &MRI,
                             const HaydnInstrInfo &TII);

  // Try to promote two independent scalar i32 operations whose inputs
  // come from the same DR64 register into a single SIMD X2 operation.
  bool tryPromoteScalarPairToSIMD(MachineInstr &MI,
                                  MachineRegisterInfo &MRI,
                                  const HaydnInstrInfo &TII);

  // Try to bypass COPY chains to reach the real def of a register.
  // Returns the defining instruction after peeling copies, or nullptr.
  MachineInstr *peekThroughCopies(Register Reg,
                                  MachineRegisterInfo &MRI) const;

  // Generic helper: try to fuse an ADD-pattern into a MAC instruction.
  // \param AddOpc The ADD opcode to look for (e.g. ADD32, ADD64, X2ADD32)
  // \param MulOpc The MUL opcode to look for in the addend
  // \param MacOpc The target MAC opcode to form
  // \param HasAccumulator True if the MAC has a 4-operand (ra) accumulator
  bool tryFuseAddMulToMAC(MachineInstr &AddInst, MachineRegisterInfo &MRI,
                          const HaydnInstrInfo &TII, unsigned AddOpc,
                          unsigned MulOpc, unsigned MacOpc,
                          bool HasAccumulator);

  // Try to recognize shift+add patterns that compute a * C (where C is not
  // a power of 2) and fuse them with an accumulator add into MAC32.
  // Pattern: %shl = SLLI32 %a, k (or SLL32 with constant amount)
  // %inner = ADD32 %shl, %a; computes a * (2^k + 1)
  // %result = ADD32 %inner, %acc
  // => materialize C = 2^k + 1
  // %mac = MAC32 %acc, %a, C
  // Also handles two-shift patterns:
  // %shl0 = SLLI32 %a, k0
  // %shl1 = SLLI32 %a, k1
  // %inner = ADD32 %shl0, %shl1; computes a * (2^k0 + 2^k1)
  // %result = ADD32 %inner, %acc
  // => materialize C = 2^k0 + 2^k1
  // %mac = MAC32 %acc, %a, C
  bool tryFormShiftAddMAC(MachineInstr &AddInst, MachineRegisterInfo &MRI,
                          const HaydnInstrInfo &TII);

};

// Create a Haydn Post-Selection Optimization pass.
FunctionPass *createHaydnPostSelectOptimizePass();

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_GISEL_HAYDNPOSTSELECTOPTIMIZE_H
