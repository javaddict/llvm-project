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
// leftover generic SET_HWLOOP{,_REG} rewrite, and VAEND no-op.
// Soft-zero R0 restore after calls is
// HaydnPostRAScratch::insertSoftZeroR0AfterCalls (this pass is the
// post-leftover call site). Product SET is SET_HWLOOP_F2_W at
// HardwareLoops (HaydnHardwareLoops.cpp:701; AIE createAIEBaseHardwareLoopsPass
// at AIE2TargetMachine.cpp:235). Leftover expand-owned semantic
// pseudos, and leftover cycle-forming SET_HWLOOP / SETCBR / LOOPCTL /
// LOAD_ADDR / LOADI32 as BUNDLE children, are fatal after this pass
// (AIE AIEPseudoBranchExpansion.cpp:43-57 expands named branch desc only;
// Haydn in-bundle expand is leftover *_POST_INC).
//
// Pseudos already handled by HaydnInstrInfo::expandPostRAPseudo (RET,
// LOADI32, MOV_GPR_TO_DR64, MOV_DR64_TO_GPR) are NOT duplicated here.
// B and BR_JT stay printer-owned: JALR_W is isCall, so a computed goto
// must not become a call before pack.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "HaydnBundleVerify.h"
#include "HaydnExpandPseudos.h"
#include "HaydnInstrInfo.h"
#include "HaydnPostRAScratch.h"
#include "HaydnSubtarget.h"
#include "MCTargetDesc/HaydnMatInt.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
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
    // NDEBUG must not silently truncate the scaled displacement (W43).
    if (Offset % 4 != 0)
      report_fatal_error(Twine("HaydnExpandPseudos: LD32_POST_INC displacement ") +
                             Twine(Offset) + " is not word-aligned",
                         /*GenCrashDiag=*/false);
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
    // NDEBUG must not silently truncate the scaled displacement (W43).
    if (Offset % 4 != 0)
      report_fatal_error(Twine("HaydnExpandPseudos: ST32_POST_INC displacement ") +
                             Twine(Offset) + " is not word-aligned",
                         /*GenCrashDiag=*/false);
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
    // NDEBUG must not silently truncate the scaled displacement (W43).
    if (Offset % 8 != 0)
      report_fatal_error(Twine("HaydnExpandPseudos: LD64_POST_INC displacement ") +
                             Twine(Offset) + " is not dword-aligned",
                         /*GenCrashDiag=*/false);
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
    // NDEBUG must not silently truncate the scaled displacement (W43).
    if (Offset % 8 != 0)
      report_fatal_error(Twine("HaydnExpandPseudos: ST64_POST_INC displacement ") +
                             Twine(Offset) + " is not dword-aligned",
                         /*GenCrashDiag=*/false);
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

