//===-- HaydnExpandPseudos.cpp - Expand pseudo instructions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands Haydn pseudo instructions into target
// instructions. It runs after register allocation and handles pseudos that
// require post-RA context such as physical register operands and call frame
// adjustments.
//
// Pseudos already handled by HaydnInstrInfo::expandPostRAPseudo (RET, B
// LOADI32, MOV_GPR_TO_DR64, MOV_DR64_TO_GPR) are NOT duplicated here.
// This pass handles the remaining pseudos that need dedicated expansion
// logic: call frame adjustments, libcall invocations, and address
// materialization.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "HaydnExpandPseudos.h"
#include "HaydnFrameLowering.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnRegisterInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "haydn-expand-pseudos"

using namespace llvm;

char HaydnExpandPseudos::ID = 0;

INITIALIZE_PASS_BEGIN(HaydnExpandPseudos, "haydn-expand-pseudos",
                      "Haydn pseudo instruction expansion pass", false, false)
INITIALIZE_PASS_END(HaydnExpandPseudos, "haydn-expand-pseudos",
                    "Haydn pseudo instruction expansion pass", false, false)

HaydnExpandPseudos::HaydnExpandPseudos() : MachineFunctionPass(ID) {
  initializeHaydnExpandPseudosPass(*PassRegistry::getPassRegistry());
}

FunctionPass *llvm::createHaydnExpandPseudosPass() {
  return new HaydnExpandPseudos();
}

// True if \p MI is the soft-zero idiom `xor32 r0, r0, r0`.
static bool isSoftZeroR0(const MachineInstr &MI) {
  if (MI.getOpcode() != Haydn::XOR32 || MI.getNumExplicitOperands() < 3)
    return false;
  if (!MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isReg())
    return false;
  return MI.getOperand(0).getReg() == Haydn::R0 &&
         MI.getOperand(1).getReg() == Haydn::R0 &&
         MI.getOperand(2).getReg() == Haydn::R0;
}

static MachineInstrBuilder buildSoftZeroR0(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator InsertPt,
                                           const DebugLoc &DL,
                                           const HaydnInstrInfo *TII) {
  // xor32 r0, r0, r0 — architectural soft-zero restore (AIE-style: in MIR
  // before pack, not AsmPrinter injection).
  return BuildMI(MBB, InsertPt, DL, TII->get(Haydn::XOR32), Haydn::R0)
      .addReg(Haydn::R0)
      .addReg(Haydn::R0);
}

bool HaydnExpandPseudos::insertSoftZeroR0Maintenance(MachineFunction &MF) {
  bool Modified = false;

  // Indirect transfers that write the link into soft-zero R0 clobber the
  // architectural zero invariant. Re-zero at the head of every successor so
  // PostRA pack and size models see the bytes (not AsmPrinter injection):
  //   * BR_JT → expands to JALR_W r0, addr (printer)
  //   * JALR_W / JALR with rd=R0 (G_BRINDIRECT pure jump; not a call)
  // RET is also JALR_W r0,lr but has no executable successors that need zero.
  DenseSet<MachineBasicBlock *> SoftZeroTargets;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      bool NeedsSuccRezero = false;
      if (Opc == Haydn::BR_JT) {
        NeedsSuccRezero = true;
      } else if ((Opc == Haydn::JALR_W || Opc == Haydn::JALR) &&
                 MI.getNumExplicitOperands() >= 1 && MI.getOperand(0).isReg() &&
                 MI.getOperand(0).getReg() == Haydn::R0) {
        // Pure jump / discard-link form (rd = R0). Skip if this looks like a
        // fall-through-less return-only edge set: still safe to re-zero any
        // listed successors (empty set for normal RET).
        NeedsSuccRezero = true;
      }
      if (!NeedsSuccRezero)
        continue;
      for (MachineBasicBlock *Succ : MBB.successors())
        SoftZeroTargets.insert(Succ);
    }
  }
  for (MachineBasicBlock *MBB : SoftZeroTargets) {
    MachineBasicBlock::iterator InsertPt = MBB->begin();
    while (InsertPt != MBB->end() &&
           (InsertPt->isMetaInstruction() || InsertPt->isDebugInstr() ||
            InsertPt->isCFIInstruction()))
      ++InsertPt;
    if (InsertPt != MBB->end() && isSoftZeroR0(*InsertPt))
      continue;
    DebugLoc DL = InsertPt != MBB->end() ? InsertPt->getDebugLoc() : DebugLoc();
    buildSoftZeroR0(*MBB, InsertPt, DL, TII);
    Modified = true;
    LLVM_DEBUG(dbgs() << "HaydnExpandPseudos: soft-zero R0 at indirect-target "
                         "bb."
                      << MBB->getNumber() << '\n');
  }

  // After calls: direct JAL/JAL_W and PseudoCALLIndirect (still pseudo
  // until printer). Callee RET is JALR_W r0,lr which clobbers R0.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator MII = MBB.begin(), E = MBB.end();
         MII != E;) {
      MachineInstr &MI = *MII;
      ++MII; // advance before possible insert after MI
      unsigned Opc = MI.getOpcode();
      bool NeedsPostCallZero =
          Opc == Haydn::JAL || Opc == Haydn::JAL_W ||
          Opc == Haydn::PseudoCALLIndirect;
      if (!NeedsPostCallZero)
        continue;
      // Skip if the next real instr is already soft-zero.
      MachineBasicBlock::iterator Next = MII;
      while (Next != MBB.end() &&
             (Next->isMetaInstruction() || Next->isDebugInstr()))
        ++Next;
      if (Next != MBB.end() && isSoftZeroR0(*Next))
        continue;
      buildSoftZeroR0(MBB, MII, MI.getDebugLoc(), TII);
      Modified = true;
      LLVM_DEBUG(dbgs() << "HaydnExpandPseudos: soft-zero R0 after call in bb."
                        << MBB.getNumber() << '\n');
    }
  }

  return Modified;
}

bool HaydnExpandPseudos::runOnMachineFunction(MachineFunction &MF) {
  STI = &MF.getSubtarget<HaydnSubtarget>();
  TII = STI->getInstrInfo();

  bool Modified = false;
  // Iterator form so VAARG expand can insert Stack/Reg/Join MBBs and still
  // expand residual pseudos in Join.
  for (MachineFunction::iterator MBBI = MF.begin(), MBBE = MF.end();
       MBBI != MBBE; ++MBBI)
    Modified |= expandMBB(*MBBI);

  // Bundle interior residual expand. Product post-inc home is
  // HaydnExpandPostIncEarly (pre-pack, default ON). LoadStoreOpt form is
  // opt-in and also pre-pack, so *_POST_INC should already be real LD/ST+ADDI
  // before packetize. This pass still expands any residual POST_INC / CALL
  // LOAD_ADDR that appear inside bundles so they are not silently dropped
  // at MC (legacy dual-path safety net — not a second product home).
  for (auto &MBB : MF)
    Modified |= expandPseudosInBundles(MBB);

  // After real call opcodes exist (PseudoCALL → JAL_W), insert soft-zero
  // maintenance so the PostRA packer schedules/sizes it (not AsmPrinter).
  Modified |= insertSoftZeroR0Maintenance(MF);

  return Modified;
}

bool HaydnExpandPseudos::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NextMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, *MBBI, NextMBBI);
    MBBI = NextMBBI;
  }

  return Modified;
}

