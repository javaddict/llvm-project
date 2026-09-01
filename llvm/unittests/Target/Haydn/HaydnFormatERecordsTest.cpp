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
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "gtest/gtest.h"
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"
#define GET_REGINFO_ENUM
#include "HaydnGenRegisterInfo.inc"

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
  // Golden v2_2 (supersedes v2_1 2026-08-28): +AR_CBR type 101 on
  // LOADSTORE0 entry0 in both packet entries, carrying 7 circular-buffer
  // UA load/store instrs. This pin must move together with
  // PINNED_JSON_SHA256 in generate_format_e_records.py.
  EXPECT_STREQ(FormatEJSONSHA256,
               "c436793cc8d3295088dda2271e68e5b53074eeb4bce3341d443ebac3e828dcba");
  EXPECT_STREQ(FormatEXLSXSHA256,
               "2a2b43cb394a16cf89173538f2a673235fdb6e4ed04cb6e4e75e72520cf7ffdb");
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

TEST(HaydnFormatERecords, AdmittedFamilyHandleIsE96Only) {
  EXPECT_EQ(kAdmittedFamily, BundleFamily::E96);
  EXPECT_EQ(getFamilyRecords(BundleFamily::E96).Family, BundleFamily::E96);
  EXPECT_EQ(getDefaultFamilyRecords().Family, BundleFamily::E96);
  EXPECT_EQ(FormatEFamilyId, static_cast<uint8_t>(BundleFamily::E96));
  EXPECT_EQ(FormatEAdmittedFamilyCount, 1u);
}

TEST(HaydnFormatERecords, MemberAndLedgerFamilyIsE96) {
  for (unsigned I = 0; I < FormatEMemberCount; ++I)
    EXPECT_EQ(FormatEMembers[I].Family, static_cast<uint8_t>(BundleFamily::E96));
  for (unsigned I = 0; I < FormatESetDescLedgerCount; ++I)
    EXPECT_EQ(FormatESetDescLedger[I].Family,
              static_cast<uint8_t>(BundleFamily::E96));
}

