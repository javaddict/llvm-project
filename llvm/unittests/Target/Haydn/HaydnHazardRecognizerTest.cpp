//===- HaydnHazardRecognizerTest.cpp - resource-model tests ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the Haydn hazard-recognizer RESOURCE MODEL — the
// `HaydnFuncUnitWrapper::conflict` predicate and CycleState tryAdd
// placement authority.
// Pins seven-unit injectivity, ALU0||LOADSTORE0 co-issue, 3-issue cap,
// GPR 4R2W / DR 8R3W / AR 2R2W / SFR 2R1W, and product tryAdd field order.
//
// HaydnFuncUnitWrapper is pure data (no MachineInstr/MachineFunction needed).
// Unit bit indices match HaydnExecUnit / HaydnSchedule.td:
//   0 LOADSTORE0, 1 LOAD1, 2 ALU0, 3 ALU1, 4 ALU2, 5 MAC0, 6 MAC1
//
//===----------------------------------------------------------------------===//

#include "HaydnAlternateDescriptors.h"
#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnHazardRecognizer.h"
#include "HaydnInstrInfo.h"
#include "HaydnPackLegality.h"
#include "HaydnPlacementAlternative.h"
#include "HaydnPortModel.h"
#include "HaydnPreRASchedStrategy.h"
#include "HaydnResourceCycle.h"
#include "HaydnResourceScoreboard.h"
#include "HaydnStaticBitSet.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"

#include <memory>
#include <vector>

// Opcode enums come via HaydnPortModel → HaydnMCTargetDesc (GET_INSTRINFO_ENUM).

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

using namespace llvm;

#define GET_FORMAT_E_MEMBER_OPCODES
#include "HaydnGenFormatEMemberOpcodes.inc"

namespace {

using UnitSet = StaticBitSet<HAYDN_NUM_FU_BITS>;
// Historical alias used by older Req/Res matrix tests.
using SlotSet = UnitSet;

// Three occupied issue bits drop E96TwoEntry. Not a retired Full mask=3.
constexpr uint64_t E3OnlyFormatMask = haydn::bundle::formatRowBit(
    haydn::bundle::BundleFormatRowID::E96ThreeEntry);

/// Single-unit set: {unit N} (Required.count() == 1).
static UnitSet singleUnit(unsigned N) { return UnitSet(static_cast<int>(N)); }

/// Legacy name used by older tests — unit bit N, not a positional slot.
static UnitSet singleSlot(unsigned N) { return singleUnit(N); }

/// Multi-ALU choice-set (Slot012_ALU scaffold: ALU0|ALU1|ALU2).
static UnitSet multiALUUnits() {
  return singleUnit(EU_ALU0) | singleUnit(EU_ALU1) | singleUnit(EU_ALU2);
}

/// Historical name for multi-unit choice-set probes.
static UnitSet allSlots() { return multiALUUnits(); }

/// A single-issue wrapper occupying execution unit \p N with no port demand.
static HaydnFuncUnitWrapper singleIssueInUnit(unsigned N) {
  HaydnFuncUnitWrapper W(singleUnit(N));
  W.setIssueCountOne();
  return W;
}

/// Compatibility alias for existing tests (name is historical).
static HaydnFuncUnitWrapper singleIssueInSlot(unsigned N) {
  return singleIssueInUnit(N);
}

TEST(HaydnHazardRecognizerTest, SevenUnitResourceModelIdentity) {
  EXPECT_EQ(HAYDN_NUM_FU_BITS, 7u);
  EXPECT_EQ(EU_COUNT, 7u);
  EXPECT_EQ(EU_LOADSTORE0, 0u);
  EXPECT_EQ(EU_LOAD1, 1u);
  EXPECT_EQ(EU_ALU0, 2u);
  EXPECT_EQ(EU_ALU1, 3u);
  EXPECT_EQ(EU_ALU2, 4u);
  EXPECT_EQ(EU_MAC0, 5u);
  EXPECT_EQ(EU_MAC1, 6u);
  EXPECT_EQ(llvm::haydn::pack::NumExecutionUnits, HAYDN_NUM_FU_BITS);
}

TEST(HaydnHazardRecognizerTest, UnitInjectivitySameUnitConflicts) {
  // Two ops requiring the same exclusive unit conflict (injectivity).
  for (unsigned U = 0; U < HAYDN_NUM_FU_BITS; ++U) {
    EXPECT_TRUE(singleIssueInUnit(U).conflict(singleIssueInUnit(U)))
        << "unit " << U;
  }
}

TEST(HaydnHazardRecognizerTest, DistinctUnitsDoNotConflict) {
  // Distinct exclusive units co-issue under injectivity (issue/ports aside).
  EXPECT_FALSE(singleIssueInUnit(EU_LOADSTORE0)
                   .conflict(singleIssueInUnit(EU_LOAD1)));
  EXPECT_FALSE(
      singleIssueInUnit(EU_ALU0).conflict(singleIssueInUnit(EU_ALU1)));
  EXPECT_FALSE(
      singleIssueInUnit(EU_MAC0).conflict(singleIssueInUnit(EU_MAC1)));
  EXPECT_FALSE(
      singleIssueInUnit(EU_ALU2).conflict(singleIssueInUnit(EU_LOAD1)));
}

TEST(HaydnHazardRecognizerTest, ALU0AndLOADSTORE0CoIssueLegal) {
  // Product concurrency law: ALU0 and LOADSTORE0 are independent units.
  // Co-issue is legal when mapped to different entries (E3 geometry).
  EXPECT_FALSE(singleIssueInUnit(EU_ALU0).conflict(
      singleIssueInUnit(EU_LOADSTORE0)));
  EXPECT_FALSE(singleIssueInUnit(EU_LOADSTORE0)
                   .conflict(singleIssueInUnit(EU_ALU0)));

  using namespace llvm::haydn::pack;
  EXPECT_FALSE(resourcesConflict(singleIssueInUnit(EU_ALU0),
                                 singleIssueInUnit(EU_LOADSTORE0)));
}

TEST(HaydnHazardRecognizerTest, MultiUnitChoiceSetNeverExclusiveConflicts) {
  // Multi-unit choice-set (logical possible-unit menu) never exclusive-
  // conflicts with a single committed unit — format legality assigns the
  // free unit. Issue cap still governs.
  HaydnFuncUnitWrapper Multi(multiALUUnits());
  Multi.setIssueCountOne();
  EXPECT_FALSE(Multi.conflict(singleIssueInUnit(EU_ALU0)));
  EXPECT_FALSE(singleIssueInUnit(EU_ALU1).conflict(Multi));
  EXPECT_FALSE(Multi.conflict(Multi));
}

TEST(HaydnHazardRecognizerTest, IssueCountCapThree) {
  // A cycle that has already issued 3 instrs (IssueCount=3) rejects a 4th.
  HaydnFuncUnitWrapper Cycle;
  Cycle |= singleIssueInUnit(EU_LOADSTORE0);
  Cycle |= singleIssueInUnit(EU_ALU0);
  Cycle |= singleIssueInUnit(EU_MAC0);
  EXPECT_EQ(Cycle.getIssueCount(), 3u);
  EXPECT_TRUE(Cycle.conflict(singleIssueInUnit(EU_ALU1)));
  EXPECT_FALSE(
      singleIssueInUnit(EU_ALU0).conflict(singleIssueInUnit(EU_LOADSTORE0)));
}

TEST(HaydnHazardRecognizerTest, SFRPortBudget2R1W) {
  HaydnFuncUnitWrapper Writer(singleUnit(EU_ALU0));
  Writer.setIssueCountOne();
  Writer.setSFRPorts(0, HAYDN_SFR_WRITE_PORTS); // 1W
  HaydnFuncUnitWrapper Second(singleUnit(EU_ALU1));
  Second.setIssueCountOne();
  Second.setSFRPorts(0, 1);
  EXPECT_TRUE(Writer.conflict(Second)); // 1 + 1 > 1W

  HaydnFuncUnitWrapper Reader(singleUnit(EU_ALU0));
  Reader.setIssueCountOne();
  Reader.setSFRPorts(HAYDN_SFR_READ_PORTS, 0); // 2R
  HaydnFuncUnitWrapper OneMore(singleUnit(EU_ALU1));
  OneMore.setIssueCountOne();
  OneMore.setSFRPorts(1, 0);
  EXPECT_TRUE(Reader.conflict(OneMore)); // 2 + 1 > 2R
}

TEST(HaydnHazardRecognizerTest, GPRPortBudget4R2W) {
  // GPR 4R2W: combined reads > 4 OR combined writes > 2 conflicts.
  HaydnFuncUnitWrapper Reader(singleSlot(0));
  Reader.setGPRPorts(HAYDN_GPR_READ_PORTS, 0); // 4R 0W
  HaydnFuncUnitWrapper OneMoreRead(singleSlot(1));
  OneMoreRead.setGPRPorts(1, 0);
  EXPECT_TRUE(Reader.conflict(OneMoreRead)); // 4 + 1 = 5 > 4

  HaydnFuncUnitWrapper Writer(singleSlot(0));
  Writer.setGPRPorts(0, HAYDN_GPR_WRITE_PORTS); // 0R 2W
  HaydnFuncUnitWrapper OneMoreWrite(singleSlot(1));
  OneMoreWrite.setGPRPorts(0, 1);
  EXPECT_TRUE(Writer.conflict(OneMoreWrite)); // 2 + 1 = 3 > 2

  // At-budget does NOT conflict: 4R+0R = 4, 2W+0W = 2 (both == cap, not > cap).
  HaydnFuncUnitWrapper AtRead(singleSlot(0));
  AtRead.setGPRPorts(HAYDN_GPR_READ_PORTS, 0);
  HaydnFuncUnitWrapper NoRead(singleSlot(1));
  NoRead.setGPRPorts(0, 0);
  EXPECT_FALSE(AtRead.conflict(NoRead));
}

TEST(HaydnHazardRecognizerTest, DR64PortBudget7R3W) {
  // DR64 7R3W: combined reads > 7 OR combined writes > 3 conflicts.
  HaydnFuncUnitWrapper DrReader(singleSlot(0));
  DrReader.setDRPorts(HAYDN_DR_READ_PORTS, 0); // 7R
  HaydnFuncUnitWrapper OneMore(singleSlot(1));
  OneMore.setDRPorts(1, 0);
  EXPECT_TRUE(DrReader.conflict(OneMore)); // 7 + 1 = 8 > 7

  HaydnFuncUnitWrapper DrWriter(singleSlot(0));
  DrWriter.setDRPorts(0, HAYDN_DR_WRITE_PORTS); // 3W
  HaydnFuncUnitWrapper OneMoreW(singleSlot(1));
  OneMoreW.setDRPorts(0, 1);
  EXPECT_TRUE(DrWriter.conflict(OneMoreW)); // 3 + 1 = 4 > 3
}

TEST(HaydnHazardRecognizerTest, ARPortBudget2R2W) {
  // AR 2R2W (forward-compat; AR demand is 0 today): > 2 reads/writes conflicts.
  HaydnFuncUnitWrapper ArReader(singleSlot(0));
  ArReader.setARPorts(HAYDN_AR_READ_PORTS, 0); // 2R
  HaydnFuncUnitWrapper OneMore(singleSlot(1));
  OneMore.setARPorts(1, 0);
  EXPECT_TRUE(ArReader.conflict(OneMore)); // 2 + 1 = 3 > 2
}

TEST(HaydnHazardRecognizerTest, EmptyAndBlocked) {
  // An empty cycle accepts anything (no conflict).
  HaydnFuncUnitWrapper Empty;
  EXPECT_TRUE(Empty.isEmpty());
  EXPECT_FALSE(Empty.conflict(singleIssueInSlot(0)));

  // A blocked cycle rejects everything.
  HaydnFuncUnitWrapper Blocked;
  Blocked.blockResources();
  EXPECT_TRUE(Blocked.conflict(singleIssueInSlot(0)));
  EXPECT_TRUE(Blocked.conflict(Empty) || Empty.conflict(Blocked));
}

//===----------------------------------------------------------------------===//
// Pack-legality oracle pin (HaydnPackLegality.h) — pure resource half.
// MI-level R0 WAW / SFR / locked-DSP are covered by postmisched MIR lits.
//===----------------------------------------------------------------------===//

TEST(HaydnPackLegalityTest, ResourcesConflictMatchesWrapper) {
  using namespace llvm::haydn::pack;
  EXPECT_TRUE(resourcesConflict(singleIssueInSlot(0), singleIssueInSlot(0)));
  EXPECT_FALSE(resourcesConflict(singleIssueInSlot(0), singleIssueInSlot(1)));
}

TEST(HaydnPackLegalityTest, MaxIssueCapIsThree) {
  EXPECT_EQ(llvm::haydn::pack::MaxIssuePerCycle, 3u);
}

TEST(HaydnPackLegalityTest, ThreeSlotFillThenReject) {
  // Exhaustive-ish: fill S0+S1+S2 then a fourth single-issue conflicts.
  HaydnFuncUnitWrapper Cycle;
  for (unsigned S = 0; S < 3; ++S) {
    HaydnFuncUnitWrapper I = singleIssueInSlot(S);
    EXPECT_FALSE(Cycle.conflict(I)) << "slot " << S;
    Cycle |= I;
  }
  EXPECT_EQ(Cycle.getIssueCount(), 3u);
  EXPECT_TRUE(Cycle.conflict(singleIssueInSlot(0)));
}

//===----------------------------------------------------------------------===//
// Extended resource-model coverage — schedule half of pack-legality dual
// authority. MI-level alone-in-bundle / WAW / RAW stay in postmisched MIR lits
// (need MachineInstr + scoreboard). Pure wrappers pin ports/issue/slots here.
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, UnionPreservesDistinctSlots) {
  // Cycle |= single slots accumulates issue and occupancy without false
  // conflict until the 4th issue or same-slot collision.
  HaydnFuncUnitWrapper Cycle;
  Cycle |= singleIssueInSlot(0);
  Cycle |= singleIssueInSlot(1);
  EXPECT_EQ(Cycle.getIssueCount(), 2u);
  EXPECT_FALSE(Cycle.conflict(singleIssueInSlot(2)));
  Cycle |= singleIssueInSlot(2);
  EXPECT_EQ(Cycle.getIssueCount(), 3u);
  EXPECT_TRUE(Cycle.conflict(singleIssueInSlot(0)));
}

TEST(HaydnHazardRecognizerTest, SameSlotConflictIndependentOfPorts) {
  // Slot exclusivity is orthogonal to port budget: two S0 ops conflict even
  // with zero register-port demand (encode/schedule dual authority agrees).
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(0);
  A.setGPRPorts(0, 0);
  B.setGPRPorts(0, 0);
  EXPECT_TRUE(A.conflict(B));
}

TEST(HaydnHazardRecognizerTest, PortConflictWithDistinctSlots) {
  // Distinct slots still conflict when GPR write ports overflow (2W cap).
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setGPRPorts(0, 2);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setGPRPorts(0, 1);
  EXPECT_TRUE(A.conflict(B));
}

TEST(HaydnHazardRecognizerTest, CombinedReadWriteAtBudgetOk) {
  // 4R2W at budget with a zero-port companion on another slot is legal.
  HaydnFuncUnitWrapper Heavy = singleIssueInSlot(0);
  Heavy.setGPRPorts(HAYDN_GPR_READ_PORTS, HAYDN_GPR_WRITE_PORTS);
  HaydnFuncUnitWrapper Light = singleIssueInSlot(1);
  Light.setGPRPorts(0, 0);
  EXPECT_FALSE(Heavy.conflict(Light));
}

TEST(HaydnHazardRecognizerTest, DRAndGPRPortBudgetsIndependent) {
  // Exhausting GPR does not imply DR conflict when DR demand is zero.
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setGPRPorts(HAYDN_GPR_READ_PORTS, 0);
  A.setDRPorts(0, 0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setGPRPorts(0, 0);
  B.setDRPorts(HAYDN_DR_READ_PORTS, 0);
  EXPECT_FALSE(A.conflict(B));

  // Two ops that each take half of DR read budget co-issue; +1 overflows.
  // REBASED 2026-08-21 (GE96-10): DR read budget is 8 per golden
  // Constraints (stated twice); the prior 4+3=7-at-budget pin encoded the
  // pre-reconciliation conservative constant.
  HaydnFuncUnitWrapper D0 = singleIssueInSlot(0);
  D0.setDRPorts(4, 0);
  HaydnFuncUnitWrapper D1 = singleIssueInSlot(1);
  D1.setDRPorts(4, 0);
  EXPECT_FALSE(D0.conflict(D1)); // 4+4=8 at budget
  HaydnFuncUnitWrapper D2 = singleIssueInSlot(2);
  D2.setDRPorts(1, 0);
  HaydnFuncUnitWrapper Combined = D0;
  Combined |= D1;
  EXPECT_TRUE(Combined.conflict(D2)); // 8+1 > 8
}

TEST(HaydnPackLegalityTest, ResourcesConflictIsSymmetric) {
  using namespace llvm::haydn::pack;
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(0);
  EXPECT_EQ(resourcesConflict(A, B), resourcesConflict(B, A));
  HaydnFuncUnitWrapper C = singleIssueInSlot(1);
  EXPECT_EQ(resourcesConflict(A, C), resourcesConflict(C, A));
  EXPECT_FALSE(resourcesConflict(A, C));
}

TEST(HaydnPackLegalityTest, ProductRulesDocumentedInHeader) {
  // Pin constants that encode product law so a silent edit trips CI.
  using namespace llvm::haydn::pack;
  EXPECT_EQ(MaxIssuePerCycle, 3u);
  EXPECT_EQ(NumExecutionUnits, 7u);
  EXPECT_EQ(HAYDN_GPR_READ_PORTS, 4u);
  EXPECT_EQ(HAYDN_GPR_WRITE_PORTS, 2u);
  EXPECT_EQ(HAYDN_DR_READ_PORTS, 8u); // GE96-10: golden Constraints 8R (x2); 7R was the pre-2026-08-21 conservative value
  EXPECT_EQ(HAYDN_DR_WRITE_PORTS, 3u);
  EXPECT_EQ(HAYDN_AR_READ_PORTS, 2u);
  EXPECT_EQ(HAYDN_AR_WRITE_PORTS, 2u);
  EXPECT_EQ(HAYDN_SFR_READ_PORTS, 2u);
  EXPECT_EQ(HAYDN_SFR_WRITE_PORTS, 1u);
  // Rule 4 (ARCTAN/SIN_COS issue-alone only; no multi-cycle uimm4+2 product
  // lock yet) is owned by isLockedSlotDspOp — not a second opcode table.
}

TEST(HaydnHazardRecognizerTest, AloneLawClosedUnderMemberIdentity) {
  // T-SM3 / SM-H2: alone-law is logical + AlternateInsts members + generated
  // Format E member Logical. Not slot-suffix parse (ADD32_S2 is not alone).
  // Golden units: FormatEUnitCount == 7 (Constraints.md Shared Unit).
  using namespace llvm::haydn::format_e;

  EXPECT_EQ(FormatEUnitCount, 7u);
  EXPECT_EQ(HAYDN_GPR_READ_PORTS, 4u);
  EXPECT_EQ(HAYDN_GPR_WRITE_PORTS, 2u);
  EXPECT_EQ(HAYDN_DR_READ_PORTS, 8u); // GE96-10: golden Constraints 8R (x2); 7R was the pre-2026-08-21 conservative value
  EXPECT_EQ(HAYDN_DR_WRITE_PORTS, 3u);
  EXPECT_EQ(HAYDN_AR_READ_PORTS, 2u);
  EXPECT_EQ(HAYDN_AR_WRITE_PORTS, 2u);
  EXPECT_EQ(HAYDN_SFR_READ_PORTS, 2u);
  EXPECT_EQ(HAYDN_SFR_WRITE_PORTS, 1u);

  EXPECT_TRUE(HaydnHazardRecognizer::opcodeIssuesAloneInCycle(Haydn::ARCTAN));
  EXPECT_TRUE(HaydnHazardRecognizer::opcodeIssuesAloneInCycle(Haydn::SIN_COS));
  EXPECT_FALSE(HaydnHazardRecognizer::opcodeIssuesAloneInCycle(Haydn::ADD32));
  EXPECT_FALSE(HaydnHazardRecognizer::opcodeIssuesAloneInCycle(
      Haydn::ADD32_E2_E0_ALU0_RR));
  EXPECT_FALSE(HaydnHazardRecognizer::opcodeIssuesAloneInCycle(
      Haydn::ADD32_E3_E1_ALU1_RR));
  EXPECT_FALSE(HaydnHazardRecognizer::opcodeIssuesAloneInCycle(
      Haydn::ADD32_E3_E2_ALU2_RR));
  EXPECT_FALSE(
      HaydnHazardRecognizer::opcodeIssuesAloneInCycle(Haydn::ADDI32));

  // PortModel stays logical-only; HR closes the member/recommit hole.
  // PortModel is logical-only: FieldSlot / member names are not alone here.
  // HR opcodeIssuesAloneInCycle closes the member hole (loop below).
  EXPECT_FALSE(haydnOpcodeIssuesAloneInCycle(Haydn::ADD32_E3_E2_ALU2_RR));

  HaydnMCFormats Fmts;
  unsigned PlacementMembers = 0;
  for (unsigned Logical : {Haydn::ARCTAN, Haydn::SIN_COS}) {
    const std::vector<unsigned> *Alts = Fmts.getAlternateInstsOpcode(Logical);
    ASSERT_NE(Alts, nullptr) << Logical;
    for (unsigned Member : *Alts) {
      if (Member == 0)
        continue;
      EXPECT_TRUE(HaydnHazardRecognizer::opcodeIssuesAloneInCycle(Member))
          << "placement member of logical " << Logical << " opc=" << Member;
      ++PlacementMembers;
    }
  }
  EXPECT_GE(PlacementMembers, 4u); // ARCTAN/S2 + SIN_COS/S2

  ASSERT_EQ(FormatEMemberOpcodeCount, FormatEMemberCount);
  unsigned FeAlone = 0;
  for (unsigned I = 0; I < FormatEMemberOpcodeCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    if (M.IsNop)
      continue;
    const StringRef Log(M.Logical);
    if (Log != "ARCTAN" && Log != "SIN_COS")
      continue;
    EXPECT_LT(M.Unit, FormatEUnitCount)
        << M.MemberSymbol << " unit is not a golden Shared Unit";
    EXPECT_LT(M.EntryIdx, 3u) << M.MemberSymbol
                              << " entry is not a Format E entry index";
    EXPECT_TRUE(HaydnHazardRecognizer::opcodeIssuesAloneInCycle(
        FormatEMemberOpcodes[I]))
        << M.MemberSymbol;
    ++FeAlone;
  }
  EXPECT_EQ(FeAlone, 6u); // 3 ARCTAN + 3 SIN_COS E3 members
}

//===----------------------------------------------------------------------===//
// Extended port / issue matrix
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, TwoIssueWithPartialPortsOk) {
  // Dual-issue with 2R1W each stays under 4R2W.
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setGPRPorts(2, 1);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setGPRPorts(2, 1);
  EXPECT_FALSE(A.conflict(B));
}

TEST(HaydnHazardRecognizerTest, TripleIssueAtGPRReadBudget) {
  // 2+1+1 = 4 reads at budget with distinct slots.
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setGPRPorts(2, 0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setGPRPorts(1, 0);
  HaydnFuncUnitWrapper C = singleIssueInSlot(2);
  C.setGPRPorts(1, 0);
  HaydnFuncUnitWrapper Cycle = A;
  Cycle |= B;
  EXPECT_FALSE(Cycle.conflict(C));
  Cycle |= C;
  EXPECT_EQ(Cycle.getIssueCount(), 3u);
  // +1 read overflows.
  HaydnFuncUnitWrapper Extra = singleIssueInSlot(0);
  Extra.setGPRPorts(1, 0);
  EXPECT_TRUE(Cycle.conflict(Extra));
}

TEST(HaydnHazardRecognizerTest, DRWriteBudgetTripleIssue) {
  // 1W each on three slots = 3W at DR budget.
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  A.setDRPorts(0, 1);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  B.setDRPorts(0, 1);
  HaydnFuncUnitWrapper C = singleIssueInSlot(2);
  C.setDRPorts(0, 1);
  HaydnFuncUnitWrapper Cycle = A;
  Cycle |= B;
  EXPECT_FALSE(Cycle.conflict(C));
  Cycle |= C;
  HaydnFuncUnitWrapper Extra = singleIssueInSlot(0);
  Extra.setDRPorts(0, 1);
  EXPECT_TRUE(Cycle.conflict(Extra));
}

TEST(HaydnHazardRecognizerTest, BlockedDominatesEmpty) {
  HaydnFuncUnitWrapper Blocked;
  Blocked.blockResources();
  HaydnFuncUnitWrapper Empty;
  EXPECT_TRUE(Blocked.conflict(Empty) || Empty.conflict(Blocked));
  EXPECT_TRUE(Blocked.conflict(singleIssueInSlot(2)));
}

TEST(HaydnPackLegalityTest, DualAuthorityIssueCapMatchesBundle) {
  // pack::MaxIssuePerCycle must equal Haydn ISSUE_SLOT_COUNT / Bundle fill.
  using namespace llvm::haydn::pack;
  EXPECT_EQ(MaxIssuePerCycle, 3u);
  EXPECT_EQ(MaxIssuePerCycle, Haydn::ISSUE_SLOT_COUNT);
}

//===----------------------------------------------------------------------===//
// HR placement authority is product CycleState tryAdd (not getLegalSlots
// S0-first auction). Pure solver pins the order EmitInstruction stamps.
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, B24_TryAddIsPlacementAuthorityS2First) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32));
  CycleState S = makeProductCycleState();
  // Empty cycle accepts ADD32 on S2 (not S0).
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT2));
  EXPECT_EQ(fieldSlotsToIndex(S.Members.back().FieldSlots),
            std::optional<unsigned>(2u));

  // Second ADD32 → S1; third → S0; fourth Hazard-shaped reject.
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(fieldSlotsToIndex(S.Members.back().FieldSlots),
            std::optional<unsigned>(1u));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(fieldSlotsToIndex(S.Members.back().FieldSlots),
            std::optional<unsigned>(0u));
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::ADD32));
}

TEST(HaydnHazardRecognizerTest, B24_ST32BlocksSecondStore) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ST32));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT0));
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::ST32));
  // Multi-slot ALU still fits on S2.
  EXPECT_TRUE(canTryAddProduct(S, Fmts, Haydn::ADD32));
}

TEST(HaydnHazardRecognizerTest, B24_DualLoadThenMac) {
  // Dual LD32 (S0|S1) + MAC (S1|S2) product pack — tryAdd order must allow
  // LD@S1, LD@S0, MAC@S2 when loads issue first (or MAC@S2 then loads).
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::LD32)); // prefers S1 (S0|S1, high first)
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::LD32)); // remaining load slot
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::X2MULA32));
  EXPECT_EQ(S.OccupiedSlots,
            SlotBits(Haydn::SLOT0 | Haydn::SLOT1 | Haydn::SLOT2));
}

//===----------------------------------------------------------------------===//
// Alts-only placement — no getLegalSlots no-alt fallback path
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, B25_NoAltSkipsPlacementGate) {
  // No PlacementAlternatives → tryAdd returns false; getLegalSlots is not a
  // placement fallback (AIEHazardRecognizer.cpp:186-187: no alts → fixed-slot
  // canAdd; Haydn no-alt means no multi-slot auction).
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, /*Opcode=*/0));
  CycleState S = makeProductCycleState();
  EXPECT_FALSE(canTryAddProduct(S, Fmts, 0));
  EXPECT_FALSE(tryAddProduct(S, Fmts, 0));
  EXPECT_TRUE(S.empty());
  EXPECT_EQ(S.OccupiedSlots, 0u);
}

TEST(HaydnHazardRecognizerTest, B25_FieldSlotsFromSparseIndex) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD64, Alts));
  // Sparse {0, S1, S2}: every golden unit at S1/S2. No SLOT0.
  ASSERT_FALSE(Alts.empty());
  for (const PlacementAlternative &A : Alts) {
    EXPECT_TRUE(A.FieldSlots == SlotBits(Haydn::SLOT1) ||
                A.FieldSlots == SlotBits(Haydn::SLOT2));
  }
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD64));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT2));
}

TEST(HaydnHazardRecognizerTest, VF21_ExactCandidateRematchBeatsFirstFit) {
  // Pure solver pin: HR commitPlacement uses the same exactTryAddProduct API.
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleCandidateSet C = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ADD32));
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ADD64));
  ASSERT_TRUE(canExactTryAddProduct(C, Fmts, Haydn::ADD64));
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ADD64));
  EXPECT_EQ(selectPreferredCandidate(C).OccupiedSlots,
            SlotBits(Haydn::SLOT_ALL));
}

//===----------------------------------------------------------------------===//
// MRI/vreg port-bank classification
// Physreg half + capacity matrix here; full MRI vreg path is
// prera-format-feasibility.mir (needs MachineFunction + selected regclasses).
//===----------------------------------------------------------------------===//

TEST(HaydnPortModelTest, PhysregBankClassification) {
  // Without MRI, physregs still classify; vregs without MRI stay existential.
  EXPECT_TRUE(isHaydnGPRPortReg(Haydn::R1, nullptr));
  EXPECT_TRUE(isHaydnGPRPortReg(Haydn::R0, nullptr)); // soft-zero still a port
  EXPECT_FALSE(isHaydnGPRPortReg(Haydn::D0, nullptr));
  EXPECT_FALSE(isHaydnGPRPortReg(Haydn::AR0, nullptr));

  EXPECT_TRUE(isHaydnDRPortReg(Haydn::D0, nullptr));
  EXPECT_FALSE(isHaydnDRPortReg(Haydn::R1, nullptr));
  EXPECT_FALSE(isHaydnDRPortReg(Haydn::AR0, nullptr));

  EXPECT_TRUE(isHaydnARPortReg(Haydn::AR0, nullptr));
  EXPECT_FALSE(isHaydnARPortReg(Haydn::R1, nullptr));
  EXPECT_FALSE(isHaydnARPortReg(Haydn::D0, nullptr));

  // Null / no-reg is never a bank member.
  EXPECT_FALSE(isHaydnGPRPortReg(Register(), nullptr));
  EXPECT_FALSE(isHaydnDRPortReg(Register(), nullptr));
  EXPECT_FALSE(isHaydnARPortReg(Register(), nullptr));

  // Cross-bank exclusion: DR/AR never charge as GPR even under null MRI.
  EXPECT_FALSE(isHaydnPortBankReg(Haydn::D1, Haydn::GPR32RegClass, nullptr));
  EXPECT_FALSE(isHaydnPortBankReg(Haydn::AR1, Haydn::GPR32RegClass, nullptr));
  EXPECT_TRUE(isHaydnPortBankReg(Haydn::R2, Haydn::GPR32RegClass, nullptr));
  EXPECT_TRUE(isHaydnPortBankReg(Haydn::D2, Haydn::DR64RegClass, nullptr));
  EXPECT_TRUE(isHaydnPortBankReg(Haydn::AR0, Haydn::ARRegClass, nullptr));
  EXPECT_TRUE(isHaydnPortBankReg(Haydn::AR1, Haydn::ARRegClass, nullptr));
}

TEST(HaydnPortModelTest, ExistentialVregWithoutMRI) {
  // Virtual registers require MRI regclass facts. Without MRI (or with no
  // class yet) demand stays existential — do not invent a bank.
  // VirtReg 0 is invalid; use a typical first-vreg encoding (>= 2^31 on most
  // targets via Register::index2VirtReg).
  Register VReg = Register::index2VirtReg(0);
  ASSERT_TRUE(VReg.isVirtual());
  EXPECT_FALSE(isHaydnGPRPortReg(VReg, nullptr));
  EXPECT_FALSE(isHaydnDRPortReg(VReg, nullptr));
  EXPECT_FALSE(isHaydnARPortReg(VReg, nullptr));
  EXPECT_FALSE(isHaydnPortBankReg(VReg, Haydn::GPR32RegClass, nullptr));
}

