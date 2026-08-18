//===- HaydnRegisterInfoTest.cpp - TRI soft RA hint order pins ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pin Haydn getRegAllocationHints soft physreg ordering:
//   1. base copy/coalesce hints (never diluted by later tiers)
//   2. optional compact-subset (GPR32Lo / low-DR) under -haydn-ra-compact-hints
//   3. existing caller-saved preference
//
// Default compact flag is OFF (metrics gate). Dual-run spill/reload/Hit/
// post-RA multi-MI attribution lives in lit (regalloc-compact-hints.ll);
// hard-bundle membership under compact-hints=true is in
// format-bundle-through-ra.mir. Unit dual-run pins compact ON/OFF membership
// independence (classification + hints stay inside Order). No hard RC
// demotion: Allocation Order stays the full bank; compact is preference only.
//
//===----------------------------------------------------------------------===//

#include "Haydn.h"
#include "HaydnRegisterInfo.h"
#include "HaydnSubtarget.h"
#include "HaydnTargetMachine.h"
#include "MCTargetDesc/HaydnMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
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

/// Toggle the metrics-gated compact soft-order flag registered in
/// HaydnRegisterInfo.cpp (-haydn-ra-compact-hints, default false).
static void setHaydnRACompactHints(bool Enable) {
  auto &Opts = cl::getRegisteredOptions();
  auto It = Opts.find("haydn-ra-compact-hints");
  ASSERT_NE(It, Opts.end()) << "-haydn-ra-compact-hints not registered";
  auto *Opt = static_cast<cl::opt<bool> *>(It->second);
  ASSERT_NE(Opt, nullptr);
  Opt->setValue(Enable);
}

class HaydnRegisterInfoTest : public testing::Test {
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
    M = std::make_unique<Module>("HaydnRAHints", *Ctx);
    M->setDataLayout(TM->createDataLayout());
    auto *FTy = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FTy, GlobalValue::ExternalLinkage, "test", *M);

    MMI = std::make_unique<MachineModuleInfo>(TM.get());
    ST = std::make_unique<HaydnSubtarget>(TM->getTargetTriple(), "generic",
                                          "generic", "", *TM);
    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, MMI->getContext(),
                                           /*FunctionNum=*/0);
    // Reserved set (R0 soft-zero, SP, LR, …) must be frozen so isReserved
    // matches real RA.
    MF->getRegInfo().freezeReservedRegs();

    // Restore default metrics gate between tests.
    setHaydnRACompactHints(false);
  }

  void TearDown() override { setHaydnRACompactHints(false); }

  const HaydnRegisterInfo &TRI() const { return *ST->getRegisterInfo(); }

  MachineRegisterInfo &MRI() { return MF->getRegInfo(); }

  /// First index of PhysReg in Hints, or -1.
  static int hintIndex(ArrayRef<MCPhysReg> Hints, MCPhysReg PhysReg) {
    for (unsigned I = 0, E = Hints.size(); I != E; ++I)
      if (Hints[I] == PhysReg)
        return static_cast<int>(I);
    return -1;
  }
};

// Product default for -haydn-ra-compact-hints is false (metrics gate).
TEST_F(HaydnRegisterInfoTest, CompactHintsDefaultOff) {
  auto &Opts = cl::getRegisteredOptions();
  auto It = Opts.find("haydn-ra-compact-hints");
  ASSERT_NE(It, Opts.end());
  auto *Opt = static_cast<cl::opt<bool> *>(It->second);
  ASSERT_NE(Opt, nullptr);
  // SetUp restored default; pin product init value stays false.
  EXPECT_FALSE(Opt->getValue());
}

// Compact-subset membership: soft preference set only (not a hard RC).
TEST_F(HaydnRegisterInfoTest, CompactSubsetMembership) {
  const HaydnRegisterInfo &RI = TRI();
  EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::R0));
  EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::R1));
  EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::R7));
  EXPECT_FALSE(isHaydnCompactSubsetPhysReg(RI, Haydn::R8));
  EXPECT_FALSE(isHaydnCompactSubsetPhysReg(RI, Haydn::R12));
  EXPECT_FALSE(isHaydnCompactSubsetPhysReg(RI, Haydn::R13));

  EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::D0));
  EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::D7));
  EXPECT_FALSE(isHaydnCompactSubsetPhysReg(RI, Haydn::D8));
  EXPECT_FALSE(isHaydnCompactSubsetPhysReg(RI, Haydn::D15));
}

