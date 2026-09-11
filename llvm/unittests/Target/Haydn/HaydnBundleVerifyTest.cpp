//===- HaydnBundleVerifyTest.cpp - committed-bundle verify -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for haydn::bundle::verifyCommittedBundle and
// haydn::bundle::verifyFrozenLayout (GR1.8 freeze uniqueness / displacement).
//
// AIE structure peers:
//   AIEBundle.h:150-156 getFormatOrNull (hasValidFormat + planFromPacketFormats)
//   AIEHazardRecognizer.cpp:278-312 applyFormatOrdering assert + finalizeBundle
//   AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction fail-closed pattern
//   BundleTest.cpp / HazardRecognizerTest.cpp unit style
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "HaydnMachineFunctionInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "gtest/gtest.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"
#define GET_REGINFO_ENUM
#include "HaydnGenRegisterInfo.inc"

namespace llvm {
const MCInstrInfo &getHaydnSharedMCInstrInfo();
}

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

// The independent verifier never consults the forward solver, so no
// "transitional packer block" skip exists anymore: every verdict is a
// deterministic inverse-table check. Tests assert exact accept/reject.

TEST(HaydnBundleVerifyTest, ProductSingletonAdd32Ok) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry, {Haydn::ADD32},
                                   Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_TRUE(isProductBundleRow(Plan.Row));
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
  EXPECT_EQ(Plan.memberCount(), 1u);
  EXPECT_EQ(Plan.MemberOpcodes[0], Haydn::ADD32);
}

TEST(HaydnBundleVerifyTest, ProductDisjointPairOk) {
  // LD32 + ADD32 under E2: catalog places S_LW_WITH_IMM at e0/LOADSTORE0 or
  // e1/LOAD1 and ADD32 at e0/ALU0 — leading-order assignment e0+e1 is legal.
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ADD32, Haydn::LD32}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 2u);
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
}

TEST(HaydnBundleVerifyTest, ProductThreeEntryRowIsProductSelected) {
  // Product row authority for three real members is E96ThreeEntry.
  EXPECT_TRUE(isProductBundleRow(BundleFormatRowID::E96ThreeEntry));
  EXPECT_EQ(selectProductRowForMemberCount(3),
            BundleFormatRowID::E96ThreeEntry);
  EXPECT_EQ(productParcelBytes(),
            *encodedBytesForRow(BundleFormatRowID::E96ThreeEntry));

  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96ThreeEntry,
      {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 3u);
  EXPECT_EQ(Plan.Row, BundleFormatRowID::E96ThreeEntry);
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
}

TEST(HaydnBundleVerifyTest, StallEmptyMembersOk) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err =
      verifyCommittedBundle(BundleFormatRowID::E96TwoEntry, {}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_TRUE(Plan.empty());
  EXPECT_EQ(Plan.Completion, CompletionStateID::StubIdle);
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
}

TEST(HaydnBundleVerifyTest, FullBundleNopIdleCompletesAllEntriesReal) {
  // Architectural NOP in every member slot of a legal format is product
  // idle, not residual membership. Pad is CompletionState (AllEntriesReal),
  // never an inverse-record root.
  HaydnMCFormats Fmts;
  BundlePlan One;
  auto OneErr = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                      {Haydn::NOP}, Fmts, &One);
  EXPECT_FALSE(OneErr.has_value()) << (OneErr ? *OneErr : "");
  EXPECT_TRUE(One.isProductLegal());
  EXPECT_TRUE(One.empty());
  EXPECT_EQ(One.Completion, CompletionStateID::AllEntriesReal);

  BundlePlan Two;
  auto TwoErr = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                      {Haydn::NOP, Haydn::NOP}, Fmts, &Two);
  EXPECT_FALSE(TwoErr.has_value()) << (TwoErr ? *TwoErr : "");
  EXPECT_TRUE(Two.isProductLegal());
  EXPECT_TRUE(Two.empty());
  EXPECT_EQ(Two.Completion, CompletionStateID::AllEntriesReal);
}

TEST(HaydnBundleVerifyTest, RejectsFourMembers) {
  HaydnMCFormats Fmts;
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96TwoEntry,
      {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32, Haydn::OR32}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("ISSUE_SLOT_COUNT"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, RejectsDualStoreAluThreeChild) {
  // libc bf16mull residual: D_SW_L_WITH_IMM + OR64 + ST8. Both stores are
  // golden LOADSTORE0 e0 only. Verify must refuse; MC must never see it.
  HaydnMCFormats Fmts;
  auto Logical = verifyCommittedBundle(
      BundleFormatRowID::E96ThreeEntry,
      {Haydn::D_SW_L_WITH_IMM, Haydn::OR64, Haydn::ST8}, Fmts);
  ASSERT_TRUE(Logical.has_value());
  EXPECT_NE(Logical->find("unit injectivity"), std::string::npos) << *Logical;

  auto Members = verifyCommittedBundle(
      BundleFormatRowID::E96ThreeEntry,
      {Haydn::D_SW_L_WITH_IMM_E3_E0_LOADSTORE0_RI6, Haydn::OR64,
       Haydn::S_SB_WITH_IMM_E2_E0_LOADSTORE0_RI6},
      Fmts);
  ASSERT_TRUE(Members.has_value());
  EXPECT_NE(Members->find("unit injectivity"), std::string::npos) << *Members;
}

TEST(HaydnBundleVerifyTest, RejectsSameSlotConflict) {
  // Two ST32 share LOADSTORE0 — Format E unit injectivity refuses (units ≠
  // encoded entries). Residual FieldSlots are not a second store slot.
  HaydnMCFormats Fmts;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ST32, Haydn::ST32}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("unit injectivity"), std::string::npos) << *Err;
}