// Expand residual pseudos inside VLIW bundles so MC never silently drops them.
// Pipeline (addPreSched2, AIE2-aligned pack order):
// optional LoadStoreOpt (form *_POST_INC, default OFF)
// ExpandPostIncEarly (product expand, default ON) ← sole post-inc home
// … MBP (O1+) → HardwareLoops (O1+) → ExpandPseudos → PostRA pack …
// For each pseudo found inside a bundle: unbundle, expand before the BUNDLE
// leave remaining real children in the bundle; drop empty BUNDLEs.
bool HaydnExpandPseudos::expandPseudosInBundles(MachineBasicBlock &MBB) {
  bool Modified = false;

  // Collect all BUNDLE instructions first to avoid iterator invalidation.
  SmallVector<MachineInstr *, 4> Bundles;
  for (MachineInstr &MI : MBB) {
    if (MI.isBundle())
      Bundles.push_back(&MI);
  }

  for (MachineInstr *Bundle : Bundles) {
    // Gather bundle children that need expansion.
    SmallVector<MachineInstr *, 4> PseudosToExpand;
    MachineBasicBlock::instr_iterator I = Bundle->getIterator();
    ++I; // Skip the BUNDLE instruction itself.
    for (MachineBasicBlock::instr_iterator E = MBB.instr_end();
         I != E && I->isInsideBundle(); ++I) {
      MachineInstr &Child = *I;
      // Only handle the pseudo opcodes we know how to expand.
      switch (Child.getOpcode()) {
      default:
        continue;
      case Haydn::LD32_POST_INC:
      case Haydn::ST32_POST_INC:
      case Haydn::LD64_POST_INC:
      case Haydn::ST64_POST_INC:
      case Haydn::LOAD_ADDR:
      case Haydn::ADJCALLSTACKDOWN:
      case Haydn::ADJCALLSTACKUP:
      case Haydn::PseudoCALL:
      case Haydn::LIBCALL_SDIV:
      case Haydn::LIBCALL_UDIV:
      case Haydn::LIBCALL_SREM:
      case Haydn::LIBCALL_UREM:
      case Haydn::LIBCALL_MUL64:
        PseudosToExpand.push_back(&Child);
        break;
      }
    }

    if (PseudosToExpand.empty())
      continue;

    // Track A / Codex : LOAD_ADDR / call / libcall bundled expansion holds
    // MachineOperand* across eraseFromBundle (UAF) and drops call regmasks.
    // Fail hard — these must expand before PostRA pack, never as a repair path.
    for (MachineInstr *P : PseudosToExpand) {
      switch (P->getOpcode()) {
      case Haydn::LOAD_ADDR:
      case Haydn::ADJCALLSTACKDOWN:
      case Haydn::ADJCALLSTACKUP:
      case Haydn::PseudoCALL:
      case Haydn::LIBCALL_SDIV:
      case Haydn::LIBCALL_UDIV:
      case Haydn::LIBCALL_SREM:
      case Haydn::LIBCALL_UREM:
      case Haydn::LIBCALL_MUL64:
        report_fatal_error(
            "HaydnExpandPseudos: bundled LOAD_ADDR/call/libcall is forbidden "
            "(UAF repair path retired). Expand before PostRA pack.");
      default:
        break;
      }
    }

    Modified = true;

    for (MachineInstr *PseudoMI : PseudosToExpand) {
      // Save expansion info before erasing from bundle.
      DebugLoc DL = PseudoMI->getDebugLoc();
      unsigned Opcode = PseudoMI->getOpcode();

      // Save operands before erasing — they become invalid after eraseFromBundle.
      struct SavedPostInc {
        Register DstReg;
        Register BaseReg;
        int64_t Stride;
        int64_t Offset;
      };
      SavedPostInc PI;
      if (Opcode == Haydn::LD32_POST_INC || Opcode == Haydn::ST32_POST_INC ||
          Opcode == Haydn::LD64_POST_INC || Opcode == Haydn::ST64_POST_INC) {
        PI.DstReg = PseudoMI->getOperand(0).getReg();
        PI.BaseReg = PseudoMI->getOperand(1).getReg();
        PI.Stride = PseudoMI->getOperand(2).getImm();
        // Operand 3 is the preserved displacement.
        PI.Offset = PseudoMI->getOperand(3).getImm();
      }

      // Save LOAD_ADDR operands.
      Register LoadAddrDst;
      MachineOperand *AddrOp = nullptr;
      if (Opcode == Haydn::LOAD_ADDR) {
        LoadAddrDst = PseudoMI->getOperand(0).getReg();
        AddrOp = &PseudoMI->getOperand(1);
      }

      // Save ADJCALLSTACK operands.
      int64_t AdjAmount = 0;
      if (Opcode == Haydn::ADJCALLSTACKDOWN || Opcode == Haydn::ADJCALLSTACKUP)
        AdjAmount = PseudoMI->getOperand(0).getImm();

      // Save PseudoCALL operand.
      MachineOperand *CallTarget = nullptr;
      if (Opcode == Haydn::PseudoCALL)
        CallTarget = &PseudoMI->getOperand(1);

      // Save libcall operands.
      Register LibResultReg;
      Register LibRs1, LibRs2;
      if (Opcode == Haydn::LIBCALL_SDIV || Opcode == Haydn::LIBCALL_UDIV ||
          Opcode == Haydn::LIBCALL_SREM || Opcode == Haydn::LIBCALL_UREM ||
          Opcode == Haydn::LIBCALL_MUL64) {
        LibResultReg = PseudoMI->getOperand(0).getReg();
        LibRs1 = PseudoMI->getOperand(1).getReg();
        LibRs2 = PseudoMI->getOperand(2).getReg();
      }

      // Erase from bundle (removes and destroys the pseudo).
      PseudoMI->eraseFromBundle();

      // Expand the pseudo, inserting real instructions before the BUNDLE.
      MachineBasicBlock::iterator BundleIter(Bundle);

      switch (Opcode) {
      default:
        llvm_unreachable("Unhandled pseudo in bundle expansion");
      case Haydn::LD32_POST_INC: {
        // emit the canonical DB-named S_LW_POST_IMM (s_lw_post_imm) when
        // the stride is encodable as imm6<<2 and there is no separate
        // displacement to preserve (— Offset==0 guard). Falls back
        // to the 2-instr LD32 + ADDI32 split otherwise. Mirrors
        // HaydnExpandPostIncEarly. NOTE: the DB D_LW_POST_IMM is a DR64
        // (paired-i32) load; the scalar i32 post-inc load is S_LW_POST_IMM
        // (GPR32 dest), which is what LD32_POST_INC models. (emitted the
        // parallel-namespace LD32_POST alias; retires it to the DB name.)
        if (PI.Offset == 0 && (PI.Stride % 4) == 0 &&
            isInt<6>(PI.Stride >> 2)) {
          BuildMI(MBB, BundleIter, DL, TII->get(Haydn::S_LW_POST_IMM))
              .addReg(PI.DstReg, RegState::Define)
              .addReg(PI.BaseReg, RegState::Define)
              .addReg(PI.BaseReg)
              .addImm(PI.Stride >> 2);
          break;
        }
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::LD32), PI.DstReg)
            .addReg(PI.BaseReg)
            .addImm(PI.Offset);
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADDI32_W), PI.BaseReg)
            .addReg(PI.BaseReg)
            .addImm(PI.Stride);
        break;
      }
      case Haydn::ST32_POST_INC: {
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ST32))
            .addReg(PI.DstReg)
            .addReg(PI.BaseReg)
            .addImm(PI.Offset);
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADDI32_W), PI.BaseReg)
            .addReg(PI.BaseReg)
            .addImm(PI.Stride);
        break;
      }
      case Haydn::LD64_POST_INC: {
        // emit the canonical DB-named D_LDW_POST_IMM (d_ldw_post_imm)
        // when the stride is encodable as imm6<<3 and there is no separate
        // displacement to preserve (— Offset==0 guard). Falls back
        // to the 2-instr LD64_S1 + ADDI32 split otherwise. Mirrors
        // HaydnExpandPostIncEarly. This is the exact analog of HiFi's
        // ae_l64.ip. (emitted the parallel-namespace LD64_POST alias;
        // retires it to the DB name.)
        if (PI.Offset == 0 && (PI.Stride % 8) == 0 &&
            isInt<6>(PI.Stride >> 3)) {
          BuildMI(MBB, BundleIter, DL, TII->get(Haydn::D_LDW_POST_IMM))
              .addReg(PI.DstReg, RegState::Define)
              .addReg(PI.BaseReg, RegState::Define)
              .addReg(PI.BaseReg)
              .addImm(PI.Stride >> 3);
          break;
        }
        // plain LD64 (slot 0/1) so the split post-inc load can pack.
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::LD64), PI.DstReg)
            .addReg(PI.BaseReg)
            .addImm(PI.Offset);
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADDI32_W), PI.BaseReg)
            .addReg(PI.BaseReg)
            .addImm(PI.Stride);
        break;
      }
      case Haydn::ST64_POST_INC: {
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ST64))
            .addReg(PI.DstReg)
            .addReg(PI.BaseReg)
            .addImm(PI.Offset);
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADDI32_W), PI.BaseReg)
            .addReg(PI.BaseReg)
            .addImm(PI.Stride);
        break;
      }
      case Haydn::LOAD_ADDR: {
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::LUI), LoadAddrDst)
            .addReg(Haydn::R0)
            .add(*AddrOp);
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADDI32_W), LoadAddrDst)
            .addReg(LoadAddrDst)
            .add(*AddrOp);
        break;
      }
      case Haydn::ADJCALLSTACKDOWN: {
        if (AdjAmount != 0) {
          assert(isInt<16>(AdjAmount) &&
                 "Call frame adjustment exceeds simm16 range");
          BuildMI(MBB, BundleIter, DL, TII->get(Haydn::SUBI32), Haydn::R13)
              .addReg(Haydn::R13)
              .addImm(AdjAmount);
        }
        break;
      }
      case Haydn::ADJCALLSTACKUP: {
        if (AdjAmount != 0) {
          assert(isInt<16>(AdjAmount) &&
                 "Call frame adjustment exceeds simm16 range");
          BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADDI32_W), Haydn::R13)
              .addReg(Haydn::R13)
              .addImm(AdjAmount);
        }
        break;
      }
      case Haydn::PseudoCALL: {
        // Phase 1a: route to the 48-bit WIDE form JAL_W.
        BuildMI(MBB, BundleIter, DL, TII->get(Haydn::JAL_W), Haydn::R15)
            .add(*CallTarget);
        break;
      }
      case Haydn::LIBCALL_SDIV:
      case Haydn::LIBCALL_UDIV:
      case Haydn::LIBCALL_SREM:
      case Haydn::LIBCALL_UREM:
      case Haydn::LIBCALL_MUL64: {
        bool Is64Bit = (Opcode == Haydn::LIBCALL_MUL64);
        const char *Symbol = nullptr;
        switch (Opcode) {
        default:
          llvm_unreachable("Unhandled libcall");
        case Haydn::LIBCALL_SDIV:
          Symbol = "__divsi3";
          break;
        case Haydn::LIBCALL_UDIV:
          Symbol = "__udivsi3";
          break;
        case Haydn::LIBCALL_SREM:
          Symbol = "__modsi3";
          break;
        case Haydn::LIBCALL_UREM:
          Symbol = "__umodsi3";
          break;
        case Haydn::LIBCALL_MUL64:
          Symbol = "__muldi3";
          break;
        }

        if (Is64Bit) {
          if (LibRs1 != Haydn::D0)
            BuildMI(MBB, BundleIter, DL, TII->get(Haydn::OR64), Haydn::D0)
                .addReg(LibRs1, getKillRegState(true))
                .addReg(LibRs1, getKillRegState(true));
          if (LibRs2 != Haydn::D1)
            BuildMI(MBB, BundleIter, DL, TII->get(Haydn::OR64), Haydn::D1)
                .addReg(LibRs2, getKillRegState(true))
                .addReg(LibRs2, getKillRegState(true));
          // Phase 1a: route to the 48-bit WIDE form JAL_W.
          BuildMI(MBB, BundleIter, DL, TII->get(Haydn::JAL_W), Haydn::R15)
              .addExternalSymbol(Symbol);
          if (LibResultReg != Haydn::D0)
            BuildMI(MBB, BundleIter, DL, TII->get(Haydn::OR64), LibResultReg)
                .addReg(Haydn::D0)
                .addReg(Haydn::D0);
        } else {
          if (LibRs1 != Haydn::R1)
            BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADD32), Haydn::R1)
                .addReg(LibRs1, getKillRegState(true))
                .addReg(Haydn::R0);
          if (LibRs2 != Haydn::R2)
            BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADD32), Haydn::R2)
                .addReg(LibRs2, getKillRegState(true))
                .addReg(Haydn::R0);
          // Phase 1a: route to the 48-bit WIDE form JAL_W.
          BuildMI(MBB, BundleIter, DL, TII->get(Haydn::JAL_W), Haydn::R15)
              .addExternalSymbol(Symbol);
          if (LibResultReg != Haydn::R1)
            BuildMI(MBB, BundleIter, DL, TII->get(Haydn::ADD32), LibResultReg)
                .addReg(Haydn::R1)
                .addReg(Haydn::R0);
        }
        break;
      }
      }
      // The pseudo was already erased via eraseFromBundle above.
    }

    // If the bundle has no remaining children, remove the empty BUNDLE.
    bool HasChildren = false;
    MachineBasicBlock::instr_iterator BI = Bundle->getIterator();
    ++BI;
    for (MachineBasicBlock::instr_iterator BE = MBB.instr_end();
         BI != BE && BI->isInsideBundle(); ++BI) {
      HasChildren = true;
      break;
    }
    if (!HasChildren) {
      Bundle->eraseFromParent();
    }
  }

  return Modified;
}