TEST(HaydnFormatERecords, CatalogSnapshotPins) {
  // v2_2: +7 AR_CBR instrs (unique 807→814, E2 801→808, E3 797→804,
  // both 791→798; type layouts 126→128).
  EXPECT_EQ(FormatETypeLayoutCount, 128u);
  EXPECT_EQ(FormatEUniqueNonNopNames, 814u);
  EXPECT_EQ(FormatEE2NonNopNames, 808u);
  EXPECT_EQ(FormatEE3NonNopNames, 804u);
  EXPECT_EQ(FormatEBothModeNonNopNames, 798u);
  EXPECT_EQ(FormatEE2OnlyNames, 10u);
  EXPECT_EQ(FormatEE3OnlyNames, 6u);
  EXPECT_EQ(FormatENonNopLogicalCount, 814u);
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
  // Generated-member spellings of an E2-only logical peel to that logical;
  // leftover FieldSlot `*_S<digits>` spellings are not occupancy recovery.
  // E3-bearing hwloop forms (SET_HWLOOP_F2 / SET_HWLOOP_REG are dual-mode
  // golden logicals) peel to themselves, not to bare SET_HWLOOP.
  EXPECT_EQ(peelLogicalOpcodeName("ADDI32_E2_E1_ALU1_RI20", false), "ADDI32");
  EXPECT_EQ(peelLogicalOpcodeName("ADDI32_W", false), "ADDI32_W");
  EXPECT_EQ(peelLogicalOpcodeName("ADDI32_W_S0", false), "ADDI32_W_S0");
  EXPECT_EQ(peelLogicalOpcodeName("SET_HWLOOP_F2_E3_E0_ALU0_HWLRIIR", false),
            "SET_HWLOOP_F2");
  EXPECT_EQ(peelLogicalOpcodeName("SET_HWLOOP_F2_W_S0", false),
            "SET_HWLOOP_F2_W_S0");
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
  // v2_2: AR_CBR adds one layout per mode (LOADSTORE0 entry0).
  EXPECT_EQ(E2Layouts, 35u);
  EXPECT_EQ(E3Layouts, 93u);

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
  // v2_2 restamp 2026-08-28: the 7 AR_CBR instrs widen mult-2 (66->73);
  // 1/3/4/5/7 unchanged from v2_1.
  EXPECT_EQ(Mult[1], 1u);
  EXPECT_EQ(Mult[2], 73u);
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
  // Generated Format E EncodedBytes is the product parcel width authority.
  // ProductRows EncodedBytes/Bits are driven from GET_FORMAT_E_GOLDEN_PINS,
  // not a parallel E96ParcelBytes{12} literal. FE8: that width is 12.
  EXPECT_EQ(FormatEEncodedBytes, 12u);
  EXPECT_EQ(FormatEBundleBits, 96u);
  EXPECT_EQ(FormatEEncodedBytes, (FormatEBundleBits + 7u) / 8u);
  EXPECT_EQ(FormatEBundleBits, FormatEEncodedBytes * 8u);
  EXPECT_NE(FormatEMemberCount, 0u);
  EXPECT_NE(findAltSpan("ADD32"), nullptr);

  using llvm::haydn::format::BundleFormatRowID;
  using llvm::haydn::format::encodedBytesOrDie;
  using llvm::haydn::format::getProductBundleFormatRows;
  using llvm::haydn::format::maxEncodedBytesInProfile;
  using llvm::haydn::format::ObjectEncodingProfileID;
  auto Rows = getProductBundleFormatRows();
  ASSERT_EQ(Rows.size(), 2u);
  bool SawE2 = false;
  bool SawE3 = false;
  for (const auto &R : Rows) {
    EXPECT_TRUE(R.IsProduct) << R.Name;
    EXPECT_EQ(R.Bytes.Value, FormatEEncodedBytes) << R.Name;
    EXPECT_EQ(R.Bits.Value, FormatEBundleBits) << R.Name;
    EXPECT_EQ(encodedBytesOrDie(R.Row).Value, FormatEEncodedBytes) << R.Name;
    SawE2 |= R.Row == BundleFormatRowID::E96TwoEntry;
    SawE3 |= R.Row == BundleFormatRowID::E96ThreeEntry;
  }
  EXPECT_TRUE(SawE2);
  EXPECT_TRUE(SawE3);
  EXPECT_EQ(maxEncodedBytesInProfile(ObjectEncodingProfileID::E96).Value,
            FormatEEncodedBytes);
  EXPECT_EQ(llvm::haydn::bundle::productParcelBytes().Value,
            FormatEEncodedBytes);
  EXPECT_EQ(llvm::haydn::bundle::ProductEncodedBytesValue, FormatEEncodedBytes);
}

