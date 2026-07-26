//===- HaydnFormatOrderingTest.cpp - applyFormatOrdering -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for applyFormatOrdering + AsmPrinter Desc-only encode-order /
// MC serialize-only contract:
//
//   AIE peers:
//     AIEHazardRecognizer.cpp:278-314 applyFormatOrdering
//     AIEHazardRecognizer.cpp:343-344 applyBundles size()>1 call site
//     AIEBundle.h:150-156 getFormatOrNull
//     AIEBaseAsmPrinter.cpp:128-184 Bundle.add → Format → slots → emit
//     AIEBaseAsmPrinter.cpp:161-164 MCBundle.setOpcode(Format->Opcode)
//     AIEBaseMCCodeEmitter.cpp:45-68 encodeInstruction serialize-only
//     BundleTest.cpp getFormatOrNull / slot map patterns
//
// Without a full MachineFunction, pin the pure data path that
// finalizeLegalMultiMI walks after setDesc + full AltDesc clear, the
// printer SlotMap → S0-S1-S2 encode order, and Format->Opcode:
//
//   1. Format.getSlots() for BUNDLE128_FULL is S2→S1→S0
//   2. SlotMap built from post-setDesc getSlotKind only; Format walk yields
//      field order regardless of schedule-input order
//   3. getFormatOrNull returns the product packet row
//   4. Member resolution is getSlotKind only (no AltDescs residual)
//   5. stampBundleFormatID(ProductFormatID) remains the durable root mark
//   6. Encode order S0→S1→S2 from SlotMap; canAdd fail-closed (no split)
//   7. Format->Opcode is BUNDLE128_FULL (composite serialize; no BUNDLE
//      wrapper); no Flags re-slot / re-auction on committed members
//
// Product: BUNDLE128_FULL only. N-format-ready via PacketFormats table scan.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/MC/MCInst.h"
#include "gtest/gtest.h"

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"

using namespace llvm;
using namespace llvm::Haydn;
using namespace llvm::haydn::bundle;

