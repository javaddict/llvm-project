//===- HaydnMCChecker.cpp - Parse-time Format E bundle check --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Peer: HexagonMCChecker.cpp (packet constraint check at assemble time).
// Haydn overlay: generated Format E unit cover + the same WAW / RF-port /
// SET_HWLOOP-sel laws as verifyParsedBundle, without stamping entry identity
// (hand-asm is not a committed inverse).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnMCChecker.h"
#include "HaydnFormatERecords.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <algorithm>

using namespace llvm;
using namespace llvm::haydn::format_e;

static int bankOf(MCRegister Reg, const MCRegisterInfo *MRI) {
  if (!Reg)
    return 0;
  if (!MRI)
    return 1;
  int Best = 0;
  for (unsigned C = 0, N = MRI->getNumRegClasses(); C != N; ++C) {
    const MCRegisterClass &RC = MRI->getRegClass(C);
    if (!RC.contains(Reg))
      continue;
    const StringRef Name = MRI->getRegClassName(&RC);
    if (Name == "DR64")
      return 2;
    if (Name == "AR")
      return 3;
    if (Name == "GPR32")
      Best = 1;
  }
  if (StringRef(MRI->getName(Reg)) == "sfr")
    return 4;
  return Best;
}

std::optional<std::string> llvm::haydnCheckParsedBundleRegs(
    ArrayRef<const MCInst *> Reals, const MCInstrInfo &MII,
    const MCRegisterInfo *MRI) {
  SmallSet<unsigned, 8> Defs;
  SmallSet<int64_t, 2> HwloopSels;
  unsigned GPRR = 0, GPRW = 0, DRR = 0, DRW = 0, ARR = 0, ARW = 0,
           SFRR = 0, SFRW = 0;
  for (const MCInst *Inst : Reals) {
    if (!Inst)
      continue;
    const MCInstrDesc &Desc = MII.get(Inst->getOpcode());
    const unsigned NumOps = Inst->getNumOperands();
    const unsigned NumDefs =
        std::min(static_cast<unsigned>(Desc.getNumDefs()), NumOps);
    SmallSet<unsigned, 6> SeenR, SeenW;
    for (unsigned I = 0; I < NumOps; ++I) {
      const MCOperand &Op = Inst->getOperand(I);
      if (!Op.isReg())
        continue;
      const unsigned Id = Op.getReg().id();
      if (!Id)
        continue;
      const bool IsDef = I < NumDefs;
      if (IsDef) {
        if (Defs.contains(Id))
          return std::string("same-register WAW in one issue cycle");
        Defs.insert(Id);
      }
      const int Bank = bankOf(Op.getReg(), MRI);
      if (IsDef && SeenW.insert(Id).second) {
        if (Bank == 1)
          ++GPRW;
        else if (Bank == 2)
          ++DRW;
        else if (Bank == 3)
          ++ARW;
        else if (Bank == 4)
          ++SFRW;
      }
      if (!IsDef && SeenR.insert(Id).second) {
        if (Bank == 1)
          ++GPRR;
        else if (Bank == 2)
          ++DRR;
        else if (Bank == 3)
          ++ARR;
        else if (Bank == 4)
          ++SFRR;
      }
    }
    const std::string Log =
        peelLogicalOpcodeName(MII.getName(Inst->getOpcode()));
    if (StringRef(Log).starts_with("SET_HWLOOP") && NumOps > 0 &&
        Inst->getOperand(0).isImm()) {
      const int64_t Sel = Inst->getOperand(0).getImm();
      if (!HwloopSels.insert(Sel).second)
        return std::string("SET_HWLOOP same-sel conflict in one cycle");
    }
  }
  // Same ceilings as HaydnPortModel / verifyCommittedBundle (incl. SFR 2R/1W).
  if (GPRR > 4 || GPRW > 2 || DRR > 7 || DRW > 3 || ARR > 2 || ARW > 2 ||
      SFRR > 2 || SFRW > 1)
    return std::string(
        "cycle RF port demand exceeds one issue cycle (GPR 4R/2W, DR 7R/3W, "
        "AR 2R/2W, SFR 2R/1W)");
  return std::nullopt;
}

std::optional<std::string> llvm::haydnCheckParsedBundle(
    ArrayRef<const MCInst *> Reals, unsigned TextEntries, const MCInstrInfo &MII,
    const MCRegisterInfo *MRI) {
  if (Reals.size() > 3)
    return std::string("memberCount > ISSUE_SLOT_COUNT (3)");

  SmallVector<std::string, 3> Logs;
  Logs.reserve(Reals.size());
  for (const MCInst *Inst : Reals) {
    if (!Inst)
      continue;
    Logs.push_back(peelLogicalOpcodeName(MII.getName(Inst->getOpcode())));
  }

  // 3 textual entries stamp E3. Fewer may still place as E3 (standalone DFS).
  const bool ForceE3 = TextEntries >= 3;
  const bool CoverE3 = logicalsHaveUnitCoverForMode(Logs, /*Mode=*/1);
  const bool CoverE2 =
      !ForceE3 && logicalsHaveUnitCoverForMode(Logs, /*Mode=*/0);
  if (!CoverE2 && !CoverE3)
    return std::string(
        "unit injectivity failed (execution units are not encoded entry "
        "identity)");

  return haydnCheckParsedBundleRegs(Reals, MII, MRI);
}
