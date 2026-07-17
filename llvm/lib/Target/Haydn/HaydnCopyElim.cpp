//===-- HaydnCopyElim.cpp - Haydn Redundant Copy Elimination ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a post-register-allocation pass that eliminates
// redundant COPY instructions for the Haydn VLIW DSP target. Patterns
// handled:
//
// 1. Identity COPY: COPY rA, rA (source == dest) -> remove entirely.
// A copy from a register to itself is a no-op.
//
// 2. Dead COPY: COPY rA, rB where the destination rA is overwritten
// before any subsequent use -> remove. The copy's result is never
// consumed, so it is dead code. Works for both GPR32 and DR64.
//
// 3. COPY/MOVE32/OR{32,64} *to* R0: never eliminated as dead. R0 is
// soft-zero (silicon does not force zero). Writes to R0 may be deliberate
// temps that later sequences re-zero; deadness analysis must not drop them.
// True identity forms (src == dst) remain removable as pure no-ops.
//
// 4. OR64 identity copy: OR64 dN, dN, dN where all three operands are
// the same DR64 register -> remove. OR64 rd, rs, rs is the standard
// DR64 register copy pattern (rd = rs | rs = rs). When rd == rs
// this is an identity operation.
//
// 5. OR32 identity copy: OR32 rN, rN, rN where all three operands are
// the same GPR32 register -> remove (GPR32 analog of case 4).
//
// The pass requires NoVRegs (runs post-RA) so that all operands are physical
// registers and the analysis is over stable register assignments.
//
//===----------------------------------------------------------------------===//

#include "HaydnCopyElim.h"
#include "Haydn.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-copy-elim"

using namespace llvm;

STATISTIC(NumIdentityCopiesEliminated,
          "Number of identity copies eliminated (src == dst)");
STATISTIC(NumDeadCopiesEliminated,
          "Number of dead copies eliminated (dst overwritten before use)");
STATISTIC(NumOR64IdentityEliminated,
          "Number of OR64 identity copies eliminated (DR64 rd, rs, rs where rd==rs)");
STATISTIC(NumOR32IdentityEliminated,
          "Number of OR32 identity copies eliminated (GPR32 rd, rs, rs where rd==rs)");

//===----------------------------------------------------------------------===//
// Public interface
//===----------------------------------------------------------------------===//

char HaydnCopyElim::ID = 0;

INITIALIZE_PASS(HaydnCopyElim, "haydn-copy-elim",
                "Haydn Copy Elimination", false, false)

FunctionPass *llvm::createHaydnCopyElimPass() {
  return new HaydnCopyElim();
}

HaydnCopyElim::HaydnCopyElim()
    : MachineFunctionPass(ID) {}

void HaydnCopyElim::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

// Check whether a COPY-like instruction is dead by scanning forward from the
// instruction to determine whether the destination register is overwritten
// before any use. Returns true if the copy can be safely eliminated.
static bool isDeadCopy(const MachineInstr &MI, Register DstReg,
                       const MachineBasicBlock &MBB) {
  bool IsDead = true;
  for (auto ScanIt = std::next(MI.getIterator());
       ScanIt != MBB.end(); ++ScanIt) {
    const MachineInstr &ScanMI = *ScanIt;

    // Check if any operand reads DstReg.
    for (const MachineOperand &MO : ScanMI.all_uses()) {
      if (MO.isReg() && MO.getReg() == DstReg) {
        IsDead = false;
        break;
      }
    }
    if (!IsDead)
      break;

    // Check if DstReg is redefined (possibly by another COPY or ALU op).
    for (const MachineOperand &MO : ScanMI.all_defs()) {
      if (MO.isReg() && MO.getReg() == DstReg) {
        // DstReg is redefined before any use -- the original copy is dead.
        return true;
      }
    }
  }

  // If we reached the end of the block without finding a use or a redef
  // DstReg may be live-out. Do NOT eliminate -- it might be used in a
  // successor block.
  return false;
}

