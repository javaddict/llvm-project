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
// printer SlotMap → reverse-of-Format.getSlots() encode order, and
// Format->Opcode:
//
//   1. Format.getSlots() for the E3 product row is E3_2→E3_1→E3_0
//   2. SlotMap built from post-setDesc getSlotKind only; Format walk yields
//      field order regardless of schedule-input order
//   3. getFormatOrNull returns the product packet row
//   4. Member resolution is getSlotKind only (no AltDescs residual)
//   5. stampBundleFormatID(ProductFormatID) remains the durable root mark
//   6. Encode order is reverse of Format.getSlots(); canAdd fail-closed
//   7. Format->Opcode is the composite row (serialize; no BUNDLE
//      wrapper); no Flags re-slot / re-auction on committed members
//
// Product: sole live composite via PacketFormats; residual full-width rows may remain as synthetic test formats.
//
//===----------------------------------------------------------------------===//

#include "HaydnBundle.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnBundlePlan.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/ADT/StringRef.h"
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
  // Format E product rows: E2 field order E2_1→E2_0; E3 field order
  // E3_2→E3_1→E3_0. Residual S2→S1→S0 ordering is the transitional issue
  // pack; product PacketFormats no longer list residual S* kinds.
  HaydnMCFormats Fmts;
  const VLIWFormat *E3 =
      Fmts.getPacketFormats().getFormat(/*Occupied=*/0x1cu);
  const VLIWFormat *E2 =
      Fmts.getPacketFormats().getFormat(/*Occupied=*/0x3u);
  ASSERT_TRUE(E3 != nullptr || E2 != nullptr);
  if (E3) {
    EXPECT_STREQ(E3->Name, "BUNDLE_E96_THREE_ENTRY");
    SmallVector<MCSlotKind, 3> Slots;
    for (MCSlotKind S : E3->getSlots())
      Slots.push_back(S);
    ASSERT_EQ(Slots.size(), 3u);
    EXPECT_EQ(Slots[0], MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2));
    EXPECT_EQ(Slots[1], MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1));
    EXPECT_EQ(Slots[2], MCSlotKind(MCSlotKind::Haydn_SLOT_E3_0));
  }
  if (E2) {
    EXPECT_STREQ(E2->Name, "BUNDLE_E96_TWO_ENTRY");
    SmallVector<MCSlotKind, 2> Slots;
    for (MCSlotKind S : E2->getSlots())
      Slots.push_back(S);
    ASSERT_EQ(Slots.size(), 2u);
    EXPECT_EQ(Slots[0], MCSlotKind(MCSlotKind::Haydn_SLOT_E2_1));
    EXPECT_EQ(Slots[1], MCSlotKind(MCSlotKind::Haydn_SLOT_E2_0));
  }
}

//===----------------------------------------------------------------------===//
// SlotMap → Format.getSlots() child order independent of schedule input
//===----------------------------------------------------------------------===//

TEST(HaydnFormatOrdering, FieldOrderIgnoresScheduleInputOrder) {
  // Three committed Format E members (E3 e0/e1/e2, distinct units, one row).
  // SlotMap placement is independent of schedule order; Format.getSlots()
  // field walk is E3_2→E3_1→E3_0 regardless of add order.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst E0, E1, E2;
  E0.setOpcode(Haydn::ADD32_E3_E0_ALU0_RR);
  E1.setOpcode(Haydn::ADD32_E3_E1_ALU1_RR);
  E2.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);

  // Schedule input order: E0 then E1 then E2 (opposite of field order).
  ASSERT_TRUE(B.canAdd(E0.getOpcode()));
  B.add(&E0);
  ASSERT_TRUE(B.canAdd(E1.getOpcode()));
  B.add(&E1);
  ASSERT_TRUE(B.canAdd(E2.getOpcode()));
  B.add(&E2);

  ASSERT_EQ(B.size(), 3u);
  EXPECT_EQ(B.getInstrs()[0]->getOpcode(), Haydn::ADD32_E3_E0_ALU0_RR);
  EXPECT_EQ(B.getInstrs()[1]->getOpcode(), Haydn::ADD32_E3_E1_ALU1_RR);
  EXPECT_EQ(B.getInstrs()[2]->getOpcode(), Haydn::ADD32_E3_E2_ALU2_RR);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96_")) << Fmt->Name;

  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_0))->getOpcode(),
            Haydn::ADD32_E3_E0_ALU0_RR);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1))->getOpcode(),
            Haydn::ADD32_E3_E1_ALU1_RR);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2))->getOpcode(),
            Haydn::ADD32_E3_E2_ALU2_RR);

  SmallVector<unsigned, 3> Fields = fieldOrderOpcodes(B, *Fmt);
  ASSERT_EQ(Fields.size(), 3u);
  EXPECT_EQ(Fields[0], Haydn::ADD32_E3_E2_ALU2_RR);
  EXPECT_EQ(Fields[1], Haydn::ADD32_E3_E1_ALU1_RR);
  EXPECT_EQ(Fields[2], Haydn::ADD32_E3_E0_ALU0_RR);
}

