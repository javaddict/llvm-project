//===- HaydnMspCloneFamilyTest.cpp - ONE encode-inverse family table ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// D1.43/D1.74/D1.130 census arm: HaydnMspCloneFamily.h is the ONE
// encode-inverse family table consumed by MC-lower (logicalNameForMspClone),
// BundleVerify (logicalOpcodeForMspClone), and InstrInfo peel
// (haydnLogicalOpcode). Drift is impossible by construction (single
// switch + peel calls that switch); these arms pin that the accessors
// resolve IDENTICAL catalog families for every clone, that ADD32_MSP
// fails closed, that InstrInfo does not remap to `_W`, that B is not a
// clone, and that the deleted `_MSP` flag clones are greppably absent.
//
//===----------------------------------------------------------------------===//

#include "HaydnMspCloneFamily.h"
#include "HaydnFormatERecords.h"
#include "HaydnInstrInfo.h"
#include "gtest/gtest.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrInfo.h"

using namespace llvm;
using namespace llvm::haydn::msp;

namespace {

TEST(HaydnMspCloneFamilyTest, NameAndOpcodeAccessorsAgreeForEveryClone) {
  // The lower site keys generated members by Logical NAME; the verify site
  // keys the inverse span by Logical OPCODE. Both must name the same
  // catalog family for each clone — pinned against the generated MC name
  // tables (same backing store the two seats use), not a second hand list.
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  for (unsigned Opc : MspCloneOpcodes) {
    const unsigned Logical = logicalOpcodeForMspClone(Opc);
    const StringRef Name = logicalNameForMspClone(Opc);
    if (Logical == 0) {
      // Unmapped `_MSP` opcode: BOTH seats must fail closed together.
      EXPECT_TRUE(Name.empty()) << MII.getName(Opc);
      continue;
    }
    EXPECT_FALSE(Name.empty()) << MII.getName(Opc);
    EXPECT_EQ(Name, StringRef(MII.getName(Logical)))
        << "clone " << MII.getName(Opc) << " name key must equal the MC "
        << "name of its catalog logical (single family)";
  }
}

TEST(HaydnMspCloneFamilyTest, SerializableCloneFamiliesPinned) {
  // Exact families the serializer/verifier pair selects (D1.18 targets):
  // the catalog logicals, never the `_W` names (no FormatEInverse rows of
  // their own — the pre-D1.18 dead chain). D1.130 honest gMIR names.
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JALR_CALL), Haydn::JALR);
  EXPECT_EQ(logicalNameForMspClone(Haydn::JALR_CALL), "JALR");
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JAL_TCO), Haydn::JAL);
  EXPECT_EQ(logicalNameForMspClone(Haydn::JAL_TCO), "JAL");
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JALR_TCO), Haydn::JALR);
  EXPECT_EQ(logicalNameForMspClone(Haydn::JALR_TCO), "JALR");
}

TEST(HaydnMspCloneFamilyTest, UnmappedMspFailsClosedInBothSeats) {
  // ADD32_MSP is `_MSP`-named with NO catalog mapping: lower keeps it
  // Desc-as-is (empty name) and the verify unit-cover pre-check / walk
  // both refuse it (0 opcode). materialize/leaveRegion setDesc baking is
  // its only legal commit path.
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::ADD32_MSP), 0u);
  EXPECT_TRUE(logicalNameForMspClone(Haydn::ADD32_MSP).empty());

  // Non-clone opcodes are never family-mapped (no suffix peel, no
  // identity): a bare catalog logical must not re-enter the table.
  // Uncond B is not a clone — encoder peels B separately (F).
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::BEQZ), 0u);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JAL), 0u);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JALR), 0u);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JALR_W), 0u);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JAL_W), 0u);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::ADD32), 0u);
  EXPECT_TRUE(logicalNameForMspClone(Haydn::ADD32).empty());
  // D1.130: B peels to catalog BEQZ (rs=R0 at MC); not a wide-cond clone.
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::B), unsigned(Haydn::BEQZ));
  EXPECT_EQ(logicalNameForMspClone(Haydn::B), "BEQZ");
}

