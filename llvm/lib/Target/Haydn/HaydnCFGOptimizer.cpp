//===-- HaydnCFGOptimizer.cpp - Haydn CFG Optimizer -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a post-register-allocation pass that performs CFG
// simplification for the Haydn VLIW DSP target. The optimizations target
// patterns that are particularly beneficial for VLIW packetization:
//
// 1. Unreachable block elimination: removes blocks with no predecessors
// (except the entry block). Propagates recursively — if removing a
// block makes its successors unreachable, those are removed too.
//
// 2. Empty block forwarding: if a block contains only an unconditional
// branch (no side-effecting instructions), redirect all predecessors
// to the branch target directly. This eliminates unnecessary jumps
// and reduces the number of blocks the packetizer must consider.
//
// 3. Identical successor merging: if a conditional branch has the same
// true and false successor, replace it with an unconditional branch.
// This simplifies the CFG and removes a redundant branch instruction.
//
// 4. Simple tail merging: if two blocks end with identical branch
// sequences (same opcode, operands, and targets), redirect the
// predecessors of later blocks to the first one. This reduces code
// size and creates larger blocks that the packetizer can pack more
// efficiently.
//
// The pass requires NoVRegs (runs post-RA) so that it can safely redirect
// branches without worrying about PHI nodes or virtual register liveness.
//
//===----------------------------------------------------------------------===//

#include "HaydnCFGOptimizer.h"
#include "Haydn.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-cfg-opt"

using namespace llvm;

STATISTIC(NumUnreachableBlocksRemoved,
          "Number of unreachable blocks removed");
STATISTIC(NumEmptyBlocksForwarded,
          "Number of empty blocks forwarded");
STATISTIC(NumIdenticalSuccMerged,
          "Number of identical-successor branches simplified");
STATISTIC(NumBlocksTailMerged,
          "Number of blocks tail-merged");

//===----------------------------------------------------------------------===//
// Public interface
//===----------------------------------------------------------------------===//

char HaydnCFGOptimizer::ID = 0;

INITIALIZE_PASS(HaydnCFGOptimizer, "haydn-cfg-opt",
                "Haydn CFG Optimizer", false, false)

FunctionPass *llvm::createHaydnCFGOptimizerPass() {
  return new HaydnCFGOptimizer();
}

HaydnCFGOptimizer::HaydnCFGOptimizer() : MachineFunctionPass(ID) {}

void HaydnCFGOptimizer::getAnalysisUsage(AnalysisUsage &AU) const {
  // Erases blocks / rewires successors — do not claim MLI preserved.
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnCFGOptimizer::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "===== Haydn CFG Optimizer: " << MF.getName()
                     << " =====\n");

  const auto &STI = MF.getSubtarget<HaydnSubtarget>();
  HII = STI.getInstrInfo();

  bool Changed = false;
  bool LocalChanged;

  // Iterate until no more changes. Each optimization may enable new
  // opportunities for the others (e.g., removing an unreachable block may
  // create an empty block that can be forwarded).
  do {
    LocalChanged = false;

    // Phase 1: Remove unreachable blocks first, since they may contain
    // references to blocks that complicate later analyses.
    LocalChanged |= eliminateUnreachableBlocks(MF);

    // Phase 2: Merge identical successors (simplifies conditional branches
    // to unconditional, which may create empty blocks).
    LocalChanged |= mergeIdenticalSuccessors(MF);

    // Phase 3: Forward empty blocks (redirect through single-branch blocks).
    LocalChanged |= forwardEmptyBlocks(MF);

    // Phase 4: Tail merge blocks with identical terminators.
    LocalChanged |= tailMergeBlocks(MF);

    Changed |= LocalChanged;
  } while (LocalChanged);

  return Changed;
}

//===----------------------------------------------------------------------===//
// Unreachable block elimination
//===----------------------------------------------------------------------===//