bool HaydnExpandPseudos::expandMI(MachineBasicBlock &MBB, MachineInstr &MI,
                                  MachineBasicBlock::iterator &NextMBBI) {
  switch (MI.getOpcode()) {
  default:
    return false;

  case Haydn::LOAD_ADDR:
    return expandLOAD_ADDR(MBB, MI);

  case Haydn::ADJCALLSTACKDOWN:
    return expandADJCALLSTACKDOWN(MBB, MI);

  case Haydn::ADJCALLSTACKUP:
    return expandADJCALLSTACKUP(MBB, MI);

  case Haydn::PseudoCALL:
    return expandPseudoCALL(MBB, MI);

  case Haydn::LIBCALL_SDIV:
    return expandLibcall(MBB, MI, "__divsi3");

  case Haydn::LIBCALL_UDIV:
    return expandLibcall(MBB, MI, "__udivsi3");

  case Haydn::LIBCALL_SREM:
    return expandLibcall(MBB, MI, "__modsi3");

  case Haydn::LIBCALL_UREM:
    return expandLibcall(MBB, MI, "__umodsi3");

  case Haydn::LIBCALL_MUL64:
    return expandLibcall(MBB, MI, "__muldi3");

  case Haydn::LD32_POST_INC:
    return expandLD32PostInc(MBB, MI);

  case Haydn::ST32_POST_INC:
    return expandST32PostInc(MBB, MI);

  case Haydn::LD64_POST_INC:
    return expandLD64PostInc(MBB, MI);

  case Haydn::ST64_POST_INC:
    return expandST64PostInc(MBB, MI);

  case Haydn::VASTART:
    return expandVASTART(MBB, MI);

  case Haydn::VACOPY:
    return expandVACOPY(MBB, MI);

  case Haydn::VAARG_I32:
    return expandVAARG(MBB, MI, NextMBBI, /*IsI64=*/false);

  case Haydn::VAARG_I64:
    return expandVAARG(MBB, MI, NextMBBI, /*IsI64=*/true);

  case Haydn::VAEND:
    // Baremetal va_list has no owned resources.
    MI.eraseFromParent();
    return true;

  // HardwareLoops emits SET_HWLOOP{,_REG} (sel, MBB start/end, count/rs).
  // Before PostRA pack they must be the real wide forms that materialize
  // into SET_HWLOOP_{W,F2_W}_S0 (same operand structure: imm + 2×brtarget +
  // cnt/rs). Flex wrongly pairs SET_HWLOOP_REG → REG_S0 (4 GPRs); packing
  // that shape corrupts encoding. Convert here so the bundle printer is
  // pure Desc-only Lower (AIE serialize path).
  case Haydn::SET_HWLOOP_REG:
    assert(MI.getNumOperands() >= 4 && MI.getOperand(0).isImm() &&
           MI.getOperand(1).isMBB() && MI.getOperand(2).isMBB() &&
           MI.getOperand(3).isReg() &&
           "SET_HWLOOP_REG shape: sel, start, end, rs");
    MI.setDesc(TII->get(Haydn::SET_HWLOOP_F2_W));
    return true;
  case Haydn::SET_HWLOOP:
    assert(MI.getNumOperands() >= 4 && MI.getOperand(0).isImm() &&
           MI.getOperand(1).isMBB() && MI.getOperand(2).isMBB() &&
           MI.getOperand(3).isImm() &&
           "SET_HWLOOP shape: sel, start, end, cnt");
    MI.setDesc(TII->get(Haydn::SET_HWLOOP_W));
    return true;
  }
}