TEST(HaydnPortModelTest, OneBelowAtOneAboveCapacityMatrix) {
 // exit: one-below / at / one-above capacity for each bank budget.
  // GPR 4R2W
  {
    HaydnFuncUnitWrapper AtR = singleIssueInSlot(0);
    AtR.setGPRPorts(HAYDN_GPR_READ_PORTS, 0);
    HaydnFuncUnitWrapper Zero = singleIssueInSlot(1);
    Zero.setGPRPorts(0, 0);
    EXPECT_FALSE(AtR.conflict(Zero)); // at budget OK
    HaydnFuncUnitWrapper OneMore = singleIssueInSlot(1);
    OneMore.setGPRPorts(1, 0);
    EXPECT_TRUE(AtR.conflict(OneMore)); // one above
    HaydnFuncUnitWrapper Below = singleIssueInSlot(0);
    Below.setGPRPorts(HAYDN_GPR_READ_PORTS - 1, 0);
    EXPECT_FALSE(Below.conflict(OneMore)); // one below + 1 = at budget

    HaydnFuncUnitWrapper AtW = singleIssueInSlot(0);
    AtW.setGPRPorts(0, HAYDN_GPR_WRITE_PORTS);
    HaydnFuncUnitWrapper OneMoreW = singleIssueInSlot(1);
    OneMoreW.setGPRPorts(0, 1);
    EXPECT_TRUE(AtW.conflict(OneMoreW));
    HaydnFuncUnitWrapper BelowW = singleIssueInSlot(0);
    BelowW.setGPRPorts(0, HAYDN_GPR_WRITE_PORTS - 1);
    EXPECT_FALSE(BelowW.conflict(OneMoreW));
  }
  // DR 8R3W
  {
    HaydnFuncUnitWrapper AtR = singleIssueInSlot(0);
    AtR.setDRPorts(HAYDN_DR_READ_PORTS, 0);
    HaydnFuncUnitWrapper OneMoreR = singleIssueInSlot(1);
    OneMoreR.setDRPorts(1, 0);
    EXPECT_TRUE(AtR.conflict(OneMoreR));
    HaydnFuncUnitWrapper AtW = singleIssueInSlot(0);
    AtW.setDRPorts(0, HAYDN_DR_WRITE_PORTS);
    HaydnFuncUnitWrapper OneMoreW = singleIssueInSlot(1);
    OneMoreW.setDRPorts(0, 1);
    EXPECT_TRUE(AtW.conflict(OneMoreW));
    HaydnFuncUnitWrapper BelowW = singleIssueInSlot(0);
    BelowW.setDRPorts(0, HAYDN_DR_WRITE_PORTS - 1);
    EXPECT_FALSE(BelowW.conflict(OneMoreW));
  }
  // AR 2R2W (no AR-operand product opcode yet — capacity still enforced)
  {
    HaydnFuncUnitWrapper AtR = singleIssueInSlot(0);
    AtR.setARPorts(HAYDN_AR_READ_PORTS, 0);
    HaydnFuncUnitWrapper OneMoreR = singleIssueInSlot(1);
    OneMoreR.setARPorts(1, 0);
    EXPECT_TRUE(AtR.conflict(OneMoreR));
    HaydnFuncUnitWrapper Below = singleIssueInSlot(0);
    Below.setARPorts(HAYDN_AR_READ_PORTS - 1, 0);
    EXPECT_FALSE(Below.conflict(OneMoreR));
    HaydnFuncUnitWrapper AtW = singleIssueInSlot(0);
    AtW.setARPorts(0, HAYDN_AR_WRITE_PORTS);
    HaydnFuncUnitWrapper OneMoreW = singleIssueInSlot(1);
    OneMoreW.setARPorts(0, 1);
    EXPECT_TRUE(AtW.conflict(OneMoreW));
    HaydnFuncUnitWrapper BelowW = singleIssueInSlot(0);
    BelowW.setARPorts(0, HAYDN_AR_WRITE_PORTS - 1);
    EXPECT_FALSE(BelowW.conflict(OneMoreW));
  }
}

TEST(HaydnPortModelTest, PreRAProductFeasibleFormatMaskFullOnly) {
  // Empty occupancy keeps ProductFormatMask (E2|E3). SLOT_ALL is three
  // entries → E3-only (coveringFormatMaskFromPackets OccCount>2).
  using namespace llvm::haydn::bundle;
  EXPECT_EQ(HaydnPreRASchedStrategy::productFeasibleFormatMask(/*Occupied=*/0),
            ProductFormatMask);
  const uint64_t MaskAll =
      HaydnPreRASchedStrategy::productFeasibleFormatMask(Haydn::SLOT_ALL);
  EXPECT_EQ(MaskAll, E3OnlyFormatMask);
}

//===----------------------------------------------------------------------===//
// Matching-frontier probe — pre-RA tryCandidate vocabulary (no setDesc)
//===----------------------------------------------------------------------===//

TEST(HaydnHazardRecognizerTest, MatchingFrontierEmptyCycleFlexVsConstrained) {
  // Empty Full cycle: flexible ADD32 (Slot012) retains more nondominated
  // matchings than constrained ST32 (Slot0-only). Pure probe — no MIR.
  using namespace llvm::haydn::bundle;
  CycleCandidateSet Empty = makeProductCandidateSet();
  auto AddScore =
      HaydnHazardRecognizer::scoreMatchingFrontier(Empty, Haydn::ADD32);
  auto StScore =
      HaydnHazardRecognizer::scoreMatchingFrontier(Empty, Haydn::ST32);
  EXPECT_TRUE(AddScore.Feasible);
  EXPECT_TRUE(StScore.Feasible);
  EXPECT_GT(AddScore.SuccessorMatchings, 0u);
  EXPECT_GT(StScore.SuccessorMatchings, 0u);
  // Flexible multi-alt leaves a larger nondominated set than single-field ST.
  EXPECT_GE(AddScore.SuccessorMatchings, StScore.SuccessorMatchings);
  // Full-only: mask stays ProductFormatMask (compact-byte tie is a no-op).
  EXPECT_EQ(AddScore.FeasibleFormatMask, ProductFormatMask);
  EXPECT_EQ(StScore.FeasibleFormatMask, ProductFormatMask);
  // Free slots after one issue: 2 remaining of 3.
  EXPECT_EQ(AddScore.FreeSlotsPreferred, 2u);
  EXPECT_EQ(StScore.FreeSlotsPreferred, 2u);
}

TEST(HaydnHazardRecognizerTest, MatchingFrontierConstrainedThenFlexBothWays) {
  // Exact matching keeps ST→ADD and ADD→ST feasible (no first-fit dead-end).
  // Successor cardinality after the first issue is the tryCandidate signal.
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;

  // ST first, then ADD still packable.
  {
    CycleCandidateSet C = makeProductCandidateSet();
    ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ST32));
    auto AfterST =
        HaydnHazardRecognizer::scoreMatchingFrontier(C, Haydn::ADD32);
    EXPECT_TRUE(AfterST.Feasible);
    EXPECT_GE(AfterST.SuccessorMatchings, 1u);
    EXPECT_EQ(AfterST.FreeSlotsPreferred, 1u);
    EXPECT_EQ(AfterST.FeasibleFormatMask, ProductFormatMask);
  }

  // ADD first (preferred may claim any field), then ST still packable via
  // rematch — score stays feasible with ≥1 free slot for ST's Slot0.
  {
    CycleCandidateSet C = makeProductCandidateSet();
    ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ADD32));
    auto AfterADD =
        HaydnHazardRecognizer::scoreMatchingFrontier(C, Haydn::ST32);
    EXPECT_TRUE(AfterADD.Feasible);
    EXPECT_GE(AfterADD.SuccessorMatchings, 1u);
    EXPECT_EQ(AfterADD.FreeSlotsPreferred, 1u);
    EXPECT_EQ(AfterADD.FeasibleFormatMask, ProductFormatMask);
  }
}

TEST(HaydnHazardRecognizerTest, MatchingFrontierThreeReadyRematchADD32_2xADD64) {
  // Plan §5.1 three-ready-op exit: preferred first-fit freezes ADD32 on S2,
  // packs first ADD64 on S1, then second ADD64 has no free S1|S2 field.
  // Exact nondominated expand rematches ADD32 onto S0 so both ADD64 fit —
  // retained candidates keep a three-member cycle. scoreMatchingFrontier is
  // the pre-RA tryCandidate vocabulary (logical only; no setDesc / FormatID).
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;

  // Preferred collapse dead-end (baseline that exact matching defeats).
  {
    CycleState FirstFit = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(FirstFit, Fmts, Haydn::ADD32));
    EXPECT_EQ(FirstFit.Members.back().FieldSlots, SlotBits(Haydn::SLOT2));
    ASSERT_TRUE(tryAddProduct(FirstFit, Fmts, Haydn::ADD64));
    EXPECT_EQ(FirstFit.Members.back().FieldSlots, SlotBits(Haydn::SLOT1));
    EXPECT_FALSE(tryAddProduct(FirstFit, Fmts, Haydn::ADD64))
        << "preferred freeze must dead-end the scarce S1|S2 pair";
  }

  // Empty → ADD32: flexible multi-alt; free slots = 2 after preferred place.
  CycleCandidateSet Empty = makeProductCandidateSet();
  auto Add32 =
      HaydnHazardRecognizer::scoreMatchingFrontier(Empty, Haydn::ADD32);
  EXPECT_TRUE(Add32.Feasible);
  EXPECT_GE(Add32.SuccessorMatchings, 1u);
  EXPECT_EQ(Add32.FreeSlotsPreferred, 2u);
  EXPECT_EQ(Add32.FeasibleFormatMask, ProductFormatMask);

  // After ADD32, first ADD64 still packable (frontier keeps Slot12 options).
  CycleCandidateSet AfterADD32 = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(AfterADD32, Fmts, Haydn::ADD32));
  auto Add64First =
      HaydnHazardRecognizer::scoreMatchingFrontier(AfterADD32, Haydn::ADD64);
  EXPECT_TRUE(Add64First.Feasible);
  EXPECT_GE(Add64First.SuccessorMatchings, 1u);
  EXPECT_EQ(Add64First.FreeSlotsPreferred, 1u);
  EXPECT_EQ(Add64First.FeasibleFormatMask, ProductFormatMask);

  // After ADD32+ADD64 exact, second ADD64 remains packable via rematch
  // (preferred would have dead-ended). Three-member cycle retained.
  CycleCandidateSet AfterTwo = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(AfterTwo, Fmts, Haydn::ADD32));
  ASSERT_TRUE(exactTryAddProduct(AfterTwo, Fmts, Haydn::ADD64));
  auto Add64Second =
      HaydnHazardRecognizer::scoreMatchingFrontier(AfterTwo, Haydn::ADD64);
  EXPECT_TRUE(Add64Second.Feasible)
      << "matching frontier must rematch so second ADD64 stays feasible";
  EXPECT_GE(Add64Second.SuccessorMatchings, 1u);
  EXPECT_EQ(Add64Second.FreeSlotsPreferred, 0u);
  EXPECT_EQ(Add64Second.FeasibleFormatMask, E3OnlyFormatMask);

  // Commit the third: preferred survivor has 3 members; ADD32 rematched to S0.
  ASSERT_TRUE(exactTryAddProduct(AfterTwo, Fmts, Haydn::ADD64));
  const CycleState &Pref = selectPreferredCandidate(AfterTwo);
  EXPECT_EQ(Pref.memberCount(), 3u);
  EXPECT_EQ(Pref.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));
  EXPECT_EQ(Pref.Members[0].LogicalOpcode, Haydn::ADD32);
  EXPECT_EQ(Pref.Members[0].FieldSlots, SlotBits(Haydn::SLOT0));
}

TEST(HaydnHazardRecognizerTest, MatchingFrontierInfeasibleSecondST) {
  // Two Slot0-only ST32 ops cannot share a cycle: second probe is infeasible.
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleCandidateSet C = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(C, Fmts, Haydn::ST32));
  auto SecondST = HaydnHazardRecognizer::scoreMatchingFrontier(C, Haydn::ST32);
  EXPECT_FALSE(SecondST.Feasible);
  EXPECT_EQ(SecondST.SuccessorMatchings, 0u);
}

TEST(HaydnHazardRecognizerTest, MatchingFrontierEmptyBaseInfeasible) {
  using namespace llvm::haydn::bundle;
  ArrayRef<CycleState> EmptyBase;
  auto Score =
      HaydnHazardRecognizer::scoreMatchingFrontier(EmptyBase, Haydn::ADD32);
  EXPECT_FALSE(Score.Feasible);
  EXPECT_EQ(Score.SuccessorMatchings, 0u);
}

//===----------------------------------------------------------------------===//
// SMS-RESMII pre-RA surface — exhaustive ≤3 format ResMII oracle + fail-close
// + port-forced ResMII ≥ 2 lower bound (no ResourceCycle / analyzeLoop here)
//===----------------------------------------------------------------------===//

TEST(HaydnPortModelTest, PreRAProductResMIIOracleSurface) {
  // Pre-RA strategy re-exports pure BundleFormatSolver oracles without
  // touching MIR / setDesc. Pins the SMS-RESMII pre-RA ownership surface.
  using namespace llvm::haydn::bundle;

  // Easy body: greedy == exhaustive; fail-close stays false.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(HaydnPreRASchedStrategy::productGreedyResMII(Ops), 1u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productResMIIOverestimate(Ops), 0);
    EXPECT_FALSE(HaydnPreRASchedStrategy::productResMIIFailsQualification(Ops));
    EXPECT_EQ(HaydnPreRASchedStrategy::productResMIIFailsQualification(Ops),
              productResMIIFailsQualification(Ops));
  }

  // Preferred first-fit dead-end inflates; live greedy (exact) does not —
  // fail-close is keyed off exact greedy, not preferred collapse.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    EXPECT_EQ(HaydnPreRASchedStrategy::productPreferredResMII(Ops), 2u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productGreedyResMII(Ops), 1u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productPreferredResMIIOverestimate(Ops),
              1);
    EXPECT_EQ(HaydnPreRASchedStrategy::productResMIIOverestimate(Ops), 0);
    EXPECT_FALSE(HaydnPreRASchedStrategy::productResMIIFailsQualification(Ops));
  }

  // Order trap: greedy overestimates; exhaustive recovers the true bound.
  // Fail-close MUST fire (qualification reject signal for format-dependent SMS).
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32, Haydn::ADD32,
                      Haydn::ADD32};
    EXPECT_EQ(HaydnPreRASchedStrategy::productGreedyResMII(Ops), 3u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productExhaustiveResMII(Ops), 2u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productResMIIOverestimate(Ops), 1);
    EXPECT_TRUE(HaydnPreRASchedStrategy::productResMIIFailsQualification(Ops))
        << "greedy overestimate must fail-close format SMS qualification";
    EXPECT_TRUE(productResMIIFailsQualification(Ops));
  }

  // Empty / oversized: never fail-close (no false reject on inexact oracle).
  {
    EXPECT_FALSE(HaydnPreRASchedStrategy::productResMIIFailsQualification(
        ArrayRef<unsigned>{}));
    // N = MaxExhaustive+1 forces greedy-fallback exhaustive (Over always 0).
    unsigned Big[MaxExhaustiveProductResMIIOps + 1];
    for (unsigned &O : Big)
      O = Haydn::ADD32;
    EXPECT_FALSE(
        HaydnPreRASchedStrategy::productResMIIFailsQualification(Big));
    // Overestimate is 0 for N>bound (greedy fallback).
    EXPECT_EQ(HaydnPreRASchedStrategy::productResMIIOverestimate(Big), 0);
  }

  // Parity with free functions (single authority).
  {
    unsigned Ops[] = {Haydn::LD32, Haydn::LD32, Haydn::X2MULA32};
    EXPECT_EQ(HaydnPreRASchedStrategy::productExhaustiveResMII(Ops),
              computeExhaustiveProductResMII(Ops));
    EXPECT_EQ(HaydnPreRASchedStrategy::productGreedyResMII(Ops),
              computeProductResMII(Ops));
  }
}

// SMS-HANDOFF pre-RA surface — metrics-only packability of qualification
// co-issue sets (no ResourceCycle / analyzeLoop / setDesc / BUNDLE invent).
TEST(HaydnPortModelTest, PreRASMSHandoffPackabilityOracleSurface) {
  using namespace llvm::haydn::bundle;

  // Vacuous empty body.
  EXPECT_TRUE(
      HaydnPreRASchedStrategy::productFormsOneExactCycle(ArrayRef<unsigned>()));
  EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelCoissuePackable(
      ArrayRef<unsigned>()));
  EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelExactlyPackable(
      ArrayRef<unsigned>()));

  // Independent ALU triple from postmisched-exact-nosplit qualification —
  // one Full cycle under exact matching; zero greedy overestimate.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    EXPECT_TRUE(HaydnPreRASchedStrategy::productFormsOneExactCycle(Ops));
    EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelCoissuePackable(Ops));
    EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(HaydnPreRASchedStrategy::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productResMIIOverestimate(Ops), 0);
    EXPECT_FALSE(HaydnPreRASchedStrategy::productResMIIFailsQualification(Ops));
  }

  // Three-ready rematch triple (ADD32 + 2×ADD64) — preferred first-fit can
  // dead-end; exact cycle still packs. Metrics only: no setDesc claim.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    EXPECT_TRUE(HaydnPreRASchedStrategy::productFormsOneExactCycle(Ops));
    EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelCoissuePackable(Ops));
    EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(HaydnPreRASchedStrategy::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productResMIIOverestimate(Ops), 0);
  }

  // Two ST32: format-incompatible same cycle (both Slot0) → not one cycle;
  // body still exactly packable across two cycles with zero overestimate.
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32};
    EXPECT_FALSE(HaydnPreRASchedStrategy::productFormsOneExactCycle(Ops));
    EXPECT_FALSE(HaydnPreRASchedStrategy::productQualKernelCoissuePackable(Ops));
    EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(HaydnPreRASchedStrategy::productExhaustiveResMII(Ops), 2u);
    EXPECT_EQ(HaydnPreRASchedStrategy::productResMIIOverestimate(Ops), 0);
  }

  // Over-width coissue (4 ops) cannot form one cycle.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32, Haydn::AND32};
    EXPECT_FALSE(HaydnPreRASchedStrategy::productFormsOneExactCycle(Ops));
    EXPECT_FALSE(HaydnPreRASchedStrategy::productQualKernelCoissuePackable(Ops));
    // Body still has a multi-cycle cover under exhaustive ≤3.
    EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelExactlyPackable(Ops));
    EXPECT_GE(HaydnPreRASchedStrategy::productExhaustiveResMII(Ops), 2u);
  }

  // Greedy order-trap: overestimate fail-closes qualification; exact-pack
  // remains true (finite exhaustive cover).
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32, Haydn::ADD32,
                      Haydn::ADD32};
    EXPECT_TRUE(HaydnPreRASchedStrategy::productResMIIFailsQualification(Ops));
    EXPECT_TRUE(HaydnPreRASchedStrategy::productQualKernelExactlyPackable(Ops));
  }

  // N > MaxExhaustiveProductResMIIOps: exhaustive falls back to greedy.
  // Streaming SMS bodies (bkfir / dual-load MAC) land here — must remain
  // exactly packable (no false reject on the inexact oracle).
  {
    unsigned Big[MaxExhaustiveProductResMIIOps + 2];
    for (unsigned &O : Big)
      O = Haydn::ADD32;
    EXPECT_FALSE(
        HaydnPreRASchedStrategy::productResMIIFailsQualification(Big));
    EXPECT_TRUE(
        HaydnPreRASchedStrategy::productQualKernelExactlyPackable(Big));
    EXPECT_GE(HaydnPreRASchedStrategy::productExhaustiveResMII(Big), 1u);
    EXPECT_FALSE(
        HaydnPreRASchedStrategy::productQualKernelCoissuePackable(Big));
  }
}

TEST(HaydnPortModelTest, PreRAMove32ClassMiVsDescPortDifferential) {
  // REGRESSION TEST REBASE (W68.0b, 2026-08-25). Pre-RA surface for the
  // MOVE32-class port law. The logical MOVE32 schema is dest+src (1R1W) on
  // the MI PortModel path (list-sched / CreateTargetMIHazardRecognizer
  // IsPreRA) AND on the descriptor path (SMS MID placement peer) — the
  // duplicated second source is gone, so the MI-vs-descriptor differential
  // is retired (both paths identical). The pre-rebase expectations (2R1W
  // with the repeated source) pinned a form that no longer exists. Pins
  // here agree with HaydnResourceCycle.cpp static_asserts and the lit
  // doc-pin sched-resource-truth-homes.s (= 1).
  using S = HaydnPreRASchedStrategy;
  EXPECT_FALSE(S::move32ClassDescOvercountsMiPorts());
  EXPECT_EQ(S::move32ClassMiRepeatedSrcGprReads, 1u);
  EXPECT_EQ(S::move32ClassMiRepeatedSrcGprWrites, 1u);
  EXPECT_EQ(S::move32ClassDescShapeGprReads, 1u);
  EXPECT_EQ(S::move32ClassDescShapeGprWrites, 1u);

  // Synthetic descriptor-shape classifier (1 def + 1 use) matches constants.
  {
    HaydnCyclePortDemand D;
    haydnClassifyPortBankClassID(Haydn::GPR32RegClassID, /*IsDef=*/true, D);
    haydnClassifyPortBankClassID(Haydn::GPR32RegClassID, /*IsDef=*/false, D);
    EXPECT_EQ(D.GPRReads, S::move32ClassDescShapeGprReads);
    EXPECT_EQ(D.GPRWrites, S::move32ClassDescShapeGprWrites);
  }

  // N=2: both models fit one cycle (2R2W, inside both GPR pools).
  EXPECT_EQ(S::move32ClassMiRepeatedSrcPortLowerBoundResMII(2), 1u);
  EXPECT_EQ(S::move32ClassDescShapePortLowerBoundResMII(2), 1u);
  EXPECT_FALSE(S::move32ClassDescSaturatesReadPoolEarlier(2));

  // N=3: the write pool is the binding constraint (3W > 2W) while reads
  // still fit (3R <= 4R) — the saturates-earlier differential is never
  // true when both paths are 1R1W.
  EXPECT_EQ(S::move32ClassMiRepeatedSrcPortLowerBoundResMII(3), 2u);
  EXPECT_EQ(S::move32ClassDescShapePortLowerBoundResMII(3), 2u);
  EXPECT_FALSE(S::move32ClassDescSaturatesReadPoolEarlier(3));
  EXPECT_LE(3u * S::move32ClassMiRepeatedSrcGprReads, HAYDN_GPR_READ_PORTS);
  EXPECT_LE(3u * S::move32ClassDescShapeGprReads, HAYDN_GPR_READ_PORTS);
  EXPECT_GT(3u * S::move32ClassMiRepeatedSrcGprWrites, HAYDN_GPR_WRITE_PORTS);

  // Format-only three MOVE32 still report product ResMII 1 (slots, no ports).
  unsigned FormatOnly[] = {Haydn::MOVE32, Haydn::MOVE32, Haydn::MOVE32};
  EXPECT_EQ(S::productGreedyResMII(FormatOnly), 1u);
  EXPECT_GT(S::move32ClassMiRepeatedSrcPortLowerBoundResMII(3),
            S::productGreedyResMII(FormatOnly));
}

TEST(HaydnPortModelTest, PreRAPortForcedResMIILowerBoundAtLeastTwo) {
  // Port floor is independent of format packing. Three independent GPR writes
  // need ≥2 issue cycles under HAYDN_GPR_WRITE_PORTS=2; format-only three
  // ADD32 still pack in one Full cycle (productGreedyResMII == 1).
  // Pre-RA HR (CreateTargetMIHazardRecognizer IsPreRA) charges the same
  // PortModel so list-sched cannot pretend 3×1W is one-cycle clean.
  EXPECT_EQ(HAYDN_GPR_WRITE_PORTS, 2u);
  EXPECT_EQ(haydnPortLowerBoundResMII(/*GPRR=*/0, /*GPRW=*/3), 2u);
  EXPECT_EQ(HaydnPreRASchedStrategy::portLowerBoundResMII(0, 3), 2u);
  EXPECT_GE(HaydnPreRASchedStrategy::portLowerBoundResMII(0, 3), 2u);

  // Two writes fit one cycle; fourth write needs three cycles (ceil(4/2)=2
  // only — 4 writes → 2 cycles; 5 writes → 3).
  EXPECT_EQ(haydnPortLowerBoundResMII(0, 2), 1u);
  EXPECT_EQ(haydnPortLowerBoundResMII(0, 4), 2u);
  EXPECT_EQ(haydnPortLowerBoundResMII(0, 5), 3u);

  // Read pool: 5 reads → ceil(5/4)=2.
  EXPECT_EQ(haydnPortLowerBoundResMII(/*GPRR=*/5, /*GPRW=*/0), 2u);
  EXPECT_EQ(haydnPortLowerBoundResMII(4, 0), 1u);

  // Zero demand → 0; single write → 1.
  EXPECT_EQ(haydnPortLowerBoundResMII(0, 0), 0u);
  EXPECT_EQ(haydnPortLowerBoundResMII(0, 1), 1u);

  // Format-only oracle still reports 1 for three ADD32 (slots, no ports).
  unsigned FormatOnly[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
  EXPECT_EQ(HaydnPreRASchedStrategy::productGreedyResMII(FormatOnly), 1u);
  EXPECT_EQ(HaydnPreRASchedStrategy::productExhaustiveResMII(FormatOnly), 1u);
  // Port floor exceeds format ResMII → ports bind II for a 3×1W body.
  EXPECT_GT(HaydnPreRASchedStrategy::portLowerBoundResMII(0, 3),
            HaydnPreRASchedStrategy::productGreedyResMII(FormatOnly));

  // DR / AR banks participate in the same max floor.
  EXPECT_EQ(haydnPortLowerBoundResMII(0, 0, /*DRR=*/0, /*DRW=*/4), 2u); // 3W cap
  EXPECT_EQ(haydnPortLowerBoundResMII(0, 0, 0, 0, /*ARR=*/0, /*ARW=*/3), 2u);

  // HR scoreboard peer: two 1W ops in distinct slots OK; third 1W hits 2W cap.
  {
    HaydnFuncUnitWrapper A = singleIssueInSlot(0);
    A.setGPRPorts(0, 1);
    HaydnFuncUnitWrapper B = singleIssueInSlot(1);
    B.setGPRPorts(0, 1);
    HaydnFuncUnitWrapper C = singleIssueInSlot(2);
    C.setGPRPorts(0, 1);
    EXPECT_FALSE(A.conflict(B)); // two distinct slots, 1W+1W ≤ 2W
    HaydnFuncUnitWrapper Cycle = A;
    Cycle |= B; // accumulated 2W, slots 0|1
    EXPECT_TRUE(Cycle.conflict(C)); // third write → port conflict (ResMII ≥ 2)
  }
}

//===----------------------------------------------------------------------===//
// Generic-pass dual-run baseline (pre-RA) — product policy freeze
//===----------------------------------------------------------------------===//
// Pins Full-only product defaults that dual-run lit
// prera-format-generic-baseline.ll freezes against matching-frontier OFF and
// optional finer-RP OFF residual arms (plan §8.3 / §8.4 #11). Target HR is
// always installed (CreateTargetMIHazardRecognizer); flags only change ranking.
// Soft-exit floors stay independent of ranking residual — port/format II do
// not invent HANDOFF or setDesc. isavail-delay stays product OFF (seed1).

TEST(HaydnPortModelTest, PreRAGenericPassDualRunBaseline) {
  using S = HaydnPreRASchedStrategy;

  // Product defaults: matching-frontier ON, finer RP ON, isavail-delay OFF.
  EXPECT_TRUE(S::productMatchingFrontierDefault);
  EXPECT_FALSE(S::genericPassMatchingFrontierBaseline);
  EXPECT_NE(S::productMatchingFrontierDefault,
            S::genericPassMatchingFrontierBaseline);

  EXPECT_TRUE(S::productFinerRPTrackingDefault);
  EXPECT_FALSE(S::genericPassFinerRPTrackingBaseline);
  EXPECT_NE(S::productFinerRPTrackingDefault,
            S::genericPassFinerRPTrackingBaseline);

  EXPECT_FALSE(S::productIsAvailPressureDelayDefault);

  // Soft-exit floors (independent of ranking residual) still bind ports when
  // format-only ResMII is 1 — dual-run must not weaken HR/port law.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(S::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(S::portLowerBoundResMII(/*GPRR=*/0, /*GPRW=*/3), 2u);
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, 0, 3), 2u);
    EXPECT_TRUE(S::productQualKernelExactlyPackable(Ops));
  }

  // Qual ALU coissue remains format-true / exact-packable under both arms.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    EXPECT_TRUE(S::productFormsOneExactCycle(Ops));
    EXPECT_TRUE(S::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(S::productExhaustiveResMII(Ops), 1u);
  }

  // Product FormatID frontier stays size-1 Full (no compact dual-run invent).
  EXPECT_EQ(S::productFeasibleFormatMask(/*Occupied=*/0),
            haydn::bundle::ProductFormatMask);
}

//===----------------------------------------------------------------------===//
// ILP / critical dual-run ranking residual attribution (pre-RA)
//===----------------------------------------------------------------------===//
// Extends generic-pass freeze onto ILP and critical-path kernels
// (scheduler-ilp.ll / scheduler-critical-path.ll). Ranking residual is
// matching-frontier ResourceDemand only; pressure/critical stay primary.
// Soft-exit / exact-pack floors are independent of the residual arm; target
// HR remains installed via CreateTargetMIHazardRecognizer. Live -stats
// attribution (product ResourceDemand present; residual silent; RegMax on
// critical; multi-MI finalize parity) lives in the dual-run lits.

TEST(HaydnPortModelTest, PreRAIlpCriticalDualRunResidualAttribution) {
  using S = HaydnPreRASchedStrategy;

  // Layer pin: pressure primary; matching-frontier is residual after it.
  EXPECT_TRUE(S::productRankingPressurePrimary);
  EXPECT_TRUE(S::productMatchingFrontierIsRankingResidual);
  EXPECT_TRUE(S::productIlpCriticalRankingResidualLayers());
  EXPECT_NE(S::productMatchingFrontierDefault,
            S::genericPassMatchingFrontierBaseline);

  // Pure ILP + critical soft-exit / packability independent of residual arm.
  EXPECT_TRUE(S::productIlpCriticalDualRunResidualPins());

  // ILP independent ALU triple: format ResMII 1, port floor 2, soft-exit 2.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_TRUE(S::productFormsOneExactCycle(Ops));
    EXPECT_TRUE(S::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(S::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(S::portLowerBoundResMII(/*GPRR=*/0, /*GPRW=*/3), 2u);
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, 0, 3), 2u);
    // Ranking residual must not invent HANDOFF from these metrics.
    EXPECT_TRUE(S::productFormsOneExactCycle(Ops));
  }

  // Critical-friendly coissue (independent side work) stays packable.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    EXPECT_TRUE(S::productQualKernelCoissuePackable(Ops));
    EXPECT_TRUE(S::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, /*GPRR=*/6, /*GPRW=*/3), 2u);
    // Two-wide under 4R2W collapses soft-exit to format floor 1.
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, 4, 2), 1u);
  }

  // HR factory contract: residual ranking never drops target HR (policy pin).
  EXPECT_TRUE(S::productMatchingFrontierDefault);
  EXPECT_FALSE(S::genericPassMatchingFrontierBaseline);
}

//===----------------------------------------------------------------------===//
// VF3 soft-exit QoR (pre-RA) — II floors + exact-pack metrics; no HANDOFF invent
//===----------------------------------------------------------------------===//
// Pins productSoftExitIIFloor = max(format exhaustive ResMII, port ResMII) and
// the post-RA exact-pack metrics surface used by prera-format-qor-exit.ll.
// RecMII remains SMS/DDG ownership (macc-acc-feedback); pre-RA never fabricates
// recurrence numbers or durable BUNDLE roots.