TEST(HaydnMspCloneFamilyTest, CensusCoversEveryMspNamedOpcode) {
  // Fail-closed census: every `_MSP`-suffixed opcode in the generated MC
  // tables must appear in MspCloneOpcodes. After D1.130 the only remaining
  // `_MSP` name is ADD32_MSP (AIE MultiSlot_Pseudo slot map).
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  SmallVector<unsigned, 8> Unlisted;
  for (unsigned Opc = 1; Opc < MII.getNumOpcodes(); ++Opc) {
    if (!StringRef(MII.getName(Opc)).ends_with("_MSP"))
      continue;
    if (!llvm::is_contained(MspCloneOpcodes, Opc))
      Unlisted.push_back(Opc);
  }
  EXPECT_TRUE(Unlisted.empty())
      << "new `_MSP` opcode outside HaydnMspCloneFamily.h census: "
      << (Unlisted.empty() ? std::string() : std::string(MII.getName(Unlisted.front())));
}

TEST(HaydnMspCloneFamilyTest, DeletedFlagClonesAbsent) {
  // D1.130: BEQZ_W_MSP / JALR_MSP / JAL_W_MSP / JALR_W_MSP are greppably
  // gone from the generated MC name tables.
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  for (unsigned Opc = 1; Opc < MII.getNumOpcodes(); ++Opc) {
    const StringRef N = MII.getName(Opc);
    EXPECT_NE(N, "BEQZ_W_MSP");
    EXPECT_NE(N, "JALR_MSP");
    EXPECT_NE(N, "JAL_W_MSP");
    EXPECT_NE(N, "JALR_W_MSP");
  }
}

TEST(HaydnMspCloneFamilyTest, InstrInfoPeelAgreesWithOneTable) {
  // D1.74/D1.90: haydnLogicalOpcode must call logicalOpcodeForMspClone.
  // Catalog BEQZ/JAL/JALR, never BEQZ_W/JAL_W. ADD32_MSP stays unmapped
  // so the peel falls through to logicalOpcodeOrSelf (fail-closed, no
  // handwritten clone arm). isWideCondClone is derived from the same
  // table (BEQZ mapping); D1.130 deleted the BEQZ clone so it is false.
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  for (unsigned Opc : MspCloneOpcodes) {
    const unsigned Catalog = logicalOpcodeForMspClone(Opc);
    EXPECT_EQ(isWideCondClone(Opc), Catalog == Haydn::BEQZ) << MII.getName(Opc);
    if (Catalog) {
      // CFG keeps gMIR names (B / JALR_CALL / JAL_TCO / JALR_TCO). Encode
      // table still peels to Catalog for freeze/MC.
      if (Opc == Haydn::JALR_CALL || Opc == Haydn::JAL_TCO ||
          Opc == Haydn::JALR_TCO || Opc == Haydn::B)
        EXPECT_EQ(haydnLogicalOpcode(Opc), Opc) << MII.getName(Opc);
      else
        EXPECT_EQ(haydnLogicalOpcode(Opc), Catalog) << MII.getName(Opc);
      EXPECT_NE(haydnLogicalOpcode(Opc), Haydn::BEQZ_W) << MII.getName(Opc);
      EXPECT_NE(haydnLogicalOpcode(Opc), Haydn::JAL_W) << MII.getName(Opc);
    } else {
      EXPECT_EQ(Opc, Haydn::ADD32_MSP);
      EXPECT_EQ(haydnLogicalOpcode(Opc),
                haydn::format_e::logicalOpcodeOrSelf(Opc));
    }
  }
  EXPECT_EQ(haydnLogicalOpcode(Haydn::JALR_CALL), Haydn::JALR_CALL);
  EXPECT_EQ(haydnLogicalOpcode(Haydn::JAL_TCO), Haydn::JAL_TCO);
  EXPECT_EQ(haydnLogicalOpcode(Haydn::JALR_TCO), Haydn::JALR_TCO);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JALR_CALL), unsigned(Haydn::JALR));
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JAL_TCO), unsigned(Haydn::JAL));
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JALR_TCO), unsigned(Haydn::JALR));
  EXPECT_FALSE(isWideCondClone(Haydn::JALR_CALL));
  EXPECT_FALSE(isWideCondClone(Haydn::JAL_TCO));
  EXPECT_FALSE(isWideCondClone(Haydn::JALR_TCO));
  EXPECT_FALSE(isWideCondClone(Haydn::BEQZ));
  EXPECT_FALSE(isWideCondClone(Haydn::BEQZ_W));
  EXPECT_FALSE(isWideCondClone(Haydn::B));
  EXPECT_FALSE(isWideCondClone(Haydn::ADD32_MSP));
}

} // namespace
