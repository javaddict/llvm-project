//===- HaydnMemoryCycleTest.cpp - per-class memory cycle tables -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pin First/LastMemoryCycle tables for Slot0_LS / Slot1_LD / Slot01_LD and the
// product vs opt-in accurate getMemoryLatency contract. Product path always
// returns latency 1; -haydn-accurate-memory-latency computes
// max(1, LastSrc-FirstDst+1) for memory itineraries only (nullopt otherwise).
// NatureDSP density A/B kept product latency-1 (accurate regressed packing);
// no densify invent; accurate flag stays opt-in default OFF.
//
//===----------------------------------------------------------------------===//

#include "HaydnInstrInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnBaseInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include "gtest/gtest.h"

#include <memory>

#define GET_INSTRINFO_ENUM
#include "HaydnGenInstrInfo.inc"
#define GET_INSTRINFO_SCHED_ENUM
#include "HaydnGenInstrInfo.inc"

extern "C" void LLVMInitializeHaydnTargetInfo();
extern "C" void LLVMInitializeHaydnTarget();
extern "C" void LLVMInitializeHaydnTargetMC();

using namespace llvm;

namespace {

/// Toggle -haydn-accurate-memory-latency registered in HaydnInstrInfo.cpp.
static void setAccurateMemoryLatency(bool Enable) {
  auto &Opts = cl::getRegisteredOptions();
  auto It = Opts.find("haydn-accurate-memory-latency");
  ASSERT_NE(It, Opts.end()) << "-haydn-accurate-memory-latency not registered";
  auto *Opt = static_cast<cl::opt<bool> *>(It->second);
  ASSERT_NE(Opt, nullptr);
  Opt->setValue(Enable);
}

class HaydnMemoryCycleTest : public testing::Test {
protected:
  std::unique_ptr<HaydnTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<HaydnSubtarget> ST;
  std::unique_ptr<MachineFunction> MF;

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

    Ctx = std::make_unique<LLVMContext>();
    M = std::make_unique<Module>("HaydnMemoryCycle", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);

    // Product default between tests.
    setAccurateMemoryLatency(false);
  }

  void TearDown() override { setAccurateMemoryLatency(false); }

  const HaydnInstrInfo &TII() const { return *ST->getInstrInfo(); }
};

// Product default for -haydn-accurate-memory-latency is OFF.
TEST_F(HaydnMemoryCycleTest, AccurateFlagDefaultOff) {
  auto &Opts = cl::getRegisteredOptions();
  auto It = Opts.find("haydn-accurate-memory-latency");
  ASSERT_NE(It, Opts.end());
  auto *Opt = static_cast<cl::opt<bool> *>(It->second);
  ASSERT_NE(Opt, nullptr);
  EXPECT_FALSE(Opt->getValue());
}

// Memory itineraries: first=0, last=1 (OperandCycles [2] / LoadLatency=2).
TEST_F(HaydnMemoryCycleTest, FirstLastMemoryCycleTables) {
  using namespace Haydn::Sched;
  const HaydnInstrInfo &II = TII();

  for (unsigned SC : {Slot0_LS, Slot1_LD, Slot01_LD}) {
    auto First = II.getFirstMemoryCycle(SC);
    auto Last = II.getLastMemoryCycle(SC);
    ASSERT_TRUE(First.has_value()) << "SC=" << SC;
    ASSERT_TRUE(Last.has_value()) << "SC=" << SC;
    EXPECT_EQ(*First, 0);
    EXPECT_EQ(*Last, 1);
  }

  // Non-memory classes never invent a memory cycle.
  for (unsigned SC : {NoInstrModel, Slot012_ALU, Slot0_ALU, Slot1_ALU,
                      Slot2_ALU, Slot12_ALU}) {
    EXPECT_FALSE(II.getFirstMemoryCycle(SC).has_value()) << "SC=" << SC;
    EXPECT_FALSE(II.getLastMemoryCycle(SC).has_value()) << "SC=" << SC;
  }

  EXPECT_EQ(II.getMinFirstMemoryCycle(), 0);
  EXPECT_EQ(II.getMaxFirstMemoryCycle(), 0);
  EXPECT_EQ(II.getMinLastMemoryCycle(), 1);
  EXPECT_EQ(II.getMaxLastMemoryCycle(), 1);
}