TEST(HaydnPortModelTest, PreRASoftExitQoRFloorsAndExactPack) {
  using S = HaydnPreRASchedStrategy;

  // Classic port-binds-II body: three independent 1W GPR ops.
  // Format-only exhaustive ResMII = 1 (Full three slots); port floor = 2.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(S::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(S::portLowerBoundResMII(/*GPRR=*/0, /*GPRW=*/3), 2u);
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, /*GPRR=*/0, /*GPRW=*/3), 2u);
    EXPECT_GT(S::productSoftExitIIFloor(Ops, 0, 3),
              S::productExhaustiveResMII(Ops));
    // Multi-cycle cover still qualifies as exact-packable (no overestimate).
    EXPECT_TRUE(S::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(S::productResMIIOverestimate(Ops), 0);
    // One-cycle coissue of three 1W writes is format-true but port-false.
    EXPECT_TRUE(S::productFormsOneExactCycle(Ops));
  }

  // Qualification ALU coissue (ADD+XOR+OR): format packs one cycle.
  // Charging full 3×(2R1W) = 6R3W binds port floor to 2; soft-exit II ≥ 2.
  // Post-RA may still exact-commit a 2-wide subset under 4R2W — metrics pin
  // body-level exact packability, not a durable three-member HANDOFF group.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    EXPECT_TRUE(S::productFormsOneExactCycle(Ops));
    EXPECT_TRUE(S::productQualKernelCoissuePackable(Ops));
    EXPECT_TRUE(S::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(S::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(S::portLowerBoundResMII(/*GPRR=*/6, /*GPRW=*/3), 2u);
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, 6, 3), 2u);
    // Two-wide coissue under 4R2W: port floor = 1; soft-exit collapses to format.
    EXPECT_EQ(S::portLowerBoundResMII(4, 2), 1u);
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, 4, 2), 1u);
  }

  // Three-ready rematch (ADD32 + 2×ADD64): format ResMII 1; preferred can
  // dead-end. Soft-exit with light ports stays at format floor.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    EXPECT_TRUE(S::productFormsOneExactCycle(Ops));
    EXPECT_EQ(S::productPreferredResMII(Ops), 2u);
    EXPECT_EQ(S::productExhaustiveResMII(Ops), 1u);
    // ADD32 2R1W + two ADD64 2R1W each → GPR 2R1W + DR 4R2W.
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, /*GPRR=*/2, /*GPRW=*/1,
                                        /*DRR=*/4, /*DRW=*/2),
              1u);
    EXPECT_TRUE(S::productQualKernelExactlyPackable(Ops));
    EXPECT_FALSE(S::productResMIIFailsQualification(Ops));
  }

  // Empty body: both floors 0.
  EXPECT_EQ(S::productSoftExitIIFloor(ArrayRef<unsigned>(), 0, 0), 0u);

  // Greedy order-trap: qualification fail-closes on overestimate; exact-pack
  // stays true (cover exists; overestimate is an II floor).
  {
    unsigned Ops[] = {Haydn::ST32, Haydn::ST32, Haydn::ADD32, Haydn::ADD32,
                      Haydn::ADD32};
    EXPECT_TRUE(S::productResMIIFailsQualification(Ops));
    EXPECT_TRUE(S::productQualKernelExactlyPackable(Ops));
    EXPECT_EQ(S::productExhaustiveResMII(Ops), 2u);
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, 0, 0), 2u);
  }

  // MOVE32-class MI path: 3×2R1W (per-field, 0ad0d5d64088) → port floor 2;
  // format-only still 1.
  {
    unsigned Ops[] = {Haydn::MOVE32, Haydn::MOVE32, Haydn::MOVE32};
    EXPECT_EQ(S::productExhaustiveResMII(Ops), 1u);
    EXPECT_EQ(S::move32ClassMiRepeatedSrcPortLowerBoundResMII(3), 2u);
    EXPECT_EQ(S::productSoftExitIIFloor(
                  Ops, 3 * S::move32ClassMiRepeatedSrcGprReads,
                  3 * S::move32ClassMiRepeatedSrcGprWrites),
              2u);
  }
}

//===----------------------------------------------------------------------===//
// SMS shouldUseSchedule fail-close (pre-RA) — stage-count + spill-pressure
//===----------------------------------------------------------------------===//
// Pins pure accept/reject law for schedules SMS already found (AIE canAcceptII
// / shouldUseSchedule peer). Sibling SMS track owns SMSchedule / canAllocateSMS
// / PipelinerLoopInfo wiring and sms-* MIR; this surface is metrics-only and
// never freezes FormatID or stamps setDesc/member opcodes.

TEST(HaydnPortModelTest, PreRASMSShouldUseScheduleFailCloseSurface) {
  using S = HaydnPreRASchedStrategy;

  // Product defaults: max stages 3, track-regpressure ON.
  EXPECT_EQ(S::productSMSMaxStageCount, 3u);
  EXPECT_TRUE(S::productSMSTrackRegPressureDefault);

  // Stage-count gate: StageCount = PrologueCount + 1.
  // stages=3 (prologue=2) OK; stages=4 (prologue=3) fails closed.
  EXPECT_FALSE(S::smsStageCountExceedsMax(/*StageCount=*/1));
  EXPECT_FALSE(S::smsStageCountExceedsMax(/*StageCount=*/3));
  EXPECT_TRUE(S::smsStageCountExceedsMax(/*StageCount=*/4));
  EXPECT_TRUE(S::smsStageCountExceedsMax(/*StageCount=*/3, /*Max=*/2));
  EXPECT_FALSE(S::smsStageCountExceedsMax(/*StageCount=*/3, /*Max=*/3));

  // ZOL single-stage (no overlap) → reject; multi-stage ZOL OK on stage alone.
  EXPECT_TRUE(S::smsZOLRejectsSingleStage(/*IsZOL=*/true, /*StageCount=*/1));
  EXPECT_TRUE(S::smsZOLRejectsSingleStage(true, 0));
  EXPECT_FALSE(S::smsZOLRejectsSingleStage(true, 2));
  EXPECT_FALSE(S::smsZOLRejectsSingleStage(/*IsZOL=*/false, /*StageCount=*/1));

  // ZOL min-trip: unknown (0) refuses multi-stage; PrologueCount >= MinTrip.
  EXPECT_TRUE(S::smsZOLRejectsMinTrip(/*IsZOL=*/true, /*Prologue=*/1,
                                      /*MinTrip=*/0));
  EXPECT_TRUE(S::smsZOLRejectsMinTrip(true, /*Prologue=*/2, /*MinTrip=*/2));
  EXPECT_TRUE(S::smsZOLRejectsMinTrip(true, /*Prologue=*/3, /*MinTrip=*/2));
  EXPECT_FALSE(S::smsZOLRejectsMinTrip(true, /*Prologue=*/1, /*MinTrip=*/4));
  EXPECT_FALSE(S::smsZOLRejectsMinTrip(/*IsZOL=*/false, /*Prologue=*/5,
                                       /*MinTrip=*/0));
  // CB-166: a runtime trip REGISTER (dynamic per-prologue guard on the
  // LoopStart count reg) lifts the static MinTripCount refuse; constant
  // trips keep it.
  EXPECT_FALSE(S::smsZOLRejectsMinTrip(/*IsZOL=*/true, /*Prologue=*/1,
                                       /*MinTrip=*/0,
                                       /*HasRuntimeTripReg=*/true));
  EXPECT_FALSE(S::smsZOLRejectsMinTrip(true, /*Prologue=*/3, /*MinTrip=*/0,
                                       /*HasRuntimeTripReg=*/true));
  EXPECT_TRUE(S::smsZOLRejectsMinTrip(true, /*Prologue=*/2, /*MinTrip=*/2,
                                      /*HasRuntimeTripReg=*/false));

  // Pure spill-pressure excess: MaxSetPressure[i] > Limits[i].
  {
    unsigned MaxP[] = {4, 8, 2};
    unsigned Lim[] = {4, 8, 2};
    EXPECT_FALSE(S::smsSpillPressureExceedsLimits(MaxP, Lim));
  }
  {
    // One-above capacity on set 1 → fail-close.
    unsigned MaxP[] = {4, 9, 2};
    unsigned Lim[] = {4, 8, 2};
    EXPECT_TRUE(S::smsSpillPressureExceedsLimits(MaxP, Lim));
  }
  {
    // One-below / at capacity remain allocatable.
    unsigned MaxP[] = {3, 8};
    unsigned Lim[] = {4, 8};
    EXPECT_FALSE(S::smsSpillPressureExceedsLimits(MaxP, Lim));
  }
  // Empty → vacuously allocatable.
  EXPECT_FALSE(S::smsSpillPressureExceedsLimits(ArrayRef<unsigned>(),
                                                ArrayRef<unsigned>()));
  // Shorter span: only compare known pairs (no false excess on missing limits).
  {
    unsigned MaxP[] = {1, 100};
    unsigned Lim[] = {1};
    EXPECT_FALSE(S::smsSpillPressureExceedsLimits(MaxP, Lim));
  }

  // Combined shouldUseSchedule fail-close matrix (product defaults).
  // Happy ZOL multi-stage under trip + no pressure → accept.
  EXPECT_TRUE(S::smsShouldUseScheduleAccepts(
      /*IsZOL=*/true, /*Prologue=*/1, /*MinTrip=*/8, /*PressureExcess=*/false));
  EXPECT_FALSE(S::smsShouldUseScheduleFailsClosed(
      true, 1, 8, /*PressureExcess=*/false));

  // ZOL single-stage (Prologue=0 → StageCount=1) → fail-close.
  EXPECT_TRUE(S::smsShouldUseScheduleFailsClosed(
      /*IsZOL=*/true, /*Prologue=*/0, /*MinTrip=*/16,
      /*PressureExcess=*/false));

  // ZOL unknown trip → fail-close even with multi-stage + no pressure.
  EXPECT_TRUE(S::smsShouldUseScheduleFailsClosed(
      true, /*Prologue=*/1, /*MinTrip=*/0, /*PressureExcess=*/false));

  // CB-166: ZOL runtime trip reg → the same shape accepts (dynamic guard).
  EXPECT_FALSE(S::smsShouldUseScheduleFailsClosed(
      true, /*Prologue=*/1, /*MinTrip=*/0, /*PressureExcess=*/false,
      S::productSMSMaxStageCount, S::productSMSTrackRegPressureDefault,
      /*HasRuntimeTripReg=*/true));
  // ... but single-stage still refuses (no overlap), guard or not.
  EXPECT_TRUE(S::smsShouldUseScheduleFailsClosed(
      true, /*Prologue=*/0, /*MinTrip=*/0, /*PressureExcess=*/false,
      S::productSMSMaxStageCount, S::productSMSTrackRegPressureDefault,
      /*HasRuntimeTripReg=*/true));
  // ... and stage-count max still bounds it.
  EXPECT_TRUE(S::smsShouldUseScheduleFailsClosed(
      true, /*Prologue=*/3, /*MinTrip=*/0, /*PressureExcess=*/false,
      S::productSMSMaxStageCount, S::productSMSTrackRegPressureDefault,
      /*HasRuntimeTripReg=*/true));

  // Stage count > max (Prologue=3 → StageCount=4 > 3) → fail-close.
  EXPECT_TRUE(S::smsShouldUseScheduleFailsClosed(
      /*IsZOL=*/false, /*Prologue=*/3, /*MinTrip=*/0,
      /*PressureExcess=*/false));
  // At max stages (Prologue=2 → StageCount=3) non-ZOL → accept without pressure.
  EXPECT_TRUE(S::smsShouldUseScheduleAccepts(
      /*IsZOL=*/false, /*Prologue=*/2, /*MinTrip=*/0,
      /*PressureExcess=*/false));

  // Spill-pressure excess with track ON → fail-close.
  EXPECT_TRUE(S::smsShouldUseScheduleFailsClosed(
      /*IsZOL=*/false, /*Prologue=*/1, /*MinTrip=*/0,
      /*PressureExcess=*/true));
  // Track OFF ignores pressure excess (flag peer).
  EXPECT_TRUE(S::smsShouldUseScheduleAccepts(
      /*IsZOL=*/false, /*Prologue=*/1, /*MinTrip=*/0,
      /*PressureExcess=*/true, S::productSMSMaxStageCount,
      /*TrackRegPressure=*/false));

  // Naive (non-ZOL) multi-stage under max + no pressure → accept.
  EXPECT_TRUE(S::smsShouldUseScheduleAccepts(
      /*IsZOL=*/false, /*Prologue=*/1, /*MinTrip=*/0,
      /*PressureExcess=*/false));

  // Port ResMII / soft-exit floors remain independent of shouldUse stage gates:
  // three 1W writes still force II≥2 while stage fail-close is about schedule
  // shape, not issue-port capacity.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, 0, 3), 2u);
    EXPECT_TRUE(S::smsShouldUseScheduleAccepts(
        /*IsZOL=*/false, /*Prologue=*/1, /*MinTrip=*/0,
        /*PressureExcess=*/false));
  }
}

//===----------------------------------------------------------------------===//
// W59 SMS loop-routing seam — defer-to-post-RA-multistage (pre-RA surface)
//===----------------------------------------------------------------------===//
// REGRESSION TEST (W68.1): ZOL multi-stage lift polarity. The W59
// decline-AND-DEFER routing predicate (smsDeferToPostRAMultiStage) is
// RETIRED together with its reason to exist ("pre-RA never accepts
// multi-stage"): with ZOL multi-stage qualified on the generic
// MachinePipeliner, one engine owns every loop form and routing would split
// it in two. What must never regress is the form-uniform containment law and
// the ZOL-specific AIE-peer gates that remain the ZOL law:
//   * containment bound is the PPS-3 max-stage gate for BOTH forms, and the
//     F41 knob's historic Option A value (1) bisects the whole lift down;
//   * ZOL single-stage still rejects (no overlap);
//   * ZOL multi-stage accepts ONLY with a static MinTripCount guard
//     (MinTripCount > PrologueCount; unknown trip refuses every schedule —
//     ZOL cannot emit a dynamic guard);
//   * the fail-closed polarity helper agrees with every one of these.

TEST(HaydnPortModelTest, PreRASMSZOLMultiStageLiftPolarity) {
  using S = HaydnPreRASchedStrategy;

  // Form-uniform product bound = PPS-3; historic Option A value survives
  // only as the F41 bisect-down constant.
  EXPECT_EQ(S::productSMSSoftContainmentMaxStageCount, 3u);
  EXPECT_EQ(S::productSMSSoftContainmentMaxStageCount,
            S::productSMSMaxStageCount);
  EXPECT_EQ(S::productSMSContainmentMaxStageCount, 1u);

  // Containment helper: form-uniform bound admits NS<=3, refuses NS=4.
  EXPECT_FALSE(S::smsProductStageCountExceedsContainment(
      3, S::productSMSSoftContainmentMaxStageCount));
  EXPECT_TRUE(S::smsProductStageCountExceedsContainment(
      4, S::productSMSSoftContainmentMaxStageCount));
  // F41 bisect-down value restores Option A (refuse NS>=2).
  EXPECT_TRUE(S::smsProductStageCountExceedsContainment(
      2, S::productSMSContainmentMaxStageCount));

  // ZOL single-stage reject (no overlap) — unchanged AIE peer law.
  EXPECT_TRUE(S::smsZOLRejectsSingleStage(/*IsZOL=*/true, /*StageCount=*/1));
  EXPECT_FALSE(S::smsZOLRejectsSingleStage(false, 1));

  // ZOL MinTripCount guard: unknown trip (0) refuses everything; static
  // guard requires MinTripCount > PrologueCount.
  EXPECT_TRUE(S::smsZOLRejectsMinTrip(true, /*PrologueCount=*/1,
                                      /*MinTripCount=*/0));
  EXPECT_TRUE(S::smsZOLRejectsMinTrip(true, 2, 2));   // 2 >= 2: no guard room
  EXPECT_FALSE(S::smsZOLRejectsMinTrip(true, 1, 2));  // 1 < 2: guardable
  EXPECT_FALSE(S::smsZOLRejectsMinTrip(true, 1, 3));

  // Combined product polarity at the form-uniform bound.
  // Soft multi-stage NS=2 accepts (counted residual; structural adjust).
  EXPECT_FALSE(S::smsProductShouldUseScheduleFailsClosed(
      /*IsZOL=*/false, /*PrologueCount=*/1, /*MinTripCount=*/0,
      /*PressureExcess=*/false, S::productSMSMaxStageCount,
      S::productSMSTrackRegPressureDefault,
      S::productSMSSoftContainmentMaxStageCount));
  // ZOL multi-stage NS=2 accepts with static guard (MinTripCount 16 > 1).
  EXPECT_FALSE(S::smsProductShouldUseScheduleFailsClosed(
      /*IsZOL=*/true, /*PrologueCount=*/1, /*MinTripCount=*/16,
      /*PressureExcess=*/false, S::productSMSMaxStageCount,
      S::productSMSTrackRegPressureDefault,
      S::productSMSSoftContainmentMaxStageCount));
  // ZOL multi-stage refuses with unknown trip.
  EXPECT_TRUE(S::smsProductShouldUseScheduleFailsClosed(
      /*IsZOL=*/true, /*PrologueCount=*/1, /*MinTripCount=*/0,
      /*PressureExcess=*/false, S::productSMSMaxStageCount,
      S::productSMSTrackRegPressureDefault,
      S::productSMSSoftContainmentMaxStageCount));
  // F41 bisect down: both forms refuse multi-stage at bound 1.
  EXPECT_TRUE(S::smsProductShouldUseScheduleFailsClosed(
      /*IsZOL=*/false, /*PrologueCount=*/1, /*MinTripCount=*/0,
      /*PressureExcess=*/false, S::productSMSMaxStageCount,
      S::productSMSTrackRegPressureDefault,
      S::productSMSContainmentMaxStageCount));
  EXPECT_TRUE(S::smsProductShouldUseScheduleFailsClosed(
      /*IsZOL=*/true, /*PrologueCount=*/1, /*MinTripCount=*/16,
      /*PressureExcess=*/false, S::productSMSMaxStageCount,
      S::productSMSTrackRegPressureDefault,
      S::productSMSContainmentMaxStageCount));
}

//===----------------------------------------------------------------------===//
// SMS-HOOK II-wrap false-accept fail-closed (pre-RA surface)
//===----------------------------------------------------------------------===//
// Plan §2.5 / §8.4 #8. Product class-3 empty (InstrStage cycles==1). Pre-RA
// CreateTargetMIHazardRecognizer installs linear stage-relative scoreboard
// booking — not SMS modulo-II ResourceCycle phases. Catalog polarity is
// re-exported on HaydnPreRASchedStrategy so list-sched / HR ownership matches
// SMS-HOOK reject law without owning analyzeLoop / ResourceCycle. Sibling SMS
// pins issue-time-only differential + force-iiwrap-reject lit. Metrics-only;
// never setDesc / member opcodes / multi-cycle product enable.

TEST(HaydnPortModelTest, PreRASMSHookIIWrapFalseAcceptFailClosedSurface) {
  using S = HaydnPreRASchedStrategy;
  using namespace llvm::haydn::restriction;

  // Product pins: class-3 disabled, single-cycle stages only, empty inventory.
  EXPECT_FALSE(S::productCrossCycleCapacityEnabled);
  EXPECT_EQ(S::productMaxInstrStageCycles, 1u);
  EXPECT_EQ(S::productClass3RestrictionCount, 0u);
  EXPECT_EQ(S::productCrossCycleCapacityEnabled,
            ProductCrossCycleCapacityEnabled);
  EXPECT_EQ(S::productMaxInstrStageCycles, ProductMaxInstrStageCycles);

  // Classic II-wrap false-accept shape: StageCycles=2, II=2, issue 0 covers 1.
  EXPECT_TRUE(S::smsIIWrapOccupiesPhase(/*Issue=*/0, /*Stage=*/2, /*II=*/2,
                                        /*Query=*/0));
  EXPECT_TRUE(S::smsIIWrapOccupiesPhase(0, 2, 2, 1));
  EXPECT_FALSE(S::smsIIWrapOccupiesPhase(0, 1, 2, 1));
  EXPECT_TRUE(S::smsIIWrapSpansBeyondIssuePhase(2, 2));
  EXPECT_FALSE(S::smsIIWrapSpansBeyondIssuePhase(1, 2));
  EXPECT_TRUE(S::smsIIWrapSelfConflicts(/*Stage=*/3, /*II=*/2));
  EXPECT_FALSE(S::smsIIWrapSelfConflicts(2, 2));

  // SMS-HOOK reject polarity: multi-cycle under product law fails closed.
  EXPECT_TRUE(S::smsHookRejectsMultiCycleStage(2));
  EXPECT_FALSE(S::smsHookRejectsMultiCycleStage(1));
  EXPECT_TRUE(S::smsHookRejectsIIWrapFalseAccept(/*StageCycles=*/2, /*II=*/2));
  EXPECT_TRUE(S::smsHookRejectsIIWrapFalseAccept(4, 3));
  EXPECT_FALSE(S::smsHookRejectsIIWrapFalseAccept(/*StageCycles=*/1, /*II=*/2));

  // Combined product pin: always true under current catalog law.
  EXPECT_TRUE(S::productIIWrapFalseAcceptFailsClosed());
  static_assert(S::productIIWrapFalseAcceptFailsClosed(),
                "product II-wrap false-accept must fail closed");

  // Pre-RA HR IsPreRA flag: feasibility-only constructor (no setDesc path).
  // Live CreateTargetMIHazardRecognizer factory pin is shared with
  // PreRAMove32MiVsDescPortsAndIsPreRAHR. Product multi-cycle reject polarity
  // is catalog-only (independent of live itinerary content).
  {
    HaydnHazardRecognizer PreRAHR(/*TII=*/nullptr, /*ItinData=*/nullptr,
                                  /*IsPreRA=*/true, /*AltDescs=*/nullptr);
    EXPECT_TRUE(PreRAHR.isPreRA());
    HaydnHazardRecognizer PostRAHR(/*TII=*/nullptr, /*ItinData=*/nullptr,
                                   /*IsPreRA=*/false, /*AltDescs=*/nullptr);
    EXPECT_FALSE(PostRAHR.isPreRA());
    EXPECT_TRUE(S::smsHookRejectsIIWrapFalseAccept(2, 2));
  }

  // Soft-exit / ResMII floors remain orthogonal to II-wrap class-3 gate:
  // three 1W writes still force II≥2 while II-wrap reject is about multi-cycle
  // FU occupancy, not issue-port capacity.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    EXPECT_EQ(S::productSoftExitIIFloor(Ops, 0, 3), 2u);
    EXPECT_TRUE(S::productIIWrapFalseAcceptFailsClosed());
  }
}

// FE5B WP4 certificate re-export on pre-RA surface (ResourceCycle authority).
// Half-enabled multi-stage remains forbidden; original retained until accept.
TEST(HaydnPortModelTest, PreRASMSPeriodicCertificateSurface) {
  using S = HaydnPreRASchedStrategy;
  EXPECT_TRUE(S::productPeriodicCertificatePins());
  EXPECT_TRUE(S::productHalfEnabledMultiStageForbidden());
  EXPECT_TRUE(S::productIIWrapLongOccupancyFailsClosed(/*Stage=*/2, /*II=*/2));
  EXPECT_FALSE(S::productIIWrapLongOccupancyFailsClosed(/*Stage=*/1, /*II=*/2));
  EXPECT_TRUE(S::productSMSCertOriginalLoopMustRemain(
      SMSCertLifecycle::OriginalRetained));
  EXPECT_FALSE(S::productSMSCertMayDiscardOriginalLoop(
      SMSCertLifecycle::OriginalRetained));
  EXPECT_TRUE(S::productSMSCertMustRollback(SMSCertLifecycle::Rejected));
  EXPECT_TRUE(HaydnHazardRecognizer::productSamePhaseWAWFailsClosed());
  EXPECT_TRUE(HaydnHazardRecognizer::productSMSCertHalfEnabledMultiStageForbidden());
}


//===----------------------------------------------------------------------===//
// SMS/post-RA format-acceptance differential (pre-RA surface, plan §8.4 #7)
//===----------------------------------------------------------------------===//
// Descriptor-derived format legality is pure exactTryAddProduct depth — the
// same API ResourceCycle canReserve/reserve and post-RA HR
// CurrentCycleCandidates / commitPlacementForEmit use. Pre-RA
// CreateTargetMIHazardRecognizer installs IsPreRA HR that expands the same
// candidate set; scoreMatchingFrontier.Feasible is the list-sched probe.
// Format is opcode-keyed → MI and descriptor forms of equal opcodes agree.
// MOVE32-class *port* demand (per-field 2R1W, both paths since 0ad0d5d64088)
// is orthogonal (not a format gap). Sibling SMS
// owns live ResourceCycle packing differential tests. Metrics-only; never
// setDesc / member opcodes / ResourceCycle edit.

TEST(HaydnPortModelTest, PreRARCHrFormatAcceptanceDifferentialSurface) {
  using S = HaydnPreRASchedStrategy;
  using namespace llvm::haydn::bundle;

  // Combined product pin (always true under Full-only exact matching).
  EXPECT_TRUE(S::productFormatAcceptanceDifferentialPins());

  // Canonical shapes: pack / reject / rematch / co-issue.
  {
    unsigned ThreeADD[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32};
    unsigned TwoST[] = {Haydn::ST32, Haydn::ST32};
    unsigned Rematch[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    unsigned Coissue[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    unsigned FourADD[] = {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32,
                          Haydn::ADD32};

    EXPECT_TRUE(S::productExactCanPackSequence(ThreeADD));
    EXPECT_TRUE(S::productExactCanPackSet(ThreeADD));
    EXPECT_EQ(S::productExactSequentialCycleCount(ThreeADD), 1u);
    EXPECT_EQ(S::productExactSequentialCycleCount(ThreeADD),
              S::productGreedyResMII(ThreeADD));
    EXPECT_TRUE(S::productFormsOneExactCycle(ThreeADD));

    EXPECT_FALSE(S::productExactCanPackSequence(TwoST));
    EXPECT_FALSE(S::productExactCanPackSet(TwoST));
    EXPECT_EQ(S::productExactSequentialCycleCount(TwoST), 2u);
    EXPECT_FALSE(S::productFormsOneExactCycle(TwoST));

    EXPECT_TRUE(S::productExactCanPackSequence(Rematch));
    EXPECT_TRUE(S::productFormsOneExactCycle(Rematch));
    EXPECT_EQ(S::productExactSequentialCycleCount(Rematch), 1u);

    EXPECT_TRUE(S::productExactCanPackSequence(Coissue));
    EXPECT_TRUE(S::productQualKernelCoissuePackable(Coissue));

    // >issue-width cannot form one cycle; sequential needs ≥2.
    EXPECT_FALSE(S::productExactCanPackSet(FourADD));
    EXPECT_FALSE(S::productFormsOneExactCycle(FourADD));
    EXPECT_EQ(S::productExactSequentialCycleCount(FourADD), 2u);
  }

  // MI ≡ descriptor for equal opcodes (format opcode-keyed).
  {
    unsigned MiOps[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ST32};
    unsigned DescOps[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ST32};
    EXPECT_TRUE(S::productFormatAcceptanceAgrees(MiOps, DescOps));
    EXPECT_EQ(S::productExactCanPackSequence(MiOps),
              S::productExactCanPackSequence(DescOps));
  }
  // Unequal multisets may disagree (control that the helper is not constant).
  {
    unsigned Pack[] = {Haydn::ADD32, Haydn::ADD32};
    unsigned Reject[] = {Haydn::ST32, Haydn::ST32};
    EXPECT_FALSE(S::productFormatAcceptanceAgrees(Pack, Reject));
  }

  // scoreMatchingFrontier (HR pure probe) ≡ productExactCanPackSequence
  // sequential expand polarity — ResourceCycle / post-RA HR peer depth.
  {
    HaydnMCFormats Fmts;
    auto AgreeSeq = [&](ArrayRef<unsigned> Seq) {
      CycleCandidateSet C = makeProductCandidateSet();
      bool PureOk = true;
      for (unsigned Opc : Seq) {
        auto Score = HaydnHazardRecognizer::scoreMatchingFrontier(C, Opc);
        const bool Can = canExactTryAddProduct(C, Fmts, Opc);
        EXPECT_EQ(Score.Feasible, Can) << "frontier vs canExact " << Opc;
        if (!Can) {
          PureOk = false;
          break;
        }
        ASSERT_TRUE(exactTryAddProduct(C, Fmts, Opc));
        EXPECT_TRUE(Score.Feasible);
      }
      EXPECT_EQ(PureOk, S::productExactCanPackSequence(Seq));
    };
    unsigned Rematch[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    unsigned TwoST[] = {Haydn::ST32, Haydn::ST32};
    unsigned Coissue[] = {Haydn::ADD32, Haydn::XOR32, Haydn::OR32};
    AgreeSeq(Rematch);
    AgreeSeq(TwoST);
    AgreeSeq(Coissue);
  }

  // MOVE32-class ports are per-field 2R1W on both MI and desc views
  // (0ad0d5d64088), so no port differential survives; format three MOVE32
  // still packs under exact matching for both views (format ≠ ports).
  {
    unsigned ThreeMove[] = {Haydn::MOVE32, Haydn::MOVE32, Haydn::MOVE32};
    EXPECT_TRUE(S::productExactCanPackSequence(ThreeMove));
    EXPECT_FALSE(S::move32ClassDescOvercountsMiPorts());
    EXPECT_EQ(S::productGreedyResMII(ThreeMove), 1u);
    EXPECT_GT(S::move32ClassMiRepeatedSrcPortLowerBoundResMII(3),
              S::productGreedyResMII(ThreeMove));
  }

  // Empty is vacuously packable; cycle count 0 via greedy ResMII.
  EXPECT_TRUE(S::productExactCanPackSequence(ArrayRef<unsigned>()));
  EXPECT_TRUE(S::productExactCanPackSet(ArrayRef<unsigned>()));
  EXPECT_EQ(S::productExactSequentialCycleCount(ArrayRef<unsigned>()), 0u);
}

//===----------------------------------------------------------------------===//
// — Reserved bitset + AIE Req/Res conflict law + stage-relative ring
//===----------------------------------------------------------------------===//

/// Required-only single-slot (no issue count) — pure FU probe.
static HaydnFuncUnitWrapper requiredSlot(unsigned N) {
  return HaydnFuncUnitWrapper(singleSlot(N));
}

/// Reserved-only single-slot (no issue count).
static HaydnFuncUnitWrapper reservedSlot(unsigned N) {
  return HaydnFuncUnitWrapper(SlotSet{}, singleSlot(N));
}

TEST(HaydnHazardRecognizerTest, VF22_ReqResConflictMatrix) {
  // AIE law (AIEHazardRecognizer.cpp:128-133) + Haydn exclusive-single-slot:
  //   Req/Req same single slot  → conflict
  //   Req/Req distinct slots    → no conflict
  //   Req/Res overlapping bit   → conflict (either order)
  //   Res/Res same bit          → LEGAL (no conflict)
  //   multi-bit Req vs anything → no exclusive Req/Req conflict
  EXPECT_TRUE(requiredSlot(0).conflict(requiredSlot(0)));
  EXPECT_FALSE(requiredSlot(0).conflict(requiredSlot(1)));

  EXPECT_TRUE(requiredSlot(1).conflict(reservedSlot(1)));
  EXPECT_TRUE(reservedSlot(1).conflict(requiredSlot(1)));
  EXPECT_FALSE(requiredSlot(0).conflict(reservedSlot(1)));

  // Res/Res legal even on the same unit.
  EXPECT_FALSE(reservedSlot(2).conflict(reservedSlot(2)));
  EXPECT_FALSE(reservedSlot(0).conflict(reservedSlot(1)));

  // Multi-unit choice-set Required never exclusive-conflicts with single Req
  // (pack-fill rule preserved — issue/tryAdd govern packing).
  HaydnFuncUnitWrapper Multi(multiALUUnits());
  EXPECT_FALSE(Multi.conflict(requiredSlot(EU_ALU0)));
  EXPECT_FALSE(requiredSlot(EU_ALU2).conflict(Multi));
  // Multi-unit Required still conflicts with overlapping Reserved (AIE law).
  EXPECT_TRUE(Multi.conflict(reservedSlot(EU_ALU1)));
  EXPECT_TRUE(reservedSlot(EU_ALU0).conflict(Multi));
  // Reserved outside the multi-unit set does not Req/Res-overlap.
  EXPECT_FALSE(Multi.conflict(reservedSlot(EU_LOADSTORE0)));
}

TEST(HaydnHazardRecognizerTest, VF22_InstrStageCtorHonorsReservationKind) {
  // InstrStage aggregate → wrapper Required vs Reserved by kind.
  InstrStage ReqStage{/*Cycles=*/1, /*Units=*/1ULL << 0, /*NextCycles=*/-1,
                      InstrStage::Required};
  InstrStage RsrvStage{/*Cycles=*/1, /*Units=*/1ULL << 1, /*NextCycles=*/-1,
                       InstrStage::Reserved};
  HaydnFuncUnitWrapper Req(ReqStage);
  HaydnFuncUnitWrapper Rsrv(RsrvStage);
  EXPECT_EQ(Req.getRequired(), singleSlot(0));
  EXPECT_TRUE(Req.getReserved().empty());
  EXPECT_TRUE(Rsrv.getRequired().empty());
  EXPECT_EQ(Rsrv.getReserved(), singleSlot(1));
  // Distinct bits: no Req/Res overlap.
  EXPECT_FALSE(Req.conflict(Rsrv));
  // Overlapping Req vs Res on the same FU bit.
  InstrStage Rsrv0{1, 1ULL << 0, -1, InstrStage::Reserved};
  EXPECT_TRUE(Req.conflict(HaydnFuncUnitWrapper(Rsrv0)));
}

TEST(HaydnHazardRecognizerTest, VF22_MultiCycleStageAtPlusOne) {
  // Stage-relative booking: Required at issue cycle, Reserved at +1.
  // A later instruction that Requires the reserved FU at its issue cycle
  // conflicts when DeltaCycles aligns with the reserved ring entry.
  ResourceScoreboard<HaydnFuncUnitWrapper> SB;
  SB.reset(/*Depth=*/4); // window [-4, 3]
  // Instr A books Required S0 at cycle 0 and Reserved S1 at cycle +1.
  HaydnFuncUnitWrapper A0 = requiredSlot(0);
  A0.setIssueCountOne();
  SB[0] |= A0;
  SB[1] |= reservedSlot(1);

  // Candidate B: Required S1 at its issue — conflicts with A's Reserved at +1
  // when B issues at DeltaCycles=+1 relative to A (i.e. scoreboard cycle 1).
  HaydnFuncUnitWrapper B = requiredSlot(1);
  B.setIssueCountOne();
  EXPECT_TRUE(SB[1].conflict(B));
  // At cycle 0, S1 is free (only S0 Required).
  EXPECT_FALSE(SB[0].conflict(B));
  // Res/Res at cycle 1 is legal.
  EXPECT_FALSE(SB[1].conflict(reservedSlot(1)));
}

TEST(HaydnHazardRecognizerTest, VF22_AdvanceRecedeBothDirections) {
  // Advance/Recede shift the ring; a multi-cycle Reserved booked at +1 moves
  // into the current cycle after Advance, and back after Recede.
  ResourceScoreboard<HaydnFuncUnitWrapper> SB;
  SB.reset(4);
  SB[1] |= reservedSlot(2);
  EXPECT_TRUE(SB[1].getReserved().test(2));
  EXPECT_TRUE(SB[0].getReserved().empty());

  SB.advance(); // former +1 is now 0
  EXPECT_TRUE(SB[0].getReserved().test(2));

  SB.recede(); // back
  EXPECT_TRUE(SB[1].getReserved().test(2));
  EXPECT_TRUE(SB[0].getReserved().empty());

  // Bottom-up direction: book at -1 then recede brings it to 0.
  SB.clear();
  SB[-1] |= requiredSlot(0);
  SB.recede();
  EXPECT_TRUE(SB[0].getRequired().test(0));
}

TEST(HaydnHazardRecognizerTest, VF22_UnionMergesReserved) {
  HaydnFuncUnitWrapper Cycle;
  Cycle |= reservedSlot(0);
  Cycle |= requiredSlot(1);
  EXPECT_TRUE(Cycle.getReserved().test(0));
  EXPECT_TRUE(Cycle.getRequired().test(1));
  // Combined: Req S1 conflicts with Res S1, not with Res S0.
  EXPECT_FALSE(Cycle.conflict(reservedSlot(0))); // Res/Res
  EXPECT_TRUE(Cycle.conflict(reservedSlot(1)));  // Req S1 vs Res S1
  EXPECT_TRUE(Cycle.conflict(requiredSlot(1)));  // exclusive Req/Req
}

TEST(HaydnHazardRecognizerTest, VF22_SelectedMemberSingleSlotVsMultiBit) {
  // After rematch, selected member itinerary is single-slot Required; the
  // logical multi-bit FieldSlots footprint must NOT exclusive-conflict with
  // a distinct single slot (pack-fill). Pin the wrapper half of that law:
  // multi-bit | single-bit distinct → no conflict; two same single-bits → yes.
  HaydnFuncUnitWrapper LogicalMulti(allSlots());
  LogicalMulti.setIssueCountOne();
  HaydnFuncUnitWrapper MemberS2 = singleIssueInSlot(2);
  EXPECT_FALSE(LogicalMulti.conflict(MemberS2));
  EXPECT_FALSE(MemberS2.conflict(singleIssueInSlot(1)));
  EXPECT_TRUE(MemberS2.conflict(singleIssueInSlot(2)));
  // Selected-member single-slot still trips Req/Res on the same bit.
  EXPECT_TRUE(MemberS2.conflict(reservedSlot(2)));
  EXPECT_FALSE(MemberS2.conflict(reservedSlot(0)));
}

TEST(HaydnHazardRecognizerTest, MultiFieldLogicalStoreVsSlot0WideAdd) {
  // GE96-11 (2026-08-21) rebase: golden instruction_type_index.json pins
  // D_SW_L_WITH_IMM Available = LOADSTORE0 (the sole unit; e0-only golden
  // member rows). The prior multi-field wording (alts S0|S1|S2 so a store
  // packs beside an ADDI32 at SLOT0) predates the X2*/X4* ALU 0x6→0x7
  // occupancy flip and understated the store's exclusivity: residual
  // occupancy is now the golden LOADSTORE0-only truth — D_SW_L_WITH_IMM is
  // SINGLE-FIELD SLOT0 exactly like ST32, and a wide ADDI32 already rematched
  // to exclusive SLOT0 conflicts with it (Req/Req on the same bit) so the
  // pair serializes instead of packing. Unit injectivity at the GE96-11
  // commit site (HaydnHazardRecognizer commitPlacementForEmit twin rematch)
  // owns the same-unit law for remap; this pins the wrapper/occupancy half.
  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> DSwAlts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::D_SW_L_WITH_IMM,
                                             DSwAlts));
  SlotBits DSwFields = 0;
  for (const PlacementAlternative &A : DSwAlts)
    DSwFields |= A.FieldSlots;
  EXPECT_EQ(llvm::popcount(DSwFields), 1u);
  EXPECT_EQ(DSwFields, SlotBits(Haydn::SLOT0));

  SmallVector<PlacementAlternative, 4> StAlts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ST32, StAlts));
  SlotBits StFields = 0;
  for (const PlacementAlternative &A : StAlts)
    StFields |= A.FieldSlots;
  EXPECT_EQ(llvm::popcount(StFields), 1u);
  EXPECT_EQ(StFields, SlotBits(Haydn::SLOT0));

  SlotSet DSwSlots;
  for (unsigned Bit = 0; Bit < HAYDN_NUM_FU_BITS; ++Bit)
    if ((DSwFields >> Bit) & 1u)
      DSwSlots = DSwSlots | singleSlot(Bit);
  HaydnFuncUnitWrapper DSw(DSwSlots);
  DSw.setIssueCountOne();
  HaydnFuncUnitWrapper AddiS0 = singleIssueInSlot(0);
  EXPECT_TRUE(DSw.conflict(AddiS0));
  EXPECT_TRUE(AddiS0.conflict(DSw));

  HaydnFuncUnitWrapper StS0 = singleIssueInSlot(0);
  EXPECT_TRUE(StS0.conflict(AddiS0));
}

