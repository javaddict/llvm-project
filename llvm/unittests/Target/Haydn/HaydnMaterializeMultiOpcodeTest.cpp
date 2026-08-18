//===- HaydnMaterializeMultiOpcodeTest.cpp - setDesc materialize -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for materializeMultiOpcodeInstrs (unconditional setDesc + clear):
//
//   AIE peer: AIEMachineScheduler.cpp:1121-1139
//     if (getSelectedOpcode(&MI)) MI.setDesc(TII->get(*AltOpcode));  // unconditional
//   AIE leaveRegion: AIEMachineScheduler.cpp:1081-1082
//     materializeMultiOpcodeInstrs(); SelectedAltDescs.clear();
//   AIEAlternateDescriptors.h:64-68 getSelectedOpcode; :74 clear()
//
// Without a full ScheduleDAG/MachineFunction, these tests pin the pure data
// path that leaveRegion materialize walks:
//
//   1. tryAddProduct chooses MemberOpcode (S2→S1→S0)
//   2. AltDescs records that MemberOpcode via setAlternateDescriptor
//   3. getSelectedOpcode is the setDesc target (format-member opcode)
//   4. setDesc is unconditional when selected (no NumOperands gate)
//   5. full clear() after materialize drops opcodes (AIEAlternateDescriptors.h:74)
//   6. post-commit Bundle canAdd via getSlotKind only (no slot side-map)
//
// : TableGen CodeGenFormat already proved logical→member setDesc shape
// (operands/ties/implicits/flags/sched) for every AlternateInsts entry. Runtime
// materialize therefore stays AIE-unconditional — no NumOperands/NumDefs gate
// and no second shape table. See SetDescCompat.td / SetDescIncompatible.td.
//
// Product: sole live composite row is BUNDLE_E96_TWO_ENTRY / BUNDLE_E96_THREE_ENTRY.
// and HazardRecognizerTest alt-try shape without inventing a second packer.
//
//===----------------------------------------------------------------------===//

#include "HaydnAlternateDescriptors.h"
#include "HaydnBundle.h"
#include "HaydnBundleMaterialize.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "MCTargetDesc/HaydnMCFormats.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "gtest/gtest.h"

// Opcode enums come via HaydnPortModel → HaydnMCTargetDesc (GET_INSTRINFO_ENUM).
// Do not re-include the enum; a second include conflicts.

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

static MachineInstr *fakeMI(uintptr_t Tag) {
  return reinterpret_cast<MachineInstr *>(Tag);
}

static MCInstrDesc makeDesc(unsigned Opcode) {
  MCInstrDesc D;
  D.Opcode = Opcode;
  return D;
}

// Pure materialize step shared with HaydnPostRASchedStrategy::
// materializeMultiOpcodeInstrs (AIEMachineScheduler.cpp:1126-1132 shape):
// resolve selected member opcode from AltDescs. Presence alone is sufficient
// for setDesc — no NumOperands/NumDefs shape-gate.
// Callers apply setDesc via TII->get(*Alt).
static std::optional<unsigned>
selectedMaterializeOpcode(HaydnAlternateDescriptors &AltDescs,
                          MachineInstr *MI) {
  return AltDescs.getSelectedOpcode(MI);
}

// Materialize policy (AIEMachineScheduler.cpp:1129-1131):
// if getSelectedOpcode → setDesc target is that opcode. No shape compare.
static bool wouldUnconditionalSetDesc(HaydnAlternateDescriptors &AltDescs,
                                      MachineInstr *MI,
                                      unsigned &OutMemberOpc) {
  if (std::optional<unsigned> Alt = AltDescs.getSelectedOpcode(MI)) {
    OutMemberOpc = *Alt;
    return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// tryAddProduct → MemberOpcode is the setDesc target
//===----------------------------------------------------------------------===//

TEST(HaydnMaterializeMultiOpcode, TryAddProductMemberIsSetDescTarget) {
  // Three ADD32s fill S2, S1, S0 (solver prefer high slots). Each MemberOpcode
  // is the format-member that materializeMultiOpcodeInstrs would setDesc.
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  ASSERT_EQ(S.Members.size(), 3u);

  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[0].MemberOpcode, 2));
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[1].MemberOpcode, 1));
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[2].MemberOpcode, 0));
  EXPECT_EQ(S.Members[0].LogicalOpcode, Haydn::ADD32);
  EXPECT_EQ(S.Members[1].LogicalOpcode, Haydn::ADD32);
  EXPECT_EQ(S.Members[2].LogicalOpcode, Haydn::ADD32);
}