namespace {

// Walk Format.getSlots() and collect Bundle.at(Slot) opcodes — pure data
// half of applyFormatOrdering (AIEHazardRecognizer.cpp:292-306).
static SmallVector<unsigned, 3>
fieldOrderOpcodes(const Bundle<MCInst> &B, const VLIWFormat &Fmt) {
  SmallVector<unsigned, 3> Out;
  for (MCSlotKind Slot : Fmt.getSlots()) {
    if (const MCInst *I = B.at(Slot))
      Out.push_back(I->getOpcode());
  }
  return Out;
}

//===----------------------------------------------------------------------===//
// Product FormatSlotData is S2 → S1 → S0
//===----------------------------------------------------------------------===//

TEST(HaydnFormatOrdering, ProductFormatSlotsAreS2S1S0) {
  HaydnMCFormats Fmts;
  const VLIWFormat *Fmt =
      Fmts.getPacketFormats().getFormat(Haydn::SLOT0 | Haydn::SLOT1 |
                                        Haydn::SLOT2);
  ASSERT_NE(Fmt, nullptr);
  EXPECT_STREQ(Fmt->Name, "BUNDLE128_FULL");

  SmallVector<MCSlotKind, 3> Slots;
  for (MCSlotKind S : Fmt->getSlots())
    Slots.push_back(S);

  ASSERT_EQ(Slots.size(), 3u);
  EXPECT_EQ(Slots[0], MCSlotKind(MCSlotKind::Haydn_SLOT_S2));
  EXPECT_EQ(Slots[1], MCSlotKind(MCSlotKind::Haydn_SLOT_S1));
  EXPECT_EQ(Slots[2], MCSlotKind(MCSlotKind::Haydn_SLOT_S0));
}

//===----------------------------------------------------------------------===//
// SlotMap → Format.getSlots() child order independent of schedule input
//===----------------------------------------------------------------------===//

TEST(HaydnFormatOrdering, FieldOrderIgnoresScheduleInputOrder) {
  // Three committed format-members in S0,S1,S2 schedule order. Field walk
  // must still emit S2→S1→S0 (BUNDLE128_FULL FormatSlotData).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst S0, S1, S2;
  S0.setOpcode(Haydn::ADD32_S0);
  S1.setOpcode(Haydn::ADD32_S1);
  S2.setOpcode(Haydn::ADD32_S2);

  // Schedule input order: S0 then S1 then S2 (opposite of field order).
  ASSERT_TRUE(B.canAdd(S0.getOpcode()));
  B.add(&S0);
  ASSERT_TRUE(B.canAdd(S1.getOpcode()));
  B.add(&S1);
  ASSERT_TRUE(B.canAdd(S2.getOpcode()));
  B.add(&S2);

  ASSERT_EQ(B.size(), 3u);
  // Insertion order is schedule order.
  EXPECT_EQ(B.getInstrs()[0]->getOpcode(), Haydn::ADD32_S0);
  EXPECT_EQ(B.getInstrs()[1]->getOpcode(), Haydn::ADD32_S1);
  EXPECT_EQ(B.getInstrs()[2]->getOpcode(), Haydn::ADD32_S2);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_STREQ(Fmt->Name, "BUNDLE128_FULL");

  auto Ordered = fieldOrderOpcodes(B, *Fmt);
  ASSERT_EQ(Ordered.size(), 3u);
  EXPECT_EQ(Ordered[0], Haydn::ADD32_S2);
  EXPECT_EQ(Ordered[1], Haydn::ADD32_S1);
  EXPECT_EQ(Ordered[2], Haydn::ADD32_S0);
}

TEST(HaydnFormatOrdering, FieldOrderFromReverseScheduleStillS2S1S0) {
  // Schedule input already S2→S1→S0 — field order matches, no reshuffle
  // identity, but still driven by Format.getSlots() not Instrs[].
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst S2, S1, S0;
  S2.setOpcode(Haydn::XOR32_S2);
  S1.setOpcode(Haydn::XOR32_S1);
  S0.setOpcode(Haydn::XOR32_S0);
  B.add(&S2);
  B.add(&S1);
  B.add(&S0);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  auto Ordered = fieldOrderOpcodes(B, *Fmt);
  ASSERT_EQ(Ordered.size(), 3u);
  EXPECT_EQ(Ordered[0], Haydn::XOR32_S2);
  EXPECT_EQ(Ordered[1], Haydn::XOR32_S1);
  EXPECT_EQ(Ordered[2], Haydn::XOR32_S0);
}

TEST(HaydnFormatOrdering, SparsePairStillFieldOrder) {
  // ST32 is S0-only; ADD64 prefers high free slot (S2). Field walk skips
  // empty S1 → [ADD64_S2?, ST32_S0] in Format slot order.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St, Ad;
  // Post-setDesc members when available; else logicals via pickSlot.
  St.setOpcode(Haydn::ST32_S0);
  Ad.setOpcode(Haydn::ADD64_S2);
  // Fall back to logicals if member enums missing shape in table.
  if (Fmts.getSlotKind(St.getOpcode()) == MCSlotKind())
    St.setOpcode(Haydn::ST32);
  if (Fmts.getSlotKind(Ad.getOpcode()) == MCSlotKind())
    Ad.setOpcode(Haydn::ADD64);

  // Schedule order: ST first (S0), ADD second (S2) — field order reverses.
  ASSERT_TRUE(B.canAdd(St.getOpcode()));
  B.add(&St);
  ASSERT_TRUE(B.canAdd(Ad.getOpcode()));
  B.add(&Ad);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  auto Ordered = fieldOrderOpcodes(B, *Fmt);
  ASSERT_EQ(Ordered.size(), 2u);
  // First present format slot with a member is S2, then S0.
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S2))->getOpcode(),
            Ordered[0]);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S0))->getOpcode(),
            Ordered[1]);
  EXPECT_EQ(Ordered[0], Ad.getOpcode());
  EXPECT_EQ(Ordered[1], St.getOpcode());
}

//===----------------------------------------------------------------------===//
// getFormatOrNull + ProductFormatID stamp contract
//===----------------------------------------------------------------------===//

TEST(HaydnFormatOrdering, GetFormatOrNullReturnsProductRow) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, X;
  A.setOpcode(Haydn::ADD32_S2);
  X.setOpcode(Haydn::XOR32_S1);
  if (Fmts.getSlotKind(A.getOpcode()) == MCSlotKind())
    A.setOpcode(Haydn::ADD32);
  if (Fmts.getSlotKind(X.getOpcode()) == MCSlotKind())
    X.setOpcode(Haydn::XOR32);
  B.add(&A);
  B.add(&X);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_STREQ(Fmt->Name, "BUNDLE128_FULL");
  EXPECT_EQ(Fmt->getSize(), 16u);
  // Size-filtered form still returns product (N-format-ready API).
  const VLIWFormat *BySize = B.getFormatOrNull(/*Size=*/16);
  ASSERT_NE(BySize, nullptr);
  EXPECT_STREQ(BySize->Name, "BUNDLE128_FULL");
}

