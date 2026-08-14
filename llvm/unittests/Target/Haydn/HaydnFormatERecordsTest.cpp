//===- HaydnFormatERecordsTest.cpp - inert Format E record pins -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generation / coverage / inverse / setDesc-ledger unit tests for the inert
// Format E records produced by FormatE/generate_format_e_records.py.
// These tables are not reachable from product selectors.
//
//===----------------------------------------------------------------------===//

#include "HaydnFormatERecords.h"
#include "gtest/gtest.h"
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::haydn::format_e;

namespace {

TEST(HaydnFormatERecords, GoldenHashPins) {
  // REPAIRED golden, not the delivery: b0b477e5... is the as-delivered
  // layout whose mapping rows carry the known generation defect (the 76
  // src1/src2 rows; sole live-member delta X4SEL16_E3_E1_ALU1_RRR). The
  // importer pins 8465132c... — the delivery after the two deterministic
  // repair passes (--fix-operand-mapping / --fix-read-ports), byte-for-byte
  // reproducible from the delivered files. This pin must move together with
  // PINNED_JSON_SHA256 in generate_format_e_records.py.
  EXPECT_STREQ(FormatEJSONSHA256,
               "8465132c2fb91e44a335d8a63577c637428d93106ed7a4d657d80ac70fdfa7f9");
  EXPECT_STREQ(FormatEXLSXSHA256,
               "9b3c06612cec47fa026bd79cff5632cb970abdfe1e161075444f7d02432574af");
  // Canonical-vector SHA-256 is enforced by generate_format_e_records.py
  // --check (PINNED_CANONICAL_SHA256 = 6d403139…b728f9). The ledger is a
  // check input, not an encode table, so it is not emitted into the .inc.
}

TEST(HaydnFormatERecords, GeometryPinsFromGeneratedConstants) {
  // Typed generated constants — no raw product parcel literals in callers.
  EXPECT_EQ(FormatEBundleBits, 96u);
  EXPECT_EQ(FormatEEncodedBytes, (FormatEBundleBits + 7u) / 8u);
  EXPECT_EQ(FormatEPayloadBudgetBits, 90u);
  EXPECT_EQ(FormatEPayloadLsb, 6u);
  EXPECT_EQ(FormatEIndicator, 0x7u);
  EXPECT_EQ(FormatEHeaderReserved, 0x0u);
  EXPECT_EQ(FormatEUnitCount, 7u);
}

TEST(HaydnFormatERecords, CatalogSnapshotPins) {
  EXPECT_EQ(FormatETypeLayoutCount, 126u);
  EXPECT_EQ(FormatEUniqueNonNopNames, 683u);
  EXPECT_EQ(FormatEE2NonNopNames, 677u);
  EXPECT_EQ(FormatEE3NonNopNames, 673u);
  EXPECT_EQ(FormatEBothModeNonNopNames, 667u);
  EXPECT_EQ(FormatEE2OnlyNames, 10u);
  EXPECT_EQ(FormatEE3OnlyNames, 6u);
  EXPECT_EQ(FormatENonNopLogicalCount, 683u);
  EXPECT_EQ(FormatEE2UnitPairCount, 9u);
  EXPECT_EQ(FormatEE3LegalTupleCount, 42u);
  EXPECT_EQ(FormatEE3IllegalTupleCount, 22u);
}

TEST(HaydnFormatERecords, LayoutAndMemberCoverage) {
  EXPECT_EQ(sizeof(FormatETypeLayouts) / sizeof(FormatETypeLayouts[0]),
            FormatETypeLayoutCount);
  EXPECT_EQ(sizeof(FormatEMembers) / sizeof(FormatEMembers[0]),
            FormatEMemberCount);

  unsigned E2Layouts = 0, E3Layouts = 0;
  for (unsigned I = 0; I < FormatETypeLayoutCount; ++I) {
    if (FormatETypeLayouts[I].Mode == 0)
      ++E2Layouts;
    else
      ++E3Layouts;
  }
  EXPECT_EQ(E2Layouts, 34u);
  EXPECT_EQ(E3Layouts, 92u);

  std::unordered_set<std::string> NonNop;
  std::unordered_set<std::string> E2, E3;
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    EXPECT_LT(M.LayoutId, FormatETypeLayoutCount);
    EXPECT_EQ(M.MemberId, I);
    if (M.IsNop)
      continue;
    NonNop.insert(M.Logical);
    if (M.Mode == 0)
      E2.insert(M.Logical);
    else
      E3.insert(M.Logical);
  }
  EXPECT_EQ(NonNop.size(), FormatEUniqueNonNopNames);
  EXPECT_EQ(E2.size(), FormatEE2NonNopNames);
  EXPECT_EQ(E3.size(), FormatEE3NonNopNames);

