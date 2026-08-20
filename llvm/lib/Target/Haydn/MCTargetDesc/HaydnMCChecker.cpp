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
#include "MCTargetDesc/HaydnMCFormats.h"
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
    const StringRef Log = MII.getName(Inst->getOpcode());
    if (Log.starts_with("SET_HWLOOP") && NumOps > 0 &&
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
    ArrayRef<const MCInst *> Reals, unsigned RowEntryCount,
    const MCInstrInfo &MII, const MCRegisterInfo *MRI) {
  // HexagonMCChecker.cpp:692-703 checkSolo uses bundleSize only as a bound on
  // an already-formed packet; it does not choose the packet format. Haydn
  // overlay: RowEntryCount is the membership-selected row capacity (2/3),
  // never raw child/text cardinality and never size≤1→E2.
  if (RowEntryCount != 2 && RowEntryCount != 3)
    return std::string(
        "bundle row is not a generated two- or three-entry Format E row");

  // Occupancy bound on the already-selected row (Hexagon checkSolo). Not a
  // TWO vs THREE identity and not a family E2/E3 capacity map.
  if (Reals.size() > RowEntryCount)
    return std::string("occupancy exceeds selected Format E row");

  SmallVector<std::string, 3> Logs;
  Logs.reserve(Reals.size());
  bool AnyE3Only = false;
  bool AnyE2Only = false;
  for (const MCInst *Inst : Reals) {
    if (!Inst)
      continue;
    const unsigned Opc = Inst->getOpcode();
    const StringRef Name = MII.getName(Opc);
    if (haydnIsResidualFieldSlotName(Name) || haydnIsGeneratedMemberName(Name) ||
        haydnFindFormatEMemberByOpcode(Opc))
      return std::string("private placement opcode");
    // Catalog occupancy / MemberId span (Hexagon MCChecker.cpp:692-703 uses
    // the packet's real opcodes). Not a row-identity peel and not `_S*`
    // recovery (those names already returned above). Unknown names fail
    // closed so unit cover cannot treat them as unconstrained occupancy.
    std::string Log = haydnCatalogOccupancyName(Name);
    if (Log.empty())
      return std::string("unknown logical occupancy");
    if (StringRef(Log).equals_insensitive("NOP"))
      continue;
    Logs.push_back(std::move(Log));
    if (haydnFormatELogicalIsE3Only(Opc))
      AnyE3Only = true;
    if (haydnFormatELogicalIsE2Only(Opc))
      AnyE2Only = true;
  }
  if (AnyE2Only && AnyE3Only)
    return std::string("E2-only and E3-only logicals cannot share a row");
  if (AnyE3Only && RowEntryCount != 3)
    return std::string("E3-only logical cannot occupy a two-entry row");
  if (AnyE2Only && RowEntryCount != 2)
    return std::string("E2-only logical cannot occupy a three-entry row");

  const bool CoverE2 = logicalsHaveUnitCoverForMode(Logs, /*Mode=*/0);
  const bool CoverE3 = logicalsHaveUnitCoverForMode(Logs, /*Mode=*/1);
  if (AnyE3Only) {
    if (!CoverE3)
      return std::string(
          "unit injectivity failed (execution units are not encoded entry "
          "identity)");
  } else if (AnyE2Only) {
    if (!CoverE2)
      return std::string(
          "unit injectivity failed (execution units are not encoded entry "
          "identity)");
  } else if (!CoverE2 && !CoverE3) {
    return std::string(
        "unit injectivity failed (execution units are not encoded entry "
        "identity)");
  }

  return haydnCheckParsedBundleRegs(Reals, MII, MRI);
}