// Late-MC / one-to-one: residual cycle-forming / multi-cycle / loop-control
// pseudos share one law between VerifyBundles and AsmPrinter. Exact-commit
// final real MIs before layout; printer is serialize-only.
TEST(HaydnBundleVerifyTest, ResidualCycleFormingPseudoSet) {
  const unsigned Residuals[] = {
      Haydn::LOADI32,       Haydn::LOADI64,      Haydn::LOAD_ADDR,
      Haydn::SETCBR_BEGIN,  Haydn::SETCBR_END,   Haydn::LoopStart,
      Haydn::LoopDec,       Haydn::LoopJNZ,      Haydn::SET_HWLOOP,
      Haydn::SET_HWLOOP_REG,
  };
  for (unsigned Opc : Residuals)
    EXPECT_TRUE(isResidualCycleFormingPseudo(Opc)) << "opc=" << Opc;

  // Product final reals, representation expands, and meta PseudoLoopEnd
  // (printer drop; no bytes) are not residual cycle-forming encodes.
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::PseudoLoopEnd));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::CSRW_W));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::SUBI32));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::BNEZ_W));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::SET_HWLOOP_W));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::SET_HWLOOP_F2_W));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::ADD32));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::B));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::RET));

  EXPECT_FALSE(isRepresentationExpandPseudo(Haydn::B));
  EXPECT_TRUE(isRepresentationExpandPseudo(Haydn::RET));
  EXPECT_TRUE(isRepresentationExpandPseudo(Haydn::BR_JT));
  EXPECT_TRUE(isRepresentationExpandPseudo(Haydn::PseudoCALLIndirect));
  EXPECT_FALSE(isRepresentationExpandPseudo(Haydn::LOADI32));
  EXPECT_FALSE(isRepresentationExpandPseudo(Haydn::ADD32));

  // Printer still classifies the shells; the independent inverse does not
  // accept them as a solo-cycle carve-out.
  HaydnMCFormats Fmts;
  for (unsigned Opc : {Haydn::RET, Haydn::BR_JT, Haydn::PseudoCALLIndirect}) {
    auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry, {Opc},
                                     Fmts);
    ASSERT_TRUE(Err.has_value()) << "opc=" << Opc;
    EXPECT_NE(Err->find("representation-expand"), std::string::npos)
        << "opc=" << Opc << " diag=" << *Err;
  }

  // Leftover expand-owned / remat / cross-bank copies are not inverse keys.
  EXPECT_TRUE(isExpandOwnedSemanticPseudo(Haydn::MOV_GPR_TO_DR64));
  EXPECT_TRUE(isExpandOwnedSemanticPseudo(Haydn::MOV_DR64_TO_GPR));
  EXPECT_TRUE(isExpandOwnedSemanticPseudo(Haydn::LOADI32));
  EXPECT_TRUE(isExpandOwnedSemanticPseudo(Haydn::LOADI64));
  EXPECT_TRUE(isExpandOwnedSemanticPseudo(Haydn::LD32_POST_INC));
  EXPECT_FALSE(isExpandOwnedSemanticPseudo(Haydn::ADD32));
  EXPECT_FALSE(isExpandOwnedSemanticPseudo(Haydn::LD32));
  EXPECT_FALSE(isExpandOwnedSemanticPseudo(Haydn::B));
}

TEST(HaydnBundleVerifyTest, ProductRowImmRoundTrip) {
  // Durable BUNDLE-root contract: product rows encode as their enum imm.
  EXPECT_EQ(formatRowToImm(BundleFormatRowID::E96TwoEntry),
            static_cast<unsigned>(BundleFormatRowID::E96TwoEntry));
  EXPECT_EQ(formatRowToImm(BundleFormatRowID::E96ThreeEntry),
            static_cast<unsigned>(BundleFormatRowID::E96ThreeEntry));
  EXPECT_TRUE(isProductBundleRow(BundleFormatRowID::E96TwoEntry));
  EXPECT_TRUE(isProductBundleRow(BundleFormatRowID::E96ThreeEntry));
}

TEST(HaydnBundleVerifyTest, DualLoadMayPack) {
  // Dual LD32 is product-legal under E2: S_LW_WITH_IMM has e0/LOADSTORE0 and
  // e1/LOAD1 members, so leading-order assignment e0+e1 is unit-injective.
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::LD32, Haydn::LD32}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 2u);
}

TEST(HaydnBundleVerifyTest, LdPlusMacIndependentOk) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  // LD32 + multi-slot MAC family — typical DSP density pack.
  // S_LW_WITH_IMM@e0/LOADSTORE0 + X2MULA32@e1/MAC1.
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96TwoEntry, {Haydn::LD32, Haydn::X2MULA32}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 2u);
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
}

TEST(HaydnBundleVerifyTest, EncodedBytesAlwaysProductParcelOnSuccess) {
  HaydnMCFormats Fmts;
  // Named arrays: ArrayRef{a,b} inside a range-for initializer_list dangles
  // after the inner list dies; Wave 2 unit-cover then indexes name tables
  // with garbage opcodes (SEGV). Peer: MCInstrInfo::getName (MCInstrInfo.h:71).
  const unsigned Nop[] = {Haydn::NOP};
  const unsigned Add[] = {Haydn::ADD32};
  const unsigned LdAdd[] = {Haydn::ADD32, Haydn::LD32};
  const unsigned Triple[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32};
  const ArrayRef<unsigned> Cases[] = {Nop, Add, LdAdd, Triple};
  for (ArrayRef<unsigned> Ops : Cases) {
    BundlePlan Plan;
    BundleFormatRowID Row = Ops.size() >= 3 ? BundleFormatRowID::E96ThreeEntry
                                            : BundleFormatRowID::E96TwoEntry;
    auto Err = verifyCommittedBundle(Row, Ops, Fmts, &Plan);
    if (Err)
      continue; // some NOP/slot combos may reject; only check successes
    EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
    EXPECT_EQ(Plan.Cycles.Value, 1u);
    EXPECT_TRUE(isProductBundleRow(Plan.Row));
  }
}

TEST(HaydnBundleVerifyTest, PlanRegistryBytesMatchInverseOcc) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ADD32, Haydn::LD32}, Fmts, &Plan);
  ASSERT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
  EXPECT_TRUE(isProductBundleRow(Plan.Row));
}

