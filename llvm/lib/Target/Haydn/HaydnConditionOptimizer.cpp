//===-- HaydnConditionOptimizer.cpp - Haydn Condition Opt -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a post-register-allocation pass that simplifies
// comparison patterns for the Haydn VLIW DSP target. The optimizations
// target patterns that reduce instruction count and improve VLIW
// packetization:
//
// 1. Identity comparison elimination: SLT32/SLTU32 r, rX, rX (same
// register compared to itself) always produces 0, since x < x is
// always false. Replace the comparison with SUB32 r, r, r (== 0).
//
// 2. Inverse comparison reuse: REMOVED.
// (y < x) == !(x < y) is false when x == y; the old
// rewrite had no dominating inequality proof and miscompiled equality.
//
// 3. Cmp+branch folding: When a comparison instruction's result feeds
// (possibly through xor-with-1) into a BNEZ/BEQZ branch and the
// result is otherwise dead, the sequence can be replaced with a single
// two-register branch. Mirrors AIE `BrcondXorCmpPat` (absorb
// xor(setcc,1) by inverting the branch) and RISCV GISel NeedInvert→XORI.
//
// Pattern A (direct):
// CMP rd, rA, rB + BNEZ rd → branch on "result is true"
// CMP rd, rA, rB + BEQZ rd → branch on "result is false"
//
// Pattern B (logical-not of 0/1 cmp result — GISel emitInvert01):
// CMP rd, rA, rB + XORI32 re, rd, 1 + BEQZ/BNEZ re
// CMP rd, rA, rB + [ADDI One,R0,1] + XOR32 re, rd, One + BEQZ/BNEZ
// Only match when the XOR is *proven* xor-with-1 (imm or ADDI/ADDI_W).
//
// Folded mappings:
// SEQ32 + BNEZ -> BEQ (rA == rB)
// SEQ32 + BEQZ -> BNE (rA != rB)
// SEQ32 + XOR+BEQZ -> BEQ (rA == rB)
// SEQ32 + XOR+BNEZ -> BNE (rA != rB)
// SLT32 + BNEZ → BLT (rA < rB, signed)
// SLT32 + BEQZ → BGE (rA >= rB, signed)
// SLT32 + XOR+BEQZ → BLT (rA < rB, signed)
// SLT32 + XOR+BNEZ → BGE (rA >= rB, signed)
// SLTU32 + BNEZ → BLTU (rA < rB, unsigned)
// SLTU32 + BEQZ → BGEU (rA >= rB, unsigned)
// SLTU32 + XOR+BEQZ → BLTU (rA < rB, unsigned)
// SLTU32 + XOR+BNEZ → BGEU (rA >= rB, unsigned)
//
// 4. Zero-branch narrowing: A two-register conditional branch whose
// operand(s) include the soft-zero register R0 is rewritten to the
// smaller single-register zero-test form (spec rows 44-53):
// BEQ rA, R0, tgt → BEQZ rA, tgt (R0 on either side)
// BNE rA, R0, tgt → BNEZ rA, tgt
// BGE rA, R0, tgt → BGEZ rA, tgt (signed: rA >= 0)
// BLT rA, R0, tgt → BLTZ rA, tgt (signed: rA < 0)
// The single-register form uses a narrower encoding and drops one GPR
// read port, helping the 4R2W VLIW packetizer constraint. Unsigned
// variants (BGEU/BLTU) are left alone because the ISA exposes only
// signed BGEZ/BLTZ.
//
// The pass requires NoVRegs (runs post-RA) so that physical registers are
// available and the register mapping is stable.
//
//===----------------------------------------------------------------------===//

#include "HaydnConditionOptimizer.h"
#include "Haydn.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-cond-opt"

using namespace llvm;

STATISTIC(NumSelfComparisonsEliminated,
          "Number of self-comparisons eliminated");
STATISTIC(NumInverseComparisonsReused,
          "Number of inverse comparisons reused");
STATISTIC(NumCmpBranchFolded,
          "Number of comparison+branch pairs folded");