// Flag OFF (default): only caller-saved soft order after empty base.
// Compact-subset must not appear ahead of non-compact caller-saved (R12).
TEST_F(HaydnRegisterInfoTest, HintOrderFlagOffCallerSavedOnly) {
  setHaydnRACompactHints(false);

  Register VirtReg = MRI().createVirtualRegister(&Haydn::GPR32RegClass);
  // Deliberately put non-compact caller-saved R12 before compact R3 so that
  // any compact-first reorder would be visible.
  const MCPhysReg Order[] = {Haydn::R8, Haydn::R12, Haydn::R3, Haydn::R9,
                             Haydn::R1};

  SmallVector<MCPhysReg, 8> Hints;
  TRI().getRegAllocationHints(VirtReg, Order, Hints, *MF, nullptr, nullptr);

  // R8/R9 are CSR — not in the caller-saved soft tier.
  EXPECT_EQ(hintIndex(Hints, Haydn::R8), -1);
  EXPECT_EQ(hintIndex(Hints, Haydn::R9), -1);

  // Caller-saved in Order encounter order: R12, R3, R1.
  ASSERT_GE(Hints.size(), 3u);
  EXPECT_EQ(Hints[0], Haydn::R12);
  EXPECT_EQ(Hints[1], Haydn::R3);
  EXPECT_EQ(Hints[2], Haydn::R1);
}

// Flag ON: compact-subset tier before caller-saved. R12 (caller-saved, not
// compact) must sort after every compact reg that was added from Order.
TEST_F(HaydnRegisterInfoTest, HintOrderFlagOnCompactBeforeCallerSaved) {
  setHaydnRACompactHints(true);

  Register VirtReg = MRI().createVirtualRegister(&Haydn::GPR32RegClass);
  const MCPhysReg Order[] = {Haydn::R8, Haydn::R12, Haydn::R3, Haydn::R9,
                             Haydn::R1};

  SmallVector<MCPhysReg, 8> Hints;
  TRI().getRegAllocationHints(VirtReg, Order, Hints, *MF, nullptr, nullptr);

  int IdxR3 = hintIndex(Hints, Haydn::R3);
  int IdxR1 = hintIndex(Hints, Haydn::R1);
  int IdxR12 = hintIndex(Hints, Haydn::R12);
  ASSERT_GE(IdxR3, 0);
  ASSERT_GE(IdxR1, 0);
  ASSERT_GE(IdxR12, 0);

  // Compact tier walks Order: R3 then R1, then CSR-preference adds R12.
  EXPECT_LT(IdxR3, IdxR1);
  EXPECT_LT(IdxR1, IdxR12);
  EXPECT_EQ(Hints[0], Haydn::R3);
  EXPECT_EQ(Hints[1], Haydn::R1);
  EXPECT_EQ(Hints[2], Haydn::R12);

  // High CSR bank stays unhinted by both soft tiers.
  EXPECT_EQ(hintIndex(Hints, Haydn::R8), -1);
  EXPECT_EQ(hintIndex(Hints, Haydn::R9), -1);
}

// Compact ON but Order has no compact-subset member → Miss path (no compact
// physreg pushed). Caller-saved may still add R12.
TEST_F(HaydnRegisterInfoTest, CompactMissWhenOrderHasNoCompactMember) {
  setHaydnRACompactHints(true);

  Register VirtReg = MRI().createVirtualRegister(&Haydn::GPR32RegClass);
  // R8/R9 CSR high; R12 caller-saved non-compact — zero compact adds.
  const MCPhysReg Order[] = {Haydn::R8, Haydn::R9, Haydn::R12};

  SmallVector<MCPhysReg, 8> Hints;
  TRI().getRegAllocationHints(VirtReg, Order, Hints, *MF, nullptr, nullptr);

  EXPECT_EQ(hintIndex(Hints, Haydn::R8), -1);
  EXPECT_EQ(hintIndex(Hints, Haydn::R9), -1);
  ASSERT_EQ(Hints.size(), 1u);
  EXPECT_EQ(Hints[0], Haydn::R12);
}

