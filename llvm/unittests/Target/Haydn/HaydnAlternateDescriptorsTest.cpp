//===- HaydnAlternateDescriptorsTest.cpp - opcode-alt map unit tests -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for HaydnAlternateDescriptors (opcode-alt map only):
//
//   AIE peer AIEAlternateDescriptors.h:27-75
//     setAlternateDescriptor / getSelectedOpcode / getDesc / clear
//
// No slot side-map (AIE has none). Post-commit placement is getSlotKind after
// leaveRegion setDesc + clear() (AIEMachineScheduler.cpp:1121-1132 / 1081-1082).
//
// Keys are raw MachineInstr* pointers. Unit tests use synthetic addresses as
// opaque keys — no MachineFunction required for map contracts. Opcode-alt
// injects use a stack MCInstrDesc with a known Opcode field.
//
//===----------------------------------------------------------------------===//

#include "HaydnAlternateDescriptors.h"
#include "HaydnBundleFormatSolver.h"
#include "HaydnPlacementAlternative.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::haydn::bundle;

namespace {

// AltDesc keys are raw MachineInstr* pointers. Unit tests use synthetic
// addresses as opaque keys — no MachineFunction required for map contracts.
static MachineInstr *fakeMI(uintptr_t Tag) {
  return reinterpret_cast<MachineInstr *>(Tag);
}

// Minimal MCInstrDesc with only Opcode set — enough for getSelectedOpcode.
static MCInstrDesc makeDesc(unsigned Opcode) {
  MCInstrDesc D;
  D.Opcode = Opcode;
  return D;
}

//===----------------------------------------------------------------------===//
// Opcode-alt map (AIE AIEAlternateDescriptors.h:39-74 shape)
//===----------------------------------------------------------------------===//

TEST(HaydnAlternateDescriptorsTest, EmptyHasNoSelectedOpcode) {
  HaydnAlternateDescriptors Alts;
  EXPECT_FALSE(Alts.getSelectedOpcode(fakeMI(0x1000)).has_value());
  EXPECT_FALSE(Alts.getSelectedDescriptor(fakeMI(0x1000)).has_value());
}

TEST(HaydnAlternateDescriptorsTest, SetAndGetSelectedOpcode) {
  // AIE peer: setAlternateDescriptor(MI, AltOpc) → getSelectedOpcode(MI).
  HaydnAlternateDescriptors Alts;
  MachineInstr *A = fakeMI(0x1000);
  MachineInstr *B = fakeMI(0x2000);
  MCInstrDesc DescA = makeDesc(/*Opcode=*/111);
  MCInstrDesc DescB = makeDesc(/*Opcode=*/222);

  Alts.setAlternateDescriptor(A, &DescA);
  Alts.setAlternateDescriptor(B, &DescB);

  ASSERT_TRUE(Alts.getSelectedOpcode(A).has_value());
  EXPECT_EQ(*Alts.getSelectedOpcode(A), 111u);
  ASSERT_TRUE(Alts.getSelectedOpcode(B).has_value());
  EXPECT_EQ(*Alts.getSelectedOpcode(B), 222u);
  EXPECT_FALSE(Alts.getSelectedOpcode(fakeMI(0x3000)).has_value());

  ASSERT_TRUE(Alts.getSelectedDescriptor(A).has_value());
  EXPECT_EQ(*Alts.getSelectedDescriptor(A), &DescA);
}

TEST(HaydnAlternateDescriptorsTest, OverwriteOpcodeIsLastWriterWins) {
  HaydnAlternateDescriptors Alts;
  MachineInstr *A = fakeMI(0x1000);
  MCInstrDesc First = makeDesc(10);
  MCInstrDesc Second = makeDesc(20);
  Alts.setAlternateDescriptor(A, &First);
  Alts.setAlternateDescriptor(A, &Second);
  ASSERT_TRUE(Alts.getSelectedOpcode(A).has_value());
  EXPECT_EQ(*Alts.getSelectedOpcode(A), 20u);
}

TEST(HaydnAlternateDescriptorsTest, GetDescFallsBackToMIDescWhenUnset) {
  // AIE peer AIEAlternateDescriptors.h:54-56: value_or(&MI->getDesc()).
  // Without a real MachineInstr we only exercise the selected path; unset
  // getSelectedDescriptor is nullopt (getDesc needs a live MI Desc).
  HaydnAlternateDescriptors Alts;
  EXPECT_FALSE(Alts.getSelectedDescriptor(fakeMI(0x1000)).has_value());
}

TEST(HaydnAlternateDescriptorsTest, ClearDropsAllOpcodes) {
  // AIE peer AIEAlternateDescriptors.h:74 clear() — leaveRegion end-state
  // after materialize (AIEMachineScheduler.cpp:1081-1082).
  HaydnAlternateDescriptors Alts;
  MachineInstr *A = fakeMI(0x1000);
  MachineInstr *B = fakeMI(0x2000);
  MCInstrDesc DA = makeDesc(/*Opcode=*/42);
  MCInstrDesc DB = makeDesc(/*Opcode=*/43);
  Alts.setAlternateDescriptor(A, &DA);
  Alts.setAlternateDescriptor(B, &DB);
  Alts.clear();
  EXPECT_FALSE(Alts.getSelectedOpcode(A).has_value());
  EXPECT_FALSE(Alts.getSelectedOpcode(B).has_value());
}

TEST(HaydnAlternateDescriptorsTest, PostMaterializeClearDropsOpcodes) {
  // materializeMultiOpcodeInstrs ends with full AltDescs.clear()
  // (AIE leaveRegion SelectedAltDescs.clear after setDesc).
  HaydnAlternateDescriptors Alts;
  MachineInstr *A = fakeMI(0x1000);
  MCInstrDesc D = makeDesc(/*Opcode=*/99);
  Alts.setAlternateDescriptor(A, &D);
  ASSERT_TRUE(Alts.getSelectedOpcode(A).has_value());
  EXPECT_EQ(*Alts.getSelectedOpcode(A), 99u);
  Alts.clear();
  EXPECT_FALSE(Alts.getSelectedOpcode(A).has_value());
  EXPECT_FALSE(Alts.getSelectedDescriptor(A).has_value());
}

TEST(HaydnAlternateDescriptorsTest, IndependentKeysDoNotCollide) {
  HaydnAlternateDescriptors Alts;
  MCInstrDesc Descs[8];
  for (unsigned I = 0; I < 8; ++I) {
    Descs[I] = makeDesc(/*Opcode=*/100 + I);
    Alts.setAlternateDescriptor(fakeMI(0x1000 + I * 0x10), &Descs[I]);
  }
  for (unsigned I = 0; I < 8; ++I) {
    auto Opc = Alts.getSelectedOpcode(fakeMI(0x1000 + I * 0x10));
    ASSERT_TRUE(Opc.has_value()) << "I=" << I;
    EXPECT_EQ(*Opc, 100u + I);
  }
}

TEST(HaydnAlternateDescriptorsTest, GetOpcodeHelper) {
  // AIE peer AIEAlternateDescriptors.h:70-72.
  HaydnAlternateDescriptors Alts;
  MachineInstr *A = fakeMI(0x1000);
  MCInstrDesc D = makeDesc(/*Opcode=*/555);
  Alts.setAlternateDescriptor(A, &D);
  EXPECT_EQ(Alts.getOpcode(A), 555u);
}

//===----------------------------------------------------------------------===//
// residualAltCompatibleFormatMask / mode-only predicates (W44 / P18(c))
//===----------------------------------------------------------------------===//

// REGRESSION TEST (W44 / P18(c), 2026-08-15):
//
// Bug: residualAltCompatibleFormatMask hand-transcribed the E2-only/E3-only
// opcode name sets as two C++ switches with count-only pins
// ("FormatEE2OnlyNames = 10") and a silent `default: return
// ProductFormatMask`. If a golden catalog update changed the SET membership
// while the count stayed 10/6 (rename, Mode move), the hand copies drifted,
// the switch fell through to the product mask, and an E2-only logical was
// wrongly admitted to a 3-wide E3 row (or an E3-only logical allowed to
// collapse FeasibleFormatMask to E2, which then finds no member and drops
// the cycle).
//
// Fix: the mask and both predicates derive from the generated
// FormatEE2OnlyNameSet / FormatEE3OnlyNameSet
// (HaydnGenFormatERecords.inc GET_FORMAT_E_MODE_ONLY_NAMES) via
// the generated mode-only name sets in HaydnAlternateDescriptors.cpp.
// HaydnFormatERecordsTest.ModeOnlyNameSetsCoverExactlyGeneratedRows proves
// set↔row equality in both directions.
//
// What breaks if the bug returns: re-introducing a name switch decouples
// admission from the generated sets; these pins fail as soon as the switch's
// first family drifts (or, for the E2Only-at-S2 pins, immediately if the S2
// drop is lost to the silent product-mask default).
TEST(HaydnAlternateDescriptorsTest, ResidualAltCompatibleFormatMaskPins) {
  const uint64_t E2Bit = formatRowBit(BundleFormatRowID::E96TwoEntry);
  const uint64_t E3Bit = formatRowBit(BundleFormatRowID::E96ThreeEntry);

  // E2-only family: entries below the E2 row width keep only the E2 row bit;
  // the third residual index (S2 / E3 entry 2) has no member and returns 0.
  const unsigned E2OnlyOps[] = {
      Haydn::ADDI32,  Haydn::ADDI32S, Haydn::ANDI32,  Haydn::MOVEI_H,
      Haydn::MOVEI_L, Haydn::ORI32,   Haydn::SET_HWLOOP, Haydn::SUBI32,
      Haydn::SUBI32S, Haydn::XORI32,
  };
  for (unsigned Opc : E2OnlyOps) {
    EXPECT_EQ(residualAltCompatibleFormatMask(Opc, 0), E2Bit)
        << haydnOpcodeName(Opc);
    EXPECT_EQ(residualAltCompatibleFormatMask(Opc, 1), E2Bit)
        << haydnOpcodeName(Opc);
    EXPECT_EQ(residualAltCompatibleFormatMask(Opc, 2), 0u)
        << haydnOpcodeName(Opc);
  }

  // E3-only family: every residual index stamps the E3 row bit only.
  const unsigned E3OnlyOps[] = {
      Haydn::ARCTAN, Haydn::EXP2,   Haydn::LOG2,
      Haydn::RECIP,  Haydn::SIN_COS, Haydn::SQRT,
  };
  for (unsigned Opc : E3OnlyOps) {
    for (unsigned Idx = 0; Idx < 3; ++Idx)
      EXPECT_EQ(residualAltCompatibleFormatMask(Opc, Idx), E3Bit)
          << haydnOpcodeName(Opc) << " idx=" << Idx;
  }

  // Dual-mode logicals keep the product frontier. Reloc `_W` identities
  // inherit the compact catalog logical's Mode in the residual mask too:
  // one peel rule (StripWide=true) for every consumer surface, so ADDI32_W
  // is E2-only exactly as ADDI32 is (MemberId occupancy is those generated
  // rows).
  //
  // REBASED 2026-08-21: the original law (pre-2026-08-20, W44-era) kept
  // ProductFormatMask for `_W` residuals via a second StripWide=false peel
  // mode, matching the pre-W44 switch default. be22ec604d9e ("Mode
  // occupancy row, not child/text count") unified classifyModeOnlySpelling
  // to StripWide=true — residual mask and coissue predicates now share one
  // mechanism over the W44 generated name sets. This pin tracks the unified
  // law; ProductFormatMask here would demand re-splitting the peel modes.
  EXPECT_EQ(residualAltCompatibleFormatMask(Haydn::ADD32, 0),
            ProductFormatMask);
  EXPECT_EQ(residualAltCompatibleFormatMask(Haydn::ADDI32_W, 0), E2Bit);
  EXPECT_EQ(residualAltCompatibleFormatMask(Haydn::SET_HWLOOP_F2_W, 0),
            ProductFormatMask);
  EXPECT_EQ(residualAltCompatibleFormatMask(Haydn::CSRW_W, 0),
            ProductFormatMask);

  // SET_HWLOOP_F2 / SET_HWLOOP_REG are dual-mode golden logicals (E3 rows
  // exist) — the bare SET_HWLOOP entry must not swallow them. They have no
  // bare logical enum; probe through a generated member of each.
  EXPECT_EQ(residualAltCompatibleFormatMask(
                Haydn::SET_HWLOOP_F2_E3_E0_ALU0_HWLRIIR, 2),
            ProductFormatMask);
  EXPECT_EQ(residualAltCompatibleFormatMask(
                Haydn::SET_HWLOOP_REG_E3_E0_ALU0_HWLRRRR, 2),
            ProductFormatMask);
}

TEST(HaydnAlternateDescriptorsTest, ModeOnlyOpcodeNamePredicates) {
  // E2-only: bare logical, member spelling, and reloc `_W` forms.
  EXPECT_TRUE(isFormatEE2OnlyOpcodeName("ADDI32"));
  EXPECT_TRUE(isFormatEE2OnlyOpcodeName("ADDI32_E2_E1_ALU1_RI20"));
  EXPECT_TRUE(isFormatEE2OnlyOpcodeName("ADDI32S"));
  EXPECT_TRUE(isFormatEE2OnlyOpcodeName("MOVEI_H"));
  EXPECT_TRUE(isFormatEE2OnlyOpcodeName("SET_HWLOOP"));
  EXPECT_TRUE(isFormatEE2OnlyOpcodeName("ADDI32_W"));
  // REBASED 2026-08-21: `_S<digits>` slot suffixes are RETIRED (0 defs in
  // TD; never-reintroduce list). The original 2026-08-16 expectation peeled
  // ADDI32_W_S0 → ADDI32_W → ADDI32 (E2-only) via a `_W_S0`-accepting peel.
  // peelLogicalOpcodeName now deliberately refuses `*_S<digits>` (returns
  // the spelling unchanged — occupancy must not recover ST8 from ST8_S0),
  // so a retired slot-suffixed spelling must NOT classify as E2-only.
  EXPECT_FALSE(isFormatEE2OnlyOpcodeName("ADDI32_W_S0"));
  EXPECT_FALSE(isFormatEE2OnlyOpcodeName("SET_HWLOOP_F2"));
  EXPECT_FALSE(isFormatEE2OnlyOpcodeName("SET_HWLOOP_F2_W"));
  EXPECT_FALSE(isFormatEE2OnlyOpcodeName("SET_HWLOOP_REG"));
  EXPECT_FALSE(isFormatEE2OnlyOpcodeName("ADD32"));
  EXPECT_FALSE(isFormatEE2OnlyOpcodeName("ADD32_E3_E2_ALU2_RR"));

  // E3-only: bare logical and member spellings.
  EXPECT_TRUE(isFormatEE3OnlyOpcodeName("ARCTAN"));
  EXPECT_TRUE(isFormatEE3OnlyOpcodeName("ARCTAN_E3_E0_ALU2_RI4"));
  EXPECT_TRUE(isFormatEE3OnlyOpcodeName("LOG2"));
  EXPECT_TRUE(isFormatEE3OnlyOpcodeName("SIN_COS"));
  EXPECT_FALSE(isFormatEE3OnlyOpcodeName("ADD32"));
  EXPECT_FALSE(isFormatEE3OnlyOpcodeName("ADDI32"));

  // Product code classifies MC-name-table spellings (enums in C++; the
  // string surface is haydnOpcodeName, never a magic opcode number).
  EXPECT_TRUE(isFormatEE2OnlyOpcodeName(haydnOpcodeName(Haydn::XORI32)));
  EXPECT_FALSE(isFormatEE3OnlyOpcodeName(haydnOpcodeName(Haydn::XORI32)));
}

} // namespace
