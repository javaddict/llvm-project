//===-- HaydnBitSimplify.cpp - Haydn Bit Simplification Pass -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a post-register-allocation MachineFunctionPass that
// simplifies bit manipulation patterns for the Haydn VLIW DSP target.
//
// The pass runs in addPreEmitPass, after the VLIW packetizer and pseudo
// expansion. It scans for AND/OR/XOR instructions with constant operands
// and applies the following simplifications:
//
// 1. Identity elimination:
// AND32 R, R, R0 → remove (AND with zero is identity for AND-of-self
// but AND32 rd, rs1, R0 = rs1 & 0 = 0, covered below)
// ANDI32 R, 0xFFFF → remove (all bits set, identity)
// ORI32 R, 0 → remove (OR with zero is identity)
// XORI32 R, 0 → remove (XOR with zero is identity)
// OR32 R, R, R0 → remove (OR with zero is identity)
// XOR32 R, R, R0 → remove (XOR with zero is identity)
//
// 2. Zero / all-ones replacement:
// ANDI32 R, 0 → replace with MOVE32 R, R0 (zero)
// AND32 R, R, R0 → replace with MOVE32 R, R0 (AND with 0 = 0)
// ORI32 R, 0xFFFF → replace with MOVE32 R, R0 then NOT or load -1
// (simplified: ORI32 R, 0xFFFF is all-ones in 16-bit
// but may not cover all 32 bits; only fold when
// operand is full 32-bit mask via OR32 R, R, ~R0)
//
// 3. Redundant AND+OR pair (same source register, same mask):
// ANDI32 R, mask followed by ORI32 R, mask → ANDI32 is redundant
//
// 4. Complement synthesis:
// ANDI32 R, C1 followed by ORI32 R, C2 where
// (C1 | C2) == 0xFFFF and (C1 & C2) == 0
// → remove AND (the OR sets exactly the complement bits)
//
// 5. XORI pair constant folding: NOT done here.
// IR/DAG/GISel (InstCombine, DAGCombiner, AIE constant_fold_binops)
// fold (xor (xor x, C1), C2) on SSA. AIE has no post-RA bit-simplify;
// RISCV does not re-fold XORI chains on physical regs after RA.
// A post-RA LastDef walk on physregs is unsafe (clobber of S between
// XORI D,S,C1 and XORI D,D,C2) and caused BundleSim seed1 miscompile
// when emitInvert01 made XORI32 common.
//
//===----------------------------------------------------------------------===//

#include "HaydnBitSimplify.h"
#include "Haydn.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "haydn-bit-simplify"

using namespace llvm;

STATISTIC(NumIdentityOpsEliminated,
          "Number of identity bit ops eliminated (AND/OR/XOR with identity)");
STATISTIC(NumZeroOpsSimplified,
          "Number of zero-result ops simplified (AND with 0)");
STATISTIC(NumRedundantAndOrPairs,
          "Number of redundant AND+OR pairs eliminated");
STATISTIC(NumComplementSynthesis,
          "Number of complement-synthesis patterns simplified");

//===----------------------------------------------------------------------===//
// Public interface
//===----------------------------------------------------------------------===//

char HaydnBitSimplify::ID = 0;

INITIALIZE_PASS(HaydnBitSimplify, "haydn-bit-simplify",
                "Haydn Bit Simplification", false, false)

FunctionPass *llvm::createHaydnBitSimplifyPass() {
  return new HaydnBitSimplify();
}

HaydnBitSimplify::HaydnBitSimplify()
    : MachineFunctionPass(ID) {}