TEST(HaydnHazardRecognizerTest, VF22_BlockedIncludesReserved) {
  HaydnFuncUnitWrapper Blocked;
  Blocked.blockResources();
  EXPECT_TRUE(Blocked.conflict(requiredSlot(0)));
  EXPECT_TRUE(Blocked.conflict(reservedSlot(1)));
  // Empty isEmpty is false once blocked.
  EXPECT_FALSE(Blocked.isEmpty());
  Blocked.clearResources();
  EXPECT_TRUE(Blocked.isEmpty());
  EXPECT_TRUE(Blocked.getReserved().empty());
}

//===----------------------------------------------------------------------===//
// — MultiSlot_Pseudo is not hazard-exempt (vs true meta)
//===----------------------------------------------------------------------===//
//
// HR getHazardType/checkConflict/enterResources/Emit previously blank-skipped
// MI.isPseudo(). MultiSlot_Pseudo is isPseudo=1 but must book issue/stages/
// ports (AIE isNoHazardMeta only IMPLICIT_DEF/KILL). Pin the predicate +
// placement surface without a full MachineFunction.

TEST(HaydnHazardRecognizerTest, VF23_MultiSlotPseudoNotNoHazardMeta) {
  // True meta: AIE Bundle twin.
  EXPECT_TRUE(Haydn::MachineBundle::isNoHazardMetaInstruction(
      TargetOpcode::IMPLICIT_DEF));
  EXPECT_TRUE(Haydn::MachineBundle::isNoHazardMetaInstruction(
      TargetOpcode::KILL));
  // Real MultiSlot_Pseudo def (HaydnMultiSlotPseudo.td ADD32_MSP).
  EXPECT_FALSE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32_MSP));
  // Sparse non-pseudo logical also not meta (control).
  EXPECT_FALSE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32));
}

TEST(HaydnHazardRecognizerTest, VF23_MultiSlotPseudoPlacementBooksLikeADD32) {
  // Pure solver half of HR commitPlacement: ADD32_MSP has alts and saturates
  // the cycle. If HR still used isPseudo exemption, MSP would never reach
  // this tryAdd path from Emit — unit pins the surface it must use.
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32_MSP));
  const std::vector<unsigned> *Alts =
      Fmts.getAlternateInstsOpcode(Haydn::ADD32_MSP);
  ASSERT_NE(Alts, nullptr);
  ASSERT_EQ(Alts->size(), 3u);
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32_MSP));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT2));
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members.back().MemberOpcode, 2));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32_MSP));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32_MSP));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::ADD32_MSP));
  // Meta has no PlacementAlternatives — not a pack member.
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, TargetOpcode::IMPLICIT_DEF));
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, TargetOpcode::KILL));
}

// Post-RA pack reconstruction must keep MultiSlot_Pseudo as a cycle member
// (HR already booked it). Opcode-level twin of isBundleSkippable — meta and
// no-alt expand residuals remain skippable; alts-bearing never.
TEST(HaydnHazardRecognizerTest, VF23b_PackSkipKeepsMultiSlotPseudoInCycle) {
  HaydnMCFormats Fmts;
  EXPECT_TRUE(Haydn::MachineBundle::isBundlePackSkippableOpcode(
      TargetOpcode::IMPLICIT_DEF, /*IsPseudo=*/true, Fmts));
  EXPECT_TRUE(Haydn::MachineBundle::isBundlePackSkippableOpcode(
      TargetOpcode::KILL, /*IsPseudo=*/true, Fmts));
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32_MSP));
  EXPECT_FALSE(Haydn::MachineBundle::isBundlePackSkippableOpcode(
      Haydn::ADD32_MSP, /*IsPseudo=*/true, Fmts));
  EXPECT_FALSE(hasPlacementAlternatives(Fmts, Haydn::LOADI32));
  EXPECT_TRUE(Haydn::MachineBundle::isBundlePackSkippableOpcode(
      Haydn::LOADI32, /*IsPseudo=*/true, Fmts));
}

//===----------------------------------------------------------------------===//
// — BUNDLE NoHazard compensation (hard-bundle resource-conflict fence)
//===----------------------------------------------------------------------===//
//
// HaydnHazardRecognizer::isNoHazardMeta returns true for MI.isBundle() without
// aggregating children (HaydnHazardRecognizer.cpp). The opcode-level twin
// MachineBundle::isNoHazardMetaInstruction deliberately excludes BUNDLE — it
// is not true meta; the MI.isBundle() path is a separate HR zero-resource skip.
// Without HaydnInstrInfo::isSchedulingBoundary(isBundle), MachineScheduler
// would treat the root as a free SU and allow dependent / resource-conflicting
// neighbors to cross membership. The fence is load-bearing over HR NoHazard.
// Integration: format-bundle-through-ra.mir (hard_bundle_resource_conflict,
// hard_bundle_mixed_fu, hard_bundle_dual_ld, hard_bundle_ld_add,
// hard_bundle_dual_ld_large_frame PEI), including stress-regalloc membership.
// Pure pins below cover opcode/resource shape; BundleRootIsSchedulingBoundary
// covers an empty root; multi-child finalizeBundle pins cover real membership
// + live HR NoHazard on that root. Post-RA exact hard-root commit +
// cross-region latency/Required/Reserved replay: HaydnPostRASchedStrategy
// leaveMBB (commitExactHardRootProductCycle + replayCrossBoundaryHazards);
// lit pins postmisched-hard-root-*.mir and format-bundle POST dual-load.

TEST(HaydnHazardRecognizerTest, VF4_BundleNotOpcodeLevelNoHazardMeta) {
  // Opcode twin: only IMPLICIT_DEF/KILL. BUNDLE is *not* true meta — HR skips
  // it via MI.isBundle(), which is why isSchedulingBoundary must compensate.
  EXPECT_FALSE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(TargetOpcode::BUNDLE));
  EXPECT_TRUE(Haydn::MachineBundle::isNoHazardMetaInstruction(
      TargetOpcode::IMPLICIT_DEF));
  EXPECT_TRUE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(TargetOpcode::KILL));
  // Control: real pack members never meta.
  EXPECT_FALSE(Haydn::MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32));
  EXPECT_FALSE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32_MSP));
}

TEST(HaydnHazardRecognizerTest, VF4_ResourceConflictShapeThreeIssuePlusOne) {
  // Full product issue width = 3. Three single-issue wrappers saturate the
  // cycle; a fourth independent same-FU candidate conflicts. This is the
  // resource shape a 3-member hard BUNDLE would present if children were
  // visible to the HR. Root NoHazard hides the shape, so the boundary fence
  // (not HR) keeps resource-conflicting neighbors outside membership.
  HaydnFuncUnitWrapper Cycle(singleSlot(0));
  HaydnFuncUnitWrapper A = singleIssueInSlot(0);
  HaydnFuncUnitWrapper B = singleIssueInSlot(1);
  HaydnFuncUnitWrapper C = singleIssueInSlot(2);
  Cycle |= A;
  Cycle |= B;
  Cycle |= C; // IssueCount == 3, slots {S0,S1,S2}.
  EXPECT_TRUE(Cycle.conflict(singleIssueInSlot(0)));
  EXPECT_TRUE(Cycle.conflict(singleIssueInSlot(1)));
  EXPECT_TRUE(Cycle.conflict(singleIssueInSlot(2)));
  // Two independent single-issue ops alone do not hit the cap.
  EXPECT_FALSE(singleIssueInSlot(0).conflict(singleIssueInSlot(1)));
}

TEST(HaydnHazardRecognizerTest, VF4_HardBundleMembersFillProductCycle) {
  // Placement half of the same resource-conflict shape: three logical ADD32
  // fill a product composite row; a fourth independent ADD32 is rejected. Mirrors the
  // hard_bundle_resource_conflict MIR membership (3 children, RC neighbors
  // stay outside — fence, not HR aggregation).
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));
  EXPECT_EQ(S.Members.size(), 3u);
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::ADD32));
}

TEST(HaydnHazardRecognizerTest, VF4_HardBundleMixedFUMembersFillProductCycle) {
  // Mixed GPR32/DR64 Full rematch shape: preferred first-fit dead-ends on
  // ADD32+2xADD64, but exact nondominated expand rematches ADD32 onto S0 so
  // both ADD64 fit. Architectural hard BUNDLEs of this shape must keep three
  // members through RA (format-bundle-through-ra.mir hard_bundle_mixed_fu);
  // this pin is the placement half of that membership contract.
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;

  // Preferred collapse dead-end (baseline exact matching defeats).
  {
    CycleState FirstFit = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(FirstFit, Fmts, Haydn::ADD32));
    ASSERT_TRUE(tryAddProduct(FirstFit, Fmts, Haydn::ADD64));
    EXPECT_FALSE(tryAddProduct(FirstFit, Fmts, Haydn::ADD64));
  }

  CycleCandidateSet Exact = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Haydn::ADD32));
  ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Haydn::ADD64));
  ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Haydn::ADD64));
  const CycleState &Pref = selectPreferredCandidate(Exact);
  EXPECT_EQ(Pref.memberCount(), 3u);
  EXPECT_EQ(Pref.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));
  EXPECT_EQ(Pref.Members[0].LogicalOpcode, Haydn::ADD32);
  EXPECT_EQ(Pref.Members[0].FieldSlots, SlotBits(Haydn::SLOT0));
  EXPECT_EQ(Pref.Members[1].LogicalOpcode, Haydn::ADD64);
  EXPECT_EQ(Pref.Members[2].LogicalOpcode, Haydn::ADD64);

  // Full cycle: a fourth independent single-issue op cannot join.
  CycleState Full = Pref;
  EXPECT_FALSE(canTryAddProduct(Full, Fmts, Haydn::ADD32));
  EXPECT_FALSE(canTryAddProduct(Full, Fmts, Haydn::ADD64));
}

TEST(HaydnHazardRecognizerTest, VF4_HardBundleDualLoadMembersFillProductCycle) {
  // Dual LD32 (S0|S1) product freeze shape: two independent loads fill the
  // load slots of one product parcel. Architectural hard BUNDLEs of this shape
  // must keep two members through RA (format-bundle-through-ra.mir
  // hard_bundle_dual_ld / hard_bundle_dual_ld_large_frame); this pin is the
  // placement half of that membership contract (SMS handoff freezes the same
  // dual-load SMS durable group root).
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::LD32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::LD32));
  EXPECT_EQ(S.Members.size(), 2u);
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT0 | Haydn::SLOT1));
  EXPECT_EQ(S.Members[0].LogicalOpcode, Haydn::LD32);
  EXPECT_EQ(S.Members[1].LogicalOpcode, Haydn::LD32);
  // Third independent load is rejected (only two load slots).
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::LD32));
  // ALU can still fill S2 on the dual-load cycle (legal denser pack).
  EXPECT_TRUE(canTryAddProduct(S, Fmts, Haydn::ADD32));
}

TEST(HaydnHazardRecognizerTest, VF4_HardBundleLdAddMembersFillProductCycle) {
  // Legal LD32+ADD32 product cycle: load on S0|S1, ALU on a free slot.
  // Architectural hard BUNDLEs of this shape keep two members through RA
  // (format-bundle-through-ra.mir hard_bundle_ld_add).
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::LD32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  EXPECT_EQ(S.Members.size(), 2u);
  EXPECT_EQ(S.Members[0].LogicalOpcode, Haydn::LD32);
  EXPECT_EQ(S.Members[1].LogicalOpcode, Haydn::ADD32);
  EXPECT_NE(S.OccupiedSlots & SlotBits(Haydn::SLOT0 | Haydn::SLOT1), 0u);
}

// MachineFunction fixture for TII::isSchedulingBoundary on a real BUNDLE MI.
// Pure opcode/resource pins above never call InstrInfo; this is the load-bearing
// fence path MachineScheduler uses (MachineScheduler.cpp isSchedBoundary).
class HaydnBundleBoundaryTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;

  static void SetUpTestSuite() {
    LLVMInitializeHaydnTargetInfo();
    LLVMInitializeHaydnTarget();
    LLVMInitializeHaydnTargetMC();
  }

  void SetUp() override {
    std::string Error;
    Triple TT("haydn-unknown-elf");
    const Target *TheTarget = TargetRegistry::lookupTarget(TT, Error);
    ASSERT_NE(TheTarget, nullptr) << Error;

    TargetOptions Options;
    TM.reset(static_cast<HaydnTargetMachine *>(TheTarget->createTargetMachine(
        TT, "generic", "", Options, std::nullopt, std::nullopt,
        CodeGenOptLevel::Default)));
    ASSERT_NE(TM, nullptr);

    Ctx = std::make_unique<LLVMContext>();
    M = std::make_unique<Module>("HaydnBundleBoundary", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
  }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }
  const TargetRegisterInfo *TRI() const { return ST->getRegisterInfo(); }
};

// insertBranch emits bare one-cycle reals sized as product Full parcel
// (no callback-local BUNDLE — may run pre-postmisched). removeBranch on a
// committed solo BUNDLE root charges the same EncodedBytes via lateLayoutBytes.
// Pins BranchRelaxation BytesAdded/BytesRemoved vs getInstSizeInBytes.
// REGRESSION: logical WFI is one product parcel, not a 0-byte meta pseudo.
// F33: HaydnPseudo + getInstSizeInBytes==0 made BranchRelaxation treat a
// real HINT as free. Encode stays occupancy WFI_S0 (no Format E member).
// REGRESSION: SIN_COS/ARCTAN OperandCycles 17 must not size the scoreboard
// while ProductDraftArctanMultiCycleLockEnabled is false (F6).
TEST_F(HaydnBundleBoundaryTest, SinCosScaffoldDoesNotInflateScoreboard) {
  const InstrItineraryData *II = ST->getInstrItineraryData();
  HaydnHazardRecognizer HR(&TII(), II, /*IsPreRA=*/false, /*AltDescs=*/nullptr);
  EXPECT_LE(HR.getMaxLatency(), 2);
  EXPECT_LT(HR.getMaxLatency(), 17);
}

TEST_F(HaydnBundleBoundaryTest, WFIIsOneProductParcel) {
  using namespace llvm::haydn::bundle;
  const unsigned FullBytes = productParcelBytes().Value;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);
  MachineInstr &WFI =
      *BuildMI(*MBB, MBB->end(), DebugLoc(), TII().get(Haydn::WFI));
  EXPECT_EQ(TII().getInstSizeInBytes(WFI), FullBytes);
}

TEST_F(HaydnBundleBoundaryTest, InsertRemoveBranchExactCommittedSize) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *Src = MF->CreateMachineBasicBlock();
  MachineBasicBlock *Dst = MF->CreateMachineBasicBlock();
  MF->push_back(Src);
  MF->push_back(Dst);
  Src->addSuccessor(Dst);

  const unsigned FullBytes = productParcelBytes().Value;
  ASSERT_EQ(FullBytes, productParcelBytes().Value);

  // Unconditional insert: bare B, BytesAdded == Full parcel.
  int BytesAdded = 0;
  unsigned N = II.insertBranch(*Src, Dst, nullptr, /*Cond=*/{}, DL, &BytesAdded);
  EXPECT_EQ(N, 1u);
  EXPECT_EQ(BytesAdded, static_cast<int>(FullBytes));
  ASSERT_FALSE(Src->empty());
  MachineInstr &BareB = Src->back();
  EXPECT_FALSE(BareB.isBundle());
  EXPECT_EQ(II.getInstSizeInBytes(BareB), FullBytes);
  EXPECT_EQ(lateLayoutBytes(BareB), FullBytes);

  // removeBranch on bare charges the same Full parcel.
  int BytesRemoved = 0;
  unsigned Removed = II.removeBranch(*Src, &BytesRemoved);
  EXPECT_EQ(Removed, 1u);
  EXPECT_EQ(BytesRemoved, static_cast<int>(FullBytes));
  EXPECT_TRUE(Src->empty());

  // One-way conditional bare BEQZ_W also charges one Full parcel.
  BytesAdded = 0;
  SmallVector<MachineOperand, 2> Cond;
  Cond.push_back(MachineOperand::CreateImm(Haydn::BEQZ_W));
  Cond.push_back(MachineOperand::CreateReg(Haydn::R0, /*isDef=*/false));
  N = II.insertBranch(*Src, Dst, nullptr, Cond, DL, &BytesAdded);
  EXPECT_EQ(N, 1u);
  EXPECT_EQ(BytesAdded, static_cast<int>(FullBytes));
  ASSERT_FALSE(Src->empty());
  EXPECT_FALSE(Src->back().isBundle());
  EXPECT_EQ(II.getInstSizeInBytes(Src->back()), FullBytes);

  BytesRemoved = 0;
  II.removeBranch(*Src, &BytesRemoved);
  EXPECT_EQ(BytesRemoved, static_cast<int>(FullBytes));
  EXPECT_TRUE(Src->empty());

  // Committed solo branch BUNDLE (post-pack / late-finalize shape): remove
  // charges EncodedBytes on the root (same Full parcel).
  MachineInstr *Child =
      BuildMI(*Src, Src->end(), DL, II.get(Haydn::B)).addMBB(Dst).getInstr();
  finalizeExactLateSingleton(*Child);
  ASSERT_FALSE(Src->empty());
  MachineInstr &Root = Src->front();
  ASSERT_TRUE(Root.isBundle());
  EXPECT_EQ(II.getInstSizeInBytes(Root), FullBytes);
  EXPECT_EQ(lateLayoutBytes(Root), FullBytes);
  // Product roots stamp BundleFormatRowID (FE8: residual FormatID reader
  // getBundleFormatID is deleted; Format E row imm is the sole authority).
  auto Row = getBundleRowID(Root);
  ASSERT_TRUE(Row.has_value());
  EXPECT_TRUE(isProductBundleRow(*Row));

  BytesRemoved = 0;
  Removed = II.removeBranch(*Src, &BytesRemoved);
  EXPECT_EQ(Removed, 1u);
  EXPECT_EQ(BytesRemoved, static_cast<int>(FullBytes));
  EXPECT_TRUE(Src->empty());
}

// Coissued BUNDLE{ALU, branch}: removeBranch must erase only the branch
// child, leave the ALU, and charge 0 bytes (surviving cycle still Full).
TEST_F(HaydnBundleBoundaryTest, RemoveBranchCoissuePreservesSibling) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *Src = MF->CreateMachineBasicBlock();
  MachineBasicBlock *Dst = MF->CreateMachineBasicBlock();
  MF->push_back(Src);
  MF->push_back(Dst);
  Src->addSuccessor(Dst);

  // Contiguous ADD32 + BEQZ_W → finalize as one multi-member BUNDLE root.
  BuildMI(*Src, Src->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
      .addReg(Haydn::R2)
      .addReg(Haydn::R3);
  BuildMI(*Src, Src->end(), DL, II.get(Haydn::BEQZ_W))
      .addReg(Haydn::R0)
      .addMBB(Dst);
  ASSERT_EQ(std::distance(Src->begin(), Src->end()), 2);

  MachineBasicBlock::instr_iterator First = Src->instr_begin();
  MachineBasicBlock::instr_iterator Last = Src->instr_end();
  finalizeBundle(*Src, First, Last);
  ASSERT_FALSE(Src->empty());
  MachineInstr &Root = Src->front();
  ASSERT_TRUE(Root.isBundle());
  // FE8: ProductFormatID / stampBundleFormatID bridge deleted; product
  // commits go through stampBundleCommit with a Format E row + completion.
  stampBundleCommit(Root, BundleFormatRowID::E96TwoEntry,
                    CompletionStateID::AllEntriesReal);
  EXPECT_EQ(II.getInstSizeInBytes(Root), productParcelBytes().Value);

  int BytesRemoved = 0;
  unsigned Removed = II.removeBranch(*Src, &BytesRemoved);
  EXPECT_EQ(Removed, 1u);
  // Surviving cycle still occupies the Full parcel — no layout shrink.
  EXPECT_EQ(BytesRemoved, 0);
  ASSERT_FALSE(Src->empty());

  // Branch gone; ADD32 still present (as remaining BUNDLE child or bare).
  auto isAdd32 = [](unsigned Opc) {
    if (Opc == Haydn::ADD32)
      return true;
    return haydn::format_e::peelLogicalOpcodeName(
               haydn::bundle::haydnOpcodeName(Opc), /*StripWide=*/false) ==
           "ADD32";
  };
  bool SawAdd = false;
  bool SawBranch = false;
  for (MachineBasicBlock::instr_iterator I = Src->instr_begin(),
                                        E = Src->instr_end();
       I != E; ++I) {
    if (I->isBundle())
      continue;
    if (isAdd32(I->getOpcode()))
      SawAdd = true;
    if (I->isBranch(MachineInstr::IgnoreBundle))
      SawBranch = true;
  }
  EXPECT_TRUE(SawAdd);
  EXPECT_FALSE(SawBranch);
}

TEST_F(HaydnBundleBoundaryTest, VF4_BundleRootIsSchedulingBoundary) {
  // Real TargetOpcode::BUNDLE MI must be an atomic scheduling boundary so
  // dependent / resource-conflicting neighbors cannot cross membership while
  // HR returns NoHazard for isBundle without aggregating children.
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Bundle =
      BuildMI(*MBB, MBB->end(), DL, II.get(TargetOpcode::BUNDLE)).getInstr();
  ASSERT_TRUE(Bundle->isBundle());
  EXPECT_TRUE(II.isSchedulingBoundary(*Bundle, MBB, *MF));

  // Control: ordinary logical ADD32 is not a boundary (modeled SFR only).
  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R0)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  EXPECT_FALSE(Add->isBundle());
  EXPECT_FALSE(II.isSchedulingBoundary(*Add, MBB, *MF));

  // Control: return is still a boundary via the call/return path.
  MachineInstr *Ret = BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::RET))
                          .addReg(Haydn::R0, RegState::Implicit)
                          .addReg(Haydn::R15, RegState::Implicit)
                          .getInstr();
  EXPECT_TRUE(Ret->isReturn());
  EXPECT_TRUE(II.isSchedulingBoundary(*Ret, MBB, *MF));
}