TEST(HaydnMaterializeMultiOpcode, AltDescRecordsMemberForMaterialize) {
  // commitPlacementForEmit shape: after tryAdd, stamp setAlternateDescriptor
  // with MemberOpcode (AIEHazardRecognizer.cpp:389). materialize reads it.
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::LD32));
  ASSERT_EQ(S.Members.size(), 1u);
  // LD32 residual {S0, S1}; prefer high → S1. setDesc is the Format E member.
  EXPECT_TRUE(formatEMemberOccupiesEntry(S.Members[0].MemberOpcode, 1));

  HaydnAlternateDescriptors AltDescs;
  MachineInstr *MI = fakeMI(0xABCD);
  MCInstrDesc MemberDesc = makeDesc(S.Members[0].MemberOpcode);
  AltDescs.setAlternateDescriptor(MI, &MemberDesc);

  auto MaterializeOpc = selectedMaterializeOpcode(AltDescs, MI);
  ASSERT_TRUE(MaterializeOpc.has_value());
  EXPECT_EQ(*MaterializeOpc, S.Members[0].MemberOpcode);
  EXPECT_TRUE(formatEMemberOccupiesEntry(*MaterializeOpc, 1));
}

TEST(HaydnMaterializeMultiOpcode, ThreeMembersStampIndependentKeys) {
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::XOR32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::NOT32));

  HaydnAlternateDescriptors AltDescs;
  // Keep Desc storage alive for the map lifetime.
  MCInstrDesc Descs[3];
  MachineInstr *MIs[3] = {fakeMI(0x10), fakeMI(0x20), fakeMI(0x30)};
  for (unsigned I = 0; I < 3; ++I) {
    Descs[I] = makeDesc(S.Members[I].MemberOpcode);
    AltDescs.setAlternateDescriptor(MIs[I], &Descs[I]);
  }

  EXPECT_EQ(*selectedMaterializeOpcode(AltDescs, MIs[0]),
            S.Members[0].MemberOpcode);
  EXPECT_EQ(*selectedMaterializeOpcode(AltDescs, MIs[1]),
            S.Members[1].MemberOpcode);
  EXPECT_EQ(*selectedMaterializeOpcode(AltDescs, MIs[2]),
            S.Members[2].MemberOpcode);

  // After materialize, full clear() drops opcode map — AIE
  // SelectedAltDescs.clear() after setDesc (AIEMachineScheduler.cpp:1081-1082;
  // AIEAlternateDescriptors.h:74).
  AltDescs.clear();
  EXPECT_FALSE(selectedMaterializeOpcode(AltDescs, MIs[0]).has_value());
}

TEST(HaydnMaterializeMultiOpcode, NoAltMeansNoMaterialize) {
  // Ops without PlacementAlternatives never get setAlternateDescriptor;
  // materialize leaves the logical opcode untouched (AIE MaterializePseudo
  // only setDesc when getSelectedOpcode is present).
  HaydnAlternateDescriptors AltDescs;
  EXPECT_FALSE(selectedMaterializeOpcode(AltDescs, fakeMI(0x1)).has_value());
}

TEST(HaydnMaterializeMultiOpcode, ST32PlusADD64Members) {
  // BundleFormatSolverTest peer: ST32 claims S0; ADD64 takes high free slot.
  HaydnMCFormats Fmts;
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ST32));
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD64));
  ASSERT_EQ(S.Members.size(), 2u);

  HaydnAlternateDescriptors AltDescs;
  MCInstrDesc ST = makeDesc(S.Members[0].MemberOpcode);
  MCInstrDesc AD = makeDesc(S.Members[1].MemberOpcode);
  MachineInstr *MI0 = fakeMI(0x50);
  MachineInstr *MI1 = fakeMI(0x60);
  AltDescs.setAlternateDescriptor(MI0, &ST);
  AltDescs.setAlternateDescriptor(MI1, &AD);

  // ST32 is S0-only → member is ST32_S0 (or ST32 if identity).
  EXPECT_EQ(*selectedMaterializeOpcode(AltDescs, MI0),
            S.Members[0].MemberOpcode);
  EXPECT_EQ(*selectedMaterializeOpcode(AltDescs, MI1),
            S.Members[1].MemberOpcode);
  // Logical identity preserved on CycleMember until setDesc.
  EXPECT_EQ(S.Members[0].LogicalOpcode, Haydn::ST32);
  EXPECT_EQ(S.Members[1].LogicalOpcode, Haydn::ADD64);
}