bool HaydnCFGOptimizer::eliminateUnreachableBlocks(MachineFunction &MF) {
  // Collect all blocks that have at least one predecessor (other than
  // themselves). The entry block is always reachable.
  SmallPtrSet<MachineBasicBlock *, 16> Reachable;
  Reachable.insert(&MF.front());

  // Fixed-point iteration: a block is reachable if it is the entry block or
  // if at least one reachable predecessor branches to it.
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      if (Reachable.contains(&MBB))
        continue;

      for (MachineBasicBlock *Pred : MBB.predecessors()) {
        if (Reachable.contains(Pred)) {
          Reachable.insert(&MBB);
          Changed = true;
          break;
        }
      }
    }
  }

  // Collect unreachable blocks.
  SmallVector<MachineBasicBlock *, 8> Unreachable;
  for (MachineBasicBlock &MBB : MF) {
    if (!Reachable.contains(&MBB))
      Unreachable.push_back(&MBB);
  }

  if (Unreachable.empty())
    return false;

  LLVM_DEBUG(dbgs() << "  Removing " << Unreachable.size()
                     << " unreachable blocks\n");

  // Remove successor edges and erase from the function.
  for (MachineBasicBlock *MBB : Unreachable) {
    while (!MBB->succ_empty())
      MBB->removeSuccessor(MBB->succ_begin());
    MBB->eraseFromParent();
    ++NumUnreachableBlocksRemoved;
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Empty block forwarding
//===----------------------------------------------------------------------===//

bool HaydnCFGOptimizer::isEmptyBlock(const MachineBasicBlock &MBB) const {
  // Empty = only debug + a single unconditional branch.
  // CFI/EH labels are NOT empty: forwarding would drop unwind state.
  bool HasBranch = false;
  for (const MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isCFIInstruction() || MI.isEHLabel())
      return false;
    if (MI.isBranch()) {
      // Only accept a single unconditional branch.
      if (HasBranch)
        return false;
      if (!MI.isUnconditionalBranch())
        return false;
      HasBranch = true;
      continue;
    }
    // Any other instruction means the block is not empty.
    return false;
  }
  return HasBranch;
}