// Verifier OutPlan carries registry product bytes from the inverse matrix.
TEST(HaydnBundleVerifyTest, OutPlanBytesFromProductParcel) {
  // OutPlan EncodedBytes follow productParcelBytes / registry.
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  auto GenBytes = productEncodedBytesFromPackets(Packets);
  ASSERT_TRUE(GenBytes.has_value());
  EXPECT_EQ(*GenBytes, productParcelBytes());

  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ADD32, Haydn::LD32}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_EQ(Plan.Bytes, productParcelBytes());
  EXPECT_EQ(Plan.Bytes, *GenBytes);
  EXPECT_TRUE(Plan.isProductLegal());
  ASSERT_EQ(Plan.memberCount(), 2u);
  EXPECT_EQ(Plan.MemberOpcodes[0], Haydn::ADD32);
  EXPECT_EQ(Plan.MemberOpcodes[1], Haydn::LD32);
}

TEST(HaydnBundleVerifyTest, ThreeEntryTripleAluOkLeadingOrder) {
  // Sequential ADD32 + ADD64 + ADD64 is legal under product E3 in leading
  // membership order (ALU members exist at e0/e1/e2 with distinct units).
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96ThreeEntry,
      {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 3u);
  EXPECT_EQ(Plan.Row, BundleFormatRowID::E96ThreeEntry);
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
}

//===----------------------------------------------------------------------===//
// Exhaustive ≤3 verifier inverse matrix (independent — no oracle parity)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleVerifyTest, VF24_ExhaustiveLe3LeadingOrderInverse) {
  // The independent verifier checks the COMMITTED order only: leading
  // membership entry assignment via the generated member table. A sequence
  // whose committed order has no member at its entry fails closed even when
  // some PERMUTATION would pack (that permutation belongs to commit, never
  // to verify). E2{ADD32, ADD32} is the canonical case: both are e0-only in
  // E2, so E2 rejects while E3 admits e0+e1.
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  static constexpr unsigned Alpha[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ST32,
                                       Haydn::LD32, Haydn::X2MULA32};
  unsigned Checked = 0;

  auto CheckSeq = [&](ArrayRef<unsigned> Seq) {
    ++Checked;
    BundlePlan Plan;
    BundleFormatRowID Row = Seq.size() >= 3 ? BundleFormatRowID::E96ThreeEntry
                                            : BundleFormatRowID::E96TwoEntry;
    auto Err = verifyCommittedBundle(Row, Seq, Fmts, &Plan);
    if (!Err) {
      EXPECT_TRUE(Plan.isProductLegal());
      EXPECT_EQ(Plan.memberCount(), Seq.size());
      EXPECT_TRUE(isProductBundleRow(Plan.Row));
      EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
    } else {
      // Every reject names its inverse reason — never a forward-solver
      // defect message.
      EXPECT_TRUE(Err->find("unit injectivity") != std::string::npos ||
                  Err->find("no generated member") != std::string::npos ||
                  Err->find("ISSUE_SLOT_COUNT") != std::string::npos ||
                  Err->find("entry capacity") != std::string::npos ||
                  Err->find("entry mismatch") != std::string::npos)
          << *Err;
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
}

TEST(HaydnBundleVerifyTest, E2TwoAlu32FailClosedIndependence) {
  // P7 independence pin: {ADD32, XOR32} under E2 is golden-illegal (both are
  // e0-only in E2) and the OLD verifier accepted it via the forward canAdd
  // oracle (residual FieldSlots covered S0|S1). The independent inverse must
  // reject it — this is the exact shape the oracle validated itself on.
  HaydnMCFormats Fmts;
  auto E2 = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                  {Haydn::ADD32, Haydn::XOR32}, Fmts);
  ASSERT_TRUE(E2.has_value());
  EXPECT_NE(E2->find("no generated member"), std::string::npos) << *E2;

  // Same members under E3 (e0/ALU2 + e1/ALU1) verify — Finalize restamps.
  // Opcode-only path cannot see leftover implicit-def $sfr; the MI-overload
  // skip is llvm/test/CodeGen/Haydn/d14-freeze-hazard-predicates.mir SFR.
  BundlePlan Plan;
  auto E3 = verifyCommittedBundle(BundleFormatRowID::E96ThreeEntry,
                                  {Haydn::ADD32, Haydn::XOR32}, Fmts, &Plan);
  EXPECT_FALSE(E3.has_value()) << (E3 ? *E3 : "");
  EXPECT_TRUE(Plan.isProductLegal());
}

TEST(HaydnBundleVerifyTest, VF24_ClosestLegalIllegalVerifierPins) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;

  // Closest legal / illegal around LOADSTORE0 exclusivity.
  {
    // ADD32@e0/ALU0 then S_SW_WITH_IMM@e1: store has no e1 member → reject.
    auto Ok = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                    {Haydn::ADD32, Haydn::ST32}, Fmts);
    ASSERT_TRUE(Ok.has_value()) << (Ok ? *Ok : "");
    EXPECT_NE(Ok->find("no generated member"), std::string::npos) << *Ok;
    // S_SW_WITH_IMM@e0/LOADSTORE0 then ADD32@e1: ADD32 has no E2 e1 member
    // → reject. Two ALU32s under E2 always reject (both e0-only).
    auto Bad = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                     {Haydn::ST32, Haydn::ADD32}, Fmts);
    ASSERT_TRUE(Bad.has_value());
    auto Bad2 = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                      {Haydn::ST32, Haydn::ST32}, Fmts);
    ASSERT_TRUE(Bad2.has_value());
    EXPECT_NE(Bad2->find("unit injectivity"), std::string::npos) << *Bad2;
  }

  // Issue-width boundary: four members always rejected.
  {
    auto Four = verifyCommittedBundle(
        BundleFormatRowID::E96ThreeEntry,
        {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32}, Fmts);
    ASSERT_TRUE(Four.has_value());
    EXPECT_NE(Four->find("ISSUE_SLOT_COUNT"), std::string::npos) << *Four;
  }

  // Dual ST never co-issues (LOADSTORE0 exclusive, e0-only).
  {
    auto Bad = verifyCommittedBundle(
        BundleFormatRowID::E96ThreeEntry,
        {Haydn::ST32, Haydn::ST32, Haydn::ADD32}, Fmts);
    ASSERT_TRUE(Bad.has_value());
    EXPECT_NE(Bad->find("unit injectivity"), std::string::npos) << *Bad;
  }

  // Three-entry product row admits three ALU32s at e0/e1/e2 (distinct
  // units) — the legal committed shape.
  EXPECT_EQ(selectProductRowForMemberCount(3),
            BundleFormatRowID::E96ThreeEntry);
  {
    BundlePlan Plan;
    auto Three = verifyCommittedBundle(
        BundleFormatRowID::E96ThreeEntry,
        {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32}, Fmts, &Plan);
    EXPECT_FALSE(Three.has_value()) << (Three ? *Three : "");
    EXPECT_TRUE(Plan.isProductLegal());
  }
}