TEST(HaydnFormatOrdering, FieldOrderFromReverseScheduleStillS2S1S0) {
  // Schedule input already E3_2→E3_1→E3_0 — SlotMap keyed by generated
  // entry kinds (field order E3_2→E3_1→E3_0).
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst E2, E1, E0;
  E2.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);
  E1.setOpcode(Haydn::ADD32_E3_E1_ALU1_RR);
  E0.setOpcode(Haydn::ADD32_E3_E0_ALU0_RR);
  B.add(&E2);
  B.add(&E1);
  B.add(&E0);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96_")) << Fmt->Name;
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2))->getOpcode(),
            Haydn::ADD32_E3_E2_ALU2_RR);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1))->getOpcode(),
            Haydn::ADD32_E3_E1_ALU1_RR);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_0))->getOpcode(),
            Haydn::ADD32_E3_E0_ALU0_RR);
}

TEST(HaydnFormatOrdering, SparsePairStillFieldOrder) {
  // Store is E3 entry 0 (LOADSTORE0); ADD32 is E3 entry 2 (ALU2).
  // SlotMap keeps both committed Format E members.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St, Ad;
  St.setOpcode(Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
  Ad.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);

  ASSERT_TRUE(B.canAdd(St.getOpcode()));
  B.add(&St);
  ASSERT_TRUE(B.canAdd(Ad.getOpcode()));
  B.add(&Ad);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96_")) << Fmt->Name;
  EXPECT_EQ(B.at(Fmts.getSlotKind(Ad.getOpcode()))->getOpcode(),
            Ad.getOpcode());
  EXPECT_EQ(B.at(Fmts.getSlotKind(St.getOpcode()))->getOpcode(),
            St.getOpcode());
}

//===----------------------------------------------------------------------===//
// getFormatOrNull + ProductFormatID stamp contract
//===----------------------------------------------------------------------===//

TEST(HaydnFormatOrdering, GetFormatOrNullReturnsProductRow) {
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, X;
  A.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);
  X.setOpcode(Haydn::ADD32_E3_E1_ALU1_RR);
  B.add(&A);
  B.add(&X);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96_")) << Fmt->Name;
  EXPECT_EQ(Fmt->getSize(), productParcelBytes().Value);
  // Size-filtered form: residual width must not select product.
  EXPECT_EQ(B.getFormatOrNull(/*Size=*/16), nullptr);
  const VLIWFormat *BySize =
      B.getFormatOrNull(/*Size=*/productParcelBytes().Value);
  ASSERT_NE(BySize, nullptr);
  EXPECT_TRUE(StringRef(BySize->Name).starts_with("BUNDLE_E96_"))
      << BySize->Name;
}

//===----------------------------------------------------------------------===//
// Member resolution: getSlotKind only (no AltDesc residual)
//===----------------------------------------------------------------------===//

TEST(HaydnFormatOrdering, MemberResolutionPrefersGetSlotKind) {
  // Post-setDesc ADD32_E3_E2_ALU2_RR has fixed kind E3_2 — SlotMap must
  // use that, not a re-auctioned tryAdd on a logical.
  HaydnMCFormats Fmts;
  MCSlotKind Fixed = Fmts.getSlotKind(Haydn::ADD32_E3_E2_ALU2_RR);
  ASSERT_NE(Fixed, MCSlotKind())
      << "ADD32_E3_E2_ALU2_RR must be a single-slot format member";
  EXPECT_EQ(Fixed, MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2));

  Bundle<MCInst> B(&Fmts);
  MCInst M;
  M.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);
  B.add(&M);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2)), &M);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1)), nullptr);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_0)), nullptr);
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
  unsigned Opc = Haydn::ADD32_E3_E2_ALU2_RR;
  MCSlotKind Fixed = Fmts.getSlotKind(Opc);
  ASSERT_NE(Fixed, MCSlotKind());
  EXPECT_EQ(Fixed, MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2));

  Bundle<MCInst> B(&Fmts);
  MCInst M;
  M.setOpcode(Opc);
  ASSERT_TRUE(B.canAdd(M.getOpcode()));
  B.add(&M);
  EXPECT_EQ(B.at(Fixed), &M);
  EXPECT_EQ(Fixed, MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2));
  EXPECT_EQ(static_cast<unsigned>(Fixed),
            static_cast<unsigned>(MCSlotKind::Haydn_SLOT_E3_2));
}

//===----------------------------------------------------------------------===//
// AsmPrinter Desc-only + Format->Opcode serialize
// (AIEBaseAsmPrinter.cpp:128-184 / :161-164 peer)
//===----------------------------------------------------------------------===//
// Printer walks Bundle SlotMap by reverse of Format.getSlots() (E3 encode
// order E3_0→E3_1→E3_2; field order is E3_2→E3_1→E3_0), NOP-pads empties,
// emits Format->Opcode composite, and fails closed on canAdd — no Flags
// re-slot / re-auction, no split.