STATISTIC(NumBranchesNarrowedToZero,
          "Number of two-register branches narrowed to BEQZ/BNEZ/BGEZ/BLTZ");

//===----------------------------------------------------------------------===//
// Public interface
//===----------------------------------------------------------------------===//

char HaydnConditionOptimizer::ID = 0;

INITIALIZE_PASS(HaydnConditionOptimizer, "haydn-cond-opt",
                "Haydn Condition Optimizer", false, false)

FunctionPass *llvm::createHaydnConditionOptimizerPass() {
  return new HaydnConditionOptimizer();
}

HaydnConditionOptimizer::HaydnConditionOptimizer()
    : MachineFunctionPass(ID) {}

void HaydnConditionOptimizer::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnConditionOptimizer::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "===== Haydn Condition Optimizer: " << MF.getName()
                     << " =====\n");

  const auto &STI = MF.getSubtarget<HaydnSubtarget>();
  HII = STI.getInstrInfo();

  // Live rules only (peep split residual): self-compare → SUB zeroing;
  // branch-to-zero narrowing. Inverse SLT / foldCmpBranch stay disabled.
  bool Changed = false;
  Changed |= eliminateSelfComparisons(MF);
  Changed |= narrowBranchToZero(MF);

  return Changed;
}

//===----------------------------------------------------------------------===//
// Identity comparison elimination
//===----------------------------------------------------------------------===//

bool HaydnConditionOptimizer::eliminateSelfComparisons(MachineFunction &MF) {
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    SmallVector<MachineInstr *, 4> ToRemove;

    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();

      // Match SLT32 r, rX, rX or SLTU32 r, rX, rX.
      if (Opc != Haydn::SLT32 && Opc != Haydn::SLTU32)
        continue;

      // Need at least 3 operands: dst, src1, src2.
      if (MI.getNumOperands() < 3)
        continue;

      const MachineOperand &Dst = MI.getOperand(0);
      const MachineOperand &Src1 = MI.getOperand(1);
      const MachineOperand &Src2 = MI.getOperand(2);

      // Both sources must be physical registers.
      if (!Src1.isReg() || !Src2.isReg())
        continue;
      if (!Src1.getReg().isPhysical() || !Src2.getReg().isPhysical())
        continue;

      // Self-comparison: src1 == src2.
      if (Src1.getReg() != Src2.getReg())
        continue;

      if (!Dst.isReg() || !Dst.getReg().isPhysical())
        continue;

      Register DstReg = Dst.getReg();
      if (!Haydn::GPR32RegClass.contains(DstReg))
        continue;

      LLVM_DEBUG(dbgs() << "  Eliminating self-comparison: " << MI);

      // Build SUB32 dst, src, src → dst = 0. Never use dst as source:
      // dst may be undefined / not equal to the compared register.
      Register SrcReg = Src1.getReg();
      DebugLoc DL = MI.getDebugLoc();
      BuildMI(MBB, MI, DL, HII->get(Haydn::SUB32), DstReg)
          .addReg(SrcReg)
          .addReg(SrcReg);

      ++NumSelfComparisonsEliminated;
      ToRemove.push_back(&MI);
      Changed = true;
    }

    // Erase in reverse order to preserve iterator validity.
    for (MachineInstr *MI : reverse(ToRemove))
      MI->eraseFromParent();
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Inverse comparison reuse
//===----------------------------------------------------------------------===//