TEST(HaydnBundleVerifyTest, ResidualLogicalUsesInverseRecordNotStamper) {
  // Residual/logical roots complete independently generated FormatEInverse
  // records at the membership entry. findFormatEMember is the stamper/UnitMap
  // helper — verify must not accept via that or any-cover at the same entry.
  // Entry is membership position — never a peeled `_S*` / `_E3_` suffix.
  HaydnMCFormats Fmts;

  BundlePlan Plan;
  auto Exact = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                     {Haydn::WFI}, Fmts, &Plan);
  EXPECT_FALSE(Exact.has_value()) << (Exact ? *Exact : "");
  EXPECT_EQ(Plan.Completion, expectedGoldenRowCompletion(1, false));

  // Catalog token is WFI<TBD>; the public opcode name is not a stamper key.
  // Opcode-keyed inverse still completes WFI via generated member→logical.
  EXPECT_EQ(haydn::format_e::findFormatEMember(
                "WFI", /*Mode=*/0, /*EntryIdx=*/0, /*UsedUnitMask=*/0),
            nullptr)
      << "WFI opcode name is not the catalog stamper key";
  EXPECT_NE(haydn::format_e::findFormatEMember(
                "WFI<TBD>", /*Mode=*/0, /*EntryIdx=*/0, /*UsedUnitMask=*/0),
            nullptr)
      << "stamper catalog token remains WFI<TBD>";

  // X2SLT32 has no E2 e1 inverse record — membership entry 1 must fail closed
  // even though other non-NOP inverse rows exist at E2 e1 (any-cover).
  auto Misplaced = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                         {Haydn::ADD32, Haydn::X2SLT32}, Fmts);
  ASSERT_TRUE(Misplaced.has_value());
  EXPECT_TRUE(Misplaced->find("no generated member") != std::string::npos ||
              Misplaced->find("unit injectivity") != std::string::npos)
      << *Misplaced;

  bool AnyCoverE2E1 = false;
  for (unsigned J = 0; J < haydn::format_e::FormatEMemberCount; ++J) {
    const haydn::format_e::FormatEInverseRec &R =
        haydn::format_e::FormatEInverse[J];
    if (R.Mode == 0 && R.EntryIdx == 1 && R.Logical && R.Logical[0] != '\0' &&
        !StringRef(R.Logical).equals_insensitive("NOP") &&
        haydn::format_e::completeInverseRecord(R)) {
      AnyCoverE2E1 = true;
      break;
    }
  }
  EXPECT_TRUE(AnyCoverE2E1)
      << "residual/logical verify must not treat any-cover as exact-entry";
}

static MCInst mcRR(unsigned Opc, unsigned Rd, unsigned Rs, unsigned Rt) {
  MCInst I;
  I.setOpcode(Opc);
  I.addOperand(MCOperand::createReg(Rd));
  I.addOperand(MCOperand::createReg(Rs));
  I.addOperand(MCOperand::createReg(Rt));
  return I;
}

TEST(HaydnBundleVerifyTest, ParseTimeSingletonAdd32Ok) {
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  MCInst Add = mcRR(Haydn::ADD32, Haydn::R1, Haydn::R2, Haydn::R3);
  const MCInst *Entries[] = {&Add, nullptr};
  auto Err = verifyParsedBundle(BundleFormatRowID::E96TwoEntry, Entries, Fmts,
                                MII, nullptr);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
}

