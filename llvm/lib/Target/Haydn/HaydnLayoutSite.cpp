//===- HaydnLayoutSite.cpp - closure-local control site table -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnLayoutSite.h"
#include "HaydnBundleVerify.h"
#include "HaydnInstrInfo.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace llvm::haydn;

namespace {

enum class ClassifyResult { Skip, Site, Fail };

bool isTwoRegCond(unsigned LogicalOpc) {
  switch (LogicalOpc) {
  case Haydn::BEQ_W:
  case Haydn::BNE_W:
  case Haydn::BGE_W:
  case Haydn::BGEU_W:
  case Haydn::BLT_W:
  case Haydn::BLTU_W:
  case Haydn::BEQ:
  case Haydn::BNE:
  case Haydn::BGE:
  case Haydn::BGEU:
  case Haydn::BLT:
  case Haydn::BLTU:
    return true;
  default:
    return false;
  }
}

bool isOneRegCond(unsigned LogicalOpc) {
  switch (LogicalOpc) {
  case Haydn::BEQZ_W:
  case Haydn::BNEZ_W:
  case Haydn::BGEZ_W:
  case Haydn::BLTZ_W:
  case Haydn::BEQZ:
  case Haydn::BNEZ:
  case Haydn::BGEZ:
  case Haydn::BLTZ:
    return true;
  default:
    return false;
  }
}

bool hasMBBOperand(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.operands())
    if (MO.isMBB())
      return true;
  return false;
}

void bindRootMember(LayoutSite &S, MachineInstr &MI) {
  S.Member = &MI;
  if (MachineInstr *Root = haydn::bundle::bundleRootOf(MI))
    S.Root = Root;
  else
    S.Root = &MI;
}

ClassifyResult classify(MachineInstr &MI, const HaydnInstrInfo &TII,
                        LayoutSite &S, std::string &Err) {
  const unsigned Opc = MI.getOpcode();
  if (Opc == TargetOpcode::BUNDLE)
    return ClassifyResult::Skip;
  if (MI.isMetaInstruction() || MI.isDebugInstr() || MI.isImplicitDef() ||
      MI.isKill() || MI.isCFIInstruction() || MI.isPosition() ||
      MI.isInlineAsm())
    return ClassifyResult::Skip;

  const unsigned Log = haydnLogicalOpcode(Opc);

  if (TII.isHardwareLoopSetupInstr(MI)) {
    S.Kind = LayoutSiteKind::HWLoop;
    bindRootMember(S, MI);
    S.FieldKind = HaydnReloc::RelocKind::HWLoopOff1;
    S.FieldKind2 = HaydnReloc::RelocKind::HWLoopOff2;
    S.Rank = LayoutSiteRank::FitPatch;
    S.Template = LayoutTemplateId::None;
    if (MI.getNumOperands() >= 3 && MI.getOperand(1).isMBB() &&
        MI.getOperand(2).isMBB()) {
      S.Dest = MI.getOperand(1).getMBB();
      S.Dest2 = MI.getOperand(2).getMBB();
      return ClassifyResult::Site;
    }
    if (Log == Haydn::LoopStart)
      return ClassifyResult::Site;
    Err = "malformed SET_HWLOOP; refusing erase-only once-through";
    return ClassifyResult::Fail;
  }

  switch (Log) {
  case Haydn::PseudoLoopEnd:
  case Haydn::LoopJNZ:
  case Haydn::LoopDec:
  case Haydn::RET:
  case Haydn::BR_JT:
  case Haydn::NOP:
  case Haydn::COPY:
  case Haydn::LUI:
  case Haydn::ADDI32:
  case Haydn::ADDI32_W:
  case Haydn::MOVE32:
    return ClassifyResult::Skip;
  default:
    break;
  }

  if (Log == Haydn::JAL || Log == Haydn::JAL_W || Log == Haydn::JAL_TCO ||
      Log == Haydn::PseudoCALL) {
    S.Kind = LayoutSiteKind::Call;
    bindRootMember(S, MI);
    S.FieldKind = HaydnReloc::RelocKind::CallSImm20;
    S.FieldKind2 = HaydnReloc::RelocKind::None;
    S.Dest = TII.getBranchDestBlock(MI);
    S.Rank = LayoutSiteRank::FitPatch;
    S.Template = LayoutTemplateId::None;
    return ClassifyResult::Site;
  }

  if (Log == Haydn::JALR || Log == Haydn::JALR_W || Log == Haydn::JALR_CALL ||
      Log == Haydn::JALR_TCO) {
    if (hasMBBOperand(MI)) {
      Err = HaydnReloc::kUnsupportedSymbolicJalrDiag;
      return ClassifyResult::Fail;
    }
    const HaydnJalrAddrMaterializeChain Chain =
        TII.getJalrAddrMaterializeChain(MI);
    if (!Chain.Dest)
      return ClassifyResult::Skip;
    const bool IsCall = Log == Haydn::JALR_CALL || Log == Haydn::JALR_TCO;
    S.Kind = IsCall ? LayoutSiteKind::Call : LayoutSiteKind::Branch;
    bindRootMember(S, MI);
    S.FieldKind = IsCall ? HaydnReloc::RelocKind::CallSImm20
                         : HaydnReloc::RelocKind::WIDE_BranchSImm12;
    S.FieldKind2 = HaydnReloc::RelocKind::None;
    S.Dest = Chain.Dest;
    S.Rank = LayoutSiteRank::LongTemplate;
    S.Template = LayoutTemplateId::InBlockJalr;
    return ClassifyResult::Site;
  }

  if (isTwoRegCond(Log)) {
    S.Kind = LayoutSiteKind::Branch;
    bindRootMember(S, MI);
    S.FieldKind = HaydnReloc::RelocKind::WIDE_BranchSImm12_RI;
    S.FieldKind2 = HaydnReloc::RelocKind::None;
    S.Dest = TII.getBranchDestBlock(MI);
    S.Rank = LayoutSiteRank::FitPatch;
    S.Template = LayoutTemplateId::None;
    return ClassifyResult::Site;
  }

  if (isOneRegCond(Log) || Log == Haydn::B) {
    S.Kind = LayoutSiteKind::Branch;
    bindRootMember(S, MI);
    S.FieldKind = HaydnReloc::RelocKind::WIDE_BranchSImm12;
    S.FieldKind2 = HaydnReloc::RelocKind::None;
    S.Dest = TII.getBranchDestBlock(MI);
    S.Rank = LayoutSiteRank::FitPatch;
    S.Template = LayoutTemplateId::None;
    return ClassifyResult::Site;
  }

  if (MI.isBranch(MachineInstr::IgnoreBundle) ||
      MI.isCall(MachineInstr::IgnoreBundle)) {
    Err = "unknown control";
    return ClassifyResult::Fail;
  }
  return ClassifyResult::Skip;
}

} // namespace