bool HaydnConditionOptimizer::reuseInverseComparisons(MachineFunction &MF) {
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Collect all SLT32 instructions in this block.
    // Key: (src1, src2) pair → defining instruction.
    struct CmpKey {
      Register Src1;
      Register Src2;

      bool operator==(const CmpKey &Other) const {
        return Src1 == Other.Src1 && Src2 == Other.Src2;
      }
    };

    struct CmpKeyInfo {
      static inline CmpKey getEmptyKey() {
        return {Register(~0u), Register(~0u)};
      }
      static inline CmpKey getTombstoneKey() {
        return {Register(~1u), Register(~1u)};
      }
      static unsigned getHashValue(const CmpKey &K) {
        return hash_combine(K.Src1.id(), K.Src2.id());
      }
      static bool isEqual(const CmpKey &LHS, const CmpKey &RHS) {
        return LHS == RHS;
      }
    };

    // Map from (src1, src2) → (def instruction, dst register).
    DenseMap<CmpKey, std::pair<MachineInstr *, Register>, CmpKeyInfo> CmpMap;

    SmallVector<MachineInstr *, 4> ToRemove;

    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != Haydn::SLT32)
        continue;

      if (MI.getNumOperands() < 3)
        continue;

      const MachineOperand &Dst = MI.getOperand(0);
      const MachineOperand &Src1 = MI.getOperand(1);
      const MachineOperand &Src2 = MI.getOperand(2);

      if (!Dst.isReg() || !Src1.isReg() || !Src2.isReg())
        continue;
      if (!Dst.getReg().isPhysical() || !Src1.getReg().isPhysical() ||
          !Src2.getReg().isPhysical())
        continue;

      Register DstReg = Dst.getReg();
      Register S1 = Src1.getReg();
      Register S2 = Src2.getReg();

      // Skip self-comparisons (already handled by
      // eliminateSelfComparisons).
      if (S1 == S2)
        continue;

      if (!Haydn::GPR32RegClass.contains(DstReg))
        continue;

      CmpKey Forward = {S1, S2};
      CmpKey Inverse = {S2, S1};

      auto It = CmpMap.find(Inverse);
      if (It != CmpMap.end()) {
        // Found an inverse: SLT32 rA, S2, S1 already exists.
        // Current: SLT32 DstReg, S1, S2 = !(SLT32 rA, S2, S1).
        // Replace with XORI32 DstReg, rA, 1.
        MachineInstr *PrevMI = It->second.first;
        Register PrevDst = It->second.second;

        // The previous comparison's destination must still be valid.
        if (PrevMI->getParent() != &MBB)
          continue;

        // Check that PrevDst AND the source operands are not redefined
        // between PrevMI and MI. The inverse-reuse identity
        // SLT32 DstReg, S1, S2 == !(SLT32 PrevDst, S2, S1)
        // only holds if S1 and S2 still hold the SAME values they had at
        // PrevMI. After regalloc these physical registers are routinely
        // reloaded with new values (fully-unrolled reductions reload r1/r2
        // every iteration), so a name-only CSE keyed on (S1,S2) reuses a
        // comparison between completely different operands.
        bool Clobbered = false;
        for (auto Iter = std::next(PrevMI->getIterator());
             Iter != MI.getIterator() && Iter != MBB.end(); ++Iter) {
          for (const MachineOperand &MO : Iter->all_defs()) {
            if (!MO.isReg())
              continue;
            Register DefReg = MO.getReg();
            if (DefReg == PrevDst || DefReg == S1 || DefReg == S2) {
              Clobbered = true;
              break;
            }
          }
          if (Clobbered)
            break;
        }
        if (Clobbered)
          continue;

        // DstReg must not be PrevDst (would create a pointless XOR).
        if (DstReg == PrevDst)
          continue;

        LLVM_DEBUG({
          dbgs() << "  Reusing inverse comparison:\n";
          dbgs() << "    Prev: " << *PrevMI;
          dbgs() << "    Curr: " << MI;
        });

        // Replace with XORI32 DstReg, PrevDst, 1.
        DebugLoc DL = MI.getDebugLoc();
        BuildMI(MBB, MI, DL, HII->get(Haydn::XORI32), DstReg)
            .addReg(PrevDst)
            .addImm(1);

        ++NumInverseComparisonsReused;
        ToRemove.push_back(&MI);
        Changed = true;
      } else {
        // Record this comparison for future inverse detection.
        CmpMap[Forward] = {&MI, DstReg};
      }
    }

    // Erase in reverse order.
    for (MachineInstr *MI : reverse(ToRemove))
      MI->eraseFromParent();
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Cmp+branch folding
//===----------------------------------------------------------------------===//

