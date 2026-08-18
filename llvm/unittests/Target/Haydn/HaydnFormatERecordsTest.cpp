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

namespace {
namespace mode_only_detail {
#define GET_FORMAT_E_MODE_ONLY_NAMES
#include "HaydnGenFormatERecords.inc"
} // namespace mode_only_detail
} // namespace

using namespace llvm;
using namespace llvm::haydn::format_e;

namespace {

TEST(HaydnFormatERecords, GoldenHashPins) {
  // Golden v2_1 (supersedes v2 2026-08-18): +120 MAC RR 32X16 instrs, +4 LS
  // D_SW_F64RS rows, RRR operand canonicalization; geometry identical. The
  // JSON is as-delivered (26098770…); the companion forced read-port repair
  // (6 FMUL*32S rows) landed in instruction_type_index.json (7a13453a…),
  // not here. This pin must move together with PINNED_JSON_SHA256 in
  // generate_format_e_records.py.
  EXPECT_STREQ(FormatEJSONSHA256,
               "2609877075156dd9749e1e8dd0b45ff1ef326dae2e1c2a9c10fbd9cd1c1c8f6a");
  EXPECT_STREQ(FormatEXLSXSHA256,
               "dd8491b7c182d006ad7d05c8cd46f64c02f439ae41bad0416f7139703d07b76f");
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
  EXPECT_EQ(FormatEUniqueNonNopNames, 807u);
  EXPECT_EQ(FormatEE2NonNopNames, 801u);
  EXPECT_EQ(FormatEE3NonNopNames, 797u);
  EXPECT_EQ(FormatEBothModeNonNopNames, 791u);
  EXPECT_EQ(FormatEE2OnlyNames, 10u);
  EXPECT_EQ(FormatEE3OnlyNames, 6u);
  EXPECT_EQ(FormatENonNopLogicalCount, 807u);
  EXPECT_EQ(FormatEE2UnitPairCount, 9u);
  EXPECT_EQ(FormatEE3LegalTupleCount, 42u);
  EXPECT_EQ(FormatEE3IllegalTupleCount, 22u);
}

// REGRESSION TEST (W44 / P18(c), 2026-08-15):
//
// Bug: residualAltCompatibleFormatMask and isFormatEE2Only/E3OnlyOpcodeName
// used hand-transcribed E2-only/E3-only name arrays with count-only pins
// ("keep in sync with FormatEE2OnlyNames = 10"). If a golden catalog update
// renamed or moved a logical between Modes, the counts could stay 10/6 while
// the *identities* drifted, and the hand copies silently disagreed with the
// generated rows — the mask then fell through a silent ProductFormatMask
// default and wrongly admitted E3 placement for an E2-only logical.
//
// Fix: generate_format_e_records.py emits the exact SETS
// (GET_FORMAT_E_MODE_ONLY_NAMES, FormatEE2Only/E3OnlyNameSet) and every
// consumer derives admission from them. This test proves set↔row equality in
// BOTH directions against the FormatEMembers table itself, so any golden
// drift that changes a Mode-only membership breaks here (and in importer
// --check) instead of at a placement decision.
//
// What breaks if the bug returns: if a consumer re-hardcodes a name list, a
// later golden rename keeps this test green (it checks the generated tables)
// but the consumer's list goes stale — which is why the same file also pins
// the peel spellings that feed the classifier (ModeOnlyLogicalPeelSpellings).
TEST(HaydnFormatERecords, ModeOnlyNameSetsCoverExactlyGeneratedRows) {
  using mode_only_detail::FormatEE2OnlyNameSet;
  using mode_only_detail::FormatEE3OnlyNameSet;
  const unsigned E2Count =
      sizeof(FormatEE2OnlyNameSet) / sizeof(FormatEE2OnlyNameSet[0]);
  const unsigned E3Count =
      sizeof(FormatEE3OnlyNameSet) / sizeof(FormatEE3OnlyNameSet[0]);
  EXPECT_EQ(E2Count, FormatEE2OnlyNames);
  EXPECT_EQ(E3Count, FormatEE3OnlyNames);

  // Rebuild the Mode frontier from the generated member rows.
  std::unordered_set<std::string> NonNop, E2, E3;
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    if (M.IsNop)
      continue;
    NonNop.insert(M.Logical);
    if (M.Mode == 0)
      E2.insert(M.Logical);
    else
      E3.insert(M.Logical);
  }
  const auto *E2Begin = FormatEE2OnlyNameSet;
  const auto *E3Begin = FormatEE3OnlyNameSet;
  std::unordered_set<std::string> E2Set(E2Begin, E2Begin + E2Count);
  std::unordered_set<std::string> E3Set(E3Begin, E3Begin + E3Count);