  unsigned Both = 0, E2Only = 0, E3Only = 0;
  for (const std::string &N : NonNop) {
    bool InE2 = E2.count(N) != 0;
    bool InE3 = E3.count(N) != 0;
    if (InE2 && InE3)
      ++Both;
    else if (InE2)
      ++E2Only;
    else
      ++E3Only;
  }
  EXPECT_EQ(Both, FormatEBothModeNonNopNames);
  EXPECT_EQ(E2Only, FormatEE2OnlyNames);
  EXPECT_EQ(E3Only, FormatEE3OnlyNames);

  // Manifest E2-only / E3-only name sets.
  for (const char *Name :
       {"ADDI32", "ADDI32S", "ANDI32", "MOVEI_H", "MOVEI_L", "ORI32",
        "SET_HWLOOP", "SUBI32", "SUBI32S", "XORI32"}) {
    EXPECT_TRUE(E2.count(Name)) << Name;
    EXPECT_FALSE(E3.count(Name)) << Name;
  }
  for (const char *Name :
       {"ARCTAN", "EXP2", "LOG2", "RECIP", "SIN_COS", "SQRT"}) {
    EXPECT_TRUE(E3.count(Name)) << Name;
    EXPECT_FALSE(E2.count(Name)) << Name;
  }
}

TEST(HaydnFormatERecords, AlternativeMultiplicityAndInverseIdentity) {
  std::unordered_map<unsigned, unsigned> Mult;
  for (unsigned I = 0; I < FormatENonNopLogicalCount; ++I) {
    const FormatEAltSpan &S = FormatEAltSpans[I];
    EXPECT_GT(S.Count, 0u);
    Mult[S.Count] += 1;
    for (unsigned J = 0; J < S.Count; ++J) {
      uint16_t Mid = FormatEAltMemberIds[S.Begin + J];
      ASSERT_LT(Mid, FormatEMemberCount);
      const FormatEMemberRec &M = FormatEMembers[Mid];
      EXPECT_STREQ(M.Logical, S.Logical);
      EXPECT_EQ(M.IsNop, 0);
      int Inv = findInverseMemberId(M.Mode, M.EntryIdx, M.Unit, M.TypeCode,
                                    M.Opcode);
      EXPECT_EQ(Inv, static_cast<int>(Mid)) << S.Logical;
    }
  }
  EXPECT_EQ(Mult[1], 1u);
  EXPECT_EQ(Mult[2], 62u);
  EXPECT_EQ(Mult[3], 15u);
  EXPECT_EQ(Mult[4], 7u);
  EXPECT_EQ(Mult[5], 412u);
  EXPECT_EQ(Mult[7], 186u);

  // SET_HWLOOP has exactly one placement (E2 entry0 ALU0).
  const FormatEAltSpan *Set = findAltSpan("SET_HWLOOP");
  ASSERT_NE(Set, nullptr);
  EXPECT_EQ(Set->Count, 1u);
  const FormatEMemberRec &Only =
      FormatEMembers[FormatEAltMemberIds[Set->Begin]];
  EXPECT_EQ(Only.Mode, 0);
  EXPECT_EQ(Only.EntryIdx, 0);
}

