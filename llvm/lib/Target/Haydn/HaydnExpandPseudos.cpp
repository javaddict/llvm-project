//===-- HaydnExpandPseudos.cpp - Expand pseudo instructions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Post-RA expansion of Haydn pseudos that require physical registers:
// LOAD_ADDR, SETCBR, leftover *_POST_INC (MIR-injected; product form is ISel),
// SET_HWLOOP descriptor rewrite, VAEND no-op, and soft-zero R0 maintenance.
//
// Pseudos already handled by HaydnInstrInfo::expandPostRAPseudo (RET, B,
// LOADI32, MOV_GPR_TO_DR64, MOV_DR64_TO_GPR) are NOT duplicated here.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "HaydnBundleVerify.h"
#include "HaydnExpandPseudos.h"
#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "haydn-expand-pseudos"

using namespace llvm;

static void setMemRefs(MachineInstrBuilder &MIB,
                       ArrayRef<MachineMemOperand *> MMOs) {
  if (!MMOs.empty())
    MIB.setMemRefs(MMOs);
}

/// Leftover *_POST_INC → fused AGU (zero displacement, scaled simm6) or
/// LD/ST + ADDI32_W. Product form is selected at InstructionSelect; this is
/// the single post-RA residual for MIR-injected leftovers (expand-owned
/// until pack). Same fused-vs-split predicate as ISel (isLegalScaledSimm6).
static bool expandPostIncPseudo(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator InsertBefore,
                                const DebugLoc &DL, const HaydnInstrInfo &TII,
                                unsigned Opcode, Register DataOrDst,
                                Register Base, int64_t Stride, int64_t Offset,
                                ArrayRef<MachineMemOperand *> MMOs) {
  switch (Opcode) {
  case Haydn::LD32_POST_INC: {
    if (Offset == 0 && (Stride % 4) == 0 && isInt<6>(Stride >> 2)) {
      auto MIB = BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::S_LW_POST_IMM))
                     .addReg(DataOrDst, RegState::Define)
                     .addReg(Base, RegState::Define)
                     .addReg(Base)
                     .addImm(Stride >> 2);
      setMemRefs(MIB, MMOs);
      return true;
    }
    assert(Offset % 4 == 0 && "LD32_POST_INC displacement must be word-aligned");
    {
      auto MIB = BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::LD32), DataOrDst)
                     .addReg(Base)
                     .addImm(Offset >> 2);
      setMemRefs(MIB, MMOs);
    }
    BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::ADDI32_W), Base)
        .addReg(Base)
        .addImm(Stride);
    return true;
  }
  case Haydn::ST32_POST_INC: {
    if (Offset == 0 && (Stride % 4) == 0 && isInt<6>(Stride >> 2)) {
      auto MIB = BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::ST32_POST))
                     .addReg(Base, RegState::Define)
                     .addReg(DataOrDst)
                     .addReg(Base)
                     .addImm(Stride >> 2);
      setMemRefs(MIB, MMOs);
      return true;
    }
    assert(Offset % 4 == 0 && "ST32_POST_INC displacement must be word-aligned");
    {
      auto MIB = BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::ST32))
                     .addReg(DataOrDst)
                     .addReg(Base)
                     .addImm(Offset >> 2);
      setMemRefs(MIB, MMOs);
    }
    BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::ADDI32_W), Base)
        .addReg(Base)
        .addImm(Stride);
    return true;
  }
  case Haydn::LD64_POST_INC: {
    if (Offset == 0 && (Stride % 8) == 0 && isInt<6>(Stride >> 3)) {
      auto MIB = BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::D_LDW_POST_IMM))
                     .addReg(DataOrDst, RegState::Define)
                     .addReg(Base, RegState::Define)
                     .addReg(Base)
                     .addImm(Stride >> 3);
      setMemRefs(MIB, MMOs);
      return true;
    }
    assert(Offset % 8 == 0 &&
           "LD64_POST_INC displacement must be dword-aligned");
    {
      auto MIB = BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::LD64), DataOrDst)
                     .addReg(Base)
                     .addImm(Offset >> 3);
      setMemRefs(MIB, MMOs);
    }
    BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::ADDI32_W), Base)
        .addReg(Base)
        .addImm(Stride);
    return true;
  }
  case Haydn::ST64_POST_INC: {
    if (Offset == 0 && (Stride % 8) == 0 && isInt<6>(Stride >> 3)) {
      auto MIB = BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::ST64_POST))
                     .addReg(Base, RegState::Define)
                     .addReg(DataOrDst)
                     .addReg(Base)
                     .addImm(Stride >> 3);
      setMemRefs(MIB, MMOs);
      return true;
    }
    assert(Offset % 8 == 0 &&
           "ST64_POST_INC displacement must be dword-aligned");
    {
      auto MIB = BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::ST64))
                     .addReg(DataOrDst)
                     .addReg(Base)
                     .addImm(Offset >> 3);
      setMemRefs(MIB, MMOs);
    }
    BuildMI(MBB, InsertBefore, DL, TII.get(Haydn::ADDI32_W), Base)
        .addReg(Base)
        .addImm(Stride);
    return true;
  }
  default:
    return false;
  }
}