//===----------------------------------------------------------------------===//
// VASTART / VACOPY — W1.2 pre-pack expand (was AsmPrinter-only)
//===----------------------------------------------------------------------===//

bool HaydnExpandPseudos::expandVASTART(MachineBasicBlock &MBB,
                                       MachineInstr &MI) {
  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  if (!FuncInfo->hasVarArgsSaveAreas()) {
    MI.eraseFromParent();
    return true;
  }

  DebugLoc DL = MI.getDebugLoc();
  Register VaListPtr = MI.getOperand(0).getReg();
  const HaydnFrameLowering *TFL = STI->getFrameLowering();
  MachineBasicBlock::iterator InsertPt = MI.getIterator();

  const int GprFI = FuncInfo->getVarArgsGprFI();
  const int DrFI = FuncInfo->getVarArgsDrFI();
  const int StackFI = FuncInfo->getVarArgsStackFI();
  const int GprSize = FuncInfo->getVarArgsGprSize();
  const int DrSize = FuncInfo->getVarArgsDrSize();

  // Free-reg first via LivePhysRegs (HaydnPostRAScratch); spill only if none.
  // PreferNotR12: R12 is a normal GPR, not free AT. Exclude VaListPtr so we
  // never steal the base used by ST32 field writes. Frame layout is final
  // (ExpandPseudos runs after PEI in addPreSched2).
  //
  // NeedsZeroBase: StoreNegSizeOff does ADDI Scr, R0, -BankSize (R0 = zero
  // source). Scr must not be R0; policy routes to scavenger (and refuses a
  // dirty R0 borrow). No hard Exclude of R0 — soft-zero state is checked.
  const Register Exclude[] = {VaListPtr};
  withPostRAScratch(
      MBB, InsertPt, DL, *TII, *STI, /*PreferNotR12=*/true,
      [&](Register Scr) {
        assert(Scr != Haydn::R0 &&
               "VASTART scratch must not be soft-zero R0");
        auto StoreFIAddr = [&](int FI, int64_t Extra, int FieldOff) {
          Register FrameReg;
          int64_t Offset =
              TFL->getFrameIndexReference(MF, FI, FrameReg).getFixed() + Extra;
          if (Offset == 0) {
            BuildMI(MBB, InsertPt, DL, TII->get(Haydn::OR32), Scr)
                .addReg(FrameReg)
                .addReg(FrameReg);
          } else {
            BuildMI(MBB, InsertPt, DL, TII->get(Haydn::ADDI32_W), Scr)
                .addReg(FrameReg)
                .addImm(Offset);
          }
          BuildMI(MBB, InsertPt, DL, TII->get(Haydn::ST32))
              .addReg(Scr)
              .addReg(VaListPtr)
              .addImm(FieldOff);
        };

        // __stack @0, __gr_top @4, __vr_top @8
        StoreFIAddr(StackFI, /*Extra=*/0, /*FieldOff=*/0);
        StoreFIAddr(GprFI, /*Extra=*/GprSize, /*FieldOff=*/4);
        StoreFIAddr(DrFI, /*Extra=*/DrSize, /*FieldOff=*/8);

        // __gr_offs @12 = -GprSize; __vr_offs @16 = -DrSize (R0 soft-zero base)
        auto StoreNegSizeOff = [&](int BankSize, int FieldOff) {
          BuildMI(MBB, InsertPt, DL, TII->get(Haydn::ADDI32_W), Scr)
              .addReg(Haydn::R0)
              .addImm(-BankSize);
          BuildMI(MBB, InsertPt, DL, TII->get(Haydn::ST32))
              .addReg(Scr)
              .addReg(VaListPtr)
              .addImm(FieldOff);
        };
        StoreNegSizeOff(GprSize, /*FieldOff=*/12);
        StoreNegSizeOff(DrSize, /*FieldOff=*/16);
      },
      Exclude, PostRASoftZero::NeedsZeroBase);

  MI.eraseFromParent();
  return true;
}

bool HaydnExpandPseudos::expandVACOPY(MachineBasicBlock &MBB,
                                      MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  Register DstPtr = MI.getOperand(0).getReg();
  Register SrcPtr = MI.getOperand(1).getReg();
  MachineBasicBlock::iterator InsertPt = MI.getIterator();

  const Register Exclude[] = {DstPtr, SrcPtr};
  withPostRAScratch(
      MBB, InsertPt, DL, *TII, *STI, /*PreferNotR12=*/true,
      [&](Register Scr) {
        for (unsigned W = 0; W < 5; ++W) {
          int64_t Off = static_cast<int64_t>(W) * 4;
          BuildMI(MBB, InsertPt, DL, TII->get(Haydn::LD32), Scr)
              .addReg(SrcPtr)
              .addImm(Off);
          BuildMI(MBB, InsertPt, DL, TII->get(Haydn::ST32))
              .addReg(Scr)
              .addReg(DstPtr)
              .addImm(Off);
        }
      },
      Exclude);

  MI.eraseFromParent();
  return true;
}