// Base copy/coalesce wins: non-empty MRI base hints must not be diluted by
// compact or caller-saved tiers (early return after TargetRegisterInfo base).
TEST_F(HaydnRegisterInfoTest, BaseCopyHintBeatsCompactAndCallerSaved) {
  setHaydnRACompactHints(true);

  Register VirtReg = MRI().createVirtualRegister(&Haydn::GPR32RegClass);
  // Target-independent MRI hint (same channel as copy/coalesce preference).
  // Prefer CSR R8 so any compact leak would be obvious.
  MRI().setSimpleHint(VirtReg, Haydn::R8);

  const MCPhysReg Order[] = {Haydn::R1, Haydn::R3, Haydn::R8, Haydn::R12};

  SmallVector<MCPhysReg, 8> Hints;
  TRI().getRegAllocationHints(VirtReg, Order, Hints, *MF, nullptr, nullptr);

  ASSERT_FALSE(Hints.empty());
  // Base hint is R8; soft tiers must not run after base is non-empty.
  EXPECT_EQ(Hints[0], Haydn::R8);
  // No compact/caller-saved append when base already non-empty.
  EXPECT_EQ(Hints.size(), 1u);
}

// DR64 bank: compact low-DR (D0–D7) before high CSR DR when flag ON.
TEST_F(HaydnRegisterInfoTest, DR64CompactBeforeHighDRWhenEnabled) {
  setHaydnRACompactHints(true);

  Register VirtReg = MRI().createVirtualRegister(&Haydn::DR64RegClass);
  // High DR first in Order so compact reorder is observable.
  const MCPhysReg Order[] = {Haydn::D8, Haydn::D0, Haydn::D9, Haydn::D3};

  SmallVector<MCPhysReg, 8> Hints;
  TRI().getRegAllocationHints(VirtReg, Order, Hints, *MF, nullptr, nullptr);

  int IdxD0 = hintIndex(Hints, Haydn::D0);
  int IdxD3 = hintIndex(Hints, Haydn::D3);
  ASSERT_GE(IdxD0, 0);
  ASSERT_GE(IdxD3, 0);
  EXPECT_LT(IdxD0, IdxD3);
  EXPECT_EQ(Hints[0], Haydn::D0);
  EXPECT_EQ(Hints[1], Haydn::D3);
  // D8/D9 are CSR DR — not compact and not caller-saved soft tier.
  EXPECT_EQ(hintIndex(Hints, Haydn::D8), -1);
  EXPECT_EQ(hintIndex(Hints, Haydn::D9), -1);
}

// Flag OFF for DR64: low-DR still appear via caller-saved tier (D0–D7), but
// the compact-only Hit/Miss path is inactive (lit owns counter pin).
TEST_F(HaydnRegisterInfoTest, DR64FlagOffStillCallerSavedLowDR) {
  setHaydnRACompactHints(false);

  Register VirtReg = MRI().createVirtualRegister(&Haydn::DR64RegClass);
  const MCPhysReg Order[] = {Haydn::D8, Haydn::D0, Haydn::D9, Haydn::D3};

  SmallVector<MCPhysReg, 8> Hints;
  TRI().getRegAllocationHints(VirtReg, Order, Hints, *MF, nullptr, nullptr);

  // Caller-saved walk of Order: D0 then D3 (D8/D9 skipped).
  ASSERT_EQ(Hints.size(), 2u);
  EXPECT_EQ(Hints[0], Haydn::D0);
  EXPECT_EQ(Hints[1], Haydn::D3);
}

