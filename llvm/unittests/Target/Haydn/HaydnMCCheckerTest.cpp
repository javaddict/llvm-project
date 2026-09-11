//===- HaydnMCCheckerTest.cpp - parse-time RF-port ceilings -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// D1.85(a): haydnCheckParsedBundleRegs DR read ceiling is golden 8R
// (HAYDN_DR_READ_PORTS), not the retired conservative 7R.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HaydnMCChecker.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

#include <memory>

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTargetMC();

using namespace llvm;

namespace {

class HaydnMCCheckerTest : public testing::Test {
protected:
  const MCInstrInfo *MII = nullptr;
  std::unique_ptr<MCRegisterInfo> MRI;

  static void SetUpTestSuite() {
    LLVMInitializeHaydnTargetInfo();
    LLVMInitializeHaydnTargetMC();
  }

  void SetUp() override {
    MII = &getHaydnSharedMCInstrInfo();
    std::string Error;
    Triple TT("haydn-unknown-elf");
    const Target *T = TargetRegistry::lookupTarget(TT, Error);
    ASSERT_NE(T, nullptr) << Error;
    MRI.reset(T->createMCRegInfo(TT.str()));
    ASSERT_NE(MRI, nullptr);
  }

  static MCInst add64(unsigned Rd, unsigned Rs1, unsigned Rs2,
                      ArrayRef<unsigned> ExtraReads) {
    MCInst I;
    I.setOpcode(Haydn::ADD64);
    I.addOperand(MCOperand::createReg(Rd));
    I.addOperand(MCOperand::createReg(Rs1));
    I.addOperand(MCOperand::createReg(Rs2));
    for (unsigned R : ExtraReads)
      I.addOperand(MCOperand::createReg(R));
    return I;
  }
};

TEST_F(HaydnMCCheckerTest, DRReadCeilingAdmits8R) {
  // Extra MC operands are extra DR reads: native ADD64 is 2R, occupancy-3
  // maxes at 6R, so the 8-vs-9 discriminator cannot be a legal 3-wide pack.
  // 4R + 4R = 8R / 2W. Retired `DRR > 7` refused this shape.
  MCInst A = add64(Haydn::D0, Haydn::D1, Haydn::D2, {Haydn::D3, Haydn::D4});
  MCInst B = add64(Haydn::D5, Haydn::D6, Haydn::D7, {Haydn::D8, Haydn::D9});
  const MCInst *Reals[] = {&A, &B};
  auto Err = haydnCheckParsedBundleRegs(Reals, *MII, MRI.get());
  EXPECT_FALSE(Err.has_value()) << (Err ? *Err : "");
}

TEST_F(HaydnMCCheckerTest, DRReadCeilingRefuses9R) {
  MCInst A = add64(Haydn::D0, Haydn::D1, Haydn::D2, {Haydn::D3, Haydn::D4});
  MCInst B =
      add64(Haydn::D5, Haydn::D6, Haydn::D7, {Haydn::D8, Haydn::D9, Haydn::D10});
  const MCInst *Reals[] = {&A, &B};
  auto Err = haydnCheckParsedBundleRegs(Reals, *MII, MRI.get());
  ASSERT_TRUE(Err.has_value());
  EXPECT_NE(Err->find("cycle RF port demand"), std::string::npos) << *Err;
  EXPECT_NE(Err->find("DR 8R/3W"), std::string::npos) << *Err;
}

} // namespace