TEST(HaydnFormatERecords, InverseCoversEveryMemberUniquely) {
  EXPECT_EQ(sizeof(FormatEInverse) / sizeof(FormatEInverse[0]),
            FormatEMemberCount);
  std::set<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint16_t>> Keys;
  std::set<uint16_t> MemberIds;
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEInverseRec &R = FormatEInverse[I];
    auto Key =
        std::make_tuple(R.Mode, R.EntryIdx, R.Unit, R.TypeCode, R.Opcode);
    EXPECT_TRUE(Keys.insert(Key).second) << "duplicate inverse key";
    EXPECT_TRUE(MemberIds.insert(R.MemberId).second);
    ASSERT_LT(R.MemberId, FormatEMemberCount);
    const FormatEMemberRec &M = FormatEMembers[R.MemberId];
    EXPECT_EQ(M.Mode, R.Mode);
    EXPECT_EQ(M.EntryIdx, R.EntryIdx);
    EXPECT_EQ(M.Unit, R.Unit);
    EXPECT_EQ(M.TypeCode, R.TypeCode);
    EXPECT_EQ(M.Opcode, R.Opcode);
    EXPECT_STREQ(M.Logical, R.Logical);
  }
  EXPECT_EQ(MemberIds.size(), FormatEMemberCount);
}

TEST(HaydnFormatERecords, UnitInjectivityTables) {
  EXPECT_EQ(sizeof(FormatEE2UnitPairs) / sizeof(FormatEE2UnitPairs[0]),
            FormatEE2UnitPairCount);
  EXPECT_EQ(sizeof(FormatEE3LegalTuples) / sizeof(FormatEE3LegalTuples[0]),
            FormatEE3LegalTupleCount);
  EXPECT_EQ(sizeof(FormatEE3IllegalTuples) / sizeof(FormatEE3IllegalTuples[0]),
            FormatEE3IllegalTupleCount);

  for (unsigned I = 0; I < FormatEE3LegalTupleCount; ++I) {
    const FormatEUnitTriple &T = FormatEE3LegalTuples[I];
    EXPECT_NE(T.U0, T.U1);
    EXPECT_NE(T.U0, T.U2);
    EXPECT_NE(T.U1, T.U2);
  }
  for (unsigned I = 0; I < FormatEE3IllegalTupleCount; ++I) {
    const FormatEUnitTriple &T = FormatEE3IllegalTuples[I];
    EXPECT_TRUE(T.U0 == T.U1 || T.U0 == T.U2 || T.U1 == T.U2);
  }
}

TEST(HaydnFormatERecords, SetDescLedgerStarted) {
  EXPECT_EQ(FormatESetDescLedgerCount,
            sizeof(FormatESetDescLedger) / sizeof(FormatESetDescLedger[0]));
  // Every non-NOP alternative placement appears once in the ledger.
  EXPECT_EQ(FormatESetDescLedgerCount, FormatEAltMemberIdCount);

  std::unordered_map<std::string, std::set<unsigned>> Groups;
  for (unsigned I = 0; I < FormatESetDescLedgerCount; ++I) {
    const FormatESetDescLedgerRec &R = FormatESetDescLedger[I];
    EXPECT_LT(R.MemberId, FormatEMemberCount);
    EXPECT_STREQ(FormatEMembers[R.MemberId].Logical, R.Logical);
    // Zero-operand golden leaves (e.g. HINT/WFI) legitimately have an empty
    // role signature; non-zero OperandCount must carry a non-empty signature.
    if (R.OperandCount == 0)
      EXPECT_TRUE(std::string(R.OperandSignature).empty());
    else
      EXPECT_FALSE(std::string(R.OperandSignature).empty());
    Groups[R.Logical].insert(R.SignatureGroup);
  }
  unsigned Multi = 0;
  for (const auto &KV : Groups)
    if (KV.second.size() > 1)
      ++Multi;
  EXPECT_EQ(Multi, FormatESetDescMultiSignatureLogicals);
}

TEST(HaydnFormatERecords, GeometryMatchesRegistryProductParcel) {
  // Generated Format E EncodedBytes is the product parcel width authority for
  // record tables; registry E96 rows share the same typed size surface.
  EXPECT_EQ(FormatEEncodedBytes, (FormatEBundleBits + 7u) / 8u);
  EXPECT_EQ(FormatEBundleBits, FormatEEncodedBytes * 8u);
  EXPECT_NE(FormatEMemberCount, 0u);
  EXPECT_NE(findAltSpan("ADD32"), nullptr);
}