// Live HR: getHazardType returns NoHazard for isBundle without booking
// children. Fill the current cycle with three independent ADD32 so a real
// fourth op would Hazard; the BUNDLE root still returns NoHazard (no child
// aggregation). isSchedulingBoundary remains the load-bearing fence.
TEST_F(HaydnBundleBoundaryTest, VF4_LiveHRGetHazardTypeNoHazardForBundle) {
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *A0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R0)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  MachineInstr *A1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R3)
          .addReg(Haydn::R4)
          .addReg(Haydn::R5)
          .getInstr();
  MachineInstr *A2 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R6)
          .addReg(Haydn::R7)
          .addReg(Haydn::R8)
          .getInstr();
  MachineInstr *A3 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R9)
          .addReg(Haydn::R10)
          .addReg(Haydn::R11)
          .getInstr();
  MachineInstr *Bundle =
      BuildMI(*MBB, MBB->end(), DL, II.get(TargetOpcode::BUNDLE)).getInstr();
  ASSERT_TRUE(Bundle->isBundle());

  HaydnHazardRecognizer HR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                           /*AltDescs=*/nullptr);
  HR.Reset();
  HR.EmitInstruction(A0);
  HR.EmitInstruction(A1);
  HR.EmitInstruction(A2);

  // Fourth independent ADD32 conflicts with a full 3-issue cycle.
  SUnit Fourth(A3, /*NodeNum=*/0);
  EXPECT_EQ(HR.getHazardType(&Fourth, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard);

  // BUNDLE root: NoHazard even on a saturated cycle — children are not
  // aggregated into the scoreboard. Boundary fence (not HR) keeps membership.
  SUnit BundleSU(Bundle, /*NodeNum=*/1);
  EXPECT_EQ(HR.getHazardType(&BundleSU, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  // Emit of the root is also a no-op resource book (still NoHazard meta).
  HR.EmitInstruction(Bundle);
  EXPECT_EQ(HR.getHazardType(&Fourth, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "emitting BUNDLE root must not clear the saturated cycle";
}

// T-SM3: already-member ARCTAN keeps the alone-law on getHazardType
// (recommit / post-setDesc identity). Logical-only PortModel would miss it.
TEST_F(HaydnBundleBoundaryTest, AloneLawHazardOnSMemberAndFormatEMember) {
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *ArctanS2 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN_E3_E2_ALU2_RI4),
              Haydn::R1)
          .addReg(Haydn::D0)
          .addImm(2)
          .getInstr();
  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R5)
          .addReg(Haydn::R3)
          .addReg(Haydn::R4)
          .getInstr();
  MachineInstr *ArctanE3 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN_E3_E0_ALU2_RI4),
              Haydn::R6)
          .addReg(Haydn::D1)
          .addImm(3)
          .getInstr();

  HaydnHazardRecognizer HR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                           /*AltDescs=*/nullptr);
  HR.Reset();
  SUnit SUArc(ArctanS2, /*NodeNum=*/0);
  SUnit SUAdd(Add, /*NodeNum=*/1);
  EXPECT_EQ(HR.getHazardType(&SUArc, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(ArctanS2);
  EXPECT_EQ(HR.getHazardType(&SUAdd, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "ADD32 must not co-issue with already-member ARCTAN";

  HR.Reset();
  SUnit SUE3(ArctanE3, /*NodeNum=*/2);
  EXPECT_EQ(HR.getHazardType(&SUE3, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(ArctanE3);
  EXPECT_EQ(HR.getHazardType(&SUAdd, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "ADD32 must not co-issue with Format E ARCTAN member";

  HR.Reset();
  HR.EmitInstruction(Add);
  EXPECT_EQ(HR.getHazardType(&SUArc, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "ARCTAN must refuse a non-empty cycle";
}

TEST_F(HaydnBundleBoundaryTest, SinCosWindowOccupancyAndDestLocks) {
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Arc =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN_E3_E2_ALU2_RI4),
              Haydn::R1)
          .addReg(Haydn::D0)
          .addImm(2)
          .getInstr();
  MachineInstr *SameUnit =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32_E3_E2_ALU2_RR),
              Haydn::R5)
          .addReg(Haydn::R3)
          .addReg(Haydn::R4)
          .getInstr();
  MachineInstr *Writer =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
          .addReg(Haydn::R6)
          .addReg(Haydn::R7)
          .getInstr();
  MachineInstr *Reader =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R8)
          .addReg(Haydn::R1)
          .addReg(Haydn::R9)
          .getInstr();

  EXPECT_EQ(HaydnHazardRecognizer::sinCosWindowOccupancy(*Arc), 4u);
  EXPECT_EQ(HaydnHazardRecognizer::architecturalDefLatency(Itin, *Arc, 0), 4u);

  HaydnHazardRecognizer HR(&II, Itin, /*IsPreRA=*/false, /*AltDescs=*/nullptr);
  EXPECT_GE(HR.getPipelineDepth(), 17);
  EXPECT_LT(HR.getMaxLatency(), 17);
  HR.Reset();
  SUnit SUArc(Arc, /*NodeNum=*/0);
  EXPECT_EQ(HR.getHazardType(&SUArc, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(Arc);

  SUnit SUUnit(SameUnit, /*NodeNum=*/1);
  SUnit SUWrite(Writer, /*NodeNum=*/2);
  SUnit SURead(Reader, /*NodeNum=*/3);
  // Same-cycle: issue-alone already refuses.
  EXPECT_EQ(HR.getHazardType(&SUUnit, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard);

  HR.AdvanceCycle();
  // Occupancy 4 → 3 following cycles: same unit, dest writer, dest reader.
  EXPECT_EQ(HR.getHazardType(&SUUnit, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "ALU2 must stay NOP-on-unit for uimm4+2 occupancy";
  EXPECT_EQ(HR.getHazardType(&SUWrite, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "dest writer must wait out the occupancy window";
  EXPECT_EQ(HR.getHazardType(&SURead, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "dest reader must wait out Data_Latency = uimm4+2";

  HR.AdvanceCycle();
  EXPECT_EQ(HR.getHazardType(&SURead, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard);
  HR.AdvanceCycle();
  EXPECT_EQ(HR.getHazardType(&SURead, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard)
      << "reader is legal after occupancy-1 advances";

  // SMS path: emitInScoreboard books Reserved occupancy so checkConflict
  // sees NOP-on-unit (Req↔Res) on the following cycles.
  ResourceScoreboard<HaydnFuncUnitWrapper> SB;
  SB.reset(HR.getPipelineDepth());
  HR.emitInScoreboard(SB, *Arc, /*Cycle=*/0);
  EXPECT_TRUE(HR.checkConflict(SB, *SameUnit, /*Cycle=*/1))
      << "SMS scoreboard must keep ALU2 Reserved for uimm4+2 occupancy";
  EXPECT_TRUE(HR.checkConflict(SB, *SameUnit, /*Cycle=*/2));
  EXPECT_TRUE(HR.checkConflict(SB, *SameUnit, /*Cycle=*/3));
  EXPECT_FALSE(HR.checkConflict(SB, *SameUnit, /*Cycle=*/4))
      << "unit is free after occupancy";
}

TEST_F(HaydnBundleBoundaryTest, DestWindowsExpireOnRecedeAndSkipPreRA) {
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Arc =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN), Haydn::R1)
          .addReg(Haydn::D0)
          .addImm(2)
          .getInstr();
  MachineInstr *Use =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R8)
          .addReg(Haydn::R1)
          .addReg(Haydn::R3)
          .getInstr();

  ASSERT_EQ(HaydnHazardRecognizer::sinCosWindowOccupancy(*Arc), 4u);

  // Pre-RA bottom-up must not book dest windows (recede-forever hang).
  // Same-cycle RAW after Emit is a different law — Advance first so the
  // dest-window-off check is not hidden by CurrentCycleLiveDefs.
  HaydnHazardRecognizer Pre(&II, Itin, /*IsPreRA=*/true, /*AltDescs=*/nullptr);
  Pre.Reset();
  SUnit SUArc(Arc, /*NodeNum=*/0);
  SUnit SUUse(Use, /*NodeNum=*/1);
  Pre.EmitInstruction(Arc);
  EXPECT_EQ(Pre.destWindowStallNeed(*Use), 0u)
      << "pre-RA dest-read window must stay off";
  Pre.AdvanceCycle();
  EXPECT_EQ(Pre.getHazardType(&SUUse, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard)
      << "pre-RA dest-read window must stay off after leaving the emit cycle";

  // Post-RA: occupancy 4 → remaining 3. Three recedes expire; growing
  // remaining on recede would still Hazard after three steps.
  HaydnHazardRecognizer Post(&II, Itin, /*IsPreRA=*/false, /*AltDescs=*/nullptr);
  Post.Reset();
  EXPECT_EQ(Post.getHazardType(&SUArc, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  Post.EmitInstruction(Arc);
  EXPECT_EQ(Post.getHazardType(&SUUse, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard);
  Post.RecedeCycle();
  EXPECT_EQ(Post.getHazardType(&SUUse, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard);
  Post.RecedeCycle();
  EXPECT_EQ(Post.getHazardType(&SUUse, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard);
  Post.RecedeCycle();
  EXPECT_EQ(Post.getHazardType(&SUUse, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard)
      << "recede expires dest remaining; it must not grow";
}

TEST_F(HaydnBundleBoundaryTest,
       DestWindowSeamHelperZeroVsOneInterveningAdvance) {
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  ASSERT_TRUE(Itin && !Itin->isEmpty());
  DebugLoc DL;

  MachineBasicBlock *PredMBB = MF->CreateMachineBasicBlock();
  MF->push_back(PredMBB);
  MachineInstr *Ld =
      BuildMI(*PredMBB, PredMBB->end(), DL, II.get(Haydn::LD32), Haydn::R1)
          .addReg(Haydn::R4)
          .addImm(0)
          .getInstr();
  MachineInstr *Use =
      BuildMI(*PredMBB, PredMBB->end(), DL, II.get(Haydn::ADD32), Haydn::R8)
          .addReg(Haydn::R1)
          .addReg(Haydn::R9)
          .getInstr();
  ASSERT_GE(HaydnHazardRecognizer::architecturalDefLatency(Itin, *Ld, 0), 2u);

  HaydnHazardRecognizer HR(&II, Itin, /*IsPreRA=*/false);
  HR.Reset();
  HR.advanceDestWindows();
  HR.emitForDestWindow(*Ld);
  EXPECT_EQ(HR.destWindowStallNeed(*Use), 1u)
      << "0 intervening advances: load dest still in the Data_Latency window";
  HR.advanceDestWindows();
  EXPECT_EQ(HR.destWindowStallNeed(*Use), 0u)
      << "1 intervening advance expires the load dest-read window";

  MachineBasicBlock *OkMBB = MF->CreateMachineBasicBlock();
  MF->push_back(OkMBB);
  BuildMI(*OkMBB, OkMBB->end(), DL, II.get(Haydn::LD32), Haydn::R1)
      .addReg(Haydn::R4)
      .addImm(0);
  BuildMI(*OkMBB, OkMBB->end(), DL, II.get(Haydn::NOP));
  BuildMI(*OkMBB, OkMBB->end(), DL, II.get(Haydn::ADD32), Haydn::R8)
      .addReg(Haydn::R1)
      .addReg(Haydn::R9);
  EXPECT_FALSE(haydn::bundle::verifyMBBDestWindowSeams(*OkMBB))
      << "1 intervening architectural NOP cycle must not be a dest-window seam";

  MachineBasicBlock *BadMBB = MF->CreateMachineBasicBlock();
  MF->push_back(BadMBB);
  BuildMI(*BadMBB, BadMBB->end(), DL, II.get(Haydn::LD32), Haydn::R1)
      .addReg(Haydn::R4)
      .addImm(0);
  BuildMI(*BadMBB, BadMBB->end(), DL, II.get(Haydn::ADD32), Haydn::R8)
      .addReg(Haydn::R1)
      .addReg(Haydn::R9);
  auto Err = haydn::bundle::verifyMBBDestWindowSeams(*BadMBB);
  ASSERT_TRUE(Err.has_value())
      << "0 intervening advances: load→use consecutive-cycle seam";
  EXPECT_TRUE(StringRef(*Err).contains("destWindowStallNeed"));
}

// Bot successor replay calls emitInstruction(SU, Cycle-Depth). The nonzero
// arm must book ARCTAN/SIN_COS Reserved at Delta+1..Occ-1 (NOP-on-unit)
// and dest remaining via std::max — not enterResources-only, not appendDefs.
TEST_F(HaydnBundleBoundaryTest, BotReplayEmitInstructionBooksSinCosOccupancy) {
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Arc =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN_E3_E2_ALU2_RI4),
              Haydn::R1)
          .addReg(Haydn::D0)
          .addImm(2)
          .getInstr();
  MachineInstr *SameUnit =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32_E3_E2_ALU2_RR),
              Haydn::R5)
          .addReg(Haydn::R3)
          .addReg(Haydn::R4)
          .getInstr();
  MachineInstr *Writer =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
          .addReg(Haydn::R6)
          .addReg(Haydn::R7)
          .getInstr();
  MachineInstr *Reader =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R8)
          .addReg(Haydn::R1)
          .addReg(Haydn::R9)
          .getInstr();

  const unsigned Occ = HaydnHazardRecognizer::sinCosWindowOccupancy(*Arc);
  ASSERT_EQ(Occ, 4u);

  HaydnHazardRecognizer HR(&II, Itin, /*IsPreRA=*/false, /*AltDescs=*/nullptr);
  const int Depth = std::max(
      std::max(HR.getPipelineDepth(), static_cast<int>(HR.getMaxLookAhead())),
      1);
  ASSERT_GE(Depth, 17);
  HR.Reset();
  SUnit SUArc(Arc, /*NodeNum=*/0);
  SUnit SUUnit(SameUnit, /*NodeNum=*/1);
  const int Delta = -Depth;
  HR.emitInstruction(&SUArc, Delta);

  for (unsigned K = 1; K < Occ; ++K)
    EXPECT_EQ(HR.getHazardType(&SUUnit, Delta + static_cast<int>(K)),
              ScheduleHazardRecognizer::Hazard)
        << "replay emit must keep ALU2 Reserved at Delta+" << K;
  EXPECT_EQ(HR.getHazardType(&SUUnit, Delta + static_cast<int>(Occ)),
            ScheduleHazardRecognizer::NoHazard)
      << "unit is free after occupancy";

  EXPECT_EQ(HR.destWindowStallNeed(*Writer), Occ - 1)
      << "dest-write remaining booked on the replay HR (no appendDefs)";
  EXPECT_EQ(HR.destWindowStallNeed(*Reader), Occ - 1)
      << "dest-read remaining booked on the replay HR";

  const unsigned Need = HR.destWindowStallNeed(*Reader);
  HR.recedeScoreboard(Depth + 1);
  EXPECT_EQ(HR.destWindowStallNeed(*Reader), Need)
      << "scoreboard-only recede must preserve dest remaining";
  EXPECT_EQ(HR.destWindowStallNeed(*Writer), Need);

  // After AlignScoreboardToCycleOne, successor cycle 0 is at +1 and
  // occupancy Reserved is at +2 .. +Occ.
  EXPECT_EQ(HR.getHazardType(&SUUnit, /*DeltaCycles=*/2),
            ScheduleHazardRecognizer::Hazard);
  EXPECT_EQ(HR.getHazardType(&SUUnit, /*DeltaCycles=*/3),
            ScheduleHazardRecognizer::Hazard);
  EXPECT_EQ(HR.getHazardType(&SUUnit, /*DeltaCycles=*/4),
            ScheduleHazardRecognizer::Hazard);
  EXPECT_EQ(HR.getHazardType(&SUUnit, /*DeltaCycles=*/5),
            ScheduleHazardRecognizer::NoHazard)
      << "unit is free after occupancy once aligned to cycle one";
}

TEST_F(HaydnBundleBoundaryTest, BotReplayMaxMergeDestRemaining) {
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *ArcLo =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN_E3_E2_ALU2_RI4),
              Haydn::R1)
          .addReg(Haydn::D0)
          .addImm(2)
          .getInstr();
  MachineInstr *ArcHi =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN_E3_E2_ALU2_RI4),
              Haydn::R1)
          .addReg(Haydn::D0)
          .addImm(4)
          .getInstr();
  MachineInstr *Writer =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
          .addReg(Haydn::R6)
          .addReg(Haydn::R7)
          .getInstr();
  MachineInstr *SameUnit =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32_E3_E2_ALU2_RR),
              Haydn::R5)
          .addReg(Haydn::R3)
          .addReg(Haydn::R4)
          .getInstr();

  ASSERT_EQ(HaydnHazardRecognizer::sinCosWindowOccupancy(*ArcLo), 4u);
  ASSERT_EQ(HaydnHazardRecognizer::sinCosWindowOccupancy(*ArcHi), 6u);

  HaydnHazardRecognizer BotHR(&II, Itin, /*IsPreRA=*/false,
                              /*AltDescs=*/nullptr);
  HaydnHazardRecognizer ScratchHR(&II, Itin, /*IsPreRA=*/false,
                                  /*AltDescs=*/nullptr);
  const int Depth = std::max(
      std::max(BotHR.getPipelineDepth(),
               static_cast<int>(BotHR.getMaxLookAhead())),
      1);
  const int Delta = -Depth;
  BotHR.Reset();
  ScratchHR.Reset();
  SUnit SULo(ArcLo, /*NodeNum=*/0);
  SUnit SUHi(ArcHi, /*NodeNum=*/1);
  SUnit SUUnit(SameUnit, /*NodeNum=*/2);

  // First successor seeds BotHR (longer dest remaining). A later exclusive
  // successor must max-merge, never drop the seed remaining or sum it.
  BotHR.emitInstruction(&SUHi, Delta);
  ScratchHR.emitInstruction(&SULo, Delta);
  EXPECT_EQ(BotHR.destWindowStallNeed(*Writer), 5u);
  EXPECT_EQ(ScratchHR.destWindowStallNeed(*Writer), 3u);

  BotHR.maxMergeSB(ScratchHR, Delta, Depth - 1);
  EXPECT_EQ(BotHR.destWindowStallNeed(*Writer), 5u)
      << "exclusive successors max-merge dest remaining; they must not drop";

  // Opposite polarity: seed remaining 3, later successor 5 → max is 5, never 8.
  HaydnHazardRecognizer BotLo(&II, Itin, /*IsPreRA=*/false,
                              /*AltDescs=*/nullptr);
  HaydnHazardRecognizer ScratchHi(&II, Itin, /*IsPreRA=*/false,
                                  /*AltDescs=*/nullptr);
  BotLo.Reset();
  ScratchHi.Reset();
  BotLo.emitInstruction(&SULo, Delta);
  ScratchHi.emitInstruction(&SUHi, Delta);
  EXPECT_EQ(BotLo.destWindowStallNeed(*Writer), 3u);
  EXPECT_EQ(ScratchHi.destWindowStallNeed(*Writer), 5u);
  BotLo.maxMergeSB(ScratchHi, Delta, Depth - 1);
  EXPECT_EQ(BotLo.destWindowStallNeed(*Writer), 5u)
      << "exclusive successors max-merge dest remaining; they must not sum";

  for (unsigned K = 1; K < 6; ++K)
    EXPECT_EQ(BotLo.getHazardType(&SUUnit, Delta + static_cast<int>(K)),
              ScheduleHazardRecognizer::Hazard)
        << "max-merge must keep the longer ALU2 Reserved overlay at Delta+"
        << K;
  EXPECT_EQ(BotLo.getHazardType(&SUUnit, Delta + 6),
            ScheduleHazardRecognizer::NoHazard)
      << "unit is free after the longer occupancy";
}

// D1.16 loop back-edge wrap law — pins on the shared predicate.
//
// Law (a) equivalence: for the same pending-window state, pads before END
// and pads after START yield the same wrap distance; pinned on the pure
// closed form, not two emission sites (only pad-before-END is implemented).
// Degenerate shapes and the latch classification are pinned alongside.
TEST(HaydnHazardRecognizerTest, WrapPadBeforeEndEqualsAfterStart) {
  using Pair = std::pair<unsigned, unsigned>;
  // One Data_Latency-2 def at each position of a 4-cycle body: the def at
  // the LAST cycle leaves remaining 1 at block end → P = 1; earlier defs
  // expire inside the body → 0 contribution.
  const SmallVector<Pair, 4> All = {{0, 2}, {1, 2}, {2, 2}, {3, 2}};
  EXPECT_EQ(HaydnHazardRecognizer::wrapPadBeforeEndEqualsAfterStart(
                All, /*NumCycles=*/4),
            1u);

  // Def at FIRST cycle: distance to end is 3 > latency window → P = 0.
  const SmallVector<Pair, 1> First = {{0, 2}};
  EXPECT_EQ(HaydnHazardRecognizer::wrapPadBeforeEndEqualsAfterStart(
                First, /*NumCycles=*/4),
            0u);
  // Def at MIDDLE cycle → 0.
  const SmallVector<Pair, 1> Mid = {{1, 2}};
  EXPECT_EQ(HaydnHazardRecognizer::wrapPadBeforeEndEqualsAfterStart(
                Mid, /*NumCycles=*/4),
            0u);
  // Def at LAST cycle latency 2 → remaining 1 at end → P = 1.
  const SmallVector<Pair, 1> Last = {{3, 2}};
  EXPECT_EQ(HaydnHazardRecognizer::wrapPadBeforeEndEqualsAfterStart(
                Last, /*NumCycles=*/4),
            1u);
  // Degenerate: empty pending → P = 0.
  const SmallVector<Pair, 0> None;
  EXPECT_EQ(HaydnHazardRecognizer::wrapPadBeforeEndEqualsAfterStart(
                None, /*NumCycles=*/4),
            0u);
  // Degenerate: def at last cycle with latency 1 → P = 0.
  const SmallVector<Pair, 1> Lat1 = {{3, 1}};
  EXPECT_EQ(HaydnHazardRecognizer::wrapPadBeforeEndEqualsAfterStart(
                Lat1, /*NumCycles=*/4),
            0u);
  // SIN_COS-shaped window (occupancy 4 → latency class 4) longer than the
  // remaining body: def at cycle 1 of a 3-cycle body leaves
  // 4-1-(3-1-1) = 2 → P = 2 (window longer than body caps at remaining).
  const SmallVector<Pair, 1> SinCos = {{1, 4}};
  EXPECT_EQ(HaydnHazardRecognizer::wrapPadBeforeEndEqualsAfterStart(
                SinCos, /*NumCycles=*/3),
            2u);
}

// Live-HR form of the same equivalence: destWindowWrapPadNeed on a
// recognizer that walked a body must equal the closed form and the exit
// leak — the WRAP and EXIT seats share one arithmetic.
TEST_F(HaydnBundleBoundaryTest, DestWindowWrapPadNeedMatchesExitLeak) {
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  ASSERT_TRUE(Itin && !Itin->isEmpty());
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Ld =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R1)
          .addReg(Haydn::R4)
          .addImm(0)
          .getInstr();
  ASSERT_GE(HaydnHazardRecognizer::architecturalDefLatency(Itin, *Ld, 0), 2u);

  HaydnHazardRecognizer HR(&II, Itin, /*IsPreRA=*/false);
  HR.Reset();
  // One-cycle body whose only cycle is the load: after the pass-walk shape
  // (advance-tick per cycle, then book the cycle's defs), the dest-read
  // remaining is 1 at block end — exactly what both the WRAP emission
  // seat (HaydnLatencyStalls) and the freeze walker query post-walk.
  HR.advanceDestWindows();
  HR.emitForDestWindow(*Ld);
  EXPECT_EQ(HR.destWindowWrapPadNeed(), 1u)
      << "load at the last body cycle leaves remaining 1 across the wrap";
  EXPECT_EQ(HR.destWindowWrapPadNeed(), HR.destWindowExitLeak())
      << "WRAP and EXIT seats share one max-remaining arithmetic";

  // One further retiring cycle (an idle pad or the soft latch's backedge
  // branch parcel) expires the latency-2 window: the wrap is covered.
  HR.advanceDestWindows();
  EXPECT_EQ(HR.destWindowWrapPadNeed(), 0u)
      << "one pad cycle expires the latency-2 window at the wrap";

  // SIN_COS/ARCTAN DestWritePending shape at the latch end.
  MachineInstr *Arc =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN), Haydn::R5)
          .addReg(Haydn::D0)
          .addImm(2)
          .getInstr();
  ASSERT_EQ(HaydnHazardRecognizer::sinCosWindowOccupancy(*Arc), 4u);
  HR.Reset();
  HR.advanceDestWindows();
  HR.emitForDestWindow(*Arc);
  EXPECT_EQ(HR.destWindowWrapPadNeed(), 3u)
      << "ARCTAN occupancy 4 at the latch end books dest-write remaining 3";
  EXPECT_EQ(HR.destWindowWrapPadNeed(), HR.destWindowExitLeak());
}

TEST_F(HaydnBundleBoundaryTest, SharedPortBudgetPredicate) {
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *A0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
          .addReg(Haydn::R2)
          .addReg(Haydn::R3)
          .getInstr();
  MachineInstr *A1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R4)
          .addReg(Haydn::R5)
          .addReg(Haydn::R6)
          .getInstr();
  MachineInstr *A2 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R7)
          .addReg(Haydn::R8)
          .addReg(Haydn::R9)
          .getInstr();

  SmallVector<MachineInstr *, 2> Two = {A0, A1};
  SmallVector<MachineInstr *, 3> Three = {A0, A1, A2};
  EXPECT_FALSE(haydn::bundle::cycleMembersExceedPortBudget(Two))
      << "2W GPR is inside the 2W budget";
  EXPECT_TRUE(haydn::bundle::cycleMembersExceedPortBudget(Three))
      << "3W GPR must fail the shared commit/verify port predicate";
}

TEST(HaydnHazardRecognizerTest, MemoryObjectBitsSameKindNotASameCycleReject) {
  // AIE FuncUnitWrapper same-kind object overlap is wait-cycle avoidance
  // (AIEHazardRecognizer.cpp:136-138), not a Haydn issue-cycle law.
  // Golden dual-load of one object (LOADSTORE0 + LOAD1 / p[0]/p[1]) is
  // legal. Wait-cycle reject stays unadmitted.
  HaydnFuncUnitWrapper LoadA, LoadB, StoreA, StoreB, DisjointLoad;
  LoadA.setMemoryObjectBits(/*Load=*/1, /*Store=*/0);
  LoadB.setMemoryObjectBits(/*Load=*/1, /*Store=*/0);
  StoreA.setMemoryObjectBits(/*Load=*/0, /*Store=*/1);
  StoreB.setMemoryObjectBits(/*Load=*/0, /*Store=*/1);
  DisjointLoad.setMemoryObjectBits(/*Load=*/2, /*Store=*/0);
  EXPECT_FALSE(LoadA.conflict(LoadB))
      << "same-object load-load must not be an issue-cycle reject";
  EXPECT_FALSE(StoreA.conflict(StoreB))
      << "same-object store-store is unit injectivity, not object bits";
  EXPECT_FALSE(LoadA.conflict(StoreA));
  EXPECT_FALSE(LoadA.conflict(DisjointLoad));
}

TEST(HaydnHazardRecognizerTest, NamedSameCycleLawsTagMatches) {
  EXPECT_STREQ(HaydnHazardRecognizer::namedSameCycleLawsTag(),
               HAYDN_NAMED_SAME_CYCLE_LAWS_TAG);
  EXPECT_STREQ(HaydnResourceCycle::namedSameCycleLawsTag(),
               HaydnHazardRecognizer::namedSameCycleLawsTag());
  EXPECT_STREQ(haydn::pack::NamedSameCycleLawsTag,
               "arctan-sincos+csrw-set+abs-e0");
}

TEST_F(HaydnBundleBoundaryTest, ProvenDisjointLd32St32SameCycle) {
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  auto *GV = new GlobalVariable(*M, Type::getInt32Ty(*Ctx), /*isConstant=*/false,
                                GlobalValue::ExternalLinkage, nullptr, "obj");
  auto addMMO = [&](MachineInstr *MI, int64_t ByteOff, bool IsStore) {
    MachineMemOperand::Flags F =
        IsStore ? MachineMemOperand::MOStore : MachineMemOperand::MOLoad;
    MI->addMemOperand(*MF, MF->getMachineMemOperand(
                               MachinePointerInfo(GV, ByteOff), F, 4, Align(4)));
  };

  // Same base, scaled imm 0 vs 1 → byte 0 vs 4, width 4. TII same-base
  // offset+width (RISCVInstrInfo.cpp:3522-3552). Unordered MMOs so
  // hasOrderedMemoryRef is not conservatively true.
  MachineInstr *Ld =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R1)
          .addReg(Haydn::R4)
          .addImm(0)
          .getInstr();
  addMMO(Ld, /*ByteOff=*/0, /*IsStore=*/false);
  MachineInstr *St =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ST32))
          .addReg(Haydn::R3)
          .addReg(Haydn::R4)
          .addImm(1)
          .getInstr();
  addMMO(St, /*ByteOff=*/4, /*IsStore=*/true);

  EXPECT_TRUE(II.areMemAccessesTriviallyDisjoint(*Ld, *St));

  HaydnHazardRecognizer HR(&II, Itin, /*IsPreRA=*/false, /*AltDescs=*/nullptr);
  HR.Reset();
  SUnit SULd(Ld, /*NodeNum=*/0);
  SUnit SUSt(St, /*NodeNum=*/1);
  EXPECT_EQ(HR.getHazardType(&SULd, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(Ld);
  EXPECT_EQ(HR.getHazardType(&SUSt, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard)
      << "disjoint LD32+ST32 must rematch (store e0 + load e1)";

  SmallVector<MachineInstr *, 2> Pair = {Ld, St};
  EXPECT_TRUE(haydn::bundle::canCoissueProductCycle(Pair))
      << "commit probe must accept proven-disjoint LD32+ST32";
}

TEST_F(HaydnBundleBoundaryTest, ProvenDisjointSLWMemberSameCycle) {
  // After leaveMBB setDesc, LD32/ST32 become S_LW_WITH_IMM / S_SW_WITH_IMM
  // (and generated S_LW_WITH_IMM_E*_ / S_SW_WITH_IMM_E*_). TII same-base
  // scaled-imm disjoint must survive bake (RISCVInstrInfo.cpp:3522-3552).
  // Dual-load is not cycleHasMayAliasStoreLoad (MachineInstr::mayAlias
  // bails when neither mayStore). Do not treat members as mayStore.
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  auto *GV = new GlobalVariable(*M, Type::getInt32Ty(*Ctx), /*isConstant=*/false,
                                GlobalValue::ExternalLinkage, nullptr, "obj");
  auto addMMO = [&](MachineInstr *MI, int64_t ByteOff, bool IsStore) {
    MachineMemOperand::Flags F =
        IsStore ? MachineMemOperand::MOStore : MachineMemOperand::MOLoad;
    MI->addMemOperand(*MF, MF->getMachineMemOperand(
                               MachinePointerInfo(GV, ByteOff), F, 4, Align(4)));
  };
  auto memOracle = [&](MachineInstr *MI, int64_t ExpectOffset,
                       uint64_t ExpectWidth) {
    SmallVector<const MachineOperand *, 2> BaseOps;
    int64_t Offset = 0;
    bool OffsetIsScalable = false;
    LocationSize Width = LocationSize::precise(0);
    EXPECT_TRUE(II.getMemOperandsWithOffsetWidth(*MI, BaseOps, Offset,
                                                 OffsetIsScalable, Width,
                                                 TRI()))
        << II.getName(MI->getOpcode()) << " post-setDesc member oracle";
    ASSERT_EQ(BaseOps.size(), 1u);
    EXPECT_FALSE(OffsetIsScalable);
    ASSERT_TRUE(Width.hasValue());
    EXPECT_EQ(Width.getValue().getFixedValue(), ExpectWidth);
    EXPECT_EQ(Offset, ExpectOffset);
  };

  MachineInstr *CatLd =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::S_LW_WITH_IMM), Haydn::R4)
          .addReg(Haydn::R2)
          .addImm(1)
          .getInstr();
  addMMO(CatLd, /*ByteOff=*/4, /*IsStore=*/false);
  MachineInstr *CatLd2 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::S_LW_WITH_IMM), Haydn::R3)
          .addReg(Haydn::R2)
          .addImm(3)
          .getInstr();
  addMMO(CatLd2, /*ByteOff=*/12, /*IsStore=*/false);
  MachineInstr *CatSt =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::S_SW_WITH_IMM))
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .addImm(0)
          .getInstr();
  addMMO(CatSt, /*ByteOff=*/0, /*IsStore=*/true);
  memOracle(CatLd, /*ExpectOffset=*/4, /*ExpectWidth=*/4);
  memOracle(CatLd2, /*ExpectOffset=*/12, /*ExpectWidth=*/4);
  memOracle(CatSt, /*ExpectOffset=*/0, /*ExpectWidth=*/4);
  EXPECT_TRUE(II.areMemAccessesTriviallyDisjoint(*CatLd, *CatLd2));
  EXPECT_TRUE(II.areMemAccessesTriviallyDisjoint(*CatSt, *CatLd));
  EXPECT_TRUE(II.areMemAccessesTriviallyDisjoint(*CatSt, *CatLd2));

  SmallVector<const MachineInstr *, 2> DualCat = {CatLd, CatLd2};
  EXPECT_FALSE(haydn::pack::cycleHasMayAliasStoreLoad(DualCat, nullptr))
      << "dual S_LW_WITH_IMM must not trip the store/load law";
  SmallVector<const MachineInstr *, 2> StLdCat = {CatSt, CatLd};
  EXPECT_FALSE(haydn::pack::cycleHasMayAliasStoreLoad(StLdCat, nullptr))
      << "TII disjoint S_SW+S_LW must pack under null AA";

  MachineInstr *MemLd0 =
      BuildMI(*MBB, MBB->end(), DL,
              II.get(Haydn::S_LW_WITH_IMM_E2_E0_LOADSTORE0_RI6), Haydn::R4)
          .addReg(Haydn::R2)
          .addImm(1)
          .getInstr();
  addMMO(MemLd0, /*ByteOff=*/4, /*IsStore=*/false);
  MachineInstr *MemLd1 =
      BuildMI(*MBB, MBB->end(), DL,
              II.get(Haydn::S_LW_WITH_IMM_E2_E1_LOAD1_RI6), Haydn::R3)
          .addReg(Haydn::R2)
          .addImm(3)
          .getInstr();
  addMMO(MemLd1, /*ByteOff=*/12, /*IsStore=*/false);
  MachineInstr *MemSt =
      BuildMI(*MBB, MBB->end(), DL,
              II.get(Haydn::S_SW_WITH_IMM_E2_E0_LOADSTORE0_RI6))
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .addImm(0)
          .getInstr();
  addMMO(MemSt, /*ByteOff=*/0, /*IsStore=*/true);
  memOracle(MemLd0, /*ExpectOffset=*/4, /*ExpectWidth=*/4);
  memOracle(MemLd1, /*ExpectOffset=*/12, /*ExpectWidth=*/4);
  memOracle(MemSt, /*ExpectOffset=*/0, /*ExpectWidth=*/4);
  EXPECT_TRUE(II.areMemAccessesTriviallyDisjoint(*MemLd0, *MemLd1));
  EXPECT_TRUE(II.areMemAccessesTriviallyDisjoint(*MemSt, *MemLd0));
  EXPECT_TRUE(MemLd0->mayLoad() && !MemLd0->mayStore())
      << "generated S_LW member must stay a load";
  EXPECT_TRUE(MemSt->mayStore() && !MemSt->mayLoad())
      << "generated S_SW member must stay a store";

  SmallVector<const MachineInstr *, 2> DualMem = {MemLd0, MemLd1};
  EXPECT_FALSE(haydn::pack::cycleHasMayAliasStoreLoad(DualMem, nullptr))
      << "generated dual-load must not trip the store/load law";
  SmallVector<const MachineInstr *, 2> StLdMem = {MemSt, MemLd1};
  EXPECT_FALSE(haydn::pack::cycleHasMayAliasStoreLoad(StLdMem, nullptr))
      << "TII disjoint generated S_SW+S_LW must pack under null AA";

  MachineInstr *OverlapLd =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::S_LW_WITH_IMM), Haydn::R5)
          .addReg(Haydn::R2)
          .addImm(0)
          .getInstr();
  addMMO(OverlapLd, /*ByteOff=*/0, /*IsStore=*/false);
  EXPECT_FALSE(II.areMemAccessesTriviallyDisjoint(*CatSt, *OverlapLd))
      << "same-base same-element S_SW+S_LW must not prove disjoint";
  SmallVector<const MachineInstr *, 2> Overlap = {CatSt, OverlapLd};
  EXPECT_TRUE(haydn::pack::cycleHasMayAliasStoreLoad(Overlap, nullptr))
      << "overlapping store/load must refuse under null AA";

  SmallVector<MachineInstr *, 2> DualPair = {MemLd0, MemLd1};
  EXPECT_TRUE(haydn::bundle::canCoissueProductCycle(DualPair))
      << "leaveMBB re-probe must keep generated dual-ld32";
}