TEST(HaydnFormatOrdering, StampProductFormatIDRemainsZero) {
  // After applyFormatOrdering, finalizeLegalMultiMI stamps ProductFormatID.
  // Pin durable encoding: Full == imm 0 (BUNDLE-root mark).
  EXPECT_EQ(ProductFormatID, FormatID::Bundle128Full);
  EXPECT_EQ(formatIDToImm(ProductFormatID), 0u);
  EXPECT_TRUE(isKnownFormatIDImm(0u));
  EXPECT_EQ(formatIDFromImm(0u), FormatID::Bundle128Full);
}

//===----------------------------------------------------------------------===//
// Member resolution: getSlotKind only (no AltDesc residual)
//===----------------------------------------------------------------------===//

TEST(HaydnFormatOrdering, MemberResolutionPrefersGetSlotKind) {
  // Post-setDesc ADD32_S2 has fixed kind S2 — SlotMap must use that, not
  // a re-auctioned tryAdd on a logical.
  HaydnMCFormats Fmts;
  MCSlotKind Fixed = Fmts.getSlotKind(Haydn::ADD32_S2);
  ASSERT_NE(Fixed, MCSlotKind())
      << "ADD32_S2 must be a single-slot format member";
  EXPECT_EQ(Fixed, MCSlotKind(MCSlotKind::Haydn_SLOT_S2));

  Bundle<MCInst> B(&Fmts);
  MCInst M;
  M.setOpcode(Haydn::ADD32_S2);
  B.add(&M);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S2)), &M);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S1)), nullptr);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S0)), nullptr);
}

TEST(HaydnFormatOrdering, ResidualLogicalUsesBundlePickSlotNotAltDesc) {
  // When getSlotKind is empty (logical / multi-slot residual),
  // finalizeLegalMultiMI uses Bundle.canAdd/add (alts tryAdd).
  // No AltDescs slot side-map (AIEAlternateDescriptors.h:27-75).
  HaydnMCFormats Fmts;
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD32), MCSlotKind())
      << "logical ADD32 must not have a fixed single slot";

  Bundle<MCInst> B(&Fmts);
  MCInst Log;
  Log.setOpcode(Haydn::ADD32);
  ASSERT_TRUE(B.canAdd(Log.getOpcode()));
  B.add(&Log);
  // Bundle pickSlot places via tryAddProduct (prefer high free → S2).
  EXPECT_NE(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S2)), nullptr);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_S2)), &Log);
}

TEST(HaydnFormatOrdering, FixedKindIsSolePostCommitAuthority) {
  // After successful setDesc, opcode identity is sole post-commit placement
  // authority (AIE getSlotKind; AIEBaseMCFormats.cpp:66-75).
  HaydnMCFormats Fmts;
  unsigned Opc = Haydn::ADD32_S2;
  MCSlotKind Fixed = Fmts.getSlotKind(Opc);
  ASSERT_NE(Fixed, MCSlotKind());
  EXPECT_EQ(Fixed, MCSlotKind(MCSlotKind::Haydn_SLOT_S2));

  Bundle<MCInst> B(&Fmts);
  MCInst M;
  M.setOpcode(Opc);
  ASSERT_TRUE(B.canAdd(M.getOpcode()));
  B.add(&M);
  EXPECT_EQ(B.at(Fixed), &M);
  // Slot enum is sequential 0/1/2 (HaydnMCFormats.h). MCInstLower does
  // not stamp Flags — encode uses getSlotKind / Format composite only.
  EXPECT_EQ(static_cast<unsigned>(Fixed), 2u);
}

//===----------------------------------------------------------------------===//
// AsmPrinter Desc-only + Format->Opcode serialize
// (AIEBaseAsmPrinter.cpp:128-184 / :161-164 peer)
//===----------------------------------------------------------------------===//
// Printer walks Bundle SlotMap by S0-S1-S2 encode order (not Format field
// order S2→S1→S0), NOP-pads empties, emits Format->Opcode composite, and
// fails closed on canAdd — no Flags re-slot / re-auction, no split.

// Pure data half of HaydnAsmPrinter composite emit: S0→S1→S2 Bundle.at, with
// null for empty (caller inserts NOP). Mirrors BUNDLE128_FULL operand dag.
static SmallVector<const MCInst *, 3>
encodeOrderSlots(const Bundle<MCInst> &B) {
  SmallVector<const MCInst *, 3> Out;
  for (unsigned K = 0; K < Haydn::ISSUE_SLOT_COUNT; ++K) {
    MCSlotKind Slot =
        MCSlotKind(MCSlotKind::Haydn_SLOT_S0 + static_cast<int>(K));
    Out.push_back(B.at(Slot));
  }
  return Out;
}