bool HaydnExpandPseudos::runOnMachineFunction(MachineFunction &MF) {
  // Never call skipFunction. AIE AIEPseudoBranchExpansion.cpp:43-57
  // walks every function before PostMachineScheduler
  // (AIE2TargetMachine.cpp:237). Leftover expand-owned and bundled
  // cycle-forming residuals are a product emission gate, not quality.
  TII = MF.getSubtarget<HaydnSubtarget>().getInstrInfo();

  bool Modified = false;
  for (MachineBasicBlock &MBB : MF)
    Modified |= expandMBB(MBB);

  // Bundle interior leftover *_POST_INC. Product post-inc form is ISel;
  // MIR-injected residuals use expandPostIncPseudo.
  for (MachineBasicBlock &MBB : MF)
    Modified |= expandBundledPostIncLeftovers(MBB, *TII);

  // After leftover expand so real JAL_W is visible. Restore emitter and
  // post-call / indirect-target insertion live in HaydnPostRAScratch.
  Modified |= insertSoftZeroR0AfterCalls(MF, *TII);

  // Residual matrix: anything this pass is responsible for expanding must be
  // gone. Relocated surfaces (VASTART/VAARG/LIBCALL/PseudoCALL/ADJCALLSTACK)
  // remain expand-owned at the late firewall so leftovers still fail closed.
  //
  // AIE AIEPseudoBranchExpansion.cpp:43-57 walks top-level MIs and
  // setDesc-expands named branch pseudos (AIE2TargetMachine.cpp:237, before
  // PostMachineScheduler). Haydn overlay: leftover cycle-forming residuals
  // (SET_HWLOOP / SETCBR / LoopDec/JNZ/LoopStart / LOAD_ADDR / LOADI32)
  // inside a BUNDLE are fail-closed here. Pack has not run, so a BUNDLE
  // child is leftover injection; in-bundle expand is only leftover
  // *_POST_INC (expandBundledPostIncLeftovers). Bare LoopDec/LoopJNZ/
  // LoopStart stay legal until Fixup / late Verify. Top-level leftover
  // LOADI32/LOADI64 after ExpandPostRA (TargetPassConfig.cpp:1192) is
  // fail-closed here so it cannot reach pack/commit.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      if (MI.isBundle())
        continue;
      const unsigned Opc = MI.getOpcode();
      const bool ExpandOwned =
          haydn::bundle::isExpandOwnedSemanticPseudo(Opc);
      const bool BundledCycle = MI.isInsideBundle() &&
                                haydn::bundle::isResidualCycleFormingPseudo(Opc);
      const bool LeftoverLoadI =
          !MI.isInsideBundle() &&
          (Opc == Haydn::LOADI32 || Opc == Haydn::LOADI64);
      if (!ExpandOwned && !BundledCycle && !LeftoverLoadI)
        continue;
      std::string Msg;
      raw_string_ostream OS(Msg);
      if (BundledCycle) {
        OS << "HaydnExpandPseudos: residual cycle-forming bundled child in "
           << MF.getName() << " BB#" << MBB.getNumber()
           << " (SET_HWLOOP/SETCBR/LOOPCTL/LOAD_ADDR must not survive "
              "Expand as a BUNDLE child):\n  MI: "
           << MI;
      } else if (LeftoverLoadI) {
        OS << "HaydnExpandPseudos: residual LOADI32/LOADI64 in "
           << MF.getName() << " BB#" << MBB.getNumber()
           << " (expandPostRAPseudo must expand before pack):\n  MI: " << MI;
      } else {
        OS << "HaydnExpandPseudos: residual expand-owned semantic pseudo in "
           << MF.getName() << " BB#" << MBB.getNumber()
           << " (must expand before pack):\n  MI: " << MI;
      }
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

  // Leftover bare MIR generic SET (AIE AIEPseudoBranchExpansion.cpp:71
  // setDesc). Product HardwareLoops already emits SET_HWLOOP_F2_W at
  // the creator (HaydnHardwareLoops.cpp:701). Bundled leftover SET is
  // fatal in the residual scan — this arm is last-chance for injected
  // top-level MIR only. NDEBUG must not rewrite a wrong-shape leftover.
  case Haydn::SET_HWLOOP_REG:
    if (!(MI.getNumOperands() >= 4 && MI.getOperand(0).isImm() &&
          MI.getOperand(1).isMBB() && MI.getOperand(2).isMBB() &&
          MI.getOperand(3).isReg())) {
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << "HaydnExpandPseudos: leftover SET_HWLOOP_REG shape is not "
            "sel/start/end/rs:\n  MI: "
         << MI;
      report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
    }
    MI.setDesc(TII->get(Haydn::SET_HWLOOP_F2_W));
    return true;
  case Haydn::SET_HWLOOP:
    if (!(MI.getNumOperands() >= 4 && MI.getOperand(0).isImm() &&
          MI.getOperand(1).isMBB() && MI.getOperand(2).isMBB() &&
          MI.getOperand(3).isImm())) {
      std::string Msg;
      raw_string_ostream OS(Msg);
      OS << "HaydnExpandPseudos: leftover SET_HWLOOP shape is not "
            "sel/start/end/cnt:\n  MI: "
         << MI;
      report_fatal_error(Twine(OS.str()), /*GenCrashDiag=*/false);
    }
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

  // LOAD_ADDR MatInt and LUI seed Cur=R0. Restore only on a proven dirty
  // def (ADDI/JALR into R0). Unknown fallthrough is not a second XOR —
  // ExpandPseudos already restores after JALR r0, and withDR64PackBase
  // keeps the conservative borrow check.
  MachineBasicBlock::iterator InsertPt(&MI);
  ensureSoftZeroR0IfKnownDirty(MBB, InsertPt, DL, *TII);

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