TEST(HaydnBundleVerifyTest, ParseTimeRejectsDualStoreUnitInjectivity) {
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  MCInst St0;
  St0.setOpcode(Haydn::ST32);
  St0.addOperand(MCOperand::createReg(Haydn::R1));
  St0.addOperand(MCOperand::createReg(Haydn::R2));
  St0.addOperand(MCOperand::createImm(0));
  MCInst St1 = St0;
  St1.getOperand(0).setReg(Haydn::R3);
  const MCInst *Entries[] = {&St0, &St1};
  auto Err = verifyParsedBundle(BundleFormatRowID::E96TwoEntry, Entries, Fmts,
                                MII, nullptr);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("unit injectivity"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, ParseTimeRejectsSameRegWAW) {
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  MCInst A = mcRR(Haydn::ADD32, Haydn::R4, Haydn::R0, Haydn::R1);
  MCInst B = mcRR(Haydn::XOR32, Haydn::R4, Haydn::R2, Haydn::R3);
  const MCInst *Entries[] = {&A, &B, nullptr};
  auto Err = verifyParsedBundle(BundleFormatRowID::E96ThreeEntry, Entries, Fmts,
                                MII, nullptr);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("WAW"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, ParseTimeRejectsThreeGprWritesPortBudget) {
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  MCInst A = mcRR(Haydn::ADD32, Haydn::R4, Haydn::R0, Haydn::R1);
  MCInst B = mcRR(Haydn::XOR32, Haydn::R5, Haydn::R2, Haydn::R3);
  MCInst C = mcRR(Haydn::OR32, Haydn::R6, Haydn::R7, Haydn::R8);
  const MCInst *Entries[] = {&A, &B, &C};
  auto Err = verifyParsedBundle(BundleFormatRowID::E96ThreeEntry, Entries, Fmts,
                                MII, nullptr);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("port demand"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, GeneratedImmTypeNamesMatchGoldenWidth) {
  auto expectType = [](const char *Logical, const char *Type) {
    bool Found = false;
    for (unsigned I = 0; I < haydn::format_e::FormatEMemberCount; ++I) {
      const haydn::format_e::FormatEMemberRec &M =
          haydn::format_e::FormatEMembers[I];
      if (!M.Logical || M.IsNop)
        continue;
      if (!StringRef(M.Logical).equals_insensitive(Logical))
        continue;
      EXPECT_STREQ(M.TypeName, Type) << Logical << " member " << M.MemberSymbol;
      Found = true;
      break;
    }
    EXPECT_TRUE(Found) << Logical << " missing from generated members";
  };
  expectType("ANDI32", "RI20");
  expectType("ORI32", "RI20");
  expectType("XORI32", "RI20");
  expectType("SIN_COS", "RI4");
  expectType("ARCTAN", "RI4");
  expectType("D_LDW_CB_IMM", "CBRI");
  expectType("D_SDW_CB_IMM", "CBRI");
}

TEST(HaydnBundleVerifyTest, ResidualFieldSlotsRemainUntilGoldenSpan) {
  // SIMD SFR-only S1 FieldSlots are retired: e1 has no 0-def member
  // (reject-not-migrate). Occupancy fills e0/e2 from generated members.
  // WFI FieldSlot is retired onto the generated HINT span.
  EXPECT_NE(Haydn::WFITBDTBDTBD_E2_E0_ALU0_HINT, Haydn::WFI);
}

TEST(HaydnBundleVerifyTest, ResidualLogicalRequiresCompletedInverseRecord) {
  HaydnMCFormats Fmts;

  BundlePlan Plan;
  auto Add = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ADD32}, Fmts, &Plan);
  EXPECT_FALSE(Add.has_value()) << (Add ? *Add : "");
  EXPECT_EQ(Plan.Completion, expectedGoldenRowCompletion(1, false));
  EXPECT_EQ(Plan.Completion, CompletionStateID::AllEntriesReal);

  auto Loadi = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                     {Haydn::LOADI32}, Fmts);
  ASSERT_TRUE(Loadi.has_value());
  EXPECT_NE(Loadi->find("residual/logical inverse record not completed"),
            std::string::npos)
      << *Loadi;

  auto Loadi64 = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                       {Haydn::LOADI64}, Fmts);
  ASSERT_TRUE(Loadi64.has_value());
  EXPECT_NE(Loadi64->find("residual/logical inverse record not completed"),
            std::string::npos)
      << *Loadi64;

  auto PostInc = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                       {Haydn::LD32_POST_INC}, Fmts);
  ASSERT_TRUE(PostInc.has_value());
  EXPECT_NE(PostInc->find("residual/logical inverse record not completed"),
            std::string::npos)
      << *PostInc;

  auto Mov = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::MOV_GPR_TO_DR64}, Fmts);
  ASSERT_TRUE(Mov.has_value());
  EXPECT_NE(Mov->find("residual/logical inverse record not completed"),
            std::string::npos)
      << *Mov;

  auto MovDr = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                     {Haydn::MOV_DR64_TO_GPR}, Fmts);
  ASSERT_TRUE(MovDr.has_value());
  EXPECT_NE(MovDr->find("residual/logical inverse record not completed"),
            std::string::npos)
      << *MovDr;

  auto Copy = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                    {TargetOpcode::COPY}, Fmts);
  ASSERT_TRUE(Copy.has_value());
  EXPECT_TRUE(Copy->find("no generated member") != std::string::npos ||
              Copy->find("residual/logical inverse record not completed") !=
                  std::string::npos)
      << *Copy;
}

// D1.39 co-issued COPY law: a COPY can never occupy a Format E entry in a
// committed cycle. The singleton reject above is not the whole law — a future
// packer path could try to co-issue a COPY beside a real member. Every
// co-issued shape must fail closed here (the verify/freeze seat is the law
// owner; see the three-wall comment at isBundleSkippable in
// HaydnPostRASchedStrategy.cpp).
TEST(HaydnBundleVerifyTest, CoissuedCopyMemberFailClosed) {
  HaydnMCFormats Fmts;

  // Real member + COPY kid under E2.
  auto AddCopy = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                       {Haydn::ADD32, TargetOpcode::COPY},
                                       Fmts);
  ASSERT_TRUE(AddCopy.has_value());
  EXPECT_TRUE(AddCopy->find("no generated member") != std::string::npos ||
              AddCopy->find("residual/logical inverse record not completed") !=
                  std::string::npos)
      << *AddCopy;

  // COPY + COPY under E2: no member of the cycle has a Format E identity.
  auto CopyCopy = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                        {TargetOpcode::COPY,
                                         TargetOpcode::COPY}, Fmts);
  ASSERT_TRUE(CopyCopy.has_value());
  EXPECT_TRUE(CopyCopy->find("no generated member") != std::string::npos ||
              CopyCopy->find("residual/logical inverse record not completed") !=
                  std::string::npos)
      << *CopyCopy;

  // E3 three-member shape with a trailing COPY kid: still fail closed —
  // entry capacity and unit injectivity are never reached because the COPY
  // has no inverse key at membership entry.
  auto TripleCopy =
      verifyCommittedBundle(BundleFormatRowID::E96ThreeEntry,
                            {Haydn::ADD32, Haydn::XOR32, TargetOpcode::COPY},
                            Fmts);
  ASSERT_TRUE(TripleCopy.has_value());
  EXPECT_TRUE(TripleCopy->find("no generated member") != std::string::npos ||
              TripleCopy->find("residual/logical inverse record not completed") !=
                  std::string::npos)
      << *TripleCopy;
}

TEST(HaydnBundleVerifyTest, PublicMnemonicInverseNotPseudoAlias) {
  // Real public mnemonics whose catalog Logical differs (LD32 vs
  // S_LW_WITH_IMM) still complete generated inverse records. MC-pseudo
  // leftovers cannot borrow that extra key.
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Ld = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                  {Haydn::LD32}, Fmts, &Plan);
  EXPECT_FALSE(Ld.has_value()) << (Ld ? *Ld : "");
  EXPECT_EQ(Plan.Completion, expectedGoldenRowCompletion(1, false));

  auto Sext = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                    {Haydn::SEXT_GPR32_TO_DR64}, Fmts);
  EXPECT_FALSE(Sext.has_value()) << (Sext ? *Sext : "");
}

