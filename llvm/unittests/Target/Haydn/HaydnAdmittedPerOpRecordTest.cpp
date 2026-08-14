//===- HaydnAdmittedPerOpRecordTest.cpp - per-op resource schema -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Differential unit seal for the reserved HaydnAdmittedPerOpResourceRecord
// schema (ports per bank, seven-unit mask, Data_Latency room, occupancy,
// required alignment). Admission stays closed: lookup is nullptr, CompleteModel
// pin is 0, and flipping admission without a generated table fails the pin
// helper. Zero-init is an empty seat, not a competitive claim.
//
//===----------------------------------------------------------------------===//

#include "HaydnPortModel.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

TEST(HaydnAdmittedPerOpRecordTest, SchemaFieldsExistAndZeroInitIsNotAClaim) {
  const HaydnAdmittedPerOpResourceRecord Rec;
  EXPECT_EQ(Rec.Opcode, 0u);
  EXPECT_EQ(Rec.GPRReadPorts, 0u);
  EXPECT_EQ(Rec.GPRWritePorts, 0u);
  EXPECT_EQ(Rec.DRReadPorts, 0u);
  EXPECT_EQ(Rec.DRWritePorts, 0u);
  EXPECT_EQ(Rec.ARReadPorts, 0u);
  EXPECT_EQ(Rec.ARWritePorts, 0u);
  EXPECT_EQ(Rec.SFRReadPorts, 0u);
  EXPECT_EQ(Rec.SFRWritePorts, 0u);
  EXPECT_EQ(Rec.UnitMask, 0u);
  EXPECT_EQ(Rec.DataLatency, 0u);
  EXPECT_EQ(Rec.OperandCycleCount, 0u);
  ASSERT_EQ(HAYDN_ADMITTED_OPERAND_CYCLE_ROOM, 8u);
  for (unsigned I = 0; I < HAYDN_ADMITTED_OPERAND_CYCLE_ROOM; ++I)
    EXPECT_EQ(Rec.OperandCycles[I], 0u) << "OperandCycles[" << I << "]";
  EXPECT_EQ(Rec.PipelineOccupancy, 0u);
  EXPECT_EQ(Rec.RequiredAlignment, 0u);

  EXPECT_EQ(HAYDN_ADMITTED_UNIT_LOADSTORE0, 1u << 0);
  EXPECT_EQ(HAYDN_ADMITTED_UNIT_LOAD1, 1u << 1);
  EXPECT_EQ(HAYDN_ADMITTED_UNIT_ALU0, 1u << 2);
  EXPECT_EQ(HAYDN_ADMITTED_UNIT_ALU1, 1u << 3);
  EXPECT_EQ(HAYDN_ADMITTED_UNIT_ALU2, 1u << 4);
  EXPECT_EQ(HAYDN_ADMITTED_UNIT_MAC0, 1u << 5);
  EXPECT_EQ(HAYDN_ADMITTED_UNIT_MAC1, 1u << 6);
  EXPECT_EQ(HAYDN_ADMITTED_UNIT_MASK_ALL,
            (1u << HAYDN_NUM_EXEC_UNITS) - 1u);
  EXPECT_EQ(HAYDN_NUM_EXEC_UNITS, 7u);

  EXPECT_FALSE(haydnCompetitivePerOpResourceClaimsAllowed(Rec.Opcode));
  EXPECT_EQ(haydnLookupAdmittedPerOpResourceRecord(Rec.Opcode), nullptr);
}

TEST(HaydnAdmittedPerOpRecordTest, FilledSeatWithoutLookupIsNotAClaim) {
  HaydnAdmittedPerOpResourceRecord Rec;
  Rec.Opcode = Haydn::ADD32;
  Rec.GPRReadPorts = 2;
  Rec.GPRWritePorts = 1;
  Rec.UnitMask = HAYDN_ADMITTED_UNIT_ALU0;
  Rec.DataLatency = 1;
  Rec.OperandCycleCount = 1;
  Rec.OperandCycles[0] = 1;
  Rec.PipelineOccupancy = 1;
  Rec.RequiredAlignment = 4;
  EXPECT_EQ(haydnLookupAdmittedPerOpResourceRecord(Rec.Opcode), nullptr);
  EXPECT_FALSE(haydnCompetitivePerOpResourceClaimsAllowed(Rec.Opcode));
}

TEST(HaydnAdmittedPerOpRecordTest, ProductPinsHoldWhileAdmissionClosed) {
  EXPECT_FALSE(haydnHasAdmittedPerOpResourceRecords());
  EXPECT_EQ(haydnLookupAdmittedPerOpResourceRecord(0), nullptr);
  EXPECT_EQ(haydnLookupAdmittedPerOpResourceRecord(Haydn::ADD32), nullptr);
  EXPECT_TRUE(haydnProductResourceAdmissionPinsHold());
  EXPECT_EQ(haydnSchedCompleteModelPin(), 0u);
  EXPECT_EQ(haydnCurrentGoldenAggregateResourceSurface().CompleteModel, 0u);
  EXPECT_FALSE(
      haydnCurrentGoldenAggregateResourceSurface().PerOpRecordsAdmitted);
}

TEST(HaydnAdmittedPerOpRecordTest, AdmissionFlipWithoutTableFailsPins) {
  EXPECT_FALSE(haydnProductResourceAdmissionPinsHoldAssuming(
      /*Admitted=*/true, /*TableHead=*/nullptr));
  EXPECT_TRUE(haydnProductResourceAdmissionPinsHoldAssuming(
      /*Admitted=*/false, /*TableHead=*/nullptr));

  HaydnAdmittedPerOpResourceRecord Dummy;
  EXPECT_FALSE(haydnProductResourceAdmissionPinsHoldAssuming(
      /*Admitted=*/true, &Dummy));
  EXPECT_FALSE(haydnProductResourceAdmissionPinsHoldAssuming(
      /*Admitted=*/false, &Dummy));

  EXPECT_TRUE(haydnProductResourceAdmissionPinsHold());
  EXPECT_FALSE(haydnHasAdmittedPerOpResourceRecords());
}

} // namespace