// Pure data half of HaydnAsmPrinter composite emit: reverse of
// Format.getSlots() Bundle.at, with null for empty (caller inserts NOP).
static SmallVector<const MCInst *, 3>
encodeOrderSlots(const Bundle<MCInst> &B) {
  SmallVector<const MCInst *, 3> Out;
  const VLIWFormat *Fmt = B.getFormatOrNull();
  if (!Fmt)
    return Out;
  SmallVector<MCSlotKind, 3> Slots;
  for (MCSlotKind S : Fmt->getSlots())
    Slots.push_back(S);
  for (auto It = Slots.rbegin(); It != Slots.rend(); ++It)
    Out.push_back(B.at(*It));
  return Out;
}

TEST(HaydnFormatOrdering, ProductFormatOpcodeIsFormatEComposite) {
  // AIEBaseAsmPrinter.cpp:161-164 — MCBundle.setOpcode(Format->Opcode).
  // Product live rows are BUNDLE_E96_*; legacy full-width is residual only.
  HaydnMCFormats Fmts;
  const VLIWFormat *Fmt = productVLIWFormat(Fmts.getPacketFormats());
  ASSERT_NE(Fmt, nullptr);
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96_")) << Fmt->Name;
  EXPECT_TRUE(Fmt->Opcode == Haydn::BUNDLE_E96_TWO_ENTRY ||
              Fmt->Opcode == Haydn::BUNDLE_E96_THREE_ENTRY)
      << "AsmPrinter MCB.setOpcode(Format->Opcode) must be Format E composite";
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96"));

  // Residual issue occupancy falls back to product representative.
  for (SlotBits Occ :
       {SlotBits(0), SlotBits(Haydn::SLOT0),
        SlotBits(Haydn::SLOT1 | Haydn::SLOT2), SlotBits(Haydn::SLOT_ALL)}) {
    EXPECT_TRUE(productCovers(Fmts.getPacketFormats(), Occ)) << "occ=" << Occ;
    Bundle<MCInst> B(&Fmts);
    // Empty bundle still has product format representative via getFormat(0).
    if (Occ == 0) {
      const VLIWFormat *F = Fmts.getPacketFormats().getFormat(0);
      ASSERT_NE(F, nullptr);
      EXPECT_TRUE(StringRef(F->Name).starts_with("BUNDLE_E96"));
    }
  }
}

TEST(HaydnFormatOrdering, AsmPrinterEncodeOrderIsS0S1S2) {
  // Members committed as Format E E3 e2/e1/e0. Printer encode order is
  // reverse of Format.getSlots() (E3_0→E3_1→E3_2); composite is Format E.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst E0, E1, E2;
  E0.setOpcode(Haydn::ADD32_E3_E0_ALU0_RR);
  E1.setOpcode(Haydn::ADD32_E3_E1_ALU1_RR);
  E2.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);
  // Schedule / field-ish input: E2 first (opposite of encode).
  B.add(&E2);
  B.add(&E1);
  B.add(&E0);

  const VLIWFormat *Fmt = B.getFormatOrNull();
  ASSERT_NE(Fmt, nullptr);
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96_")) << Fmt->Name;
  EXPECT_TRUE(StringRef(Fmt->Name).starts_with("BUNDLE_E96"));

  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_0))->getOpcode(),
            Haydn::ADD32_E3_E0_ALU0_RR);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1))->getOpcode(),
            Haydn::ADD32_E3_E1_ALU1_RR);
  EXPECT_EQ(B.at(MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2))->getOpcode(),
            Haydn::ADD32_E3_E2_ALU2_RR);

  auto Enc = encodeOrderSlots(B);
  ASSERT_EQ(Enc.size(), 3u);
  ASSERT_NE(Enc[0], nullptr);
  ASSERT_NE(Enc[1], nullptr);
  ASSERT_NE(Enc[2], nullptr);
  EXPECT_EQ(Enc[0]->getOpcode(), Haydn::ADD32_E3_E0_ALU0_RR);
  EXPECT_EQ(Enc[1]->getOpcode(), Haydn::ADD32_E3_E1_ALU1_RR);
  EXPECT_EQ(Enc[2]->getOpcode(), Haydn::ADD32_E3_E2_ALU2_RR);
}

TEST(HaydnFormatOrdering, AsmPrinterSparseEncodePadsEmptyWithNull) {
  // E3 entry-0 store alone → encode slots [store, null, null]; printer
  // inserts NOP.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst St;
  St.setOpcode(Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
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
  // Two fixed E3 entry-0 store members: second canAdd must fail — printer
  // reports fatal instead of emergency multi-parcel split.
  HaydnMCFormats Fmts;
  Bundle<MCInst> B(&Fmts);
  MCInst A, Dup;
  A.setOpcode(Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
  Dup.setOpcode(Haydn::S_SW_WITH_IMM_E3_E0_LOADSTORE0_RI6);
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  EXPECT_FALSE(B.canAdd(Dup.getOpcode()))
      << "second E3-e0 store member must conflict (no printer re-auction)";
}

} // end anonymous namespace
