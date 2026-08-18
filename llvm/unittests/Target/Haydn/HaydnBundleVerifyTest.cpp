//===- HaydnBundleVerifyTest.cpp - committed-bundle verify -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for haydn::bundle::verifyCommittedBundle.
//
// AIE structure peers:
//   AIEBundle.h:150-156 getFormatOrNull (hasValidFormat + planFromPacketFormats)
//   AIEHazardRecognizer.cpp:278-312 applyFormatOrdering assert + finalizeBundle
//   AIEBaseInstrInfo.cpp:1440-1459 verifyInstruction fail-closed pattern
//   BundleTest.cpp / HazardRecognizerTest.cpp unit style
//
//===----------------------------------------------------------------------===//

#include "HaydnBundleFormatSolver.h"
#include "HaydnBundleVerify.h"
#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "gtest/gtest.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"
#define GET_REGINFO_ENUM
#include "HaydnGenRegisterInfo.inc"

namespace llvm {
const MCInstrInfo &getHaydnSharedMCInstrInfo();
}

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
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
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
      Haydn::LOADI32,       Haydn::LOAD_ADDR,   Haydn::SETCBR_BEGIN,
      Haydn::SETCBR_END,    Haydn::LoopStart,    Haydn::LoopDec,
      Haydn::LoopJNZ,       Haydn::SET_HWLOOP,   Haydn::SET_HWLOOP_REG,
  };
  for (unsigned Opc : Residuals)
    EXPECT_TRUE(isResidualCycleFormingPseudo(Opc)) << "opc=" << Opc;

  // Product final reals and representation expands are not residual.
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::CSRW_W));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::SUBI32));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::BNEZ_W));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::SET_HWLOOP_W));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::SET_HWLOOP_F2_W));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::ADD32));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::B));
  EXPECT_FALSE(isResidualCycleFormingPseudo(Haydn::RET));

  EXPECT_TRUE(isRepresentationExpandPseudo(Haydn::B));
  EXPECT_TRUE(isRepresentationExpandPseudo(Haydn::RET));
  EXPECT_TRUE(isRepresentationExpandPseudo(Haydn::BR_JT));
  EXPECT_TRUE(isRepresentationExpandPseudo(Haydn::PseudoCALLIndirect));
  EXPECT_FALSE(isRepresentationExpandPseudo(Haydn::LOADI32));
  EXPECT_FALSE(isRepresentationExpandPseudo(Haydn::ADD32));
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

TEST(HaydnBundleVerifyTest, ResidualFieldSlotUsesExactMemberNotAnyCover) {
  // F12: residual `_S*` verify must call findFormatEMember (logical, mode,
  // membership entry), not "any non-NOP inverse exists at that entry".
  // Entry is membership position — never a peeled `_S*` / `_E3_` suffix.
  HaydnMCFormats Fmts;

  const haydn::format_e::FormatEMemberRec *BnezE2E0 =
      haydn::format_e::findFormatEMember(
          "BNEZ", /*Mode=*/0, /*EntryIdx=*/0, /*UsedUnitMask=*/0);
  ASSERT_NE(BnezE2E0, nullptr)
      << "BNEZ_W_S0 residual exact-cover requires a BNEZ E2 e0 member";

  BundlePlan Plan;
  auto Exact = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                     {Haydn::WFI}, Fmts, &Plan);
  EXPECT_FALSE(Exact.has_value()) << (Exact ? *Exact : "");

  // Catalog token is WFI<TBD>; the public opcode name is not the inverse key.
  EXPECT_EQ(haydn::format_e::findFormatEMember(
                "WFI", /*Mode=*/0, /*EntryIdx=*/0, /*UsedUnitMask=*/0),
            nullptr)
      << "WFI opcode name is not the catalog inverse key";
  EXPECT_NE(haydn::format_e::findFormatEMember(
                "WFI<TBD>", /*Mode=*/0, /*EntryIdx=*/0, /*UsedUnitMask=*/0),
            nullptr)
      << "WFI peels onto the generated HINT span";
  EXPECT_EQ(haydn::format_e::findFormatEMember(
                "X2SLT32", /*Mode=*/0, /*EntryIdx=*/1, /*UsedUnitMask=*/0),
            nullptr)
      << "X2SLT32 has no E2 e1 0-def member";

  bool AnyCoverE2E1 = false;
  for (unsigned J = 0; J < haydn::format_e::FormatEMemberCount; ++J) {
    const haydn::format_e::FormatEInverseRec &R =
        haydn::format_e::FormatEInverse[J];
    if (R.Mode == 0 && R.EntryIdx == 1 && R.Logical && R.Logical[0] != '\0' &&
        !StringRef(R.Logical).equals_insensitive("NOP")) {
      AnyCoverE2E1 = true;
      break;
    }
  }
  EXPECT_TRUE(AnyCoverE2E1)
      << "F12 residual must not treat any-cover as exact-entry";
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

} // namespace