void HaydnBitSimplify::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool HaydnBitSimplify::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "===== Haydn Bit Simplification: " << MF.getName()
                     << " =====\n");

  bool Changed = false;
  SmallVector<MachineInstr *, 8> ToRemove;

  for (MachineBasicBlock &MBB : MF) {
    // Build a map from register to its last def instruction for two-instruction
    // pattern matching (AND+OR, XOR+XOR pairs).
    // We iterate forward, so the map is always up-to-date for the current
    // instruction's predecessors in the same block.
    DenseMap<Register, MachineInstr *> LastDef;

    for (MachineInstr &MI : MBB) {
      // Skip bundled instructions — they cannot be individually removed.
      if (MI.isInsideBundle())
        continue;

      unsigned Opcode = MI.getOpcode();

      // NOTE: Def recording happens at the END of this loop body, AFTER all
      // pattern matching. This is critical: the LastDef map must reflect the
      // state *before* the current instruction so that pair patterns (AND+OR
      // XOR+XOR) correctly find the preceding instruction.

      // Pattern group 1: Register-register identity operations with R0
      // AND32 rd, rs, R0 → AND with zero = zero result → MOVE32 rd, R0
      if (Opcode == Haydn::AND32) {
        if (MI.getNumOperands() >= 3 &&
            MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
            MI.getOperand(2).isReg()) {
          Register DstReg = MI.getOperand(0).getReg();
          Register Src1Reg = MI.getOperand(1).getReg();
          Register Src2Reg = MI.getOperand(2).getReg();

          // AND32 rd, rs, R0 → rd = 0
          if (Src2Reg == Haydn::R0 && DstReg != Haydn::R0) {
            LLVM_DEBUG(dbgs() << "  AND32 with R0 (zero): " << MI);
            ++NumZeroOpsSimplified;
            // Replace with MOVE32 rd, R0 (so rd gets zero).
            // One source: MOVE32 is `rd = rs1` and the database agrees, so
            // the second was only ever filling FmtALU32's rs2 field. That
            // field is bound in the.td now (§ 5.11).
            BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                         .getInstrInfo()->get(Haydn::MOVE32),
                    DstReg)
                .addReg(Haydn::R0);
            ToRemove.push_back(&MI);
            Changed = true;
            continue;
          }

          // AND32 rd, R0, rs → rd = 0 (commutative)
          if (Src1Reg == Haydn::R0 && DstReg != Haydn::R0) {
            LLVM_DEBUG(dbgs() << "  AND32 with R0 (zero, commutative): " << MI);
            ++NumZeroOpsSimplified;
            BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                         .getInstrInfo()->get(Haydn::MOVE32),
                    DstReg)
                .addReg(Haydn::R0);
            ToRemove.push_back(&MI);
            Changed = true;
            continue;
          }

          // AND32 rd, rs, rs → identity (rd = rs & rs = rs).
          // Only simplify if rd != rs (otherwise it's already a no-op in terms
          // of result, but may still be a redundant instruction).
          if (Src1Reg == Src2Reg && DstReg != Src1Reg && DstReg != Haydn::R0) {
            LLVM_DEBUG(dbgs() << "  AND32 rd, rs, rs (identity copy): " << MI);
            ++NumIdentityOpsEliminated;
            BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                         .getInstrInfo()->get(Haydn::MOVE32),
                    DstReg)
                .addReg(Src1Reg).addReg(Src1Reg);
            ToRemove.push_back(&MI);
            Changed = true;
            continue;
          }
        }
      }

      // OR32 rd, rs, R0 → rd = rs (identity) → MOVE32 rd, rs
      if (Opcode == Haydn::OR32) {
        if (MI.getNumOperands() >= 3 &&
            MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
            MI.getOperand(2).isReg()) {
          Register DstReg = MI.getOperand(0).getReg();
          Register Src1Reg = MI.getOperand(1).getReg();
          Register Src2Reg = MI.getOperand(2).getReg();

          if (DstReg != Haydn::R0) {
            // OR32 rd, rs, R0 → rd = rs | 0 = rs
            if (Src2Reg == Haydn::R0) {
              LLVM_DEBUG(dbgs() << "  OR32 with R0 (identity): " << MI);
              ++NumIdentityOpsEliminated;
              BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                           .getInstrInfo()->get(Haydn::MOVE32),
                      DstReg)
                  .addReg(Src1Reg).addReg(Src1Reg);
              ToRemove.push_back(&MI);
              Changed = true;
              continue;
            }
            // OR32 rd, R0, rs → rd = 0 | rs = rs
            if (Src1Reg == Haydn::R0) {
              LLVM_DEBUG(dbgs() << "  OR32 with R0 (identity, commutative): "
                                << MI);
              ++NumIdentityOpsEliminated;
              BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                           .getInstrInfo()->get(Haydn::MOVE32),
                      DstReg)
                  .addReg(Src2Reg).addReg(Src2Reg);
              ToRemove.push_back(&MI);
              Changed = true;
              continue;
            }
          }
        }
      }

      // XOR32 rd, rs, R0 → rd = rs ^ 0 = rs (identity)
      if (Opcode == Haydn::XOR32) {
        if (MI.getNumOperands() >= 3 &&
            MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
            MI.getOperand(2).isReg()) {
          Register DstReg = MI.getOperand(0).getReg();
          Register Src1Reg = MI.getOperand(1).getReg();
          Register Src2Reg = MI.getOperand(2).getReg();

          if (DstReg != Haydn::R0) {
            if (Src2Reg == Haydn::R0) {
              LLVM_DEBUG(dbgs() << "  XOR32 with R0 (identity): " << MI);
              ++NumIdentityOpsEliminated;
              BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                           .getInstrInfo()->get(Haydn::MOVE32),
                      DstReg)
                  .addReg(Src1Reg).addReg(Src1Reg);
              ToRemove.push_back(&MI);
              Changed = true;
              continue;
            }
            if (Src1Reg == Haydn::R0) {
              LLVM_DEBUG(dbgs() << "  XOR32 with R0 (identity, commutative): "
                                << MI);
              ++NumIdentityOpsEliminated;
              BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                           .getInstrInfo()->get(Haydn::MOVE32),
                      DstReg)
                  .addReg(Src2Reg).addReg(Src2Reg);
              ToRemove.push_back(&MI);
              Changed = true;
              continue;
            }
          }
        }
      }

      // Pattern group 2: Register-immediate identity operations
      // ANDI32 rd, rs, 0 → rd = 0
      if (Opcode == Haydn::ANDI32) {
        if (MI.getNumOperands() >= 3 &&
            MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
            MI.getOperand(2).isImm()) {
          Register DstReg = MI.getOperand(0).getReg();
          int64_t Imm = MI.getOperand(2).getImm();

          if (DstReg != Haydn::R0) {
            if (Imm == 0) {
              // AND with zero → result is zero.
              LLVM_DEBUG(dbgs() << "  ANDI32 with 0 (zero): " << MI);
              ++NumZeroOpsSimplified;
              BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                           .getInstrInfo()->get(Haydn::MOVE32),
                      DstReg)
                  .addReg(Haydn::R0);
              ToRemove.push_back(&MI);
              Changed = true;
              continue;
            }

            if (Imm == 0xFFFF) {
              // AND with all-ones (16-bit uimm16 field max) → identity.
              // Note: ANDI32 uses uimm16, so max value is 0xFFFF.
              // For 32-bit AND identity we'd need 0xFFFFFFFF, but uimm16 can't
              // represent that. However, ANDI32 with 0xFFFF is still useful to
              // keep (it masks the lower 16 bits).
              // Only treat as identity if the source and dest are the same reg
              // AND the instruction is a pure mask of the lower 16 bits that
              // we know is already clean — but we can't prove that here.
              // Skip this pattern for safety.
            }

            // ANDI32 rd, rs, rs when rd == rs and Imm covers all source bits
            // → identity. Not applicable since ANDI32 has an immediate operand.

            // Pair pattern: ANDI32 followed by ORI32 with same dest reg.
            // Look up if the previous def of DstReg was an ANDI32.
            // (This handles the case where current MI is ORI32 and previous
            // was ANDI32 — handled below in the ORI32 section.)
          }
        }
      }

      // ORI32 rd, rs, 0 → rd = rs (identity)
      if (Opcode == Haydn::ORI32) {
        if (MI.getNumOperands() >= 3 &&
            MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
            MI.getOperand(2).isImm()) {
          Register DstReg = MI.getOperand(0).getReg();
          Register SrcReg = MI.getOperand(1).getReg();
          int64_t Imm = MI.getOperand(2).getImm();

          if (DstReg != Haydn::R0) {
            if (Imm == 0) {
              // OR with zero → identity.
              LLVM_DEBUG(dbgs() << "  ORI32 with 0 (identity): " << MI);
              ++NumIdentityOpsEliminated;
              BuildMI(MBB, MI, MI.getDebugLoc(), MI.getMF()->getSubtarget()
                           .getInstrInfo()->get(Haydn::MOVE32),
                      DstReg)
                  .addReg(SrcReg).addReg(SrcReg);
              ToRemove.push_back(&MI);
              Changed = true;
              continue;
            }
          }
        }
      }

      // XORI32 rd, rs, 0 → rd = rs (identity only — local, no chain walk).
      // Do NOT fold XORI C1 then XORI C2 here (see file header §5 / AIE+RISCV).
      if (Opcode == Haydn::XORI32) {
        if (MI.getNumOperands() >= 3 &&
            MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
            MI.getOperand(2).isImm()) {
          Register DstReg = MI.getOperand(0).getReg();
          Register SrcReg = MI.getOperand(1).getReg();
          int64_t Imm = MI.getOperand(2).getImm();

          if (DstReg != Haydn::R0 && Imm == 0) {
            LLVM_DEBUG(dbgs() << "  XORI32 with 0 (identity): " << MI);
            ++NumIdentityOpsEliminated;
            BuildMI(MBB, MI, MI.getDebugLoc(),
                    MI.getMF()->getSubtarget().getInstrInfo()->get(
                        Haydn::MOVE32),
                    DstReg)
                .addReg(SrcReg)
                .addReg(SrcReg);
            ToRemove.push_back(&MI);
            Changed = true;
            continue;
          }
        }
      }

      // Pattern group 3 (ANDI+ORI pair / complement synthesis): REMOVED
      // Both folds were algebraically
      // wrong for uimm20 ANDI/ORI (16-bit 0xFFFF universe; (O&A)==O drops bits
      // not forced by O). Keep single-MI identities only.

      // Record defs for pair matching (MUST be after all patterns)
      if (MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
          MI.getOperand(0).getReg().isPhysical()) {
        Register DstReg = MI.getOperand(0).getReg();
        if (DstReg != Haydn::R0)
          LastDef[DstReg] = &MI;
      }

    } // for each MI in MBB
  } // for each MBB in MF

  // Erase collected instructions in reverse order to maintain valid iterators.
  for (MachineInstr *MI : reverse(ToRemove))
    MI->eraseFromParent();

  if (Changed) {
    LLVM_DEBUG(dbgs() << "  Total changes: " << ToRemove.size() << "\n");
  }
  return Changed;
}
