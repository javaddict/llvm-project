//===- HaydnMspCloneFamilyTest.cpp - ONE `_MSP` clone-family table -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// D1.43 census arm: HaydnMspCloneFamily.h is the ONE `_MSP` clone-family
// table consumed by BOTH the MC-lower entry binding
// (haydn::msp::logicalNameForMspClone — HaydnMCInstLower.cpp generated-
// member Logical NAME key) and the structural inverse walk
// (haydn::msp::logicalOpcodeForMspClone — HaydnBundleVerify.cpp inverse-
// span OPCODE key). Drift is impossible by construction (single switch);
// these arms pin that the two accessors resolve IDENTICAL families for
// every clone opcode, that the unmapped `_MSP` opcode (ADD32_MSP) fails
// closed in both, and that no `_MSP`-named opcode exists outside the
// census (a new TD clone cannot silently miss the table).
//
//===----------------------------------------------------------------------===//

#include "HaydnMspCloneFamily.h"
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
  // their own — the pre-D1.18 dead chain).
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::BEQZ_W_MSP), Haydn::BEQZ);
  EXPECT_EQ(logicalNameForMspClone(Haydn::BEQZ_W_MSP), "BEQZ");
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JALR_MSP), Haydn::JALR);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JALR_W_MSP), Haydn::JALR);
  EXPECT_EQ(logicalNameForMspClone(Haydn::JALR_W_MSP), "JALR");
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JAL_W_MSP), Haydn::JAL);
  EXPECT_EQ(logicalNameForMspClone(Haydn::JAL_W_MSP), "JAL");
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
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::BEQZ), 0u);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::JAL), 0u);
  EXPECT_EQ(logicalOpcodeForMspClone(Haydn::ADD32), 0u);
  EXPECT_TRUE(logicalNameForMspClone(Haydn::ADD32).empty());
}

TEST(HaydnMspCloneFamilyTest, CensusCoversEveryMspNamedOpcode) {
  // Fail-closed census: every `_MSP`-suffixed opcode in the generated MC
  // tables must appear in MspCloneOpcodes. A future TD clone lands in this
  // set and immediately exercises the agree/fail-closed arms above — it
  // cannot silently drift past the ONE table.
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

} // namespace