//===----------------------------------------------------------------------===//
// VAARG_I32 / VAARG_I64 — unified two-bank + stack overflow (CB-131)
//===----------------------------------------------------------------------===//
//
// AArch64-style:
//   if (offs + n_reg > 0)  // bank exhausted (offs starts at -BankSize; 0 if empty)
//     val = *__stack;  __stack += 8;   // CCAssignToStack<8,8>
//   else
//     val = *(top + offs);  offs += n_reg;  // GR:4, DR:8
//
// Post-RA: both paths write the same phys Dst; split MBB for the compare.
//
// Scratch contract (must NOT use withPostRAScratch around the branch):
//   withPostRAScratch restores at InsertPt after the lambda. If the lambda
//   emits terminators, that restore lands after BNEZ/B → verifier:
//   "Non-terminator after first terminator". Instead: free-first scavengers;
//   if a live reg must be borrowed, spill at MBB head and restore at Join
//   entry (after both arms define Dst).

namespace {

// Spill a live GPR. The shared PostRAScratchFI is only safe for ONE
// concurrent borrow — if two scratches both need spill they must not share
// that slot (looped va_arg: IV and a constant both landed on sp+0 → ABORT).
// UseAllowFI: true only for the first spill in a multi-scratch expand.
struct VAARGSpillHome {
  enum Kind { None, FrameIndex, SPBracket } K = None;
  Register FrameReg;
  int64_t Off = 0;
};

static VAARGSpillHome vaargBeginSpill(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator I,
                                      const DebugLoc &DL,
                                      const TargetInstrInfo &TII,
                                      const HaydnSubtarget &ST, Register Scr,
                                      bool UseAllowFI) {
  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  int SpillFI = FuncInfo->getBranchRelaxationScratchFI();
  if (SpillFI < 0)
    SpillFI = FuncInfo->getPostRAScratchFI();

  VAARGSpillHome Home;
  if (UseAllowFI && SpillFI >= 0) {
    const HaydnFrameLowering *TFL = ST.getFrameLowering();
    Home.K = VAARGSpillHome::FrameIndex;
    Home.Off =
        TFL->getFrameIndexReference(MF, SpillFI, Home.FrameReg).getFixed();
    if (isInt<16>(Home.Off)) {
      BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
          .addReg(Scr)
          .addReg(Home.FrameReg)
          .addImm(Home.Off);
    } else {
      // Large frame: materialize via soft-zero R0 temp.
      BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Haydn::R0)
          .addReg(Home.FrameReg)
          .addImm(Home.Off);
      BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
          .addReg(Scr)
          .addReg(Haydn::R0)
          .addImm(0);
      BuildMI(MBB, I, DL, TII.get(Haydn::XOR32), Haydn::R0)
          .addReg(Haydn::R0)
          .addReg(Haydn::R0);
    }
    return Home;
  }

  // Nested SP brackets: each spill owns 8 bytes at [sp]; restore LIFO.
  Home.K = VAARGSpillHome::SPBracket;
  BuildMI(MBB, I, DL, TII.get(Haydn::SUBI32), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(8);
  BuildMI(MBB, I, DL, TII.get(Haydn::ST32))
      .addReg(Scr)
      .addReg(Haydn::R13)
      .addImm(0);
  return Home;
}

static void vaargEndSpill(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                          const DebugLoc &DL, const TargetInstrInfo &TII,
                          Register Scr, const VAARGSpillHome &Home) {
  if (Home.K == VAARGSpillHome::None)
    return;
  if (Home.K == VAARGSpillHome::FrameIndex) {
    if (isInt<16>(Home.Off)) {
      BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr)
          .addReg(Home.FrameReg)
          .addImm(Home.Off);
    } else {
      BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Haydn::R0)
          .addReg(Home.FrameReg)
          .addImm(Home.Off);
      BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr)
          .addReg(Haydn::R0)
          .addImm(0);
      BuildMI(MBB, I, DL, TII.get(Haydn::XOR32), Haydn::R0)
          .addReg(Haydn::R0)
          .addReg(Haydn::R0);
    }
    return;
  }
  BuildMI(MBB, I, DL, TII.get(Haydn::LD32), Scr)
      .addReg(Haydn::R13)
      .addImm(0);
  BuildMI(MBB, I, DL, TII.get(Haydn::ADDI32_W), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(8);
}

} // namespace