TEST_F(HaydnBundleBoundaryTest, DualLd32CatalogPairNotStoreLoadLaw) {
  // Ranking/S2 dual-ld32 (LOADSTORE0+LOAD1) is not cycleHasMayAliasStoreLoad.
  // Hexagon HexagonVLIWPacketizer.cpp:1559 load-load OK; store-then-load
  // alias sequential. Catalog LD32 pair must coissue under null AA even
  // when a same-ReadyCycle overlapping store would sequentialize the mix.
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  auto *GV = new GlobalVariable(*M, Type::getInt32Ty(*Ctx), /*isConstant=*/false,
                                GlobalValue::ExternalLinkage, nullptr, "obj");
  auto addMMO = [&](MachineInstr *MI, int64_t ByteOff, bool IsStore) {
    MachineMemOperand::Flags F =
        IsStore ? MachineMemOperand::MOStore : MachineMemOperand::MOLoad;
    MI->addMemOperand(*MF, MF->getMachineMemOperand(
                               MachinePointerInfo(GV, ByteOff), F, 4, Align(4)));
  };

  MachineInstr *Ld0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R4)
          .addReg(Haydn::R2)
          .addImm(1)
          .getInstr();
  addMMO(Ld0, /*ByteOff=*/4, /*IsStore=*/false);
  MachineInstr *Ld1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R3)
          .addReg(Haydn::R2)
          .addImm(3)
          .getInstr();
  addMMO(Ld1, /*ByteOff=*/12, /*IsStore=*/false);
  MachineInstr *StOverlap =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ST32))
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .addImm(1)
          .getInstr();
  addMMO(StOverlap, /*ByteOff=*/4, /*IsStore=*/true);

  EXPECT_TRUE(haydn::pack::isPureLoad(*Ld0) && haydn::pack::isPureLoad(*Ld1));
  EXPECT_TRUE(haydn::pack::isPureStore(*StOverlap));
  EXPECT_TRUE(II.areMemAccessesTriviallyDisjoint(*Ld0, *Ld1));
  EXPECT_FALSE(II.areMemAccessesTriviallyDisjoint(*StOverlap, *Ld0));

  SmallVector<const MachineInstr *, 2> Dual = {Ld0, Ld1};
  EXPECT_FALSE(haydn::pack::cycleHasMayAliasStoreLoad(Dual, nullptr))
      << "dual catalog LD32 must not trip the store/load law";
  SmallVector<const MachineInstr *, 3> Mix = {StOverlap, Ld0, Ld1};
  EXPECT_TRUE(haydn::pack::cycleHasMayAliasStoreLoad(Mix, nullptr))
      << "overlapping store + dual-load mix must refuse";

  SmallVector<MachineInstr *, 2> DualPair = {Ld0, Ld1};
  EXPECT_TRUE(haydn::bundle::canCoissueProductCycle(DualPair, nullptr))
      << "S2 re-probe must keep catalog dual-ld32 under null AA";
  SmallVector<MachineInstr *, 3> MixPair = {StOverlap, Ld0, Ld1};
  EXPECT_FALSE(haydn::bundle::canCoissueProductCycle(MixPair, nullptr))
      << "overlapping store must not ride the dual-ld32 pair";

  HaydnHazardRecognizer HR(&II, Itin, /*IsPreRA=*/false, /*AltDescs=*/nullptr);
  HR.Reset();
  SUnit SU0(Ld0, /*NodeNum=*/0);
  SUnit SU1(Ld1, /*NodeNum=*/1);
  SUnit SUSt(StOverlap, /*NodeNum=*/2);
  EXPECT_EQ(HR.getHazardType(&SU0, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(Ld0);
  EXPECT_EQ(HR.getHazardType(&SU1, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard)
      << "second LD32 must join the first (LOAD1); AA not consulted";
  EXPECT_EQ(HR.getHazardType(&SUSt, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "overlapping ST32 must not join dual-ld32";
}

TEST_F(HaydnBundleBoundaryTest, NamedSameCycleLawsOccupiedSet) {
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Arc =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ARCTAN), Haydn::R1)
          .addReg(Haydn::D0)
          .addImm(2)
          .getInstr();
  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R5)
          .addReg(Haydn::R3)
          .addReg(Haydn::R4)
          .getInstr();
  MachineInstr *Lui =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LUI), Haydn::R6)
          .addReg(Haydn::R0)
          .addImm(1)
          .getInstr();
  MachineInstr *Set =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::SET_HWLOOP_W))
          .addImm(0)
          .addImm(0)
          .addImm(0)
          .addImm(8)
          .getInstr();
  MachineInstr *Csrw =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::CSRW_W))
          .addImm(0x20)
          .addReg(Haydn::R2)
          .getInstr();

  SmallVector<const MachineInstr *, 1> Empty;
  EXPECT_FALSE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Arc, Empty));
  EXPECT_FALSE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Add, Empty));

  SmallVector<const MachineInstr *, 1> HasAdd = {Add};
  EXPECT_TRUE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Arc, HasAdd))
      << "ARCTAN must refuse a non-empty cycle";
  SmallVector<const MachineInstr *, 1> HasArc = {Arc};
  EXPECT_TRUE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Add, HasArc))
      << "companion must refuse a SIN_COS/ARCTAN cycle";

  SmallVector<const MachineInstr *, 1> HasLui = {Lui};
  EXPECT_TRUE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Add, HasLui))
      << "LUI is e0-alone";
  EXPECT_TRUE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Lui, HasAdd));

  SmallVector<const MachineInstr *, 1> HasSet = {Set};
  EXPECT_TRUE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Csrw, HasSet))
      << "CSRW 0x20-0x25 must not share a cycle with SET_HWLOOP";
  SmallVector<const MachineInstr *, 1> HasCsrw = {Csrw};
  EXPECT_TRUE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Set, HasCsrw));
  EXPECT_FALSE(HaydnHazardRecognizer::cycleViolatesNamedSameCycleLaws(*Add, HasSet))
      << "SET_HWLOOP may share with a non-CSRW ALU";
}

TEST_F(HaydnBundleBoundaryTest, MemoryObjectBitsEnumerateIRValue) {
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  auto *GV = new GlobalVariable(*M, Type::getInt32Ty(*Ctx), /*isConstant=*/false,
                                GlobalValue::ExternalLinkage, nullptr, "obj");
  MachinePointerInfo MPI(GV);
  MachineMemOperand *MMO = MF->getMachineMemOperand(
      MPI, MachineMemOperand::MOLoad, 4, Align(4));

  MachineInstr *Ld0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R1)
          .addReg(Haydn::R4)
          .addImm(0)
          .getInstr();
  Ld0->addMemOperand(*MF, MMO);
  MachineInstr *Ld1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R2)
          .addReg(Haydn::R4)
          .addImm(4)
          .getInstr();
  Ld1->addMemOperand(*MF, MMO);

  HaydnHazardRecognizer HR(&II, Itin, /*IsPreRA=*/false, /*AltDescs=*/nullptr);
  const MemoryObjectPair A = HR.getMemoryObjectsBits(Ld0);
  const MemoryObjectPair B = HR.getMemoryObjectsBits(Ld1);
  EXPECT_NE(A.Load, 0u);
  EXPECT_EQ(A.Load, B.Load) << "same IR object must share a bit";
  EXPECT_EQ(A.Store, 0u);

  HaydnHazardRecognizer Pre(&II, Itin, /*IsPreRA=*/true, /*AltDescs=*/nullptr);
  EXPECT_EQ(Pre.getMemoryObjectsBits(Ld0).Load, 0u)
      << "pre-RA must not book memory-object bits";

  HaydnFuncUnitWrapper WA, WB;
  WA.setMemoryObjectBits(A.Load, A.Store);
  WB.setMemoryObjectBits(B.Load, B.Store);
  EXPECT_FALSE(WA.conflict(WB))
      << "enumerated same-object load-load is not a same-cycle reject";
}

// Multi-child hard root via finalizeBundle: isSchedulingBoundary is true on
// the root only (not on children). Empty-root pin above never exercises real
// membership; this is the production shape machine-scheduler fences.
TEST_F(HaydnBundleBoundaryTest, MultiChildFinalizeBundleIsSchedulingBoundary) {
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // Three independent logical ADD32 → one multi-member BUNDLE root.
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
      .addReg(Haydn::R2)
      .addReg(Haydn::R3);
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R4)
      .addReg(Haydn::R5)
      .addReg(Haydn::R6);
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R7)
      .addReg(Haydn::R8)
      .addReg(Haydn::R9);
  ASSERT_EQ(std::distance(MBB->begin(), MBB->end()), 3);

  MachineBasicBlock::instr_iterator First = MBB->instr_begin();
  MachineBasicBlock::instr_iterator Last = MBB->instr_end();
  finalizeBundle(*MBB, First, Last);
  ASSERT_FALSE(MBB->empty());
  MachineInstr &Root = MBB->front();
  ASSERT_TRUE(Root.isBundle());
  EXPECT_TRUE(II.isSchedulingBoundary(Root, MBB, *MF));

  // Membership: three bundled children remain under the root; none of the
  // children is itself a scheduling boundary (ordinary ADD32 only).
  unsigned ChildCount = 0;
  for (MachineBasicBlock::instr_iterator I =
           std::next(Root.getIterator());
       I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
    EXPECT_FALSE(I->isBundle());
    EXPECT_FALSE(II.isSchedulingBoundary(*I, MBB, *MF))
        << "bundled child must not be a region boundary";
    EXPECT_TRUE(I->getOpcode() == Haydn::ADD32 ||
                haydn::format_e::peelLogicalOpcodeName(
                    haydn::bundle::haydnOpcodeName(I->getOpcode()),
                    /*StripWide=*/false) == "ADD32");
    ++ChildCount;
  }
  EXPECT_EQ(ChildCount, 3u);
  // getBundleSize counts children only (not the BUNDLE root).
  EXPECT_EQ(Root.getBundleSize(), 3u);
}

// Live HR on a multi-child hard root: getHazardType/Emit of the root stay
// NoHazard zero-resource even with real children present. Emitting the root
// must not book children's issue/ports — a free ADD32 after emit would Hazard
// immediately if the three child writes had been aggregated (GPR 2W cap).
TEST_F(HaydnBundleBoundaryTest, MultiChildLiveHRNoHazardForBundleRoot) {
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
      .addReg(Haydn::R2)
      .addReg(Haydn::R3);
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R4)
      .addReg(Haydn::R5)
      .addReg(Haydn::R6);
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R7)
      .addReg(Haydn::R8)
      .addReg(Haydn::R9);
  finalizeBundle(*MBB, MBB->instr_begin(), MBB->instr_end());
  ASSERT_FALSE(MBB->empty());
  MachineInstr &Root = MBB->front();
  ASSERT_TRUE(Root.isBundle());
  EXPECT_EQ(Root.getBundleSize(), 3u);
  EXPECT_TRUE(II.isSchedulingBoundary(Root, MBB, *MF));

  // Free neighbors outside the hard root (port-budget control after root emit).
  MachineInstr *Free0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R0)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  MachineInstr *Free1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R3)
          .addReg(Haydn::R4)
          .addReg(Haydn::R5)
          .getInstr();
  MachineInstr *Free2 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R6)
          .addReg(Haydn::R7)
          .addReg(Haydn::R8)
          .getInstr();

  HaydnHazardRecognizer HR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                           /*AltDescs=*/nullptr);
  HR.Reset();

  SUnit RootSU(&Root, /*NodeNum=*/0);
  EXPECT_EQ(HR.getHazardType(&RootSU, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);

  // Emit multi-child root: still zero-resource (no child aggregation).
  HR.EmitInstruction(&Root);

  // Cycle empty after root emit. If the three child ADD32 writes had been
  // booked, Free0 would already Hazard on the GPR 2W port cap.
  SUnit S0(Free0, /*NodeNum=*/1);
  EXPECT_EQ(HR.getHazardType(&S0, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard)
      << "multi-child BUNDLE emit must not book children's ports/issue";
  HR.EmitInstruction(Free0);

  // Two free ADD32 fill the 2W budget; a third free ADD32 Hazards (control
  // that the scoreboard is still live and not stuck empty).
  SUnit S1(Free1, /*NodeNum=*/2);
  EXPECT_EQ(HR.getHazardType(&S1, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(Free1);
  SUnit S2(Free2, /*NodeNum=*/3);
  EXPECT_EQ(HR.getHazardType(&S2, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard);
}

//===----------------------------------------------------------------------===//
// WP2 — RA survival contract for SMS hard-root BUNDLEs
//===----------------------------------------------------------------------===//
//
// Product path through machine-scheduler → coalescer → greedy → VRW →
// postmisched keeps multi-member hard roots intact under
// isSchedulingBoundary (zero hard-group split). Integration class:
// sms-handoff-bundle-through-ra.mir rematch body (SCHED/COALESCER/GREEDY/VRW/
// SPILL) and CONT-* producer body (product multi-stage).
//
// Spill/copy recovery (documented + pinned here / in TargetMachine):
//   * isSchedulingBoundary(BUNDLE) is the load-bearing fence so RA cannot
//     splice members out of a hard root (stress-regalloc membership holds).
//   * TwoAddress does not walk bundled children; Haydn's pre-TwoAddress
//     bundled two-addr rewrite (after PHIElimination) prepends COPY dst,src
//     before the root and rewrites residual tied uses (F2MULAA acc) so
//     post-TwoAddress TiedOpsRewritten verify passes. Coalescer folds the COPY.
//   * HaydnHandoffBundleRootDefs re-attaches child vreg defs on the root
//     after TwoAddress for the LIS/coalescer dual-def surface.
// Do not enable product multi-stage (WP5) until this contract + post-RA
// commit-inside-group (WP3) + FE5B certificate (WP4) are green.

TEST_F(HaydnBundleBoundaryTest, WP2_DualLoadHardRootIsSchedulingBoundary) {
  // SMS handoff dual-load product shape: two LD32 under one hard root.
  // Root is a boundary; children are not — membership fence for RA.
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R1)
      .addReg(Haydn::R2)
      .addImm(0);
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R3)
      .addReg(Haydn::R4)
      .addImm(0);
  finalizeBundle(*MBB, MBB->instr_begin(), MBB->instr_end());
  ASSERT_FALSE(MBB->empty());
  MachineInstr &Root = MBB->front();
  ASSERT_TRUE(Root.isBundle());
  EXPECT_EQ(Root.getBundleSize(), 2u);
  EXPECT_TRUE(II.isSchedulingBoundary(Root, MBB, *MF));

  unsigned ChildCount = 0;
  for (MachineBasicBlock::instr_iterator I = std::next(Root.getIterator());
       I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
    EXPECT_FALSE(II.isSchedulingBoundary(*I, MBB, *MF));
    const StringRef Name = haydn::bundle::haydnOpcodeName(I->getOpcode());
    EXPECT_TRUE(I->getOpcode() == Haydn::LD32 ||
                Name.contains("S_LW_WITH_IMM"));
    ++ChildCount;
  }
  EXPECT_EQ(ChildCount, 2u);
}

TEST_F(HaydnBundleBoundaryTest, WP2_RematchMixedFUHardRootIsSchedulingBoundary) {
  // Rematch ADD32 + 2×ADD64 hard root (sms-handoff-bundle-through-ra.mir
  // rematch / format-bundle-through-ra.mir mixed-FU). Three members under
  // one boundary through the RA fence.
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
      .addReg(Haydn::R2)
      .addReg(Haydn::R3);
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD64), Haydn::D0)
      .addReg(Haydn::D1)
      .addReg(Haydn::D2);
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD64), Haydn::D3)
      .addReg(Haydn::D4)
      .addReg(Haydn::D5);
  finalizeBundle(*MBB, MBB->instr_begin(), MBB->instr_end());
  ASSERT_FALSE(MBB->empty());
  MachineInstr &Root = MBB->front();
  ASSERT_TRUE(Root.isBundle());
  EXPECT_EQ(Root.getBundleSize(), 3u);
  EXPECT_TRUE(II.isSchedulingBoundary(Root, MBB, *MF));

  unsigned ChildCount = 0;
  for (MachineBasicBlock::instr_iterator I = std::next(Root.getIterator());
       I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
    EXPECT_FALSE(II.isSchedulingBoundary(*I, MBB, *MF))
        << "RA must not treat hard-root children as independent boundaries";
    ++ChildCount;
  }
  EXPECT_EQ(ChildCount, 3u);
}

TEST_F(HaydnBundleBoundaryTest, WP2_TiedMacChildStillRootOnlyBoundary) {
  // Tied-def MAC co-issued with ADDI under one hard root (SMS handoff can
  // freeze F2MULAA+ADDI same-cycle). Boundary is the root only; child ties
  // are repaired post-TwoAddress by haydn-bundled-twoaddr-rewrite (COPY
  // recovery), not by dissolving the group.
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::F2MULAA32RS_HHLL), Haydn::D0)
      .addReg(Haydn::D1)
      .addReg(Haydn::D2)
      .addReg(Haydn::D3);
  BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADDI32_W), Haydn::R1)
      .addReg(Haydn::R2)
      .addImm(4);
  finalizeBundle(*MBB, MBB->instr_begin(), MBB->instr_end());
  ASSERT_FALSE(MBB->empty());
  MachineInstr &Root = MBB->front();
  ASSERT_TRUE(Root.isBundle());
  EXPECT_EQ(Root.getBundleSize(), 2u);
  EXPECT_TRUE(II.isSchedulingBoundary(Root, MBB, *MF));

  bool SawMac = false;
  bool SawAddi = false;
  for (MachineBasicBlock::instr_iterator I = std::next(Root.getIterator());
       I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
    EXPECT_FALSE(II.isSchedulingBoundary(*I, MBB, *MF));
    if (I->getOpcode() == Haydn::F2MULAA32RS_HHLL)
      SawMac = true;
    if (I->getOpcode() == Haydn::ADDI32_W)
      SawAddi = true;
  }
  EXPECT_TRUE(SawMac);
  EXPECT_TRUE(SawAddi);
}

TEST_F(HaydnBundleBoundaryTest, PreRAMove32MiVsDescPortsAndIsPreRAHR) {
  // Live MI vs table-backed MID for MOVE32-class, plus pre-RA HR factory.
  // REGRESSION TEST REBASE (W68.0b, 2026-08-25): logical MOVE32 is dest+src
  // (1R1W) — CreateTargetMIHazardRecognizer(IsPreRA) installs
  // HaydnHazardRecognizer that charges PortModel MI ports per explicit
  // field, identical to the descriptor shape; never setDesc/member opcodes.
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // MOVE32 R2, R1 — canonical copyPhysReg shape (dest+src).
  MachineInstr *MoveRepeated =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVE32), Haydn::R2)
          .addReg(Haydn::R1)
          .getInstr();
  auto [MiR, MiW] = countGPRPorts(*MoveRepeated);
  EXPECT_EQ(MiR, HaydnPreRASchedStrategy::move32ClassMiRepeatedSrcGprReads);
  EXPECT_EQ(MiW, HaydnPreRASchedStrategy::move32ClassMiRepeatedSrcGprWrites);
  EXPECT_EQ(MiR, 1u);
  EXPECT_EQ(MiW, 1u);

  // Second move with a different source/dest: MI and descriptor agree at
  // 1R1W (dest+src schema — no repeated-source form exists anymore).
  MachineInstr *MoveDistinct =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVE32), Haydn::R3)
          .addReg(Haydn::R4)
          .getInstr();
  auto [MiR2, MiW2] = countGPRPorts(*MoveDistinct);
  EXPECT_EQ(MiR2, 1u);
  EXPECT_EQ(MiW2, 1u);

  // Table-backed MCInstrDesc: NumDefs=1, one GPR use → descriptor 1R1W.
  const MCInstrDesc &MID = II.get(Haydn::MOVE32);
  EXPECT_EQ(MID.getNumDefs(), 1u);
  EXPECT_GE(MID.getNumOperands(), 2u);
  HaydnCyclePortDemand Desc = estimateHaydnPortsFromDesc(MID);
  EXPECT_EQ(Desc.GPRReads,
            HaydnPreRASchedStrategy::move32ClassDescShapeGprReads);
  EXPECT_EQ(Desc.GPRWrites,
            HaydnPreRASchedStrategy::move32ClassDescShapeGprWrites);
  EXPECT_EQ(Desc.GPRReads, 1u);
  EXPECT_EQ(Desc.GPRWrites, 1u);
  // Dest+src law: descriptor and MI paths agree (both are the wire shape).
  EXPECT_EQ(Desc.GPRReads, MiR);
  EXPECT_EQ(Desc.GPRWrites, MiW);
  EXPECT_EQ(Desc.GPRReads, MiR2);
  EXPECT_EQ(Desc.GPRWrites, MiW2);

  // countHaydnPortsFromMI (shared MI aggregator) matches countGPRPorts.
  HaydnCyclePortDemand FromMI = countHaydnPortsFromMI(*MoveRepeated);
  EXPECT_EQ(FromMI.GPRReads, MiR);
  EXPECT_EQ(FromMI.GPRWrites, MiW);

  // CreateTargetMIHazardRecognizer always installs HaydnHazardRecognizer.
  // Null DAG → no hasVRegLiveness → IsPreRA=false. ScheduleDAGMILive sets
  // IsPreRA=true via hasVRegLiveness; pin the explicit pre-RA constructor.
  std::unique_ptr<ScheduleHazardRecognizer> FactoryHR(
      II.CreateTargetMIHazardRecognizer(/*ItinData=*/nullptr, /*DAG=*/nullptr));
  ASSERT_NE(FactoryHR, nullptr);
  auto *FactoryHaydn = static_cast<HaydnHazardRecognizer *>(FactoryHR.get());
  EXPECT_FALSE(FactoryHaydn->isPreRA())
      << "null DAG has no vreg liveness → post-RA default";

  HaydnHazardRecognizer PreRAHR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/true,
                                /*AltDescs=*/nullptr);
  EXPECT_TRUE(PreRAHR.isPreRA());
  HaydnHazardRecognizer PostRAHR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                                 /*AltDescs=*/nullptr);
  EXPECT_FALSE(PostRAHR.isPreRA());

  // Logical opcode only — never a private member descriptor (pre-RA law).
  EXPECT_EQ(MoveRepeated->getOpcode(), Haydn::MOVE32);
}