// Determine the folded two-register branch opcode for a given comparison
// opcode and effective branch sense. When Inverted is false, the branch
// fires when the comparison result is true (non-zero); when Inverted is
// true, the branch fires when the result is false (zero).
// Examples:
// SEQ32 + !Inverted (BNEZ) -> BEQ (branch when rA == rB)
// SEQ32 + Inverted (BEQZ) -> BNE (branch when rA != rB)
// SLT32 + !Inverted (BNEZ) → BLT (branch when rA < rB)
// SLT32 + Inverted (BEQZ) → BGE (branch when rA >= rB)
static unsigned getFoldedBranchOpcode(unsigned CmpOpc, bool Inverted) {
  switch (CmpOpc) {
  case Haydn::SEQ32:
    // SEQ32 rd = (rs == rt). BNEZ (fire on true == equal) -> BEQ;
    // BEQZ (fire on false == not-equal) -> BNE. The previous mapping
    // (BNE/BEQ) was polarity-swapped, inverting every == / != branch.
    return Inverted ? Haydn::BNE : Haydn::BEQ;
  case Haydn::SLT32:
    return Inverted ? Haydn::BGE : Haydn::BLT;
  case Haydn::SLTU32:
    return Inverted ? Haydn::BGEU : Haydn::BLTU;
  default:
    return 0;
  }
}

// Check whether a register is dead after the given instruction.
// A register is dead after \p MI only if (a) it has no use in the rest of
// the same basic block AND (b) it is not live-out of the block (no successor
// block reads it). The cross-block check is mandatory: when foldCmpBranch
// deletes the defining CMP, the destination register keeps whatever stale
// value it held before the CMP. If a successor block reads that register
// (carrying the CMP result forward, e.g. a found-flag summed after a loop)
// the stale value corrupts the successor.
static bool isRegDeadAfter(const TargetRegisterInfo *TRI, Register Reg,
                           MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  auto It = std::next(MI.getIterator());
  auto End = MBB->end();
  for (; It != End; ++It) {
    for (const MachineOperand &MO : It->operands()) {
      if (MO.isReg() && MO.getReg() == Reg && MO.isUse())
        return false;
    }
  }
  // No intra-block use. Now verify the register is not live-out of the block:
  // if any successor reads it, deleting the CMP would leak a stale value.
  LivePhysRegs LiveOuts(*TRI);
  LiveOuts.addLiveOutsNoPristines(*MBB);
  if (LiveOuts.contains(Reg))
    return false;
  return true;
}

// Check that no instruction between \p From (exclusive) and \p To
// (exclusive) clobbers either \p RegA or \p RegB.
static bool srcRegsNotClobbered(const TargetRegisterInfo *TRI,
                                MachineBasicBlock::iterator From,
                                MachineBasicBlock::iterator To,
                                Register RegA, Register RegB) {
  LiveRegUnits LRUs(*TRI);
  LRUs.clear();
  for (auto It = std::next(From); It != To && It != From->getParent()->end();
       ++It) {
    LiveRegUnits::accumulateUsedDefed(*It, LRUs, LRUs, TRI);
    if (!LRUs.available(RegA) || !LRUs.available(RegB))
      return false;
  }
  return true;
}

// True if \p MI is ADDI32/ADDI32_W defining \p Reg as R0 + imm 1.
static bool isMaterializeOne(const MachineInstr &MI, Register Reg) {
  unsigned Opc = MI.getOpcode();
  if (Opc != Haydn::ADDI32)
    return false;
  if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
    return false;
  return MI.getOperand(0).getReg() == Reg &&
         MI.getOperand(1).getReg() == Haydn::R0 &&
         MI.getOperand(2).getImm() == 1;
}