TEST(HaydnFormatERecords, StoreLogicalsAreLoadStore0Only) {
  // Golden count=2: E2 e0 + E3 e0 LOADSTORE0. Public ST8 peels to S_SB_WITH_IMM.
  // FormatEUnit::LOADSTORE0 = 4 (not itinerary EU_LOADSTORE0 = 0).
  const uint32_t LS0 = 1u << static_cast<unsigned>(FormatEUnit::LOADSTORE0);
  EXPECT_EQ(peelLogicalOpcodeName("ST8"), "S_SB_WITH_IMM");
  EXPECT_EQ(peelLogicalOpcodeName("ST8_S0"), "S_SB_WITH_IMM");
  EXPECT_EQ(peelLogicalOpcodeName("D_SW_L_WITH_IMM_S2"), "D_SW_L_WITH_IMM");
  // Earliest mode marker: E3-e2 must not peel as LOGICAL_E3.
  EXPECT_EQ(peelLogicalOpcodeName("ADD32_E3_E2_ALU2_RR"), "ADD32");
  EXPECT_EQ(peelLogicalOpcodeName("ADD32_E2_E0_ALU0_RR"), "ADD32");
  EXPECT_EQ(unitMaskForLogical("D_SW_L_WITH_IMM", 0), LS0);
  EXPECT_EQ(unitMaskForLogical("D_SW_L_WITH_IMM", 1), LS0);
  EXPECT_EQ(unitMaskForLogical("S_SB_WITH_IMM", 0), LS0);
  EXPECT_EQ(unitMaskForLogical("S_SB_WITH_IMM", 1), LS0);
  const std::string DualStoreAlu[] = {"D_SW_L_WITH_IMM", "OR64",
                                      "S_SB_WITH_IMM"};
  EXPECT_FALSE(logicalsHaveUnitCover(DualStoreAlu));
  const std::string StoreAlu[] = {"D_SW_L_WITH_IMM", "OR64"};
  EXPECT_TRUE(logicalsHaveUnitCover(StoreAlu));
}

TEST(HaydnFormatERecords, CanonicalVectorGeometryAndHeaderHex) {
  // Consumes format_e_canonical_vectors_v1.json geometry + published hex
  // (STATUS.md five-file pin). Importer --check owns full ledger walk +
  // XLSX parse; this unit pins the header facts the C++ tables must match.
  EXPECT_EQ(FormatEBundleBits, 96u);
  EXPECT_EQ(FormatEEncodedBytes, (FormatEBundleBits + 7u) / 8u);
  EXPECT_EQ(FormatEIndicator, 0x7u);
  EXPECT_EQ(FormatEHeaderReserved, 0x0u);

  auto indicator = [](unsigned Byte0) { return Byte0 & 0x7u; };
  auto entryNum = [](unsigned Byte0) { return (Byte0 >> 3) & 0x1u; };
  auto reserved = [](unsigned Byte0) { return (Byte0 >> 4) & 0x3u; };

  // MAL_ALL_ZERO_12B / MAL_INDICATOR_000: all-zero is not Format E.
  EXPECT_NE(indicator(0x00), FormatEIndicator);
  // STRUCT_E2_HEADER_ENVELOPE wire_hex_le_12 = 0700…00
  EXPECT_EQ(indicator(0x07), FormatEIndicator);
  EXPECT_EQ(entryNum(0x07), 0u);
  EXPECT_EQ(reserved(0x07), FormatEHeaderReserved);
  // STRUCT_E3_HEADER_ENVELOPE wire_hex_le_12 = 0f00…00
  EXPECT_EQ(indicator(0x0f), FormatEIndicator);
  EXPECT_EQ(entryNum(0x0f), 1u);
  EXPECT_EQ(reserved(0x0f), FormatEHeaderReserved);
  // MAL_E2_HEADER_RESERVED_01 wire_hex_le_12 = 1700…00
  EXPECT_EQ(indicator(0x17), FormatEIndicator);
  EXPECT_NE(reserved(0x17), FormatEHeaderReserved);
}