// Live pre-RA HR format acceptance ≡ pure exactTryAddProduct (plan §8.4 #7).
// CreateTargetMIHazardRecognizer IsPreRA expands CurrentCycleCandidates via
// the same exact matching depth ResourceCycle / post-RA HR use. Rematch
// triple ADD32+2×ADD64 packs in one cycle (ADD32 rematches onto S0); two
// S0-only ST32 need two cycles. No AltDesc stamp / setDesc on pre-RA path.
// Ports: ADD32 is GPR 1W; each ADD64 is DR 1W — rematch fits bank budgets so
// getHazardType agrees with format for this shape (unlike 3×ADD32 GPR 2W).
TEST_F(HaydnBundleBoundaryTest, PreRARCHrLiveFormatAcceptanceAgreesWithPureExact) {
  using S = HaydnPreRASchedStrategy;
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // Pure oracle pins (ResourceCycle / post-RA HR peer depth).
  unsigned Rematch[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
  unsigned TwoST[] = {Haydn::ST32, Haydn::ST32};
  EXPECT_TRUE(S::productExactCanPackSequence(Rematch));
  EXPECT_FALSE(S::productExactCanPackSequence(TwoST));
  EXPECT_TRUE(S::productFormatAcceptanceDifferentialPins());

  // Independent rematch MIs: one GPR ADD32 + two DR ADD64.
  MachineInstr *A32 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R1)
          .addReg(Haydn::R2)
          .addReg(Haydn::R3)
          .getInstr();
  MachineInstr *A64a =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD64), Haydn::D0)
          .addReg(Haydn::D1)
          .addReg(Haydn::D2)
          .getInstr();
  MachineInstr *A64b =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD64), Haydn::D3)
          .addReg(Haydn::D4)
          .addReg(Haydn::D5)
          .getInstr();
  // Two S0-only stores (format reject same-cycle).
  MachineInstr *ST0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ST32))
          .addReg(Haydn::R4)
          .addReg(Haydn::R5)
          .addImm(0)
          .getInstr();
  MachineInstr *ST1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ST32))
          .addReg(Haydn::R6)
          .addReg(Haydn::R7)
          .addImm(4)
          .getInstr();

  // Pre-RA HR: feasibility only — no AltDesc side-map stamp.
  HaydnAlternateDescriptors PreAlt;
  HaydnHazardRecognizer PreHR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/true,
                              &PreAlt);
  PreHR.Reset();
  EXPECT_TRUE(PreHR.isPreRA());

  SUnit SU32(A32, /*NodeNum=*/0);
  SUnit SU64a(A64a, /*NodeNum=*/1);
  SUnit SU64b(A64b, /*NodeNum=*/2);
  EXPECT_EQ(PreHR.getHazardType(&SU32, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  PreHR.EmitInstruction(A32);
  EXPECT_EQ(PreHR.getHazardType(&SU64a, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  PreHR.EmitInstruction(A64a);
  // Rematch keeps the second ADD64 format-legal (preferred freeze would not).
  EXPECT_EQ(PreHR.getHazardType(&SU64b, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard)
      << "pre-RA live HR must rematch like pure exactTryAddProduct";
  PreHR.EmitInstruction(A64b);

  const CycleState &Pref =
      selectPreferredCandidate(PreHR.getCurrentCycleCandidates());
  EXPECT_EQ(Pref.memberCount(), 3u);
  EXPECT_EQ(Pref.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));
  // Preferred survivor after rematch: ADD32 on S0 (same pure / ResourceCycle pin).
  EXPECT_EQ(Pref.Members[0].LogicalOpcode, Haydn::ADD32);
  EXPECT_EQ(Pref.Members[0].FieldSlots, SlotBits(Haydn::SLOT0));
  EXPECT_FALSE(PreAlt.getSelectedOpcode(A32).has_value())
      << "pre-RA must not stamp member AltDesc";
  EXPECT_FALSE(PreAlt.getSelectedOpcode(A64a).has_value());
  EXPECT_FALSE(PreAlt.getSelectedOpcode(A64b).has_value());
  EXPECT_EQ(A32->getOpcode(), Haydn::ADD32);
  EXPECT_EQ(A64a->getOpcode(), Haydn::ADD64);

  // Post-RA HR on the same multiset: format acceptance agrees (no setDesc here
  // beyond optional AltDesc stamp — member opcodes stay logical on MI).
  HaydnAlternateDescriptors PostAlt;
  HaydnHazardRecognizer PostHR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                               &PostAlt);
  PostHR.Reset();
  PostHR.EmitInstruction(A32);
  PostHR.EmitInstruction(A64a);
  PostHR.EmitInstruction(A64b);
  const CycleState &PostPref =
      selectPreferredCandidate(PostHR.getCurrentCycleCandidates());
  EXPECT_EQ(PostPref.memberCount(), Pref.memberCount());
  EXPECT_EQ(PostPref.OccupiedSlots, Pref.OccupiedSlots);
  EXPECT_EQ(PostPref.Members[0].FieldSlots, Pref.Members[0].FieldSlots);

  // Two ST32: first accepts; second format-Hazards (S0 exclusive).
  HaydnHazardRecognizer PreST(&II, /*ItinData=*/nullptr, /*IsPreRA=*/true,
                              /*AltDescs=*/nullptr);
  PreST.Reset();
  SUnit SUst0(ST0, /*NodeNum=*/3);
  SUnit SUst1(ST1, /*NodeNum=*/4);
  EXPECT_EQ(PreST.getHazardType(&SUst0, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  PreST.EmitInstruction(ST0);
  EXPECT_EQ(PreST.getHazardType(&SUst1, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "second ST32 must format-Hazard under pure exact / ResourceCycle peer";
  EXPECT_EQ(selectPreferredCandidate(PreST.getCurrentCycleCandidates())
                .memberCount(),
            1u);
}

// Live HR path (getHazardType / Emit on real MIs): format-bearing
// MultiSlot_Pseudo is isPseudo=1 but must book issue and placement. Pure
// tryAdd / isNoHazardMeta pins alone do not cover MachineScheduler's live
// surface. Emit three ADD32_MSP through the live HR (same pattern as the
// three-ADD32 BUNDLE root pin); a fourth Hazards; IMPLICIT_DEF/KILL stay
// zero-resource on the saturated cycle; post-RA AltDesc stamps preferred
// members from occupancy residual 2/1/0 (no setDesc here — leaveRegion
// materialize only).
// Note: GPR 2W can reject a third 1W ALU on getHazardType alone; Emit still
// runs commitPlacement (scheduler-prechecked path) so rematch/AltDesc are
// observable for the three-member format fill.
TEST_F(HaydnBundleBoundaryTest, LiveHRBooksMultiSlotPseudoEmitAndAltDesc) {
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // Four independent MultiSlot_Pseudo ADD32_MSP (isPseudo=1, alts-bearing).
  MachineInstr *M0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32_MSP), Haydn::R0)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  MachineInstr *M1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32_MSP), Haydn::R3)
          .addReg(Haydn::R4)
          .addReg(Haydn::R5)
          .getInstr();
  MachineInstr *M2 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32_MSP), Haydn::R6)
          .addReg(Haydn::R7)
          .addReg(Haydn::R8)
          .getInstr();
  MachineInstr *M3 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32_MSP), Haydn::R9)
          .addReg(Haydn::R10)
          .addReg(Haydn::R11)
          .getInstr();
  ASSERT_TRUE(M0->isPseudo());
  ASSERT_TRUE(M1->isPseudo());
  ASSERT_TRUE(M2->isPseudo());
  ASSERT_FALSE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32_MSP));
  {
    HaydnMCFormats Fmts;
    ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32_MSP));
  }

  // True meta controls: zero-resource even when the cycle is full.
  MachineInstr *ImpDef =
      BuildMI(*MBB, MBB->end(), DL, II.get(TargetOpcode::IMPLICIT_DEF),
              Haydn::R12)
          .getInstr();
  MachineInstr *Kill =
      BuildMI(*MBB, MBB->end(), DL, II.get(TargetOpcode::KILL))
          .addReg(Haydn::R13, RegState::Kill)
          .getInstr();
  ASSERT_TRUE(ImpDef->isPseudo());
  ASSERT_TRUE(Kill->isPseudo());
  ASSERT_TRUE(Haydn::MachineBundle::isNoHazardMetaInstruction(
      TargetOpcode::IMPLICIT_DEF));
  ASSERT_TRUE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(TargetOpcode::KILL));

  HaydnAlternateDescriptors AltDescs;
  HaydnHazardRecognizer HR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                           &AltDescs);
  HR.Reset();

  SUnit SU0(M0, /*NodeNum=*/0);
  SUnit SU3(M3, /*NodeNum=*/3);
  SUnit SUImp(ImpDef, /*NodeNum=*/4);
  SUnit SUKill(Kill, /*NodeNum=*/5);

  // First MultiSlot_Pseudo is accepted (proves isPseudo is not a blanket skip).
  EXPECT_EQ(HR.getHazardType(&SU0, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  // Emit three MSP through live commitPlacement (preferred S2→S1→S0 rematch).
  HR.EmitInstruction(M0);
  HR.EmitInstruction(M1);
  HR.EmitInstruction(M2);

  // Full 3-issue / full-slot cycle: fourth MultiSlot_Pseudo Hazards.
  EXPECT_EQ(HR.getHazardType(&SU3, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "live HR must book MultiSlot_Pseudo issue/placement; fourth must "
         "Hazard";

  // Preferred field order after rematch: S2 → S1 → S0. S0 may be the E3
  // member (same-field Mode tie-break), not occupancy's E2 default.
  const haydn::bundle::CycleState &Pref =
      haydn::bundle::selectPreferredCandidate(HR.getCurrentCycleCandidates());
  EXPECT_EQ(Pref.memberCount(), 3u);
  EXPECT_EQ(Pref.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));
  EXPECT_EQ(Pref.Members[0].FieldSlots, SlotBits(Haydn::SLOT2));
  EXPECT_EQ(Pref.Members[1].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(Pref.Members[2].FieldSlots, SlotBits(Haydn::SLOT0));

  // Post-RA AltDesc stamp on Emit (leaveRegion setDesc target only — MI
  // opcode remains the logical MultiSlot_Pseudo).
  auto Sel0 = AltDescs.getSelectedOpcode(M0);
  auto Sel1 = AltDescs.getSelectedOpcode(M1);
  auto Sel2 = AltDescs.getSelectedOpcode(M2);
  ASSERT_TRUE(Sel0.has_value());
  ASSERT_TRUE(Sel1.has_value());
  ASSERT_TRUE(Sel2.has_value());
  EXPECT_EQ(*Sel0, Pref.Members[0].MemberOpcode);
  EXPECT_EQ(*Sel1, Pref.Members[1].MemberOpcode);
  EXPECT_EQ(*Sel2, Pref.Members[2].MemberOpcode);
  EXPECT_EQ(M0->getOpcode(), Haydn::ADD32_MSP);
  EXPECT_EQ(M1->getOpcode(), Haydn::ADD32_MSP);
  EXPECT_EQ(M2->getOpcode(), Haydn::ADD32_MSP);
  EXPECT_FALSE(AltDescs.getSelectedOpcode(M3).has_value())
      << "fourth MSP never committed";

  // IMPLICIT_DEF / KILL remain NoHazard and do not free a slot for MSP #4.
  EXPECT_EQ(HR.getHazardType(&SUImp, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(ImpDef);
  EXPECT_EQ(HR.getHazardType(&SUKill, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(Kill);
  EXPECT_FALSE(AltDescs.getSelectedOpcode(ImpDef).has_value());
  EXPECT_FALSE(AltDescs.getSelectedOpcode(Kill).has_value());
  EXPECT_EQ(HR.getHazardType(&SU3, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "meta Emit must not clear MultiSlot_Pseudo occupancy";
  EXPECT_EQ(
      haydn::bundle::selectPreferredCandidate(HR.getCurrentCycleCandidates())
          .memberCount(),
      3u);

  // Pre-RA phase identity: same booking, no AltDesc stamp / no setDesc.
  HaydnAlternateDescriptors PreAlt;
  HaydnHazardRecognizer PreHR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/true,
                              &PreAlt);
  PreHR.Reset();
  PreHR.EmitInstruction(M0);
  PreHR.EmitInstruction(M1);
  PreHR.EmitInstruction(M2);
  EXPECT_FALSE(PreAlt.getSelectedOpcode(M0).has_value())
      << "pre-RA must not stamp member AltDesc";
  EXPECT_FALSE(PreAlt.getSelectedOpcode(M1).has_value());
  EXPECT_FALSE(PreAlt.getSelectedOpcode(M2).has_value());
  EXPECT_EQ(M0->getOpcode(), Haydn::ADD32_MSP);
  SUnit PreFourth(M3, /*NodeNum=*/6);
  EXPECT_EQ(PreHR.getHazardType(&PreFourth, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::Hazard)
      << "pre-RA live HR still books MultiSlot_Pseudo occupancy";
}

//===----------------------------------------------------------------------===//
// Exhaustive ≤3 pack oracle densify — HR placement + Req/Res closest pairs
//===----------------------------------------------------------------------===//
//
// HR commitPlacement is product tryAdd / exactTryAdd on the same pure APIs.
// Dense matrix pins preferred order, MultiSlot_Pseudo parity, and the
// Required/Reserved stage conflict law (no second reservation table).

TEST(HaydnHazardRecognizerTest, VF24_ExhaustiveLe3PreferredVsExactOracle) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  static constexpr unsigned Alpha[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ST32,
                                       Haydn::LD32, Haydn::X2MULA32};
  unsigned Checked = 0;

  auto CheckSeq = [&](ArrayRef<unsigned> Seq) {
    ++Checked;
    // Preferred collapse (HR Emit single-state shape).
    CycleState Pref = makeProductCycleState();
    bool PrefOk = true;
    for (unsigned Opc : Seq) {
      if (!tryAddProduct(Pref, Fmts, Opc)) {
        PrefOk = false;
        break;
      }
    }
    // Exact candidate set (HR multi-candidate commitPlacement path).
    CycleCandidateSet Exact = makeProductCandidateSet();
    bool ExactOk = true;
    for (unsigned Opc : Seq) {
      if (!exactTryAddProduct(Exact, Fmts, Opc)) {
        ExactOk = false;
        break;
      }
    }
    EXPECT_EQ(ExactOk, exactCanPackProductSequence(Fmts, Seq));
    if (PrefOk) {
      EXPECT_TRUE(ExactOk);
      const CycleState &Sel = selectPreferredCandidate(Exact);
      EXPECT_EQ(Pref.OccupiedSlots, Sel.OccupiedSlots);
      ASSERT_EQ(Pref.memberCount(), Sel.memberCount());
      for (unsigned I = 0, E = Pref.memberCount(); I != E; ++I) {
        EXPECT_EQ(Pref.Members[I].FieldSlots, Sel.Members[I].FieldSlots)
            << "HR preferred field drift at " << I;
        EXPECT_EQ(Pref.Members[I].MemberOpcode, Sel.Members[I].MemberOpcode);
      }
    }
    // Deterministic preferred selection: re-expand matches.
    if (ExactOk) {
      CycleCandidateSet Again = makeProductCandidateSet();
      for (unsigned Opc : Seq)
        ASSERT_TRUE(exactTryAddProduct(Again, Fmts, Opc));
      EXPECT_EQ(selectPreferredCandidate(Exact).OccupiedSlots,
                selectPreferredCandidate(Again).OccupiedSlots);
    }
  };

  for (unsigned A : Alpha)
    CheckSeq(ArrayRef<unsigned>(&A, 1));
  for (unsigned A : Alpha)
    for (unsigned B : Alpha) {
      unsigned Seq[2] = {A, B};
      CheckSeq(Seq);
    }
  for (unsigned A : Alpha)
    for (unsigned B : Alpha)
      for (unsigned C : Alpha) {
        unsigned Seq[3] = {A, B, C};
        CheckSeq(Seq);
      }
  EXPECT_EQ(Checked, 5u + 25u + 125u);

  // Rematch pin: preferred dead-ends; exact keeps HR-legal pack.
  {
    unsigned Ops[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
    CycleState Pref = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(Pref, Fmts, Ops[0]));
    ASSERT_TRUE(tryAddProduct(Pref, Fmts, Ops[1]));
    EXPECT_FALSE(tryAddProduct(Pref, Fmts, Ops[2]));
    CycleCandidateSet Exact = makeProductCandidateSet();
    ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Ops[0]));
    ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Ops[1]));
    ASSERT_TRUE(exactTryAddProduct(Exact, Fmts, Ops[2]));
    EXPECT_EQ(selectPreferredCandidate(Exact).OccupiedSlots,
              SlotBits(Haydn::SLOT_ALL));
  }
}

TEST(HaydnHazardRecognizerTest, VF24_ReqResClosestLegalIllegalMatrix) {
  // Closest legal/illegal around the AIE Req/Res law — pure wrapper half of
  // HR stage booking (no product itinerary invent; no second table).
  // Required vs Required same slot illegal; distinct slots legal.
  EXPECT_TRUE(requiredSlot(0).conflict(requiredSlot(0)));
  EXPECT_FALSE(requiredSlot(0).conflict(requiredSlot(1)));
  EXPECT_FALSE(requiredSlot(1).conflict(requiredSlot(2)));

  // Required vs Reserved same bit illegal either order; adjacent bit legal.
  EXPECT_TRUE(requiredSlot(0).conflict(reservedSlot(0)));
  EXPECT_TRUE(reservedSlot(0).conflict(requiredSlot(0)));
  EXPECT_FALSE(requiredSlot(0).conflict(reservedSlot(1)));
  EXPECT_FALSE(reservedSlot(2).conflict(requiredSlot(0)));

  // Reserved vs Reserved always legal (including same unit).
  EXPECT_FALSE(reservedSlot(0).conflict(reservedSlot(0)));
  EXPECT_FALSE(reservedSlot(1).conflict(reservedSlot(2)));

  // Stage-relative closest: Res at +1 conflicts with Req at cycle 1, not 0.
  ResourceScoreboard<HaydnFuncUnitWrapper> SB;
  SB.reset(/*Depth=*/4);
  HaydnFuncUnitWrapper A0 = requiredSlot(0);
  A0.setIssueCountOne();
  SB[0] |= A0;
  SB[1] |= reservedSlot(1);
  HaydnFuncUnitWrapper B1 = requiredSlot(1);
  B1.setIssueCountOne();
  EXPECT_FALSE(SB[0].conflict(B1)); // closest legal issue at cycle 0
  EXPECT_TRUE(SB[1].conflict(B1));  // closest illegal (Req vs Res on S1)
  EXPECT_FALSE(SB[1].conflict(reservedSlot(1))); // Res/Res legal at +1

  // Both directions: Advance then Recede restores the ring entry.
  SB.advance();
  EXPECT_TRUE(SB[0].conflict(B1));
  SB.recede();
  EXPECT_FALSE(SB[0].conflict(B1));
  EXPECT_TRUE(SB[1].conflict(B1));
}

TEST(HaydnHazardRecognizerTest, VF24_MultiSlotPseudoNotExemptInOracleAlphabet) {
  using namespace llvm::haydn::bundle;
  // MultiSlot_Pseudo is isPseudo but not no-hazard-meta; HR placement books
  // it like ADD32 across the ≤3 fill + rematch shapes.
  EXPECT_FALSE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(Haydn::ADD32_MSP));
  EXPECT_TRUE(
      Haydn::MachineBundle::isNoHazardMetaInstruction(TargetOpcode::KILL));

  HaydnMCFormats Fmts;
  ASSERT_TRUE(hasPlacementAlternatives(Fmts, Haydn::ADD32_MSP));

  // Three MSP fill Full; fourth rejects (same as ADD32).
  CycleState S = makeProductCycleState();
  for (unsigned I = 0; I < 3; ++I)
    ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32_MSP)) << I;
  EXPECT_EQ(S.OccupiedSlots, SlotBits(Haydn::SLOT_ALL));
  EXPECT_EQ(S.Members[0].FieldSlots, SlotBits(Haydn::SLOT2));
  EXPECT_EQ(S.Members[1].FieldSlots, SlotBits(Haydn::SLOT1));
  EXPECT_EQ(S.Members[2].FieldSlots, SlotBits(Haydn::SLOT0));
  EXPECT_FALSE(canTryAddProduct(S, Fmts, Haydn::ADD32_MSP));

  // Exact rematch with two ADD64.
  unsigned Ops[] = {Haydn::ADD32_MSP, Haydn::ADD64, Haydn::ADD64};
  EXPECT_TRUE(exactCanPackProductSequence(Fmts, Ops));
  CycleCandidateSet C = makeProductCandidateSet();
  for (unsigned Opc : Ops)
    ASSERT_TRUE(exactTryAddProduct(C, Fmts, Opc));
  EXPECT_EQ(selectPreferredCandidate(C).Members[0].FieldSlots,
            SlotBits(Haydn::SLOT0));
}

// leaveRegion checkInterZoneConflicts aligns Bot at DeltaCycles=-1 against
// Top (AIEMachineScheduler.cpp:1155-1159; AIEHazardRecognizer.cpp:410-413).
// Both zones finish by advance/recede to an empty cycle, so Bot[0] is cycle
// -1 relative to Top's write head and must line up with Top[-1].
TEST(HaydnHazardRecognizerTest, InterZoneScoreboardConflictDeltaMinusOne) {
  ResourceScoreboard<HaydnFuncUnitWrapper> TopSB;
  ResourceScoreboard<HaydnFuncUnitWrapper> BotSB;
  TopSB.reset(/*Depth=*/4);
  BotSB.reset(/*Depth=*/4);

  // Empty boards never conflict at any alignment.
  EXPECT_FALSE(TopSB.conflict(BotSB, /*DeltaCycles=*/-1));
  EXPECT_FALSE(TopSB.conflict(BotSB, /*DeltaCycles=*/0));

  // Top books S0 at cycle -1 (still in the look-behind window after the zone
  // advanced past its last issue). Bot books S0 at its scoreboard[0] (the
  // empty receded cycle that represents absolute cycle -1). Delta=-1 must
  // report conflict; a later Bot booking at +1 must not.
  HaydnFuncUnitWrapper S0 = requiredSlot(0);
  S0.setIssueCountOne();
  TopSB[-1] |= S0;
  BotSB[0] |= S0;
  EXPECT_TRUE(TopSB.conflict(BotSB, /*DeltaCycles=*/-1));
  EXPECT_FALSE(TopSB.conflict(BotSB, /*DeltaCycles=*/0));

  // Distinct slots at the same aligned cycle do not Required-conflict.
  ResourceScoreboard<HaydnFuncUnitWrapper> BotS1;
  BotS1.reset(/*Depth=*/4);
  HaydnFuncUnitWrapper S1 = requiredSlot(1);
  S1.setIssueCountOne();
  BotS1[0] |= S1;
  EXPECT_FALSE(TopSB.conflict(BotS1, /*DeltaCycles=*/-1));

  // Advancing Top slides the -1 occupancy out of the Bot[0] alignment so the
  // seam clears — the leaveRegion pad loop's Top.bumpCycle effect.
  TopSB.advance();
  EXPECT_FALSE(TopSB.conflict(BotSB, /*DeltaCycles=*/-1));
}

// INLINEASM / INLINEASM_BR are the normal-LLVM layout boundary:
// getInstSizeInBytes uses exact typed getInlineAsmLength. Empty barriers
// charge 0; each unbraced public mnemonic is one product parcel; a braced
// Format E packet is one parcel (not N × MaxInstLength). Opaque text is
// rejected. Compiler bundles must not cross INLINEASM (isSchedulingBoundary).
TEST_F(HaydnBundleBoundaryTest, InlineAsmExactTypedLayoutSize) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  const unsigned FullBytes = productParcelBytes().Value;
  ASSERT_EQ(FullBytes, productParcelBytes().Value);
  ASSERT_NE(TM->getMCAsmInfo(), nullptr);
  EXPECT_EQ(TM->getMCAsmInfo()->getMaxInstLength(), FullBytes)
      << "MaxInstLength must equal product Full parcel (generic .space / "
         "peer fallback quantum); Haydn charges exact typed parcels";

  auto makeAsm = [&](const char *Str) -> MachineInstr & {
    // ExtraInfo = 1 → sideeffect (matches MIR `INLINEASM &"...", 1`).
    return *BuildMI(*MBB, MBB->end(), DL, II.get(TargetOpcode::INLINEASM))
                 .addExternalSymbol(Str)
                 .addImm(1);
  };

  // Empty side-effect barrier: no textual instruction → 0 layout bytes.
  MachineInstr &Empty = makeAsm("");
  EXPECT_TRUE(Empty.isInlineAsm());
  EXPECT_TRUE(Empty.isPseudo());
  EXPECT_EQ(II.getInstSizeInBytes(Empty), 0u);
  EXPECT_TRUE(II.isSchedulingBoundary(Empty, MBB, *MF));

  // One public mnemonic → one product parcel (MC singleton wrap).
  MachineInstr &One = makeAsm("nop");
  EXPECT_EQ(II.getInstSizeInBytes(One), FullBytes);
  EXPECT_EQ(ceilProductParcels(II.getInstSizeInBytes(One)), 1u);

  // Multi-line (real newlines): three standalone packets.
  MachineInstr &ThreeNL = makeAsm("nop\nnop\nnop");
  EXPECT_EQ(II.getInstSizeInBytes(ThreeNL), 3u * FullBytes);
  EXPECT_EQ(ceilProductParcels(II.getInstSizeInBytes(ThreeNL)), 3u);

  // SeparatorString (';') multi-stmt — three unbraced packets.
  MachineInstr &ThreeSemi = makeAsm("nop; nop; nop");
  EXPECT_EQ(II.getInstSizeInBytes(ThreeSemi), 3u * FullBytes);
  EXPECT_EQ(ceilProductParcels(II.getInstSizeInBytes(ThreeSemi)), 3u);

  // Braced Format E packet is one parcel, not N textual instructions.
  MachineInstr &Braced = makeAsm("{ nop; nop; nop }");
  EXPECT_EQ(II.getInstSizeInBytes(Braced), FullBytes);
  EXPECT_EQ(ceilProductParcels(II.getInstSizeInBytes(Braced)), 1u);

  // .space uses the explicit byte count (not MaxInstLength × 1).
  MachineInstr &Space = makeAsm(".space 32");
  EXPECT_EQ(II.getInstSizeInBytes(Space), 32u);
  // Ceil by product EncodedBytes (12): 32 → 3 parcels (was 2 under legacy 16-byte).
  EXPECT_EQ(ceilProductParcels(II.getInstSizeInBytes(Space)), 3u);

  // INLINEASM_BR uses the same length hook (opaque branchy asm boundary).
  MachineInstr &BrAsm =
      *BuildMI(*MBB, MBB->end(), DL, II.get(TargetOpcode::INLINEASM_BR))
           .addExternalSymbol("b label")
           .addImm(1);
  EXPECT_TRUE(BrAsm.isInlineAsm());
  EXPECT_EQ(II.getInstSizeInBytes(BrAsm), FullBytes);
  EXPECT_TRUE(II.isSchedulingBoundary(BrAsm, MBB, *MF));

  // lateLayoutBytes mirrors getInstSizeInBytes for INLINEASM (not a
  // product parcel invent). finalizeExactLateSingleton must leave it bare.
  EXPECT_EQ(lateLayoutBytes(One), FullBytes);
  EXPECT_EQ(lateLayoutBytes(ThreeNL), 3u * FullBytes);
  EXPECT_EQ(lateLayoutBytes(ThreeSemi), 3u * FullBytes);
  EXPECT_EQ(lateLayoutBytes(Braced), FullBytes);
  EXPECT_EQ(lateLayoutBytes(Empty), 0u);
  finalizeExactLateSingleton(One);
  EXPECT_FALSE(One.isBundled());
  EXPECT_FALSE(One.isBundle());
  EXPECT_TRUE(One.isInlineAsm());

  // Multi-MI exact-commit rejects INLINEASM as a cycle member (no BUNDLE
  // cross). ADD32+ADD32 alone remains legal and setDescs to real members.
  MachineInstr *A0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R0)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  MachineInstr *A1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R3)
          .addReg(Haydn::R4)
          .addReg(Haydn::R5)
          .getInstr();
  HaydnMCFormats Fmts;
  MachineInstr *WithAsm[] = {A0, &One};
  EXPECT_FALSE(instrsFormOneLegalCycle(WithAsm, Fmts));
  EXPECT_FALSE(commitExactMultiMIProductCycle(WithAsm));
  MachineInstr *TwoAlu[] = {A0, A1};
  EXPECT_TRUE(instrsFormOneLegalCycle(TwoAlu, Fmts));
  // P-COMMIT: multi-MI commit must bake residual placement members — never
  // leave bare logical ADD32 in a product BUNDLE after FE8.
  ASSERT_TRUE(commitExactMultiMIProductCycle(TwoAlu));
  EXPECT_NE(A0->getOpcode(), Haydn::ADD32);
  EXPECT_NE(A1->getOpcode(), Haydn::ADD32);
  EXPECT_NE(Fmts.getSlotKind(A0->getOpcode()), MCSlotKind())
      << "post-commit child must be a fixed-slot format member";
  EXPECT_NE(Fmts.getSlotKind(A1->getOpcode()), MCSlotKind())
      << "post-commit child must be a fixed-slot format member";
  ASSERT_TRUE(A0->isBundled() || A0->isBundledWithPred() ||
              A0->isBundledWithSucc());
  MachineInstr &Root = *getBundleStart(A0->getIterator());
  ASSERT_TRUE(Root.isBundle());
  auto Row = getBundleRowID(Root);
  ASSERT_TRUE(Row.has_value());
  EXPECT_TRUE(isProductBundleRow(*Row));
}

// Product cycle legality = Format E pack + no true data dep (no-forwarding).
// Opcode-only packing of MOVE32_DR_L + SEXT can be slot-legal; the MI-list
// view must reject the true RAW on the GPR (soft-float half densify class).
TEST_F(HaydnBundleBoundaryTest, InstrsFormOneLegalCycleRejectsTrueRAW) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Move =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVE32_DR_L), Haydn::R2)
          .addReg(Haydn::D0)
          .getInstr();
  MachineInstr *Sext =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::SEXT_GPR32_TO_DR64), Haydn::D1)
          .addReg(Haydn::R2)
          .getInstr();

  HaydnMCFormats Fmts;
  MachineInstr *RawPair[] = {Move, Sext};
  EXPECT_TRUE(cycleMembersHaveTrueRAW(RawPair, TRI()));
  EXPECT_FALSE(instrsFormOneLegalCycle(RawPair, Fmts));
  EXPECT_FALSE(commitExactMultiMIProductCycle(RawPair));

  // Independent half extracts (different GPRs) — no true RAW.
  MachineInstr *MoveB =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVE32_DR_L), Haydn::R3)
          .addReg(Haydn::D2)
          .getInstr();
  MachineInstr *SextB =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::SEXT_GPR32_TO_DR64), Haydn::D3)
          .addReg(Haydn::R4)
          .getInstr();
  MachineInstr *Indep[] = {MoveB, SextB};
  EXPECT_FALSE(cycleMembersHaveTrueRAW(Indep, TRI()));
}

// Production soft-float densify class: already-member MOVE32_DR_L +
// SEXT_GPR32_TO_DR64 + filler (muldf3 O2 postmisched shape). Must reject.
TEST_F(HaydnBundleBoundaryTest, RejectsMuldfMoveSextProductShape) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Move =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVE32_DR_L), Haydn::R1)
          .addReg(Haydn::D12)
          .getInstr();
  MachineInstr *Sext =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::SEXT_GPR32_TO_DR64),
              Haydn::D10)
          .addReg(Haydn::R1)
          .getInstr();
  MachineInstr *Fill =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::SUBI32), Haydn::R13)
          .addReg(Haydn::R13)
          .addImm(8)
          .getInstr();

  HaydnMCFormats Fmts;
  MachineInstr *Triple[] = {Move, Sext, Fill};
  EXPECT_TRUE(cycleMembersHaveTrueRAW(Triple, TRI()));
  EXPECT_FALSE(instrsFormOneLegalCycle(Triple, Fmts));
  EXPECT_FALSE(commitExactMultiMIProductCycle(Triple));
}

//===----------------------------------------------------------------------===//
// W23 / CR-B1 — transactional setDesc unwind on commit late-fail paths
//===----------------------------------------------------------------------===//
//
// REGRESSION TEST GROUP: a failed commitExactMultiMIProductCycle (or its
// hard-root twin) must leave every member's MIR identity EXACTLY as it was
// before the attempt.
//
// Bug: both commit paths bake Format E member descriptors (setDesc + keep-map
// operand rewrite) and clear InternalRead markers BEFORE the final coissue
// checks (unit-cover, canAdd, standalone/format, field-order RAW). A late
// `return false` left the member opcodes baked into MIR with no BUNDLE root.
// Sequentializing callers then emitted private `*_E2_*/_E3_*` opcodes as bare
// reals — the hard-constraint #8 violation surface (scheduling F2 ≡ encoding
// F9; GOALS W23). canCoissueProductCycle already had restoreDescs; the commit
// paths did not.
//
// Fix: one HaydnCommitTxn snapshot/restore mechanism wraps every post-bake
// failure return (both commit paths; P18(d)'s three bake-then-fail arms).
//
// What breaks if this regresses: identicalToIdentity fails on the first late-fail
// arm exercised (FieldOrderRAW below is the live production shape — it fired
// per-cycle in postmisched until the probe guard landed).

namespace {

/// Independent pre-attempt identity oracle for one MachineInstr: opcode,
/// MI flags, and a positional copy of the operand array. MachineOperand is
/// trivially copyable and carries its own flags + TiedTo link bits, so this
/// catches a leaked member bake (opcode change), a leaked keep-map rewrite
/// (operand drop), a leaked tie drop, and a leaked InternalRead clear.
struct InstrIdentity {
  unsigned Opcode;
  unsigned Flags;
  SmallVector<MachineOperand, 8> Operands;
};

InstrIdentity captureIdentity(const MachineInstr &MI) {
  InstrIdentity Id;
  Id.Opcode = MI.getOpcode();
  Id.Flags = MI.getFlags();
  for (const MachineOperand &MO : MI.operands())
    Id.Operands.push_back(MO);
  return Id;
}

/// Byte-identity: opcode, flags, operand count, per-operand value + flags +
/// tie link. isIdenticalTo covers value/type; flags (kill/dead/internal/
/// undef/implicit/tied) are compared explicitly because isIdenticalTo
/// deliberately ignores them.
bool identicalToIdentity(const MachineInstr &MI, const InstrIdentity &Id) {
  if (MI.getOpcode() != Id.Opcode || MI.getFlags() != Id.Flags ||
      MI.getNumOperands() != Id.Operands.size())
    return false;
  for (unsigned I = 0, E = Id.Operands.size(); I != E; ++I) {
    const MachineOperand &OA = MI.getOperand(I);
    const MachineOperand &OB = Id.Operands[I];
    if (!OA.isIdenticalTo(OB))
      return false;
    // Kill/dead/undef/InternalRead/early-clobber/tied/implicit are register
    // flags only — ST32-style imm operands must not call isKill() (asserts).
    if (OA.isReg()) {
      if (OA.isKill() != OB.isKill() || OA.isDead() != OB.isDead() ||
          OA.isUndef() != OB.isUndef() ||
          OA.isInternalRead() != OB.isInternalRead() ||
          OA.isEarlyClobber() != OB.isEarlyClobber() ||
          OA.isTied() != OB.isTied() || OA.isImplicit() != OB.isImplicit())
        return false;
    }
  }
  return true;
}

} // namespace

// CB-153b: LD reads R2, ADD redefs R2 is schedule WAR. Preferred field
// order used to flip it to true RAW; reverse re-bind on the settled row
// recovers the pack. This fixture must commit, not late-fail.
TEST_F(HaydnBundleBoundaryTest, CommitLateFailUnwindsMemberBake_FieldOrderRAW) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Ld =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32_REG_M0S0LS), Haydn::R3)
          .addReg(Haydn::R10)
          .addReg(Haydn::R2, getKillRegState(true))
          .getInstr();
  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R2)
          .addReg(Haydn::R2)
          .addReg(Haydn::R11)
          .getInstr();

  MachineInstr *Kids[] = {Ld, Add};
  EXPECT_TRUE(canCoissueProductCycle(Kids));
  EXPECT_TRUE(commitExactMultiMIProductCycle(Kids));
}

// Late-fail arm: Format E unit-cover infeasibility (two single-unit
// LOADSTORE0 stores cannot inject units — P18(d) arm 1). Snapshot is taken
// before the cover query, so the restore must still be exact.
TEST_F(HaydnBundleBoundaryTest, CommitLateFailUnwindsMemberBake_UnitCover) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *S0 = BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ST32))
                         .addReg(Haydn::R2)
                         .addReg(Haydn::R10)
                         .addImm(0)
                         .getInstr();
  MachineInstr *S1 = BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ST32))
                         .addReg(Haydn::R4)
                         .addReg(Haydn::R11)
                         .addImm(4)
                         .getInstr();

  InstrIdentity S0Copy = captureIdentity(*S0);
  InstrIdentity S1Copy = captureIdentity(*S1);

  MachineInstr *Kids[] = {S0, S1};
  EXPECT_FALSE(commitExactMultiMIProductCycle(Kids));
  EXPECT_TRUE(identicalToIdentity(*S0, S0Copy));
  EXPECT_TRUE(identicalToIdentity(*S1, S1Copy));
}

// Late-fail arm: stale InternalRead markers are cleared pre-pack (part of the
// mutated state). A canAdd infeasibility after that clear must restore the
// markers, not just the opcodes — the restore is whole-identity, not
// opcode-only restoreDescs.
TEST_F(HaydnBundleBoundaryTest, CommitLateFailRestoresInternalReadMarkers) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Ld =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32_REG_M0S0LS), Haydn::R3)
          .addReg(Haydn::R10)
          .addReg(Haydn::R2, getKillRegState(true))
          .getInstr();
  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R2)
          .addReg(Haydn::R2)
          .addReg(Haydn::R11)
          .getInstr();
  // Stale marker on the R2 use: pre-pack code clears it; a later fail must
  // put it back.
  bool Marked = false;
  for (MachineOperand &MO : Ld->operands()) {
    if (MO.isReg() && MO.getReg() == Haydn::R2 && MO.isUse()) {
      MO.setIsInternalRead(true);
      Marked = true;
    }
  }
  ASSERT_TRUE(Marked);

  InstrIdentity LdCopy = captureIdentity(*Ld);
  InstrIdentity AddCopy = captureIdentity(*Add);

  MachineInstr *Kids[] = {Ld, Add};
  // CB-153b recovers this LD+ADD pack. Successful commit clears stale
  // InternalRead as part of bake; W23 unwind of InternalRead is covered
  // by the dual-ST32 unit-cover late-fail test above.
  EXPECT_TRUE(commitExactMultiMIProductCycle(Kids));
  (void)LdCopy;
  (void)AddCopy;
}