// Walk backward from \p From (exclusive) toward block start for a unique
// dominating materialize-one of \p OneReg with no redef of OneReg in between.
static bool isProvenConstantOne(Register OneReg, const MachineInstr &FromMI) {
  if (!OneReg.isPhysical() || OneReg == Haydn::R0)
    return false;
  const MachineBasicBlock *MBB = FromMI.getParent();
  for (auto It = FromMI.getIterator(); It != MBB->begin();) {
    --It;
    // Redef of OneReg before we see mat-one → not proven.
    for (const MachineOperand &MO : It->all_defs()) {
      if (MO.isReg() && MO.getReg() == OneReg) {
        return isMaterializeOne(*It, OneReg);
      }
    }
  }
  return false;
}

// Match logical-not of a 0/1 cmp result: XORI32 Out, CmpDst, 1 or
// XOR32 Out, CmpDst, One with One proven == 1. Returns true and sets OutReg.
// Mirrors AIE xor(setcc,1) and RISCV NeedInvert→XORI; refuses unproven XOR32.
static bool matchInvert01OfCmp(const MachineInstr &MI, Register CmpDst,
                               Register &OutReg) {
  if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg())
    return false;
  OutReg = MI.getOperand(0).getReg();
  if (!OutReg.isPhysical())
    return false;

  // Primary (emitInvert01 / CondOpt inverse-reuse): XORI32 Out, CmpDst, 1.
  if (MI.getOpcode() == Haydn::XORI32) {
    if (!MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
      return false;
    return MI.getOperand(1).getReg() == CmpDst &&
           MI.getOperand(2).getImm() == 1;
  }

  // Legacy dual-instr invert: XOR32 Out, CmpDst, One (or swapped srcs).
  if (MI.getOpcode() != Haydn::XOR32)
    return false;
  if (!MI.getOperand(1).isReg() || !MI.getOperand(2).isReg())
    return false;
  Register S0 = MI.getOperand(1).getReg();
  Register S1 = MI.getOperand(2).getReg();
  Register OneReg;
  if (S0 == CmpDst)
    OneReg = S1;
  else if (S1 == CmpDst)
    OneReg = S0;
  else
    return false;
  return isProvenConstantOne(OneReg, MI);
}