/// Leftover *_POST_INC inside a BUNDLE: expandMBB only visits top-level MIs.
/// Product form is ISel; this is the MIR-injected residual
/// (pseudo-in-bundle.mir). Uses expandPostIncPseudo — not a third copy.
static bool expandBundledPostIncLeftovers(MachineBasicBlock &MBB,
                                          const HaydnInstrInfo &TII) {
  bool Modified = false;

  SmallVector<MachineInstr *, 4> Bundles;
  for (MachineInstr &MI : MBB) {
    if (MI.isBundle())
      Bundles.push_back(&MI);
  }

  for (MachineInstr *Bundle : Bundles) {
    SmallVector<MachineInstr *, 4> PseudosToExpand;
    MachineBasicBlock::instr_iterator I = std::next(Bundle->getIterator());
    for (MachineBasicBlock::instr_iterator E = MBB.instr_end();
         I != E && I->isInsideBundle(); ++I) {
      switch (I->getOpcode()) {
      case Haydn::LD32_POST_INC:
      case Haydn::ST32_POST_INC:
      case Haydn::LD64_POST_INC:
      case Haydn::ST64_POST_INC:
        PseudosToExpand.push_back(&*I);
        break;
      default:
        break;
      }
    }

    if (PseudosToExpand.empty())
      continue;

    Modified = true;
    for (MachineInstr *PseudoMI : PseudosToExpand) {
      DebugLoc DL = PseudoMI->getDebugLoc();
      unsigned Opcode = PseudoMI->getOpcode();
      Register DstReg = PseudoMI->getOperand(0).getReg();
      Register BaseReg = PseudoMI->getOperand(1).getReg();
      int64_t Stride = PseudoMI->getOperand(2).getImm();
      int64_t Offset = PseudoMI->getOperand(3).getImm();
      SmallVector<MachineMemOperand *, 2> MMOs(PseudoMI->memoperands_begin(),
                                               PseudoMI->memoperands_end());
      PseudoMI->eraseFromBundle();
      expandPostIncPseudo(MBB, MachineBasicBlock::iterator(Bundle), DL, TII,
                          Opcode, DstReg, BaseReg, Stride, Offset, MMOs);
    }

    MachineBasicBlock::instr_iterator BI = std::next(Bundle->getIterator());
    if (BI == MBB.instr_end() || !BI->isInsideBundle())
      Bundle->eraseFromParent();
  }

  return Modified;
}

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

  // After calls: JAL/JAL_W and PseudoCALLIndirect. Callee RET is
  // JALR_W r0,lr which clobbers R0. Direct calls are JAL_W from CallLowering.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator MII = MBB.begin(), E = MBB.end();
         MII != E;) {
      MachineInstr &MI = *MII;
      ++MII;
      unsigned Opc = MI.getOpcode();
      bool NeedsPostCallZero =
          Opc == Haydn::JAL || Opc == Haydn::JAL_W ||
          Opc == Haydn::PseudoCALLIndirect;
      if (!NeedsPostCallZero)
        continue;
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
  TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();

  bool Modified = false;
  for (MachineBasicBlock &MBB : MF)
    Modified |= expandMBB(MBB);

  // Bundle interior leftover *_POST_INC. Product post-inc form is ISel;
  // MIR-injected residuals use expandPostIncPseudo.
  for (MachineBasicBlock &MBB : MF)
    Modified |= expandBundledPostIncLeftovers(MBB, *TII);

  // Runs after expand so real JAL_W is visible. Single named owner of
  // architectural R0 restore (HaydnPostRAScratch owns borrow/restore at
  // scavenge sites; this pass owns post-call / indirect-target insertion).
  Modified |= insertSoftZeroR0Maintenance(MF);

  // Residual matrix: anything this pass is responsible for expanding must be
  // gone. Relocated surfaces (VASTART/VAARG/LIBCALL/PseudoCALL/ADJCALLSTACK)
  // remain expand-owned at the late firewall so leftovers still fail closed.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      if (MI.isBundle())
        continue;
      if (!haydn::bundle::isExpandOwnedSemanticPseudo(MI.getOpcode()))
        continue;
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << "HaydnExpandPseudos: residual expand-owned semantic pseudo in "
         << MF.getName() << " BB#" << MBB.getNumber()
         << " (must expand before pack):\n  MI: " << MI;
      report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
    }
  }

  return Modified;
}