TEST(HaydnMaterializeMultiOpcode, EnumerateAltsMatchSetDescCandidates) {
  // PlacementAlternative rows are exactly the opcodes materialize may setDesc.
  HaydnMCFormats Fmts;
  SmallVector<PlacementAlternative, 4> Alts;
  ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Haydn::ADD32, Alts));
  ASSERT_EQ(Alts.size(), 3u);
  EXPECT_TRUE(formatEMemberOccupiesEntry(Alts[0].MemberOpcode, 0));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Alts[1].MemberOpcode, 1));
  EXPECT_TRUE(formatEMemberOccupiesEntry(Alts[2].MemberOpcode, 2));

  // Any single tryAdd picks one of these three.
  CycleState S = makeProductCycleState();
  ASSERT_TRUE(tryAddProduct(S, Fmts, Haydn::ADD32));
  unsigned Chosen = S.Members[0].MemberOpcode;
  bool Found = false;
  for (const PlacementAlternative &A : Alts)
    if (A.MemberOpcode == Chosen)
      Found = true;
  EXPECT_TRUE(Found) << "MemberOpcode " << Chosen
                     << " not in PlacementAlternative set";
}

// getSlotKind on committed members is the MCInstLower placement source
// (no AltDesc slot read; Desc-as-is after setDesc).
TEST(HaydnMaterializeMultiOpcode, GetSlotKindIsPostCommitPlacement) {
  HaydnMCFormats Fmts;
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD32_E2_E0_ALU0_RR),
            MCSlotKind(MCSlotKind::Haydn_SLOT_E2_0));
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD32_E3_E1_ALU1_RR),
            MCSlotKind(MCSlotKind::Haydn_SLOT_E3_1));
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD32_E3_E2_ALU2_RR),
            MCSlotKind(MCSlotKind::Haydn_SLOT_E3_2));
  // Multi-slot logical has no fixed slot (alts path until setDesc).
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD32), MCSlotKind());
  // Format E entry kinds are pairwise distinct (not residual S*).
  EXPECT_NE(MCSlotKind::Haydn_SLOT_E2_0, MCSlotKind::Haydn_SLOT_E3_1);
  EXPECT_NE(MCSlotKind::Haydn_SLOT_E3_1, MCSlotKind::Haydn_SLOT_E3_2);
  EXPECT_NE(MCSlotKind::Haydn_SLOT_E2_0, MCSlotKind::Haydn_SLOT_E3_2);
}

// After setDesc, Bundle canAdd must accept committed members via getSlotKind
// (AIE AIEBundle.h:92-104; AIEBaseMCFormats.cpp:66-75) — not tryAddProduct.
TEST(HaydnMaterializeMultiOpcode, BundleCanAddCommittedMembers) {
  HaydnMCFormats Fmts;
  EXPECT_NE(Fmts.getSlotKind(Haydn::ADD32_E3_E0_ALU0_RR), MCSlotKind());
  EXPECT_NE(Fmts.getSlotKind(Haydn::ADD32_E3_E1_ALU1_RR), MCSlotKind());
  EXPECT_NE(Fmts.getSlotKind(Haydn::ADD32_E3_E2_ALU2_RR), MCSlotKind());
  // Multi-slot logical has no fixed slot (alts path).
  EXPECT_EQ(Fmts.getSlotKind(Haydn::ADD32), MCSlotKind());

  Haydn::Bundle<MCInst> B(&Fmts);
  MCInst A0, A1, A2;
  A0.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);
  A1.setOpcode(Haydn::ADD32_E3_E1_ALU1_RR);
  A2.setOpcode(Haydn::ADD32_E3_E0_ALU0_RR);
  ASSERT_TRUE(B.canAdd(A0.getOpcode()));
  B.add(&A0);
  ASSERT_TRUE(B.canAdd(A1.getOpcode()));
  B.add(&A1);
  ASSERT_TRUE(B.canAdd(A2.getOpcode()));
  B.add(&A2);
  EXPECT_TRUE(B.hasValidFormat());
  // Same member slot twice must fail.
  MCInst Dup;
  Dup.setOpcode(Haydn::ADD32_E3_E0_ALU0_RR);
  EXPECT_FALSE(B.canAdd(Dup.getOpcode()));
}