bool HaydnConditionOptimizer::foldCmpBranch(MachineFunction &MF) {
  const auto *TRI = MF.getSubtarget<HaydnSubtarget>().getRegisterInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Walk the block and look for comparison instructions followed by
    // conditional branches. Two patterns are handled:
    //
    // Pattern A (direct): CMP rd, rA, rB → BNEZ/BEQZ rd, target
    // Pattern B (xor-1): CMP rd, rA, rB → XORI32 re, rd, 1
    // → BNEZ/BEQZ re, target
    // (or ADDI One,R0,1 + XOR32 re, rd, One)
    //
    // For Pattern A: BEQZ means "branch when result is 0" (inverted sense)
    // BNEZ means "branch when result is non-zero" (normal).
    // For Pattern B: The XOR inverts the boolean, so BEQZ after XOR means
    // "branch when original result was non-zero" (normal)
    // and BNEZ after XOR means "branch when original was 0"
    // (inverted).

    SmallVector<MachineInstr *, 8> ToRemove;

    for (auto It = MBB.begin(), End = MBB.end(); It != End; ++It) {
      MachineInstr &MI = *It;
      unsigned Opc = MI.getOpcode();

      // Only interested in comparison instructions.
      if (Opc != Haydn::SEQ32 && Opc != Haydn::SLT32 && Opc != Haydn::SLTU32)
        continue;

      if (MI.getNumOperands() < 3)
        continue;

      const MachineOperand &DstOp = MI.getOperand(0);
      const MachineOperand &Src1Op = MI.getOperand(1);
      const MachineOperand &Src2Op = MI.getOperand(2);

      if (!DstOp.isReg() || !Src1Op.isReg() || !Src2Op.isReg())
        continue;
      if (!DstOp.getReg().isPhysical() || !Src1Op.getReg().isPhysical() ||
          !Src2Op.getReg().isPhysical())
        continue;

      Register CmpDst = DstOp.getReg();
      Register SrcReg1 = Src1Op.getReg();
      Register SrcReg2 = Src2Op.getReg();

      LLVM_DEBUG(dbgs() << "  Found CMP: " << MI);

      // If the CMP destination aliases either source register, the CMP
      // clobbers that source. After folding, the two-register branch would
      // use the clobbered register, producing wrong results. Skip.
      if (TRI->regsOverlap(CmpDst, SrcReg1) || TRI->regsOverlap(CmpDst, SrcReg2))
        continue;

      // Scan forward looking for a BNEZ/BEQZ that tests the comparison
      // result (possibly through a proven xor-with-1).
      auto ScanIt = std::next(It);
      if (ScanIt == End)
        continue;

      MachineInstr *XorMI = nullptr;
      MachineInstr *MatOneMI = nullptr; // optional ADDI One,R0,1 before XOR32
      Register BranchReg = CmpDst;

      while (ScanIt != End &&
             (ScanIt->isDebugInstr() || ScanIt->isCFIInstruction()))
        ++ScanIt;
      if (ScanIt == End)
        continue;

      MachineInstr *NextMI = &*ScanIt;
      LLVM_DEBUG(dbgs() << "    Next after CMP: " << *NextMI);

      // Optional: ADDI One, R0, 1 between CMP and XOR32 (legacy dual-path).
      // Do not skip arbitrary instructions — only pure mat-one of a new reg.
      if ((NextMI->getOpcode() == Haydn::ADDI32 ||
           NextMI->getOpcode() == Haydn::ADDI32) &&
          NextMI->getNumOperands() >= 3 && NextMI->getOperand(0).isReg() &&
          isMaterializeOne(*NextMI, NextMI->getOperand(0).getReg()) &&
          NextMI->getOperand(0).getReg() != CmpDst) {
        MatOneMI = NextMI;
        ++ScanIt;
        while (ScanIt != End &&
               (ScanIt->isDebugInstr() || ScanIt->isCFIInstruction()))
          ++ScanIt;
        if (ScanIt == End)
          continue;
        NextMI = &*ScanIt;
      }

      Register XorOut;
      if (matchInvert01OfCmp(*NextMI, CmpDst, XorOut)) {
        // Cmp sources must stay live through the invert.
        if (!srcRegsNotClobbered(TRI, It, ScanIt, SrcReg1, SrcReg2))
          continue;

        // CmpDst may only be used by the invert (and optional mat-one must
        // not read CmpDst — already ensured above).
        bool CmpResultHasOtherUse = false;
        for (auto CheckIt = std::next(It);
             CheckIt != ScanIt && CheckIt != MBB.end(); ++CheckIt) {
          if (MatOneMI && &*CheckIt == MatOneMI)
            continue;
          for (const MachineOperand &MO : CheckIt->operands()) {
            if (MO.isReg() && MO.isUse() && MO.getReg() == CmpDst) {
              if (&*CheckIt != NextMI)
                CmpResultHasOtherUse = true;
              break;
            }
          }
          if (CmpResultHasOtherUse)
            break;
        }
        if (CmpResultHasOtherUse)
          continue;

        XorMI = NextMI;
        BranchReg = XorOut;

        ++ScanIt;
        while (ScanIt != End &&
               (ScanIt->isDebugInstr() || ScanIt->isCFIInstruction()))
          ++ScanIt;
        if (ScanIt == End)
          continue;
        NextMI = &*ScanIt;
      } else if (MatOneMI) {
        // Mat-one without a following invert is not Pattern B.
        continue;
      }

      // Now NextMI should be a BNEZ/BEQZ testing BranchReg.
      unsigned BrOpc = NextMI->getOpcode();
      if (BrOpc != Haydn::BNEZ && BrOpc != Haydn::BEQZ)
        continue;

      if (NextMI->getOperand(0).getReg() != BranchReg)
        continue;

      // Determine the effective sense. For the direct pattern:
      // BNEZ = branch when non-zero = "true" sense (not inverted)
      // BEQZ = branch when zero = "false" sense (inverted)
      // For the XOR pattern, the XOR already inverted, so:
      // BNEZ after XOR = branch when original was false (inverted)
      // BEQZ after XOR = branch when original was true (not inverted)
      bool EffectiveInverted;
      if (XorMI) {
        // XOR inverts once; the branch inverts again.
        // BEQZ after XOR = original true → not inverted relative to CMP
        // BNEZ after XOR = original false → inverted relative to CMP
        EffectiveInverted = (BrOpc == Haydn::BNEZ);
      } else {
        // Direct: BEQZ = inverted, BNEZ = not inverted
        EffectiveInverted = (BrOpc == Haydn::BEQZ);
      }

      unsigned FoldedOpc = getFoldedBranchOpcode(Opc, EffectiveInverted);
      if (!FoldedOpc)
        continue;

      // Verify source registers not clobbered between CMP and branch.
      auto BranchIt = ScanIt;
      if (!srcRegsNotClobbered(TRI, It, BranchIt, SrcReg1, SrcReg2))
        continue;

      // The register tested by the branch must be dead after the branch
      // (both intra-block and not live-out). For the XOR pattern, BranchReg
      // is the XOR output (may == CmpDst). For the direct pattern
      // BranchReg == CmpDst. When BranchReg == CmpDst, deleting the CMP
      // would leave CmpDst holding a stale value — only safe if no successor
      // block reads it.
      if (!isRegDeadAfter(TRI, BranchReg, *NextMI))
        continue;

      // For the direct pattern (no XOR), also verify CmpDst is not used
      // by anything other than the branch between CMP and branch.
      if (!XorMI) {
        bool CmpUsedElsewhere = false;
        for (auto CheckIt = std::next(It); CheckIt != ScanIt && CheckIt != MBB.end();
             ++CheckIt) {
          if (&*CheckIt == NextMI)
            continue; // The branch itself is fine
          for (const MachineOperand &MO : CheckIt->operands()) {
            if (MO.isReg() && MO.isUse() && MO.getReg() == CmpDst) {
              CmpUsedElsewhere = true;
              break;
            }
          }
          if (CmpUsedElsewhere)
            break;
        }
        if (CmpUsedElsewhere)
          continue;
      }

      LLVM_DEBUG({
        dbgs() << "  Folding cmp+branch:\n";
        dbgs() << "    Cmp: " << MI;
        if (XorMI)
          dbgs() << "    Xor: " << *XorMI;
        dbgs() << "    Br:  " << *NextMI;
        dbgs() << "    → folded branch\n";
      });

      // Build the folded two-register branch.
      MachineBasicBlock *Target = NextMI->getOperand(1).getMBB();
      DebugLoc DL = NextMI->getDebugLoc();
      BuildMI(MBB, NextMI, DL, HII->get(FoldedOpc))
          .addReg(SrcReg1)
          .addReg(SrcReg2)
          .addMBB(Target);

      // Remove the original branch.
      ToRemove.push_back(NextMI);

      // Remove the invert (and optional mat-one) if present.
      if (XorMI)
        ToRemove.push_back(XorMI);
      if (MatOneMI)
        ToRemove.push_back(MatOneMI);

      // Remove the comparison.
      ToRemove.push_back(&MI);

      ++NumCmpBranchFolded;
      Changed = true;
    }

    // Erase in reverse order to preserve iterator validity.
    for (MachineInstr *MI : reverse(ToRemove))
      MI->eraseFromParent();
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Zero-branch narrowing
//===----------------------------------------------------------------------===//

// Map a two-register branch opcode to its single-register zero-test form
// when the second operand is the soft-zero R0. Returns 0 if the branch
// does not have a zero-test equivalent in the ISA.
// Mapping (per spec VLIW_Engine_ISA_Reference.md rows 44-53):
// BEQ rA, R0 → BEQZ rA (equality is symmetric)
// BNE rA, R0 → BNEZ rA
// BGE rA, R0 → BGEZ rA (signed; rA >= 0)
// BLT rA, R0 → BLTZ rA (signed; rA < 0)
// BGEU/BLTU are not mapped: the ISA only exposes signed BGEZ/BLTZ, and
// rewriting an unsigned compare-against-zero to a signed form would change
// the observable behavior for the sign bit (irrelevant in practice since
// both produce the same result against zero, but we stay conservative to
// keep the rewrite semantics pure).
static unsigned getZeroBranchOpcode(unsigned BrOpc) {
  switch (BrOpc) {
  case Haydn::BEQ:
    return Haydn::BEQZ;
  case Haydn::BNE:
    return Haydn::BNEZ;
  case Haydn::BGE:
    return Haydn::BGEZ;
  case Haydn::BLT:
    return Haydn::BLTZ;
  default:
    return 0;
  }
}

bool HaydnConditionOptimizer::narrowBranchToZero(MachineFunction &MF) {
  const auto *TRI = MF.getSubtarget<HaydnSubtarget>().getRegisterInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    SmallVector<MachineInstr *, 4> ToRemove;

    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();

      // Only two-register conditional branches with a zero-test form.
      if (Opc != Haydn::BEQ && Opc != Haydn::BNE && Opc != Haydn::BGE &&
          Opc != Haydn::BLT)
        continue;

      // Operands: rs1, rs2, brtarget.
      if (MI.getNumOperands() < 3)
        continue;

      const MachineOperand &Rs1Op = MI.getOperand(0);
      const MachineOperand &Rs2Op = MI.getOperand(1);
      const MachineOperand &TgtOp = MI.getOperand(2);

      if (!Rs1Op.isReg() || !Rs2Op.isReg() || !TgtOp.isMBB())
        continue;
      if (!Rs1Op.getReg().isPhysical() || !Rs2Op.getReg().isPhysical())
        continue;

      Register Rs1 = Rs1Op.getReg();
      Register Rs2 = Rs2Op.getReg();

      // Determine which source holds the non-zero operand. For BEQ/BNE the
      // operands are symmetric, so R0 on either side is fine. For BGE/BLT
      // the comparison sense matters: only the *second* operand may be R0
      // (BGE rA, R0 == rA >= 0 == BGEZ rA). Rewriting when the *first*
      // operand is R0 would require a different mnemonic (BGTZ/BLEZ), which
      // the ISA does not provide, so skip.
      Register NonZeroReg;
      if (Opc == Haydn::BEQ || Opc == Haydn::BNE) {
        if (Rs2 == Haydn::R0 && Rs1 != Haydn::R0)
          NonZeroReg = Rs1;
        else if (Rs1 == Haydn::R0 && Rs2 != Haydn::R0)
          NonZeroReg = Rs2;
        else
          continue;
      } else {
        // BGE / BLT: only second-operand R0 narrows cleanly.
        if (Rs2 != Haydn::R0 || Rs1 == Haydn::R0)
          continue;
        NonZeroReg = Rs1;
      }

      if (!Haydn::GPR32RegClass.contains(NonZeroReg))
        continue;

      unsigned NarrowedOpc = getZeroBranchOpcode(Opc);
      if (!NarrowedOpc)
        continue;

      LLVM_DEBUG({
        dbgs() << "  Narrowing branch to zero-test form (opcode "
               << NarrowedOpc << "):\n";
        dbgs() << "    " << MI;
      });

      MachineBasicBlock *Target = TgtOp.getMBB();
      DebugLoc DL = MI.getDebugLoc();
      BuildMI(MBB, MI, DL, HII->get(NarrowedOpc))
          .addReg(NonZeroReg)
          .addMBB(Target);

      ToRemove.push_back(&MI);
      ++NumBranchesNarrowedToZero;
      Changed = true;
    }

    for (MachineInstr *MI : reverse(ToRemove))
      MI->eraseFromParent();
  }

  return Changed;
}