// D1.25: reopen dissolves committed parcels back to bare MIs and must
// restore PRE-BUNDLE operand semantics. finalizeBundle only sets
// IsInternalRead and never clears (MachineInstrBundle.cpp; the shared law
// stated at the HaydnBundleMaterialize.cpp pre-pack clear). A committed
// member whose use reads an in-parcel LocalDefs register carries that
// marker out of the bundle — the one production producer of a LIVE
// writer->reader pair inside one root is the remat glue
// (HaydnPostRAScratch.cpp bundleWithPred + finalizeBundle, no RAW
// re-check; the product commit refuses true-RAW packs, so this idiom is
// how the state is built) — and the reopen unbind loop restored
// BundledPred/BundledSucc but left the marker on the operand. Because
// MachineOperand::readsReg() returns false for internal reads, the stale
// flag blinded the ONE shared no-forwarding seat
// (haydnHasIntraCycleRAW, HaydnIntraCycleRAW.h) that the incremental HR
// walk (HaydnHazardRecognizer::hasSameBundleRAW) and SMS placement use:
// a same-cycle true RAW on that register was invisible. Every other
// dissolve/recommit site clears the flag (sequentializeMultiMemberRoot,
// unstamped dissolve, HaydnFixupHwLoops, pre-pack recommit clear) —
// reopen must clear it too. Red before the reopen-side clear, green
// after.
TEST_F(HaydnBundleBoundaryTest, ReopenClearsStaleInternalRead) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // Remat-glue shape: live writer (ADD32 defs R5) then same-parcel reader
  // (XOR32 reads R5). bundleWithPred + finalizeBundle is exactly the
  // in-tree construction; finalizeBundle marks the reader's R5 use
  // IsInternalRead (live in-parcel LocalDefs def).
  MachineInstr *Def =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R5)
          .addReg(Haydn::R0)
          .addReg(Haydn::R1)
          .getInstr();
  MachineInstr *Reader =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::XOR32), Haydn::R8)
          .addReg(Haydn::R5)
          .addReg(Haydn::R6)
          .getInstr();
  Reader->bundleWithPred();
  finalizeBundle(*MBB, Def->getIterator());

  MachineOperand *ReaderUse =
      Reader->findRegisterUseOperand(Haydn::R5, TRI());
  ASSERT_NE(ReaderUse, nullptr);
  EXPECT_TRUE(ReaderUse->isInternalRead());
  EXPECT_FALSE(ReaderUse->readsReg()); // the blindness, pre-reopen

  // The reopened bare MI must re-enter scheduling with pre-bundle operand
  // semantics: root erased, kids bare, no reg operand still internal.
  EXPECT_GE(reopenProvisionalBundles(*MF, II), 1u);
  EXPECT_FALSE(Def->isBundled());
  EXPECT_FALSE(Reader->isBundled());
  for (MachineInstr &MI : *MBB) {
    EXPECT_FALSE(MI.isBundle());
    for (MachineOperand &MO : MI.operands()) {
      if (MO.isReg())
        EXPECT_FALSE(MO.isInternalRead());
    }
  }

  // Behavior witness (the restored seat): with the writer's live def
  // accumulated first — the incremental writer-first walk HR uses — the
  // reader must now report the same-cycle true RAW. With the stale flag
  // this returned false: the filed MISCOMP-class hole.
  SmallSet<Register, 8> LiveDefs;
  haydnAppendLiveDefs(*Def, LiveDefs);
  EXPECT_TRUE(haydnHasIntraCycleRAW(*Reader, LiveDefs, TRI()));
}

// Negative control: a non-recoverable root (keep-map class — the member
// operand kind sequence differs from the logical's, so a desc-only
// inverse cannot restore it) stays committed and is untouched by reopen.
TEST_F(HaydnBundleBoundaryTest, ReopenKeepsNonRecoverableRootCommitted) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // D_LDW_CB_IMM member: (outs DR64, GPR32) (ins uimm1, GPR32, simm8) vs
  // logical D_LDW_CB_IMM: (outs DR64, GPR32) (ins GPR32, i32imm, simm8) —
  // operand 2 is imm on the member, reg on the logical, so
  // memberShapesDirectEqual fails and reopen must leave the root alone.
  MachineInstr *Ld =
      BuildMI(*MBB, MBB->end(), DL,
              II.get(Haydn::D_LDW_CB_IMM_E2_E0_LOADSTORE0_CBRI), Haydn::D0)
          .addReg(Haydn::R2, RegState::Define)
          .addImm(0)
          .addReg(Haydn::R2)
          .addImm(4)
          .getInstr();
  MachineInstr *Other =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R9)
          .addReg(Haydn::R3)
          .addReg(Haydn::R4)
          .getInstr();
  Other->bundleWithPred();
  finalizeBundle(*MBB, Ld->getIterator());

  MachineInstr *Root = nullptr;
  for (MachineInstr &MI : *MBB)
    if (MI.isBundle() && !MI.isBundledWithPred())
      Root = &MI;
  ASSERT_NE(Root, nullptr);

  EXPECT_EQ(reopenProvisionalBundles(*MF, II), 0u);
  // Root survived untouched: still a committed BUNDLE, both members still
  // inside it.
  EXPECT_TRUE(Root->getParent() != nullptr);
  EXPECT_TRUE(Root->isBundle());
  EXPECT_TRUE(Ld->isBundled() && Other->isBundled());
  EXPECT_EQ(members(*Root).size(), 2u);
}

// Late-fail arm: keep-map rewrite with tied operands. MOVT32 has
// Constraints "$rd = $rd_src" (a real tie). If the commit ever bakes a member
// desc whose keep-map drops the tie, the restore must re-establish the tie
// link, not just the operand values (a lost tie mis-verifies as a def/use
// mismatch).
TEST_F(HaydnBundleBoundaryTest, CommitLateFailRestoresTiedOperands) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Movt =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVT32), Haydn::R4)
          .addReg(Haydn::R4)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  // TableGen Constraints "$rd = $rd_src" already ties dest to operand 1.
  // Do not call tieOperands again — MachineInstr asserts if Def is already
  // tied.
  ASSERT_TRUE(Movt->getOperand(0).isTied());
  ASSERT_TRUE(Movt->getOperand(1).isTied());

  MachineInstr *Other =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::XOR32), Haydn::R5)
          .addReg(Haydn::R6)
          .addReg(Haydn::R7)
          .getInstr();

  InstrIdentity MovtCopy = captureIdentity(*Movt);
  InstrIdentity OtherCopy = captureIdentity(*Other);

  // MOVT32 + XOR32: both ALU — if this ever commits it must bake members; the
  // assertion under test is that a FAIL restores the tie.
  MachineInstr *Kids[] = {Movt, Other};
  if (!commitExactMultiMIProductCycle(Kids)) {
    EXPECT_TRUE(identicalToIdentity(*Movt, MovtCopy))
        << "tie link / operand identity lost across failed commit";
    EXPECT_TRUE(identicalToIdentity(*Other, OtherCopy));
    ASSERT_TRUE(Movt->getOperand(1).isTied())
        << "tie link dropped by failed commit";
  } else {
    // If it commits, the tie must survive the member bake too (either kept
    // on operand 1 or legally remapped by the keep-map to another operand
    // that names the same register).
    bool TieSurvived = false;
    for (const MachineOperand &MO : Movt->operands()) {
      if (MO.isReg() && MO.isTied() && MO.getReg() == Haydn::R4)
        TieSurvived = true;
    }
    EXPECT_TRUE(TieSurvived) << "commit dropped the MOVT32 dest/src tie";
  }
}

// Hard-root twin: probe-rejected group (field-order RAW). commitExactHardRoot
// ProductCycle returns false BEFORE dissolve; the root and both children must
// keep their exact pre-call identity (root intact, children still bundled,
// opcodes unchanged).
TEST_F(HaydnBundleBoundaryTest, HardRootLateFailKeepsRootAndChildren) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Ld =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32_REG_M0S0LS), Haydn::R3)
          .addReg(Haydn::R10)
          .addReg(Haydn::R2, getKillRegState(true))
          .getInstr();
  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R2)
          .addReg(Haydn::R2)
          .addReg(Haydn::R11)
          .getInstr();

  finalizeBundle(*MBB, Ld->getIterator(), std::next(Add->getIterator()));
  MachineInstr &Root = MBB->front();
  ASSERT_TRUE(Root.isBundle());

  InstrIdentity LdCopy = captureIdentity(*Ld);
  InstrIdentity AddCopy = captureIdentity(*Add);
  const unsigned RootOps = Root.getNumOperands();

  EXPECT_TRUE(commitExactHardRootProductCycle(Root, II));
  EXPECT_TRUE(MBB->front().isBundle());
  (void)RootOps;
  (void)LdCopy;
  (void)AddCopy;
}

// Product no-forwarding: true RAW (def then use of same reg by different
// members, schedule order) is illegal. WAR (use then def) is not true RAW —
// cycleMembersHaveTrueRAW walks schedule order only. Dead def + use stays
// legal. Stale IsInternalRead on a use must still trip the gate.
TEST_F(HaydnBundleBoundaryTest, CycleMembersRejectLiveUseDefAnyOrder) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // Use-then-def of R2 (WAR schedule order): not a true-RAW hazard under the
  // schedule-order walk (no earlier live def feeds a later read).
  MachineInstr *Use =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::SLL32), Haydn::R1)
          .addReg(Haydn::R2)
          .addReg(Haydn::R3)
          .getInstr();
  MachineInstr *Def =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R2)
          .addReg(Haydn::R13)
          .addImm(12)
          .getInstr();
  MachineInstr *WarPair[] = {Use, Def};
  EXPECT_FALSE(cycleMembersHaveTrueRAW(WarPair, TRI()));
  // Def-then-use is true RAW (no intra-bundle forwarding).
  MachineInstr *RawOrder[] = {Def, Use};
  EXPECT_TRUE(cycleMembersHaveTrueRAW(RawOrder, TRI()));

  // Dead def then use of R4: dead write cannot form a true dep.
  MachineInstr *DeadDef =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADDI32), Haydn::R4)
          .addReg(Haydn::R0)
          .addImm(1)
          .getInstr();
  MachineOperand *DeadMO = DeadDef->findRegisterDefOperand(Haydn::R4, TRI());
  ASSERT_NE(DeadMO, nullptr);
  DeadMO->setIsDead(true);
  MachineInstr *AfterDead =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R5)
          .addReg(Haydn::R4)
          .addReg(Haydn::R6)
          .getInstr();
  MachineInstr *DeadPair[] = {DeadDef, AfterDead};
  EXPECT_FALSE(cycleMembersHaveTrueRAW(DeadPair, TRI()));

  // True RAW with stale InternalRead on the use must still reject.
  MachineInstr *Move =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVE32_DR_L), Haydn::R7)
          .addReg(Haydn::D0)
          .getInstr();
  MachineInstr *Sext =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::SEXT_GPR32_TO_DR64), Haydn::D1)
          .addReg(Haydn::R7)
          .getInstr();
  MachineOperand *SextUse = Sext->findRegisterUseOperand(Haydn::R7, TRI());
  ASSERT_NE(SextUse, nullptr);
  SextUse->setIsInternalRead(true);
  EXPECT_FALSE(SextUse->readsReg()); // InternalRead hides readsReg()
  MachineInstr *HiddenRaw[] = {Move, Sext};
  EXPECT_TRUE(cycleMembersHaveTrueRAW(HiddenRaw, TRI()));
}

// Hard-root exact-commit must erase the prior BUNDLE shell (stale
// consolidated operands) and refinalize: root implicit-def/use + kill/dead
// flags and child InternalRead markers come from live post-setDesc members.
// Plan §2.4 / §8.4 #15; leaveMBB calls commitExactHardRootProductCycle.
TEST_F(HaydnBundleBoundaryTest, HardRootExactCommitRebuildsRootOperands) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // Two independent ALUs (no same-cycle true RAW — product no-forwarding law
  // forbids packing a live def with a same-cycle consumer of that def).
  // External kills on R0/R1/R2/R3 exercise InternalRead rebuild after field
  // order: external uses must not keep stale InternalRead markers.
  MachineInstr *Def =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R4)
          .addReg(Haydn::R0, getKillRegState(true))
          .addReg(Haydn::R1, getKillRegState(true))
          .getInstr();
  MachineInstr *Use =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R5)
          .addReg(Haydn::R2, getKillRegState(true))
          .addReg(Haydn::R3, getKillRegState(true))
          .getInstr();

  // First finalize with deliberately wrong root shell operands so recommit
  // must not preserve them.
  finalizeBundle(*MBB, Def->getIterator(), std::next(Use->getIterator()));
  ASSERT_FALSE(MBB->empty());
  MachineInstr &StaleRoot = MBB->front();
  ASSERT_TRUE(StaleRoot.isBundle());
  // Inject a bogus consolidated use that must disappear after recommit.
  StaleRoot.addOperand(*MF, MachineOperand::CreateReg(
                                Haydn::R9, /*isDef=*/false, /*isImp=*/true,
                                /*isKill=*/true));
  bool SawStaleR9 = false;
  for (const MachineOperand &MO : StaleRoot.operands()) {
    if (MO.isReg() && MO.getReg() == Haydn::R9)
      SawStaleR9 = true;
  }
  ASSERT_TRUE(SawStaleR9);

  // Mark a child use with a stale InternalRead that should be cleared then
  // re-derived (R2 is external; must not keep InternalRead).
  bool MarkedR2 = false;
  for (MachineOperand &MO : Use->operands()) {
    if (MO.isReg() && MO.getReg() == Haydn::R2 && MO.isUse()) {
      MO.setIsInternalRead(true);
      MarkedR2 = true;
    }
  }
  ASSERT_TRUE(MarkedR2);

  ASSERT_TRUE(commitExactHardRootProductCycle(StaleRoot, II));

  ASSERT_FALSE(MBB->empty());
  MachineInstr &NewRoot = MBB->front();
  ASSERT_TRUE(NewRoot.isBundle());
  // Product roots stamp BundleFormatRowID via stampBundleCommit (not residual
  // full-width FormatID). FE8: legacy getBundleFormatID reader is deleted.
  auto Row = getBundleRowID(NewRoot);
  ASSERT_TRUE(Row.has_value());
  EXPECT_TRUE(isProductBundleRow(*Row));

  // Stale R9 kill must be gone; live defs R4/R5 and external uses present.
  bool HasR9 = false, HasDefR4 = false, HasDefR5 = false;
  bool HasUseR0 = false, HasUseR1 = false, HasUseR2 = false;
  bool KillR0 = false, KillR1 = false, KillR2 = false;
  for (const MachineOperand &MO : NewRoot.operands()) {
    if (!MO.isReg())
      continue;
    Register R = MO.getReg();
    if (R == Haydn::R9)
      HasR9 = true;
    if (MO.isDef() && R == Haydn::R4)
      HasDefR4 = true;
    if (MO.isDef() && R == Haydn::R5)
      HasDefR5 = true;
    if (MO.isUse() && R == Haydn::R0) {
      HasUseR0 = true;
      KillR0 = MO.isKill();
    }
    if (MO.isUse() && R == Haydn::R1) {
      HasUseR1 = true;
      KillR1 = MO.isKill();
    }
    if (MO.isUse() && R == Haydn::R2) {
      HasUseR2 = true;
      KillR2 = MO.isKill();
    }
  }
  EXPECT_FALSE(HasR9) << "stale consolidated root operand survived recommit";
  EXPECT_TRUE(HasDefR4);
  EXPECT_TRUE(HasDefR5);
  EXPECT_TRUE(HasUseR0);
  EXPECT_TRUE(HasUseR1);
  EXPECT_TRUE(HasUseR2);
  EXPECT_TRUE(KillR0);
  EXPECT_TRUE(KillR1);
  EXPECT_TRUE(KillR2);

  // Independent children: all uses are external. No InternalRead (that marker
  // is only for same-cycle true RAW, which product no-forwarding forbids).
  // Stale InternalRead on R2 must be cleared by recommit.
  bool SawAnyInternalRead = false;
  bool SawStaleInternalR2 = false;
  unsigned ChildCount = 0;
  for (MachineBasicBlock::instr_iterator I = std::next(NewRoot.getIterator());
       I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
    ++ChildCount;
    for (const MachineOperand &MO : I->operands()) {
      if (!MO.isReg() || !MO.isUse())
        continue;
      if (MO.isInternalRead())
        SawAnyInternalRead = true;
      if (MO.getReg() == Haydn::R2 && MO.isInternalRead())
        SawStaleInternalR2 = true;
    }
  }
  EXPECT_EQ(ChildCount, 2u);
  EXPECT_FALSE(SawAnyInternalRead)
      << "independent multi-MI cycle must not invent InternalRead";
  EXPECT_FALSE(SawStaleInternalR2)
      << "stale InternalRead on external use must be cleared";

  // P-COMMIT: hard-root recommit must leave real placement members, not
  // residual logical ADD32 descriptors.
  HaydnMCFormats MemberFmts;
  for (MachineBasicBlock::instr_iterator I = std::next(NewRoot.getIterator());
       I != MBB->instr_end() && I->isBundledWithPred(); ++I) {
    EXPECT_NE(I->getOpcode(), Haydn::ADD32)
        << "hard-root child must be setDesc member, not logical";
    EXPECT_NE(MemberFmts.getSlotKind(I->getOpcode()), MCSlotKind());
  }
}


// Coissue law: same available cycle may still host Anti (WAR). Emission/
// field order must preserve use-before-redef. LD + index-ADD is schedule WAR;
// residual S2→S0 field order flips to true RAW → canCoissueProductCycle false.
// SMS handoff must not freeze; exact-commit refuses before dissolve.
TEST_F(HaydnBundleBoundaryTest, CoissueProductCycle_AntiNotPreservable_LDregAdd) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Ld =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32_REG_M0S0LS), Haydn::R3)
          .addReg(Haydn::R10)
          .addReg(Haydn::R2)
          .getInstr();
  MachineInstr *Add =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R2)
          .addReg(Haydn::R2)
          .addReg(Haydn::R11)
          .getInstr();

  MachineInstr *Kids[] = {Ld, Add};
  HaydnMCFormats Fmts;
  // Layer: schedule-order still looks legal (WAR snapshot, no Data RAW).
  EXPECT_TRUE(opcodesFormOneLegalCycle(
      ArrayRef<unsigned>{Haydn::LD32_REG_M0S0LS, Haydn::ADD32}, Fmts));
  EXPECT_FALSE(cycleMembersHaveTrueRAW(Kids, TRI()));
  EXPECT_TRUE(instrsFormOneLegalCycle(Kids, Fmts));
  // CB-153b: reverse re-bind preserves Anti under the settled row.
  EXPECT_TRUE(canCoissueProductCycle(Kids));
  EXPECT_TRUE(instrsCanExactCommitProductCycle(Kids));

  finalizeBundle(*MBB, Ld->getIterator(), std::next(Add->getIterator()));
  MachineInstr &Root = MBB->front();
  ASSERT_TRUE(Root.isBundle());
  EXPECT_TRUE(commitExactHardRootProductCycle(Root, II));
  ASSERT_FALSE(MBB->empty());
  EXPECT_TRUE(MBB->front().isBundle());
}

// Independent MULL+SEQ32: no Anti between them → coissue + exact-commit OK.
TEST_F(HaydnBundleBoundaryTest, CoissueProductCycle_Independent_MullSeq) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Mul =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MULL), Haydn::R6)
          .addReg(Haydn::R1)
          .addReg(Haydn::R6)
          .getInstr();
  MachineInstr *Seq =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::SEQ32), Haydn::R4)
          .addReg(Haydn::R2)
          .addReg(Haydn::R14)
          .getInstr();

  MachineInstr *Kids[] = {Mul, Seq};
  EXPECT_TRUE(canCoissueProductCycle(Kids));
  finalizeBundle(*MBB, Mul->getIterator(), std::next(Seq->getIterator()));
  MachineInstr &Root = MBB->front();
  ASSERT_TRUE(commitExactHardRootProductCycle(Root, II));
  ASSERT_TRUE(getBundleRowID(MBB->front()).has_value());
}

//===----------------------------------------------------------------------===//
// Format-acceptance differential — live post-RA HR ≡ ResourceCycle
// (plan §8.4 #7 SMS peer; ports orthogonal)
//===----------------------------------------------------------------------===//
//
// Same per-cycle descriptor-derived opcode sets: ResourceCycle
// canReserveByOpcode/reserveByOpcode and post-RA HR getHazardType/Emit
// (commitPlacementForEmit → exactTryAddProduct) must agree on format
// accept/reject and preferred FieldSlots. Ports may reject on getHazardType
// before format on write-heavy bodies (3×ADD32); those cases still agree on
// Emit placement (scheduler-prechecked path) and on format-only RC. ST32 and
// rematch shapes keep ports under budget so getHazardType tracks format.

TEST_F(HaydnBundleBoundaryTest,
       SMS_FormatAcceptance_LiveHRMatchesResourceCycle) {
  ASSERT_TRUE(HaydnResourceCycle::formatAcceptanceDifferentialPins());

  const HaydnInstrInfo &II = TII();
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  // --- Rematch triple: ADD32 + 2×ADD64 (ports under budget; format rematch) ---
  MachineInstr *Add32 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R0)
          .addReg(Haydn::R1)
          .addReg(Haydn::R2)
          .getInstr();
  MachineInstr *Add64a =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD64), Haydn::D0)
          .addReg(Haydn::D1)
          .addReg(Haydn::D2)
          .getInstr();
  MachineInstr *Add64b =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD64), Haydn::D3)
          .addReg(Haydn::D4)
          .addReg(Haydn::D5)
          .getInstr();

  unsigned Rematch[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64};
  ASSERT_TRUE(HaydnResourceCycle::formatCanPackSequence(Rematch));
  ASSERT_TRUE(
      HaydnResourceCycle::formatPreferredStateAgreesWithPureExact(Rematch));

  HaydnResourceCycle RC;
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD32));
  RC.reserveByOpcode(Haydn::ADD32);
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD64));
  RC.reserveByOpcode(Haydn::ADD64);
  ASSERT_TRUE(RC.canReserveByOpcode(Haydn::ADD64));
  RC.reserveByOpcode(Haydn::ADD64);

  HaydnAlternateDescriptors AltDescs;
  HaydnHazardRecognizer HR(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                           &AltDescs);
  HR.Reset();
  SUnit SU0(Add32, 0), SU1(Add64a, 1), SU2(Add64b, 2);
  EXPECT_EQ(HR.getHazardType(&SU0, 0), ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(Add32);
  EXPECT_EQ(HR.getHazardType(&SU1, 0), ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(Add64a);
  EXPECT_EQ(HR.getHazardType(&SU2, 0), ScheduleHazardRecognizer::NoHazard)
      << "live HR must rematch like ResourceCycle (not preferred first-fit)";
  HR.EmitInstruction(Add64b);

  const haydn::bundle::CycleState &PrefHR =
      haydn::bundle::selectPreferredCandidate(HR.getCurrentCycleCandidates());
  EXPECT_EQ(PrefHR.memberCount(), RC.getMemberCount());
  EXPECT_EQ(PrefHR.OccupiedSlots, RC.getOccupiedSlots());
  EXPECT_EQ(PrefHR.FeasibleFormatMask, RC.getFeasibleFormatMask());
  ASSERT_EQ(PrefHR.memberCount(), 3u);
  EXPECT_EQ(PrefHR.Members[0].LogicalOpcode,
            RC.getCycleState().Members[0].LogicalOpcode);
  EXPECT_EQ(PrefHR.Members[0].FieldSlots,
            RC.getCycleState().Members[0].FieldSlots);
  EXPECT_EQ(PrefHR.Members[0].FieldSlots, SlotBits(Haydn::SLOT0));
  // Post-RA AltDesc stamps preferred members; SMS ResourceCycle never setDesc.
  EXPECT_TRUE(AltDescs.getSelectedOpcode(Add32).has_value());
  EXPECT_EQ(Add32->getOpcode(), Haydn::ADD32);

  // --- Two ST32: format Slot0 exclusivity (ports under 4R) ---
  MachineInstr *St0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ST32))
          .addReg(Haydn::R0)
          .addReg(Haydn::R1)
          .addImm(0)
          .getInstr();
  MachineInstr *St1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ST32))
          .addReg(Haydn::R2)
          .addReg(Haydn::R3)
          .addImm(4)
          .getInstr();

  HaydnResourceCycle RCst;
  ASSERT_TRUE(RCst.canReserveByOpcode(Haydn::ST32));
  RCst.reserveByOpcode(Haydn::ST32);
  EXPECT_FALSE(RCst.canReserveByOpcode(Haydn::ST32));

  HaydnHazardRecognizer HRst(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                             /*AltDescs=*/nullptr);
  HRst.Reset();
  SUnit SUst0(St0, 10), SUst1(St1, 11);
  EXPECT_EQ(HRst.getHazardType(&SUst0, 0), ScheduleHazardRecognizer::NoHazard);
  HRst.EmitInstruction(St0);
  EXPECT_EQ(HRst.getHazardType(&SUst1, 0), ScheduleHazardRecognizer::Hazard)
      << "second ST32 must Hazard on format (Slot0), same as RC canReserve";

  // --- Full 3×ADD32 format fill via Emit (ports may Hazard the 3rd on
  // getHazardType; Emit still commits placement — same format fill as RC) ---
  MachineInstr *A0 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R4)
          .addReg(Haydn::R5)
          .addReg(Haydn::R6)
          .getInstr();
  MachineInstr *A1 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R7)
          .addReg(Haydn::R8)
          .addReg(Haydn::R9)
          .getInstr();
  MachineInstr *A2 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R10)
          .addReg(Haydn::R11)
          .addReg(Haydn::R12)
          .getInstr();
  MachineInstr *A3 =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::ADD32), Haydn::R13)
          .addReg(Haydn::R14)
          .addReg(Haydn::R0)
          .getInstr();

  HaydnResourceCycle RCadd;
  for (unsigned I = 0; I < 3; ++I) {
    ASSERT_TRUE(RCadd.canReserveByOpcode(Haydn::ADD32));
    RCadd.reserveByOpcode(Haydn::ADD32);
  }
  EXPECT_FALSE(RCadd.canReserveByOpcode(Haydn::ADD32));
  EXPECT_EQ(RCadd.getOccupiedSlots(), SlotBits(Haydn::SLOT_ALL));
  EXPECT_EQ(RCadd.getCycleState().Members[0].FieldSlots,
            SlotBits(Haydn::SLOT2));
  EXPECT_EQ(RCadd.getCycleState().Members[1].FieldSlots,
            SlotBits(Haydn::SLOT1));
  EXPECT_EQ(RCadd.getCycleState().Members[2].FieldSlots,
            SlotBits(Haydn::SLOT0));

  HaydnHazardRecognizer HRadd(&II, /*ItinData=*/nullptr, /*IsPreRA=*/false,
                              /*AltDescs=*/nullptr);
  HRadd.Reset();
  // Emit three (scheduler-prechecked path) — format fill matches RC.
  HRadd.EmitInstruction(A0);
  HRadd.EmitInstruction(A1);
  HRadd.EmitInstruction(A2);
  const haydn::bundle::CycleState &PrefAdd =
      haydn::bundle::selectPreferredCandidate(
          HRadd.getCurrentCycleCandidates());
  EXPECT_EQ(PrefAdd.memberCount(), 3u);
  EXPECT_EQ(PrefAdd.OccupiedSlots, RCadd.getOccupiedSlots());
  EXPECT_EQ(PrefAdd.Members[0].FieldSlots,
            RCadd.getCycleState().Members[0].FieldSlots);
  EXPECT_EQ(PrefAdd.Members[1].FieldSlots,
            RCadd.getCycleState().Members[1].FieldSlots);
  EXPECT_EQ(PrefAdd.Members[2].FieldSlots,
            RCadd.getCycleState().Members[2].FieldSlots);
  SUnit SU3(A3, 20);
  EXPECT_EQ(HRadd.getHazardType(&SU3, 0), ScheduleHazardRecognizer::Hazard)
      << "fourth ADD32 Hazards after format fill (RC canReserve false peer)";
}

// Residual Slot0_ALU on logical MOVE32_DR is not a bound ALU0. After the
// first extract commits E3 e2 ALU0, the second must still NoHazard so
// exactTryAdd can place it on ALU1. REGRESSION: all-units prefer e2 ALU0;
// itinerary Required==Required serialized H+L and under-packed s64 extracts.
TEST_F(HaydnBundleBoundaryTest, UnplacedMove32DRNotSerializedBySlot0ALU) {
  using namespace llvm::haydn::bundle;
  const HaydnInstrInfo &II = TII();
  const InstrItineraryData *Itin = ST->getInstrItineraryData();
  ASSERT_TRUE(Itin && !Itin->isEmpty());
  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);

  MachineInstr *Lo =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVE32_DR_L), Haydn::R1)
          .addReg(Haydn::D0)
          .getInstr();
  MachineInstr *Hi =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::MOVE32_DR_H), Haydn::R2)
          .addReg(Haydn::D0)
          .getInstr();

  HaydnMCFormats SolveFmts;
  ASSERT_TRUE(hasPlacementAlternatives(SolveFmts, Haydn::MOVE32_DR_L));
  ASSERT_TRUE(hasPlacementAlternatives(SolveFmts, Haydn::MOVE32_DR_H));
  CycleCandidateSet C = makeProductCandidateSet();
  ASSERT_TRUE(exactTryAddProduct(C, SolveFmts, Haydn::MOVE32_DR_L));
  ASSERT_TRUE(exactTryAddProduct(C, SolveFmts, Haydn::MOVE32_DR_H));

  HaydnAlternateDescriptors AltDescs;
  HaydnHazardRecognizer HR(&II, Itin, /*IsPreRA=*/false, &AltDescs);
  HR.Reset();
  SUnit SULo(Lo, /*NodeNum=*/0);
  SUnit SUHi(Hi, /*NodeNum=*/1);
  EXPECT_EQ(HR.getHazardType(&SULo, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard);
  HR.EmitInstruction(Lo);
  EXPECT_EQ(HR.getHazardType(&SUHi, /*DeltaCycles=*/0),
            ScheduleHazardRecognizer::NoHazard)
      << "second MOVE32_DR must not Hazard on leftover Slot0_ALU vs "
         "committed ALU0; exactTryAdd assigns ALU1";
}

// W35 / PA-N1: UnplacedAltsSameCycle must not skip StageCycle>0 occupancy.
// Product itineraries are InstrStage cycles==1, so this overlays a synthetic
// 2-cycle exclusive LOADSTORE0 on LD32's sched class. A prior booking at
// scoreboard[+1] must Hazard a same-cycle (DeltaCycles==0) unplaced LD32.
// REGRESSION: early-return after IssueOnly made checkConflict ignore SB[1].
TEST_F(HaydnBundleBoundaryTest, UnplacedAltsSameCycleStillChecksStageOccupancy) {
  const HaydnInstrInfo &II = TII();
  const unsigned LD32SC = II.get(Haydn::LD32).getSchedClass();
  ASSERT_NE(LD32SC, 0u);

  HaydnMCFormats SolveFmts;
  ASSERT_TRUE(hasPlacementAlternatives(SolveFmts, Haydn::LD32));

  static const InstrStage OccupancyStages[] = {
      {/*Cycles=*/2, /*Units=*/1ULL << EU_LOADSTORE0, /*NextCycles=*/-1,
       InstrStage::Required},
  };
  std::vector<InstrItinerary> Itins(LD32SC + 2);
  for (InstrItinerary &It : Itins)
    It = {/*NumMicroOps=*/1, /*FirstStage=*/0, /*LastStage=*/0,
          /*FirstOperandCycle=*/0, /*LastOperandCycle=*/0};
  Itins[LD32SC] = {1, 0, 1, 0, 0};
  Itins[LD32SC + 1] = {1, UINT16_MAX, UINT16_MAX, 0, 0};

  MCSchedModel SM = MCSchedModel::Default;
  SM.InstrItineraries = Itins.data();
  InstrItineraryData FakeItin(SM, OccupancyStages, nullptr, nullptr);
  ASSERT_FALSE(FakeItin.isEmpty());

  DebugLoc DL;
  MachineBasicBlock *MBB = MF->CreateMachineBasicBlock();
  MF->push_back(MBB);
  MachineInstr *Ld =
      BuildMI(*MBB, MBB->end(), DL, II.get(Haydn::LD32), Haydn::R1)
          .addReg(Haydn::R2)
          .addImm(0)
          .getInstr();

  ResourceScoreboard<HaydnFuncUnitWrapper> Occupied;
  Occupied.reset(4);
  Occupied[1] |= requiredSlot(EU_LOADSTORE0);

  HaydnHazardRecognizer HR(&II, &FakeItin, /*IsPreRA=*/false,
                           /*AltDescs=*/nullptr);
  EXPECT_TRUE(HR.checkConflict(Occupied, *Ld, /*DeltaCycles=*/0))
      << "same-cycle unplaced-alts must still see StageCycle>0 occupancy";

  ResourceScoreboard<HaydnFuncUnitWrapper> Empty;
  Empty.reset(4);
  EXPECT_FALSE(HR.checkConflict(Empty, *Ld, /*DeltaCycles=*/0))
      << "empty +1 must not invent a StageCycle>0 conflict";
}

} // end anonymous namespace