// cycleCanFormLegalBundle shape: Bundle.canAdd + getFormatOrNull only.
TEST(HaydnMaterializeMultiOpcode, ExactCommitBundleShapeGetSlotKindOnly) {
  HaydnMCFormats Fmts;
  Haydn::Bundle<MCInst> B(&Fmts);
  MCInst A, X;
  A.setOpcode(Haydn::ADD32_E3_E2_ALU2_RR);
  X.setOpcode(Haydn::ADD32_E3_E1_ALU1_RR);
  ASSERT_TRUE(B.canAdd(A.getOpcode()));
  B.add(&A);
  ASSERT_TRUE(B.canAdd(X.getOpcode()));
  B.add(&X);
  ASSERT_FALSE(B.isStandalone());
  ASSERT_NE(B.getFormatOrNull(), nullptr);
  EXPECT_TRUE(StringRef(B.getFormatOrNull()->Name).starts_with("BUNDLE_E96_"));
  // Shared legality authority (opcode view of the same members).
  unsigned Ops[] = {Haydn::ADD32_E3_E2_ALU2_RR, Haydn::ADD32_E3_E1_ALU1_RR};
  EXPECT_TRUE(opcodesFormOneLegalCycle(Ops, Fmts));
}

//===----------------------------------------------------------------------===//
// Unconditional setDesc; BREV members share logical shape
//===----------------------------------------------------------------------===//

TEST(HaydnMaterializeMultiOpcode, UnconditionalSetDescWhenSelected) {
  // AIEMachineScheduler.cpp:1129-1131 — presence of getSelectedOpcode alone
  // drives setDesc. No NumOperands/NumDefs compare.
  HaydnAlternateDescriptors AltDescs;
  MachineInstr *MI = fakeMI(0xB301);
  MCInstrDesc Member = makeDesc(Haydn::S_SW_BREV_IMM_E2_E0_LOADSTORE0_RI6);
  AltDescs.setAlternateDescriptor(MI, &Member);

  unsigned Out = 0;
  ASSERT_TRUE(wouldUnconditionalSetDesc(AltDescs, MI, Out));
  EXPECT_EQ(Out, Haydn::S_SW_BREV_IMM_E2_E0_LOADSTORE0_RI6);
  // After leaveRegion clear (AIE :1081-1082), no residual setDesc.
  AltDescs.clear();
  EXPECT_FALSE(wouldUnconditionalSetDesc(AltDescs, MI, Out));
}

TEST(HaydnMaterializeMultiOpcode, BrevStoreMembersAreSetDescTargets) {
  // S_SW_BREV_* / D_SDW_BREV_* PlacementAlternatives are live setDesc members.
  // tryAdd picks a Format E e0 member; S1/S2 FieldSlots remain Fallback.
  HaydnMCFormats Fmts;
  for (unsigned Logical :
       {Haydn::S_SW_BREV_IMM, Haydn::S_SW_BREV_REG, Haydn::D_SDW_BREV_IMM,
        Haydn::D_SDW_BREV_REG}) {
    SmallVector<PlacementAlternative, 4> Alts;
    ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Logical, Alts))
        << "logical " << Logical;
    ASSERT_FALSE(Alts.empty());

    CycleState S = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(S, Fmts, Logical)) << "logical " << Logical;
    ASSERT_EQ(S.Members.size(), 1u);
    unsigned Chosen = S.Members[0].MemberOpcode;
    EXPECT_NE(Chosen, Logical);
    EXPECT_EQ(S.Members[0].LogicalOpcode, Logical);

    bool Found = false;
    for (const PlacementAlternative &A : Alts)
      if (A.MemberOpcode == Chosen)
        Found = true;
    EXPECT_TRUE(Found) << "MemberOpcode " << Chosen << " not in alts for "
                       << Logical;

    HaydnAlternateDescriptors AltDescs;
    MachineInstr *MI = fakeMI(0x1000 + Logical);
    MCInstrDesc Member = makeDesc(Chosen);
    AltDescs.setAlternateDescriptor(MI, &Member);
    unsigned Out = 0;
    ASSERT_TRUE(wouldUnconditionalSetDesc(AltDescs, MI, Out));
    EXPECT_EQ(Out, Chosen);
    // Post-setDesc placement authority is getSlotKind on the member.
    EXPECT_NE(Fmts.getSlotKind(Chosen), MCSlotKind())
        << "member " << Chosen << " must have fixed slot after setDesc";
  }
}