  // Direction 1: every generated set member is non-NOP and truly Mode-only.
  for (const std::string &N : E2Set) {
    EXPECT_TRUE(NonNop.count(N)) << N;
    EXPECT_TRUE(E2.count(N)) << N;
    EXPECT_FALSE(E3.count(N)) << N;
  }
  for (const std::string &N : E3Set) {
    EXPECT_TRUE(NonNop.count(N)) << N;
    EXPECT_TRUE(E3.count(N)) << N;
    EXPECT_FALSE(E2.count(N)) << N;
  }

  // Direction 2: set equality with the Mode frontier rebuilt from rows —
  // a rename or Mode move that the count pins absorb still breaks here.
  std::unordered_set<std::string> ExpectedE2Only, ExpectedE3Only;
  for (const std::string &N : E2)
    if (!E3.count(N))
      ExpectedE2Only.insert(N);
  for (const std::string &N : E3)
    if (!E2.count(N))
      ExpectedE3Only.insert(N);
  EXPECT_EQ(E2Set, ExpectedE2Only);
  EXPECT_EQ(E3Set, ExpectedE3Only);
}

TEST(HaydnFormatERecords, ModeOnlyLogicalPeelSpellings) {
  // The admission surface sees peeled names, exactly like the placement
  // enumerate path (enumerateFormatEMemberAlts peels with StripWide=false).
  // Residual/member spellings of an E2-only logical peel to that logical;
  // E3-bearing hwloop forms (SET_HWLOOP_F2 / SET_HWLOOP_REG are dual-mode
  // golden logicals) peel to themselves, not to bare SET_HWLOOP.
  EXPECT_EQ(peelLogicalOpcodeName("ADDI32_E2_E1_ALU1_RI20", false), "ADDI32");
  EXPECT_EQ(peelLogicalOpcodeName("ADDI32_W", false), "ADDI32_W");
  EXPECT_EQ(peelLogicalOpcodeName("ADDI32_W_S0", false), "ADDI32_W");
  EXPECT_EQ(peelLogicalOpcodeName("SET_HWLOOP_F2_E3_E0_ALU0_HWLRIIR", false),
            "SET_HWLOOP_F2");
  EXPECT_EQ(peelLogicalOpcodeName("SET_HWLOOP_F2_W_S0", false),
            "SET_HWLOOP_F2_W");
  EXPECT_EQ(peelLogicalOpcodeName("ARCTAN_E3_E0_ALU2_RI4", false), "ARCTAN");
  // The generated sets are keyed by exact golden logicals; `_W` reloc forms
  // are not members (they are residual FieldSlot rows, not catalog logicals)
  // — their admission derives from the base logical at the consumer.
  using mode_only_detail::FormatEE2OnlyNameSet;
  const std::unordered_set<std::string> E2Only(
      FormatEE2OnlyNameSet,
      FormatEE2OnlyNameSet + sizeof(FormatEE2OnlyNameSet) /
                                 sizeof(FormatEE2OnlyNameSet[0]));
  EXPECT_TRUE(E2Only.count("ADDI32"));
  EXPECT_FALSE(E2Only.count("ADDI32_W"));
  EXPECT_TRUE(E2Only.count("SET_HWLOOP"));
  EXPECT_FALSE(E2Only.count("SET_HWLOOP_F2"));
  EXPECT_FALSE(E2Only.count("SET_HWLOOP_REG"));
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
  // v2_1 restamp 2026-08-18: 32X16 family widens mult-2 (62->66) and
  // mult-5 (412->532); 1/3/4/7 unchanged.
  EXPECT_EQ(Mult[1], 1u);
  EXPECT_EQ(Mult[2], 66u);
  EXPECT_EQ(Mult[3], 15u);
  EXPECT_EQ(Mult[4], 7u);
  EXPECT_EQ(Mult[5], 532u);
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

  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BEQ_E2_E0_ALU0_RI12),
            Haydn::BEQ);
  EXPECT_EQ(lookupGeneratedMemberToLogical(Haydn::BNEZ_E2_E0_ALU0_I12),
            Haydn::BNEZ);

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
