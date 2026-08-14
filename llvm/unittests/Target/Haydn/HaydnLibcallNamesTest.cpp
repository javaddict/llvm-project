//===- HaydnLibcallNamesTest.cpp - one libcall-name table -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lock the single Haydn libcall-name table:
//   HaydnSubtarget::initLibcallLoweringInfo is the only registration site.
//   TargetLoweringBase constructs TLI.Libcalls from that hook
//   (TargetLoweringBase.cpp, Libcalls(RuntimeLibcallInfo, STI)).
//   GISel createLibcall reads TLI.getLibcallName
//   (LegalizerHelper.cpp). IR-level LibcallLoweringInfo calls the same hook
//   (LibcallLoweringInfo.cpp). RISCV/AArch64 do not override the hook — they
//   are default RuntimeLibcalls.td arches; Haydn is not, so it follows the
//   ARM Subtarget::initLibcallLoweringInfo table shape.
//
// Both consumers (TLI used by GISel, and an independently constructed
// LibcallLoweringInfo) must resolve identical names. A second setLibcallImpl
// list in HaydnTargetLowering, or hardcoded "__addsf3"/"fmodf" strings in
// the legalizer, would desync them or bypass the table.
//
//===----------------------------------------------------------------------===//

#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "llvm/CodeGen/LibcallLoweringInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

#include "gtest/gtest.h"

#include <memory>

using namespace llvm;

namespace {

static StringRef libcallName(const char *Name) {
  return Name ? StringRef(Name) : StringRef();
}

class HaydnLibcallNamesTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<RTLIB::RuntimeLibcallsInfo> RTLCI;
  std::unique_ptr<LibcallLoweringInfo> IRLibcalls;

  static void SetUpTestSuite() {
    LLVMInitializeHaydnTargetInfo();
    LLVMInitializeHaydnTarget();
    LLVMInitializeHaydnTargetMC();
  }

  void SetUp() override {
    std::string Error;
    Triple TT("haydn-unknown-elf");
    const Target *TheTarget = TargetRegistry::lookupTarget(TT, Error);
    ASSERT_NE(TheTarget, nullptr) << Error;

    TargetOptions Options;
    TM.reset(static_cast<HaydnTargetMachine *>(TheTarget->createTargetMachine(
        TT, "generic", "", Options, std::nullopt, std::nullopt,
        CodeGenOptLevel::Default)));
    ASSERT_NE(TM, nullptr);

    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    ASSERT_NE(ST, nullptr);
    ASSERT_NE(ST->getTargetLowering(), nullptr);

    // Second consumer: reconstruct LibcallLoweringInfo from the same
    // Subtarget hook the TLI constructor already ran. Matching TM Options
    // so default-available impls cannot explain a name mismatch.
    RTLCI = std::make_unique<RTLIB::RuntimeLibcallsInfo>(
        TM->getTargetTriple(), TM->Options.ExceptionModel,
        TM->Options.FloatABIType, TM->Options.EABIVersion,
        TM->Options.MCOptions.getABIName(), TM->Options.VecLib);
    IRLibcalls = std::make_unique<LibcallLoweringInfo>(*RTLCI, *ST);
  }

  const TargetLowering &TLI() const { return *ST->getTargetLowering(); }

  const LibcallLoweringInfo &IRInfo() const { return *IRLibcalls; }

  void expectBothMatch(RTLIB::Libcall LC, RTLIB::LibcallImpl Impl) const {
    EXPECT_EQ(TLI().getLibcallImpl(LC), Impl)
        << "TLI impl mismatch for libcall " << static_cast<int>(LC);
    EXPECT_EQ(IRInfo().getLibcallImpl(LC), Impl)
        << "IR LibcallLoweringInfo impl mismatch for libcall "
        << static_cast<int>(LC);
    const StringRef Want =
        RTLIB::RuntimeLibcallsInfo::getLibcallImplName(Impl);
    EXPECT_EQ(libcallName(TLI().getLibcallName(LC)), Want)
        << "TLI name mismatch for libcall " << static_cast<int>(LC);
    EXPECT_EQ(libcallName(IRInfo().getLibcallName(LC)), Want)
        << "IR name mismatch for libcall " << static_cast<int>(LC);
  }
};

// TLI (GISel createLibcall) and a fresh LibcallLoweringInfo must not drift
// for any RTLIB entry. Catches a second setLibcallImpl table on TLI.
TEST_F(HaydnLibcallNamesTest, TLIAndIRLoweringResolveIdenticalNames) {
  for (RTLIB::Libcall LC : RTLIB::libcalls()) {
    EXPECT_EQ(TLI().getLibcallImpl(LC), IRInfo().getLibcallImpl(LC))
        << "impl split-brain at libcall " << static_cast<int>(LC);
    EXPECT_EQ(libcallName(TLI().getLibcallName(LC)),
              libcallName(IRInfo().getLibcallName(LC)))
        << "name split-brain at libcall " << static_cast<int>(LC);
  }
}

// Soft-float / libm rows that used to live in a second ISelLowering list.
TEST_F(HaydnLibcallNamesTest, SoftFloatW6NamesMatchSubtargetRegistrations) {
  expectBothMatch(RTLIB::ADD_F32, RTLIB::impl___addsf3);
  expectBothMatch(RTLIB::SUB_F32, RTLIB::impl___subsf3);
  expectBothMatch(RTLIB::MUL_F32, RTLIB::impl___mulsf3);
  expectBothMatch(RTLIB::DIV_F32, RTLIB::impl___divsf3);
  expectBothMatch(RTLIB::ADD_F64, RTLIB::impl___adddf3);
  expectBothMatch(RTLIB::SUB_F64, RTLIB::impl___subdf3);
  expectBothMatch(RTLIB::MUL_F64, RTLIB::impl___muldf3);
  expectBothMatch(RTLIB::DIV_F64, RTLIB::impl___divdf3);

  expectBothMatch(RTLIB::REM_F32, RTLIB::impl_fmodf);
  expectBothMatch(RTLIB::REM_F64, RTLIB::impl_fmod);
  expectBothMatch(RTLIB::SQRT_F32, RTLIB::impl_sqrtf);
  expectBothMatch(RTLIB::SQRT_F64, RTLIB::impl_sqrt);
  expectBothMatch(RTLIB::FMA_F32, RTLIB::impl_fmaf);
  expectBothMatch(RTLIB::FMA_F64, RTLIB::impl_fma);

  expectBothMatch(RTLIB::FPEXT_F32_F64, RTLIB::impl___extendsfdf2);
  expectBothMatch(RTLIB::FPROUND_F64_F32, RTLIB::impl___truncdfsf2);
  expectBothMatch(RTLIB::FPEXT_F16_F32, RTLIB::impl___extendhfsf2);
  expectBothMatch(RTLIB::FPEXT_F16_F64, RTLIB::impl___extendhfdf2);
  expectBothMatch(RTLIB::FPROUND_F32_F16, RTLIB::impl___truncsfhf2);
  expectBothMatch(RTLIB::FPROUND_F64_F16, RTLIB::impl___truncdfhf2);
}

} // namespace
