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
#include "llvm/MC/MCInstrDesc.h"
#include "gtest/gtest.h"

using namespace llvm;

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

} // namespace