bool HaydnCFGOptimizer::forwardEmptyBlocks(MachineFunction &MF) {
  bool Changed = false;

  // Collect empty blocks and their targets. We iterate to a fixed point
  // because forwarding one empty block may make the target another empty
  // block (transitive forwarding).
  bool LocalChanged;
  do {
    LocalChanged = false;

    SmallVector<std::pair<MachineBasicBlock *, MachineBasicBlock *>, 8>
        ToForward;

    for (MachineBasicBlock &MBB : MF) {
      // Skip the entry block — it cannot be forwarded.
      if (MBB.isEntryBlock())
        continue;

      if (!isEmptyBlock(MBB))
        continue;

      // Find the unconditional branch target.
      MachineBasicBlock *Target = nullptr;
      for (const MachineInstr &MI : MBB) {
        if (MI.isDebugInstr())
          continue;
        // isEmptyBlock already rejected CFI/EH; still skip only debug here.
        if (MI.isUnconditionalBranch()) {
          Target = HII->getBranchDestBlock(MI);
          break;
        }
      }

      if (!Target || Target == &MBB)
        continue;

      // Do not forward if the target has address-taken (e.g., jump table
      // indirect branch target, or EH label).
      if (Target->isEHPad() || Target->isInlineAsmBrIndirectTarget())
        continue;

      // Do not forward if the block is an EH pad or has address-taken.
      if (MBB.isEHPad() || MBB.isInlineAsmBrIndirectTarget())
        continue;

      ToForward.emplace_back(&MBB, Target);
    }

    for (auto &[Block, Target] : ToForward) {
      // Re-check: the block may have been removed in a previous iteration.
      if (Block->getParent() == nullptr)
        continue;

      // Re-check that the block is still empty (predecessor redirection
      // could have changed it if another pass modified it concurrently).
      if (!isEmptyBlock(*Block))
        continue;

      // Check that the block still has a predecessor to redirect.
      if (Block->pred_empty())
        continue;

      LLVM_DEBUG(dbgs() << "  Forwarding empty block "
                         << printMBBReference(*Block) << " -> "
                         << printMBBReference(*Target) << "\n");

      // After erasing Block, a predecessor that currently falls through into
      // Block will fall through to Block's former layout successor instead.
      // ReplaceUsesOfBlockWith rewrites CFG edges and *explicit* MBB operands
      // only — it does not insert a branch for a fallthrough edge. If Target
      // is not that post-erase layout successor, leave an explicit B so the
      // fallthrough successor stays Target (yarpgen seed 2288).
      MachineBasicBlock *PostEraseLayoutNext = nullptr;
      {
        auto NextIt = std::next(Block->getIterator());
        if (NextIt != MF.end())
          PostEraseLayoutNext = &*NextIt;
      }

      SmallVector<MachineBasicBlock *, 4> Preds(Block->predecessors());
      for (MachineBasicBlock *Pred : Preds) {
        MachineBasicBlock *TBB = nullptr;
        MachineBasicBlock *FBB = nullptr;
        SmallVector<MachineOperand, 4> Cond;
        const bool CanAnalyze = !HII->analyzeBranch(*Pred, TBB, FBB, Cond);

        // Pred reaches Block by fallthrough (no explicit branch operand to
        // Block) iff Block is the layout successor and analyzeBranch reports
        // no branch to Block as TBB/FBB.
        const bool LayoutSucc = Pred->isLayoutSuccessor(Block);
        const bool ExplicitToBlock = (TBB == Block) || (FBB == Block);
        const bool FallThroughToBlock =
            CanAnalyze && LayoutSucc && Pred->isSuccessor(Block) &&
            !ExplicitToBlock &&
            ((Cond.empty() && !TBB) || (!Cond.empty() && !FBB));

        Pred->ReplaceUsesOfBlockWith(Block, Target);

        // Explicit branches were rewritten by ReplaceUsesOfBlockWith. Only
        // fallthrough preds need a terminator rewrite when Target will not
        // be the physical fallthrough after the erase.
        if (!FallThroughToBlock || Target == PostEraseLayoutNext)
          continue;

        LLVM_DEBUG(dbgs() << "    fix fallthrough pred "
                           << printMBBReference(*Pred) << " -> explicit B to "
                           << printMBBReference(*Target) << "\n");

        if (Cond.empty() && !TBB) {
          // Pure fallthrough → unconditional B to Target.
          HII->insertBranch(*Pred, Target, nullptr, Cond, DebugLoc());
        } else if (!Cond.empty() && !FBB) {
          // One-way conditional with fallthrough to Block. Taken stays TBB;
          // materialize B to Target as the false leg.
          HII->removeBranch(*Pred);
          HII->insertBranch(*Pred, TBB, Target, Cond, DebugLoc());
        }
      }

      // Remove the empty block. Clear successor edges before erasing.
      while (!Block->succ_empty())
        Block->removeSuccessor(Block->succ_begin());
      Block->eraseFromParent();
      ++NumEmptyBlocksForwarded;
      LocalChanged = true;
    }

    Changed |= LocalChanged;
  } while (LocalChanged);

  return Changed;
}

//===----------------------------------------------------------------------===//
// Identical successor merging
//===----------------------------------------------------------------------===//