// Dual-run compact ON/OFF: soft preference only. Compact-subset classification
// and full-bank RC membership are independent of the flag; hints never invent
// physregs outside AllocationOrder. Soft order cannot demote hard membership
// (lit owns pipeline unbundle pin in format-bundle-through-ra.mir).
TEST_F(HaydnRegisterInfoTest, CompactOnOffMembershipIndependence) {
  const HaydnRegisterInfo &RI = TRI();
  const MCPhysReg Order[] = {Haydn::R8, Haydn::R12, Haydn::R3, Haydn::R9,
                             Haydn::R1};

  auto inOrder = [&](MCPhysReg PhysReg) {
    for (MCPhysReg O : Order)
      if (O == PhysReg)
        return true;
    return false;
  };

  // Compact-subset classification is pure TRI (flag-independent).
  for (bool Enable : {false, true}) {
    setHaydnRACompactHints(Enable);
    EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::R1));
    EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::R3));
    EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::R7));
    EXPECT_FALSE(isHaydnCompactSubsetPhysReg(RI, Haydn::R8));
    EXPECT_FALSE(isHaydnCompactSubsetPhysReg(RI, Haydn::R12));
    EXPECT_TRUE(isHaydnCompactSubsetPhysReg(RI, Haydn::D0));
    EXPECT_FALSE(isHaydnCompactSubsetPhysReg(RI, Haydn::D8));
  }

  // Full GPR32 / DR64 banks still admit non-compact high regs (no hard RC
  // demotion under either flag).
  EXPECT_TRUE(Haydn::GPR32RegClass.contains(Haydn::R1));
  EXPECT_TRUE(Haydn::GPR32RegClass.contains(Haydn::R8));
  EXPECT_TRUE(Haydn::GPR32RegClass.contains(Haydn::R12));
  EXPECT_TRUE(Haydn::DR64RegClass.contains(Haydn::D0));
  EXPECT_TRUE(Haydn::DR64RegClass.contains(Haydn::D8));

  Register VirtReg = MRI().createVirtualRegister(&Haydn::GPR32RegClass);

  auto collectHints = [&](bool Enable) {
    setHaydnRACompactHints(Enable);
    SmallVector<MCPhysReg, 8> Hints;
    TRI().getRegAllocationHints(VirtReg, Order, Hints, *MF, nullptr, nullptr);
    return Hints;
  };

  SmallVector<MCPhysReg, 8> Off = collectHints(false);
  SmallVector<MCPhysReg, 8> On = collectHints(true);

  // Every soft hint under either flag is a member of Order — preference only.
  for (MCPhysReg R : Off)
    EXPECT_TRUE(inOrder(R)) << "OFF invented physreg outside Order";
  for (MCPhysReg R : On)
    EXPECT_TRUE(inOrder(R)) << "ON invented physreg outside Order";

  // Soft order differs: OFF caller-saved-first (R12); ON compact-first (R3).
  // Membership of the soft tier is a reorder of the same Order set — not an
  // unbundle / demotion of hard AllocationOrder availability.
  ASSERT_GE(Off.size(), 3u);
  ASSERT_GE(On.size(), 3u);
  EXPECT_EQ(Off[0], Haydn::R12);
  EXPECT_EQ(Off[1], Haydn::R3);
  EXPECT_EQ(Off[2], Haydn::R1);
  EXPECT_EQ(On[0], Haydn::R3);
  EXPECT_EQ(On[1], Haydn::R1);
  EXPECT_EQ(On[2], Haydn::R12);

  // High CSR bank stays unhinted by both soft tiers under either flag.
  EXPECT_EQ(hintIndex(Off, Haydn::R8), -1);
  EXPECT_EQ(hintIndex(Off, Haydn::R9), -1);
  EXPECT_EQ(hintIndex(On, Haydn::R8), -1);
  EXPECT_EQ(hintIndex(On, Haydn::R9), -1);
}

// Soft-zero / stack / link reserved; R12 stays allocatable. DWARF numbers
// are the compiler manifest consumed by LLDB ABISysV_haydn.
TEST_F(HaydnRegisterInfoTest, ReservedSoftZeroAndDwarfManifest) {
  const HaydnRegisterInfo &RI = TRI();
  const BitVector Reserved = RI.getReservedRegs(*MF);

  EXPECT_TRUE(Reserved.test(Haydn::R0));
  EXPECT_FALSE(Reserved.test(Haydn::R1));
  EXPECT_FALSE(Reserved.test(Haydn::R12));
  EXPECT_TRUE(Reserved.test(Haydn::R13));
  EXPECT_FALSE(Reserved.test(Haydn::R14));
  EXPECT_TRUE(Reserved.test(Haydn::R15));
  EXPECT_TRUE(Reserved.test(Haydn::SFR));
  EXPECT_TRUE(Reserved.test(Haydn::CBR0));
  EXPECT_TRUE(Reserved.test(Haydn::CBR1));

  EXPECT_EQ(RI.getDwarfRegNum(Haydn::R0, /*isEH=*/false), 0);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::R1, /*isEH=*/false), 1);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::R15, /*isEH=*/false), 15);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::D0, /*isEH=*/false), 16);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::D15, /*isEH=*/false), 31);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::AR0, /*isEH=*/false), 32);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::AR1, /*isEH=*/false), 33);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::SFR, /*isEH=*/false), 36);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::CSR, /*isEH=*/false), 37);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::CBR0, /*isEH=*/false), 38);
  EXPECT_EQ(RI.getDwarfRegNum(Haydn::CBR1, /*isEH=*/false), 39);

  EXPECT_TRUE(RI.isInlineAsmReadOnlyReg(*MF, Haydn::R0));
  EXPECT_TRUE(RI.isInlineAsmReadOnlyReg(*MF, Haydn::R13));
  EXPECT_FALSE(RI.isInlineAsmReadOnlyReg(*MF, Haydn::R12));
}

} // namespace