TEST(HaydnFormatOrdering, ProductFormatOpcodeIsBundle128Full) {
  // AIEBaseAsmPrinter.cpp:161-164 — MCBundle.setOpcode(Format->Opcode).
  // Product sole live row; N-format-ready via PacketFormats table (Opcode
  // field), not a hard-coded second product path in AsmPrinter.
  HaydnMCFormats Fmts;
  const VLIWFormat *Fmt =
      Fmts.getPacketFormats().getFormat(Haydn::SLOT0 | Haydn::SLOT1 |
                                        Haydn::SLOT2);
  ASSERT_NE(Fmt, nullptr);
  EXPECT_STREQ(Fmt->Name, "BUNDLE128_FULL");
  EXPECT_EQ(Fmt->Opcode, Haydn::BUNDLE128_FULL)
      << "AsmPrinter MCB.setOpcode(Format->Opcode) must be BUNDLE128_FULL";

  // Empty / sparse occupancy still covers via the same product row.
  for (SlotBits Occ :
       {SlotBits(0), SlotBits(Haydn::SLOT0),
        SlotBits(Haydn::SLOT1 | Haydn::SLOT2), SlotBits(Haydn::SLOT_ALL)}) {
    const VLIWFormat *F = Fmts.getPacketFormats().getFormat(Occ);
    ASSERT_NE(F, nullptr) << "occ=" << Occ;
    EXPECT_EQ(F->Opcode, Haydn::BUNDLE128_FULL) << "occ=" << Occ;
  }
}

TEST(HaydnFormatOrdering, AsmPrinterEncodeOrderIsS0S1S2) {
  // Members committed as S2/S1/S0. MIR field order is S2→S1→S0; printer
  // encode order must be S0→S1→S2; composite opcode is Format->Opcode.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst S0, S1, S2;
  S0.setOpcode(Haydn::ADD32_S0);
  S1.setOpcode(Haydn::ADD32_S1);
  S2.setOpcode(Haydn::ADD32_S2);
  // Schedule / field-ish input: S2 first (opposite of encode).
  B.add(&S2);
  B.add(&S1);
  B.add(&S0);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_STREQ(Fmt->Name, "BUNDLE128_FULL");
  EXPECT_EQ(Fmt->Opcode, Haydn::BUNDLE128_FULL);

  auto Field = fieldOrderOpcodes(B, *Fmt);
  ASSERT_EQ(Field.size(), 3u);
  EXPECT_EQ(Field[0], Haydn::ADD32_S2);
  EXPECT_EQ(Field[1], Haydn::ADD32_S1);
  EXPECT_EQ(Field[2], Haydn::ADD32_S0);

  auto Enc = encodeOrderSlots(B);
  ASSERT_EQ(Enc.size(), 3u);
  ASSERT_NE(Enc[0], nullptr);
  ASSERT_NE(Enc[1], nullptr);
  ASSERT_NE(Enc[2], nullptr);
  EXPECT_EQ(Enc[0]->getOpcode(), Haydn::ADD32_S0);
  EXPECT_EQ(Enc[1]->getOpcode(), Haydn::ADD32_S1);
  EXPECT_EQ(Enc[2]->getOpcode(), Haydn::ADD32_S2);
}

TEST(HaydnFormatOrdering, AsmPrinterSparseEncodePadsEmptyWithNull) {
  // ST32_S0 alone → encode slots [ST32, null, null]; printer inserts NOP.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St;
  St.setOpcode(Haydn::ST32_S0);
  if (Fmts.getSlotKind(St.getOpcode()) == MCSlotKind())
    St.setOpcode(Haydn::ST32);
  ASSERT_TRUE(B.canAdd(St.getOpcode()));
  B.add(&St);

  ASSERT_NE(B.getFormatOrNull(), nullptr);
  auto Enc = encodeOrderSlots(B);
  ASSERT_EQ(Enc.size(), 3u);
  ASSERT_NE(Enc[0], nullptr);
  EXPECT_EQ(Enc[0]->getOpcode(), St.getOpcode());
  EXPECT_EQ(Enc[1], nullptr);
  EXPECT_EQ(Enc[2], nullptr);
}

TEST(HaydnFormatOrdering, AsmPrinterFailClosedOnSameSlotConflict) {
  // Two fixed S0 members: second canAdd must fail — printer reports fatal
  // instead of emergency multi-parcel split.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, Dup;
  A.setOpcode(Haydn::ST32_S0);
  Dup.setOpcode(Haydn::ST32_S0);
  if (Fmts.getSlotKind(A.getOpcode()) == MCSlotKind()) {
    A.setOpcode(Haydn::ST32);
    Dup.setOpcode(Haydn::ST32);
  }
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  EXPECT_FALSE(B.canAdd(Dup.getOpcode()))
      << "second S0-only member must conflict (no printer re-auction)";
}

} // end anonymous namespace
