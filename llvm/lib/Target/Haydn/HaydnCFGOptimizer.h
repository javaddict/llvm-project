//===-- HaydnCFGOptimizer.h - Haydn CFG Optimizer -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a MachineFunctionPass that performs CFG simplification
// for the Haydn VLIW DSP target. The pass runs post-register-allocation and
// targets VLIW-specific patterns that generic LLVM passes may miss:
//
// Unreachable block elimination (recursive)
// Empty block forwarding (skip blocks that only contain an unconditional
// branch, reducing packetization pressure)
// Identical successor merging (conditional branch with the same true and
// false target becomes unconditional)
// Simple tail merging (blocks ending with identical branch sequences)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HAYDN_HAYDNCFGOPTIMIZER_H
#define LLVM_LIB_TARGET_HAYDN_HAYDNCFGOPTIMIZER_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class MachineBasicBlock;
class HaydnInstrInfo;

// HaydnCFGOptimizer - Simplifies the machine CFG by removing redundant
// blocks and branches. Runs after register allocation but before the VLIW
// packetizer so that packetization sees a minimal CFG with fewer blocks and
// branches to schedule.
class HaydnCFGOptimizer : public MachineFunctionPass {
public:
  static char ID;

  HaydnCFGOptimizer();

  StringRef getPassName() const override {
    return "Haydn CFG Optimizer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  const HaydnInstrInfo *HII = nullptr;

  // Remove blocks that have no predecessors (except the entry block).
  // Propagates: if removing a block makes its successors unreachable
  // removes those too. Returns true if any blocks were removed.
  bool eliminateUnreachableBlocks(MachineFunction &MF);

  // If a block contains only an unconditional branch (no side-effecting
  // instructions), redirect all predecessors to the branch target and
  // remove the block. Returns true if any blocks were forwarded.
  bool forwardEmptyBlocks(MachineFunction &MF);

  // If a conditional branch has the same true and false successor, replace
  // it with an unconditional branch to that successor. Returns true if any
  // branches were simplified.
  bool mergeIdenticalSuccessors(MachineFunction &MF);

  // If two (or more) blocks end with identical unconditional branch targets
  // or identical conditional branch (same opcode, operands, and target)
  // redirect the predecessors of all but the first to the first block.
  // Returns true if any blocks were merged.
  bool tailMergeBlocks(MachineFunction &MF);

  // Check whether a basic block is "empty" for forwarding purposes:
  // contains only debug instructions, CFI instructions, and a single
  // unconditional branch.
  bool isEmptyBlock(const MachineBasicBlock &MBB) const;

  // Check whether two terminators are identical (same opcode, same operands).
  // Used by tail merging.
  bool branchesAreIdentical(const MachineBasicBlock &MBB1,
                            const MachineBasicBlock &MBB2) const;
};

// Factory function for the pass.
FunctionPass *createHaydnCFGOptimizerPass();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HAYDN_HAYDNCFGOPTIMIZER_H