// Opcode → itinerary wiring that MemoryEdges actually reads via getSchedClass.
// Stores share Slot0_LS; plain LD32/LD64 use Slot01_LD (dual-slot load choice);
// slot-1 promoted loads use Slot1_LD.
TEST_F(HaydnMemoryCycleTest, OpcodeSchedClassesAreMemoryItineraries) {
  using namespace Haydn::Sched;
  const HaydnInstrInfo &II = TII();

  EXPECT_EQ(II.get(Haydn::ST32).getSchedClass(), static_cast<unsigned>(Slot0_LS));
  EXPECT_EQ(II.get(Haydn::LD32).getSchedClass(), static_cast<unsigned>(Slot01_LD));
  EXPECT_EQ(II.get(Haydn::S_LW_WITH_IMM_E2_E1_LOAD1_RI6).getSchedClass(),
            static_cast<unsigned>(Slot1_LD));
  EXPECT_EQ(II.get(Haydn::LD64).getSchedClass(), static_cast<unsigned>(Slot01_LD));
}

// Product path: class-agnostic latency 1 for every src/dst pair.
TEST_F(HaydnMemoryCycleTest, ProductGetMemoryLatencyAlwaysOne) {
  using namespace Haydn::Sched;
  const HaydnInstrInfo &II = TII();
  setAccurateMemoryLatency(false);

  const unsigned Mem[] = {Slot0_LS, Slot1_LD, Slot01_LD};
  const unsigned NonMem[] = {Slot0_ALU, Slot012_ALU};

  for (unsigned Src : Mem) {
    for (unsigned Dst : Mem) {
      auto Lat = II.getMemoryLatency(Src, Dst);
      ASSERT_TRUE(Lat.has_value());
      EXPECT_EQ(*Lat, 1) << "product mem→mem SC " << Src << "→" << Dst;
    }
    for (unsigned Dst : NonMem) {
      auto Lat = II.getMemoryLatency(Src, Dst);
      ASSERT_TRUE(Lat.has_value());
      EXPECT_EQ(*Lat, 1) << "product mem→nonmem still 1";
    }
  }
  // Cross-class store/load itineraries used by dual-load packing.
  auto Cross = II.getMemoryLatency(Slot0_LS, Slot1_LD);
  ASSERT_TRUE(Cross.has_value());
  EXPECT_EQ(*Cross, 1);
}

// Opt-in accurate path: max(1, Last-First+1)=2 for memory pairs; nullopt else.
TEST_F(HaydnMemoryCycleTest, AccurateGetMemoryLatencyFromTables) {
  using namespace Haydn::Sched;
  const HaydnInstrInfo &II = TII();
  setAccurateMemoryLatency(true);

  const unsigned Mem[] = {Slot0_LS, Slot1_LD, Slot01_LD};
  for (unsigned Src : Mem) {
    for (unsigned Dst : Mem) {
      auto Lat = II.getMemoryLatency(Src, Dst);
      ASSERT_TRUE(Lat.has_value()) << "SC " << Src << "→" << Dst;
      EXPECT_EQ(*Lat, 2) << "accurate mem→mem SC " << Src << "→" << Dst;
    }
  }

  // Unknown cycle → nullopt (MemoryEdges keeps its local default of 1).
  EXPECT_FALSE(II.getMemoryLatency(Slot0_LS, Slot0_ALU).has_value());
  EXPECT_FALSE(II.getMemoryLatency(Slot0_ALU, Slot0_LS).has_value());
  EXPECT_FALSE(II.getMemoryLatency(Slot012_ALU, Slot012_ALU).has_value());
}

} // namespace