bool HaydnCopyElim::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "===== Haydn Copy Elimination: " << MF.getName()
                     << " =====\n");

  bool Changed = false;
  SmallVector<MachineInstr *, 8> ToRemove;

  //===--------------------------------------------------------------------===
  // Pass 1: OR64 / OR32 identity copy elimination.
  // OR64 rd, rs, rs is the DR64 register copy pattern; OR32 rd, rs, rs is the
  // GPR32 analog. In both cases, when rd == rs == rs the instruction is an
  // identity (rd = rd | rd = rd) and can be removed.
  //===--------------------------------------------------------------------===
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned OrOpcode = MI.getOpcode();
      bool IsOR64 = (OrOpcode == Haydn::OR64);
      bool IsOR32 = (OrOpcode == Haydn::OR32);
      if (!IsOR64 && !IsOR32)
        continue;
      if (MI.getNumOperands() < 3)
        continue;

      const MachineOperand &DstOp = MI.getOperand(0);
      const MachineOperand &Src0Op = MI.getOperand(1);
      const MachineOperand &Src1Op = MI.getOperand(2);

      if (!DstOp.isReg() || !Src0Op.isReg() || !Src1Op.isReg())
        continue;

      Register DstReg = DstOp.getReg();
      Register Src0Reg = Src0Op.getReg();
      Register Src1Reg = Src1Op.getReg();

      // OR{32,64} rd, rs, rs is a bank copy (not bare COPY — Bundle128 needs
      // the real opcode). Safe deletes only:
      // (a) identity: rd == rs → no-op
      // (b) dead: rd redefined before use in this MBB
      // Do NOT rewrite to bare COPY (post-RA COPY is dropped by AsmPrinter).
      if (!DstReg.isPhysical() || !Src0Reg.isPhysical() ||
          !Src1Reg.isPhysical() || Src0Reg != Src1Reg)
        continue;

      if (DstReg == Src0Reg) {
        if (IsOR64) {
          LLVM_DEBUG(dbgs() << "  Eliminating OR64 identity copy: " << MI);
          ++NumOR64IdentityEliminated;
        } else {
          LLVM_DEBUG(dbgs() << "  Eliminating OR32 identity copy: " << MI);
          ++NumOR32IdentityEliminated;
        }
        ToRemove.push_back(&MI);
        Changed = true;
        continue;
      }

      // Soft-zero R0: never delete non-identity bank-copies into R0 as dead.
      // (W0.3 / durable rule 31 — silicon does not force R0=0.)
      if (DstReg == Haydn::R0)
        continue;

      if (isDeadCopy(MI, DstReg, MBB)) {
        if (IsOR64) {
          LLVM_DEBUG(dbgs() << "  Eliminating dead OR64 bank-copy: " << MI);
          ++NumOR64IdentityEliminated;
        } else {
          LLVM_DEBUG(dbgs() << "  Eliminating dead OR32 bank-copy: " << MI);
          ++NumOR32IdentityEliminated;
        }
        ToRemove.push_back(&MI);
        Changed = true;
      }
    }
  }

  //===--------------------------------------------------------------------===
  // Pass 2: COPY/MOVE32 redundant copy elimination (identity, dead, R0).
  // Works for both GPR32 and DR64 register classes.
  //===--------------------------------------------------------------------===
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      // Handle both COPY and MOVE32 (Haydn register move) as copy-like
      // instructions that may be redundant.
      bool IsMove32 = (MI.getOpcode() == Haydn::MOVE32);
      if (!MI.isCopy() && !IsMove32)
        continue;

      const MachineOperand &DstOp = MI.getOperand(0);
      const MachineOperand &SrcOp = MI.getOperand(1);

      // Both operands must be physical registers (guaranteed by NoVRegs
      // property, but defensive check for sub-register copies).
      if (!DstOp.isReg() || !SrcOp.isReg())
        continue;
      if (!DstOp.getReg().isPhysical() || !SrcOp.getReg().isPhysical())
        continue;

      Register DstReg = DstOp.getReg();
      Register SrcReg = SrcOp.getReg();

      // Case 1: Identity copy -- COPY rA, rA.
      if (DstReg == SrcReg) {
        LLVM_DEBUG(dbgs() << "  Eliminating identity copy: " << MI);
        ++NumIdentityCopiesEliminated;
        ToRemove.push_back(&MI);
        Changed = true;
        continue;
      }

      // Case 2: Soft-zero R0 — never eliminate non-identity COPY/MOVE32 into
      // R0. Silicon does not hardwire R0=0; treating writes as free "dead"
      // hardwired sinks is incorrect (W0.3 / durable rule 31). True identity
      // COPY r0,r0 is already handled above.
      if (DstReg == Haydn::R0) {
        LLVM_DEBUG(dbgs() << "  Keeping write to soft-zero R0: " << MI);
        continue;
      }

      // Case 3: Dead copy -- the destination is overwritten before any use.
      // Works for both GPR32 and DR64 registers. R0 is excluded above.
      if (isDeadCopy(MI, DstReg, MBB)) {
        LLVM_DEBUG(dbgs() << "  Eliminating dead copy: " << MI);
        ++NumDeadCopiesEliminated;
        ToRemove.push_back(&MI);
        Changed = true;
      }
    }
  }

  // Erase collected instructions in reverse order to avoid iterator
  // invalidation (later instructions have higher addresses in the list).
  for (MachineInstr *MI : reverse(ToRemove))
    MI->eraseFromParent();

  return Changed;
}