bool HaydnExpandPseudos::expandVAARG(MachineBasicBlock &MBB, MachineInstr &MI,
                                     MachineBasicBlock::iterator &NextMBBI,
                                     bool IsI64) {
  MachineFunction &MF = *MBB.getParent();
  auto *FuncInfo = MF.getInfo<HaydnMachineFunctionInfo>();
  DebugLoc DL = MI.getDebugLoc();

  Register Dst = MI.getOperand(0).getReg();
  Register VaList = MI.getOperand(1).getReg();

  // va_list field offsets (must match VASTART / ISel comments).
  constexpr int kStackField = 0;
  constexpr int kGrTopField = 4;
  constexpr int kVrTopField = 8;
  constexpr int kGrOffsField = 12;
  constexpr int kVrOffsField = 16;
  // HaydnCallingConv.td CCAssignToStack<8, 8>.
  constexpr int64_t kStackStep = 8;

  const int TopField = IsI64 ? kVrTopField : kGrTopField;
  const int OffsField = IsI64 ? kVrOffsField : kGrOffsField;
  const int64_t RegStep = IsI64 ? 8 : 4;
  const unsigned LoadOpc = IsI64 ? Haydn::LD64 : Haydn::LD32;

  // Do NOT gate on hasVarArgsSaveAreas(). VAARG only walks the structured
  // va_list in memory; the list may have been created in a *different*
  // function (stdarg-2: foo(int, va_list) / bar reading global gap). Gating
  // on the current MF being variadic erased those VAARGs and left Dst
  // undefined → ABORT. VASTART still requires save areas in *its* MF.
  (void)FuncInfo;

  // --- Scavenge 3 GPRs at MI (free first; spill live ones). ---
  // Only one FI home exists; if more than one scratch needs a spill we fall
  // back to SP brackets (nested). Restores always land on Join, never after
  // the MBB terminators.
  SmallVector<Register, 4> Exclude;
  if (VaList.isPhysical())
    Exclude.push_back(VaList);
  if (Dst.isPhysical())
    Exclude.push_back(Dst);

  bool Spill0 = false, Spill1 = false, Spill2 = false;
  Register S0 =
      findPostRAScratchGPR(MBB, MI.getIterator(), /*PreferNotR12=*/true,
                           Spill0, Exclude);
  Exclude.push_back(S0);
  Register S1 =
      findPostRAScratchGPR(MBB, MI.getIterator(), /*PreferNotR12=*/true,
                           Spill1, Exclude);
  Exclude.push_back(S1);
  Register S2 =
      findPostRAScratchGPR(MBB, MI.getIterator(), /*PreferNotR12=*/true,
                           Spill2, Exclude);

  // --- Split MBB at MI: rest → Join; Stack/Reg arms; head stays in MBB. ---
  const BasicBlock *BB = MBB.getBasicBlock();
  MachineBasicBlock *StackMBB = MF.CreateMachineBasicBlock(BB);
  MachineBasicBlock *RegMBB = MF.CreateMachineBasicBlock(BB);
  MachineBasicBlock *JoinMBB = MF.CreateMachineBasicBlock(BB);
  MachineFunction::iterator MIt = std::next(MBB.getIterator());
  // Layout: MBB → Stack → Reg → Join → <old next> (explicit B, no fallthrough).
  MF.insert(MIt, StackMBB);
  MF.insert(MIt, RegMBB);
  MF.insert(MIt, JoinMBB);

  JoinMBB->splice(JoinMBB->end(), &MBB, std::next(MI.getIterator()), MBB.end());
  JoinMBB->transferSuccessorsAndUpdatePHIs(&MBB);

  // Spill lives at head (before MI). Always SP-bracket (never shared
  // PostRAScratchFI): the FI is a single slot also used by branch-relax /
  // other post-RA scavenges in the same window — two st32 to the same home
  // clobbered the loop IV (looped va_arg ABORT). Nested brackets: S0,S1,S2
  // then restore S2,S1,S0 at Join.
  MachineBasicBlock::iterator HeadPt = MI.getIterator();
  VAARGSpillHome Home0, Home1, Home2;
  if (Spill0)
    Home0 = vaargBeginSpill(MBB, HeadPt, DL, *TII, *STI, S0,
                            /*UseAllowFI=*/false);
  if (Spill1)
    Home1 = vaargBeginSpill(MBB, HeadPt, DL, *TII, *STI, S1,
                            /*UseAllowFI=*/false);
  if (Spill2)
    Home2 = vaargBeginSpill(MBB, HeadPt, DL, *TII, *STI, S2,
                            /*UseAllowFI=*/false);

  // Head: load offs/top, UseStack = (offs + RegStep > 0).
  //   S0 = CurOff, S1 = Top, S2 = Tentative then UseStack.
  BuildMI(MBB, HeadPt, DL, TII->get(Haydn::LD32), S0)
      .addReg(VaList)
      .addImm(OffsField);
  BuildMI(MBB, HeadPt, DL, TII->get(Haydn::LD32), S1)
      .addReg(VaList)
      .addImm(TopField);
  BuildMI(MBB, HeadPt, DL, TII->get(Haydn::ADDI32_W), S2)
      .addReg(S0)
      .addImm(RegStep);
  // UseStack = (0 < Tentative) → SLT rd, r0, Tentative; overwrites Tentative.
  BuildMI(MBB, HeadPt, DL, TII->get(Haydn::SLT32), S2)
      .addReg(Haydn::R0)
      .addReg(S2);

  // Drop the pseudo now so terminators are truly last in MBB.
  MI.eraseFromParent();

  // MBB ends with: BNEZ UseStack → Stack; B → Reg.
  BuildMI(MBB, MBB.end(), DL, TII->get(Haydn::BNEZ))
      .addReg(S2)
      .addMBB(StackMBB);
  BuildMI(MBB, MBB.end(), DL, TII->get(Haydn::B)).addMBB(RegMBB);
  MBB.addSuccessor(StackMBB);
  MBB.addSuccessor(RegMBB);

  // Result materialization: never write Dst before va_list stores if Dst may
  // alias VaList (common: last va_arg into the return GPR that held ap).
  // I32: load into S2, update list, then Dst = S2. I64: Dst is DR — cannot
  // alias VaList (GPR); load into Dst after address is ready, still update
  // list using VaList before any further uses of Dst as a GPR (n/a).
  auto EmitI32Result = [&](MachineBasicBlock &BB,
                           MachineBasicBlock::iterator Ins, Register Val) {
    if (Dst != Val)
      BuildMI(BB, Ins, DL, TII->get(Haydn::ADD32), Dst)
          .addReg(Val)
          .addReg(Haydn::R0);
  };

  // --- Stack path: val = *__stack; __stack += 8. ---
  {
    auto Ins = StackMBB->end();
    BuildMI(*StackMBB, Ins, DL, TII->get(Haydn::LD32), S1)
        .addReg(VaList)
        .addImm(kStackField);
    if (IsI64) {
      BuildMI(*StackMBB, Ins, DL, TII->get(LoadOpc), Dst)
          .addReg(S1)
          .addImm(0);
      BuildMI(*StackMBB, Ins, DL, TII->get(Haydn::ADDI32_W), S0)
          .addReg(S1)
          .addImm(kStackStep);
      BuildMI(*StackMBB, Ins, DL, TII->get(Haydn::ST32))
          .addReg(S0)
          .addReg(VaList)
          .addImm(kStackField);
    } else {
      // Value in S2 first so VaList stays valid for the cursor store.
      BuildMI(*StackMBB, Ins, DL, TII->get(Haydn::LD32), S2)
          .addReg(S1)
          .addImm(0);
      BuildMI(*StackMBB, Ins, DL, TII->get(Haydn::ADDI32_W), S0)
          .addReg(S1)
          .addImm(kStackStep);
      BuildMI(*StackMBB, Ins, DL, TII->get(Haydn::ST32))
          .addReg(S0)
          .addReg(VaList)
          .addImm(kStackField);
      EmitI32Result(*StackMBB, Ins, S2);
    }
    BuildMI(*StackMBB, Ins, DL, TII->get(Haydn::B)).addMBB(JoinMBB);
    StackMBB->addSuccessor(JoinMBB);
  }

  // --- Reg path: reload offs/top, val = *(top+off), offs += n. ---
  {
    auto Ins = RegMBB->end();
    BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::LD32), S0)
        .addReg(VaList)
        .addImm(OffsField);
    BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::LD32), S1)
        .addReg(VaList)
        .addImm(TopField);
    BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::ADD32), S1)
        .addReg(S1)
        .addReg(S0);
    if (IsI64) {
      BuildMI(*RegMBB, Ins, DL, TII->get(LoadOpc), Dst)
          .addReg(S1)
          .addImm(0);
      BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::ADDI32_W), S0)
          .addReg(S0)
          .addImm(RegStep);
      BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::ST32))
          .addReg(S0)
          .addReg(VaList)
          .addImm(OffsField);
    } else {
      BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::LD32), S2)
          .addReg(S1)
          .addImm(0);
      BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::ADDI32_W), S0)
          .addReg(S0)
          .addImm(RegStep);
      BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::ST32))
          .addReg(S0)
          .addReg(VaList)
          .addImm(OffsField);
      EmitI32Result(*RegMBB, Ins, S2);
    }
    BuildMI(*RegMBB, Ins, DL, TII->get(Haydn::B)).addMBB(JoinMBB);
    RegMBB->addSuccessor(JoinMBB);
  }

  // Restore borrowed lives at Join entry (before residual code). Reverse of
  // spill order so nested SP brackets unwind correctly.
  {
    auto JoinPt = JoinMBB->begin();
    if (Spill2)
      vaargEndSpill(*JoinMBB, JoinPt, DL, *TII, S2, Home2);
    if (Spill1)
      vaargEndSpill(*JoinMBB, JoinPt, DL, *TII, S1, Home1);
    if (Spill0)
      vaargEndSpill(*JoinMBB, JoinPt, DL, *TII, S0, Home0);
  }

  // Post-RA split live-ins: regs live across the original VAARG must enter
  // Stack/Reg/Join; Dst is defined on both arms → live-in Join; VaList used
  // on both arms.
  {
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
    LivePhysRegs LPR(TRI);
    LPR.addLiveOuts(*JoinMBB);
    for (MachineBasicBlock::iterator II = JoinMBB->end();
         II != JoinMBB->begin();) {
      --II;
      LPR.stepBackward(*II);
    }
    for (MCRegister R : LPR) {
      if (!R)
        continue;
      if (!StackMBB->isLiveIn(R))
        StackMBB->addLiveIn(R);
      if (!RegMBB->isLiveIn(R))
        RegMBB->addLiveIn(R);
      if (!JoinMBB->isLiveIn(R))
        JoinMBB->addLiveIn(R);
    }
    if (Dst.isPhysical() && !JoinMBB->isLiveIn(Dst))
      JoinMBB->addLiveIn(Dst);
    if (VaList.isPhysical()) {
      if (!StackMBB->isLiveIn(VaList))
        StackMBB->addLiveIn(VaList);
      if (!RegMBB->isLiveIn(VaList))
        RegMBB->addLiveIn(VaList);
    }
  }

  // Expand loop: do not walk into the new Stack/Reg/Join blocks from here;
  // runOnMachineFunction iterates all MBBs and will expand Join later.
  NextMBBI = MBB.end();
  return true;
}

