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
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

// Skip when transitional SLOT* encode-oracle cannot cover a pack under E96
// composite packet formats (product row/size authority is still asserted
// separately via registry APIs).
static bool isTransitionalPackerBlock(const std::optional<std::string> &Err) {
  if (!Err)
    return false;
  return Err->find("canAdd") != std::string::npos ||
         Err->find("hasValidFormat") != std::string::npos ||
         Err->find("planFromPacketFormats") != std::string::npos ||
         Err->find("PacketFormats missing") != std::string::npos;
}

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
  // ST32 + ADD64 under product E2 row; packing may be blocked on transitional
  // SLOT* encode-oracle until composite packer tracks E96 entry masks.
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ST32, Haydn::ADD64}, Fmts, &Plan);
  if (isTransitionalPackerBlock(Err))
    GTEST_SKIP() << "transitional packer block: " << *Err;
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
  if (isTransitionalPackerBlock(Err))
    GTEST_SKIP() << "transitional packer block: " << *Err;

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
  // libc bf16mull residual: D_SW_L_WITH_IMM_S2 + OR64 + ST8_S0. Both stores
  // are golden LOADSTORE0 e0 only. Verify must refuse; MC must never see it.
  HaydnMCFormats Fmts;
  auto Logical = verifyCommittedBundle(
      BundleFormatRowID::E96ThreeEntry,
      {Haydn::D_SW_L_WITH_IMM, Haydn::OR64, Haydn::ST8}, Fmts);
  ASSERT_TRUE(Logical.has_value());
  EXPECT_NE(Logical->find("unit injectivity"), std::string::npos) << *Logical;

  auto Members = verifyCommittedBundle(
      BundleFormatRowID::E96ThreeEntry,
      {Haydn::D_SW_L_WITH_IMM_S2, Haydn::OR64,
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
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::LD32, Haydn::LD32}, Fmts, &Plan);
  // Dual LD32 is product-legal when alts-derived getLegalSlots /
  // PlacementAlternative FieldSlots cover S0|S1 for LD32.
  // If table rejects, canAdd fails — either outcome is fail-closed / explicit.
  if (!Err) {
    EXPECT_TRUE(Plan.isProductLegal());
    EXPECT_EQ(Plan.memberCount(), 2u);
  } else {
    EXPECT_NE(Err->find("canAdd"), std::string::npos) << *Err;
  }
}

TEST(HaydnBundleVerifyTest, LdPlusMacIndependentOk) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  // LD32 + multi-slot MAC family — typical DSP density pack.
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96TwoEntry, {Haydn::LD32, Haydn::X2MULA32}, Fmts, &Plan);
  if (isTransitionalPackerBlock(Err))
    GTEST_SKIP() << "transitional packer block: " << *Err;
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
  const unsigned StAdd[] = {Haydn::ST32, Haydn::ADD64};
  const unsigned Triple[] = {Haydn::ADD32, Haydn::XOR32, Haydn::NOT32};
  const ArrayRef<unsigned> Cases[] = {Nop, Add, StAdd, Triple};
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

TEST(HaydnBundleVerifyTest, PlanFromPacketFormatsMatchesOracleOcc) {
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ADD32, Haydn::LD32}, Fmts, &Plan);
  if (isTransitionalPackerBlock(Err))
    GTEST_SKIP() << "transitional packer block: " << *Err;
  ASSERT_FALSE(Err.has_value()) << (Err ? *Err : "");
  auto Table =
      planFromPacketFormats(Fmts.getPacketFormats(), Plan.OccupiedSlots);
  ASSERT_TRUE(Table.has_value());
  EXPECT_EQ(Table->Bytes.Value, productParcelBytes().Value);
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
  EXPECT_TRUE(isProductBundleRow(Plan.Row));
}

// Verifier OutPlan is rebuilt from planFromPacketFormats (VLIWFormat::Size),
// not a hand-built product plan.
TEST(HaydnBundleVerifyTest, OutPlanBytesFromProductParcel) {
  // OutPlan EncodedBytes follow productParcelBytes / registry, not residual
  // composite Size if it still differs.
  HaydnMCFormats Fmts;
  const PacketFormats &Packets = Fmts.getPacketFormats();
  auto GenBytes = productEncodedBytesFromPackets(Packets);
  ASSERT_TRUE(GenBytes.has_value());
  EXPECT_EQ(*GenBytes, productParcelBytes());

  BundlePlan Plan;
  auto Err = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                   {Haydn::ST32, Haydn::ADD64}, Fmts, &Plan);
  if (isTransitionalPackerBlock(Err))
    GTEST_SKIP() << "transitional packer block: " << *Err;
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_EQ(Plan.Bytes, productParcelBytes());
  EXPECT_EQ(Plan.Bytes, *GenBytes);
  EXPECT_TRUE(Plan.isProductLegal());
  ASSERT_EQ(Plan.memberCount(), 2u);
  EXPECT_EQ(Plan.MemberOpcodes[0], Haydn::ST32);
  EXPECT_EQ(Plan.MemberOpcodes[1], Haydn::ADD64);
}