TEST(HaydnMaterializeMultiOpcode, BrevLoadMembersAreSetDescTargets) {
  // D_LDW_BREV_* / S_LW_BREV_* alts are LS *_S* members (tied shape aligned
  // with logical). LD *_LD_S* are encode peers, not PlacementAlternatives.
  HaydnMCFormats Fmts;
  for (unsigned Logical : {Haydn::D_LDW_BREV_IMM, Haydn::D_LDW_BREV_REG,
                           Haydn::S_LW_BREV_IMM, Haydn::S_LW_BREV_REG}) {
    SmallVector<PlacementAlternative, 4> Alts;
    ASSERT_TRUE(enumeratePlacementAlternatives(Fmts, Logical, Alts))
        << "logical " << Logical;
    CycleState S = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(S, Fmts, Logical)) << "logical " << Logical;
    unsigned Chosen = S.Members[0].MemberOpcode;
    EXPECT_NE(Chosen, Logical);

    HaydnAlternateDescriptors AltDescs;
    MachineInstr *MI = fakeMI(0x2000 + Logical);
    MCInstrDesc Member = makeDesc(Chosen);
    AltDescs.setAlternateDescriptor(MI, &Member);
    auto Mat = selectedMaterializeOpcode(AltDescs, MI);
    ASSERT_TRUE(Mat.has_value());
    EXPECT_EQ(*Mat, Chosen);
    EXPECT_NE(Fmts.getSlotKind(Chosen), MCSlotKind());
  }
}

//===----------------------------------------------------------------------===//
// Late layout firewall — empty-cycle tryAdd setDesc (commitLateProductCycle)
//===----------------------------------------------------------------------===//
//
// AIE peers: AIEMachineScheduler.cpp:1121-1139 materializeMultiOpcodeInstrs;
// AIEHazardRecognizer.cpp:174-214 empty Bundle alt try; AIE PreEmit empty
// (AIE2TargetMachine.cpp:88) so AIE never re-runs this path. Haydn BR inserts
// and FixupHwLoops deficit pads / demote trip-mat / stack LD-ST / soft edges /
// exit B call commitLateProductCycle at creation
// ; late Finalize remains the idempotent
// firewall for any residual bare MI.

TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleADD32) {
  // CLOSED singleton takes the documented default row (ProductDefaultRowID =
  // E96TwoEntry; singleton-bundle-formatid.mir pins the same BUNDLE 0). The
  // former expectation here (entry 2) pinned the OPEN-cycle S2-first fill
  // heuristic leaking into the late firewall — that is CB-152b, settled for
  // the doc/MIR side. The preferred E2-committable state lands on e0.
  HaydnMCFormats Fmts;
  auto C = commitLateProductCycle(Haydn::ADD32, Fmts);
  ASSERT_TRUE(C.has_value());
  EXPECT_EQ(C->LogicalOpcode, Haydn::ADD32);
  EXPECT_TRUE(formatEMemberOccupiesEntry(C->MemberOpcode, 0));
  EXPECT_EQ(C->Plan.Row, haydn::bundle::BundleFormatRowID::E96TwoEntry);
  EXPECT_TRUE(C->NeedsSetDesc);
  EXPECT_TRUE(isProductBundleRow(C->Plan.Row));
  EXPECT_EQ(C->Plan.Bytes.Value, productParcelBytes().Value);
  EXPECT_TRUE(C->Plan.isProductLegal());

  auto SetDesc = lateSingletonSetDescOpcode(Haydn::ADD32, Fmts);
  ASSERT_TRUE(SetDesc.has_value());
  EXPECT_TRUE(formatEMemberOccupiesEntry(*SetDesc, 0));
}

TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleNOP) {
  // Pad NOP occupancy is a generated HINT member (Haydn::NOP). FieldSlot
  // NOP_S0 is retired.
  HaydnMCFormats Fmts;
  auto C = commitLateProductCycle(Haydn::NOP, Fmts);
  ASSERT_TRUE(C.has_value());
  EXPECT_EQ(C->LogicalOpcode, Haydn::NOP);
  EXPECT_EQ(C->MemberOpcode, Haydn::NOP);
  EXPECT_TRUE(isProductBundleRow(C->Plan.Row));

  auto SetDesc = lateSingletonSetDescOpcode(Haydn::NOP, Fmts);
  if (SetDesc.has_value())
    EXPECT_EQ(*SetDesc, Haydn::NOP);
}

TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleDemoteSoftEdge) {
  // Final-real demotion soft edge: SUBI32 + BNEZ_W (no residual LoopDec /
  // LoopJNZ). Shared empty-cycle exact-commit must accept both as product
  // singletons so late creators can setDesc + finalize before the second
  // BranchRelaxation. Member choice is the same surface object encode sees
  // (prefer high slots; E2-only SUBI32 drops residual S2 so S1 is highest).
  HaydnMCFormats Fmts;
  auto Dec = commitLateProductCycle(Haydn::SUBI32, Fmts);
  ASSERT_TRUE(Dec.has_value());
  EXPECT_EQ(Dec->LogicalOpcode, Haydn::SUBI32);
  EXPECT_TRUE(formatEMemberOccupiesEntry(Dec->MemberOpcode, 1));
  EXPECT_TRUE(Dec->NeedsSetDesc);
  // FE8: BundlePlan.FID residual field is removed; Format E row identity is
  // the stamped BundleFormatRowID via stampBundleCommit.
  EXPECT_TRUE(isProductBundleRow(Dec->Plan.Row));
  EXPECT_TRUE(Dec->Plan.isProductLegal());
  EXPECT_EQ(Dec->Plan.Bytes.Value, productParcelBytes().Value);

  auto Br = commitLateProductCycle(Haydn::BNEZ_W, Fmts);
  ASSERT_TRUE(Br.has_value());
  EXPECT_EQ(Br->LogicalOpcode, Haydn::BNEZ_W);
  EXPECT_TRUE(formatEMemberOccupiesEntry(Br->MemberOpcode, 0));
  EXPECT_TRUE(Br->NeedsSetDesc);
  EXPECT_TRUE(isProductBundleRow(Br->Plan.Row));
  EXPECT_TRUE(Br->Plan.isProductLegal());
  EXPECT_EQ(Br->Plan.Bytes.Value, productParcelBytes().Value);

  // lateProductMemberOpcode is the shared hook late creators call.
  EXPECT_TRUE(formatEMemberOccupiesEntry(lateProductMemberOpcode(Haydn::SUBI32),
                                        1));
  EXPECT_TRUE(formatEMemberOccupiesEntry(lateProductMemberOpcode(Haydn::BNEZ_W),
                                        0));
}

TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleDemoteTripMaterialize) {
 // demote trip materialize + stack-counter glue must exact-commit:
  // XOR32 / ADDI32_W / MOVE32 / ST32 / LD32 / B (exit).
  HaydnMCFormats Fmts;
  for (unsigned Opc : {Haydn::XOR32, Haydn::ADDI32_W, Haydn::MOVE32, Haydn::ST32,
                       Haydn::LD32, Haydn::B}) {
    auto C = commitLateProductCycle(Opc, Fmts);
    ASSERT_TRUE(C.has_value()) << "opc " << Opc;
    EXPECT_TRUE(isProductBundleRow(C->Plan.Row)) << "opc " << Opc;
    EXPECT_TRUE(C->Plan.isProductLegal()) << "opc " << Opc;
    EXPECT_NE(C->MemberOpcode, 0u) << "opc " << Opc;
  }
}

TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleAlreadyMember) {
  // Already-setDesc member: no alts → wrap-only, NeedsSetDesc false.
  HaydnMCFormats Fmts;
  auto C = commitLateProductCycle(Haydn::ADD32_E3_E2_ALU2_RR, Fmts);
  ASSERT_TRUE(C.has_value());
  EXPECT_EQ(C->MemberOpcode, Haydn::ADD32_E3_E2_ALU2_RR);
  EXPECT_FALSE(C->NeedsSetDesc);
  EXPECT_FALSE(lateSingletonSetDescOpcode(Haydn::ADD32_E3_E2_ALU2_RR, Fmts)
                   .has_value());
}

TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleNoAltWrapOnly) {
  // B / RET have no PlacementAlternatives — explicit ProductFormatID singleton
  // without setDesc (AIE would never insert these post-finalize; Haydn BR does).
  HaydnMCFormats Fmts;
  for (unsigned Opc : {Haydn::B, Haydn::RET}) {
    auto C = commitLateProductCycle(Opc, Fmts);
    ASSERT_TRUE(C.has_value()) << "opc " << Opc;
    EXPECT_EQ(C->MemberOpcode, Opc);
    EXPECT_FALSE(C->NeedsSetDesc) << "opc " << Opc;
    EXPECT_TRUE(isProductBundleRow(C->Plan.Row));
    EXPECT_FALSE(lateSingletonSetDescOpcode(Opc, Fmts).has_value());
  }
}

TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleBEQZ) {
  // BranchRelaxation / insertBranch emit bare BEQZ. Occupancy prefers the
  // Format E e0 member; residual BEQZ_S0 is not a required identity.
  HaydnMCFormats Fmts;
  auto C = commitLateProductCycle(Haydn::BEQZ, Fmts);
  ASSERT_TRUE(C.has_value());
  EXPECT_TRUE(formatEMemberOccupiesEntry(C->MemberOpcode, 0));
  EXPECT_TRUE(C->NeedsSetDesc);
}

TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleMatchesEmptyTryAdd) {
  // The firewall shares the empty-cycle tryAdd expansion — same candidate
  // set, same legality — but a CLOSED singleton settles on the documented
  // E2 default row (CB-152b), so the committed member is the preferred
  // E2-committable state when one exists, not necessarily the raw open-cycle
  // (S2-first) pick. Assert set-membership plus the row policy rather than
  // pointer-equality with the open-cycle heuristic.
  HaydnMCFormats Fmts;
  for (unsigned Logical :
       {Haydn::ADD32, Haydn::XOR32, Haydn::LD32, Haydn::ST32, Haydn::ADDI32,
        Haydn::NOP}) {
    CycleState S = makeProductCycleState();
    ASSERT_TRUE(tryAddProduct(S, Fmts, Logical)) << "logical " << Logical;
    auto C = commitLateProductCycle(Logical, Fmts);
    ASSERT_TRUE(C.has_value());
    // The late member must still be one of the logical's alternatives (no
    // second theory of legality)...
    const std::vector<unsigned> *Alts =
        Fmts.getAlternateInstsOpcode(Logical);
    if (Alts) {
      EXPECT_TRUE(llvm::is_contained(*Alts, C->MemberOpcode) ||
                  C->MemberOpcode == Logical)
          << "late member not in alt set for " << Logical;
    } else {
      EXPECT_EQ(C->MemberOpcode, S.Members[0].MemberOpcode);
    }
    // ...and a closed singleton with any E2-committable state stamps the
    // documented default row.
    if (formatECompositeSlotIsE2(Fmts.getSlotKind(C->MemberOpcode)))
      EXPECT_EQ(C->Plan.Row, haydn::bundle::BundleFormatRowID::E96TwoEntry)
          << "logical " << Logical;
  }
}