//===----------------------------------------------------------------------===//
// LOAD_ADDR: materialize a 32-bit global address
//===----------------------------------------------------------------------===//

bool HaydnExpandPseudos::expandLOAD_ADDR(MachineBasicBlock &MBB,
                                         MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  Register DstReg = MI.getOperand(0).getReg();
  const MachineOperand &AddrOp = MI.getOperand(1);

  // For immediate addresses: chain the HaydnMatInt constant-materialisation
  // sequence (correct 12-bit LUI; handles the bits[19:16] "hole" that LUI+ADDI32
  // cannot reach) from R0 into DstReg. Post-RA, so chain through the physical
  // DstReg (first instr reads R0, the rest read DstReg). ISA-43 #1.
  if (AddrOp.isImm()) {
    HaydnMatInt::InstSeq Seq = HaydnMatInt::generate(AddrOp.getImm());
    Register Cur = Haydn::R0;
    for (const HaydnMatInt::Inst &MatInst : Seq) {
      BuildMI(MBB, MI, DL, TII->get(MatInst.Opc), DstReg)
          .addReg(Cur)
          .addImm(MatInst.Imm);
      Cur = DstReg;
    }
    MI.eraseFromParent();
    return true;
  }

  // For global addresses: LUI + ADDI32_W with relocations applied by the linker.
  // The MC layer emits HI12/LO20 fixups based on the instruction opcode.
  if (AddrOp.isGlobal() || AddrOp.isSymbol()) {
    BuildMI(MBB, MI, DL, TII->get(Haydn::LUI), DstReg)
        .addReg(Haydn::R0)
        .add(AddrOp);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), DstReg)
        .addReg(DstReg)
        .add(AddrOp);
    MI.eraseFromParent();
    return true;
  }

  // For constant pool indices and jump table addresses.
  if (AddrOp.isCPI()) {
    BuildMI(MBB, MI, DL, TII->get(Haydn::LUI), DstReg)
        .addReg(Haydn::R0)
        .add(AddrOp);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), DstReg)
        .addReg(DstReg)
        .add(AddrOp);
    MI.eraseFromParent();
    return true;
  }

  // For block addresses (e.g., taking address of a basic block).
  if (AddrOp.isBlockAddress()) {
    BuildMI(MBB, MI, DL, TII->get(Haydn::LUI), DstReg)
        .addReg(Haydn::R0)
        .add(AddrOp);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), DstReg)
        .addReg(DstReg)
        .add(AddrOp);
    MI.eraseFromParent();
    return true;
  }

  // For jump table indices — the.LJTI_N_M label lives in.rodata, the same
  // high-memory region as globals, so it needs the full HI12/LO20 pair. A bare
  // ADDI32 only carries a LO16 fixup and cannot reach the rodata base, which
  // corrupts the JT base and produces OOB loads.
  if (AddrOp.isJTI()) {
    BuildMI(MBB, MI, DL, TII->get(Haydn::LUI), DstReg)
        .addReg(Haydn::R0)
        .add(AddrOp);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), DstReg)
        .addReg(DstReg)
        .add(AddrOp);
    MI.eraseFromParent();
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// ADJCALLSTACKDOWN / ADJCALLSTACKUP: adjust SP around calls
//===----------------------------------------------------------------------===//

bool HaydnExpandPseudos::expandADJCALLSTACKDOWN(MachineBasicBlock &MBB,
                                                MachineInstr &MI) {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // If the frame information says we don't need call frame manipulation
  // just erase the pseudo. The PEI pass handles the adjustment via
  // getMaxCallFrameSize incorporated into the static stack size.
  if (MFI.isCalleeSavedInfoValid() && !MFI.hasVarSizedObjects()) {
    MI.eraseFromParent();
    return true;
  }

  DebugLoc DL = MI.getDebugLoc();
  int64_t Amount = MI.getOperand(0).getImm();

  if (Amount == 0) {
    MI.eraseFromParent();
    return true;
  }

  // SUBI32 SP, SP, Amount
  assert(isInt<16>(Amount) && "Call frame adjustment exceeds simm16 range");
  BuildMI(MBB, MI, DL, TII->get(Haydn::SUBI32), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(Amount);

  MI.eraseFromParent();
  return true;
}

bool HaydnExpandPseudos::expandADJCALLSTACKUP(MachineBasicBlock &MBB,
                                              MachineInstr &MI) {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  if (MFI.isCalleeSavedInfoValid() && !MFI.hasVarSizedObjects()) {
    MI.eraseFromParent();
    return true;
  }

  DebugLoc DL = MI.getDebugLoc();
  int64_t Amount = MI.getOperand(0).getImm();

  if (Amount == 0) {
    MI.eraseFromParent();
    return true;
  }

  // ADDI32 SP, SP, Amount
  assert(isInt<16>(Amount) && "Call frame adjustment exceeds simm16 range");
  BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), Haydn::R13)
      .addReg(Haydn::R13)
      .addImm(Amount);

  MI.eraseFromParent();
  return true;
}

//===----------------------------------------------------------------------===//
// PseudoCALL: expand to JAL_W R15, target (Phase 1a: 48-bit WIDE).
//===----------------------------------------------------------------------===//

bool HaydnExpandPseudos::expandPseudoCALL(MachineBasicBlock &MBB,
                                          MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  const MachineOperand &Target = MI.getOperand(1);
  MachineFunction &MF = *MBB.getParent();

  // JAL_W R15, target — stores return address in R15 (LR) and jumps.
  auto MIB = BuildMI(MBB, MI, DL, TII->get(Haydn::JAL_W), Haydn::R15).add(Target);

  // Transfer any implicit operands from the pseudo (e.g., callee-saved defs).
  bool HasRegMask = false;
  for (const MachineOperand &MO : MI.implicit_operands()) {
    if (MO.isRegMask())
      HasRegMask = true;
    MIB.add(MO);
  }
  // Ensure the expanded call carries a regmask so caller-saved GPRs
  // (incl. R12) appear clobbered to post-RA cleanup/scheduling.
  if (!HasRegMask) {
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
    const uint32_t *Mask =
        TRI->getCallPreservedMask(MF, MF.getFunction().getCallingConv());
    assert(Mask && "Missing call preserved mask for calling convention");
    MIB.addRegMask(Mask);
  }

  MI.eraseFromParent();
  return true;
}

//===----------------------------------------------------------------------===//
// Libcall pseudos: expand to a JAL to compiler-rt function
//===----------------------------------------------------------------------===//