bool HaydnCFGOptimizer::mergeIdenticalSuccessors(MachineFunction &MF) {
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Analyze the branch structure.
    MachineBasicBlock *TBB = nullptr;
    MachineBasicBlock *FBB = nullptr;
    SmallVector<MachineOperand, 4> Cond;

    // Skip blocks whose branches we cannot analyze.
    if (HII->analyzeBranch(MBB, TBB, FBB, Cond))
      continue;

    // Need a conditional branch (non-empty condition) with both successors
    // set and identical.
    if (Cond.empty() || !TBB || !FBB)
      continue;

    if (TBB != FBB)
      continue;

    LLVM_DEBUG(dbgs() << "  Merging identical successors in "
                       << printMBBReference(MBB) << " -> "
                       << printMBBReference(*TBB) << "\n");

    // Remove the conditional branch and replace with an unconditional branch.
    HII->removeBranch(MBB);
    HII->insertBranch(MBB, TBB, nullptr, {}, DebugLoc());

    // Drop the duplicate successor entry left over from the conditional
    // branch. analyzeBranch with TBB==FBB==X reports two successors to X;
    // after the unconditional rewrite only one should remain. Leaving the
    // duplicate causes MachineBlockPlacement::updateTerminator to assert
    // (isSuccessor(PreviousLayoutSuccessor) fires on the stale edge).
    unsigned NumSuccTo = 0;
    for (MachineBasicBlock *Succ : MBB.successors())
      if (Succ == TBB) ++NumSuccTo;
    while (NumSuccTo > 1) {
      for (auto It = MBB.succ_begin(), End = MBB.succ_end(); It != End; ++It) {
        if (*It == TBB) {
          MBB.removeSuccessor(It);
          --NumSuccTo;
          break;
        }
      }
    }

    ++NumIdenticalSuccMerged;
    Changed = true;
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Simple tail merging
//===----------------------------------------------------------------------===//

bool HaydnCFGOptimizer::branchesAreIdentical(
    const MachineBasicBlock &MBB1, const MachineBasicBlock &MBB2) const {
  // Collect the terminators from each block, skipping debug/CFI instructions.
  SmallVector<const MachineInstr *, 4> Terms1;
  SmallVector<const MachineInstr *, 4> Terms2;

  for (const MachineInstr &MI : MBB1.terminators()) {
    if (MI.isDebugInstr())
      continue;
    Terms1.push_back(&MI);
  }
  for (const MachineInstr &MI : MBB2.terminators()) {
    if (MI.isDebugInstr())
      continue;
    Terms2.push_back(&MI);
  }

  if (Terms1.size() != Terms2.size())
    return false;

  for (unsigned I = 0, E = Terms1.size(); I != E; ++I) {
    const MachineInstr &MI1 = *Terms1[I];
    const MachineInstr &MI2 = *Terms2[I];

    // Must be the same opcode.
    if (MI1.getOpcode() != MI2.getOpcode())
      return false;

    // Must have the same number of operands.
    if (MI1.getNumOperands() != MI2.getNumOperands())
      return false;

    // Compare all operands.
    for (unsigned OpIdx = 0, OpEnd = MI1.getNumOperands(); OpIdx != OpEnd;
         ++OpIdx) {
      const MachineOperand &Op1 = MI1.getOperand(OpIdx);
      const MachineOperand &Op2 = MI2.getOperand(OpIdx);

      if (Op1.getType() != Op2.getType())
        return false;

      switch (Op1.getType()) {
      case MachineOperand::MO_Register:
        if (Op1.getReg() != Op2.getReg() ||
            Op1.getSubReg() != Op2.getSubReg())
          return false;
        break;
      case MachineOperand::MO_Immediate:
        if (Op1.getImm() != Op2.getImm())
          return false;
        break;
      case MachineOperand::MO_MachineBasicBlock:
        // Both must target the same basic block.
        if (Op1.getMBB() != Op2.getMBB())
          return false;
        break;
      case MachineOperand::MO_FrameIndex:
        if (Op1.getIndex() != Op2.getIndex())
          return false;
        break;
      case MachineOperand::MO_RegisterMask:
        // Register masks are pointer-comparable (they are uniqued).
        if (Op1.getRegMask() != Op2.getRegMask())
          return false;
        break;
      default:
        // For other operand types, be conservative and return false.
        return false;
      }
    }
  }

  return true;
}

bool HaydnCFGOptimizer::tailMergeBlocks(MachineFunction &MF) {
  bool Changed = false;

  // Simple O(n^2) approach: for each pair of blocks, check if they have
  // identical terminators. If so, redirect predecessors of one to the other.
  // This is sufficient for the typical patterns produced by GISel.

  SmallVector<MachineBasicBlock *, 32> Blocks;
  for (MachineBasicBlock &MBB : MF) {
    // Skip blocks that are EH pads or address-taken.
    if (MBB.isEHPad() || MBB.isInlineAsmBrIndirectTarget())
      continue;
    // Skip the entry block — its predecessors are implicit.
    if (MBB.isEntryBlock())
      continue;
    // Only consider blocks with at least one predecessor.
    if (MBB.pred_empty())
      continue;
    Blocks.push_back(&MBB);
  }

  SmallPtrSet<MachineBasicBlock *, 16> Merged;

  for (unsigned I = 0, E = Blocks.size(); I != E; ++I) {
    MachineBasicBlock *BB1 = Blocks[I];

    // Skip if already merged into another block.
    if (Merged.contains(BB1))
      continue;

    // Skip blocks with no terminators (fall-through only, no branch to merge).
    if (BB1->getFirstTerminator() == BB1->end())
      continue;

    // Only merge blocks that are analyzable.
    MachineBasicBlock *TBB1 = nullptr;
    MachineBasicBlock *FBB1 = nullptr;
    SmallVector<MachineOperand, 4> Cond1;
    if (HII->analyzeBranch(*BB1, TBB1, FBB1, Cond1))
      continue;

    for (unsigned J = I + 1; J != E; ++J) {
      MachineBasicBlock *BB2 = Blocks[J];

      if (Merged.contains(BB2))
        continue;

      // Skip blocks with different sizes (quick filter).
      if (BB1->size() != BB2->size())
        continue;

      if (!branchesAreIdentical(*BB1, *BB2))
        continue;

      // Also check that the non-terminator content is compatible.
      // For safety, only merge blocks that have the same number of
      // non-terminator instructions. This is conservative but correct.
      // A full tail merge would compare instruction-by-instruction
      // but that requires PHI insertion and is better left to the
      // generic MachineBlockPlacement pass.
      auto CountNonTerms = [](const MachineBasicBlock &MBB) {
        unsigned Count = 0;
        for (const MachineInstr &MI : MBB) {
          if (!MI.isTerminator() && !MI.isDebugInstr() &&
              !MI.isCFIInstruction())
            ++Count;
        }
        return Count;
      };

      if (CountNonTerms(*BB1) != CountNonTerms(*BB2))
        continue;

      // Both blocks must have identical non-terminator content for a safe
      // merge. Since we only redirect predecessors (not move instructions)
      // we require the blocks to have the SAME instructions (not just the
      // same count). For now, be very conservative: only merge blocks
      // that have zero non-terminator instructions (i.e., they consist
      // entirely of terminators + debug/CFI). This covers the common case
      // of blocks that are pure branch trampolines.
      if (CountNonTerms(*BB1) != 0)
        continue;

      LLVM_DEBUG(dbgs() << "  Tail-merging "
                         << printMBBReference(*BB2) << " into "
                         << printMBBReference(*BB1) << "\n");

      // Redirect all predecessors of BB2 to BB1. Same fallthrough hazard as
      // forwardEmptyBlocks : ReplaceUsesOfBlockWith rewrites CFG +
      // explicit MBB operands only. If Pred falls through into BB2 and BB1
      // is not the post-erase layout successor, insert an explicit branch.
      MachineBasicBlock *PostEraseLayoutNext = nullptr;
      {
        auto NextIt = std::next(BB2->getIterator());
        if (NextIt != MF.end())
          PostEraseLayoutNext = &*NextIt;
      }

      SmallVector<MachineBasicBlock *, 4> Preds(BB2->predecessors());
      for (MachineBasicBlock *Pred : Preds) {
        MachineBasicBlock *TBB = nullptr;
        MachineBasicBlock *FBB = nullptr;
        SmallVector<MachineOperand, 4> Cond;
        const bool CanAnalyze = !HII->analyzeBranch(*Pred, TBB, FBB, Cond);
        const bool LayoutSucc = Pred->isLayoutSuccessor(BB2);
        const bool ExplicitToBB2 = (TBB == BB2) || (FBB == BB2);
        const bool FallThroughToBB2 =
            CanAnalyze && LayoutSucc && Pred->isSuccessor(BB2) &&
            !ExplicitToBB2 &&
            ((Cond.empty() && !TBB) || (!Cond.empty() && !FBB));

        Pred->ReplaceUsesOfBlockWith(BB2, BB1);

        if (!FallThroughToBB2 || BB1 == PostEraseLayoutNext)
          continue;

        if (Cond.empty() && !TBB) {
          HII->insertBranch(*Pred, BB1, nullptr, Cond, DebugLoc());
        } else if (!Cond.empty() && !FBB) {
          HII->removeBranch(*Pred);
          HII->insertBranch(*Pred, TBB, BB1, Cond, DebugLoc());
        }
      }

      // BB2 is now unreachable; it will be cleaned up by the unreachable
      // block elimination phase in the next iteration.
      Merged.insert(BB2);
      ++NumBlocksTailMerged;
      Changed = true;
    }
  }

  return Changed;
}