TEST(HaydnBundleVerifyTest, InverseRecordMutationFailClosed) {
  const haydn::format_e::FormatEInverseRec *Inv =
      haydn::format_e::inverseRecordForMemberId(1);
  ASSERT_NE(Inv, nullptr);
  EXPECT_TRUE(haydn::format_e::completeInverseRecord(*Inv));

  haydn::format_e::FormatEInverseRec Mut = *Inv;
  Mut.EntryIdx = static_cast<uint8_t>(Mut.EntryIdx == 0 ? 1 : 0);
  EXPECT_FALSE(haydn::format_e::completeInverseRecord(Mut));

  Mut = *Inv;
  Mut.Unit = static_cast<uint8_t>(Mut.Unit ^ 1u);
  EXPECT_FALSE(haydn::format_e::completeInverseRecord(Mut));

  Mut = *Inv;
  Mut.Mode = static_cast<uint8_t>(Mut.Mode ^ 1u);
  EXPECT_FALSE(haydn::format_e::completeInverseRecord(Mut));
}

TEST(HaydnBundleVerifyTest, ParseTimeResidualLogicalRequiresCompletedInverse) {
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();

  auto parseOne = [&](unsigned Opc) {
    MCInst I;
    I.setOpcode(Opc);
    const MCInst *Entries[] = {&I, nullptr};
    return verifyParsedBundle(BundleFormatRowID::E96TwoEntry, Entries, Fmts,
                              MII, nullptr);
  };

  auto Loadi = parseOne(Haydn::LOADI32);
  ASSERT_TRUE(Loadi.has_value());
  EXPECT_NE(Loadi->find("residual/logical inverse record not completed"),
            std::string::npos)
      << *Loadi;

  auto PostInc = parseOne(Haydn::LD32_POST_INC);
  ASSERT_TRUE(PostInc.has_value());
  EXPECT_NE(PostInc->find("residual/logical inverse record not completed"),
            std::string::npos)
      << *PostInc;

  auto Mov = parseOne(Haydn::MOV_GPR_TO_DR64);
  ASSERT_TRUE(Mov.has_value());
  EXPECT_NE(Mov->find("residual/logical inverse record not completed"),
            std::string::npos)
      << *Mov;

  auto Copy = parseOne(TargetOpcode::COPY);
  ASSERT_TRUE(Copy.has_value());
  EXPECT_TRUE(Copy->find("no generated member") != std::string::npos ||
              Copy->find("residual/logical inverse record not completed") !=
                  std::string::npos)
      << *Copy;

  // D1.39 co-issued COPY law at the MC-parse seat: a COPY entry beside a
  // real member must fail closed (verifyParsedBundle walks the same inverse
  // membership as verifyCommittedBundle).
  MCInst RealAdd = mcRR(Haydn::ADD32, Haydn::R1, Haydn::R2, Haydn::R3);
  MCInst CopyEntry;
  CopyEntry.setOpcode(TargetOpcode::COPY);
  const MCInst *Mixed[] = {&RealAdd, &CopyEntry};
  auto MixedErr = verifyParsedBundle(BundleFormatRowID::E96TwoEntry, Mixed,
                                     Fmts, MII, nullptr);
  ASSERT_TRUE(MixedErr.has_value());
  EXPECT_TRUE(MixedErr->find("no generated member") != std::string::npos ||
              MixedErr->find("residual/logical inverse record not completed") !=
                  std::string::npos)
      << *MixedErr;

  MCInst Add = mcRR(Haydn::ADD32, Haydn::R1, Haydn::R2, Haydn::R3);
  const MCInst *Ok[] = {&Add, nullptr};
  auto AddErr = verifyParsedBundle(BundleFormatRowID::E96TwoEntry, Ok, Fmts,
                                   MII, nullptr);
  EXPECT_FALSE(AddErr.has_value()) << (AddErr ? *AddErr : "");
}

TEST(HaydnBundleVerifyTest, LeadingPadDoesNotReplanResidualLogical) {
  // Independent inverse must not compact a leading pad so ADD32 (E2 e0-only)
  // is accepted at entry 1. Pad is an unused encode-dag window, not a
  // membership re-plan. Peer: AIE unused format entry is idle
  // (AIEMCFormats.h:376-379), not an alternate opcode.
  HaydnMCFormats Fmts;
  auto LeadPad = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                       {Haydn::NOP, Haydn::ADD32}, Fmts);
  ASSERT_TRUE(LeadPad.has_value());
  EXPECT_TRUE(LeadPad->find("no generated member") != std::string::npos ||
              LeadPad->find("residual/logical inverse record not completed") !=
                  std::string::npos)
      << *LeadPad;

  BundlePlan Trailing;
  auto TrailPad = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                        {Haydn::ADD32, Haydn::NOP}, Fmts,
                                        &Trailing);
  EXPECT_FALSE(TrailPad.has_value()) << (TrailPad ? *TrailPad : "");
  EXPECT_EQ(Trailing.memberCount(), 1u);
  EXPECT_EQ(Trailing.Completion, CompletionStateID::AllEntriesReal);

  auto ExtraPads = verifyCommittedBundle(
      BundleFormatRowID::E96TwoEntry, {Haydn::NOP, Haydn::NOP, Haydn::NOP},
      Fmts);
  ASSERT_TRUE(ExtraPads.has_value());
  EXPECT_NE(ExtraPads->find("entry count"), std::string::npos) << *ExtraPads;
}

TEST(HaydnBundleVerifyTest, CycleFormingResidualRootRequiresInverse) {
  HaydnMCFormats Fmts;
  const unsigned Residuals[] = {
      Haydn::LoopStart, Haydn::SET_HWLOOP, Haydn::SET_HWLOOP_REG,
      Haydn::LOAD_ADDR, TargetOpcode::INSERT_SUBREG,
      TargetOpcode::EXTRACT_SUBREG, TargetOpcode::SUBREG_TO_REG,
      TargetOpcode::REG_SEQUENCE,
  };
  for (unsigned Opc : Residuals) {
    auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry, {Opc},
                                     Fmts);
    ASSERT_TRUE(Err.has_value()) << "opc=" << Opc;
    EXPECT_NE(Err->find("residual/logical inverse record not completed"),
              std::string::npos)
        << "opc=" << Opc << " diag=" << *Err;
  }
}