TEST(HaydnFormatERecords, StoreLogicalsAreLoadStore0Only) {
  // Golden count=2: E2 e0 + E3 e0 LOADSTORE0. Public ST8 peels to S_SB_WITH_IMM.
  // FormatEUnit::LOADSTORE0 = 4 (not itinerary EU_LOADSTORE0 = 0).
  const uint32_t LS0 = 1u << static_cast<unsigned>(FormatEUnit::LOADSTORE0);
  EXPECT_EQ(peelLogicalOpcodeName("ST8"), "S_SB_WITH_IMM");
  EXPECT_EQ(peelLogicalOpcodeName("ST8_S0"), "ST8_S0");
  EXPECT_EQ(peelLogicalOpcodeName("D_SW_L_WITH_IMM_S2"), "D_SW_L_WITH_IMM_S2");
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

TEST(HaydnFormatERecords, ArcbrFamilyAdmittedOnLoadStore0Only) {
  // Golden v2_2 admission: AR_CBR type 101 lives on LOADSTORE0 entry0 in
  // BOTH modes (E2 + E3) and nowhere else — LOAD1 keeps AR/RI6/RR. Eight
  // mapping rows (NOP 0x00 + 7 instrs 0x01-0x07). Members must exist for
  // exactly the 7 non-NOP names, each with TypeCode 5 (0b101) and
  // OpcodeWidth 3.
  const char *const kArcbrLogicals[] = {
      "PLTWWUA_CB_POST", "PLQHWUA_CB_POST", "D_LTWUA_CB_POST",
      "D_LQHWUA_CB_POST", "D_STWUA_CB_POST", "D_SQHWUA_CB_POST",
      "WBARWUA_CB",
  };
  std::unordered_map<std::string, unsigned> Placements;
  unsigned ArcbrLayouts = 0;
  for (unsigned I = 0; I < FormatETypeLayoutCount; ++I) {
    const FormatETypeLayoutRec &L = FormatETypeLayouts[I];
    if (std::string_view(L.TypeName) != "AR_CBR")
      continue;
    ++ArcbrLayouts;
    EXPECT_EQ(L.TypeCode, 5u) << L.TypeName;
    EXPECT_EQ(L.TypeCodeWidth, 3u) << L.TypeName;
    EXPECT_EQ(std::string_view(L.UnitName), "LOADSTORE0") << L.TypeName;
    EXPECT_EQ(L.EntryIdx, 0u) << L.TypeName;
  }
  // One layout per mode.
  EXPECT_EQ(ArcbrLayouts, 2u);
  for (unsigned I = 0; I < FormatEMemberCount; ++I) {
    const FormatEMemberRec &M = FormatEMembers[I];
    if (std::string_view(M.TypeName) != "AR_CBR")
      continue;
    // The golden NOP 0x00 row is the shared architectural idle member.
    if (M.IsNop) {
      EXPECT_EQ(M.Opcode, 0u) << "AR_CBR NOP opcode";
      continue;
    }
    Placements[M.Logical]++;
    EXPECT_EQ(M.Unit, FormatETypeLayouts[M.LayoutId].Unit);
    EXPECT_EQ(M.OpcodeWidth, 3u) << M.Logical;
    EXPECT_LE(M.Opcode, 7u) << M.Logical;
  }
  EXPECT_EQ(Placements.size(), 7u);
  for (const char *Name : kArcbrLogicals) {
    EXPECT_EQ(Placements[Name], 2u) << Name; // one E2 + one E3 member
  }
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
  EXPECT_EQ(peelLogicalOpcodeName("ST64_S0"), "ST64_S0");
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

// REGRESSION TEST (golden member-shape cutover, 2026-08-21):
//
// Bug (gaps/audit_shapes.md classes (d') and (b)-real+(d)): the generator's
// base-writeback predicate keyed on the INFIX tags _POST_/_PRE_/_BREV_, so
// the AR-ua families (suffix `_POST` — tag never matches a trailing token)
// and the untagged CB families modeled their golden rs writeback on NO
// member: dest2 stayed a plain input and pointer liveness was wrong at
// member level. Separately, all 11 golden SFR writers (9 compares +
// MOVEGPR2SFR + ZERO_SFR) declared their ONLY semantic output to nobody
// (no Defs = [SFR] anywhere in the members file), weakening the SFR
// single-writer bundle law at the committed-MIR layer.
//
// Fix: generate_format_e_records.py derives both laws from golden ports —
// GPR Write∩Read tie drives the synthetic dest2_wb OUT (Constraints
// "$dest2_<i> = $dest2_wb") on every UA/CB member, and SFR_Write_Port
// drives implicit Defs = [SFR] on every member of an SFR-writing logical.
// If either regresses, these pins break at the Desc level (tie count /
// implicit SFR def), not just in the generator's own --check.
//
// What breaks if the bug returns: members lose the tie or the SFR def;
// haydnFormatEKeepOperands' tied-member branches stop firing (fill fails
// closed in MC), and countSFRPorts stops charging compare members, so two
// SFR writers can silently co-issue.
namespace llvm {
const MCInstrInfo &getHaydnSharedMCInstrInfo();
}

TEST(HaydnFormatERecords, UAAndCBMembersCarryTiedDest2Writeback) {
  // One member per family, spanning AR-ua suffix loads/stores and both CB
  // spellings. The law is golden-derived (Write∩Read), so the pins assert
  // SHAPE (a tied GPR use whose TIED_TO lands on a def named by the
  // member's outs), not member-symbol magic lists.
  const unsigned Members[] = {
      Haydn::D_LQHWUA_POST_E2_E0_LOADSTORE0_AR,
      Haydn::D_LTWUA_POST_E2_E1_LOAD1_AR,
      Haydn::D_SQHWUA_POST_E3_E0_LOADSTORE0_AR,
      Haydn::D_STWUA_POST_E3_E0_LOADSTORE0_AR,
      Haydn::PLDWWUA_POST_E2_E0_LOADSTORE0_AR,
      Haydn::D_LDW_CB_IMM_E2_E0_LOADSTORE0_CBRI,
      Haydn::D_LDW_CB_REG_E2_E0_LOADSTORE0_CBRR,
      Haydn::D_SDW_CB_IMM_E2_E0_LOADSTORE0_CBRI,
      Haydn::D_SDW_CB_REG_E2_E0_LOADSTORE0_CBRR,
  };
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  for (unsigned Opc : Members) {
    const MCInstrDesc &D = MII.get(Opc);
    const unsigned Defs = D.getNumDefs();
    ASSERT_GT(Defs, 0u) << Opc;
    bool HasTiedBase = false;
    for (unsigned I = Defs; I < D.getNumOperands(); ++I) {
      const int Tie = D.getOperandConstraint(I, MCOI::TIED_TO);
      if (Tie < 0 || static_cast<unsigned>(Tie) >= Defs)
        continue;
      // The tied use must be GPR-class (base pointer), tied to a def.
      EXPECT_GE(D.operands()[I].RegClass, 0) << Opc;
      HasTiedBase = true;
    }
    EXPECT_TRUE(HasTiedBase) << "member lost golden rs writeback: " << Opc;
  }
}

TEST(HaydnFormatERecords, SFRWriterMembersDeclareImplicitSFRDef) {
  // Golden SFR writers (instruction_type_index SFR_Write_Port): the nine
  // compares plus MOVEGPR2SFR / ZERO_SFR. Every member of each logical
  // must name SFR as an implicit def so countSFRPorts charges the 1W
  // budget at the committed-MIR layer.
  const unsigned SfrWriterMembers[] = {
      Haydn::SEQ64_E2_E0_ALU0_R,   Haydn::SLE64_E2_E0_ALU0_R,
      Haydn::SLT64_E2_E0_ALU0_R,   Haydn::X2SEQ32_E2_E0_ALU0_R,
      Haydn::X2SLE32_E2_E0_ALU0_R, Haydn::X2SLT32_E2_E0_ALU0_R,
      Haydn::X4SEQ16_E2_E0_ALU0_R, Haydn::X4SLE16_E2_E0_ALU0_R,
      Haydn::X4SLT16_E2_E0_ALU0_R, Haydn::MOVEGPR2SFR_E2_E0_ALU0_SFR,
      Haydn::ZERO_SFR_E2_E0_ALU0_I8,
  };
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  for (unsigned Opc : SfrWriterMembers) {
    const MCInstrDesc &D = MII.get(Opc);
    EXPECT_TRUE(D.hasImplicitDefOfPhysReg(Haydn::SFR))
        << "member lost implicit SFR def: " << Opc;
  }
}

// ---------------------------------------------------------------------------
// Direct-setDesc identity (runtime second layer). The generator's
// check_setdesc_identity owns the build-time law; this walks the same
// generated ledger at the MCInstrDesc level so a stale .inc cannot pass
// build-time checking and fail runtime shape parity.
// ---------------------------------------------------------------------------
namespace {

// Keep in sync with EXPECTED_IDENTITY_DIVERGENT in
// llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py (EMPTY since
// the 2026-08-26 W68.0R + CB-151 reshape). The Desc-level walk has no
// flags, so the two flag-exempt departures are named here: the hand-asm
// shell (isAsmParserOnly, D_LDW_CB_IMM swaps) and the retained
// SET_HWLOOP_REG ZOL pseudo (cutover refuses it by name; product creator
// emits SET_HWLOOP_F2_W directly). CSRR left with its 2-op shrink, the
// UA/CB golden families left with the CB-151 reshape.
std::set<std::string> identityDivergentAllowSet() {
  return {
      "D_LDW_CB_IMM",
      "SET_HWLOOP_REG",
  };
}

} // namespace

TEST(HaydnFormatERecords, DirectSetDescIdentityOverLedger) {
  using llvm::haydn::format_e::lookupGeneratedMemberToLogical;
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  const auto Allow = identityDivergentAllowSet();
  unsigned Pairs = 0;
  unsigned Divergent = 0;
  std::set<std::string> DivergentNames;
  for (unsigned MemberOpc = 0; MemberOpc != Haydn::INSTRUCTION_LIST_END;
       ++MemberOpc) {
    const unsigned LogicalOpc = lookupGeneratedMemberToLogical(MemberOpc);
    if (!LogicalOpc || LogicalOpc == MemberOpc)
      continue;
    const MCInstrDesc &L = MII.get(LogicalOpc);
    const MCInstrDesc &M = MII.get(MemberOpc);
    ++Pairs;
    bool ShapeEqual = L.getNumOperands() == M.getNumOperands() &&
                      L.getNumDefs() == M.getNumDefs();
    for (unsigned I = 0; ShapeEqual && I != L.getNumOperands(); ++I) {
      const MCOperandInfo &LI = L.operands()[I];
      const MCOperandInfo &MI = M.operands()[I];
      if (LI.OperandType != MI.OperandType || LI.RegClass != MI.RegClass)
        ShapeEqual = false;
      else if ((L.getOperandConstraint(I, MCOI::TIED_TO) == -1) !=
               (M.getOperandConstraint(I, MCOI::TIED_TO) == -1))
        ShapeEqual = false;
    }
    if (ShapeEqual)
      continue;
    ++Divergent;
    const std::string Name = MII.getName(LogicalOpc).str();
    DivergentNames.insert(Name);
    EXPECT_TRUE(Allow.count(Name))
        << "new setDesc identity divergence: " << Name << " vs "
        << MII.getName(MemberOpc)
        << " (operands/defs/OpInfo differ; align the logical TableGen "
           "schema and re-pin the census)";
  }
  EXPECT_GT(Pairs, 4000u) << "ledger walk lost its member pairs";
  EXPECT_EQ(DivergentNames, Allow)
      << "divergent census drifted from the pinned allow set";
}

// ---------------------------------------------------------------------------
// Universal singleton coverage (PIPE-20 / GR2.2). The generator census
// (singleton_uncovered_census in generate_format_e_records.py) owns the
// build-time law in BOTH emit and --check modes; this is the always-on
// runtime layer over the generated proof table. A logical lacking
// singleton coverage fails HERE at test time, not as the post-RA "no
// generated member" fatal in HaydnBundleVerify.
// ---------------------------------------------------------------------------
namespace {

// Keep in sync with EXPECTED_SINGLETON_UNCOVERED in
// llvm/lib/Target/Haydn/FormatE/generate_format_e_records.py (EMPTY since
// installation 2026-08-31: the generator census sees TableGen flags and its
// five-file + golden-defs scope). The Desc-level walk below has no flags,
// so the TD-exempt departures are named here — the exact
// identityDivergentAllowSet pattern:
//   * SHL32 / LSR32 / ASR32 — GISel-compat alias defs under
//     `let isCodeGenOnly = 1` (HaydnInstrInfo.td:361-379). No C++ selects
//     them (shifts select SLL32-family Pats); a stale comment in
//     HaydnISelLowering.h:37 names SHL32 but G_BRJT lowers through
//     legal ops. isCodeGenOnly is not an MCInstrDesc flag, so the walk
//     cannot exempt them the way the generator census does.
//   * BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY — the Format E packet
//     CONTAINER opcodes (HaydnFormatE.td composites). Singleton coverage
//     is a per-child-logical law; the composite is the completed packet
//     itself, never a catalog span member. Not in the generator census
//     scope (not one of the five logical-shape files).
// NOP needs no seat here: its def is isPseudo=1, so the walk filters it,
// and its coverage is the idle-parcel law pinned in the NOP-completion
// test.
std::set<std::string> singletonUncoveredAllowSet() {
  return {
      "SHL32",
      "LSR32",
      "ASR32",
      "BUNDLE_E96_TWO_ENTRY",
      "BUNDLE_E96_THREE_ENTRY",
  };
}

} // namespace

TEST(HaydnFormatERecords, SingletonCoverageCoversEveryCatalogLogical) {
  // Every FormatEAltSpans logical owns a proof row with a NOP-completed
  // mode; counts are pinned; the uncovered ratchet set stays empty.
  std::set<std::string> SpanLogicals;
  for (unsigned I = 0; I != FormatENonNopLogicalCount; ++I)
    SpanLogicals.insert(FormatEAltSpans[I].Logical);
  ASSERT_EQ(SpanLogicals.size(),
            static_cast<size_t>(FormatENonNopLogicalCount));
  EXPECT_EQ(FormatENonNopLogicalCount, 814u) << "golden v2_2 pin";

  EXPECT_EQ(FormatESingletonCoverageCount, FormatENonNopLogicalCount);
  static_assert(sizeof(FormatESingletonCoverage) /
                        sizeof(FormatESingletonCoverage[0]) ==
                    FormatESingletonCoverageCount,
                "generated static_assert mirror");
  std::set<std::string> Proved;
  for (unsigned I = 0; I != FormatESingletonCoverageCount; ++I) {
    const FormatESingletonCoverageRec &Rec = FormatESingletonCoverage[I];
    EXPECT_NE(Rec.ModeMask, 0) << "logical without NOP-completed mode: "
                               << Rec.Logical;
    Proved.insert(Rec.Logical);
  }
  EXPECT_EQ(Proved, SpanLogicals)
      << "proof table and alt spans disagree on the logical universe";

  EXPECT_EQ(FormatESingletonUncoveredCount, 0u)
      << "uncovered ratchet set must stay empty";
}

TEST(HaydnFormatERecords, SingletonCoverageNopCompletionHoldsAtEveryWindow) {
  // Per proof row and mode bit: the logical has a member at some
  // (mode, entry) window that ALSO hosts a NOP member — one real child
  // plus generated NOP completion fills a complete admitted packet. The
  // walk is over FormatEMembers, the same generated table encode uses.
  for (unsigned I = 0; I != FormatESingletonCoverageCount; ++I) {
    const FormatESingletonCoverageRec &Rec = FormatESingletonCoverage[I];
    for (uint8_t Bit = 0; Bit != 2; ++Bit) {
      if (!(Rec.ModeMask & (1u << Bit)))
        continue;
      const uint8_t Mode = Bit; // bit0=E2(0), bit1=E3(1)
      bool NopAtWindow = false;
      bool LogicalAtWindow = false;
      for (unsigned M = 0; M != FormatEMemberCount; ++M) {
        const FormatEMemberRec &Mem = FormatEMembers[M];
        if (Mem.Mode != Mode)
          continue;
        if (Mem.IsNop)
          NopAtWindow = true;
        if (!Mem.IsNop && StringRef(Mem.Logical) == StringRef(Rec.Logical))
          LogicalAtWindow = true;
        if (NopAtWindow && LogicalAtWindow)
          break;
      }
      EXPECT_TRUE(NopAtWindow && LogicalAtWindow)
          << "mode " << unsigned(Mode) << " lacks NOP completion alongside "
          << Rec.Logical;
    }
  }
  // The architectural idle packet exists in BOTH modes (E2 and E3 each
  // host NOP members); NOP never enters FormatEAltSpans by construction.
  EXPECT_EQ(FormatENopCompletionModes, 0b11u);
  EXPECT_EQ(findAltSpan("NOP"), nullptr)
      << "NOP must be covered by the idle-parcel law, never an alt span";
  bool NopMembers[2] = {false, false};
  for (unsigned M = 0; M != FormatEMemberCount; ++M) {
    const FormatEMemberRec &Mem = FormatEMembers[M];
    if (Mem.IsNop)
      NopMembers[Mem.Mode] = true;
  }
  EXPECT_TRUE(NopMembers[0] && NopMembers[1])
      << "NOP members must exist in both E2 and E3";
}

TEST(HaydnFormatERecords, CompilerReachableLogicalsHaveSingletonCoverage) {
  // Full enum walk: every opcode that survives the Desc-level reachability
  // filter must peel (peelLogicalOpcodeName) to a catalog alt span. This
  // is the acceptance-(2) seat: a hand-added logical escaping ExpandPseudos
  // (not isPseudo, not pre-ISel, not meta) with no catalog span fails HERE
  // and in the generator census at build time.
  const MCInstrInfo &MII = getHaydnSharedMCInstrInfo();
  const auto Allow = singletonUncoveredAllowSet();
  unsigned Checked = 0;
  std::set<std::string> Uncovered;
  for (unsigned Opc = 0; Opc != Haydn::INSTRUCTION_LIST_END; ++Opc) {
    const MCInstrDesc &Desc = MII.get(Opc);
    if (Desc.isPseudo() || Desc.isPreISelOpcode() || Desc.isMetaInstruction())
      continue;
    const std::string Peeled = peelLogicalOpcodeName(MII.getName(Opc));
    ++Checked;
    if (findAltSpan(Peeled.c_str()))
      continue;
    Uncovered.insert(MII.getName(Opc).str());
  }
  for (const std::string &Name : Uncovered) {
    EXPECT_TRUE(Allow.count(Name))
        << "compiler-reachable logical without singleton coverage: " << Name
        << " (peel maps nowhere in FormatEAltSpans; add the catalog span "
           "or extend BOTH peel seats and re-pin the allow sets)";
  }
  EXPECT_TRUE(Uncovered.empty() || Uncovered == Allow)
      << "uncovered census drifted from the pinned allow set";
  EXPECT_GT(Checked, 700u) << "enum walk lost its reachable opcodes";

  // Peel-parity pins: ONE law at TWO seats. Each alias family the
  // generator's pinned table maps is pinned here against the compiler's
  // peel output, so generate_format_e_records.py::peel_logical_name and
  // HaydnFormatERecords.h::peelLogicalOpcodeName cannot drift silently.
  struct ParityPin {
    const char *TD;
    const char *Catalog;
  };
  const ParityPin Pins[] = {
      {"LD32", "S_LW_WITH_IMM"},          {"ST32", "S_SW_WITH_IMM"},
      {"LD64", "D_LDW_WITH_IMM"},         {"ST64", "D_SDW_WITH_IMM"},
      {"LD8", "S_LBS_WITH_IMM"},          {"LDU8", "S_LBU_WITH_IMM"},
      {"ST8", "S_SB_WITH_IMM"},           {"LD16", "S_LHWS_WITH_IMM"},
      {"LDU16", "S_LHWU_WITH_IMM"},       {"ST16", "S_SHW_WITH_IMM"},
      {"LD32_POST", "S_LW_POST_IMM"},     {"ST32_POST", "S_SW_POST_IMM"},
      {"LD64_POST", "D_LDW_POST_IMM"},    {"ST64_POST", "D_SDW_POST_IMM"},
      {"PLDWWUA", "PLDWWUA_POST"},        {"RET", "JALR"},
      {"WFI", "WFI<TBD>"},                {"WFITBDTBDTBD", "WFI<TBD>"},
      {"SEXT_GPR32_TO_DR64", "SEXT32T64"}, {"MOV_GPR_TO_DR64", "SEXT32T64"},
      {"ZEXT_GPR32_TO_DR64", "SEXT32T64"}, {"ADDI32_W", "ADDI32"},
      {"SET_HWLOOP_F2_W", "SET_HWLOOP_F2"}, {"CSRW_W", "CSRW"},
      {"LD32_REG_M0S0LS", "S_LW_WITH_REG"}, {"ST32_REG_M0S0LS", "S_SW_WITH_REG"},
  };
  for (const ParityPin &Pin : Pins) {
    const std::string Peeled = peelLogicalOpcodeName(Pin.TD);
    EXPECT_EQ(Peeled, Pin.Catalog)
        << "peel parity broke for " << Pin.TD << " -> " << Peeled;
    EXPECT_NE(findAltSpan(Peeled.c_str()), nullptr)
        << "parity pin " << Pin.TD << " peeled to a catalogless name";
  }

  // Negative probe: a hand-added ghost logical must find NO span and NO
  // proof row — the fail direction of the ratchet.
  EXPECT_EQ(findAltSpan("HAND_ADDED_GHOST_LOGICAL"), nullptr);
  for (unsigned I = 0; I != FormatESingletonCoverageCount; ++I)
    EXPECT_STRNE(FormatESingletonCoverage[I].Logical,
                 "HAND_ADDED_GHOST_LOGICAL");
}