TEST(HaydnFormatERecords, MemberToLogicalGeneratedInverse) {
  // T-TII6: generated member→logical inverse. AIE peer is
  // AIEMCFormats::getAlternateInstsOpcode inverted (AIEMCFormats.h:376-379).
  // Fail-closed: absent opcodes return 0, never the member itself.
  using llvm::haydn::format_e::lookupGeneratedMemberToLogical;
  using llvm::haydn::format_e::logicalOpcodeOrSelf;

  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BEQ_W_S0), Haydn::BEQ_W);
  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BNEZ_W_S0), Haydn::BNEZ_W);
  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BEQ_E2_E0_ALU0_RI12),
            Haydn::BEQ);

  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::ADD32_E2_E0_ALU0_RR),
            Haydn::ADD32);
  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BEQ_E2_E0_ALU0_RI12),
            Haydn::BEQ);
  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BNEZ_E2_E0_ALU0_I12),
            Haydn::BNEZ);
  EXPECT_EQ(lookupGeneratedMemberToLogical(
                Haydn::SET_HWLOOP_E2_E0_ALU0_HWLRIII),
            Haydn::SET_HWLOOP);
  EXPECT_EQ(lookupGeneratedMemberToLogical(
                Haydn::SET_HWLOOP_F2_E2_E0_ALU0_HWLRIIR),
            Haydn::SET_HWLOOP_F2_W);

  EXPECT_EQ(logicalOpcodeOrSelf(Haydn::ADD32_E2_E0_ALU0_RR), Haydn::ADD32);
  EXPECT_EQ(logicalOpcodeOrSelf(Haydn::BEQ_E2_E0_ALU0_RI12), Haydn::BEQ);
  EXPECT_EQ(logicalOpcodeOrSelf(Haydn::ADD32), Haydn::ADD32);
  EXPECT_EQ(logicalOpcodeOrSelf(Haydn::BEQ_W), Haydn::BEQ_W);

  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::ADD32), 0u);
  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BEQ_W), 0u);
  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BEQ), 0u);
  EXPECT_EQ(lookupGeneratedMemberToLogical(0xFFFFFFFFu), 0u);
  EXPECT_NE(lookupGeneratedMemberToLogical(Haydn::BEQ_E2_E0_ALU0_RI12),
            Haydn::BEQ_E2_E0_ALU0_RI12);
}

TEST(HaydnFormatERecords, AssignThreeChildStoreLastE3) {
  EXPECT_EQ(peelLogicalOpcodeName("ST64_S0"), "D_SDW_WITH_IMM");
  const std::string Logs[3] = {"SRLI64", "SEXT32T64", "D_SDW_WITH_IMM"};
  auto A = assignFormatEMemberEntries(Logs, /*Mode=*/1);
  ASSERT_TRUE(A.has_value());
  ASSERT_EQ(A->size(), 3u);
  EXPECT_EQ((*A)[2].EntryIdx, 0u);
  ASSERT_NE((*A)[2].Mem, nullptr);
  EXPECT_EQ((*A)[2].Mem->Unit, static_cast<uint8_t>(FormatEUnit::LOADSTORE0));
  EXPECT_NE((*A)[0].EntryIdx, (*A)[1].EntryIdx);
  EXPECT_NE((*A)[0].EntryIdx, (*A)[2].EntryIdx);
  EXPECT_NE((*A)[1].EntryIdx, (*A)[2].EntryIdx);
}

TEST(HaydnFormatERecords, AssignDualLoadE3) {
  const std::string Logs[3] = {"SLT32", "D_LDW_WITH_IMM", "S_LW_WITH_IMM"};
  auto A = assignFormatEMemberEntries(Logs, /*Mode=*/1);
  ASSERT_TRUE(A.has_value());
  bool SawLS0 = false;
  bool SawLoad1 = false;
  for (const FormatEEntryAssign &E : *A) {
    ASSERT_NE(E.Mem, nullptr);
    if (E.Mem->Unit == static_cast<uint8_t>(FormatEUnit::LOADSTORE0))
      SawLS0 = true;
    if (E.Mem->Unit == static_cast<uint8_t>(FormatEUnit::LOAD1))
      SawLoad1 = true;
  }
  EXPECT_TRUE(SawLS0);
  EXPECT_TRUE(SawLoad1);
}

TEST(HaydnFormatERecords, AssignTwoStoresRejected) {
  const std::string Logs[2] = {"D_SDW_WITH_IMM", "S_SW_WITH_IMM"};
  auto A = assignFormatEMemberEntries(Logs, /*Mode=*/1);
  EXPECT_FALSE(A.has_value());
}

} // namespace