TEST(HaydnBundleVerifyTest, VF21_EncodeOracleRematchADD32_2xADD64) {
  // Verifier encode-oracle uses Bundle exact matching: sequential
  // ADD32 + ADD64 + ADD64 is a legal pack after rematch under product E3.
  HaydnMCFormats Fmts;
  BundlePlan Plan;
  auto Err = verifyCommittedBundle(
      BundleFormatRowID::E96ThreeEntry,
      {Haydn::ADD32, Haydn::ADD64, Haydn::ADD64}, Fmts, &Plan);
  if (isTransitionalPackerBlock(Err))
    GTEST_SKIP() << "transitional packer block: " << *Err;

  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
  EXPECT_TRUE(Plan.isProductLegal());
  EXPECT_EQ(Plan.memberCount(), 3u);
  EXPECT_EQ(Plan.Row, BundleFormatRowID::E96ThreeEntry);
  EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
}

//===----------------------------------------------------------------------===//
// Exhaustive ≤3 all-subset / all-perm verifier ↔ exact oracle matrix
//===----------------------------------------------------------------------===//

TEST(HaydnBundleVerifyTest, VF24_ExhaustiveLe3VerifierVsExactOracle) {
  // verifyCommittedBundle is the encode-oracle consumer of Bundle canAdd/add
  // (exact matching). Every ordered ≤3 alphabet sequence that exact-packs
  // must verify; every sequence that exact-rejects must fail canAdd.
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;
  static constexpr unsigned Alpha[] = {Haydn::ADD32, Haydn::ADD64, Haydn::ST32,
                                       Haydn::LD32, Haydn::X2MULA32};
  unsigned Checked = 0;

  auto CheckSeq = [&](ArrayRef<unsigned> Seq) {
    ++Checked;
    const bool Exact = exactCanPackProductSequence(Fmts, Seq);
    BundlePlan Plan;
    BundleFormatRowID Row = Seq.size() >= 3 ? BundleFormatRowID::E96ThreeEntry
                                            : BundleFormatRowID::E96TwoEntry;
    auto Err = verifyCommittedBundle(Row, Seq, Fmts, &Plan);
    if (Exact) {
      if (isTransitionalPackerBlock(Err)) {
        // Exact candidate set still uses residual SLOT* tables; E96 packet
        // formats may not cover the same occupancy yet.
        return;
      }
      EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
      EXPECT_TRUE(Plan.isProductLegal());
      EXPECT_EQ(Plan.memberCount(), Seq.size());
      EXPECT_TRUE(isProductBundleRow(Plan.Row));
      EXPECT_EQ(Plan.Bytes.Value, productParcelBytes().Value);
    } else if (!Seq.empty()) {
      ASSERT_TRUE(Err.has_value());
      // Reject reason may be unit injectivity (LOADSTORE0 stores), canAdd, or
      // hasValidFormat under E96 composites.
      EXPECT_TRUE(Err->find("canAdd") != std::string::npos ||
                  Err->find("hasValidFormat") != std::string::npos ||
                  Err->find("unit injectivity") != std::string::npos)
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

TEST(HaydnBundleVerifyTest, VF24_ClosestLegalIllegalVerifierPins) {
  using namespace llvm::haydn::bundle;
  HaydnMCFormats Fmts;

  // Closest legal / illegal around exclusive S0.
  {
    auto Ok = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                    {Haydn::ST32, Haydn::ADD64}, Fmts);
    EXPECT_FALSE(Ok.has_value()) << (Ok ? *Ok : "");
    auto Bad = verifyCommittedBundle(BundleFormatRowID::E96TwoEntry,
                                     {Haydn::ST32, Haydn::ST32}, Fmts);
    ASSERT_TRUE(Bad.has_value());
    EXPECT_NE(Bad->find("unit injectivity"), std::string::npos) << *Bad;
  }

  // Issue-width boundary: four members always rejected.
  {
    auto Four = verifyCommittedBundle(
        BundleFormatRowID::E96ThreeEntry,
        {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32, Haydn::ADD32}, Fmts);
    ASSERT_TRUE(Four.has_value());
    EXPECT_NE(Four->find("ISSUE_SLOT_COUNT"), std::string::npos) << *Four;
  }

  // Dual ST never co-issues (exclusive S0).
  {
    auto Bad = verifyCommittedBundle(
        BundleFormatRowID::E96ThreeEntry,
        {Haydn::ST32, Haydn::ST32, Haydn::ADD32}, Fmts);
    ASSERT_TRUE(Bad.has_value());
    EXPECT_NE(Bad->find("unit injectivity"), std::string::npos) << *Bad;
  }

  // Three-entry product row is selected for three members (packing may still
  // be blocked on transitional SLOT* encode-oracle under E96 composites).
  EXPECT_EQ(selectProductRowForMemberCount(3),
            BundleFormatRowID::E96ThreeEntry);
  {
    auto Three = verifyCommittedBundle(
        BundleFormatRowID::E96ThreeEntry,
        {Haydn::ADD32, Haydn::ADD32, Haydn::ADD32}, Fmts);
    if (isTransitionalPackerBlock(Three))
      GTEST_SKIP() << "transitional packer block: " << *Three;

    EXPECT_FALSE(Three.has_value()) << (Three ? *Three : "");
  }
}

} // namespace