TEST(HaydnBundleVerifyTest, ParseTimeLeadingHoleDoesNotReplanAdd32) {
  HaydnMCFormats Fmts;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  MCInst Add = mcRR(Haydn::ADD32, Haydn::R1, Haydn::R2, Haydn::R3);
  const MCInst *LeadHole[] = {nullptr, &Add};
  auto Err = verifyParsedBundle(BundleFormatRowID::E96TwoEntry, LeadHole, Fmts,
                                MII, nullptr);
  ASSERT_TRUE(Err.has_value());
  EXPECT_TRUE(Err->find("no generated member") != std::string::npos ||
              Err->find("residual/logical inverse record not completed") !=
                  std::string::npos)
      << *Err;

  const MCInst *TrailHole[] = {&Add, nullptr};
  auto Ok = verifyParsedBundle(BundleFormatRowID::E96TwoEntry, TrailHole, Fmts,
                               MII, nullptr);
  EXPECT_FALSE(Ok.has_value()) << (Ok ? *Ok : "");
}

TEST(HaydnBundleVerifyTest, LookupPrivateMemberUsesCompletedInverse) {
  const haydn::format_e::FormatEMemberRec *Add =
      lookupPrivateFormatEMember(Haydn::ADD32_E2_E0_ALU0_RR);
  ASSERT_NE(Add, nullptr);
  const haydn::format_e::FormatEInverseRec *Inv =
      haydn::format_e::inverseRecordForMemberId(Add->MemberId);
  ASSERT_NE(Inv, nullptr);
  EXPECT_TRUE(haydn::format_e::completeInverseRecord(*Inv));
  EXPECT_EQ(Inv->MemberId, Add->MemberId);

  EXPECT_EQ(lookupPrivateFormatEMember(Haydn::ADD32), nullptr);
  EXPECT_EQ(lookupPrivateFormatEMember(Haydn::LOADI32), nullptr);
  EXPECT_EQ(lookupPrivateFormatEMember(Haydn::MOV_GPR_TO_DR64), nullptr);
}

TEST(HaydnBundleVerifyTest, RejectsMixedLogicalAndPrivateChildren) {
  HaydnMCFormats Fmts;
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96TwoEntry,
      {Haydn::ADD32, Haydn::ADDI32_E2_E1_ALU1_RI20}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("mixed logical and private"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, FreezeRejectsResidualLogicalAcceptsPrivate) {
  HaydnMCFormats Fmts;
  BundlePlan LogicalPlan;
  auto Logical = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                       {Haydn::ADD32}, Fmts, &LogicalPlan,
                                       /*Freeze=*/false);
  EXPECT_FALSE(Logical.has_value()) << (Logical ? *Logical : "");

  auto FreezeLogical = verifyCommittedBundle(
      BundleFormatRowID::E96TwoEntry, {Haydn::ADD32}, Fmts, nullptr,
      /*Freeze=*/true);
  ASSERT_TRUE(FreezeLogical.has_value());
  EXPECT_NE(FreezeLogical->find("freeze residual logical"), std::string::npos)
      << *FreezeLogical;

  BundlePlan PrivPlan;
  auto FreezePriv = verifyCommittedBundle(
      BundleFormatRowID::E96TwoEntry, {Haydn::ADD32_E2_E0_ALU0_RR}, Fmts,
      &PrivPlan, /*Freeze=*/true);
  EXPECT_FALSE(FreezePriv.has_value()) << (FreezePriv ? *FreezePriv : "");
  EXPECT_TRUE(PrivPlan.isProductLegal());
}

//===----------------------------------------------------------------------===//
// gMIR encode overlays (JAL_TCO / B) traverse the structural inverse (D1.18/D1.130)
//===----------------------------------------------------------------------===//

TEST(HaydnBundleVerifyTest, CloneOnlyBundleVerifiesStructurally) {
  // A clone-only bundle is NOT a vacuous idle plan: the clone is a Real, so
  // the walk runs, Occupied comes from SeenEntryBits (bit set), and
  // Completion is golden-row fill over the real member count. Before D1.18
  // the clone skip emptied Reals and this exact committed shape returned
  // Occupied=0/StubIdle from the idle-plan early return.
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::JAL_TCO}, Fmts, &Plan);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 1u);
  EXPECT_NE(Plan.OccupiedSlots, 0u) << "clone-only plan must not be idle";
  EXPECT_EQ(Plan.Completion, CompletionStateID::AllEntriesReal);
  EXPECT_EQ(Plan.Completion, expectedGoldenRowCompletion(1, false));

  // Same law for the uncond-barrier clone at its stamped E2 entry 0.
  BundlePlan BrPlan;
  auto BrErr = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                     {Haydn::B}, Fmts, &BrPlan);
  EXPECT_FALSE(BrErr.has_value()) << (BrErr ? *BrErr : "");
  EXPECT_TRUE(BrPlan.isProductLegal());
  EXPECT_EQ(BrPlan.memberCount(), 1u);
  EXPECT_NE(BrPlan.OccupiedSlots, 0u);
  EXPECT_EQ(BrPlan.Completion, CompletionStateID::AllEntriesReal);

  // Freeze admission is unchanged (clones admitted) but now structurally
  // verified: freeze on a clone-only bundle still succeeds.
  BundlePlan FreezePlan;
  auto FreezeErr =
      verifyCommittedBundle(BundleFormatRowID::E96TwoEntry, {Haydn::JAL_TCO},
                            Fmts, &FreezePlan, /*Freeze=*/true);
  EXPECT_FALSE(FreezeErr.has_value()) << (FreezeErr ? *FreezeErr : "");
  EXPECT_TRUE(FreezePlan.isProductLegal());
}

