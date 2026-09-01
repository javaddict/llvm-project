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

// ---------------------------------------------------------------------------
// M18 golden per-op import (HaydnGenPerOpResources.inc). Golden FACTS only:
// the import is partial (806 covered / 44 uncovered census), so every
// admission pin above stays byte-identical and CompleteModel stays 0.
// ---------------------------------------------------------------------------

TEST(HaydnAdmittedPerOpRecordTest, GoldenImportCensusPins) {
  // Census pins: coverage counts are generator-owned and must move ONLY
  // with a golden index change (ratchet like the setDesc ledger).
  EXPECT_EQ(haydnGoldenPerOpRecordCount(), 806u);
  EXPECT_EQ(haydnGoldenPerOpUncoveredCount(), 44u);
  // The import itself does NOT flip admission or CompleteModel.
  EXPECT_FALSE(haydnHasAdmittedPerOpResourceRecords());
  EXPECT_EQ(haydnSchedCompleteModelPin(), 0u);
  EXPECT_FALSE(haydnCompetitiveIIDensityClaimsAllowed());
}

TEST(HaydnAdmittedPerOpRecordTest, GoldenImportLatencyFacts) {
  // golden instruction_type_index Pipeline_Info Data_Latency, now served
  // from the generated table (the 2026-08-21 itinerary re-map comments in
  // HaydnInstrInfo.td document the same golden fields).
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::ADD32), 1u);  // RR ALU lat 1
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::CSRR), 2u);   // I8 CsrLat 2
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::LOG2), 2u);   // R DspLat 2
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::SQRT), 2u);
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::EXP2), 2u);
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::RECIP), 2u);
  // SIN_COS/ARCTAN are (uimm4+2): no scalar; the published conservative
  // dest bound 17 rides OperandCycles[0].
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::SIN_COS), 17u);
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::ARCTAN), 17u);
  // Golden-silent surface: store sides publish no Data_Latency (0 = no
  // claim), and the load side keeps golden 2.
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::S_SW_WITH_IMM), 0u);
  // Uncovered census member: NOP has no golden row, lookup is nullptr.
  EXPECT_EQ(haydnGetAdmittedPerOpResourceRecord(Haydn::NOP), nullptr);
  EXPECT_EQ(haydnGoldenDataLatency(Haydn::NOP), 0u);
}

TEST(HaydnAdmittedPerOpRecordTest, GoldenImportUnitAndPortFacts) {
  // ADD32: golden Available ALU0|ALU1|ALU2 (RR), 2 GPR reads, 1 GPR write.
  const auto *Add = haydnGetAdmittedPerOpResourceRecord(Haydn::ADD32);
  ASSERT_NE(Add, nullptr);
  EXPECT_EQ(Add->Opcode, static_cast<unsigned>(Haydn::ADD32));
  EXPECT_EQ(Add->UnitMask, HAYDN_ADMITTED_UNIT_ALU0 |
                               HAYDN_ADMITTED_UNIT_ALU1 |
                               HAYDN_ADMITTED_UNIT_ALU2);
  EXPECT_EQ(Add->GPRReadPorts, 2u);
  EXPECT_EQ(Add->GPRWritePorts, 1u);
  EXPECT_EQ(Add->PipelineOccupancy, 1u);
  // CSRR: golden I8, ALU0|ALU1|ALU2, writes rt only.
  const auto *Csr = haydnGetAdmittedPerOpResourceRecord(Haydn::CSRR);
  ASSERT_NE(Csr, nullptr);
  EXPECT_EQ(Csr->GPRWritePorts, 1u);
  EXPECT_EQ(Csr->DataLatency, 2u);
  // SEQ64: DR reads rsd1/rsd2, SFR write is the only semantic output.
  const auto *Seq = haydnGetAdmittedPerOpResourceRecord(Haydn::SEQ64);
  ASSERT_NE(Seq, nullptr);
  EXPECT_EQ(Seq->DRReadPorts, 2u);
  EXPECT_EQ(Seq->SFRWritePorts, 1u);
  // D_LDW_POST_IMM: dual load menu LOADSTORE0|LOAD1, GPR rs read+writeback.
  const auto *Ld = haydnGetAdmittedPerOpResourceRecord(Haydn::D_LDW_POST_IMM);
  ASSERT_NE(Ld, nullptr);
  EXPECT_EQ(Ld->UnitMask, HAYDN_ADMITTED_UNIT_LOADSTORE0 |
                              HAYDN_ADMITTED_UNIT_LOAD1);
  EXPECT_EQ(Ld->GPRReadPorts, 1u);
  EXPECT_EQ(Ld->GPRWritePorts, 1u);
  EXPECT_EQ(Ld->DataLatency, 2u);
}

} // namespace
