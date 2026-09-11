//===- HaydnCallRelaxTest.cpp - cycle-neutral call encoding -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HaydnFormatERecords.h"
#include "MCTargetDesc/HaydnCallRelax.h"
#include "MCTargetDesc/HaydnFormat.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "gtest/gtest.h"
#include "llvm/MC/MCRegisterInfo.h"

#include <cstring>

using namespace llvm;
using namespace llvm::haydn;

TEST(HaydnCallRelax, E2JalSingletonMatchesMC) {
  ArrayRef<uint8_t> Idle = format::canonicalFullSlotIdleParcel();
  ASSERT_EQ(Idle.size(), 12u);

  uint8_t JalLR[12] = {};
  ASSERT_TRUE(call_relax::writeE2JalSingleton(JalLR, 12, /*RtEnc=*/15));
  // llvm-mc `{ jal lr, 0 }` → 07 0e f8 00 + 8 zero bytes.
  const uint8_t WantLR[12] = {0x07, 0x0e, 0xf8, 0x00};
  EXPECT_EQ(std::memcmp(JalLR, WantLR, 4), 0);
  EXPECT_EQ(std::memcmp(JalLR + 4, WantLR + 4, 8), 0);
  EXPECT_TRUE(call_relax::e2SingletonLogical(JalLR, 12, "JAL"));
  unsigned Rt = 0;
  ASSERT_TRUE(call_relax::readE2E0DestEnc(JalLR, 12, Rt));
  EXPECT_EQ(Rt, 15u);

  uint8_t JalR12[12] = {};
  ASSERT_TRUE(call_relax::writeE2JalSingleton(JalR12, 12, /*RtEnc=*/12));
  const uint8_t WantR12[12] = {0x07, 0x0e, 0xc8, 0x00};
  EXPECT_EQ(std::memcmp(JalR12, WantR12, 4), 0);
  EXPECT_TRUE(call_relax::e2SingletonLogical(JalR12, 12, "JAL"));
  ASSERT_TRUE(call_relax::readE2E0DestEnc(JalR12, 12, Rt));
  EXPECT_EQ(Rt, 12u);
}

TEST(HaydnCallRelax, CapturedGeneralCallSingletons) {
  // llvm-mc of the ISel general form (imm 0, reloc applied later):
  //   lui r1, %hi12(callee)     07 0a 12 00 ...
  //   addi32 r1, r1, %lo20(...) 07 0f 12 01 ...
  //   jalr lr, r1, 0            07 0d f2 01 ...
  const uint8_t Lui[12] = {0x07, 0x0a, 0x12, 0x00};
  const uint8_t Addi[12] = {0x07, 0x0f, 0x12, 0x01};
  const uint8_t Jalr[12] = {0x07, 0x0d, 0xf2, 0x01};
  EXPECT_TRUE(call_relax::e2SingletonLogical(Lui, 12, "LUI"));
  EXPECT_TRUE(call_relax::e2SingletonLogical(Addi, 12, "ADDI32"));
  EXPECT_TRUE(call_relax::e2SingletonLogical(Jalr, 12, "JALR"));
  EXPECT_FALSE(call_relax::e2SingletonLogical(Jalr, 12, "JAL"));
  unsigned Rt = 0;
  ASSERT_TRUE(call_relax::readE2E0DestEnc(Jalr, 12, Rt));
  EXPECT_EQ(Rt, 15u);
}

TEST(HaydnCallRelax, GeneratedWindowsAndLinkRegEncoding) {
  EXPECT_EQ(format_e::FormatEE2E0DestLsb, 20u);
  EXPECT_EQ(format_e::FormatEE2E0RsLsb, 24u);
  EXPECT_EQ(format_e::FormatEGPRFieldBits, 4u);
  const MCRegisterInfo &MRI = getHaydnSharedMCRegisterInfo();
  EXPECT_EQ(MRI.getEncodingValue(Haydn::R15), 15u);

  uint8_t JalLR[12] = {};
  ASSERT_TRUE(call_relax::writeE2JalSingleton(
      JalLR, 12, MRI.getEncodingValue(Haydn::R15)));
  const uint8_t WantLR[12] = {0x07, 0x0e, 0xf8, 0x00};
  EXPECT_EQ(std::memcmp(JalLR, WantLR, 4), 0);
  unsigned Rt = 0;
  ASSERT_TRUE(call_relax::readE2E0DestEnc(JalLR, 12, Rt));
  EXPECT_EQ(Rt, MRI.getEncodingValue(Haydn::R15));
}

TEST(HaydnCallRelax, MixedPacketDoesNotLookLikeSingleton) {
  // `{ xor32; lui }` is not an LUI singleton — e1 is live. Do not relax.
  const uint8_t Mixed[12] = {0x07, 0x8b, 0x00, 0x00, 0x0a, 0x12};
  EXPECT_FALSE(call_relax::e2SingletonLogical(Mixed, 12, "LUI"));
}

TEST(HaydnCallRelax, ReturningCallTripleOnly) {
  // Captured ISel general returning call (jalr lr, r1).
  const uint8_t Lui[12] = {0x07, 0x0a, 0x12, 0x00};
  const uint8_t Addi[12] = {0x07, 0x0f, 0x12, 0x01};
  const uint8_t JalrCall[12] = {0x07, 0x0d, 0xf2, 0x01};
  EXPECT_TRUE(call_relax::isReturningCallRelaxTriple(Lui, Addi, JalrCall, 12));
  unsigned Rt = 0, Rs = 0, LuiRd = 0, AddiRd = 0;
  ASSERT_TRUE(call_relax::readE2E0DestEnc(JalrCall, 12, Rt));
  ASSERT_TRUE(call_relax::readE2E0RsEnc(JalrCall, 12, Rs));
  ASSERT_TRUE(call_relax::readE2E0DestEnc(Lui, 12, LuiRd));
  ASSERT_TRUE(call_relax::readE2E0DestEnc(Addi, 12, AddiRd));
  EXPECT_EQ(Rt, 15u);
  EXPECT_EQ(Rs, 1u);
  EXPECT_EQ(LuiRd, 1u);
  EXPECT_EQ(AddiRd, 1u);

  // Mixed packet in the JALR slot is not a returning-call triple.
  const uint8_t Mixed[12] = {0x07, 0x8b, 0x00, 0x00, 0x0a, 0x12};
  EXPECT_FALSE(call_relax::isReturningCallRelaxTriple(Lui, Addi, Mixed, 12));

  // JAL (not JALR) in the third slot is not a returning-call triple.
  uint8_t Jal[12] = {};
  ASSERT_TRUE(call_relax::writeE2JalSingleton(Jal, 12, /*RtEnc=*/15));
  EXPECT_FALSE(call_relax::isReturningCallRelaxTriple(Lui, Addi, Jal, 12));

  // LUI of a different temp than JALR.rs is not a returning-call triple.
  EXPECT_FALSE(call_relax::isReturningCallRelaxTriple(Mixed, Addi, JalrCall, 12));
}