bool HaydnExpandPseudos::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NextMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, *MBBI);
    MBBI = NextMBBI;
  }
  return Modified;
}

bool HaydnExpandPseudos::expandMI(MachineBasicBlock &MBB, MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;

  case Haydn::LOAD_ADDR:
    return expandLOAD_ADDR(MBB, MI);

  case Haydn::LD32_POST_INC:
  case Haydn::ST32_POST_INC:
  case Haydn::LD64_POST_INC:
  case Haydn::ST64_POST_INC: {
    SmallVector<MachineMemOperand *, 2> MMOs(MI.memoperands_begin(),
                                             MI.memoperands_end());
    if (!expandPostIncPseudo(MBB, MI.getIterator(), MI.getDebugLoc(), *TII,
                             MI.getOpcode(), MI.getOperand(0).getReg(),
                             MI.getOperand(1).getReg(), MI.getOperand(2).getImm(),
                             MI.getOperand(3).getImm(), MMOs))
      return false;
    MI.eraseFromParent();
    return true;
  }

  case Haydn::VAEND:
    MI.eraseFromParent();
    return true;

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

  case Haydn::SETCBR_BEGIN:
  case Haydn::SETCBR_END: {
    assert(MI.getOperand(0).isImm() && "SETCBR: cbr_sel must be immediate");
    assert(MI.getOperand(1).isReg() && "SETCBR: val must be register");
    unsigned CbrSel = MI.getOperand(0).getImm();
    assert((CbrSel == 0 || CbrSel == 1) && "SETCBR: cbr_sel must be 0 or 1");
    const MachineOperand &ValMO = MI.getOperand(1);
    Register ValReg = ValMO.getReg();
    unsigned CsrBase =
        (MI.getOpcode() == Haydn::SETCBR_BEGIN) ? 0x2Cu : 0x2Du;
    unsigned CsrAddr = CsrBase + (CbrSel << 1);
    Register CbrReg = (CbrSel == 0) ? Haydn::CBR0 : Haydn::CBR1;
    DebugLoc DL = MI.getDebugLoc();
    BuildMI(MBB, MI, DL, TII->get(Haydn::CSRW_W))
        .addImm(CsrAddr)
        .addReg(ValReg, getKillRegState(ValMO.isKill()))
        .addReg(CbrReg, RegState::ImplicitDefine);
    MI.eraseFromParent();
    return true;
  }
  }
  return false;
}

bool HaydnExpandPseudos::expandLOAD_ADDR(MachineBasicBlock &MBB,
                                         MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  Register DstReg = MI.getOperand(0).getReg();
  const MachineOperand &AddrOp = MI.getOperand(1);

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

  if (AddrOp.isGlobal() || AddrOp.isSymbol() || AddrOp.isCPI() ||
      AddrOp.isBlockAddress() || AddrOp.isJTI()) {
    BuildMI(MBB, MI, DL, TII->get(Haydn::LUI), DstReg)
        .addReg(Haydn::R0)
        .add(AddrOp);
    BuildMI(MBB, MI, DL, TII->get(Haydn::ADDI32_W), DstReg)
        .addReg(DstReg)
        .add(AddrOp);
    MI.eraseFromParent();
    return true;
  }

  std::string Msg;
  raw_string_ostream OS(Msg);
  OS << "HaydnExpandPseudos: LOAD_ADDR operand kind is not expandable "
        "(imm/global/symbol/CPI/BA/JTI only; no FI residual):\n  MI: "
     << MI;
  report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
  return false;
}