TEST(HaydnBundleVerifyTest, CloneOnlyBundleMemberlessEntryFails) {
  // JAL's golden span is E2 e0 / E3 e0 / E3 e1 — entry 2 has no member.
  // Two pad NOPs put the clone at membership entry 2 of an E3 row; pads are
  // unused windows (never compacted), so the walk must fail closed at the
  // stamped entry. This exact bundle was silently accepted before D1.18.
  HaydnMCFormats Fmts;
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96ThreeEntry,
      {Haydn::NOP, Haydn::NOP, Haydn::JAL_TCO}, Fmts);
  ASSERT_TRUE(Err.has_value()) << "memberless clone entry must fail";
  EXPECT_NE(Err->find("no generated member"), std::string::npos) << *Err;
  EXPECT_NE(Err->find("JAL_TCO"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, CloneAtE2Entry1Fails) {
  // BEQZ has no mode-0 entry-1 member (E2 span is e0 only; the E3 cover
  // keeps the unit-cover pre-check quiet), so the failure comes from the
  // walk's mode/entry match — the walk diagnostic, not "unit-injective".
  HaydnMCFormats Fmts;
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96TwoEntry, {Haydn::ADD32, Haydn::B},
      Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("no generated member"), std::string::npos) << *Err;
  EXPECT_NE(Err->find("B"), std::string::npos) << *Err;
}

TEST(HaydnBundleVerifyTest, UnmappedMspFailsClosed) {
  // ADD32_MSP is `_MSP`-named with no catalog mapping: it has no inverse
  // ids, so its unit mask is 0 and the unit-cover pre-check refuses it as
  // a mask-0 singleton ("Unknown opcodes have mask 0 and must not pass —
  // even as a singleton") BEFORE the walk. Fail-closed either way;
  // materialize/leaveRegion setDesc baking is the only legal way such
  // pseudos reach commit (postmisched-msp-pack-reconstruct.mir asserts
  // zero residual ADD32_MSP).
  HaydnMCFormats Fmts;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ADD32_MSP}, Fmts);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("unit-injective"), std::string::npos) << *Err;
}

class HaydnFreezeLayoutTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;
  MachineBasicBlock *BB0 = nullptr;
  MachineBasicBlock *BB1 = nullptr;
  MachineBasicBlock *BB2 = nullptr;

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
    M = std::make_unique<Module>("HaydnFreezeLayout", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    if (!MF->getInfo<HaydnMachineFunctionInfo>())
      MF->initTargetMachineFunctionInfo(*ST);
    BB0 = MF->CreateMachineBasicBlock();
    BB1 = MF->CreateMachineBasicBlock();
    BB2 = MF->CreateMachineBasicBlock();
    MF->push_back(BB0);
    MF->push_back(BB1);
    MF->push_back(BB2);
    BB0->addSuccessor(BB1);
    BB1->addSuccessor(BB2);
  }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }

  MachineInstr *addBundleRoot(MachineBasicBlock *BB) {
    return BuildMI(*BB, BB->end(), DebugLoc(), TII().get(TargetOpcode::BUNDLE))
        .addImm(0)
        .addImm(0)
        .getInstr();
  }

  void addNopBundle(MachineBasicBlock *BB) {
    addBundleRoot(BB);
    MachineInstr *Nop =
        BuildMI(*BB, BB->end(), DebugLoc(), TII().get(Haydn::NOP)).getInstr();
    Nop->bundleWithPred();
  }

  void addBeqzBundle(MachineBasicBlock *BB, MachineBasicBlock *Dest) {
    addBundleRoot(BB);
    MachineInstr *Br =
        BuildMI(*BB, BB->end(), DebugLoc(), TII().get(Haydn::BEQZ))
            .addReg(Haydn::R0)
            .addMBB(Dest)
            .getInstr();
    Br->bundleWithPred();
  }
};

TEST_F(HaydnFreezeLayoutTest, SharedCounterFIIsFatal) {
  HaydnMachineFunctionInfo *MFI = MF->getInfo<HaydnMachineFunctionInfo>();
  ASSERT_NE(MFI, nullptr);
  MFI->bindHwLoopStackCounterFI(BB0, /*FI=*/0);
  MFI->bindHwLoopStackCounterFI(BB1, /*FI=*/0);
  auto Err = verifyFrozenLayout(*MF);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("overlapping hwloop counter homes"), std::string::npos)
      << *Err;
}

TEST_F(HaydnFreezeLayoutTest, DistinctCounterFIsOk) {
  HaydnMachineFunctionInfo *MFI = MF->getInfo<HaydnMachineFunctionInfo>();
  ASSERT_NE(MFI, nullptr);
  MFI->bindHwLoopStackCounterFI(BB0, /*FI=*/0);
  MFI->bindHwLoopStackCounterFI(BB1, /*FI=*/1);
  auto Err = verifyFrozenLayout(*MF);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
}

TEST_F(HaydnFreezeLayoutTest, DuplicateControlInPacketIsFatal) {
  addBundleRoot(BB0);
  MachineInstr *A = BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::BEQZ))
                        .addReg(Haydn::R0)
                        .addMBB(BB1)
                        .getInstr();
  A->bundleWithPred();
  MachineInstr *B = BuildMI(*BB0, BB0->end(), DebugLoc(), TII().get(Haydn::BNEZ))
                        .addReg(Haydn::R1)
                        .addMBB(BB1)
                        .getInstr();
  B->bundleWithPred();
  auto Err = verifyFrozenLayout(*MF);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("duplicate-control"), std::string::npos) << *Err;
}

TEST_F(HaydnFreezeLayoutTest, InRangeBeqzDisplacementOk) {
  addBeqzBundle(BB0, BB1);
  addNopBundle(BB1);
  auto Err = verifyFrozenLayout(*MF);
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
}

TEST_F(HaydnFreezeLayoutTest, OutOfRangeBeqzDisplacementIsFatal) {
  // simm12 byte window is ±2048. 171 idle parcels after the BEQZ packet
  // is 12 + 171*12 = 2064, past WIDE_BranchSImm12.
  addBeqzBundle(BB0, BB2);
  for (unsigned I = 0; I < 171; ++I)
    addNopBundle(BB1);
  auto Err = verifyFrozenLayout(*MF);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("relocation offset out of range"), std::string::npos)
      << *Err;
}

TEST_F(HaydnFreezeLayoutTest, ResidualMBBAlignmentIsFatal) {
  addBeqzBundle(BB0, BB1);
  addNopBundle(BB1);
  BB1->setAlignment(Align(16));
  auto Err = verifyFrozenLayout(*MF);
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("residual MBB alignment metadata"), std::string::npos)
      << *Err;
}

} // namespace