bool HaydnExpandPseudos::expandLibcall(MachineBasicBlock &MBB, MachineInstr &MI,
                                       const char *Symbol) {
  DebugLoc DL = MI.getDebugLoc();

  // Determine which register class the result and operands use.
  // For 32-bit division/remainder: GPR32 inputs and output.
  // For 64-bit multiply: DR64 inputs and output.
  bool Is64Bit = (MI.getOpcode() == Haydn::LIBCALL_MUL64);

  unsigned ResultReg = MI.getOperand(0).getReg();

  if (Is64Bit) {
    // LIBCALL_MUL64: D0 = __muldi3(D0, D1)
    // Input operands are in rs1, rs2. Move them into the argument registers
    // before the call.
    Register Rs1 = MI.getOperand(1).getReg();
    Register Rs2 = MI.getOperand(2).getReg();

    // Move arguments into D0, D1.
    if (Rs1 != Haydn::D0) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::OR64), Haydn::D0)
          .addReg(Rs1, getKillRegState(true))
          .addReg(Rs1, getKillRegState(true));
    }
    if (Rs2 != Haydn::D1) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::OR64), Haydn::D1)
          .addReg(Rs2, getKillRegState(true))
          .addReg(Rs2, getKillRegState(true));
    }
  } else {
    // 32-bit libcalls: R0 = __divsi3(R0, R1) etc.
    // Note: R0 here means the first argument register, not the zero register.
    // Haydn calling convention: first GPR arg = R0, second = R1.
    // But R0 is hardwired to zero! This is a design issue.
    //
    // Haydn calling convention uses R1-R4 for arguments (R0 is zero).
    // The selector should have placed arguments in the correct registers.
    // For 32-bit: first arg in R1, second in R2, result in R1.
    // But the pseudo has virtual register operands — we need to move them
    // into the physical argument registers.
    Register Rs1 = MI.getOperand(1).getReg();
    Register Rs2 = MI.getOperand(2).getReg();

    // Move arguments into calling convention registers.
    // Haydn calling convention: args in R1, R2, R3, R4; result in R1.
    if (Rs1 != Haydn::R1) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::ADD32), Haydn::R1)
          .addReg(Rs1, getKillRegState(true))
          .addReg(Haydn::R0);
    }
    if (Rs2 != Haydn::R2) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::ADD32), Haydn::R2)
          .addReg(Rs2, getKillRegState(true))
          .addReg(Haydn::R0);
    }
  }

  // Emit the call: JAL_W R15, symbol (Phase 1a: 48-bit WIDE).
  // Attach CSR_Haydn regmask so reserved AT (R12) and other non-CSR regs are
  // modeled as call-clobbered. TableGen Defs on LIBCALL_* cover the
  // pseudo itself; the expanded JAL_W must carry the mask for post-RA passes.
  {
    MachineFunction &MF = *MBB.getParent();
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
    const uint32_t *Mask =
        TRI->getCallPreservedMask(MF, MF.getFunction().getCallingConv());
    assert(Mask && "Missing call preserved mask for calling convention");
    BuildMI(MBB, MI, DL, TII->get(Haydn::JAL_W), Haydn::R15)
        .addExternalSymbol(Symbol)
        .addRegMask(Mask);
  }

  // Move result from the return register to the destination.
  if (Is64Bit) {
    // 64-bit result is in D0 (first DR64 arg/result register).
    if (ResultReg != Haydn::D0) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::OR64), ResultReg)
          .addReg(Haydn::D0)
          .addReg(Haydn::D0);
    }
  } else {
    // 32-bit result is in R1 (first GPR arg/result register, since R0=zero).
    if (ResultReg != Haydn::R1) {
      BuildMI(MBB, MI, DL, TII->get(Haydn::ADD32), ResultReg)
          .addReg(Haydn::R1)
          .addReg(Haydn::R0);
    }
  }

  MI.eraseFromParent();
  return true;
}

//===----------------------------------------------------------------------===//
// Post-increment addressing mode expansion
//===----------------------------------------------------------------------===//
//
// These pseudos are created by the HaydnLoadStoreOptimizer when it detects
// a load/store followed by an ADDI32 base-register update. Currently they
// expand back into the two-instruction sequence (load/store + ADDI32).
//
// When the MC layer supports native S_LW_POST_IMM / S_SW_POST_IMM encoding
// these expansions can be replaced with direct emission of the native
// post-increment instructions, saving one instruction per converted sequence.
//===----------------------------------------------------------------------===//

bool HaydnExpandPseudos::expandLD32PostInc(MachineBasicBlock &MBB,
                                            MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  Register DstReg = MI.getOperand(0).getReg();
  Register BaseReg = MI.getOperand(1).getReg();
  int64_t Stride = MI.getOperand(2).getImm();
  // Operand 3 is the preserved displacement. Pre-existing post-inc
  // formations pass 0 here; the cross-bank fold's offset-4 LD32 passes 4.
  int64_t Offset = MI.getOperand(3).getImm();

  // LD32 rt, base, offset (load from base + offset, preserving displacement)
  BuildMI(MBB, MI, DL, TII->get(Haydn::LD32), DstReg)
      .addReg(BaseReg)
      .addImm(Offset);

  // ADDI32 base, base, stride (update base by stride)
  BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), BaseReg)
      .addReg(BaseReg)
      .addImm(Stride);

  MI.eraseFromParent();
  return true;
}

bool HaydnExpandPseudos::expandST32PostInc(MachineBasicBlock &MBB,
                                            MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  Register DataReg = MI.getOperand(0).getReg();
  Register BaseReg = MI.getOperand(1).getReg();
  int64_t Stride = MI.getOperand(2).getImm();
  int64_t Offset = MI.getOperand(3).getImm();

  // ST32 rt, base, offset (store to base + offset, preserving displacement)
  BuildMI(MBB, MI, DL, TII->get(Haydn::ST32))
      .addReg(DataReg)
      .addReg(BaseReg)
      .addImm(Offset);

  // ADDI32 base, base, stride (update base by stride)
  BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), BaseReg)
      .addReg(BaseReg)
      .addImm(Stride);

  MI.eraseFromParent();
  return true;
}

bool HaydnExpandPseudos::expandLD64PostInc(MachineBasicBlock &MBB,
                                            MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  Register DstReg = MI.getOperand(0).getReg();
  Register BaseReg = MI.getOperand(1).getReg();
  int64_t Stride = MI.getOperand(2).getImm();
  int64_t Offset = MI.getOperand(3).getImm();

  // plain LD64 (slot 0/1) so this load can pack with a sibling.
  BuildMI(MBB, MI, DL, TII->get(Haydn::LD64), DstReg)
      .addReg(BaseReg)
      .addImm(Offset);

  // ADDI32 base, base, stride (update base by stride)
  BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), BaseReg)
      .addReg(BaseReg)
      .addImm(Stride);

  MI.eraseFromParent();
  return true;
}

bool HaydnExpandPseudos::expandST64PostInc(MachineBasicBlock &MBB,
                                            MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  Register DataReg = MI.getOperand(0).getReg();
  Register BaseReg = MI.getOperand(1).getReg();
  int64_t Stride = MI.getOperand(2).getImm();
  int64_t Offset = MI.getOperand(3).getImm();

  // ST64 rt, base, offset (store 64-bit to base + offset)
  BuildMI(MBB, MI, DL, TII->get(Haydn::ST64))
      .addReg(DataReg)
      .addReg(BaseReg)
      .addImm(Offset);

  // ADDI32 base, base, stride (update base by stride)
  BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), BaseReg)
      .addReg(BaseReg)
      .addImm(Stride);

  MI.eraseFromParent();
  return true;
}