const HaydnReloc::RelocFieldInfo &LayoutSite::fieldInfo() const {
  return HaydnReloc::getRelocFieldInfo(FieldKind);
}

void LayoutSite::raiseRank(LayoutSiteRank NewRank) {
  if (static_cast<uint8_t>(NewRank) < static_cast<uint8_t>(Rank))
    report_fatal_error("HaydnLayoutSite: rank decrease is illegal",
                       /*GenCrashDiag=*/false);
  Rank = NewRank;
}

bool LayoutSite::canFitPatch() const {
  if (Rank != LayoutSiteRank::FitPatch)
    return false;
  if (HaydnReloc::isSymbolicJalrReloc(FieldKind) ||
      FieldKind == HaydnReloc::RelocKind::Invalid)
    return false;
  return fieldInfo().Trans != HaydnReloc::RelocTrans::Unresolved;
}

void LayoutSiteTable::clear() {
  Sites.clear();
  ByMember.clear();
}

bool LayoutSiteTable::collect(MachineFunction &MF, const HaydnInstrInfo &TII,
                              std::string &Err) {
  struct PrevState {
    LayoutSiteRank Rank;
    LayoutTemplateId Template;
    LayoutSiteKind Kind;
  };
  SmallDenseMap<const MachineInstr *, PrevState, 16> Prev;
  for (const LayoutSite &S : Sites)
    if (S.Member)
      Prev[S.Member] = {S.Rank, S.Template, S.Kind};

  clear();
  Err.clear();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB.instrs()) {
      LayoutSite S;
      std::string OneErr;
      switch (classify(MI, TII, S, OneErr)) {
      case ClassifyResult::Skip:
        break;
      case ClassifyResult::Fail:
        Err = OneErr;
        clear();
        return false;
      case ClassifyResult::Site: {
        const LayoutSiteRank FormRank = S.Rank;
        const LayoutTemplateId FormTemplate = S.Template;
        if (auto It = Prev.find(S.Member); It != Prev.end()) {
          // Kind mismatch is a recycled MI (D1.172). Do not restore
          // NeutralizedNop / HwLoopSoftLatch onto a live Branch site.
          if (It->second.Kind == S.Kind) {
            S.Rank = It->second.Rank;
            S.Template = It->second.Template;
            if (FormRank > S.Rank)
              S.raiseRank(FormRank);
            if (FormTemplate != LayoutTemplateId::None)
              S.Template = FormTemplate;
          }
        }
        ByMember[S.Member] = Sites.size();
        Sites.push_back(S);
        break;
      }
      }
    }
  }
  return true;
}

LayoutSite *LayoutSiteTable::findByMember(MachineInstr *Member) {
  auto It = ByMember.find(Member);
  if (It == ByMember.end())
    return nullptr;
  return &Sites[It->second];
}

const LayoutSite *LayoutSiteTable::findByMember(
    const MachineInstr *Member) const {
  auto It = ByMember.find(Member);
  if (It == ByMember.end())
    return nullptr;
  return &Sites[It->second];
}

LayoutSite *LayoutSiteTable::requireMember(MachineInstr *Member,
                                           StringRef Who) {
  LayoutSite *S = findByMember(Member);
  if (!S)
    report_fatal_error(Twine(Who) + ": missing layout site",
                       /*GenCrashDiag=*/false);
  return S;
}

void LayoutSiteTable::dropMember(MachineInstr *Member) {
  if (!Member)
    return;
  auto It = ByMember.find(Member);
  if (It == ByMember.end())
    return;
  Sites[It->second].Member = nullptr;
  ByMember.erase(It);
}