// Fixed BR → FixupHwLoops → BR → late Finalize/Verify multi-pass charges every
// late residual as one Full parcel (EncodedBytes == productParcelBytes). The
// committed row may not compact or shrink later; layout size and format row
// stay locked to a Format E product row.
TEST(HaydnMaterializeMultiOpcode, CommitLateProductCycleStableRowFullParcel) {
  HaydnMCFormats Fmts;
  const unsigned FullBytes = productParcelBytes().Value;
  ASSERT_EQ(FullBytes, productParcelBytes().Value);
  for (unsigned Opc : {Haydn::ADD32, Haydn::XOR32, Haydn::OR32, Haydn::NOP,
                       Haydn::B, Haydn::BEQZ, Haydn::BNEZ_W, Haydn::SUBI32,
                       Haydn::ST32, Haydn::LD32, Haydn::ADDI32_W, Haydn::MOVE32,
                       Haydn::RET, Haydn::JALR, Haydn::XOR32,
                       Haydn::ADD32_E3_E2_ALU2_RR, Haydn::NOP,
                       Haydn::BNEZ_E2_E0_ALU0_I12,
                       Haydn::SUBI32_E2_E1_ALU1_RI20}) {
    auto C = commitLateProductCycle(Opc, Fmts);
    ASSERT_TRUE(C.has_value()) << "opc " << Opc;
    EXPECT_TRUE(isProductBundleRow(C->Plan.Row)) << "opc " << Opc;
    EXPECT_EQ(C->Plan.Bytes.Value, FullBytes) << "opc " << Opc;
    EXPECT_TRUE(C->Plan.isProductLegal()) << "opc " << Opc;
    // Stable-row: EncodedBytes charged to layout equals registry product parcel.
    auto EB = encodedBytesForRow(C->Plan.Row);
    ASSERT_TRUE(EB.has_value()) << "opc " << Opc;
    EXPECT_EQ(EB->Value, FullBytes) << "opc " << Opc;
  }
}

// BranchRelaxation insert/remove hooks share lateProductMemberOpcode with
// Fixup pads so every late creator charges one Full parcel (EncodedBytes).
TEST(HaydnMaterializeMultiOpcode, LateProductMemberOpcodeBranchHooks) {
  HaydnMCFormats Fmts;
  const unsigned FullBytes = productParcelBytes().Value;
  for (unsigned Opc : {Haydn::B, Haydn::BEQZ, Haydn::BEQZ_W, Haydn::BNEZ_W,
                       Haydn::LUI, Haydn::ADDI32_W, Haydn::JALR_W}) {
    unsigned Member = lateProductMemberOpcode(Opc);
    auto C = commitLateProductCycle(Opc, Fmts);
    ASSERT_TRUE(C.has_value()) << "opc " << Opc;
    EXPECT_EQ(Member, C->MemberOpcode) << "opc " << Opc;
    EXPECT_EQ(C->Plan.Bytes.Value, FullBytes) << "opc " << Opc;
    // Bare real size model matches committed Full parcel before finalize;
    // insert hooks charge the same number on the BUNDLE root after wrap.
    EXPECT_EQ(C->Plan.Bytes.Value, productParcelBytes().Value) << "opc " << Opc;
  }
  // B / RET have no PlacementAlternatives → wrap-only member == logical.
  EXPECT_EQ(lateProductMemberOpcode(Haydn::B), Haydn::B);
  EXPECT_EQ(lateProductMemberOpcode(Haydn::RET), Haydn::RET);
}

TEST(HaydnMaterializeMultiOpcode, MakeProductPlanForOpcodesDerivesE3Row) {
  // F13: glueDefToUse stamps makeProductPlanForOpcodes, not hardcoded
  // E96TwoEntry. An E3-only pair must select E96ThreeEntry.
  const unsigned E3Pair[] = {Haydn::ADD32_E3_E0_ALU0_RR,
                             Haydn::ADD32_E3_E1_ALU1_RR};
  EXPECT_EQ(selectProductRowForOpcodes(E3Pair),
            BundleFormatRowID::E96ThreeEntry);
  BundlePlan Plan = makeProductPlanForOpcodes(/*Occupied=*/0, E3Pair);
  EXPECT_EQ(Plan.Row, BundleFormatRowID::E96ThreeEntry);
  EXPECT_EQ(Plan.Completion, CompletionStateID::AllEntriesReal);
  EXPECT_EQ(Plan.memberCount(), 2u);

  const unsigned E2Pair[] = {Haydn::ADD32, Haydn::LD32};
  BundlePlan E2Plan = makeProductPlanForOpcodes(/*Occupied=*/0, E2Pair);
  EXPECT_EQ(E2Plan.Row, selectProductRowForOpcodes(E2Pair));
}

} // namespace
