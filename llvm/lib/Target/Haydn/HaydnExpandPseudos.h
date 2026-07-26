//===-- HaydnExpandPseudos.h - Expand pseudo instructions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the HaydnExpandPseudos pass, which
// expands Haydn pseudo instructions into real target instructions after
// register allocation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPSEUDOS_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPSEUDOS_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class HaydnInstrInfo;
class HaydnSubtarget;

// Expands Haydn pseudo instructions into real machine instructions.
// This pass runs after register allocation and expands pseudos that could not
// be lowered earlier in the pipeline. It handles call frame adjustments
// libcall invocations (soft-float division/remainder), global address
// materialization, and call/return pseudos.
// Cross-bank register moves (MOV_GPR_TO_DR64, MOV_DR64_TO_GPR) are handled
// in HaydnInstrInfo::expandPostRAPseudo instead, because they create frame
// indices that must be eliminated by PEI.
class HaydnExpandPseudos : public MachineFunctionPass {
public:
  static char ID;

  HaydnExpandPseudos();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "Haydn pseudo instruction expansion pass";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    // VAARG expand may split MBBs (reg-bank vs stack overflow). Do not claim
    // CFG preservation.
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  const HaydnSubtarget *STI = nullptr;
  const HaydnInstrInfo *TII = nullptr;

  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineInstr &MI,
                MachineBasicBlock::iterator &NextMBBI);

  // Expand residual pseudos inside VLIW bundles (CALL/LOAD_ADDR/libcall;
  // late *_POST_INC only if ExpandPostIncEarly was off or skipped).
  // Product post-inc expand home is HaydnExpandPostIncEarly (pre-pack).
  bool expandPseudosInBundles(MachineBasicBlock &MBB);

  // insert soft-zero R0 maintenance in MIR (before PostRA pack)
  // so AsmPrinter is representation-only. Covers JT-dispatch targets
  // (BR_JT → JALR clobbers R0) and after direct/indirect calls.
  bool insertSoftZeroR0Maintenance(MachineFunction &MF);

  // Expand LOAD_ADDR pseudo (LUI + ADDI32 for global addresses).
  bool expandLOAD_ADDR(MachineBasicBlock &MBB, MachineInstr &MI);

  // Expand ADJCALLSTACKDOWN pseudo (subtract from SP or emit frame fixup).
  bool expandADJCALLSTACKDOWN(MachineBasicBlock &MBB, MachineInstr &MI);

  // Expand ADJCALLSTACKUP pseudo (add to SP or emit frame fixup).
  bool expandADJCALLSTACKUP(MachineBasicBlock &MBB, MachineInstr &MI);

  // Expand PseudoCALL pseudo to JAL R15, target.
  bool expandPseudoCALL(MachineBasicBlock &MBB, MachineInstr &MI);

  // Expand a libcall pseudo (SDIV/UDIV/SREM/UREM/MUL64) to a JAL to the
  // given compiler-rt symbol.
  bool expandLibcall(MachineBasicBlock &MBB, MachineInstr &MI,
                     const char *Symbol);

  // Expand LD32_POST_INC pseudo into LD32 + ADDI32.
  bool expandLD32PostInc(MachineBasicBlock &MBB, MachineInstr &MI);

  // Expand ST32_POST_INC pseudo into ST32 + ADDI32.
  bool expandST32PostInc(MachineBasicBlock &MBB, MachineInstr &MI);

  // Expand LD64_POST_INC pseudo into LD64_S1 + ADDI32.
  bool expandLD64PostInc(MachineBasicBlock &MBB, MachineInstr &MI);

  // Expand ST64_POST_INC pseudo into ST64 + ADDI32.
  bool expandST64PostInc(MachineBasicBlock &MBB, MachineInstr &MI);

  // W1.2: expand VASTART/VACOPY before pack (was AsmPrinter-only). Uses
  // withPostRAScratch (free GPR first; spill only if needed) + FrameLowering
  // FI refs. Residual at AsmPrinter is fatal.
  bool expandVASTART(MachineBasicBlock &MBB, MachineInstr &MI);
  bool expandVACOPY(MachineBasicBlock &MBB, MachineInstr &MI);

  // CB-131: expand VAARG_I32/I64 — unified reg-bank + stack overflow.
  // May split MBB (reg path / stack path / join). \p NextMBBI updated when
  // the original MBB is split so the expand loop does not rescan new blocks
  // incorrectly.
  bool expandVAARG(MachineBasicBlock &MBB, MachineInstr &MI,
                   MachineBasicBlock::iterator &NextMBBI, bool IsI64);
};

// Creates and returns a HaydnExpandPseudos pass.
FunctionPass *createHaydnExpandPseudosPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNEXPANDPSEUDOS_H
